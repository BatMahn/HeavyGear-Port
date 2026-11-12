/*
 * net.h - the multiplayer session over UDP/IP: the dp* calls the sim makes (dpOpen, dpCreatePlayer, dpSend,
 * dpReceive, dpEnumPlayers, dpNumPlayers, dpClose; HGSIM 0x10030053 resolves them from ANET2.DLL).
 *
 * What the sim needs from the session (traced, HGSIM 0x10031389 / 0x1006a860 / 0x1006a890 / 0x1006a8c4):
 * - players with 16-bit ids; the session's master is the player with the lowest id (0x10031389: DAT_1013152c, the
 *   lowest of the session table's ids; DAT_10131524 = 1 on the master, 2 elsewhere);
 * - dpSend(dp, from = my id, to = an id or 0 for every other player, flags (1: reliable), buffer, size);
 * - dpReceive(dp, &from, &to, flags, buffer (0x100 bytes), &size), 0xfa01 when nothing waits (tried 3 times).
 *
 * The transport underneath is the port's own (ANET2.DLL's wire format and its comm DLL WINETS2.DLL aren't traced,
 * so a port game doesn't talk to the original's): one host, the others join it by address; every datagram is
 * "HGN1", type, from, to, sequence, size, payload; the host relays what one joiner sends to the others; reliable
 * messages are resent every 200 ms until acknowledged and delivered once. Players' ids are given in joining order
 * from 1 (the host): the host is the session's master, as the lowest id.
 */
#ifndef HG_NET_H
#define HG_NET_H

#define NET_MAX_PLAYERS 10      /* the sim's vehicle table loops run to 10 for the players (0x10031a86) */
#define NET_MAX_MSG 0x100       /* the sim's receive buffer (0x107caa78, 0x100 bytes) */
#define NET_DEFAULT_PORT 47624  /* the port's choice */

typedef struct net net;

typedef struct {
    int  id;                    /* 1 the host, then in joining order */
    char name[17];
    int  slot;                  /* 0 .. players - 1, in id order (the session table's order) */
} net_player;

net *net_host(int port, const char *name, char *err, int errlen);
net *net_join(const char *host, int port, const char *name, char *err, int errlen);
void net_close(net *n);
/* receive and resend; call often (every frame at least) */
void net_poll(net *n);
/* to: a player's id, 0 every other player; reliable: resent until acknowledged */
int  net_send(net *n, int to, int reliable, const void *buf, int len);
/* the next message for this player: its size (-1 none), *from the sender's id */
int  net_recv(net *n, int *from, void *buf, int max);
int  net_me(const net *n);
int  net_is_host(const net *n);
int  net_players(const net *n, net_player *out);   /* returns the count */
/* the host stops taking joins and tells everyone the final roster (the game starts) */
void net_start(net *n);
int  net_started(const net *n);
/* the session's name and the most players it takes (B49: the game's name and count), the host's */
void net_set_session(net *n, const char *name, int max);
const char *net_session_name(const net *n);

/* Finding sessions (the lobby's list; the original's dpEnumSessions through ANET, HGSHELL 0x1002a202 .., refreshed
 * by B36's timer every 0x1388 ms, FUN_10016668(0x1388)): the port asks the LAN by a broadcast "HGQ1" on the port
 * and every host not yet started answers "HGR1", its player count, its most, its name. */
#define NET_MAX_SESSIONS 16
#define NET_FIND_MS 0x1388
typedef struct { char host[32]; int port, players, max; char name[33]; } net_session;
typedef struct net_find net_find;
net_find *net_find_open(int port);
void net_find_close(net_find *f);
void net_find_poll(net_find *f);
int  net_find_list(const net_find *f, net_session *out, int max);

/* milliseconds, monotonic */
unsigned net_ms(void);

#endif
