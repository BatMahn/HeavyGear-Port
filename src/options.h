/*
 * options.h - the game's option files and the options screen's spinners (HGSHELL B64, FUN_10163a91).
 *
 * Traced: the shell keeps three option blocks it reads at start (FUN_1012f1d8 -> FUN_1011e641) and writes on leaving
 * (FUN_1012f17b -> FUN_1011e5db) in the game folder: hgdif.cfg (0x17 bytes, the sim's 0x1014a564 in a single-player
 * game, 0x10070147), hgnet.cfg (the same for multiplayer) and hgsnd.cfg (0x6c bytes). The options screen's spinners
 * (FUN_100122e5; their items' data in brackets) and where B64's accept (FUN_100e3bae -> 0x100e3c57) puts them:
 *   DY Graphics Type "Software 640" (-2), the Direct3D devices (0..)  -> hgsnd +0x58 (the item) and +0x48 ("mcga.dll"
 *      for item -1, else "vesa480.dll"); the port offers Software 640 (-2) and its 3D look as device 0 ("Direct3D":
 *      the original's names are the system's devices, enumerated at run time)
 *   DI Difficulty Low (0) / Normal (1) / High (2)            -> hgdif +5 (the internal points x 12 / 6 / 1, 0x1004e4cc)
 *   IN Invulnerability Off (3) / On (4)                                       -> hgdif +1 (On = 1)
 *   UA Unlimited Ammo Off (5) / On (6)                                       -> hgdif +0 (On = 1: the player's shots spend no ammo)
 *   PA Particles Off (7) / On (8)                                       -> hgsnd +0x28
 *   SH Shadows Off (9) / On (10)                                      -> hgsnd +0x30
 *   ML Multiple Light Sources Off (11) / On (12)                                     -> hgsnd +0x2c
 *   OT Textures Off (13) / On (14)                                     -> hgsnd +0x14, +0x18 and +0x34
 *   ST Sky Off (19) / On (20)                                     -> hgsnd +0x38
 *   TF Texture Filtering Off (21) / On (22)                                     -> hgsnd +0x3c
 *   DS 3D Sound: Off, Headphones, Mono, Quad, Stereo, Surround (23..28) -> hgsnd +0x40 (0..5)
 * B64's cancel reloads them from the blocks (FUN_100e3ed0); the cfg values are i32 in hgsnd, bytes in hgdif.
 */
#ifndef HG_OPTIONS_H
#define HG_OPTIONS_H

#include <stdint.h>

enum { OPT_DI, OPT_IN, OPT_UA, OPT_PA, OPT_SH, OPT_ML, OPT_OT, OPT_ST, OPT_TF, OPT_DS, OPT_DY, OPT_COUNT };

typedef struct {
    uint8_t dif[0x17];
    uint8_t snd[0x6c];
    int     sel[OPT_COUNT];   /* the spinners' item index */
    int     snd_found;        /* hgsnd.cfg was there (else the port keeps its renderer's options on) */
} options;

/* the spinner's stem in B64 ("B64DI"), its items' texts and count */
const char *options_stem(int k);
int         options_items(int k, const char *const **text);

/* read hgdif.cfg / hgsnd.cfg from the game folder (missing: zeros) and set the spinners from them */
void options_load(options *o, const char *game_dir);
/* the spinners into the blocks, then both written (0 on success) */
int  options_save(options *o, const char *game_dir);
/* the blocks into the spinners (B64's cancel) */
void options_from_blocks(options *o);
/* the difficulty byte (hgdif +5): 0, 1, 2 */
int  options_difficulty(const options *o);
/* the internal points' multiplier for the player's group (0x1004e4cc: x 12, x 6, x 1 by hgdif +5) */
float options_internal_scale(const options *o);
/* Invulnerability (hgdif +1) and Unlimited Ammo (hgdif +0): 1 when on (the sim reads them for the player only) */
int  options_invulnerable(const options *o);
int  options_unlimited_ammo(const options *o);
/* hgsnd's Particles (+0x28), Sky (+0x38), Texture Filtering (+0x3c) i32s, read by the sim at its start */
int  options_snd_i32(const options *o, int off);
/* the Graphics Type: 1 when hgsnd +0x58 names a Direct3D device (>= 0), 0 for the software ones; -1 without the file */
int  options_graphics_3d(const options *o);

#endif
