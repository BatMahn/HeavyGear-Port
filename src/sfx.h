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

#endif
