/*
 * sfx.h - the sim's sound effects (HGSIM's sound calls; RE_NOTES "Sound effects").
 *
 * Sounds are named ("AFXLAC_F"), loaded once from the archive ("WAV", the language variant first) and played on the
 * mixer. Volumes are DirectSound's hundredths of a dB.
 *  - flat (0x10008723 name, volume, pan, -, priority): the player's own sounds and the cockpit's;
 *  - looped (0x10008788): the same with the loop flag (0x2000);
 *  - placed (0x1000863a x, y, z, name, quiet, ...): x, y, z the offset between the camera and the source; the
 *    distance d (world units) gives the volume -(d / 100)^2 / 100 (0x10008bbe: -2500 x (d/100)^2 / 250000, beyond
 *    50000 silent), 1000 less when `quiet` (the callers pass the cockpit view's flag 0x101316f4, set by 0x10037a72); under
 *    -6000 the sound isn't played (0x1000825f); it starts d x 0.00549 ticks late (its travel time, 5 ms ticks), and
 *    its pan is sin(camera heading - bearing) x 10000, held to +-7500 (0x1000889f).
 */
#ifndef HG_SFX_H
#define HG_SFX_H

#include "hgprj.h"

void sfx_init(hg_archive *a);
void sfx_free(void);
/* the listener: the camera's position and heading (degrees, clockwise from +z) */
void sfx_listener(float x, float y, float z, float heading);
void sfx_listener_pos(float *x, float *y, float *z);
/* returns the voice (0 none) */
/* flags: the priority in the low byte (0x32, 0x50 ...), 0x100 / 0x200 / 0x400 the sharing rules (sfx.c), 0x2000 loop */
int  sfx_flat(const char *name, int vol, int pan, unsigned flags);
/* quiet: 10 dB down (the callers pass the cockpit view's flag, 0x101316f4) */
int  sfx_at(const char *name, float x, float y, float z, int quiet, unsigned flags);
/* a looped placed sound kept up to date */
void sfx_move(int voice, float x, float y, float z, int quiet);
void sfx_stop(int voice);
int  sfx_playing(int voice);
/* stop the first channel playing a sound (0x100087d8); set the frequency of every channel playing it (0x100084f1) */
void sfx_stop_name(const char *name);
void sfx_rate_name(const char *name, int hz);

/*
 * The gear's motion sounds, as HGSIM plays them (every one only for the player's own vehicle). Volumes go through the
 * effects' level (hgsnd.cfg, audio_level_db) like every effect. The callers (the walk / skate / jump code) call:
 *  - sfx_footstep: AFXHGWKL placed at the vehicle (its +0x54..: x, y, z), priority 0x32, quiet in the cockpit view
 *    (0x101316f4) - at each foot-down, the animation's event flag 8 in the vehicle's +0x84 (0x1003fbb7, where the
 *    foot's dust is thrown too, 0x10086007);
 *  - skating (0x1005d75c, the gear block's +0x562 bit 1 skating): sfx_skate_engage when the skates go down (AFXHGWEN
 *    -20 dB, priority 0x50; the spin-up +0x607 = 1, +0x60f = +0x60b, and the "skates" count +0x110 of HGCAR);
 *    sfx_skate_run when the spin-up ends (+0x60f below 1: AFXHGWHE looped at -20 dB, 0x150); sfx_skate_speed each
 *    tick while skating and moving (the wheel loop's frequency = (int)(|velocity +0x548| x 200 + 13538) Hz,
 *    0x1005ff13); sfx_skate_stop when skating ends or the spin-up is cut (AFXHGWHE stopped, AFXHGWDE -20 dB, 0x50);
 *  - jump jets (0x1005d75c): sfx_jump_burn as a burn starts (+0x510 at 0x2d4 when the jets fire: AFXHGJJT looped at
 *    0 dB 0x150, AFXHGJJF 0 dB 0x32); sfx_jump_burn_end when the burn's timer runs out (AFXHGJJT stopped, 0x100610bd);
 *  - sfx_fall: AFXHGFLS -10 dB, 0x50, when the gear falls (the 0x562 bits 4 / 0x400 set, a leg lost or knocked down);
 *  - sfx_landing: on touching down hard (0x1005366d from 0x1005d75c): AFXHGJLR when the fall speed is at most 140,
 *    else AFXHGCOO, placed at the vehicle's position less its +0x520 (the soles), priority 0x32, quiet in the cockpit;
 *  - sfx_bump: another vehicle running into the player (0x100354eb, the network's collision message): AFXHGCOO at
 *    (int)(-10000 x min(speed, 25) / 150 / 6) hundredths of a dB, 0x32.
 * And the particle engine's laser beams: sfx_laser_loop(1) as a laser's beam segment starts (0x1007b956: AFXWLLLC
 * looped at -10 dB, 0x150 - its 0x100 lets one play at a time), (0) as one ends (0x1007aec2).
 */
void sfx_footstep(float x, float y, float z, int quiet);
void sfx_skate_engage(void);
void sfx_skate_run(void);
void sfx_skate_speed(float speed);
void sfx_skate_stop(void);
void sfx_jump_burn(void);
void sfx_jump_burn_end(void);
void sfx_fall(void);
void sfx_landing(float x, float y, float z, float speed, int quiet);
void sfx_bump(float speed);
void sfx_laser_loop(int on);

#endif
