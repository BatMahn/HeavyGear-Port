/*
 * player.h - the player's gear's sounds from the sim's events (HGSIM 0x1005d75c / 0x10060491's sound calls), and its
 * power loss (+0xc3d) counted down.
 *  - skating: AFXHGWEN as the wheels come down (-20 dB, priority 0x50), AFXHGWHE looped while they run (0x10008788,
 *    0x150), AFXHGWDE as they come up (the loop stopped first, 0x100087d8);
 *  - jump jets: AFXHGJJT looped (0x150) and AFXHGJJF (0x32) as a jump starts, the loop stopped at its end;
 *  - a landing over 140 with the landing-damage option (hgdif +3): the legs damaged, AFXHGJLR / AFXHGCOO (0x1005366d);
 *  - the power back (+0xc3d run out, 0x1005d75c): AFXHBEEP.
 */
#ifndef HG_PLAYER_H
#define HG_PLAYER_H

#include "sim.h"
#include "combat.h"

typedef struct {
    int skate_loop;   /* the voices of the looped sounds */
    int jet_loop;
    int cockpit;      /* the view is the cockpit (the placed sounds quiet, 0x101316f4) */
} player_sound;

void player_sounds(player_sound *ps, int events, const sim_gear *g, combatant *c, int dt);

#endif
