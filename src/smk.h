/*
 * smk.h - Smacker (SMK2) animations: the shell's screen animations (the game folder's SMK/<name>.SMK, played
 * through SMACKW32.DLL).
 *
 * Only what the shell's files use: 8-bit frames, the palette chunks, no sound tracks (the files have none). The format: a 104-byte header ('SMK2', width, height, frames, the frame rate, flags, 7 audio
 * sizes, the trees' size, the four trees' unpacked sizes, 7 audio rates), the frames' sizes and types, the four
 * Huffman trees (MMap, MClr, Full, Type) and the frames; each frame an optional palette delta, then 4 x 4 blocks:
 * mono (two colours and a 16-bit mask), full, skip, or fill, in runs.
 */
#ifndef HG_SMK_H
#define HG_SMK_H

#include <stddef.h>
#include <stdint.h>

typedef struct smk smk;

/* the file's bytes are kept by the decoder (freed by smk_free) */
smk *smk_open(uint8_t *data, size_t len);
void smk_free(smk *s);
int  smk_width(const smk *s);
int  smk_height(const smk *s);
int  smk_frames(const smk *s);
/* milliseconds per frame */
double smk_frame_ms(const smk *s);
/* decode frame n (frames must go in order from 0; going back restarts from 0). The picture: width x height
 * palette indices; the palette: 256 x RGB. 0 on success */
int  smk_frame(smk *s, int n);
const uint8_t *smk_pixels(const smk *s);
const uint8_t *smk_palette(const smk *s);

#endif
