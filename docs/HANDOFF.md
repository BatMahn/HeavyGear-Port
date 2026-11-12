# Session handoff (2026-11-12, v1.0.0)

Repository: https://github.com/BatMahn/HeavyGear-Port (BatMahn; commits as `BatMahn <65992136+BatMahn@users.noreply.github.com>`).

See the top-level handoff notes for the workspace rebuild. New in this session:
- Ghidra exports as before. Reference frames: missions 1, 2, 5, 10 (8-bit) and 1 (Direct3D).
- The Wine recipe's USERSTAR.BWD needs the header's third word = the largest chunk size, and the player's record has
  group 0, `+5` = 1, `+6` = 0 (tools/ref/mkuserstar.py). Without that the sim drops the file and shows vehicle 0.
- Wine relocates HGSIM.DLL; read its base from /proc/<pid>/maps (tools/ref/origstate.py does).

v0.7.2 traced the sim drawing's INFERRED items: detail levels (0x10090750), effects facing the eye (0x10037fcc),
type-3 sequences' drawing and timing (0x10026610, 0x1008700c, 0x10001725), debris turning (0x100955b7), bounding
spheres (0x10003326). DX5DRAW.DLL is now exported too (out/DX5DRAW.DLL.c, HG's own Direct3D plugin: textures are
16-bit surfaces with a source colour key, 0x10005690) for the TEX alpha item. Still INFERRED in world.c: TEX alpha,
multi-object records, oa / oc objects, block objects' y, PALG's palette, the shot streaks; the software luma's | 7
is calibrated against a frame but conflicts with the trace (KNOWN_ISSUES).

v1.0.0: three rounds of parallel work cleared most of KNOWN_ISSUES (what remains is listed there). New files:
src/hgrand.c (the sim's random tables), src/player.c, src/views.c, src/input.c, src/radar.c, src/escmenu.c,
src/radio_text.h (tools/ref/gen_radio_text.py), src/debrief.c, src/barracks.c, src/options.c, src/credits.h,
src/garagepic.c, src/garagepanel.c, src/randmsn.c + src/rmg_tables.h (tools/ref/gen_rmg_tables.py), src/tour.c,
src/net.c, src/mp.c, src/mpscreens.c, src/weapon_heat.h. ANET2.DLL is packed: unpacked under Wine and exported as
out/ANET2U.DLL.c (WINETS2 too). Test aids in heavygear: HG_SHOT=<file.ppm>:<frame>, HG_VIEW=1, HG_MAP, HG_TD,
HG_SCRIPT steps (z <ms> waits), HG_NET_HOST / HG_NET_JOIN / HG_NET_PLAYERS / HG_NET_AUTO.

v0.7.4: no INFERRED left in src/ or tools/ (`grep -rn INFERRED src tools` is empty); what isn't done is in
KNOWN_ISSUES (AI cover walk, aircraft circling, obstacle probe, AI torso / heat, walk animation, MFD windows ...).
New files: src/ai_rules.h (tools/ref/gen_ai_rules.py), src/anim.c (the .3DI poses), src/particles.c (the particle
engine). RE_NOTES has a section for each.

v0.7.3 traced most of those: blocks (BLKX / BLK / ENDB matrices, ground snapping), PALG by time of day, multi-object
records (first object only), wreck swaps (0x1009d8f2), TEX alpha (DX5DRAW 4444 + blending), the objectives' order,
the story's progress (HGSHELL 0x100df8e4), vehicle slots (0x100425c9), zones by class (0x1005ab68), structures'
explosions, OBJ flags (0x800 = ground). Left INFERRED (31 lines; `grep -rn INFERRED src tools/heavygear.c`): the
AI (units.c - the AI files and state machine are a large untraced area), the HUD's message box / compass details,
version-1 vehicles' mount names (0x1004f86d), part -> zone for infantry and vehicles (the polygon hit, 0x100229d9),
guided shots' zone (0x8000 hits), beams' life, the shot streaks' look (particles 0x10084362), the cover search
(0x100279a3), the radio queue's voice limit.

The original's shell runs under Wine too (RE_NOTES "Working Wine recipe" step 7): the gear editor's view was matched
against it (refs: /home/claude/refs/b07_hunter.png). No INFERRED left in the shell / front end; not done: the debriefing's score list (0x100b9fad; the story's progress in 0x100df8e4 is traced), the editor's
list pictures, details, armour arrows, statistics panel.

Front end (after v0.6.0, no release yet): src/shell.c (screens, text, lists, maps), src/front.c (state machine and
actions), src/career.c (CHARINFO.HG1), src/brf.c, src/zlite.c. The screen builders are FUN_101377e4's cases; the
button actions are listed by tools/ref (see RE_NOTES "Shell"). Original reference captures of the shell: game
selection (pixel-exact with a game "Mike"), briefing of mission 1 (map and summary pixel-exact).

Sound (no release yet): src/audio.c (mixer, WAV, CD-image music), RE_NOTES "Sound". The music check: a track read
from HEAVYG2.mdf through the mixer is bit-exact with the image's sectors.

Sim (no release yet): src/sim.c drives the player's gear (RE_NOTES "Sim: the player's gear moving"); src/units.c
places the mission's vehicles from their GPS records and runs the AI (src/units.c, traced); src/gearmodel.c builds models from GDF
skeletons; src/gear.c reads weapons and damage zones; src/combat.c does weapon slots, shots and damage (RE_NOTES "The
other vehicles", "Damage zones", "Weapons and shots", "Damage"). Reference runs: start the original as in the Wine
recipe, find the window with xdotool, press keys (1-0 throttle, arrows), and log the gear with tools/ref/simlog.py
<pid> <samples> <seconds>; the vehicle table is 0x107f24c0 (count 0x10130754), each vehicle's gear block at +0x24
(zones at its +0x88, weapon slots at +0x84). `HG_MISSION=m01_scn1 heavygear <game>` skips the front end.
src/objectives.c runs the MTBL objectives and decides the outcome; src/structures.c the GT structures (RE_NOTES
"Objectives", "Structures"); src/hud.c draws the whole cockpit HUD from a hud_state the mission fills (RE_NOTES
"HUD"). The voices play through the original's radio queue (src/radio.c; the archive's WAVX sounds, RE_NOTES
"Voices"); the sim's sounds through src/sfx.c; crits in src/combat.c; the radio menus in tools/heavygear.c;
cutscenes in src/movie.c (FFmpeg optional; `HG_MOVIE=ZL_CUT01` plays one); the shell's Smacker animations in
src/smk.c + src/shell.c; the gear builder's sums in src/gear.c; the visible effects in src/effects.c (the pool from
gearsht1's HGXP records, RE_NOTES "Effects"); the wingmen in src/squad.c (the briefing's panel, src/front.c, hands
them to the mission); the gear editor's catalogue in src/garage.c. Next: the squad mates' replies, the AI (the GPS records' AI files), the
hit test on polygons for vehicles (the mission's objects already use world_ray), arm aiming, the AI's lock skill
(+0x1e4) and line of sight, the death
movies (state 0x4b, reached from B39's button 0x20); the gear editor's building (src/garage.c has the catalogue;
src/design.c + src/vdf.c are the original's design model and GDF writer; next the 3D view's camera, the parts'
pictures, armour, clear / details / delete piece; src/modelview.c
draws a model in the shell, RE_NOTES "The gear editor"); then
FUN_1005d75c's integration, the legs / torso split, skating / jump jets.

Next (shell): the briefing's pilot and gear panels (the game's squad), the barracks and gear editor, tour of duty,
the mission outcome's effect on the save, the story-end and death movies.

Next (viewer):
1. The cockpit model (the gear's arm and gun in view).
2. The rest of the HUD: message text (objective state), speed text, radar lines and contacts.
3. The gear's pitch on slopes, and the eye from the skeleton (other gears).
4. Ground edge alpha blending (material types 8-11). Animated materials.
5. Mission 10's left slope and mission 5's trees (KNOWN_ISSUES).
6. Then the shell, the sim, sound and music.
