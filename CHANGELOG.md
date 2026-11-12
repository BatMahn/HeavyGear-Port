# Changelog

Heavy Gear native port by BatMahn - https://github.com/BatMahn/HeavyGear-Port

## v0.7.0 (2026-11-11)

- **The sim's sounds.** Shots, hits and impacts are heard where they happen: the original's volume by distance, its
  sound delay, its stereo pan and its 16-channel priority rules (traced, RE_NOTES "Sound effects"). The player's gear
  starts up, beeps on weapon select and an empty trigger, chimes at navs. Every shot sound and hit sound comes from
  the weapon table; impacts from the effect table.
- **Critical hits** (traced, 0x1005b5c6): hits that reach a gear's internals roll on its zone's crit slots: engines,
  sensors, actuators, hydraulics, cockpit, weapons knocked out, zones destroyed. The gear's computer reports each to
  the player ("Leg actuator damaged", "Light autocannon destroyed" ...). Destroyed legs stop a gear.
- **The radio.** F1 opens the mission channel: its messages are the mission's radio objectives (call the air strike,
  call the Orca, request fire support ...) and sending one carries the mission on, as in the original. F2-F7 open the
  squad mates' channels with the original's six orders (their AI is a stand-in). F12 lists the objectives. Keys as
  in the game's GAMEKEY.MAP; the menus show in the message box.
- **Cutscenes.** The campaign's movies play between missions, read from disc 2 (a CD image of it in the game folder,
  or its VIDEO folder copied in, or wavmap.ini's DVDMoviePath). Indeo 5 needs FFmpeg's libraries at build time.
- **The landship's animations.** A Smacker decoder (src/smk.c, checked against FFmpeg on all 16 files) plays the hub
  screens' animations; the landship's doors play theirs before the screen changes, as in the original.
- **Effects to see.** Explosions, hit flashes on gears, muzzle flashes, gears' deaths and structures' explosions are
  drawn: the original's effect pool from gearsht1.BWD (the HGXP records: a model and a count per effect type), its
  lives, its limit of three of a kind within 500, the vehicle-hit offset (traced, 0x1001b3a1 / 0x1001c40d); the models
  are flat quads with gearxpl1's animated materials (EXPLOSION = WXEXPL01..14 ...), which now animate. An effect's
  sound plays only when the effect starts, as in the original (so bullets striking the ground are silent). A
  destroyed vehicle burns for 0x712 ticks, throwing debris that falls, bounces and settles (traced physics).
  Explosions harm what is near them (traced splash: structures always, vehicles from ground explosions).
- **Missile lock** (traced, 0x100bc861 / 0x10021a8d): a guided weapon's seeker acquires a target inside its band and
  a 16-degree cone, locks after 0x16a ticks with the original's tones, and only a locked missile homes; the reticle
  shows the lock. Guided mortars steer only once they come back down to the target's height (traced arming).
- **Wingmen.** The briefing shows the squad panel: the pilots the mission allows (Janus and Reeves in mission 1),
  their portraits, names and gears (the original's gear list: the side's stock gears within the game's tech level),
  chosen with the panel's arrows as in the original; a wingman killed in a mission is gone for good. The player's
  own gear comes from the briefing's third list too (kept in the saved game; a new story starts in the Cheetah); they fly the mission beside
  the player. Their channels have the original's six orders, accepted or refused by its rules, and each answers in
  his own voice (traced menus, orders, reply table and voice sets).
- **Gear editor, first part.** The briefing's "build" opens the gear editor: the category buttons show the parts and
  weapons the game offers (the original's lists by side, the design's model and the tech level) with their
  statistics as the original lists them (strength, speed, armour, struct, stealth, load, range, power ...), the name arrows
  cycle the designs, shown as their textured 3D models turning in the middle panel (with the shell's own material
  tables). Drag a part or weapon from the list onto the model: it goes where the original would put it (the
  nearest attach point that takes it; a torso starts a new design), and the design is written exactly as the
  original's editor writes its GDFs - the stock designs rebuild from their own data byte for byte. Accept saves it
  as the player's gear under the original's file naming.
- **The gear editor's 3D view, as the original draws it** (traced and checked against the original running under
  Wine): its camera and viewport, the design placed and turned 180 degrees to face you, the shell's point light (which
  turns with the model, as the original's does), the 8-bit look (XEL textures, the FOG luma table, the screen's
  palette) and the most detailed models; the hand grenade in the gear's hand. Left / Right turn the design as in the
  original. The details, delete-piece, clear and category buttons enable and disable as the original's, the
  selector opens on the game's gear and the category stays from your last visit.
- **The briefing's gear panel**: the player's gear turns in the panel with its weapons' names beside it. The default
  wingmen and gears now match the original's (Reeves and Janus in Hunters in mission 1), and the gear names in the
  squad panel are placed pixel for pixel.
- The hub's door animations are silent (the original never plays the WAVs named after them), a resting animation
  restarts by the original's own roll, and the story's last movie switches as the original's.
- **Your own gear designs** (GDF files in the game folder) join the gear lists after the stock gears, by the
  original's rules (side, tech level), for the player and the wingmen.
- **The AI engages as the original's does** (traced, 0x100ac524): enemies take on the player (or whoever shot them
  and targets them) within 15000 and in sight, instead of anything within their weapons' reach - mission 1 no longer
  opens with the infantry shooting from 2 km.
- Shells stop on the mission's rocks, walls and other objects (their polygons), not only on the ground and the
  structures.
- **The design's sums** (traced, 0x1004e6a0): gears' top speed, stealth, sensors, handling, weight and load from
  their equipment and options; the radar hides enemies whose stealth beats the player's sensors beyond the
  original's ranges.
- **Guided missiles and beams** (traced, 0x1001aec9 / 0x100baf31): missiles steer toward the shooter's target; beams
  strike along the aim over their reach.
- **The mission's start and end** as the original's: engines take 0x43e ticks and a random part to start (nothing
  moves before); "Exiting Mission Area..." shows for 0x16a ticks when the mission is decided or the player's gear is
  lost.
- **The debriefing's statistics and objectives**, counted as the original's (the sim's counters 0x1028cae0 and the
  shell's sums, 0x100e27c9).
- Debug keys moved: Ctrl+F12 ends a mission as accomplished (F12 is the objectives list now), ` the free camera.

- **Voices in the sim.** The spoken lines are the archive's English WAVs (type WAVX: avon* / avos* / avom*), found
  through the original's lookup, so nothing extra is needed from the CD.
  - Traced: the radio queue (HGSIM 0x10016aa0 / 0x10016e7d / 0x10016f28): 8 messages of one or two sounds, one
    speaking at a time, a pair played back to back only when both sounds exist, waiting duplicates dropped.
  - Objectives speak their success / failure voice once each (0x10067cfc, the voice even for hidden objectives),
    the mission its own when it's won or lost (0x100680d0), the name's fourth letter set by the player's side.
  - The gear computer announces enemies destroyed by kind ("gear / tank / vehicle / aircraft destroyed",
    0x10059e9d).
  - The mission waits for the last voice before it ends.
  - tests/test_radio.c checks the queue.

- **The HUD, complete for the cockpit view.** Traced from HGSIM's HUD code and checked against the running original:
  - the message box shows the current objective ("Primary: Destroy all Enemies"), pixel-for-pixel as the original,
    and "completed / failed" lines as objectives change;
  - the speed bar shows the throttle setting and the readout the speed in Kph (|velocity| x 6 x 1.38);
  - the damage display colours each part by its zone's damage (green, yellow, red, black) through the original's
    colour remap tables; destroyed weapons show their damaged icons;
  - the radar: a top-down view on the gear with its view lines, the navs and the vehicles by side, the target and
    the selected nav highlighted;
  - the reticle at the aim point's projection, its shape by the selected weapon and its state (reloading, locked);
  - targets (T / Shift+T, F friendly, Q under the reticle, the sim's INPUTW.MAP keys): the four corner brackets in
    the side's colour, or an arrow at the screen's edge, and a compass marker;
  - navs (N / Shift+N): the animated marker, "Way Point:Echo" and its range in metres or km, a compass marker.

- **The sim, third part: objectives and the mission's outcome.** Missions now end the way the original's do: won,
  lost, or out of time, from the mission's own objective tables.
  - Traced (HGSIM 0x10066090 .. 0x1006932f): every group's MTBL objectives with their conditions, time limits,
    mandatory flags and actions; targets registered from the BTAG blocks (navpoints, vehicles, structures, the player's
    own record); the step rules for destroy, defend, reach, go to, leave, hit, waits and the actions (win, lose,
    complete / fail / reset an objective, show / hide); the group's outcome. Mission 1 plays out as its data says
    (Echo opens the patrol and the fleeing Antelope, straying 240000 from the start loses, losing the gear loses,
    destroying the enemies and the base wins).
  - Structures (GT chunks): tents, radio masts, flag poles and the rest take the shells' power off their hit points
    and give way to their wreck models.
  - The AI groups follow their objectives: a group whose current objective is a go-to drives to its navpoint
    (the steering INFERRED), so patrols and the Antelope's flight move.
  - The objectives' "completed / failed" lines go to the console for now (the HUD's message line isn't drawn; the
    voices came later, see above). The mission ends 5 seconds after the outcome; F12 / Esc still end it at once.
  - tests/test_objectives.c checks the rules on a made-up table.

- **The sim, second part: the other vehicles, weapons and damage.** Mission 1 now has its enemies and allies, and
  both sides shoot.
  - Placement, traced and checked against the running original: every GPS vehicle at its group's start navpoint and
    formation slot (FTBL formations, the group's formation from the STAR / GRP chunks), on its sole height (the Orca
    at its 4000 cruise height): all 11 of mission 1's vehicles at the original's positions and headings.
  - Models from the GDF skeletons (the parts at their links less their pivots; the gears' 'g' names use the next
    detail slot, as the original's).
  - Damage zones, traced: armor and internal points per zone from the GDF's equipment records and the sim's equipment
    and option tables; every zone of mission 1's Hunter, Jager, Antelope, Redjacket, Orca and infantry matches the
    original. The player's Hunter has 6 x the internal points, as MEASURED in the original.
  - Weapons, traced: the weapon table (burst, interval, reload, speed, life, gravity, power, multiplier, chip damage,
    mode) regenerated from HGSIM.DLL by tools/ref/gen_gear_tables.py; the slot timing; shells flying at their speed
    with their gravity for their life; the penetration / armor / internal damage rule and what a destroyed zone does
    (arms and weapons come off; head, torso, backpack destroy the vehicle). Vehicles' weapons and rounds from version 1
    GDFs, checked against the original's slots.
  - The player fires with Space and picks the next weapon with Enter (KEYBOARD.MAP's keys); the HUD's weapon panel
    shows the selection and the rounds left. Shots draw as streaks (INFERRED look).
  - The AI is a stand-in (INFERRED): armed vehicles turn toward the nearest vehicle of another side within reach and
    fire, aiming with some spread. The player's gear destroyed ends the mission as failed.
  - tests/test_combat.c checks the damage rule and the slot timing; test_data checks the zones against the
    original's.

- **The sim, first part: driving the player's gear.** In heavygear's missions the player now drives the Hunter
  instead of flying a camera: keys 1-9 / 0 set the throttle as the original's do (1 stop .. 0 full), W / S step it,
  the arrows turn, a mouse drag looks up and down; F2 keeps the old free camera. The cockpit eye rides on the gear and
  the gear follows the ground.
  - Traced (HGSIM 0x10060491): the sim's 5 ms tick, the throttle-to-speed rule (a full throttle is 1/64 of the gear's
    top speed per tick) and the top speed from the gear's speed points (x 47.1744; the Hunter's 7 give 330.221).
  - MEASURED in the running original (tools/ref/simlog.py samples the gear's state through the process memory while
    xdotool presses keys): the speed, velocity and turn lags and the turn rate (85 degrees per second at full speed).
    With the same inputs the port's path stays within 80 units of the original's over 4,000, and the end frame shows
    the same view.

- **Sound.** A mixer on SDL3 audio (44.1 kHz stereo) for the game's WAV samples and the music:
  - Music, traced (HGSIM 0x10015cdc / 0x10015a38): a mission's record names a CD track ("track2" = CD track 2),
    played looping. The port reads the track straight from a CD image in the game folder (.mds / .mdf or .cue / .bin),
    from music/trackNN.wav, or from a wavmap.ini, which the original also reads (its code for a DVD drive). Checked bit-exact against the
    image's track. hgview and heavygear's missions play it (`HGVIEW_MUSIC=0` silences hgview).
  - The front end's screen loops, traced: each screen's sound from its builder (FUN_100277c5's fourth argument) into
    the shell's WAV table (AFXSHABK ... AFXSHAVB), looped at -15 dB (FUN_1003293b); popups keep the one playing.
  - The briefing's "briefing" button plays the mission's narration: each part's WAV at its start time from the BRF
    (FUN_10072f4e); pressing it again stops it.

- **The front end (HGSHELL), first part.** A new program, `heavygear`, runs the shell's screens and launches the
  mission viewer from the briefing:
  - Screens are drawn from the shell's picture table (1,691 records, regenerated from HGSHELL.DLL by
    tools/ref/gen_shell_tables.py): pictures, buttons with their rest / highlighted / pressed art, popups at their
    placement record.
  - The shell's state machine, traced (FUN_101377e4 and the screen builders): state n is screen Bnn; buttons carry the
    builders' actions (go to a state, set a shell variable, open / close a popup, launch). The story path works: main
    menu -> game selection (new game, delete with its confirmation, the game and mission lists) -> the landship ->
    briefing (with the situation map) -> launch -> debriefing -> continue to the next briefing.
  - Saved games: CHARINFO.HG1 is read and written in the original's format (zlib; the port writes stored blocks).
    A new story game starts with the original's values (FUN_10110eaa); its missions-reached list and current mission
    are kept.
  - Text, traced: the shell's fonts, its left-aligned / centred layout with the original's tab stops and line breaks
    (FUN_10032e5f, FUN_10033a59), lists with the selection bar (FUN_1000364b), the text entry (FUN_100132f0).
  - Briefing: the mission summary from the BRF and the campaign map with the story's front line for the mission
    (FUN_100af954, the front-line table at 0x101af0a0), pixel-exact against the original; the situation screen with
    the BRF's situation text and map markers; the debriefing's title and aftermath text.
  - Missions come from the campaign table (0x101ace60). In the viewer, F12 ends the mission as accomplished and Esc as
    failed, until the sim reports outcomes.

## v0.6.0 (2026-11-10)

- **Weapons, traced** (HGSIM 0x1004e6a0): a gear's weapon slots come from its GDF equipment records (kinds 8..16),
  with the weapon type and ammunition from the sim's equipment tables (src/gear_tables.h, regenerated from
  HGSIM.DLL by tools/ref/gen_gear_tables.py). The Hunter and the Jaguar match the running original.
- **HUD weapon panels** (0x100b9e60): one per slot with its icon, ammunition and short name, the selected one framed,
  each clipped to its window. They match the original pixel for pixel at mission 1's start.
- hgview: `HGVIEW_GEAR=<gdf name>` picks the player's gear (default gdhuntea, the Hunter).
- make check also checks two gears' weapon lists against the original.

## v0.5.0 (2026-11-10)

- **HUD** (HGSIM's HUD windows, traced; the layout read from the running original): SHP 1.10 shapes and FNT fonts are
  decoded, and hgview draws the compass (the ring turning with the heading, the reading in the box), the message box
  frame, the damage display, the speed bar and the weapon reticle. H toggles it; `HGVIEW_HUD=0` hides it in shots.
- **Sky, traced** (0x1008dff3 / 0x1008da61):
  - 3D: the cloud layer is the game's quad around the eye, reaching the horizon, with its tiling and its drift
    (skyspeed / 46336 per 5 ms tick). It replaces v0.3's plane matched by eye.
  - Both looks: the horizon band is 128 high at 640 x 480 (was 30 in 3D, a quarter of the screen in software).
- make check also decodes every SHP and FNT record.
- Ground materials 8-11 were thought to blend over the ground; traced, they are opaque ground (RE_NOTES).

## v0.4.0 (2026-11-10)

- **Player start, traced.** The viewer now opens in the cockpit, where the game puts the player:
  - The player's squad is group 0. The sim puts each group's leader on the navpoint its first objective names, facing
    that navpoint's heading (HGSIM 0x10066a68 -> 0x1004271a). Navpoints (NAVP), gear placements (GPS), objectives
    (MTBL) and BTAG blocks are now read.
  - Checked against the running original in missions 1, 2, 5 and 10: the same position and heading (make check tests
    them when given the game folder).
  - Headings: the port's yaw is the engine's heading. v0.3's "engine = port - 90" was wrong: the test USERSTAR.BWD
    was being rejected, so that frame showed an infantry unit's view.
  - The cockpit view: a 90-degree field of view (the projection, 0x100b8496), the eye 171.3 above the gear and 48.3
    ahead, the gear 245.3 above the ground (both measured on the running original, for the Hunter).
- **SOFTWARE look: lighting, traced** (HGSIM 0x1004bad6, I76DRAW Render). Each vertex gets a luma row from:
  - the ART record's LITE light and ambient, scaled by SKYGND's globalintensity
  - fog toward the upper rows when fogswitch is on, or the LITE "shadedist" darkening beyond a distance
  - I76DRAW's row mapping: its table is handed over upside down and indexed by (1 - intensity / 255) * 31.
  Hills now fall into shadow as in the original (missions 2, 5 and 10).
- SOFTWARE look: texel index 255 is transparent, as in I76DRAW's rasterizers (trees, fences).
- **3D look: lighting, traced** (the same function): the LITE light adds `gl`, scaled by hardwareintensity; `l1` /
  `l2` count at 255 / 128 of the dot product; the dot product is scaled by 128 (traced, was INFERRED 127).
- Fog and the far distance: the far distance is the VIEW chunk's distance * 2.5 (traced), and fog is measured along the
  view depth, as the sim does.
- tools/ref: `mkuserstar.py` (a USERSTAR.BWD that the sim accepts: the BWD header's third word is the largest chunk
  size) and `origstate.py` (the player and eyepoint, read from the original running under Wine).

## v0.3.1 (2026-11-10)

- The original's Direct3D look now runs under Wine too (Mesa OpenGL in the virtual display), giving a second reference
  frame.
- 3D look, matched against it:
  - bilinear texture filtering
  - the `skytexture` cloud layer drawn as a plane over the whole sky, fading into the fog colour toward the horizon.
    Its plane height and tiling are matched by eye (INFERRED).
  - The flat palette sky colour and the horizon band are now SOFTWARE look only.
- hgview: L toggles the SOFTWARE / 3D look.

## v0.3.0 (2026-11-10)

- The original game now runs under Wine for reference frames. The recipe is in docs/RE_NOTES.md; the first comparison
  is in docs/COMPARISON.md.
- **SOFTWARE look** (hgview: `HGVIEW_LOOK=software`). It works like the original's 8-bit renderer (I76DRAW):
  - XEL texels go through the mission's luma table (its LTBL chunk) and palette (the first PALG entry).
  - The luma row comes from the fog visibility. Clear ground uses rows 19-20, calibrated on the original's mission 1
    frame (ground mean 116,64,33 there, 114,63,31 in the port).
  - Sky: the `horizontexture` cloud band, a quarter of the screen tall (measured on the frame), on top of
    `skycolor[0]`.
- hgview: camera eye height, pitch and FOV options for matched shots (`HGVIEW_EYE`, `HGVIEW_PITCH`, `HGVIEW_FOV`).

## v0.2.0 (2026-11-09)

- Objects:
  - Placed objects show their standing state only. The destroyed states (`oa...` remains, `oc...` collapse frames)
    are left out until destruction is implemented. Before, all three were drawn on top of each other.
  - Detail levels: each `o...` object's SDF record lists its levels and the distance each one switches in (HGSIM
    0x100c1152, SDF `+0x14c`).
  - Child nodes are placed from their parent node.
- SKYGND.PAR: read with the game's own field list (0x1008ceab), from the game folder as the sim does. Patch 1.2's
  copy is used when the port supplies the patch.
- 3D lighting from the game's code (0x10042520 / 0x1009250c): per-vertex `hambient`, plus the two directional lights
  `l1` / `l2` (colour * intensity >> 7). This replaces v0.1.0's placeholder sun.
- Fog (0x1004b93a): linear from `density` Z to the far distance, toward `fogcolor`.
- Horizon (3D): sky and ground colours from the mission palette, and the `hardwarehorizon` band on the horizon line.
- The rasterizer now carries RGB light and fog per vertex.
- RE_NOTES: the SKYGND fields, lighting, fog, horizon, palettes, luma tables, and the two software plugins (RENDDRAW
  16-bit, I76DRAW 8-bit).

## v0.1.0 (2026-11-09) - first preview

- New port, written from the decompiled Heavy Gear 1.2 binaries (`HG.EXE`, `HGSHELL.DLL`, `HGSIM.DLL`, renderer plugins).
  The MechWarrior 2 port's findings are used only for comparison (docs/RE_NOTES.md).
- Resource archive:
  - HEAVYG.MAP / HEAVYG.PRJ read as `sysfile.c` does (HGSIM 0x100a4a91).
  - PKWARE DCL implode and zlib decoders written for the port.
  - The game's lookup order from `syscache.c` (0x100a2bc9): `Patch\` folder, loose files, archive, each trying the
    language variant first.
- Models (WTB): HG's float layout (HGSIM 0x100c5e73), plus MechWarrior 2's integer layout for the 45 tiles still in it.
- Textures: TEX (palette + indices, the hardware renderers' format) and XEL (8-bit, the software renderer's).
- Materials:
  - polygon label -> TLT -> MLT -> texture (HGSIM 0x100bef4c)
  - untextured material colours
  - first frame of animated materials
- Missions: the BWD INCL tree, ground tiles, BLKX blocks, OBJ placement (16.16 degree rotations) and parent / child
  nodes. Block objects stand on the ground: INFERRED, docs/RE_NOTES.md.
- hgview: a mission viewer on the port's own CPU rasterizer (z-buffer, perspective-correct texturing). Its lighting is a
  placeholder (PORT PREVIEW) until the game's own is traced.
- Patch 1.2's data files are included in `gamedata/patch12/` and used automatically.
- Builds: macOS, Linux, Windows (MinGW-w64), with GitHub Actions releases on `v*` tags.
