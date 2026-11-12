/*
 * test_particles - the particle engine's lifetimes and frames (HGSIM 0x10079c54 and its kinds), with the default
 * types (no PartSpec.ini) and a view that sees everything.
 */
#include "../src/particles.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int effects;
static void eff(void *ctx, int type, const float at[3]) { (void)ctx; (void)at; if (type == 0x49) effects++; }

static void view(particles *p, int software)
{
    part_view v;
    memset(&v, 0, sizeof v);
    v.cam.fov = 90;
    v.w = 640; v.h = 480;
    v.far = 125000;
    v.camdist = 4906;
    v.zoom = 1;
    v.software = software;
    particles_set_view(p, &v);
}

static int alive_after(particles *p, int ticks, int step)
{
    for (int c = step; c <= ticks; c += step) particles_frame(p, c, eff, NULL);
    return particles_count(p);
}

int main(void)
{
    particles *p = particles_new(NULL, NULL);
    float at[3] = {0, 0, 1000}, d[3] = {0, 0, 100};
    view(p, 0);
    /* the tracer glow lasts 0x17 ticks (0x10083dde), the missile trail 0x6f (0x10081afb), the smoke 0x16b (0x1007ffb3) */
    particles_tracer(p, 0, at, d);
    CHECK(particles_count(p) == 1, "tracer spawned %d", particles_count(p));
    CHECK(alive_after(p, 0x16, 1) == 1 && alive_after(p, 0x17, 0x17) == 0, "tracer life");
    particles_missile(p, 0, at, d);
    CHECK(alive_after(p, 0x6e, 0x6e) == 1, "missile alive at 0x6e");
    particles_frame(p, 0x6f, eff, NULL);
    CHECK(particles_count(p) == 0, "missile gone at 0x6f");
    particles_smoke(p, 0, at, d);
    particles_frame(p, 0x16a, eff, NULL);
    CHECK(particles_count(p) == 1, "smoke alive at 0x16a");
    particles_frame(p, 0x16b, eff, NULL);
    CHECK(particles_count(p) == 0, "smoke gone at 0x16b");
    /* behind the eye: no system */
    float back[3] = {0, 0, -1000};
    particles_tracer(p, 0, back, d);
    CHECK(particles_count(p) == 0, "behind the eye %d", particles_count(p));
    /* the spray is the 3D renderers' only (0x1008514d) */
    view(p, 1);
    particles_spray(p, 0, at, d, 0x10);
    CHECK(particles_count(p) == 0, "spray in software %d", particles_count(p));
    view(p, 0);
    particles_spray(p, 0, at, d, 0x10);
    CHECK(particles_count(p) == 1, "spray in 3D %d", particles_count(p));
    particles_frame(p, 0x5b, eff, NULL);
    CHECK(particles_count(p) == 0, "spray gone at 0x5b");
    /* beams (0x100baf31): a rail gun's 8 points end k x 0x16 ticks on, each with effect 0x49; a laser's 15 segments
     * last its life; an accelerator's 15 segments k x 0x16 */
    float pts[16][3];
    for (int k = 0; k < 16; k++) { pts[k][0] = 0; pts[k][1] = 0; pts[k][2] = 100.0f * (float)k; }
    particles_beam(p, 0, 0, 4, 0, (const float (*)[3])pts);
    CHECK(particles_count(p) == 8, "rail gun points %d", particles_count(p));
    effects = 0;
    alive_after(p, 400, 1);
    CHECK(effects == 8 && particles_count(p) == 0, "rail gun effects %d left %d", effects, particles_count(p));
    particles_beam(p, 0, 1, 2, 181, (const float (*)[3])pts);
    CHECK(particles_count(p) == 15, "laser segments %d", particles_count(p));
    particles_frame(p, 181, eff, NULL);
    CHECK(particles_count(p) == 15, "laser alive at its life");
    particles_frame(p, 182, eff, NULL);
    CHECK(particles_count(p) == 0, "laser gone after");
    particles_beam(p, 0, 2, 6, 133, (const float (*)[3])pts);
    particles_frame(p, 1, eff, NULL);
    CHECK(particles_count(p) == 14, "accelerator: segment 0 gone at once (%d)", particles_count(p));
    particles_free(p);
    if (fails) { printf("test_particles: %d failures\n", fails); return 1; }
    printf("test_particles: ok\n");
    return 0;
}
