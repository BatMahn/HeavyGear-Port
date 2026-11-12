# Session handoff (2026-11-11, v0.7.0)

Repository: https://github.com/BatMahn/HeavyGear-Port (BatMahn; commits as `BatMahn <65992136+BatMahn@users.noreply.github.com>`).

See the top-level handoff notes for the workspace rebuild. New in this session:
- Ghidra exports as before. Reference frames: missions 1, 2, 5, 10 (8-bit) and 1 (Direct3D).
- The Wine recipe's USERSTAR.BWD needs the header's third word = the largest chunk size, and the player's record has
  group 0, `+5` = 1, `+6` = 0 (tools/ref/mkuserstar.py). Without that the sim drops the file and shows vehicle 0.
- Wine relocates HGSIM.DLL; read its base from /proc/<pid>/maps (tools/ref/origstate.py does).

The original's shell runs under Wine too (RE_NOTES "Working Wine recipe" step 7): the gear editor's view was matched
against it (refs: /home/claude/refs/b07_hunter.png). No INFERRED left in the shell / front end; not done: the debriefing's score list (0x100b9fad, the scoring 0x100df8e4, front_mission_over), the editor's
list pictures, details, armour arrows, statistics panel.

Front end (after v0.6.0, no release yet): src/shell.c (screens, text, lists, maps), src/front.c (state machine and
actions), src/career.c (CHARINFO.HG1), src/brf.c, src/zlite.c. The screen builders are FUN_101377e4's cases; the
button actions are listed by tools/ref (see RE_NOTES "Shell"). Original reference captures of the shell: game
selection (pixel-exact with a game "Mike"), briefing of mission 1 (map and summary pixel-exact).

Sound (no release yet): src/audio.c (mixer, WAV, CD-image music), RE_NOTES "Sound". The music check: a track read
from HEAVYG2.mdf through the mixer is bit-exact with the image's sectors.

Sim (no release yet): src/sim.c drives the player's gear (RE_NOTES "Sim: the player's gear moving"); src/units.c
places the mission's vehicles from their GPS records and runs the stand-in AI; src/gearmodel.c builds models from GDF
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
