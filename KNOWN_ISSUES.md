# Known issues (v0.6.0 + front end)

- Not fully playable yet: the player's gear drives and fights, the objectives decide the outcome, the radio calls and
  orders work, the cutscenes play (with FFmpeg), but the AI is a stand-in and the gear can't skate, crouch or jump.
- Objectives:
  - The registration needs the objective's state at 7 (0x10066794); its setter isn't traced: every objective is
    taken to accept targets at load.
  - Go-to objectives with vehicle or object targets (rather than navpoints) never complete.
  - The mission ends as the original's does ("Exiting Mission Area...", 0x16a ticks after the outcome or the player's
    gear's loss, 0x1003ad72), except that the port lets the last voice finish (INFERRED) and doesn't switch to the
    outside view when the gear is lost.
  - The mission's other objects (rocks, walls, trees ...) stop shells by their polygons (INFERRED: the original's
    scene test, 0x100229d9, isn't traced for which objects it skips); the strike counts as on the ground.
  - The radio menus (F1 mission channel, F2-F7 squad mates) show as lines in the opened message box (INFERRED look: the original's menus are their own windows, FUN_100636da, not traced). Digits pick an
    item, as in the original (0x10063231).
  - The wingmen come from the briefing's squad panel as the original's (traced, src/squad.h: the BRF's pilot mask,
    the two pilot lists, the launch's records and voices); the port hands them to the sim directly instead of writing
    USERSTAR.BWD. They stand at the group's formation slots beside the player. Their gears come from the original's
    gear list (the stock gears of the side within the game's tech level, traced: a new story offers the Cheetah and
    the Hunter), then the game folder's own GDFs (findfirst's order: by name, as NTFS gives it). A list starts on
    its last gear when the folder has no GDF, else its first (traced: a new story's squad flies Hunters, as the
    original shows). The player's own gear is the game's
    (the briefing's third list, kept in the game record as the original's); the
    port flies that GDF directly instead of copying it to USERGEAR. `HG_MISSION` runs use the Hunter (`HG_GEAR` picks
    another). A wingman killed in a mission dies in the game at the
    debriefing's continue, as the original's (traced, 0x100df783), and isn't offered again.
  - A squad mate channel has the original's six orders (0x101063b4: attack, flank, defend, scout - to the player's
    nav -, get to cover, return to formation), taken or refused by the original's tests and answered with its reply
    lines (0x1000f2b0 / 0x10017566, traced), in each pilot's voice set (from his record, as the original's; the
    second wingman's voice takes his number unadjusted, as 0x100ff1e6 writes it). The
    stand-in AI carries the orders out (INFERRED): attack / flank / defend close in on the target, scout drives to the
    nav, get to cover holds, return to formation follows the player. The cover search (0x100279a3) isn't traced.
- Combat:
  - The AI (a stand-in around traced rules): an enemy engages as the original's AI state machine does (0x100ac524:
    the player, or a vehicle that shot it and targets it, within 15000 and in sight); the player's own group engages
    any hostile within 15000 in sight (INFERRED: their AI files aren't traced). Sight is tested against the ground only
    (INFERRED for the scene ray, 0x1000510e / 0x10004d60). Vehicles turn toward their target and fire within the
    weapon's band (0x1000502f) with a fixed aim spread; the original's other states (evading shots, 0x100abf2d, the AI
    files' patrols and scripts) aren't done; with no
    enemy in reach they drive toward their group's go-to navpoint (from the objectives, traced). The original's AI
    files, formations on the move and skill aren't traced. Gears move at their design's top speed (traced); version
    1 vehicles (whose builder isn't traced for speed) at 1.5 x the Hunter's, aircraft 3 x, infantry a fifth. Every
    vehicle waits for its engine to run first (0x43e ticks and a random part, traced).
  - Hits strike the parts' model boxes, turned with the vehicle; the original tests the polygons (0x100229d9).
    Infantry and vehicle parts take their zone from the segment type (INFERRED pairing from the gears').
  - Hand weapons fire from beside the torso: the original aims the arm (the rifle hangs muzzle-down at rest).
  - The player aims along the view at the first vehicle on it, else 150000 ahead (the aim distance's setter isn't
    traced).
  - Beams (lasers, rail guns, particle accelerators) are tested at once along the aim over their reach; the original
    keeps the non-laser ones up for their life, testing each tick (here one test).
  - Guided missiles steer as traced (0x1001aec9). The player's seeker is traced (0x100bc861: the band, the 16-degree
    cone, 0x16a ticks to lock, the tones): fired unlocked, a missile flies unguided. The AI's missiles always home:
    the original rolls each firing against the vehicle's lock skill (+0x1e4, 0x10006672; kinds 5 and 8 always lock),
    whose source isn't traced. A missile within 100 strikes the torso
    (INFERRED zone).
  - Splash damage is traced (0x1001c75c / 0x1001c96a): every frame an explosion harms what is near by the frame's
    ticks x its power, truncated, so like the original's it depends on the frame rate; it reaches vehicles only from
    ground explosions (0x2c / 0x2d) unless the option at 0x1014a564 + 2 is on (no options screen yet: off,
    INFERRED). Other vehicles' size (+0x53c) is taken as the model's standing height (INFERRED); structures' bounding
    spheres are the sim's (0x10003326). Wrecks stay where they fell but don't fall over. Their burning throws debris (traced, 0x100533d2 /
    0x1001ce0a / 0x1001ef21), with the port's own random numbers (the original's tables come from its C library's
    rand() and an unknown seed); the chunks' turning and size are the original's (traced).
  - The effects (explosions, hit flashes, muzzle flashes, gear deaths, structures' explosions) come from the
    original's pool and rules (traced, src/effects.h), turned to face the eye, drawn unlit and unfogged, their
    animation one pass (or looping) from their start (traced). Not done: their alpha (200 of 255; drawn opaque), and
    effects of a type sharing one animation (each runs its own). The muzzle flash shows at the shot's start (the original fixes it to
    the weapon on the vehicle, 0x100bd13f); the ground's spray of particles (0x1008514d) isn't done. The ground is
    taken to be what the original's hit test flags 0x800 (INFERRED).
  - Critical hits (traced, 0x1005b5c6, with the head's equipment flags): the arm-actuator, torso-twist and power-loss
    effects (aim, twist, the 0x21f-tick stun) aren't modelled; the speed, sensor and weapon ones are. A crippled gear
    stops (the original's fall and crawl aren't done).
  - Some version 1 records' internal bonuses aren't added.
  - The player's internal points x 6: traced as the difficulty (0x1014a564 + 5: x 12, x 6, x 1); the port uses the
    middle setting (no options screen yet) for the player's group (the original's test, the vehicle record's +8, read
    as the group: INFERRED).
  - The builder's sums (speed, stealth, sensors, load, internal factor) are traced from the version 2 gears' records;
    version 1 vehicles have no stealth (always on the radar).
- Sim (player gear):
  - The movement uses the traced throttle / top-speed rule with lags MEASURED from the original; FUN_1005d75c's real
    integration (its 45-tick acceleration, gravity, collisions with objects, FUN_10056c90) is still to trace.
  - Legs and torso are one: the original turns the torso separately (its twist, FEET_TO_TORSO), and the legs carry
    the motion.
  - Skating, crouching, jump jets, slopes (the gear's pitch) and the walk animation aren't done.
  - The HUD's km/h scale is one MEASURED point (40 km/h at the Hunter's full speed).
- Sound:
  - The sim's sounds (src/sfx.c, traced): shots, impacts, hits on vehicles, the computer's reports, start-up, weapon
    select and empty, nav reached, the radio menus. Not yet: footsteps (no walk animation), skating, jump jets
    and the wingmen's replies. The placed sounds are panned in stereo from the
    original's DirectSound pan (it used A3D / DirectSound 3D when present).
  - The impact sounds' quiet flag reads 0x101316ec, taken as the cockpit view (INFERRED).
  - An effect's sound plays only when the effect starts (traced): kinds with no pool (bullets on the ground, among
    others) make no impact sound, as in the original.
  - The hub animations are silent, as the original's: their SMKs have no audio and nothing in the binaries plays the
    archive's b15d1smk / b15d2smk / b15ucsmk WAVs.
  - Many objectives name failure voices ("avon009F", "avom033F") that the archive doesn't have; the original plays
    nothing for those, and neither does the port. The mission's out-of-time voice, "BET68", isn't in the archive
    either.
  - The voices' queue priorities (the sound table 0x107f9780) are taken as equal for every voice (INFERRED): first
    come, first served.
  - The original also looks for spoken lines as `<name>.sfl` in a KEATING folder (game folder or CD root,
    0x1006a65c), which neither disc has; the port doesn't read .sfl.
  - No volume settings yet (the original's options screen); music and effects play at full volume, the screen loops at
    the original's -15 dB.
  - The original's screen loop buffer holds the whole WAV file, header included (a click each loop); the port plays
    only the sound data.
  - Music WAVs (music/ or wavmap.ini) play at 44.1 kHz whatever their own rate; MP3 / OGG / FLAC rips aren't read.
- Front end (`heavygear`):
  - Only the story's screens have their actions: main menu, game selection, the new-game and confirmation popups,
    the landship, briefing, situation, debriefing, and the barracks' exit. Tour of duty, training, multiplayer, the
    barracks and options are drawn but do nothing yet.
  - The gear editor (the briefing's "build") is the original's design model: items dragged from the list onto the
    model go on the nearest attach point that takes them (a torso starts a new design), and the GDF is built as the
    original writes it (traced; the stock designs rebuild byte for byte but the session leftovers, test_data). Accept
    saves it under the original's naming as the player's gear; clicking a part selects its item for delete piece;
    clear asks and empties the design. The 3D view is the original's (traced camera, light and 8-bit look; checked
    against the original under Wine); Left / Right turn it 7 degrees a pass of the loop, as the original, so its
    speed is the frame rate's. The buttons enable as the original's (details, delete piece, clear, the categories).
    Not yet: the parts' pictures in the list, the selection's highlight, details, the armour and thickness arrows,
    the statistics panel, accept's low-speed question.
  - The Smacker animations play on B02, B09 and the landship B15 (traced tables and modes); a resting one restarts
    as the original's, by a 1-in-32768 roll each pass of the loop once 5 seconds have passed (so its chance per
    second is the frame rate's).
  - The briefing's squad panel: the wingmen's portraits and names (their lists' arrows work, the default pair as the
    original's: Reeves, Janus in mission 1), their gears' names (their arrows work) and the player's, and the
    player's gear turning with its weapons' names (traced; the names pixel-exact against the original). Not done:
    a gear above the game's level drawn dark under its message. The debriefing's statistics (Vehicle Kills,
    Hit Rate, Hits Taken, Squad-mate Kills, Squad-mates Killed) are counted as the original's (traced: the sim's
    0x1028cae0 counters, HGSHELL 0x100e27c9's sums); the squad's slots 1 and 2 are taken as the player's group's
    vehicles in GPS order, and the victim flag 0x1400 the mates' kills also ask isn't traced. The statistics are
    drawn as the original's (one text, a line each, traced). Not done: the objectives' score list (In Progress /
    Complete / Incomplete, Mission Bonus, Kill Bonus, Death Penalty, Mission Total; FUN_100b9a09 and the scoring
    0x100df8e4): the port lists the objectives in its rectangle. The font has no '%': the hit rate's sign shows as
    the font's glyph.
  - The briefing's narration (BRF WAVs, the "briefing" button's playback with units on the map) isn't done.
  - Cutscenes play when the port is built with FFmpeg (Indeo 5 video); without it they're skipped. They're shown
    twice their size on black at the screen's left, centred down, as the original's DirectShow window (traced,
    0x10022dce); a key or the left button skips them. The
    story's last movie switches as the original's (entry 0x20 -> ZL_CUT16, 0x100ffd56, traced); the death movies
    (NDEATH / SDEATH, the pilot's death isn't modelled) aren't wired.
  - A mission's outcome (and what the original updates in the save from it) isn't traced: the port advances the
    story's current mission on success (INFERRED). Ctrl+F12 / Esc in a mission stand for accomplished / failed.
  - The save is CHARINFO.HG1 in the game folder, as the original's. The last-selected values in its header are kept,
    not updated.
  - B20's accept and delete do nothing without a game selected (INFERRED from their disabled art).
  - The situation screen's marker icons are drawn at their hot spot (INFERRED).
- HUD: all the windows of the original's cockpit view are drawn, matched against the running original at mission 1's
  start (message box, compass, speed, damage, weapons pixel-for-pixel or near). Not yet:
  - the radar's sensor rule is traced (stealth minus sensors picks a range, 0x1004e6a0 sums), yet in the reference
    shot of mission 1 far Hunters hide that the rule shows (unexplained); the radar's view lines are solid where the
    original's look dotted; the radar is clipped to the compass ring
    (INFERRED);
  - the nav label's offset from its marker (INFERRED), the MFD windows (rear / weapon views);
  - "completed / failed" lines show in the message box's third line for 5 seconds (INFERRED place);
  - the target / nav cycling order (the vehicle and nav tables' order, not traced);
  - in hgview the player's gear is the Hunter unless HGVIEW_GEAR names another;
  - South gears (the HS shapes) and the low-resolution set aren't selected yet; the North set is always used.
- No cockpit model yet (the gear's arm and gun in view).
- Player start:
  - The eye's offset from the gear (171.3 up, 48.3 ahead) and the gear's standing height (245.3) are measured on the
    running original for the Hunter, not traced through its skeleton. Other gears take their skeleton's EYE_ point
    raised by the Hunter's difference from its own (15.7) and their soles' depth (INFERRED).
  - On a slope the gear pitches with the ground (mission 10 starts pitched 3 degrees). The port's view stays level.
  - The objective state byte that 0x10066794 checks (== 7) is not traced; objective 0 passes it in every mission tried.
- SOFTWARE look:
  - Point lights aren't done. The fog's height term (`density` Y) isn't done.
  - Untextured (colour) materials keep their RGB colour (the luma path for them, row = L * colour >> 10, isn't done).
  - The intensity's low three bits (| 7) are calibrated on mission 1's ground: the traced software path leaves them
    clear, which lands one table row darker than the original's frame. The difference isn't traced yet.
  - Mission 1: near the start the original's ground shows more red, orange and grey-green texels than the port's
    (a different ground texture or mapping there; the luma row is the same).
  - Mission 10: near the start the original shows a smooth snow slope on the left where the port shows a steep ridge.
    The terrain there is still to compare.
  - Mission 5: the trees on the ridge sit lower and smaller than in the original.
- 3D look:
  - Mission 1: the shaded hill on the left is darker in the original (about 0.77 of the port's).
- The sky colours' palette (PALG's first) is INFERRED.
- Animated materials loop on the clock from the mission's start (the original starts each when it loads it; the
  phase may differ). An effect's runs from its start (traced).
- Software look: patch 1.2's effect textures have no XEL, which the original's software renderer would leave
  undrawn; the port draws their TEX.
- Mission 27's ground uses 45 tiles in MechWarrior 2's older layout. The game's float parser would reject them (their
  checksum fails), so the game probably doesn't draw them. hgview does.
- The OBJ / BLKX fields are read from the data and match the screen, but the sim's object handler (HGSIM 0x100c1152)
  is not traced field by field yet. Block objects standing on the ground is INFERRED.
