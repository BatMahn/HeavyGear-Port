# Original vs port: reference comparisons

Reference frames come from the original 1.2 running under Wine (recipe in RE_NOTES.md, "Working Wine recipe"). The
player's state is read from the running game with `tools/ref/origstate.py`.

## Player starts (v0.4.0)

| Mission | Original: gear x, z, heading | Port: start navpoint |
|---------|------------------------------|----------------------|
| 1 | -26279, 70690, 180 (WPPL00) | same |
| 2 | 9217, -123110, -51 | same |
| 5 | 27463, 122162, -150.1 | 27463, 122162, -150 (START) |
| 10 | 6202, 106266, 173 | same (heading -187 = 173) |

The eyepoint in mission 1: (-26279, 489.6, 70641.7), heading 180, pitch 0. The port's: (-26279, 490, 70642).

## SOFTWARE look, missions 1, 2, 5, 10 (8-bit, i76draw)

- Geometry lines up in all four: hills, valley and horizon in the same places.
- Mission 1: the far right hill 117,45,27 (original) vs 111,59,42 (port). Near ground 115,60,29 vs 97,55,26: both at
  luma row 19, but the original's texels there are redder (KNOWN_ISSUES).
- Mission 2: both hills black in shadow, as in the original (left hill 63,37,18 vs 69,37,20).
- Mission 5: the hills black; ground 87,83,56 vs 88,83,56. The trees on the ridge differ in placement (KNOWN_ISSUES).
- Mission 10: the ground beyond shadedist (15,000) darkens to row 4 in both; near ground 193,203,213 vs 207,220,231.
  Near the start the left slope differs.
- Sampled in the running original (mission 1): flat ground at row 18 near the player, rising with the fog to row 31
  by 72,000 units, as the traced formula gives.

## 3D look, mission 1 (dx5draw, Wine + Mesa)

- Captured during the gear's start-up (eye pitched 18 degrees up); compared at the same eye.
- Ground 167,89,38 vs 171,92,44; the lit right hill 175,53,24 vs 176,57,30; the shaded left hill 132,68,30 vs
  172,87,43 (open).
- Sky: the original's clouds reach the horizon; the port's layer fades into the fog colour (open).
