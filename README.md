# Heavy Gear (1997) - native port

https://github.com/BatMahn/HeavyGear-Port - by [BatMahn](https://github.com/BatMahn)

A native reimplementation of Activision's Heavy Gear, rebuilt from the game's own code. **Heavy Gear 1.2** is the
reference: the retail disc's `HG.EXE`, `HGSHELL.DLL` and `HGSIM.DLL` with Activision's 1.2 patch applied, decompiled and
traced. Heavy Gear runs on a later generation of the MechWarrior 2 engine. The MechWarrior 2 port's findings are used to
compare against, never as code: every format and value here is taken from Heavy Gear's own binaries and data.
Portable C99 with SDL3; builds on macOS, Linux and Windows. You need your own copy of the game (see FOLDER_SETUP.md).

## Status (2026-11-11, v0.7.0 - early preview)

**Not fully playable yet.** It loads the game's data, runs the story's front end (briefing, squad, gear editor) and
lets you fight a mission. See CHANGELOG.md for what each release added.

- **Data:**
  - HEAVYG.MAP / HEAVYG.PRJ, including the PKWARE-imploded and zlib-packed records. All 19,545 records unpack, with
    decoders written for the port.
  - The game's file lookup order: patch folder, then loose files, then the archive, with language variants.
  - Models (WTB): all 10,345, including the 45 terrain tiles still in MechWarrior 2's layout.
  - Textures (TEX and XEL): all 2,627.
  - Texture and material tables (TLT / MLT).
  - Missions (BWD): the INCL tree, ground tiles, placed objects and their hierarchy.
- **hgview:** a mission viewer drawn by the port's own software rasterizer. It opens in the cockpit, where the game
  puts the player (traced and checked against the original), in the 3D look:
  - the game's textures, ground and objects, with SDF detail levels
  - the game's vertex lighting (traced), SKYGND.PAR's fog, horizon and cloud layer (traced)
  - the HUD: compass, damage display, weapon panels (the gear's weapons, traced), speed bar, message box and reticle
    (H toggles it; `HGVIEW_GEAR=gfjaguar` picks another gear)
  - the SOFTWARE look (`HGVIEW_LOOK=software`, or L): the 8-bit renderer's texels and luma table with its traced
    lighting and fog, and its cloud band, matched against the original running under Wine
- **heavygear:** the front end (HGSHELL's screens and state machine, traced): main menu, game selection with saved
  games (CHARINFO.HG1, the original's format), the landship, the briefing with its campaign map and summary, the
  situation map, the squad panel, the gear editor (the original's design model and 3D view), launching the mission and
  the debriefing. See KNOWN_ISSUES for what's missing.
- **Sound:** the mission's music from the game CD's tracks (read straight from a CD image, or WAV rips; see
  FOLDER_SETUP.md), the front end's screen sounds and the briefing narration; in missions the weapons, hits and
  impacts placed around you, and the spoken lines (objectives, mission results, the gear's computer) from the
  archive's English voices.
- **Cutscenes and animations:** the campaign's cutscenes (disc 2's VIDEO folder, read from a CD image of disc 2 or a
  copy of the folder; needs a build with FFmpeg) and the landship's Smacker animations.
- **Patch 1.2:** its data files (new textures, mission tables, particle definitions, SKYGND.PAR) are in
  `gamedata/patch12/` and are used automatically.

Planned looks: **SOFTWARE** (the original's 8-bit software renderer: XEL texels through its lighting tables) and **3D**
(the hardware renderers' textures, the 1.2 patch's textures, particles and lighting), chosen with one in-game setting.

docs/RE_NOTES.md holds everything traced so far, with addresses, and how it compares with MechWarrior 2.
KNOWN_ISSUES.md lists what is open.

## Build

Needs a C99 compiler, make, pkg-config and SDL3 3.2 or newer (development files). For the cutscenes (Indeo 5 AVIs)
also FFmpeg's libavformat / libavcodec (development files); the Makefile uses them when pkg-config finds them
(`make MOVIES=0` leaves them out).

```
make            # heavygear (front end), hgview (the viewer), hgprj (archive tool), test_data
make check      # unit tests; add GAME=/path/to/game to also parse every model and texture of your copy
```

On macOS: `brew install sdl3 ffmpeg`. Windows builds are cross-compiled with MinGW-w64 (see the Makefile header). Tagged
releases (`v*`) are built by GitHub Actions on https://github.com/BatMahn/HeavyGear-Port: macOS (Apple Silicon), Linux x86_64 and Windows x64, without FFmpeg (no
cutscenes; build from source with FFmpeg installed for them).

## Running

```
heavygear <game folder>                  the front end; in a mission you drive the gear (1-9 / 0 throttle, W / S,
                                         arrows turn, mouse drag looks, Space fires, Enter next weapon, T / F / Q
                                         target, N nav; F1 mission channel, F2-F7 squad mates, then a digit picks;
                                         F12 objectives; ` free camera); the objectives end it, or Ctrl+F12
                                         accomplished, Esc failed
hgview <game folder> m01                 mission 1 from the player's start (WASD / arrows, mouse drag to look, Q / E
                                         down / up, Shift fast, L the SOFTWARE / 3D look, H the HUD)
hgview <game folder> m01 -40000 20000 135   start at x, z, facing (degrees)
hgprj  <game folder> list | verify | get NAME.EXT out | extract outdir
```

## Legal

Heavy Gear is a trademark of its owners, and the game's data is theirs: this repository ships none of it, except the
freely distributed 1.2 patch's data files in `gamedata/patch12/`. The port's source is GPL-2.0-or-later (LICENSE).

## Credits

Port by BatMahn (https://github.com/BatMahn). Heavy Gear is a trademark of Dream Pod 9; the game is (c) 1997
Activision. You need your own copy of the game.
