/*
 * shell.h - the game's front end (HGSHELL.DLL), drawn from its picture table.
 *
 * Every screen is a tree in the picture table (src/shell_tables.h, 0x101ddd70): a root (kind 5, its own parent, named
 * Bnn"UNDER") and records whose parent chain leads to it, placed relative to their parent. Kinds: 5 a picture, 1..4 a
 * button's disabled / rest / highlighted / selected art (records named stem + D / R / H / S), 0 an area without
 * its own state (lists' inner areas, selection bars, Smacker animations). The screen draws at 640 x 480 in the
 * root picture's palette.
 */
#ifndef HG_SHELL_H
#define HG_SHELL_H

#include "hgprj.h"

#include <stdint.h>

typedef struct shell shell;

shell *shell_open(hg_archive *a);
void   shell_close(shell *s);
/* show screen `root` (e.g. "B00UNDER"); 0 on success */
int    shell_goto(shell *s, const char *root);
const char *shell_screen(const shell *s);
/* mouse at (x, y) in 640 x 480; down: button state. Returns the stem of a button released over itself ("B00STRY"),
 * else NULL. */
const char *shell_mouse(shell *s, int x, int y, int down);
/* the campaign table entry (0x101ace60; 1..32 the story's missions, 35..38 training) the briefing screens show;
 * loads its BRF. 0 on success. */
int    shell_set_mission(shell *s, int entry);
int    shell_mission(const shell *s);
/* the loaded briefing (NULL without one) */
const struct hg_brf_s *shell_brf(const shell *s);
/* the last mission's outcome for the debriefing: 2 accomplished, 1 failed */
void   shell_set_outcome(shell *s, int outcome);
/* the debriefing's statistics text (lines split by '\n') */
void   shell_set_stats(shell *s, const char *text);
/* the debriefing's objectives list (lines split by '\n') */
void   shell_set_objectives(shell *s, const char *text);
/* the briefing's squad panel: the wingmen's names, portrait pictures (-1 none) and gear names, and the player's gear */
void   shell_set_squad(shell *s, const char *name1, int face1, const char *gear1, const char *name2, int face2,
                       const char *gear2, const char *player_gear);
/* the briefing's gear (B05's view): the player's design as the editor builds it (extra and place as there), its
 * weapons' short names; its turn about y, degrees */
void   shell_set_briefing_gear(shell *s, const unsigned char *gdf, size_t len, const char (*extra)[9],
                               const float place[3], const char *const *weapons, int nweapons);
void   shell_briefing_gear_turn(shell *s, float turn);
/* the gear editor (B07): the category button shown selected ("B07TRSO"), the left list's items from its first
 * shown, the design's name and its GDF in memory (drawn in the view, src/modelview.h; extra: models drawn at
 * records besides the GDF's, modelview_set_data) */
void   shell_set_garage(shell *s, const char *cat, const char *const *items, int n, const char *gear,
                        const unsigned char *gdf, size_t len, const char (*extra)[9]);
/* the gear editor's view: the root object's place and its turn about y, degrees (src/modelview.h) */
void   shell_garage_view(shell *s, const float place[3], float turn);
/* the left list's item at (x, y), counted from the first shown; -1 none */
int    shell_garage_pick(shell *s, int x, int y);
/* the part record drawn under (x, y) in the middle panel, -1 none */
int    shell_garage_model_pick(shell *s, int x, int y);
/* where part record rec of the shown design was drawn; 0 if known */
int    shell_garage_screen(shell *s, int rec, float *x, float *y);
/* its scenario record ("M01_SCN1") */
const char *shell_mission_scenario(const shell *s);
/* a button of the screen not shown (until the next shell_goto) */
void   shell_hide(shell *s, const char *stem);
/* a button disabled (off 1) or enabled again (the control's +0x48 / +0x4c): drawn with its D art, not clickable */
void   shell_disable(shell *s, const char *stem, int off);
/* a popup over the screen (B19UNDER, B72UNDER: FUN_10028f08), placed at its *LOC* record; it takes the mouse */
int    shell_popup(shell *s, const char *root);
void   shell_popup_close(shell *s);
const char *shell_popup_name(const shell *s);
/* a list (FUN_10002f9f) by its picture's name (B20CLIST): items, the selected one */
int    shell_list_set(shell *s, const char *name, const char *const *items, int n, int sel);
int    shell_list_selected(const shell *s, const char *name);
/* a text entry (FUN_10013137) in a rectangle (absolute, inclusive) */
int    shell_edit_set(shell *s, int x0, int y0, int x1, int y1, int max, const char *text);
const char *shell_edit_text(const shell *s);
enum { SHELL_KEY_BACKSPACE = 8, SHELL_KEY_LEFT = 0x25, SHELL_KEY_RIGHT = 0x27, SHELL_KEY_DELETE = 0x2e };
/* key down (Windows key codes, as the original's key events) and typed characters */
void   shell_key(shell *s, int key);
void   shell_char(shell *s, int ch);
/* a text in a rectangle (FUN_100140a7) */
int    shell_box_set(shell *s, int x0, int y0, int x1, int y1, const char *text);
/* draw into 640 x 480 ARGB */
void   shell_draw(shell *s, uint32_t *out);
/* the screen's animations: advance to time ms; play slot with a mode (FUN_100323c3); is a mode-3 one still playing? */
void   shell_anim_tick(shell *s, uint64_t ms);
void   shell_anim_play(shell *s, int slot, int mode);
int    shell_anim_busy(const shell *s);
const char *shell_anim_name(const shell *s, int slot);

#endif
