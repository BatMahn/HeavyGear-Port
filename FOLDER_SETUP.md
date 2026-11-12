# Setting up the game folder

The port reads the game's files from one folder: the "game folder".

1. **Disc 1:** copy the contents of `INSTALL\DATA\` from the Heavy Gear CD (disc 1) into a folder. An installed copy of the
   game works too (its install folder, usually `C:\Program Files\Activision\Heavy Gear`). It must hold:
   - `HEAVYG.MAP` and `HEAVYG.PRJ` (96 MB): the game's art, models, sounds and missions
   - `LANG.INI` (optional: picks the language; English when missing)
   - `SMK\` (the shell's animations; optional)
2. **Patch 1.2:** nothing to do. Its data files ship with the port in `gamedata/patch12/` and are used when the game folder
   has no `Patch` folder of its own. If your installed copy is already patched, its own `Patch` folder is used instead.
3. **Music:** the game's music is the audio tracks (2-12) on the game CD. Any one of these, in the game folder:
   - the CD image itself: an Alcohol `.mds` + `.mdf` pair, or a `.cue` + `.bin` (raw 2352-byte sectors), in the game
     folder or a `cd` subfolder; the port reads the tracks straight from it
   - a `music` folder with the tracks ripped to WAV files: `track02.wav` ... `track12.wav` (44.1 kHz 16-bit, as
     ripped)
   - a `wavmap.ini`, which the original also reads (its code for a DVD drive): `[WAVMap]` then `Track2=music\track2.wav` ... (paths relative
     to the game folder)
4. **Cutscenes:** disc 2's `VIDEO\` folder holds the movies. Any one of these:
   - disc 2's CD image (the `.mds` + `.mdf` above, or a `.bin` / `.iso`) in the game folder or its `cd` subfolder: the
     port reads `VIDEO\<name>.AVI` straight from it
   - the `VIDEO` folder copied into the game folder
   - `wavmap.ini`'s `[WAVMap]` `DVDMoviePath=` (the original's setting), relative to the game folder
   The movies are Indeo 5: the port plays them when it was built with FFmpeg (see README).

File names are matched without regard to case.

Example:

```
HeavyGear/
  HEAVYG.MAP
  HEAVYG.PRJ
  LANG.INI
  HEAVYG2.mds        (optional: the CD image, for the music)
  HEAVYG2.mdf
  ...
```

Then run `heavygear HeavyGear`, or `hgview HeavyGear m01` for the mission viewer.
