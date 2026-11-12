/* the sim's analog controls (src/input.h), the radar / HUD state (src/radar.h) and the views' zoom (src/views.h) */
#include "../src/input.h"
#include "../src/radar.h"
#include "../src/views.h"

#include <math.h>
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    hg_axis ax[AX_COUNT];
    hg_axes_init(ax);
    /* zoom_factor rests at 1; held up (rate 6: 192 a tick of speed) it reaches 4, reset brings it back */
    CHECK(fabsf(hg_axis_value(&ax[AX_ZOOM]) - 1.0f) < 1e-4f, "zoom rest %g", hg_axis_value(&ax[AX_ZOOM]));
    float z = 0;
    for (int f = 0; f < 4; f++) z = hg_axis_step(&ax[AX_ZOOM], 10, 1, 0, 0);
    /* speeds 1920, 3840, 5760, 7680 add to 19200 of 0x20000 across 1..4 */
    float want = 1.0f + 3.0f * 19200.0f / 131072.0f;
    CHECK(fabsf(z - want) < 1e-3f, "zoom after 4 frames %g, want %g", z, want);
    for (int f = 0; f < 100; f++) z = hg_axis_step(&ax[AX_ZOOM], 10, 1, 0, 0);
    CHECK(fabsf(z - 4.0f) < 1e-4f, "zoom held %g", z);
    z = hg_axis_step(&ax[AX_ZOOM], 10, 0, 0, 1);
    CHECK(fabsf(z - 1.0f) < 1e-4f, "zoom reset %g", z);
    /* a _delta axis: its position doubles each frame and drops to 0 when released */
    float d = hg_axis_step(&ax[AX_TRACK_DIST], 1, 1, 0, 0);   /* (0 + 192) x 2 = 384 */
    CHECK(fabsf(d - 384.0f * 3600.0f / 65536.0f) < 1e-2f, "track distance delta %g", d);
    d = hg_axis_step(&ax[AX_TRACK_DIST], 1, 0, 0, 0);
    CHECK(d == 0.0f, "track distance released %g", d);

    radar r;
    radar_init(&r);
    CHECK(radar_step(&r, 0) == 0 && r.mode == 1, "radar starts in mode 1 (%d)", r.mode);
    radar_zoom(&r, 1, 0);
    CHECK(radar_range(&r) == 200000, "zoom without hgdif +9 changes nothing (%d)", radar_range(&r));
    radar_zoom(&r, 1, 1); radar_zoom(&r, 1, 1);
    CHECK(radar_range(&r) == 50000, "zoomed in twice %d", radar_range(&r));
    radar_zoom(&r, 1, 1);
    CHECK(radar_range(&r) == 400000, "zoom in past the minimum wraps to the maximum (%d)", radar_range(&r));
    radar_zoom(&r, 2, 1);
    CHECK(radar_range(&r) == 50000, "zoom out past the maximum wraps to the minimum (%d)", radar_range(&r));
    /* F8 without the uplink: the message; with it: into the map (3 then 4), the HUD off */
    radar_map_key(&r, 0, 1, 100);
    CHECK(r.msg_until == 100 + 0x389 && r.mode == 1, "no uplink: message till %d", r.msg_until);
    radar_map_key(&r, 1, 1, 100);
    CHECK(radar_step(&r, 0) == 3 && r.hud == 0, "into the map: step 3, HUD off");
    radar_step(&r, 0);
    CHECK(r.mode == 4, "map mode %d", r.mode);
    CHECK(!radar_toggle_hud(&r), "F11 is ignored on the map");
    radar_map_key(&r, 1, 1, 200);
    CHECK(radar_step(&r, 0) == 5 && r.hud == 1, "out of the map: step 5, HUD on");
    radar_step(&r, 0);
    CHECK(r.mode == 1, "back to mode %d", r.mode);
    /* F11: 1 -> 2 (nothing saved: 0) -> 1 */
    radar_toggle_hud(&r);
    CHECK(r.hud == 0, "F11 with no saved HUD turns it off (%d)", r.hud);
    radar_toggle_hud(&r);
    CHECK(r.hud == 1, "F11 again: on (%d)", r.hud);

    views v;
    views_init(&v, 0);
    CHECK(!views_zoom(&v, 1.0f), "no change at 1");
    CHECK(views_zoom(&v, 2.0f) && v.zoom == 2.0f, "the cockpit takes zoom_factor");
    v.view = VIEW_TRACK;
    views_zoom(&v, 3.0f);
    CHECK(v.zoom == 1.0f, "the track view keeps its own zoom (%g)", v.zoom);

    if (fails) { printf("test_views: %d failures\n", fails); return 1; }
    printf("test_views: ok\n");
    return 0;
}
