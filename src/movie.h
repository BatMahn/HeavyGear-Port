/*
 * movie.h - the cutscenes (the game CD's VIDEO/<name>.AVI: Indeo 5 video, 11025 Hz PCM sound).
 *
 * The shell plays the campaign table's movie for the mission it goes to (HGSHELL 0x100ffd56, at the transition
 * state 0x0e, FUN_10147527: the table's +0x12 for var 0x1a; at entry 0x20, ZL_CUT16 instead when the story has
 * ended) and NDEATH / SDEATH when the player's character dies (0x1010699f, by side). It finds the files in
 * wavmap.ini's [WAVMap] DVDMoviePath (0x100fc5b4) or on the CD. The port looks, in order, in:
 *   - wavmap.ini's [WAVMap] DVDMoviePath in the game folder;
 *   - the game folder's VIDEO/ (or video/) folder and the game folder itself;
 *   - a CD image of disc 2 in the game folder or its cd/ folder (.mdf / .bin / .iso: the ISO 9660 data track's
 *     VIDEO/<name>.AVI).
 * Decoding needs FFmpeg's libavformat / libavcodec at build time (Indeo 5); without them movies are skipped.
 */
#ifndef HG_MOVIE_H
#define HG_MOVIE_H

#include <SDL3/SDL.h>

/* is there a decoder in this build? */
int movie_supported(void);
/* play `name` ("ZL_CUT01") full screen until it ends or a key / click skips it. 0 played, -1 not found or not
 * playable, 1 the window was closed during it */
int movie_play(SDL_Renderer *ren, const char *game_dir, const char *name);

#endif
