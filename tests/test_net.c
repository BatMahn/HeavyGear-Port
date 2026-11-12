/* the session layer on the loopback: a host and two joiners; roster, relay, reliable and unreliable delivery */
#include "../src/net.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void spin(net **n, int k, int ms)
{
    unsigned t0 = net_ms();
    while (net_ms() - t0 < (unsigned)ms) {
        for (int i = 0; i < k; i++) net_poll(n[i]);
#ifdef _WIN32
        Sleep(2);
#else
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, NULL);
#endif
    }
}

int main(void)
{
    char err[128];
    net *n[3];
    n[0] = net_host(47790, "Alpha", err, sizeof err);
    if (!n[0]) { printf("test_net: skipped (%s)\n", err); return 0; }
    n[1] = net_join("127.0.0.1", 47790, "Bravo", err, sizeof err);
    n[2] = net_join("127.0.0.1", 47790, "Charlie", err, sizeof err);
    CHECK(n[1] && n[2], "join: %s", err);
    spin(n, 3, 300);
    CHECK(net_players(n[0], NULL) == 3, "host roster %d", net_players(n[0], NULL));
    net_start(n[0]);
    spin(n, 3, 300);
    net_player pl[NET_MAX_PLAYERS];
    CHECK(net_started(n[1]) == 1 && net_players(n[1], pl) == 3, "joiner roster");
    CHECK(pl[0].id == 1 && !strcmp(pl[0].name, "Alpha") && !strcmp(pl[2].name, "Charlie"), "roster names");
    CHECK(net_me(n[1]) == 2 && net_me(n[2]) == 3 && net_is_host(n[0]), "ids %d %d", net_me(n[1]), net_me(n[2]));
    /* a joiner to all: the host takes it and relays it to the other joiner */
    net_send(n[1], 0, 1, "DA hello", 9);
    net_send(n[2], 1, 0, "GO", 2);
    spin(n, 3, 300);
    char b[NET_MAX_MSG];
    int from = 0, got0 = 0, got2 = 0, go = 0;
    for (int l; (l = net_recv(n[0], &from, b, sizeof b)) >= 0;) {
        if (l == 9 && !strcmp(b, "DA hello") && from == 2) got0++;
        if (l == 2 && !memcmp(b, "GO", 2) && from == 3) go++;
    }
    for (int l; (l = net_recv(n[2], &from, b, sizeof b)) >= 0;) if (l == 9 && from == 2) got2++;
    CHECK(got0 == 1 && got2 == 1 && go == 1, "delivery %d %d %d", got0, got2, go);
    /* reliable messages arrive once each, in any case */
    for (int i = 0; i < 50; i++) { unsigned char m[4] = {'K', 'I', (unsigned char)i, 0}; net_send(n[0], 0, 1, m, 4); }
    spin(n, 3, 500);
    int seen[50] = {0}, dup = 0;
    for (int l; (l = net_recv(n[1], &from, b, sizeof b)) >= 0;) if (l == 4 && b[0] == 'K') { if (seen[(int)b[2]]++) dup++; }
    int all = 1;
    for (int i = 0; i < 50; i++) if (!seen[i]) all = 0;
    CHECK(all && !dup, "reliable: all %d dup %d", all, dup);
    net_close(n[2]);
    spin(n, 2, 300);
    CHECK(net_players(n[0], NULL) == 2, "left: %d", net_players(n[0], NULL));
    /* the lobby's search (net_find): the host answers with its session's name, players and most; once started, not */
    net *h2 = net_host(47791, "Gamma", err, sizeof err);
    net_set_session(h2, "Test Game", 4);
    net_find *nf = net_find_open(47791);
    net_session ss[4];
    int found = 0;
    for (int t = 0; t < 200 && !found; t++) { net_find_poll(nf); spin(&h2, 1, 5); found = net_find_list(nf, ss, 4); }
    CHECK(found == 1 && !strcmp(ss[0].name, "Test Game") && ss[0].players == 1 && ss[0].max == 4,
          "find: %d %s %d/%d", found, found ? ss[0].name : "", found ? ss[0].players : 0, found ? ss[0].max : 0);
    net_find_close(nf);
    net_close(h2);
    net_close(n[1]);
    net_close(n[0]);
    printf("test_net: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
