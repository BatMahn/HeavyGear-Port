/* test_sim - the player gear's movement against the running original (mission 1, the Hunter, tools/ref/simlog.py):
 * from a standstill heading 180, full throttle at tick 0; the original had moved 1,929 units after 532 ticks and was
 * at 5.155 units per tick; a full-right turn of 1.5 s (300 ticks) at full speed turned it 127.5 degrees. */
#include "../src/sim.h"

#include <math.h>
#include <stdio.h>

int main(void)
{
    int bad = 0;
    sim_gear g;
    sim_gear_init(&g, 0, 0, 0, 180, SIM_HUNTER_TOP_SPEED, 245.3f);
    for (int t = 0; t < 532; t++) sim_gear_tick(&g, 1, 0, NULL, NULL);
    float d = -g.z;
    int ok = fabsf(d - 1929.0f) < 0.05f * 1929.0f;
    printf("sim: %.0f units in 532 ticks (original 1929) %s\n", d, ok ? "ok" : "FAIL");
    bad += !ok;
    for (int t = 0; t < 2000; t++) sim_gear_tick(&g, 1, 0, NULL, NULL);
    ok = fabsf(g.speed - 5.155f) < 0.01f;
    printf("sim: top speed %.3f units per tick (original 5.155) %s\n", g.speed, ok ? "ok" : "FAIL");
    bad += !ok;
    float h0 = g.heading;
    for (int t = 0; t < 300; t++) sim_gear_tick(&g, 1, 1, NULL, NULL);
    for (int t = 0; t < 600; t++) sim_gear_tick(&g, 1, 0, NULL, NULL);
    float dh = g.heading - h0;
    while (dh < 0) dh += 360;
    ok = fabsf(dh - 127.5f) < 3.0f;
    printf("sim: a 1.5 s turn at full speed turns %.1f degrees (original 127.5) %s\n", dh, ok ? "ok" : "FAIL");
    bad += !ok;
    return bad;
}
