# Heavy Gear (1997) - native port

https://github.com/BatMahn/HeavyGear-Port - by [BatMahn](https://github.com/BatMahn)

A native reimplementation of Activision's Heavy Gear, rebuilt from the game's own code. **Heavy Gear 1.2** is the
reference: the retail disc's `HG.EXE`, `HGSHELL.DLL` and `HGSIM.DLL` with Activision's 1.2 patch applied, decompiled and
traced. Heavy Gear runs on a later generation of the MechWarrior 2 engine. The MechWarrior 2 port's findings are used to
compare against, never as code: every format and value here is taken from Heavy Gear's own binaries and data.
Portable C99 with SDL3; builds on macOS, Linux and Windows. You need your own copy of the game's two CDs (see "Setting up the game folder": no installation needed).

## Status (2026-11-12, v1.0.0)

**Playable.** The story campaign, training, Tour of Duty (with the original's random mission generator) and LAN
multiplayer, with the original's AI, gear simulation, animations, HUD, views, sounds and both looks (SOFTWARE and 3D).
Every value is traced from the original binaries (docs/RE_NOTES.md). KNOWN_ISSUES.md lists what is still missing or
different; CHANGELOG.md lists what each release added.

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

Looks: **SOFTWARE** (the original's 8-bit software renderer: XEL texels through its lighting tables) and **3D** (the
Direct3D renderer's textures, the 1.2 patch's textures, particles and lighting), picked by the options' Graphics Type
(hgsnd.cfg) or L in a mission.

docs/RE_NOTES.md holds everything traced so far, with addresses, and how it compares with MechWarrior 2.
KNOWN_ISSUES.md lists what is still open.

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

## Setting up the game folder

The port needs the original game's files in one folder (called `HeavyGear` below; any name and place will do). **You
don't need to install the game.** Heavy Gear's installer is a 1997 InstallShield setup for Windows 95 that won't run on
current Windows, macOS or Linux, but you don't need it: everything the port reads is on the two retail CDs as plain
files, and you copy them over by hand. **See [FOLDER_SETUP.md](FOLDER_SETUP.md) for a finished example and the usual
mistakes.**

    HeavyGear/
      HEAVYG.MAP     REQUIRED  (disc 1, INSTALL\DATA\) the archive's index
      HEAVYG.PRJ     REQUIRED  (disc 1, INSTALL\DATA\, 96 MB) the game: art, models, sounds, voices, missions
      GEAR.VDF       REQUIRED for the gear editor (disc 1, INSTALL\DATA\) the editor's skeletons
      MAMMOTH.VDF    REQUIRED for the gear editor (disc 1, INSTALL\DATA\)
      NAGA.VDF       REQUIRED for the gear editor (disc 1, INSTALL\DATA\)
      LANG.INI       optional  (disc 1, INSTALL\DATA\) the language; English without it
      SMK\           optional  (disc 1, INSTALL\DATA\SMK\) the landship's and the menus' animations
      music\         optional  the soundtrack: disc 2's audio tracks as track02 ... track12 (.wav, .ogg or .mp3; see "The music")
      VIDEO\         optional  (disc 2, VIDEO\) the cutscenes (see "The cutscenes")

The port writes your games (`CHARINFO.HG1`) and the gears you design in the gear editor (`*.GDF`) into this folder, so
it must be writable. File names are matched without regard to case (`heavyg.prj` is fine).

### Which disc goes where

Heavy Gear came on two CDs, both labelled `HEAVYG`:

- **Disc 1** (the game) holds the data. Its `INSTALL\DATA\` folder is the game exactly as the installer would have
  laid it out on your hard drive: `HEAVYG.MAP`, `HEAVYG.PRJ`, the `.VDF` files, `SMK\`, and the original's
  Windows programs (`HG.EXE`, `HGSHELL.DLL`, `HGSIM.DLL` ...), which the port doesn't use. Disc 1 has no music.
  (`INSTALL\` itself also holds the InstallShield setup, `DATA1.CAB` and friends: ignore those. `DIRECTX\`,
  `DRIVERS\`, `SPLASH\`, `HELP\` and `ZGIDEMO\` aren't needed either.)
- **Disc 2** (the movies and the soundtrack) is a mixed-mode CD: a data track with the `VIDEO\` folder (the cutscenes,
  ZL_CUT01-16, the deaths and the ending), then the game's music as eleven CD audio tracks, tracks 2-12. The original
  played the music from this disc while you played; the port plays it from your rip or an image of it instead.

### Copying the files (no installer)

Put disc 1 in the drive (or mount an image of it) and copy the *contents* of `INSTALL\DATA\` into your game folder.
Copying the whole folder is simplest (113 MB); only the files in the list above are read.

- **Windows:** in File Explorer open the disc, then `INSTALL\DATA`, select everything (Ctrl+A) and copy it into
  `C:\Games\HeavyGear` (or anywhere you like). Or, in a Command Prompt, with the disc in drive D:

      xcopy D:\INSTALL\DATA\*.* C:\Games\HeavyGear\ /E /I

  Files copied from a CD keep their read-only flag on some systems; the port only reads them, so that's fine.
- **macOS:** the disc mounts as `HEAVYG`. In Terminal:

      mkdir -p ~/HeavyGear
      cp -R /Volumes/HEAVYG/INSTALL/DATA/ ~/HeavyGear/

- **Linux:** mount the disc (or `sudo mount -o loop heavygear1.iso /mnt`) and

      mkdir -p ~/HeavyGear
      cp -r /run/media/$USER/HEAVYG/INSTALL/DATA/. ~/HeavyGear/
      chmod -R u+w ~/HeavyGear

  (the mount point varies: `/media/$USER/HEAVYG` on Debian / Ubuntu). Copies off an ISO 9660 disc can come out
  read-only; `chmod` makes the folder writable for your saves.

If you have only images of the discs (`.iso`, `.cue` + `.bin`, `.mds` + `.mdf`), mount disc 1's image (Windows
Explorer and macOS mount `.iso` files with a double-click; other formats need a tool such as WinCDEmu, or
`bchunk` / `iat` to convert them to `.iso`) and copy from it the same way. The port can't read disc 1's data from inside
an image: copy `INSTALL\DATA\` out of it.

### Patch 1.2

Nothing to do. Activision's 1.2 patch changed some data files (textures, mission tables, particle definitions,
`SKYGND.PAR`); those are freely distributed and ship with the port in `gamedata/patch12/`. The port finds them next to
its own program (or in the folder you run it from) whenever your game folder has no `Patch` folder of its own. If you
copied a game folder that was already installed and patched on an old PC, its own `Patch` folder is used instead.

### The music

The soundtrack is disc 2's audio tracks 2-12 (track 1 is the data). The story's missions use tracks 2-9; each mission
names its track, and the port plays it from its start to the next track's, as the original did. Give the port the
tracks in any one of these ways (it tries them in this order):

1. **`wavmap.ini`** in the game folder, the original's own setting (the game's code for DVD-ROM editions reads it):

       [WAVMap]
       Track2=music\track02.wav
       Track3=music\track03.wav
       ...

   paths relative to the game folder. Only needed if your files are named differently from the next option.
2. **A `music` folder:** rip disc 2's tracks 2-12 to `music\track02.wav` ... `music\track12.wav` (`track2.wav` also
   works), or the same names as **Ogg Vorbis** (`track02.ogg`) or **MP3** (`track02.mp3`). WAV is read as it is
   (16-bit stereo PCM at 44.1 kHz, as any CD ripper writes it); Ogg and MP3 at any bit rate and sample rate, mono or
   stereo (the port decodes a track whole when it starts, and converts other rates to 44.1 kHz). Keep the CD's track
   numbers: the first music track is `track02`, not `track01`. Rippers: Exact Audio Copy or Windows Media Player
   (format WAV) on Windows; XLD, or the Music app with its import format set to WAV, on macOS; `cdparanoia -B 2-12`
   on Linux (it writes `track02.cdda.wav` ...: rename them to `track02.wav` ...).

       HeavyGear/music/track02.wav
       HeavyGear/music/track03.wav
       ...
       HeavyGear/music/track12.wav       (or track02.ogg ... / track02.mp3 ...)

   FLAC isn't read: convert it to WAV, Ogg or MP3 first (e.g. `ffmpeg -i track02.flac track02.ogg`). When a track has
   several files, WAV wins, then Ogg, then MP3. `wavmap.ini` paths may name Ogg and MP3 files too.
3. **An image of disc 2** in the game folder or a `cd` subfolder: an Alcohol 120% `.mds` + `.mdf` pair, or a
   `.cue` + `.bin` (raw 2352-byte sectors, one BIN file). The port plays the audio tracks straight out of the image -
   and finds the cutscenes in it too. A plain `.iso` holds only the data track (no music).

Without any of them the missions are silent apart from their sound effects.

### The cutscenes

Disc 2's `VIDEO\` folder (186 MB of AVI files): copy it into the game folder as `VIDEO\`, or put disc 2's image there
(the `.mds` / `.cue` image above, or an `.iso` / `.bin`, in the game folder or `cd\`), or point `wavmap.ini`'s
`DVDMoviePath=` at a folder holding them (the original's setting, relative to the game folder). The movies are Indeo 5
video: the port plays them only when it was built with FFmpeg (see "Build"); otherwise they're skipped.

    macOS / Linux:  cp -R /Volumes/HEAVYG/VIDEO ~/HeavyGear/
    Windows:        xcopy D:\VIDEO C:\Games\HeavyGear\VIDEO\ /E /I

### A finished folder

    HeavyGear/
      HEAVYG.MAP  HEAVYG.PRJ  GEAR.VDF  MAMMOTH.VDF  NAGA.VDF  LANG.INI
      SMK/        B02GBSMK.SMK ... B15WISMK.SMK
      music/      track02.wav ... track12.wav   (or .ogg / .mp3)
      VIDEO/      ZL_CUT01.AVI ... ZL_CUT16.AVI  B51MOVIE.AVI  NDEATH.AVI  SDEATH.AVI
      (plus whatever else came along from INSTALL\DATA\: HG.EXE, the DLLs, CFGS\ ... - harmless)

Then run `heavygear ~/HeavyGear` (on Windows `heavygear.exe C:\Games\HeavyGear`).

## Running

```
heavygear <game folder>                  the front end and the game; in a mission (INPUTW.MAP / GAMEKEY.MAP keys):
                                         1-9 / 0 / = / - throttle, arrows turn the legs, Shift + arrows side-step,
                                         Ctrl + arrows glance; mouse / Comma / Period / Up / Down the torso, M feet to
                                         torso, Keypad 5 reset; W skate, J jump jets, Tab crouch, S stand, Backspace
                                         reverse; Space fire, Enter next weapon; T / Y / F target any / enemy /
                                         friend (Shift back), E nearest enemy, Q under the reticle, N nav; C cockpit /
                                         outside, F10 missile view, F9 target display, F8 map, F11 HUD, Z zoom, X radar
                                         zoom; F1 mission channel, F2-F7 squad mates, F12 objectives; Esc the menu;
                                         Ctrl + ` free camera; L the look
heavygear with HG_MISSION=m01            a mission straight away (HG_GEAR picks the gear); HG_NET_HOST=<port> /
                                         HG_NET_JOIN=<host>:<port> a LAN game on that mission
hgview <game folder> m01                 mission 1 from the player's start (WASD / arrows, mouse drag to look, Q / E
                                         down / up, Shift fast, L the SOFTWARE / 3D look, H the HUD)
hgview <game folder> m01 -40000 20000 135   start at x, z, facing (degrees)
hgprj  <game folder> list | verify | get NAME.EXT out | extract outdir
```

## Legal

Heavy Gear is a trademark of its owners, and the game's data is theirs: this repository ships none of it, except the
freely distributed 1.2 patch's data files in `gamedata/patch12/`. The port's source is GPL-2.0-or-later (LICENSE).
Third-party code keeps its own licence (THIRD_PARTY.md): dr_mp3 (public domain / MIT-0) and stb_vorbis (public domain
/ MIT), in `src/third_party/`, decode the soundtrack's MP3 and Ogg files.

## Credits

Port by BatMahn (https://github.com/BatMahn). Heavy Gear is a trademark of Dream Pod 9; the game is (c) 1997
Activision. You need your own copy of the game.
