/*
 * mpscreens.h - the multiplayer screens' session side (HGSHELL's network screens, traced flow):
 *
 *   B01 accept with a multiplayer game -> state 0x15 (B21, FUN_1014a59f: the connection list, popup) -> its accept
 *   goes to the selected connection's state (var 0xb = the item's value: IPX 0x3a, Internet 0x32, Modem 0x17, Null
 *   Modem 0x1c; FUN_1002e1xx files them into slots 0..3 by the comm DLL's name) -> 0x3a (B58, FUN_1015e872: an
 *   action screen, the session layer opened (FUN_100d3e35), then 0x24; on failure the message popup 0x4a with string
 *   var 0x13) -> 0x24 (B36, FUN_1015118b: the LAN lobby: the sessions, the players, chat; create -> 0x31, join ->
 *   0x2f (FUN_100da0fa(0x2f, 0x45, 0xd)), channels -> 0x3c, barracks -> 3, build -> 7, abort -> 1; a timer action
 *   every 0x1388 ms refreshes the list) -> 0x31 (B49, FUN_1015d831, popup: the game's name, password and player
 *   count; create -> the session hosted (FUN_100d37b7) -> 0x2e) -> 0x2e (B46, FUN_101581dd: the host's setup:
 *   enemies / friends, the mission, its description, the gear, chat; launch) / 0x2f (B47, FUN_1015af7c: a joiner's,
 *   ready) -> the sim -> 0x27 (B39, FUN_10152777: the multiplayer debriefing; abort -> var 0x29 = 2).
 *
 * The port's session is its own UDP transport (src/net.h): ANET2.DLL's wire format isn't traced, so the port offers
 * one connection, "LAN (UDP/IP)", standing in for the original's IPX path (B58 -> B36). The mission is the random
 * mission generator's multiplayer variant (0x1006540f: mode 2, 'M' templates; the host picks the type), generated
 * by every machine from the host's seed.
 */
#ifndef HG_MPSCREENS_H
#define HG_MPSCREENS_H

#include "hgprj.h"
#include "net.h"
#include "shell.h"

typedef struct mps mps;

mps  *mps_open(hg_archive *a, const char *player);
void  mps_close(mps *m);
/* the screen just entered: its areas filled */
void  mps_enter(mps *m, shell *sh, int state);
/* each pass of the shell's loop on a multiplayer screen: the session polled, the areas refreshed. Returns the state
 * to go to (the session gone: 0x24; a joiner's game started: -2 = launch), else -1 */
int   mps_tick(mps *m, shell *sh, int state);
/* the screens' actions: 0 B21 accept, 2 B36 join, 3 leave the session (abort), 4 B49 create (name from the entry),
 * 6 B46 launch, 7 B46 / B47 abort, 8 B47 ready, 9 the session list's selection (dir), 12 B49's count (dir),
 * 13 B46's mission type (dir), 14 chat: the entry's text sent. Returns a state to go to, -2 launch, or -1 */
int   mps_action(mps *m, shell *sh, int act, int dir, const char *text);
/* the game being launched: its scenario, and the session the sim takes over (NULL outside multiplayer) */
const char *mps_scenario(const mps *m);
net  *mps_net(mps *m);
/* back from the sim: the session is closed and the results kept for B39 */
void  mps_over(mps *m, const char *results);

#endif
