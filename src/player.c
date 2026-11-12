#include "player.h"
#include "sfx.h"

#include <math.h>

void player_sounds(player_sound *ps, int ev, const sim_gear *g, combatant *c, int dt)
{
    if (ev & SIM_EV_SKATE_ENGAGE) sfx_skate_engage();
    if (ev & SIM_EV_SKATE_ON) { sfx_skate_run(); ps->skate_loop = 1; }
    if (ev & SIM_EV_SKATE_OFF) { sfx_skate_stop(); ps->skate_loop = 0; }
    /* the wheels' loop pitched by the speed while skating (0x1005ff13) */
    if (ps->skate_loop && g && g->skating) sfx_skate_speed(sqrtf(g->vx * g->vx + g->vz * g->vz));
    if (ev & SIM_EV_JUMP_START) { sfx_jump_burn(); ps->jet_loop = 1; }
    if (ev & SIM_EV_JUMP_END) { sfx_jump_burn_end(); ps->jet_loop = 0; }
    /* a landing over 140 (0x1005d75c): with the landing-damage option (hgdif +3, 0x1014a564 + 3) both legs take
     * ftol(speed) at x 0.1, no chip, mode 1 (zones 6 then 5, 0x1005c1f8), and, but for aircraft (0x10066eeb), the
     * landing's sound (0x1005366d); without the option nothing */
    if ((ev & SIM_EV_LAND_HARD) && g && g->fall_damage) {
        if (c) {
            combat_damage_raw(c, (int)g->land_speed, 0.1f, 0, 1, 6);
            combat_damage_raw(c, (int)g->land_speed, 0.1f, 0, 1, 5);
        }
        if (g->vclass != 5 && g->vclass != 8) sfx_landing(g->x, g->y - g->stand, g->z, g->land_speed, ps->cockpit);
    }
    if (c && c->stun > 0) {
        c->stun -= dt;
        if (c->stun <= 0) { c->stun = 0; sfx_flat("AFXHBEEP", 0, 0, 0x32); }
    }
}
