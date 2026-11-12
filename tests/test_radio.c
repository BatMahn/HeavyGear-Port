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
static int clk;
static char shown[64];
static int t_off(void *c) { (void)c; return 1; }
static int t_clock(void *c) { (void)c; return clk; }
static void t_show(void *c, const char *t, int ticks) { (void)c; (void)ticks; snprintf(shown, sizeof shown, "%s", t); }
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    radio_ops ops = {f_start, f_busy, f_exists, NULL, NULL, NULL, NULL, NULL};
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
    /* priorities from the registry's ids (0x10016aa0): id 1 has priority 80, the rest -1 */
    for (int i = 0; i < 400; i++) radio_tick(&r);
    for (int i = 0; i < RADIO_SLOTS; i++) r.msg[i].used = 0;
    r.head = -1;
    sound_reg_reset();
    CHECK(sound_reg_id("hipri") == 1 && sound_reg_id("h0") == 2 && sound_reg_id("hipri") == 1);
    log_[0] = 0;
    radio_say(&r, "h0", NULL);
    radio_say(&r, "lo1", NULL);
    radio_say(&r, "lo2", NULL);
    radio_say(&r, "hipri", NULL);
    for (int i = 0; i < 200; i++) radio_tick(&r);
    CHECK(!strcmp(log_, "h0 hipri lo1 lo2 "));
    /* the voices off (0x10016f28): the texts (0x10016aa0: "<a> <b>.") show for 0x712 ticks each, nothing plays */
    {
        radio_ops off = {f_start, f_busy, f_exists, NULL, NULL, t_off, t_show, t_clock};
        radio q;
        radio_init(&q, &off);
        log_[0] = 0;
        CHECK(radio_say(&q, "AVONENGE", "AVONDAMG"));
        CHECK(!strcmp(shown, "engine damaged."));
        CHECK(radio_say(&q, "AVONIRED", "AVONDEAD"));
        clk = 0x711; radio_tick(&q);
        CHECK(!strcmp(shown, "engine damaged."));
        clk = 0x712; radio_tick(&q);
        CHECK(!strcmp(shown, "infrared destroyed."));
        CHECK(log_[0] == 0);
        CHECK(!radio_say(&q, "nosuchvc", NULL) || radio_active(&q));
    }
    if (!fails) printf("test_radio: ok\n");
    return fails != 0;
}
