# Known issues (v1.0.0)

The port plays the story campaign, training, Tour of Duty and LAN multiplayer. Everything it does is traced from the
original binaries (docs/RE_NOTES.md has the addresses). What follows is what the port doesn't do yet, or does
differently from the original, by area.

## Player gear

- From a standstill the traced movement covers 2,142 units in 532 ticks where the running original covered 1,929.
  The vehicle record's +0x84 bit 2, which gives the speed a 0.5 s lag, has no setter anywhere in HGSIM; the gap isn't
  explained.
- As traced, a stopped gear creeps at top / 512 (the original's always-true |turn| >= 0 test) and straffing in the
  movement code never runs (Shift + arrows only play the side-step animations).
- Collisions with objects and vehicles (0x10056c90, 0x1005815c / 0x100589ad, with their knockdowns) aren't done.
- Recoil and its compensation (the builder's +0xbd7 sums, the weapons' +0x74) aren't traced, so neither are the
  arm-actuator and hydraulics crits' effects.
- Not done: jump-jet flames (effect 0x19 at mount slots 4 / 5, 0x1005359c: the port's gear model doesn't keep the
  builder's slot order), the grenade throw (0x3d sets +0x562 bit 0x800; the throw animation's release event isn't
  traced), the lost leg's smoke and the foot dust (0x1007f8a9 / 0x10086007 particle systems), and the TRHP / RASH /
  HDHD aim rotations on the skeleton (the torso's twist and tilt are done).
- The held torso keys' rates (90 / 30 degrees a second), the mouse's 0.25 degree a pixel and the tilt's sign are the
  port's choices.
- The AI's vehicles step every tick without the original's 1/16 velocity dead zone (with one tick a step it would hold
  them still).

## AI

- The original's mode rules, combat states (cover included), obstacle probe, aircraft altitude, torso weave, twist and
  tilt, fire at structures and weapon choice are in. The probe takes every object of a walkable kind with a flat
  polygon as passable, as traced; the drawn torso twist is applied in the vehicle's frame (exact for the standing pose).
- Structures can't be locked by the missile seeker; a structure target's HUD box is sized by its bounding radius.

## HUD and views

- The F8 map is drawn unfogged (0x100784e0's fog at the map's height and the 0x1014b5c0 offset aren't done).
- Window 11's other MFD modes (0x100654a8 cycles 14; their draws aren't followed) aren't done; NEXT_RADAR_MODE and MFD
  cycling have no default key in GAMEKEY.MAP, as in the original.
- The in-sim menu's sliders only step up (KEYBOARD.DLL's codes 200 / 201 aren't mapped). Shadows, Multiple Lights and
  Joystick Auto Center are kept but change nothing (no shadows, no joystick); 3Dsound shows Off.
- The free eye (view 2) is debug-only in the original; the port's Ctrl+` free camera stands in.

## Rendering

- The SOFTWARE look's intensity is one table row lighter than the traced path (| 7, calibrated on mission 1's ground):
  the traced path gives one row darker than the original's frame and the difference isn't found.
- The 3D look: mission 1's shaded hill on the left is darker in the original (about 0.8 of the port's); not found.
- The damage brightness nibble (0x1005c1f8 / 0x10026610) isn't applied: its flags 0x100 / 0x50 aren't set on the
  port's drawn parts.
- Point lights light per polygon; the original lights each vertex once a frame from the first polygon that holds it.
- The cockpit's roll is the gear's roll (the head's own pose roll isn't added); the sky's gradient band doesn't roll.
- Mission 27's 45 ground tiles in MechWarrior 2's older layout fail the original's checksum (a fatal error there);
  the port leaves them out.
- B64's Textures Off isn't done (render flags 0x100 / 0x200 / 0x800 traced, their effect not).

## Front end

- The gear editor: the selected piece's statistics text (one builder per type, 0x1016d114 ..), the selection's
  highlight, the details popup and the armour / thickness panel aren't done.
- The briefing narration plays its WAVs; the 3D units on its map (0x100725fb / 0x10072251) aren't drawn.
- Medal awards (0x100e0679 .. 0x100e0fe3, 22 conditions in 0x100df8e4) aren't given.
- Tour of Duty: B04's map region and points panel, B01's unit panel and B08's insignia aren't drawn.
- B65 (controls) isn't done; B64's Graphics Type offers "Software 640" and "Direct3D" (the original lists the
  system's devices). With the game's hgsnd.cfg saying software, missions start in the SOFTWARE look (L toggles).
- The list-zeroing flag (HGCAR +0x218) is set only by the original's typed cheat codes, which the port doesn't read.
- The briefing's gear above the game's level is drawn dark only for a game whose gear is above it (the lists keep to
  the level).

## Multiplayer

- LAN play over the port's own UDP transport: ANET2's wire format isn't traced, so the port can't play against the
  original, and only "LAN (UDP/IP)" is offered (no IPX, Internet lobby, modem or null modem).
- Missions come from the original's generator in its multiplayer mode; the players' EN00STAR .. EN07STAR records
  aren't written as the shell does: they start at the template's start navs, each on his own team, and the template's
  objectives don't end the session.
- Not done: B49's password, B46's reject / encrypt / gear selector, B60's channels, the mission description, the
  death movies (state 0x4b), the sim's CH / CO / SN / AI / DI / TC / CA messages (chat in the lobby uses the port's
  own tags), localized kill messages, DA +0x3a / +0x3e (sent as 0).

## Sound

- With the voices' level at 0 the radio's text shows in the message bar for 0x712 ticks, as the original.
- Spoken-line .sfl files are looked for in the game folder's KEATING folder (a cd/ subfolder stands in for the CD
  root); neither disc has any.
- Many objectives name failure voices the archive doesn't have ("avon009F", "avom033F"); the original plays nothing
  for those, and neither does the port. "BET68" (out of time) isn't in the archive either.
- The hub animations are silent, as the original's.

## Ports and build

- The releases have FFmpeg built in for the movies (Indeo 5); a build of your own needs FFmpeg for them, otherwise
  they're skipped.
- Generated missions (Tour of Duty, multiplayer) are written into the game folder as RAN*.BWD / RAND*.BWD files, as
  the original does.
