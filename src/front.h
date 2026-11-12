/*
 * front.h - the front end's flow: HGSHELL's state machine over the shell's screens.
 *
 * The shell runs one state at a time (FUN_101377e4 builds state n's screen, n the screen's number: 0 the main
 * menu B00, 5 the briefing B05, 0x14 game selection B20 ...). Buttons carry actions (FUN_1000dbf4): go to a state,
 * set one of the shell's variables, go to the state a variable holds, open / close a popup state, launch the
 * mission ... Variables used here: 0 the state after a mission, 1 the state to go back to, 2 the state after the
 * transition state 0xe, 0x13 a popup's text, 0x1a the campaign entry, 0x29 the game kind (0 story, 1 tour of duty,
 * 2 multiplayer), 0x2a a confirmation's answer.
 */
#ifndef HG_FRONT_H
#define HG_FRONT_H

#include "squad.h"
#include "debrief.h"

#include "hgprj.h"

#include <stdint.h>

typedef struct front front;

enum { FRONT_RUN, FRONT_QUIT, FRONT_LAUNCH, FRONT_MOVIE };

/* game_dir: where CHARINFO.HG1 lives (the game folder) */
front *front_open(hg_archive *a, const char *game_dir);
void   front_close(front *f);
void   front_mouse(front *f, int x, int y, int down);
/* the arrow keys held now (the gear editor turns the design while one is, FUN_100f1516) */
void   front_keys(front *f, int left, int right);
void   front_key(front *f, int vk);     /* Windows key codes: 8 Backspace, 0x25 / 0x27 Left / Right, 0x2e Delete */
void   front_char(front *f, int ch);
/* what the front end wants: FRONT_QUIT, FRONT_LAUNCH (then front_scenario), FRONT_MOVIE (then front_movie), else
 * FRONT_RUN; reported once */
int    front_poll(front *f);
const char *front_scenario(const front *f);
/* the wingmen of the mission being launched (filled at FRONT_LAUNCH; front_squad_update recomputes them) */
const squad *front_squad(const front *f);
void   front_squad_update(front *f);
/* the player's gear for the launch (a GDF name), "" before the briefing */
const char *front_player_gear(const front *f);
/* after the mission: whether wingman 1 / 2 was killed (they die in the game at the debriefing's continue) */
void   front_squad_lost(front *f, int first, int second);

/* after FRONT_MOVIE: the cutscene to play ("ZL_CUT01") */
const char *front_movie(const front *f);
/* back from the mission: success advances the story (traced: HGSHELL 0x100df8e4) */
void   front_mission_over(front *f, int success);
/* a multiplayer launch: the session the sim takes over (NULL otherwise); after the mission, front_mp_over with the
 * scores' text closes it and shows B39 */
struct net;
struct net *front_net(front *f);
void   front_mp_over(front *f, const char *results);
/* the mission's statistics for the debriefing (shell_set_stats) */
void   front_mission_stats(front *f, const char *text);
void   front_mission_objectives(front *f, const char *text);
/* before front_mission_over: the sim's objectives, kills and deaths (hgmsn.cfg / HGCAR, src/debrief.h) */
void   front_mission_result(front *f, const debrief_in *in);
void   front_draw(front *f, uint32_t *out);
/* the time (ms) for the briefing's narration */
void   front_tick(front *f, uint64_t ms);
/* silence the front end's sounds (before a mission); the next screen starts its own again */
void   front_sound_pause(front *f);
/* testing: go straight to a state */
void   front_state(front *f, int state);
const char *front_screen(const front *f);

#include "options.h"
/* the option blocks (hgdif.cfg / hgsnd.cfg) as the options screen leaves them */
const options *front_options(const front *f);
#endif
