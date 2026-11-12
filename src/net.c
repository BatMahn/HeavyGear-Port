#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSESOCK closesocket
static void sock_nonblock(SOCKET s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
static void sock_nonblock(SOCKET s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

enum { T_JOIN = 1, T_WELCOME, T_ROSTER, T_DATA, T_RDATA, T_ACK, T_BYE };
#define HDR 15        /* "HGN1", type, from u16, to u16, seq u32, size u16 */
#define RESEND_MS 200
#define MAXQ 512      /* reliable messages in flight */
#define INQ 256       /* messages waiting for net_recv */

typedef struct {
    int  used, id, slot;
    char name[17];
    struct sockaddr_in addr;
    unsigned seq_out;                 /* next reliable sequence to this peer */
    unsigned seen[64];                /* reliable sequences delivered from it (a ring) */
    int  nseen;
} peer;

typedef struct { int used, to; unsigned seq, last; int len; unsigned char d[HDR + NET_MAX_MSG]; } pending;
typedef struct { int from, len; unsigned char d[NET_MAX_MSG]; } inmsg;

struct net {
    SOCKET s;
    int host, me, started, next_id;
    peer p[NET_MAX_PLAYERS + 1];      /* the host's: the joiners (p[0] unused); a joiner's: p[0] the host */
    net_player roster[NET_MAX_PLAYERS];
    int nroster;
    pending q[MAXQ];
    inmsg in[INQ];
    int in_head, in_n;
    unsigned join_sent;
    char name[17];
    char sname[33];                   /* the session's (game's) name, the host's */
    int  max;                         /* the most players the host takes (B49's count) */
};

unsigned net_ms(void)
{
#ifdef _WIN32
    return (unsigned)GetTickCount();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
#endif
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, unsigned v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned get32(const unsigned char *p) { return get16(p) | get16(p + 2) << 16; }

static int sock_open(int port, char *err, int errlen)
{
#ifdef _WIN32
    static int wsa;
    if (!wsa) { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d)) { snprintf(err, errlen, "WSAStartup"); return -1; } wsa = 1; }
#endif
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) { snprintf(err, errlen, "socket"); return -1; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { snprintf(err, errlen, "bind port %d", port); CLOSESOCK(s); return -1; }
    sock_nonblock(s);
    return (int)s;
}

static void raw_send(net *n, const struct sockaddr_in *to, const unsigned char *d, int len)
{
    sendto(n->s, (const char *)d, len, 0, (const struct sockaddr *)to, sizeof *to);
}

static peer *peer_by_id(net *n, int id)
{
    for (int i = 0; i <= NET_MAX_PLAYERS; i++) if (n->p[i].used && n->p[i].id == id) return &n->p[i];
    return NULL;
}

static peer *peer_by_addr(net *n, const struct sockaddr_in *a)
{
    for (int i = 0; i <= NET_MAX_PLAYERS; i++)
        if (n->p[i].used && n->p[i].addr.sin_addr.s_addr == a->sin_addr.s_addr && n->p[i].addr.sin_port == a->sin_port)
            return &n->p[i];
    return NULL;
}

static int frame(unsigned char *d, int type, int from, int to, unsigned seq, const void *buf, int len)
{
    memcpy(d, "HGN1", 4);
    d[4] = (unsigned char)type;
    put16(d + 5, (unsigned)from);
    put16(d + 7, (unsigned)to);
    put32(d + 9, seq);
    put16(d + 13, (unsigned)len);
    if (len) memcpy(d + HDR, buf, (size_t)len);
    return HDR + len;
}

/* a message over one link (to a peer); reliable ones wait for the peer's ACK */
static void link_send(net *n, peer *pr, int type, int from, int to, int reliable, const void *buf, int len)
{
    unsigned char d[HDR + NET_MAX_MSG];
    if (len > NET_MAX_MSG) return;
    unsigned seq = reliable ? ++pr->seq_out : 0;
    int l = frame(d, reliable && type == T_DATA ? T_RDATA : type, from, to, seq, buf, len);
    raw_send(n, &pr->addr, d, l);
    if (!reliable) return;
    for (int i = 0; i < MAXQ; i++)
        if (!n->q[i].used) {
            n->q[i].used = 1;
            n->q[i].to = pr->id;
            n->q[i].seq = seq;
            n->q[i].last = net_ms();
            n->q[i].len = l;
            memcpy(n->q[i].d, d, (size_t)l);
            return;
        }
}

static void deliver(net *n, int from, const unsigned char *buf, int len)
{
    if (n->in_n >= INQ || len > NET_MAX_MSG) return;
    inmsg *m = &n->in[(n->in_head + n->in_n) % INQ];
    m->from = from;
    m->len = len;
    memcpy(m->d, buf, (size_t)len);
    n->in_n++;
}

static void roster_build(net *n)
{
    n->nroster = 0;
    for (int id = 1; id < 256 && n->nroster < NET_MAX_PLAYERS; id++) {
        const char *nm = NULL;
        if (id == n->me) nm = n->name;
        else { peer *pr = peer_by_id(n, id); if (pr) nm = pr->name; }
        if (!nm) continue;
        net_player *q = &n->roster[n->nroster];
        q->id = id;
        q->slot = n->nroster++;
        snprintf(q->name, sizeof q->name, "%s", nm);
    }
}

static void roster_send(net *n)
{
    unsigned char b[NET_MAX_MSG];
    int l = 2;
    b[0] = (unsigned char)n->nroster;
    b[1] = (unsigned char)n->started;
    for (int i = 0; i < n->nroster; i++) {
        put16(b + l, (unsigned)n->roster[i].id);
        memcpy(b + l + 2, n->roster[i].name, 16);
        l += 18;
    }
    for (int i = 1; i <= NET_MAX_PLAYERS; i++)
        if (n->p[i].used) link_send(n, &n->p[i], T_ROSTER, n->me, n->p[i].id, 1, b, l);
}

static net *net_new(int port, const char *name, char *err, int errlen)
{
    int s = sock_open(port, err, errlen);
    if (s < 0) return NULL;
    net *n = calloc(1, sizeof *n);
    n->s = (SOCKET)s;
    snprintf(n->name, sizeof n->name, "%s", name && name[0] ? name : "Player");
    return n;
}

net *net_host(int port, const char *name, char *err, int errlen)
{
    net *n = net_new(port ? port : NET_DEFAULT_PORT, name, err, errlen);
    if (!n) return NULL;
    n->host = 1;
    n->me = 1;
    n->next_id = 2;
    roster_build(n);
    return n;
}

net *net_join(const char *host, int port, const char *name, char *err, int errlen)
{
    net *n = net_new(0, name, err, errlen);
    if (!n) return NULL;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port ? port : NET_DEFAULT_PORT);
    if (getaddrinfo(host, ps, &hints, &res) || !res) { snprintf(err, errlen, "unknown host %s", host); net_close(n); return NULL; }
    memcpy(&n->p[0].addr, res->ai_addr, sizeof n->p[0].addr);
    freeaddrinfo(res);
    n->p[0].used = 1;
    n->p[0].id = 1;
    snprintf(n->p[0].name, sizeof n->p[0].name, "host");
    return n;
}

void net_close(net *n)
{
    if (!n) return;
    unsigned char d[HDR];
    for (int i = 0; i <= NET_MAX_PLAYERS; i++)
        if (n->p[i].used) { int l = frame(d, T_BYE, n->me, n->p[i].id, 0, NULL, 0); raw_send(n, &n->p[i].addr, d, l); }
    CLOSESOCK(n->s);
    free(n);
}

static int seen(peer *pr, unsigned seq)
{
    for (int i = 0; i < 64 && i < pr->nseen; i++) if (pr->seen[i] == seq) return 1;
    pr->seen[pr->nseen++ % 64] = seq;
    if (pr->nseen >= 128) pr->nseen = 64;
    return 0;
}

static void drop_peer(net *n, peer *pr)
{
    int id = pr->id;
    memset(pr, 0, sizeof *pr);
    for (int i = 0; i < MAXQ; i++) if (n->q[i].used && n->q[i].to == id) n->q[i].used = 0;
    if (n->host) { roster_build(n); roster_send(n); }
}

void net_poll(net *n)
{
    unsigned char d[HDR + NET_MAX_MSG + 64];
    struct sockaddr_in a;
    for (;;) {
        socklen_t al = sizeof a;
        int l = (int)recvfrom(n->s, (char *)d, sizeof d, 0, (struct sockaddr *)&a, &al);
        if (l >= 4 && !memcmp(d, "HGQ1", 4)) {   /* a lobby looking for sessions (net_find) */
            if (n->host && !n->started) {
                unsigned char r[48];
                memset(r, 0, sizeof r);
                memcpy(r, "HGR1", 4);
                r[4] = (unsigned char)n->nroster;
                r[5] = (unsigned char)(n->max ? n->max : NET_MAX_PLAYERS);
                memcpy(r + 6, n->sname[0] ? n->sname : n->name, 32);
                sendto(n->s, (const char *)r, 6 + 33, 0, (const struct sockaddr *)&a, sizeof a);
            }
            continue;
        }
        if (l < HDR || memcmp(d, "HGN1", 4)) { if (l < 0) break; continue; }
        int type = d[4], from = (int)get16(d + 5), to = (int)get16(d + 7), len = (int)get16(d + 13);
        unsigned seq = get32(d + 9);
        if (len > l - HDR || len > NET_MAX_MSG) continue;
        const unsigned char *pl = d + HDR;
        peer *pr = peer_by_addr(n, &a);
        if (type == T_JOIN) {
            if (!n->host) continue;
            if (!pr) {
                if (n->started || (n->max && n->nroster >= n->max)) continue;
                for (int i = 1; i <= NET_MAX_PLAYERS && !pr; i++)
                    if (!n->p[i].used) {
                        pr = &n->p[i];
                        memset(pr, 0, sizeof *pr);
                        pr->used = 1;
                        pr->id = n->next_id++;
                        pr->addr = a;
                        snprintf(pr->name, sizeof pr->name, "%.*s", len < 16 ? len : 16, (const char *)pl);
                    }
                if (!pr) continue;
                roster_build(n);
            }
            unsigned char w[4];
            put16(w, (unsigned)pr->id);
            int fl = frame(d, T_WELCOME, n->me, pr->id, 0, w, 2);
            raw_send(n, &a, d, fl);
            roster_send(n);
            continue;
        }
        if (!pr) continue;
        if (type == T_ACK) {
            for (int i = 0; i < MAXQ; i++)
                if (n->q[i].used && n->q[i].to == pr->id && n->q[i].seq == seq) n->q[i].used = 0;
            continue;
        }
        if (type == T_BYE) {
            if (n->host) drop_peer(n, pr);
            else n->started = -1;   /* the host left: the session is gone */
            continue;
        }
        if (seq) {   /* reliable: acknowledge, deliver once */
            unsigned char k[HDR];
            int kl = frame(k, T_ACK, n->me, from, seq, NULL, 0);
            raw_send(n, &a, k, kl);
            if (seen(pr, seq)) continue;
        }
        if (type == T_WELCOME) { n->me = (int)get16(pl); continue; }
        if (type == T_ROSTER) {
            n->nroster = 0;
            n->started = pl[1];
            for (int i = 0; i < pl[0] && i < NET_MAX_PLAYERS && 2 + i * 18 + 18 <= len; i++) {
                net_player *q = &n->roster[n->nroster];
                q->id = (int)get16(pl + 2 + i * 18);
                memcpy(q->name, pl + 4 + i * 18, 16);
                q->name[16] = 0;
                q->slot = n->nroster++;
            }
            continue;
        }
        if (type == T_DATA || type == T_RDATA) {
            if (n->host && to != n->me) {   /* relay: to one joiner, or to every other one */
                for (int i = 1; i <= NET_MAX_PLAYERS; i++)
                    if (n->p[i].used && n->p[i].id != from && (to == 0 || n->p[i].id == to))
                        link_send(n, &n->p[i], T_DATA, from, to, type == T_RDATA, pl, len);
                if (to != 0) continue;
            }
            deliver(n, from, pl, len);
        }
    }
    unsigned now = net_ms();
    if (!n->host && !n->me && now - n->join_sent > 300) {   /* knock until welcomed */
        int l = frame(d, T_JOIN, 0, 1, 0, n->name, (int)strlen(n->name));
        raw_send(n, &n->p[0].addr, d, l);
        n->join_sent = now;
    }
    for (int i = 0; i < MAXQ; i++)
        if (n->q[i].used && now - n->q[i].last >= RESEND_MS) {
            peer *pr = peer_by_id(n, n->q[i].to);
            if (!pr) { n->q[i].used = 0; continue; }
            raw_send(n, &pr->addr, n->q[i].d, n->q[i].len);
            n->q[i].last = now;
        }
}

int net_send(net *n, int to, int reliable, const void *buf, int len)
{
    if (!n || len > NET_MAX_MSG) return -1;
    if (n->host) {
        for (int i = 1; i <= NET_MAX_PLAYERS; i++)
            if (n->p[i].used && (to == 0 || n->p[i].id == to)) link_send(n, &n->p[i], T_DATA, n->me, to, reliable, buf, len);
    } else if (n->p[0].used) {
        link_send(n, &n->p[0], T_DATA, n->me, to, reliable, buf, len);
    }
    return 0;
}

int net_recv(net *n, int *from, void *buf, int max)
{
    if (!n || !n->in_n) return -1;
    inmsg *m = &n->in[n->in_head];
    n->in_head = (n->in_head + 1) % INQ;
    n->in_n--;
    int l = m->len < max ? m->len : max;
    memcpy(buf, m->d, (size_t)l);
    if (from) *from = m->from;
    return l;
}

int net_me(const net *n) { return n ? n->me : 0; }
int net_is_host(const net *n) { return n && n->host; }

int net_players(const net *n, net_player *out)
{
    if (!n) return 0;
    if (out) memcpy(out, n->roster, sizeof(net_player) * (size_t)n->nroster);
    return n->nroster;
}

void net_start(net *n)
{
    if (!n || !n->host) return;
    n->started = 1;
    roster_build(n);
    roster_send(n);
}

int net_started(const net *n) { return n ? n->started : 0; }

void net_set_session(net *n, const char *name, int max)
{
    if (!n) return;
    snprintf(n->sname, sizeof n->sname, "%s", name ? name : "");
    n->max = max > 0 && max <= NET_MAX_PLAYERS ? max : 0;
}

const char *net_session_name(const net *n) { return n ? (n->sname[0] ? n->sname : n->name) : ""; }

/* ---- finding sessions on the LAN (the port's stand-in for dpEnumSessions) ---- */
struct net_find {
    SOCKET s;
    int port;
    unsigned last;
    net_session found[NET_MAX_SESSIONS];
    unsigned seen[NET_MAX_SESSIONS];
    int n;
};

net_find *net_find_open(int port)
{
    char err[64];
    int s = sock_open(0, err, sizeof err);
    if (s < 0) return NULL;
    int one = 1;
    setsockopt((SOCKET)s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    net_find *f = calloc(1, sizeof *f);
    f->s = (SOCKET)s;
    f->port = port ? port : NET_DEFAULT_PORT;
    return f;
}

void net_find_close(net_find *f)
{
    if (!f) return;
    CLOSESOCK(f->s);
    free(f);
}

void net_find_poll(net_find *f)
{
    if (!f) return;
    unsigned now = net_ms();
    if (!f->last || now - f->last >= NET_FIND_MS) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)f->port);
        a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(f->s, "HGQ1", 4, 0, (const struct sockaddr *)&a, sizeof a);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* a session on this machine */
        sendto(f->s, "HGQ1", 4, 0, (const struct sockaddr *)&a, sizeof a);
        f->last = now;
    }
    unsigned char d[64];
    struct sockaddr_in a;
    for (;;) {
        socklen_t al = sizeof a;
        int l = (int)recvfrom(f->s, (char *)d, sizeof d, 0, (struct sockaddr *)&a, &al);
        if (l < 0) break;
        if (l < 6 + 33 || memcmp(d, "HGR1", 4)) continue;
        char host[32];
        snprintf(host, sizeof host, "%s", inet_ntoa(a.sin_addr));
        int port = ntohs(a.sin_port), k;
        /* one session answers twice when it is on this machine (the broadcast and the loopback): one entry */
        for (k = 0; k < f->n; k++)
            if (f->found[k].port == port && (!strcmp(f->found[k].host, host) || !memcmp(f->found[k].name, d + 6, 32))) break;
        if (k < f->n && strcmp(host, "127.0.0.1") && !strcmp(f->found[k].host, "127.0.0.1")) {}   /* keep the LAN address */
        else if (k < f->n) { f->seen[k] = now; continue; }
        if (k == f->n) { if (f->n >= NET_MAX_SESSIONS) continue; f->n++; }
        net_session *ss = &f->found[k];
        snprintf(ss->host, sizeof ss->host, "%s", host);
        ss->port = port;
        ss->players = d[4];
        ss->max = d[5];
        memcpy(ss->name, d + 6, 32);
        ss->name[32] = 0;
        f->seen[k] = now;
    }
    for (int k = 0; k < f->n; k++)   /* a session silent for three rounds is gone */
        if (now - f->seen[k] > 3 * NET_FIND_MS) {
            f->found[k] = f->found[f->n - 1];
            f->seen[k] = f->seen[f->n - 1];
            f->n--;
            k--;
        }
}

int net_find_list(const net_find *f, net_session *out, int max)
{
    if (!f) return 0;
    int n = f->n < max ? f->n : max;
    if (out) memcpy(out, f->found, sizeof(net_session) * (size_t)n);
    return n;
}
