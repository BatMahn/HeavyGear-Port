/* the radio queue's order, pairing, duplicates and missing sounds */
#include "../src/radio.h"

#include <stdio.h>
#include <string.h>

static char log_[512];
static int  playing, left;

static int f_exists(void *c, const char *n) { (void)c; return strcmp(n, "missing") != 0; }
static int f_start(void *c, const char *n)
{
    (void)c;
    if (!f_exists(c, n)) return 0;
    strcat(log_, n); strcat(log_, " ");
    left = 3;
    return ++playing;
}
static int f_busy(void *c, int h) { (void)c; (void)h; return left-- > 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    radio_ops ops = {f_start, f_busy, f_exists, NULL};
    radio r;
    radio_init(&r, &ops);
    CHECK(radio_say(&r, "avon003s", NULL));
    CHECK(radio_say(&r, "avonhead", "avondamg"));
    CHECK(!radio_say(&r, "avonhead", "avondamg"));     /* a waiting duplicate */
    CHECK(radio_say(&r, "avonarm_", "missing"));        /* queued, but can't start */
    CHECK(radio_say(&r, "missing", NULL));
    CHECK(radio_say(&r, "avongded", ""));
    for (int i = 0; i < 100; i++) radio_tick(&r);
    CHECK(!strcmp(log_, "avon003s avonhead avondamg avongded "));
    CHECK(!radio_active(&r));
    CHECK(!radio_say(&r, "missing", NULL));
    CHECK(radio_say(&r, "", "avonwyes"));
    CHECK(radio_active(&r));
    for (int i = 0; i < 100; i++) radio_tick(&r);
    CHECK(!radio_active(&r));
    for (int i = 0; i < 20; i++) radio_say(&r, "x", i & 1 ? "a" : "b");
    int used = 0;
    for (int i = 0; i < RADIO_SLOTS; i++) used += r.msg[i].used;
    CHECK(used == 3);   /* x, then x+a, x+b; the rest duplicates */
    if (!fails) printf("test_radio: ok\n");
    return fails != 0;
}
