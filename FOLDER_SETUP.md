# Setting up the game folder

The port reads the game from one folder, the "game folder", which you fill from your two Heavy Gear CDs by hand. **No
installation:** the original's Windows 95 installer isn't needed (and won't run on a current system). The full
explanation, with the commands for Windows, macOS and Linux, is in README.md, "Setting up the game folder". This page is
the short version: what a working folder looks like, and the usual mistakes.

## A working folder

    HeavyGear/
      HEAVYG.MAP          disc 1  INSTALL\DATA\HEAVYG.MAP            required
      HEAVYG.PRJ          disc 1  INSTALL\DATA\HEAVYG.PRJ (96 MB)    required
      GEAR.VDF            disc 1  INSTALL\DATA\GEAR.VDF              the gear editor
      MAMMOTH.VDF         disc 1  INSTALL\DATA\MAMMOTH.VDF           the gear editor
      NAGA.VDF            disc 1  INSTALL\DATA\NAGA.VDF              the gear editor
      LANG.INI            disc 1  INSTALL\DATA\LANG.INI              optional (English without it)
      SMK/                disc 1  INSTALL\DATA\SMK\                  optional: the menus' animations
        B02GBSMK.SMK ... B15WISMK.SMK  (16 files)
      music/                      disc 2's audio tracks 2-12, ripped  optional: the soundtrack
        track02.wav ... track12.wav    (16-bit stereo, 44.1 kHz)
      VIDEO/              disc 2  VIDEO\                         optional: the cutscenes (needs FFmpeg)
        ZL_CUT01.AVI ... ZL_CUT16.AVI  B51MOVIE.AVI  NDEATH.AVI  SDEATH.AVI
      CHARINFO.HG1                written by the port: your games
      *.GDF                       written by the port: the gears you design

The simplest way: copy everything in disc 1's `INSTALL\DATA\` (113 MB) into the folder, add `music\` and `VIDEO\` from
disc 2. The extra files that come along (HG.EXE, the DLLs, CFGS\, PLUGIN\ ...) are the original's Windows programs;
the port ignores them.

Instead of `music\` and `VIDEO\`, an image of disc 2 works: an `.mds` + `.mdf` or `.cue` + `.bin` pair in the game folder
or a `cd\` subfolder gives both the music and the cutscenes (an `.iso` only the cutscenes: it has no audio tracks).

Patch 1.2's data files come with the port (`gamedata/patch12/`); keep that folder next to the port's program.

Then run `heavygear /path/to/HeavyGear`.

## Common mistakes

- **Copying `INSTALL\` instead of `INSTALL\DATA\`'s contents.** `HEAVYG.PRJ` must sit directly in the game folder, not in
  `HeavyGear/DATA/` or `HeavyGear/INSTALL/DATA/`. The port says "HEAVYG.PRJ not found in ..." otherwise.
- **Running the installer's `.CAB` files through an extractor.** Not needed: `DATA1.CAB` holds the installer's own bits;
  the game's data is the plain files in `INSTALL\DATA\`.
- **Looking for the music on disc 1.** Disc 1 is data only. The soundtrack is disc 2's audio tracks.
- **Numbering the music from 1.** Keep the CD's numbers: the first music track is `track02.wav`. A ripper that numbers
  the audio tracks from 1 needs its files renamed (track 1 -> track02, ... track 11 -> track12), or a `wavmap.ini`.
- **FLAC or MP3 rips.** Only WAV is read; convert them back to 16-bit, 44.1 kHz WAV.
- **cdparanoia's names.** `track02.cdda.wav` must be renamed `track02.wav`.
- **A plain `.iso` of disc 2 for the music.** An ISO holds only the data track; use an `.mds` / `.cue` image or rip the
  tracks.
- **A read-only folder.** Copies off a CD can keep their read-only flag; the folder itself must let the port write
  `CHARINFO.HG1` and your designs (on Linux, `chmod -R u+w` the folder).
- **Disc 1 left as an image.** The port reads music and movies out of a disc 2 image, but disc 1's data must be copied
  out: mount the image and copy `INSTALL\DATA\` from it.

File names are matched without regard to case.
