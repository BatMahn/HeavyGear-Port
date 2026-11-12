# Heavy Gear: reverse-engineering notes

The reference is **Heavy Gear 1.2** (the 1.0 disc plus Activision's 1.2 patch): `HG.EXE`, `HGSHELL.DLL`, `HGSIM.DLL`
(all built 1998-07-23) and the renderer plugins in `PLUGIN\`. Addresses are virtual addresses in those files (DLL base
0x10000000, EXE base 0x400000). Every value the port uses is traced here or in a source comment, and marked INFERRED where it
isn't.

"MW2" refers to what the MechWarrior 2 port found in MW2.EXE / MW2SHELL.EXE (DOS), the 3D editions' MW2.DLL and the
Titanium edition's mercsw.dll. That code was only compared against; no MW2 code or binary is used here.

## Lineage

- `HGSIM.DLL` carries MW2's revision tags: `Project File $Name: RELEASE10F ... PREWIN95 ... MERCS7C ... MERCS1B
  MERCSPREALPHA GBL_RELA REL_11 CODE_REL_A BETA_PATCH $`, and source paths `C:\projx\Code\sim\...` and `C:\hg\Mem\...`.
- `HG.INI`'s `[SystemError]` table is MW2's (e.g. "digpak driver is outdated", "error loading mech-spec").
- MW2's sim-team easter-egg page is still inside the sim.
- Closest MW2 relative: the Titanium edition (1998, Windows, `mercsw.dll` + `plugin\*.dll`). Its TEX format, its
  SKYGND.PAR style and its plugin renderer interface are the same family.

## Program structure (HG.EXE)

- 0x402710 WinMain body:
  1. Deletes `nmsave.cfg` and `hgprm.cfg`.
  2. Checks for an 8-bit-or-better desktop and that no second instance is running.
  3. Loads `ANET2.DLL` and creates a 640 x 480 window.
  4. Loops: shell, then sim, then shell.
- 0x402620 runs the shell: `LoadLibrary("HGSHELL.DLL")`, `ShellMain(hModule, 0, name, flags, hwnd)`, starting with
  `"intro"`.
- The shell writes `hgprm.cfg`, a 0x344-byte parameter block. HG.EXE reads it and hands it to the sim.
- 0x402580 runs the sim: `LoadLibrary("HGSIM.DLL")`, `SimMain(...)`. It returns to the shell when the mission ends.
- MW2 comparison: the DOS MW2 ran MW2SHELL.EXE and MW2.EXE as separate programs from a batch loop. The DLL split here is the
  Windows form of the same handoff.

## Resource archive (HEAVYG.MAP / HEAVYG.PRJ) - src/hgprj.c

- HGSIM `sysfile.c` 0x100a4a91 reads the map:
  - u32 entry count, then the type list (tag + hash size), the PMA list and the codec DLL names.
  - Then 20-byte entries: name[8], type[4], offset, 4 unused bytes.
- The codecs are loaded by name (0x100a4980) and called through their `Uncompress` export: `pkware.dll` is method 1
  (PKWARE DCL implode) and `zlib.dll` is method 2.
- PRJ record: char src[12], u16 method, u32 size, u32 packed.
- Language variants are their own types: the extension plus the language letter (`WAVX`, `PCXX` for English). The letter
  comes from `lang.ini` (0x100a21be); the default is `X`.
- File lookup, `syscache.c` 0x100a2bc9. Each step tries the language variant first:
  1. `Patch\name.EXTL`
  2. `Patch\name.EXT`
  3. `name.EXTL` in the game folder
  4. `name.EXT` in the game folder
  5. The archive, type `EXTL` then type `EXT` (0x100a4779)
- MW2 comparison: MW2's MW2.PRJ was a single file with "PROJ"/"DDIT"/"INDX"/"SYMB" blocks and checksummed records, and
  had no compression. The HG format is new.

## Models (WTB, "WTBO") - src/wtb.c

Parser: 0x100c5e73, called through 0x100c5e01.

- Header:
  - `+0x00` "WTBO"
  - `+0x04` i32 checksum
  - `+0x08` name[16]. Stored negated when the first byte is above 0x80 (un-negated on load, as MW2).
  - `+0x18` i16 vertex count
  - `+0x1a` i16 polygon count
  - `+0x1c` u16 flags. `& 0xcfff`: 1 or 2 select two lighting modes. 0x2000 is set on multi-object records.
- Vertex, 44 bytes: float x, y, z; double normal[3] (stored negated); float u, v.
- Polygon, 0x38 bytes (n <= 4) or 0x3e bytes (n 5-7):
  - char material[16]: a label, resolved through the TLT tables below
  - `+0x10` u16 (enters the checksum)
  - `+0x13` .. `+0x15` RGB, written for material type 4
  - `+0x16` double normal[3] (negated)
  - `+0x2e` u16 n
  - `+0x30` u16 idx[4 or 7]
- Records hold one or more objects back to back.
- 10,300 of the 10,345 archive models parse to the exact byte. The other 45 (`TL1MBA*`, flag 2) are in the MW2 DOS layout:
  i32 vertices of 16 bytes and 12-byte polygons. Mission 27's ground references them. The float parser would fail their
  checksum, so in the game they likely don't load (INFERRED; to be checked in the game).
- MW2 comparison: same WTBO header. MW2 DOS used i32 vertices and colour-index polygons. MW2's 3D editions used i32 vertices
  plus double normals and u16 texel coords. HG moves to float positions and float UVs, and names a material per polygon
  instead of a colour index.

## Materials: TLT / MLT

- A polygon's label is upper-cased (0x100ea690) and matched against a TLT entry's label.
- TLT (texture lookup) entry, 72 bytes, count at `+0x28`, entries from `+0x2c`: char key[32], char label[32], i32 slot
  (-1 until loaded), u32 (the cached material).
- The TLT key finds the MLT entry. MLT (material lookup) entry, 96 bytes, same header:
  - char key[32]
  - char name[32]: **the texture actually loaded** (0x100bef4c compares `+0x20` against the loaded texture names)
  - u32 type `+0x40`, u32 `+0x44`, u32 `+0x48`
  - type 3 only: first frame `+0x4c`, last frame `+0x50`, rate divisor `+0x54`
  - RGB at `+0x5d..0x5f`
- Type 3 is an animated sequence. 0x100bef4c loads `name` + frame number (zero-padded below 10) for every frame, at rate
  0xb5 / `+0x54`.
- Open ground: polygon labels "Solid Sand" / "Solid Rock" -> keys SOLIDONE / SOLIDTWO -> textures T1SANDRD / T1ROCKRD (in
  mission 1). The edge and corner labels (sand-to-rock transition textures) are types 8-11 by
  direction, like the solid ones (below). The loader also remembers the Solid Rock / Solid Sand texture slots (0x1010504c / 0x10105050).
- Type picks the polygon's draw mode (0x100c5e73 switch): 0/1/4 flat or Gouraud, 2/5/6/7 textured, 3, and 8-11 the
  ground kinds 0xb000-0xe000. Every mission 1 terrain material is one of them (the solid sand and rock too; the edges
  and corners use 8-11 by direction). They are drawn opaque: the render switch (0x1008bd80) sends them to 0x100012c5
  with its "ground" flag, which calls I76DRAW's Render mode 1 (0x10001ca0, a rasterizer without the colour key).
  The kind only flags a triangle's split (0x100c5e73: bit 0 of the polygon record's `+8`).
- A mission's `xxx_TABL` BWD lists its TLT / MLT files in TLUT / MLUT chunks (`ffff name[10]`).

## Textures

- TEX (hardware renderers): "1.22", u16 type, u16 w, u16 h, u16 n, n x 4-byte palette (B, G, R, A), w*h indices. All 2,255
  files check, archive and patch alike. Sizes: 64x64 (1,651), 256x256 (305), 128x128 (189, mostly patch 1.2 upgrades) and
  32x32. Types: 0 (1,936), 3 (317), 1 (2). This is the Titanium edition's format.
- XEL (software renderer): u16 w, u16 h, w*h palette indices. The DOS-style CEL.
- PCX: ordinary 8-bit RLE PCX (shell art, 640x480).

## Missions (BWD)

- Same container as MW2: "BWD\0", u32 size, u32, then chunks (tag[4], u32 size including the 8-byte header). Rev "1.22".
- INCL in HG names its target only: `u16 0xffff, char name[10]`. MW2 used a PRJ resource id, with 0xfffe for files on
  disk.
- A mission is xxx_SCN1, which includes:
  - xxx_TABL: TLUT/MLUT
  - xxx_ART1: PLNT, PALG, CLIM, LITE, INIT, VIEW (view distance 50000), HRZM, HTXT, LTBL
  - GEARSHT1
  - xxx_GRND: ground tiles as OBJ chunks
  - xxx_OBJ1: objects (BTAG/BLKX/BLK/OBJ/ENDB/ETAG, GT, TSK)
  - USERSTAR, FORMATNS
  - plus NAVP, GPS (gear placements: gear name, AI file), MUSI, CAMO, STAR
- OBJ is 60 bytes in HG (52 in MW2):
  - i16 index, i16 parent (-2 at the top level), i16
  - i32 scale[3] (1 = unit)
  - i32 rot[3]: 16.16 fixed **degrees** (0x5a0000 = 90)
  - i32 offset[3], 8 bytes
  - char model[10] at `+0x32`
  These fields are INFERRED from the data and confirmed on screen; HGSIM 0x100c1152 is still to be traced.
- BLKX (36 bytes) starts a placed block: i32 x, y, z at `+0x18`. The OBJ offsets inside it are relative to that position.
  Every block in mission 1 has y = -4150 while its objects carry small negative y offsets. The viewer stands block objects
  on the ground plus their own y offset (INFERRED; matches the screen).
- Ground tiles (xxx_GRND) are OBJs in a plain BLK with their world coordinates in the model itself.
- Object models come as triples (`o_1tents`, `oa1tents`, `oc1tents`). Models whose name starts with 'o' load an **SDF**
  record of the same name (the type string at 0x10122d50 is "SDF", in the 0x100c1152 loader). The SDF holds up to 10 model
  names at `+0x0c + n*0x20`.
  - `o_` objects: the names are detail levels, with a float switch distance per level at `+0x14c + n*4` (o_1tents:
    o_2tents beyond 25,000). The first level's is 10 when there are more, 0 when there aren't (all 443 o-SDFs). The
    loader (0x1000361c) keeps the levels in a list sorted by distance; the draw (0x10090750) takes the last level whose
    distance times the view's detail scale (view +0xb8 = half width x zoom / (160 x detail), detail 0x101244f8 = 1, or 2
    for low detail; set in 0x100b8496) the object's depth reaches, and the first level when its distance is 0. The
    depth is the object's bounding-box centre along the view direction (0x10078797 / 0x100b899d: (centre - eye) .
    forward, the forward axis at 0x10152e98).
  - `oc` objects (second letter 'c', special-cased by the loader): the names are the collapse animation frames
    (oC1..oC8).
  - `oa` objects: the remains (INFERRED from the names).
  - OBJ nodes: `oa1` is a root (`+4` = 7), `oc1` is its child (parent = oa1's index), `o_1` is a separate root (`+4` = 0).
  The viewer shows the `o_` object with its detail levels and leaves the destroyed states out until destruction is
  implemented.
- Chunk types new to HG: HGXP, HGBT. The rest are MW2's.
- The BWD header's third word is the largest chunk size in the file: the chunk walker (0x1001d8a1) rejects a chunk bigger
  than it (error 0x2c), so a record with 0 there reads as empty.

### Navpoints, gear placements and objectives

- **BTAG** name[16] ... **ETAG** bracket a tagged block. The navpoint and gear-placement handlers pass the current tag on.
- **NAVP** (the NAVP branch of 0x100c1152, 0x100c3718-0x100c38fb): nav table at 0x107bc3a0, 0x54 bytes per nav, at most
  0x80 navs. Payload:
  - `+0x00` i32 x, y, z
  - `+0x0c` i32 heading, 16.16 fixed degrees (* 1/65536, 0x10103880)
  - `+0x10` i16, `+0x12` u16, `+0x14` i16, `+0x16` i16 group, `+0x18` u16 (* 100.0, 0x10103898)
  - `+0x1a` u16 type mask, widened by 0x100c1120 (any of 0x730 sets all of 0x730; any of 3 sets 3)
  - `+0x1c` name
  Each NAVP is registered on the objectives that name its BTAG (0x10066794, with the nav index | 0x100).
- **GPS** (92 bytes, the GPS branch of 0x100c1152 from 0x100c3f1f): `+4` group, `+5` 1 on a group's first record, `+6`
  role (0 = the player; the branch takes the player's group from that record), `+0x16` index, `+0x18` flags, `+0x1a`,
  `+0x1c` gear name[8], `+0x25` AI file[8], `+0x2e` type name ("Gear"), `+0x44` display name ("Hunter"). The shell's
  USERSTAR records are the same layout.
- **MTBL** (0x10066090): one per group (`+0` group). Objectives of 0x97 bytes from `+0x1e`. In objective k (at chunk
  `+0x26 + k * 0x97`): `+0` u32 mask, `+4` kind letter ('H' / 'V'), `+0x4a` tag name (what 0x10066794 matches against
  BTAG names, case-insensitively, with the masks meeting), message names at `+0x34` / `+0x3f`, text at `+0x57`.
  The registration also needs an objective state byte to be 7; its setter is not traced yet (objective 0 passes it in
  every mission tried).

## Player start (traced; checked against the running original)

- The shell writes the player's GPS record with group 0, leader 1, role 0, flags 0x4006 / 0x400 (HGSHELL 0x100ff1e6);
  wingmen get group 0, role 2, flags 0x4006 / 0x480.
- SimMain, after loading ("DoFirstObjtv", 0x1006fae1), calls 0x10066a68 for every group. If the group's objective 0
  has a registered navpoint (state `+0x128` count, `+0x12a` == 1 for a nav, `+0x129` its index), the group goes there:
  0x1004271a puts the leader on the nav's x, y, z facing the nav's heading, and the others at their formation slots
  (tables 0x107c95d0 / 0x107c95f0 / 0x107c9610, 0x74 bytes per group, by the vehicle's slot `+0x10`).
- Mission 1: group 0's objective 0 names WPPL00, whose NAVP is at (-26279, 0, 70690), heading 180. The running
  original has the player there with heading 180, and so do missions 2 (START-style tags), 5 and 10.
- Headings: 0 looks along +z, and the port's yaw is the engine's heading (v0.3's "engine = port - 90" was wrong: that
  frame showed vehicle 0, an infantry unit, because the test USERSTAR was rejected).
- The gear stands on the ground: its origin is 245.3 above the ground for the Hunter (gear block `+0x53c`, set by the
  GPS loader from the skeleton's node positions, 0x100c4b3a). MEASURED from the running original (ground 73.0, gear
  318.3), the skeleton maths not traced.

## The cockpit view

- The eyepoint struct is at 0x1011f908 (pointer 0x1011f9e8): x, y, z, heading, pitch, roll (degrees), zoom (`+0x18`),
  the LITE light (`+0x1c..+0x2a`), the view rectangle, near (`+0x3c`), far (`+0x40`), and the projection (`+0x84..`).
- Projection (0x100b8496): centre = the middle of the view rectangle; focal length x = half the view width * zoom,
  y = that * aspect (`+0x44`, 1.0). Unzoomed (1.0, 0x10110404) that is a 90-degree horizontal field of view.
  Near = half width * 0.5 + 1 (161 at 640 wide).
- Far = the VIEW chunk's distance * 2.5 (0x100c1152; 125000 in mission 1, 100000 in mission 10).
- The eye rides on the gear's skeleton. For the Hunter at rest it is 171.3 above the gear's origin and 48.3 ahead
  (MEASURED from the running original). On a slope the gear pitches with it (mission 10 starts pitched 3 degrees);
  the port doesn't do that yet.
- The engine's pitch is positive looking down.

## SKYGND.PAR

- The sim opens `skygnd.par` with a plain fopen in the game folder (0x1008ceab via 0x100db910), not through the archive
  lookup. So patch 1.2's game-folder SKYGND.PAR (15,737 bytes) is the one used, not the different copy in `Patch\`.
  Both are shipped in gamedata/patch12.

- Free-form `key=value` lines per mission (`mission=m01_ globalintensity=45 hardwareintensity=80 ...`), with a commented
  key list at the top (software and hardware values side by side). The parser is 0x1008ceab. Same style as Titanium's.

### The values (0x1008ceab)

- Every format field lands in a global.
- Hardware mode: the horizon is `hardwarehorizon`, with colours `skycolor[4]` (above) and `skycolor[5]` (below).
- Software mode: the horizon is `horizontexture`, with colours `skycolor[0]` and `skycolor[2]`.
- The rest of skycolor: `skycolor[1]` is the scrolling sky's alpha and `skycolor[3]` its tile rate (capped at 0x100).
- `hambient`, `l1`, `l2`, `gl` colours are clamped to 0..255. The `l1` / `l2` directions are normalized.

## Vertex lighting (HGSIM 0x1004bad6)

One function lights a polygon's vertices for every renderer. It keeps two results per vertex: a luma value L (the
software renderer's) and an RGB colour (the 3D renderers').

- Intensity (0x1009250c): i = clamp(dot(normal, light) / |light| * 128, -127, 127). The 128 is at 0x101035e8. A point
  light uses the vector from the vertex to the light.
- The **LITE** light (the ART record's LITE chunk: direction at `+8`, ambient `+0x14`, mode `+0x16`, shadedist
  `+0x18`; into the eyepoint `+0x1c..+0x2a` and 0x1011fa34). On unless its direction is 999999 x3 (0x10118190).
  j = i * intensity / 128, where the intensity is SKYGND's globalintensity in software, hardwareintensity in 3D,
  gintensity in Gouraud mode (0x1008ceab -> 0x1004b249, clamped 0..128). When j > 0, L += j and RGB += gl * j >> 7.
- L starts at the ambient: the LITE ambient in software, SKYGND's hardwareambient in 3D, gambient in Gouraud mode.
  RGB starts at `hambient`.
- `l1` and `l2` (SKYGND, normalized): j = i * 255 / 128; when j > 0, RGB += colour * j >> 7. They don't touch L.
- Point lights (0x10138d14 list) add to L and full white to RGB when in range.
- Each RGB channel is clamped to 0..255.
- In mission 1 the LITE light points along (1, 1, 0), the sun l1 straight up (0, 1, 0), the fill l2 along (-1, -1, 0).

### The software luma row

- Fog, when fogswitch is on (the software path clamps a negative fogswitch to 0): L += 128 - visibility * 128.
- Shadedist (the LITE `+0x18`, software only, replaces fog): beyond it, L -= 128 - shadedist / z * intensity.
- L is clamped to 0..128, then scaled by the polygon's draw word (0x10026610): row = L * p >> 10, p = 255 for textured
  kinds (0x2000, 0x5000-0x7000, 0xb000-0xe000) and the colour's low byte for flat ones. Rows are clamped to 4..31.
- The vertex intensity handed to the plugin is row << 3. I76DRAW scales it by 1/255 (Render 0x10003a70); its
  rasterizers index their 32-row table with (1 - that) * 7936 (31 * 256, 8.8 fixed point), interpolated across the
  polygon, the high byte picking the row. The sim hands that table over upside down (0x1000103f), so the TBL row
  reached is 31 - the index. The intensity's low three bits: the software branch of 0x100012c5 (0x1014a5e8 == 0)
  hands over row << 3 with them clear (the 3D branch: << 3 | 7), and I76DRAW's mode-2 rasterizer (0x10002650) rounds
  (1 - i / 255) * 7936 to nearest (the 1.5 x 2^52 trick, 0x1000f0d8) and takes the high byte of the 8.8 value - so
  row 18 reaches table row 18. The original's frame showed mission 1's flat ground (row 18 by the port's sums) at row
  19, which | 7 reproduces; the port keeps | 7, calibrated, until the difference is found.
- I76DRAW's rasterizers skip texel index 255 (the colour key, e.g. 0x10002db5).
- Checked in the running original: flat ground in mission 1 has row 18 near the player and climbs with the fog to 31
  by 72,000 units; in mission 10 the ground beyond shadedist is at row 4.

## Fog (0x1004b93a)

- Visibility is 1 up to `density` Z, then falls linearly to 0 at the far distance (0x10152ed8: the eyepoint's far,
  VIEW * 2.5). The distance is the vertex's view depth (its `+8`). `density` Y adds a height term on the vertex's
  `+0x2c` (not yet used; what `+0x2c` holds is not traced).
- The 3D renderers fog toward `fogcolor`. The software renderer moves the luma row instead (`fogswitch` -1 / 1).

## Horizon and sky (0x1008dff3)

Drawn first each frame, for both looks:
- The colour above the horizon line and below it: `skycolor[0]` / `[2]` in software, `[4]` / `[5]` in 3D (the
  SKYGND parser hands one pair or the other to the same globals, 0x1010e594 / 0x1010e598).
- A band of the horizon texture standing on the horizon line. The parser copies `horizontexture` (software) or
  `hardwarehorizon` (3D) into the same slot (0x1014b978). Its height is DAT_1011fa58: 36 in the file, scaled by the
  view transform (0x100763fb / 0x1007602d) - 128 at 640 x 480 in the running original, both looks. The band turns
  with the roll (0x10051940).
  - U from 1 - heading / 360 * 8 (0 when above 4), across `horizontile` units over the screen; V 0.01 .. 0.99.
  - Software: the vertices carry intensity 0x80 (table row 16, the identity row).
- **3D only, the cloud layer (0x1008da61):** a quad in world space around the eye (sin / cos by 0x1006f3e6):
  - near edge 500 / tan(45 - pitch) ahead and 500 above the eye; far edge 40000 ahead and 100 below the eye; both
    80000 wide. So the clouds reach the horizon.
  - The `skytexture` repeats every 40000 / `skycolor[3]` units (0x1011f8d0 / 0x1011f8c8). Texel coordinates are taken
    relative to the eye (snapped to whole periods), so the layer moves with the viewer.
  - It drifts along u and v by `skyspeed` / 46336 per tick (0x101035c4), wrapping at 256. A tick is 5 ms
    (0x1001fe62 sets the period, 0x1001fd80 counts timeGetTime in it).
  - Drawn without fog (DAT_1011fa3c cleared around it); `skycolor[1]` is its alpha.

## Palettes and luma tables

- COL: 256 x RGB, 6-bit VGA values. The mission ART record's PALG chunk lists 20 palette names; the first is the
  normal one.
- TBL (luma, the software renderer): 32 rows x 256 entries (8,192 bytes). Row 0 is all one colour (full fog); the
  rows run up through a near-identity row (16). LTBL in the ART record names the mission's (`m01fog01`).

## Renderer plugins (PLUGIN\*.DLL)

- Four plugins:
  - RENDDRAW: software
  - DX5DRAW: Direct3D 5
  - 3DFXGEAR: Glide
  - I76DRAW: Interstate '76's renderer
- Each exports CheckFunc, FirstDevice, GetFuncDesc, GetNumDevice, GetSocketCaps, LastDevice, (Lock/Unlock)Display,
  LostDeviceDisplay, PreloadTexture, (Refresh)Display, Render, RenderNoClip, RenderRefresh, RestoreDevice, SetLumaTable,
  SetState, SetTexturePalette, UpdateTexture.
- The sim loads them from `plugin\%s` (0x1002abc8). `DLLFILE.INI` names the default: `i76draw.dll`, `3DFXGEAR.DLL`.
- **RENDDRAW** is a 16-bit software renderer: it converts RGB555 textures to 565 (0x10003830), with a colour key.
- **I76DRAW** is the 8-bit one:
  - Its `SetLumaTable` (0x10004110) copies a 256 x 256 table (64 KB).
  - Its `Render` (0x10003a70) takes a per-vertex intensity byte, scaled by 1/255.
  - So the sim expands the 32-row TBL to 256 rows before handing it over. That expansion is next to trace.
  - `DLLFILE.INI` lists it first.
- The port's two looks:
  - **SOFTWARE**: I76DRAW's 8-bit path, with XEL texels through the luma table.
  - **3D**: the hardware plugins' path, with TEX textures, patch 1.2's textures and particles, and the RGB lights above.

## Weapons

- A gear's weapons come from its GDF (loader 0x1004c8c4): after the parts (0x438 bytes each, count at `+0x84`) come
  u32 version (1 / 2), a 0x3c-byte header (`+0x24` the equipment count) and the equipment records, 0x6c bytes each:
  i32 kind, i32 id, ....
- The gear builder (0x1004e6a0) makes a weapon slot of every record of kind 8..16, in order (the gear block has 8
  slots of 0x128 bytes at `+0xc51`, 0x1002bb2b). Slot `+4` weapon type = the i32 at 0x10101d98 + id * 12, `+0x20`
  ammunition = the i16 at 0x100eb862 + id * 0xaa, `+8` 1 (-1 for an empty type), `+0xc` the subkind.
- The sim's weapon table: 0x10119382 + type * 0xd6 holds the HUD icon name (`+0x10` the damaged icon), and
  0x100626b0 copies each type's short name ("LAC") in from a language lookup of English defaults.
- Checked in the running original: the Hunter (gdhuntea) has HG 6, LAC 240; the Jaguar (gfjaguar) HG 6, MAC 200,
  LRP32 32, APGL 6. `tools/ref/gen_gear_tables.py` regenerates src/gear_tables.h from HGSIM.DLL.

## HUD (0x10020d48, 0x10020fc0)

- 30 windows, set up per faction (North / South: the HN / HS shapes) and resolution (DAT_10188b44: 0 low, 1 high):
  - rectangles in a 320 x 200 layout, 0x1010b800 (4 sets x 30 x 20 bytes), scaled to the screen by the view transform;
  - hot spots, 0x1010c160 (4 x 30 x 8);
  - the shape names, 0x1010c520 (4 x 119 x 16 bytes: index 0 HNCOMPS0, 1 HNCOMPS1, 4 / 5 HNMSGBX0 / 1, 6 HNDAMGRD,
    7-13 the damage parts, 14 HNWEPNBX, 15-50 HNSPED00-35, 51 HNRPROJT ...);
  - fonts: SIMFNTLO (low) / SERPDEUS (high), 0x1010e2e0.
- At 640 x 480 (North, high), as the running original holds them: compass window 24 at (26, 0) - (232, 221), hot spot
  (94, 94), text at (165, 13); damage window 1 at (519, 0), hot spot (29, 7); weapon windows 3-10 at (523, 135 + 29k),
  hot spot (39, 4), text at (8, 10) / (8, 0); message windows 14 (244, 0) and 15 (244, 31); speed window 18 (0, 2).
- Compass (0x10055924): HNCOMPS1 (the ring, N up) turned clockwise by 360 - the heading about the hot spot, HNCOMPS0
  over it, then the reading 360 - heading (`%d`) in colour 0x0c. Waypoint / target markers and the radar contacts
  come from the nav and vehicle tables.
- Damage display (0x1009f290): HNDAMGRD at the window's corner, then the seven parts at the hot spot through a colour
  remap by damage: 0 -> table 5 (green, the parts' own colours), 1-7 -> 7, 8-14 -> 4 (red), 15 -> 8 (black). The
  software remap tables (0x107c1100, 256 bytes each) move the 8-colour HUD groups (0-7 red, 8-15 green, 16-23 blue,
  24-31) onto each other.
- Message box (0x100b26e6): HNMSGBX1 in window 15 at y DAT_1014cb04 (-195 closed, sliding to half that open), HNMSGBX0
  in window 14 (244, 0) - (517, 233), text position (11, 12). The text (0x100b1c8f): the player's group's first shown,
  open, mandatory objective: its label by the order byte (1 "Primary: ", 8 "Return: ", else "Secondary: "; the
  English defaults of the language lookups at 0x10120180..) in colour 4, then its text in 0x0c; over 20 characters
  (0x10123db8 by resolution) it breaks at the last space within 21 and goes on one line down from the same x.
  Matches the original pixel for pixel ("Primary: Destroy all Enemies" at mission 1's start).
- Speed (window 18, 0x100b336b -> 0x10041c28): HNSPED00, then HNSPEDnn for the *throttle* setting, (int)(control +8 x 11
  / (1/128)) = throttle x 22, 0..21 (skating uses the other set, 36.., 0..13); the readout "%3d" = |velocity| (units a
  tick, gear +0x548) x 6 x 1.38 in colour 0x0c (0x14 backing up) at the window's hot spot, the "Kph" string below.
- Damage display: level = 15 - (int)((internal x 0.5 + armor x 0.5) x 3 / zone +0x38), clamped 0..15, a 15 with
  internal points left reads 14; zone +0x38 = (int)((internal x 0.5 + armor x 0.5) / 5) as built (0x1004e6a0).
  Parts in order (0x10122d88): backpack 7, head 1, torso 2, right arm 3, left arm 4, right leg 5, left leg 6.
- Colour remaps (0x10053946, 256-byte tables at 0x107c1100): 0..3 move colours 0-7 onto groups 0..3 (+0, +8, +16,
  +24); 4..8 colours 8-15 onto groups 0..3 and 0; 9..12 colours 16-23; 13..16 colours 24-31; the rest of each table 0.
- Reticle (0x1005413c) at the aim point's projection (0x10055de4: the 150000 ray at the aim distance, projected):
  the selected slot ready: missiles (shot kinds 0x3e / 0x3f) HNRMISLE / HNRMISLK, lasers (0, 1, 2) HNRLASER /
  HNRLABSY, others HNRPROJT; the second when locked: a target, its distance within the weapon's +0x4c .. +0x50 and
  the aim within 3 degrees both ways (guided weapons: HNRMISLE, HNRMISLK once the seeker locks, gear block +0x560 bit
  0x80). Not ready: HNRPROBY (projectiles), HNRPROJT (missiles, lasers). Name table indices 51..62.
- Target (vehicle +0xec, 0x10055e60): in view, HNTARGT1..4 above / left / right / below its middle at the focal
  length x its standing height / its depth (at least 0x51f of the view), remap 0 / 1 / 2 by side (enemy, friend,
  neutral; 0x10042b07); out of view HOFFLEFT / RIGH / UPWD / DOWN at the edge, remap 9 / 10 / 11. Compass marker
  (0x10055924): HCMPSTGT (enemy) or HCMPSFRN turned to its bearing; the selected nav's HCMPSWPT likewise.
- Nav (vehicle +0x114, none at the start; INPUTW.MAP: N / Shift+N): in view HWAYPNT0..6 in turn (0x100545f7, remap 5,
  7 once visited) and the label "Way Point:<name>" (colour 0x1c) and "Range:" "%3.0fm" up to 1000 m, "%2.2fk"
  beyond (0x0f) in a box of 0x0c (0x10054ae9); out of view the label in window 20 (10, 445). 100 units a metre
  (0x10101204). Navs: the player's group's with NAVP +0x10 set; the name is NAVP +0x1c ("Echo").
- Radar (0x10087b59, mode 1 of the table at 0x1011f568): window 0 (44, 22) - (198, 166), a top-down view on the
  gear, heading up, 200000 units across the window's width; view lines at the view's half angle in colour 0x1c
  (0x10088be7); contacts (shapes from the mode's name table, 0x1011f110): the player's group's shown navs HRHNAVPN /
  HRHNAVPR visited (0x100889c9), vehicles HRHFRGPN / HRHENGPN / HRHNEGPN by side (0x100882b3), the target
  HRH??GPT / nav HRHNAVPT on top (0x100885f3). Enemies whose stealth beats the player's sensors show only within
  a range table (0x1011ef5d); not modelled.
- Targeting keys (SIMx\INPUTW.MAP): T / Shift+T next / previous target, F friendly, Q the one under the reticle.
- Weapons (0x100b9e60): HNWEPNBX, the weapon's icon (by type, table 0x10119382, 0xd6 bytes per type) at the hot spot,
  the name and ammunition with the font.
- Shapes are drawn with their own offsets from the given point (0x100cb3b0 software, 0x100d0d79 3D). Font glyph bytes
  go through a 16-entry colour table that is the identity except entry 0x0e, the text colour (0x107ca800).

- The radar's enemies (0x100882b3 -> 0x10088182): k = the enemy's stealth (+0xbdf) - the player's sensors (+0xbe3),
  at most 4; with k >= 1 the contact shows only within distance / 100 <= {0, 750, 500, 300, 150}[k] (0x1011ef5d +
  k x 0x1d). The builder (0x1004e6a0) sums the sensors from the head record (0x1003e20c: the equipment's +0x10
  (0x100eb85e) plus each fitted option's) and the stealth from the head (0x1003e35d), the torso (0x1003d525, with a
  weight term 0x1003d306) and record 7 (0x1003d845): see "Gear design".

- DISPLAY_OBJECTIVES (command 0x59 toggles 0x1014cb00) draws in window 13 ((10, 100) - (260, 199) of the 320 x 200
  layout; 0x100b21ce): "MISSION OBJECTIVES" (colour 0x14, underlined), then the player's group's shown objectives of
  order 1, 2, 4, 0, 8 (order 0 never shows; not the radio ones; 0x100b15fb): "Primary: " / "Secondary: " / "Return: "
  in 0x14, the text in 0x0c after the width of "Secondary: " (wrapped at 32), the state right-aligned: "Complete"
  (0x17), "InComplete" (7), "In progress" (0x0c); then "Elapsed Time: " / "Time remaining: " hh:mm:ss.ss (181-tick
  seconds, 0x100b13bb) or "Completed at " / "Incompleted at " / "Out of time at " hh:mm:ss (0x100b143b).

## Shell (HGSHELL.DLL)

### Picture table (0x101ddd70) and screens

1,691 records of 41 bytes: name[9], ext[4] (PCX, SHP, SMK, AVI, LOC, TEX, BWD), i32 language (1: the art has a
language variant, PCXX), i32 parent (a record index; a screen's root is its own parent and is named Bnn"UNDER"), i32
kind (5 a picture; 1..4 a button's disabled / rest / highlighted / selected art, named stem + D / R / H / S; 0 an
area: a list's inner area, a selection bar, an animation), i32 x0, y0, x1, y1 (inclusive, relative to the parent).
src/shell_tables.h is generated from it (tools/ref/gen_shell_tables.py), with the font table (0x101ddab0: 10 x 41
bytes, F0E0607A, F0E0C0EB, F0E0607B, F0E0709C, FSERPNTL, SERPDEUT, FSERPNTS, SERPDEUS, INDUSTRI, DEVDDEUT), the
campaign table and the front lines.

Popups (FUN_10028f08: B19 "game description", B72 confirmation) are SHP roots placed at their kind-0 *LOC* record
((159, 163), (192, 152)); some records are their own parent by mistake (B19TINNR), so widgets take their rectangles
from the builders, as the original does.

### State machine (FUN_101377e4)

State n builds screen Bnn (0 main menu, 1 tour of duty, 3 barracks, 5 briefing, 6 debriefing, 7 gear editor, 0xe the
transition that plays a mission's cutscene then goes to var 2, 0xf the landship, 0x13 the new-game popup, 0x14 game
selection, 0x3e situation, 0x48 confirmation ...). A builder creates the screen (FUN_100277c5), its widgets and its
buttons (FUN_1000f68f / FUN_1000f7ce: action count, rectangle, the four pictures) and attaches actions to a button
(FUN_1000dbf4). The actions used so far (constructor: what its execute does):

| ctor | does |
| --- | --- |
| FUN_10014c29 (n) | go to state n (vtable +0x14) |
| FUN_10014cdf (v) | go to the state var v holds |
| FUN_10014e8f (n) | open state n as a popup (vtable +0x18) |
| FUN_10014f3b | close the popup (vtable +0x1c) |
| FUN_10015f82 (v, x) | var v = x (FUN_10029ca0) |
| FUN_100161a0 (v, s) | string var v = s |
| FUN_10016026 (v) | var v = the list's selection |
| FUN_100df6b3 | launch the mission (vtable +0xc) |
| FUN_100df62f | leave the game |
| FUN_100e8d0a (a, b, c) | go to a, b or c by the game's class (tour, story, multiplayer) |
| FUN_100e7fe4 (a, b, c) | the same; for the story it loads the briefing of stat 12 first |
| FUN_100e2421 (a, b) | stat 12 = var a (var b when a is 0x22) |
| FUN_100f0739 (a, s) | var a = stat 12; string var s = its scenario |
| FUN_100ea5b9 | the game list's selection becomes the current game |
| FUN_100181c5 (list, v) | var v = the list item's data (the mission number) |
| FUN_100ea492 | B19's accept: a new game named by the entry, of kind var 0x29 |
| FUN_100ffc4d (v) | load the briefing of campaign entry var v |

Variables: 0 the state after a mission (6), 1 the state to return to, 2 the state after the transition, 0x13 the
confirmation's text, 0x1a the campaign entry, 0x29 the game kind (0 story, 1 tour, 2 multiplayer), 0x2a the
confirmation's answer. The debriefing's continue and abort share a place: continue is shown after a success or
outside the story, abort after a failed story mission (FUN_100e2386, the button's +0x88).

### Campaign (0x101ace60) and the front line (0x101af0a0)

39 entries of 127 bytes: scenario[9], briefing[9], cutscene[9] (ZL_CUTnn, AVI), title[100]. Entry 0 is the intro
(M00_BRF1, B51MOVIE), 1..32 the story's missions, 33 the ending (ZL_CUT16), 34 the random mission, 35..38 training.

The briefing map (FUN_1008994f, drawn by FUN_100af954): three PCX maps (B05_BASE, B05NORTH, B05SOUTH; B62_* full
screen for the situation). For each column x (stepping over the image's bytes per line, 272 for the 271-wide B05
maps, which also scales x), t = the front line's height there, linear between 19 points at x = 0, .1, .15 ... .9, 1
(0x101a9fd8) with heights from the story's table row for the mission (19 floats per entry; other game kinds keep
their own in the save), clamped to 0..1. North map rows 0..(t - 0.15 t^2) H, colour 7 at t H, south map from
(0.15 t^2 + 0.7 t + 0.15) H; the base map between. Then the frame B05MAPOV. Pixel-exact against the original.

### BRF (FUN_101100b6)

name[13], name2[13], text[0x400] (the summary, at 0x1a); i32 n + n x 0x4a0 debriefings (name[32], title[128],
text[0x400]; [0] success, [1] failure); i32 n + n x 0x2d objectives (name[32], file[13]); i32 n + n x 0x33 map
markers (f32 x, y in 0..1, icon[13] upper-cased, label[30]); f32 x4, the situation text's rectangle in 0..1; text
[0x400] the situation; i32; i32 n + n x 21 narration parts (i32 seconds, i32, wav[13], '.' cut).

### Text (FUN_10032e5f, FUN_10033a59, FUN_101833ab)

A glyph's bytes go through the window's 256-entry table, the identity but for 0x0e, the text colour (FUN_10034a24);
0xff is not drawn. Left-aligned text: lines break at '\n' and past the width at the last space, tab or '-' (dropped;
a line without one loses the overflowing character); a tab becomes 10 - x % 10 '{' glyphs (1 pixel wide, empty).
Centred text: x = (width - the width up to the overflowing character) / 2. Wrapped height (FUN_10034645): lines x the
font's height. The briefing's summary: DEVDDEUT, colour 0xfe in (353, 195)-(619, 365); the situation: SERPDEUS.

Lists (FUN_10002f9f / FUN_1000364b): the selected item in the selection bar, centred in its height; the items before
it upward from the bar, those after it downward, clipped to the inner area; DEVDDEUT, 0xfe. The game list matches the
original pixel for pixel.

### Saved games (CHARINFO.HG1; FUN_101371a2 / FUN_101373bf)

i32 count, i32 x3, i32 the records' size, zlib data (compress / uncompress; the original writes as many bytes as the
uncompressed size, so the file has junk after the stream). A record (FUN_10111ce0): two strings (name, description;
i32 length, i32, UTF-16 x length + 1), i32 type (2 story; 0 / 1 tour; 3 / 4 multiplayer), i32 stat[26] (2 rank, 10
missions reached, 12 the current one), i32 [8], [14], [22], 10 x 20 bytes, f32 front[19], 128 bytes, 10 strings. A new
story game (FUN_10110eaa): stat 7 = 1, 8 = 1 << stat 7, 10 = 1, 12 = 1, 16 = 1, rank 3, the 20-byte records start
with 1; the rest 0. It matches a save written by the original byte for byte.

## Sound

The shell plays its WAVs through DirectSound as 22,050 Hz 8-bit mono, copying the whole archive record (header
included) into the buffer (FUN_1003293b, FUN_10032bb2). Two channels:
- the screen loop (FUN_1003293b): looped, volume -1500 (-15 dB). Entering a state (FUN_10029a8a) starts the screen's
  sound, FUN_100277c5's fourth argument: -1 keeps the playing one, -2 none, else an index into the WAV table at
  0x101ddc50 (41-byte records: 1 AFXSHABK, 2 AFXSHABR, 3 AFXSHADB, 4 AFXSHANB, 5 AFXSHASB, 6 AFXSHAVB). B00, B01, B15,
  B20: 6; B05, B12, B16, B24, B25, B46, B47: 2; B06, B17, B39: 3; B03: 1; B02: 4; B09: 5; the rest and every popup -1.
- the voice (FUN_10032bb2): one at a time, full volume. The briefing's narration (FUN_10072f4e) plays part i's WAV
  once clock() / 1000 reaches the sum of the earlier parts' seconds (BRF narration list); M01's one part is
  M01_BRF1 (21.3 s, listed as 22).

Music (HGSIM): the mission record's music name, "track<n>" (sscanf "%d" past the first five characters,
0x10015cdc), is CD track n, played by MCI from the start of track n to the start of track n + 1 (0x1008b8e9) and
restarted when it stops (polled every 0x169 ticks, 0x10015e1a). With a wavmap.ini in the game folder ([WAVMap]
DVDDrive, Track1..Track40, DVDMoviePath; 0x10015a38; a DVD support path whose release is not known) the sim plays Track<n>'s WAV file instead. Missions 1-32
use tracks 2-9; the game CD has audio tracks 2-12. The shell itself plays no music.

## Sim: the player's gear moving

- Time: the sim's clock counts 5 ms ticks (0x1001fe62 sets the period to 5; 0x1001ff23 makes each frame's delta
  DAT_10130770 in ticks). Speeds are units per tick.
- Vehicle classes (0x1010b5a8, 32 bytes each: type, init, then the class's functions): types 1, 2, 4, 5, 6, 8, 9, 10
  share init 0x100623d5 and 0x1005cf76 / 0x1005d75c / 0x10060491 / 0x100620b8 / 0x10062363 / 0x10062387; type 3 has
  0x10094777...; type 7 0x1002bb2b... The player's control block is 0x101334d8 (0x5c bytes; others +0x29c of the
  vehicle): +8 the throttle (0..1/64), +0xc the turn (-1024..1024), +0x59 the skate toggle.
- 0x10060491 (the gear's control step): throttle target (+0x78) = control +8 x the factor (+0x500, 1) + 1/64; it
  lags into +0x7c; the speed target (+0x28) = (+0x7c - 1/64) x the top speed (+0x4d4); halved and negated when
  skating backwards (+0x59). The turn rate (+0x38) = turn / 1024 x 90 x 1.1 (0x101013a8, or x 1.5 / x 0.55 by the
  gear's state) x cos(an angle, 0x1006f3e6).
- 0x1004e6a0 (the gear builder): top speed = speed points x 47.1744 (0x10101154). The points are the sum of the
  engine-type equipment's table values (0x100eb862 + id x 0xaa, plus the options in 0x100ff068 by the record's
  bitfields), less an overload penalty from weight / capacity (0x1003d251: 0, -2 ... -8).
- 0x1005d75c integrates: acceleration = (target velocity - velocity) / 45 when the frame is under 45 ticks (else /
  the frame), position += (frame x acceleration / 2 + velocity) x frame; gravity 0.0296 (0x10110088).
- MEASURED (tools/ref/simlog.py, mission 1, the Hunter): top speed 330.221 (7 points), so 5.155 units per tick at
  full throttle, shown as 40 km/h. The keys 1..9 set the throttle to 0, 1/9 .. 8/9 (x 1/64) and 0 to full. Left /
  Right put -1024 / 1024 in the turn. The heading turns at 85.0 degrees per second at full speed (+0x38 reads 77.196,
  x 1.1), 99 standing. The speed, velocity and turn rate behave as first-order lags of 45, 125 and 54 ticks.

## The other vehicles (GPS placement, traced; checked against the running original)

- Every GPS record but the player's (role 0) is a vehicle; its GDF names the model. DoFirstObjtv (0x10066a68 ->
  0x1004271a) puts each group's leader on its objective-0 navpoint (the player's rule) facing the nav's heading; the
  others go to their formation slots (0x100428c2): the group's formation slot offset, less the leader's slot offset
  (0x10042603), through the leader's matrix (0x10051940), facing the slot's own heading.
- Formations: FTBL chunks (0x100bfca4), up to 32: name (16), then slots of i32 x, z, heading (x 1/65536, 0x10103868).
  A group's formation by name (0x100424be, case-insensitive): GRP chunks (0x100c0005, 16 names of 0x11) and STAR
  chunks (0x100bfeee: per group 0x18 bytes, `+4` the group's side, `+8` the formation name; /OE= and /OF= on the
  command line override it). A vehicle's slot is its order within its group (the GPS loader's per-group counter,
  0x107c8ee4; INFERRED from the decompile). Mission 1's groups all fly "Star".
- Sides: STAR `+4` (0x107c8ef4, read by 0x10042b07). Mission 1: 0 for the player's group, the Orca and the
  Redjackets, 1 for the rest.
- Heights: on the ground at the standing height (the SOL_ point), except the vehicle class 5 (the Orca), which starts
  at ground + 4000: the gear block's +0x520 is set to 4000 for classes 5 and 8 (0x100c4e37 on). The class 8
  Redjackets stand on their soles; the difference isn't traced.
- The vehicle class: after the GDF's parts, a 0x3c-byte block (first word 1 or 2, the GDF's version), whose `+0x28`
  (the template's `+0xb4`) SimMain switches on: 1 gears, 2 (read as 4) ground vehicles, 5 / 8 aircraft, 9 infantry.
- Checked: all 11 of mission 1's vehicles at the original's positions and headings at the mission's start.

## Vehicle models (GDF skeletons)

- Parts (0x438 bytes): `+8` the segment type, `+0x10` the damage zone (0 in infantry and vehicle GDFs), `+0x54` next
  sibling, `+0x78` first child, `+0x7c` six detail-level model names of 0x20, `+0x15c` the part's pivot, `+0x168` ten
  links of 0x48 (`+0` 1 when used, `+0x24` x, y, z).
- 0x1004da65 / 0x1004dc8f build the object tree: the k-th sibling sits at the parent's k-th link less its own pivot;
  a part with no model still takes a link; a model name starting 'g' (the gears') means the next detail slot's name
  is the one drawn (gd12trhp for gd11trhp). The part's `+0x10` goes to the object (the hit zone).

## Damage zones (traced; every zone of mission 1's vehicles checked against the running original)

- The gear block keeps 17 zones of 0x3c bytes (`+0x88` -> block + 0x1591): `+0` armor, `+4` armor at the start,
  `+8` internal, `+0x36` flags (0x2000 destroyed).
- Version 2 GDFs (gears, 0x1004e6a0): equipment record kind k (1..16) is zone k. Armor = the record's `+8` (+5 / +10
  by the template's `+0xc4` bits 1 / 2). Internal: kinds 1 and 8..16 the equipment table's points (0x100eb852 + id x
  0xaa); kind 2 three options, kinds 3 / 4 and 5 / 6 five: (points + each option's add) x each option's multiplier,
  truncated. The options are 4-bit fields of the record's `+0xc`, each plus a base (2: 6, 9, 0xb; 3 / 4: 0xe x 3,
  0x16, 0x13; 5 / 6: 0x18 x 3, 0x1d, 0x20) indexing 0x100ff058 + n x 0x98 (`+0` add, `+4` multiplier). Kind 7: `+0xc`'s
  low 16 bits, over 950 only a fifth of the excess counts (0x1003d744). All internals x an overload factor
  (0x1003d0a5), 1 for the gears checked.
- Version 1 GDFs (vehicles, infantry, 0x1004f86d): the kind picks the zone (1 / 2 -> 2, 3 -> 1, 5..0xc -> 4,
  0xd..0x14 -> 3, 0x16..0x1a -> 6, 0x1b..0x1f -> 5, 0x20 -> 7, 0x21 -> 14, 0x22 -> 16, 0x23 -> 13, 0x24 -> 15,
  0x26 -> 11, 0x27 -> 9, 0x29 -> 12, 0x2a -> 10, 0x2c -> 8); armor `+8`, internal `+0xc` (plus bonuses for some ids,
  not modelled).
- The player's Hunter reads 6 x the internal points in the running original (MEASURED; where it comes from isn't
  traced).

## Weapons and shots (traced)

- The weapon table (0x101192f0 + type x 0xd6): `+0` a per-tick function, `+4` the shot function (0x100bb425 shells,
  0x100baf31 beams), `+8` flags, `+0x10` the shot kind, `+0x18` the sound id, `+0x20` keep firing while held,
  `+0x2c` the burst, `+0x30` / `+0x34` rounds per magazine, `+0x38` the shot's gravity factor, `+0x3c` its speed
  (units per tick), `+0x54` the reload and `+0x58` the interval (ticks), `+0x5c` its life (ticks; speed x life is the
  range), `+0x78` the power, `+0x7c` the damage multiplier, `+0x80` the chip damage, `+0x84` (u16) the damage mode,
  `+0x92` the HUD icon, `+0xb2` the firing sound.
- Slots (0x100ba550): a pull on a ready slot starts a burst; a shot every interval ticks (the pull's tick counts),
  each using a round; then the reload; weapons with `+0x20` fire again while held. The slot's `+0x28` adds to the
  power: by the record's ammunition option (`+0xc` low 16 bits -> the option's `+0x1c`: 1 -> +5, 2 / 3 -> +10).
  Version 1 vehicles: the weapon records are those of weapon zones; type = the record's `+4`, rounds = `+0x68`
  magazines x `+0x30` x the burst (checked: the Antelope's LAC 1200, the infantry's LMG 1600, the Redjacket's 4 / 8 /
  200).
- Shots (0x100bb425, a pool of 0x200 at 0x1078d0e0, 0x156 bytes each): velocity = the aim direction x speed; vertical
  acceleration = -0.0296 x the gravity factor; position += (acceleration x frame / 2 + velocity) x frame, velocity
  += acceleration x frame (0x100191d7). The aim direction: from the mount toward a point on a 150000 ray along the
  vehicle's aim, at its aim distance (the vehicle's `+0xb0`; its setter isn't traced).
- Mounts: the POF point a gear's equipment kind names ("POF_AWLA" + n x 9 at 0x10100ec0: kind 8 EWEN, 9 AWRA,
  10 HWRH, 11 AWLA, 12 HWLH, 13 TWRU, 14 TWLU, 15 TWRL, 16 TWLL).

## Damage (0x1005c1f8, 0x1005ab68; traced)

- A hit on zone z: penetration = power - armor (modes 3 / 4: power - armor / 2). Penetrating: d = (int)(penetration
  x multiplier); with armor left, mode 1 takes d off the armor, mode 2 d / 2 off both, modes 3 / 4 d off the
  internal; armor below zero spills into the internal. Not penetrating: the chip damage comes off the armor the same
  way. Internal at 0 destroys the zone (0x1005a9c6 -> 0x1005ab68): zone 1 destroys a gear or infantry; zones 2 and 7
  destroy 3, 4, 1 and the vehicle; 8 and 17 the vehicle; 3 / 4 (arms) and 9..16 (weapons) come off; 5 / 6 (legs)
  cripple. Infantry (class 9) play AFXINFD1..3.
- The hit's zone is the object's (the part's `+0x10`) the shot's segment strikes (0x100229d9, not traced).

## Objectives and the mission's outcome (traced: HGSIM 0x10066090 .. 0x1006932f)

- **MTBL** (0x10066090), one per group: chunk `+8` group, `+0xc` the group's time limit (-1 none), `+0x12` / `+0x1d`
  the mission's success / failure voices; objectives of 0x97 bytes from `+0x26`, each: `+0` type, `+4` 'V' shown in
  the objectives list, `+6` all conditions (else any), `+0xa + k * 4` eight conditions (letter 'C' done, 'S'
  succeeded, 'F' failed, else none; then the objective and the group), `+0x2a` time limit, `+0x2e` order, `+0x2f`
  'M' mandatory ('N' / 'O' not), `+0x34` / `+0x3f` its success / failure voices, `+0x4a` the BTAG of its targets,
  `+0x53` / `+0x55` the group and objective an action works on, `+0x57` its text. Runtime records: 0x10290cba +
  objective x 0x13f + group x 0x4fc3a (`+0` state: 3 pending, 5 done, 6 failed, 8 failed by an AI group;
  `+0x67` / `+0x6b` start / end, `+0xee` target count, `+0xef` targets (index, kind)).
- Targets (0x10066794, as each chunk loads inside a BTAG): navpoints (kind 1; NAVP `+0x1a` mask), vehicles (2; GPS
  `+0x18` flags) and structures (4; GT `+0x12`), masks widened by 0x100c1120, matched by BTAG name and mask, 0x28 per
  objective at most. The player's own record (USERSTAR, flags 0x4006 from the shell) is a vehicle target too.
- Time: sim seconds of 181 ticks (0x1028fc10 = clock / 0xb5).
- Each objective steps (0x100684db) when active (0x100683d6: not done, conditions met; 0x10 and 0x8000..0x8002
  always): 1 / 2 every target destroyed (vehicle `+0x18` & 0xe, structure `+0` & 0xe); 4 a target destroyed fails
  it, its time running out completes it; 8 / 0x20 a target nav visited by the group (0x20 also the group's other
  mandatory objectives done); 0x100 the group's leader within a target nav (radius NAVP `+0x18` x 100, 20000 if 0;
  aircraft, classes 5 / 8, measured flat; an AI group only for its current objective); 0x2000 the leader outside;
  0x4000 a target hit by the group (vehicle `+0x14c`); waits 0 / 0x200 / 0x400 / 0x800 complete when their time runs
  out; others fail when it does. Actions: 0x10000 / 0x20000 the group (arg) wins / loses, 0x40000 toggles an
  objective's display, 0x80000 / 0x100000 fail / complete one, 0x200000 / 0x400000 reset a group / one.
- The group's mission (0x1006932f): won when every mandatory objective is done, lost when one failed, timed out at
  its limit (state 2 / 3 / 4, 0x10290cb8). The player's group ending sets 0x10131968 (the end of the sim). A group's
  current objective (0x1028fc20) is its first active one whose type is below 0x10000.
- Announcements (0x10067cfc, once per objective, 0x1028fc60): "<text> <completed / failed>" (blank for hidden
  objectives) and the objective's voice through the radio queue (see "Voices"); the mission's own voice when the
  group is won / lost / out of time (0x100680d0: MTBL `+0xa` / `+0x15` / "BET68"). In both, a voice name holding
  "avon" gets 's' as its fourth letter when the player's gear (+0x613) is South (1), one holding "avos" 'n' when
  North (0).

## Voices (the radio queue)

- Every spoken line is a sound by name: "WAV" through the file lookup, so the English variant WAVX in the archive
  (avon* North, avos* South, avom* the missions' own lines). First, 0x1006a7a6 tries `<name>.sfl` in a KEATING
  folder (0x1006a65c: the game folder, then the CD root, "%s\\*.sfl"); neither disc has one, so its handle is 0.
- The queue (0x10016aa0): 8 slots at 0x1012fe88 (0x6b bytes: +0 used, +4 name, +0x14 second name, +0x24 / +0x28
  the names' .sfl handles, +0x2c the text's end time, +0x30 priority, +0x34 text of 0x33, +0x67 next). The caller
  passes a record {name[16], text*, sfl}. Priority: the sound table's 0x107f9780[id * 8] (id from the name registry
  0x10007abc; the table is -1 but for the 0x2c effects of 0x10108d08). Inserted before the first waiting message of
  lower priority; a waiting message with the same names is a duplicate (dropped when the table's +2 count is -1).
- Starting (0x10016f28): sound off (0x1012d488 / 0x10108c30 bit 2), the text is shown for 0x712 ticks instead.
  Sound on: one name plays it (0x1000825f flags 0x2b11); two need both sounds to exist (0x100a2bc9) and play the
  first with the second chained (0x5622, the channel's +0x334). 0x101301e4 is set while it plays; the sound step
  (0x1002dc30) plays the chain or clears it, and 0x10016e7d moves to the next message that starts.
- The gear computer (0x100176df(subject, verdict)): subject 0x1010a4a0 + faction x 0x420 (44 names of 0x18: IRED,
  LOWL, APLT, IDTR, SENS, CAMR, ENGE, ENGE, ARM_, LEG_, GDMC, FIRE, TORS, TORL, HEAD, COCK, SATL, WDED, the weapon
  kinds MGU_ .. MIS_, INCM, GRPF, GDED, TDED, VDED, ADED, EDED, SATC, HYDR, INTW, POWL, JJET, each "AVON" / "AVOS" +
  it), verdict 0x1010a3e0 + faction x 0x60 (DISE, ENGA, DAMG, DEAD; -1 none). A vehicle destroyed (0x10059e9d) that
  is the player's enemy (0x10042b07 = 1) says GDED / TDED / VDED / ADED / EDED for its type 1 / 3 / 4 / 5 and 8 / 10.
  The damage reports come from the critical-hit table (0x1005b5c6: e.g. 0x1c20 IRED DEAD, 0x1cb6 ENGE DAMG ... and
  9000, a weapon destroyed, by weapon type 0x1005bfca).
- The radio commands (0x100173e1 / 0x10017566) pick wingman replies from the AVOSH001 .. tables (0x1010a128); not
  done.
- Mission 1's table runs on its own data: start objectives complete at once; actions pre-complete "Destroy Returning
  Patrol" and "Destroy Fleeing Antelope Jeep" and re-open them when the player reaches Echo (WPPL01), which also
  starts the Antelope's flight (group 5's timer and its chain of go-tos WPEN51..57); leaving 240000 from the start
  (WPPL99) loses the mission; losing the player's gear fails "Defend Your Unit" and loses it; destroying every enemy
  and the base's 13 structures completes "Defend Your Unit" by action and wins.

## Sound effects (traced: HGSIM's sound calls) - src/sfx.c

- Sounds go by name through a registry (0x10007abc: name -> id, counted) and the sound block 0x1012d488 (16 channels;
  channel 0 is the radio's, 0x10007bf9). Channel choice: a free one, else the last whose priority (its flags' low
  byte) is below the new flags; a channel playing the same sound refuses the new one when both carry 0x100 (or both
  0x400), and with both 0x200 the old one stops. 0x2000 loops. Volumes are DirectSound's hundredths of a dB; under
  -6000 nothing plays (0x1000825f). The frequency argument is overwritten (the sound table's +4, else 11025), so every
  sound plays at its own rate.
- Flat sounds (0x10008723 name, volume, pan, -, priority), looped (0x10008788), placed (0x1000863a x, y, z, name,
  quiet, priority, vehicle): x, y, z the camera minus the source (some callers pass the opposite); distance d gives
  -2500 x (d/100)^2 / 250000 (0x10008bbe, silent past 50000), 1000 less when `quiet` (the callers pass the cockpit
  view's flag 0x101316f4, set by 0x10037a72), a start delay of d x 0.00549 ticks (the sound's travel), and the pan
  sin(camera heading - bearing) x 10000 held to +-7500 (0x1000889f, the bearing from 0x100b0d4b's atan2).
- Shots (0x100ba550): a weapon whose flags (+8) have bit 0 or 1 plays its sound (+0xb2) as each shot leaves: the
  player's own flat in the cockpit, others placed; bit 1 adds 0x400 to the priority. An empty trigger plays AFXWPMTY
  at -10 dB (-30 dB for a missing slot).
- Hits on a vehicle (0x1005c1f8): pierced, the weapon's hit sound (+0xc2, AFXWI*); not, AFXWISTB (the player's own:
  AFXWISTB or AFXWISHR at random, -20 dB). The player's flat at priority 0x50, others placed at twice their offset,
  priority 10.
- Effects (0x1001b3a1, the table 0x1011b968, 0x3c bytes each, 0x4d of them): a shot's kind (+0x10) picks the effect,
  changed by what it struck (flags from 0x100191d7: 0x100 a vehicle, 0x4000 the player's, 0x2000 its zones 1 / 2,
  0x400 an object of type 0x800): e.g. kinds 1 / 2 / 6 on a vehicle 0x4b, 4 / 0x39 / 0x3c 0x4a, 5 0x2a, 0x3d / 0x40
  .. 0x42 0x2a. The effect's +8 > 0 and its chance +0xc (percent, -1 always) let its sound (+0x2c) play, placed -
  but only once the effect has started (see "Effects": a kind with no pool returns first, silent).
- The player's gear (0x100620b8): at the start AFXHGENG at -20 dB; after 0x43e ticks (and a random part) it is
  running (state 2, flag 0x2000) and AFXHBEEP sounds. Weapon select AFXHDWPS. Missile lock AFXHDMLG pan 5000, lost
  AFXHDMLD pan -5000 (0x10021a8d). Skating AFXHGWEN / AFXHGWHE (looped) / AFXHGWDE; jump jets AFXHGJJT / AFXHGJJF;
  footsteps AFXHGWKL (0x1003f7b0's step event); landing AFXHGCOO / AFXHGJLR; infantry deaths AFXINFD1..3.
- A nav reached by the player (0x100673af / 0x10067855, its +0x10 set): its flag 0x20, AFXHDSAT at -25 dB.

## Critical hits (traced: 0x1004e320, 0x1005b5c6) - src/combat.c

- Gears only (class 1). The crit slots (+0x617, 0x50 per zone, counts +0xb67) come from a table by the gear's type
  (+0xbdb: 1..5 from the torso record's id in the builder's switch, 0x1004e6a0) and the zone's group (0x1010e670:
  zones 1..17 -> 0 1 2 3 3 4 4 5 6 6 6 6 6 6 6 6 0). The fill writes slot list z - 1 from group[z - 1], the crit code
  reads list z (the zone number): zone z uses group[z]. Slot ids: 0x1b8a the zone destroyed, 0x1bbc nothing, 0x1bee
  the low-light / satellite uplink (if fitted), 0x1c20 IR / autopilot (if fitted), 0x1c52 camera, 0x1c84 / 0x1cb6
  engine (the throttle factor +0x500 x 0.9 / x 0.5), 0x1ce8 engine dead (x 0), 0x1d1a / 0x1d4c sensors (+0xbe3 halved
  / cleared), 0x1d7e arm actuator (+0xbd7 - 1), 0x1db0 leg actuator (x 0.9), 0x1de2 hydraulics (x 0.9, +0xbd7 - 1),
  0x1e14, 0x1e46 torso (twist stops), 0x1e78, 0x1eaa power (a 0x21f-tick stun), 8000 cockpit, 9000 the zone's weapon
  (0x1005bfca: off, reported by its type). A used slot becomes 0; a 0 passes on to the next slot.
- When a hit reaches the internal points (and the zone stands): two rolls (0x1005c1f8), slot -5 / -1 / 0 / 1 / 5 by
  5 / 20 / 50 / 20 / 5 percent, plus the damage / 50, held to the zone's count.
- A leg destroyed (0x1005ab68, zones 5 / 6) sets +0x500 to 0: the gear can't move.
- The player's report for each (0x100176df, only while the gear runs): see "Voices".
- Difficulty (0x1004e4cc): for the player's side (the vehicles whose player record's +8 is the player's) in a
  single-player game every zone's internal points are multiplied by 12, 6 or 1 by 0x1014a564 + 5 (0, 1, 2).

## Radio menus and the keys (traced: GAMEKEY.MAP, 0x10114dc7, 0x1003b3ba, 0x10063231)

- GAMEKEY.MAP in the game folder names the sim's commands' keys (the command table 0x10114dc7: id byte + name): F1
  MISSION_CHANNEL_MENU, F2..F7 SQUADMATE1..6_CHANNEL_MENU, F8 radar map, F9 target display, F10 ordinance view, F11
  HUD, F12 DISPLAY_OBJECTIVES, c cockpit view, 1..0 throttle, x / Shift+x radar zoom, a autopilot, i infrared, l
  enhanced vision, Ctrl+Alt+x self-destruct, Alt+p pause.
- A channel key (0x1003b3ba, commands one above the table's ids) opens its menu (0x10062afd: toggles; AFXHDMSB) when
  the squad mate is there (0x1000edfa), else AFXHDWPS. In an open menu the keys '0'..'9' pick an item, Esc closes
  (0x10063231).
- The mission channel's three items (0x100096c3 / 0x100097a7 / 0x1000988b) are the player's group's last shown
  objectives of type 0x8000 / 0x8001 / 0x8002 ("NO MESSAGE" when none). Picking one (0x10009d0e) changes its type to
  0x10000: at its next step it completes (its arg group is -1 in every shipped mission), speaks its voice, and the
  objectives waiting on it go on (the air strikes, the Orca's calls ...). Objective 0 can't be sent.
- The menus are records (0x10105bbc .. 0x10106ae8): a header {title, ?, item count, ...} and items of 0x14 bytes
  {text, handler 0x10064a52, its command record (0x101057c0 + n x 0x28, the function at +0x24), 0, type}. The six
  SQUAD MATE n CHANNEL menus (0x101063b4 ..) have Attack My Target (0x10009aa4: order 2), Flank my target (0x1000996f:
  0xd), Defend my target (0x10009bd9: 7), Scout my target (0x100099d6: 0xe), Get to cover (0x10009a3d: 0xf), Return
  to Formation (0x10009b72: 5) and "(Esc to exit)"; the lance menus (Status, Attack, Flank 0xb, Defend, Return, Engage
  at Will 3) belong to the command channels. Disengage isn't on any menu.
- An order (0x1000f2b0(mate slot, order), the mate the player's group's vehicle with +0x10 == slot, running): 2 needs
  the player's target (+0xec): none - nothing; the mate itself or a friendly vehicle, or a 0x100 object - refused
  (reply 0xc); else taken (reply 1). 0xd needs a vehicle target other than the player (reply 8). 7 any target but the
  mate (reply 3). 0xe the player's nav (+0x114, reply 10). 0xf a hostile to hide from with cover within 30000
  (0x1000f0f4 / 0x100279a3: reply 9; none 0xb, not sent). 5 always (0x2201, reply 2). Refusals reply 0xc. Taken
  orders go to the mate (0x1000eec9(player, mate | 0x200, order, target)).
- Replies (0x10017566 -> 0x100173e1(slot, line)): 1 line 2, 2 line 7, 3 line 3, 4 line 5, 5 line 6, 8 / 9 line 4, 10
  line 1, 0xb line 9, 0xc line 8; others (0, 6) the "ALLPOINT / P2 / P3" + 0x1010a200 pair. Line n of the mate's set
  (0x1010a128: 20 sets of 10 names, AVOSH, AVOSR, AVOS3..8, 14, 00, 13, 9, 15..19, 2X, 11, 12); slot 0 speaks both
  the first and second mates' lines; line 0 alternates the set's first two (the STARMATE_SET_* keys, 0x10009e91).
  Mates 1..4 only. A mate's set (0x1010a178 + (slot - 1) x 8): its vehicle record's +0x192 at load (0x1000a223 ->
  0x10017766: 0xffff 0, below 9 + 1, else - 1; slot 4 is skipped), -1 (silent) when never set.

## Cutscenes (traced: HGSHELL 0x100ffd56, 0x10147527, 0x1010699f, 0x100fc5b4) - src/movie.c

- The transition state 0x0e plays the campaign entry var 0x1a's movie (the entry's +0x12; at entry 0x20 with the story
  done, 0x21's ZL_CUT16: 0x100ffd56 switches when var 0x1a is 0x20, the game's stat 11 is 0x20, var 1 is 6 and HGCAR
  +0x10 is 2) through 0x100319d5, then goes to var 2. The player (0x100319d5 -> 0x10022dce, DirectShow): B00BACK1 /
  B00BACK2 are drawn on the screen, then a popup window over it holds the video window: 640 x 480 at (0, 0), black
  border (IVideoWindow Left / Top 0, Width 0x280, Height 0x1e0, BorderColor 0); the video twice its size
  (IBasicVideo DestinationWidth / Height = 2 x its own) at DestinationTop 0xf0 - its height, DestinationLeft left
  at 0. WM_KEYDOWN, WM_LBUTTONDOWN or WM_QUIT end it. NDEATH / SDEATH
  play when the pilot dies, by side. The files: wavmap.ini's [WAVMap] DVDDrive / DVDMoviePath, else the CD's VIDEO
  folder. Disc 2 holds them: Indeo 5 (IV50) 320 x 192 (B51MOVIE 320 x 240) at 15 frames a second with 11025 Hz mono
  PCM.

## Shell animations (traced: HGSHELL 0x10031e3d, 0x10032469, 0x100323c3) - src/smk.c, src/shell.c

- A screen's builder lists its Smacker animations (FUN_100158a0: picture record, mode, buffered): B02 0x45 / 0x46 loop,
  0x48 / 0x49 / 0x47 still; B09 0x21f rests, 0x222 loops, 0x21e / 0x220 / 0x221 still; B15 0x241 / 0x242 loop,
  0x23e / 0x23f / 0x240 still. Files: SMK\<name>.SMK in the game folder, drawn at the record's place in the screen's
  own palette (SmackToBuffer into the 8-bit screen).
- Modes (0x100323c3, the end of a run in 0x10032469): 0 plays once, 1 loops, 2 loops with rests (a run's end notes
  time(); once more than 5 seconds have passed, each pass of the loop restarts it when (rand() & 0xfffff) == 0, MSVC's
  rand being 15 bits: 1 in 32768; srand(time) at the start, FUN_1002fb63), 3 plays once while the shell waits (0x1003236d: the next actions run after it), 4 waits. A button's
  action FUN_10015965(slot, mode) changes one: the landship's doors play slot 2 / 3 / 4 in mode 3 before going to the
  briefing / barracks / game list.
- The Smacker files: SMK2, no sound tracks (and 0x10031e3d turns Smacker's sound off); the port's decoder matches
  FFmpeg's output on all 16 files. The archive's b15d1smk / b15d2smk / b15ucsmk WAVs are named by nothing in the
  binaries: the animations are silent.

## Gear design: the builder's sums (traced: 0x1004e6a0 and its helpers 0x1003d0a5 .. 0x1003e6e9) - src/gear.c

- Per version 2 record (kind, id, armor +8, options +0xc): the equipment table 0x100eb846 + id x 0xaa (+0 a weapon's
  weight, +4 capacity, +8 the torso's full armor, +0xc internal, +0x10 handling, +0x14 stealth, +0x18 sensors, +0x1c
  speed, +0x20 the speed cap), 0x10101d90 + id x 12 (weight, +4 a weapon's capacity), the options 0x100ff058 + n x
  0x98 (+8 handling, +0xc stealth, +0x10 speed, +0x14 cap, +0x18 sensors) and their weight / capacity 0x10103398 + n x 8.
  Option indices: torso fields (+0xc bits 0-3, 4-7, 8-11) + 6 / 9 / 0xb; arms (5 fields) + 0xe, 0xe, 0xe, 0x16, 0x13;
  legs + 0x18, 0x18, 0x18, 0x1d, 0x20; the engine (kind 7) +0xc's high 16 bits; the head's flag bits 0-7, 9-11 are
  options 34 .. 44.
- Sums: speed = torso + legs + (int)sqrt(engine rating, +0xc low 16) + options; the cap = torso + legs + engine +
  options; stealth = head + torso + engine (rating < 400: +1, 600..749 -1, 750..949 -2, from 950 -3) + options + the
  torso's armor against its full armor (equal -3, >= 0.75 -2, >= 0.5 -1); sensors = head + its flags; handling =
  torso + arms + engine (600 / 750 / 950 steps); weight and capacity over every record, weapons included, the torso
  bit 12 adding 500, plus the armor total x table[torso +0x68] + table[torso +0x64].
- The load f = weight / capacity: f > 1.26 the cap becomes 1; else the cap takes -5 (> 1.15), -3 (> 1.1), -2 (> 1.05),
  -1 (> 1.0), and the torso's armor ratio -3 (= 1), -2 (>= 0.9), -1 (>= 0.75) (0x1003d125); the internal points are
  multiplied by 0.25 / 0.5 / 0.6 / 0.75 / 1 (> 1.26 / 1.2 / 1.15 / 1.1, 0x1003d0a5); the speed takes -8 / -6 / -5 / -4
  / -3 / -2 (> 1.26 .. > 1.0, 0x1003d251). Then speed is at least 1 and at most the cap; top speed (+0x4d4) = speed x
  47.1744. The Hunter: 7 points, 330.22, as MEASURED.
- The head's flags give the crit effects' equipment: bit 0 IR (+0xbab), 1 low-light (+0xbaf), 4 autopilot (+0xc35),
  6 backup sensors (+0xbc3), 7 satellite uplink (+0xbb3), 11 (+0xbb7, spares the cockpit); a torso whose second field
  is 0 spares the power crit (+0xbbf); any limb spares the first hydraulics crit (+0xbbb).
- A vehicle starts at state 0 (AFXHGENG for the player), runs (state 2, flag 0x2000) after 0x43e + rand(0x16a) ticks;
  below state 2 its speeds are zeroed (0x10060491).

## Guided shots and beams (traced: 0x100bb425, 0x1001aec9, 0x100baf31, 0x10018a9c)

- A shot whose weapon has +0x86 bit 0 (copied to the shot's +0x7e) takes the slot's target (+0x18 / +0x1c: id and
  type 0x200 vehicle / 0x400 object). Each tick (when its age passes +0x82) the acceleration (+0x24..) becomes the unit
  vector to the target less the velocity's unit vector (vertical only without +0x7e bit 0x20); within 3000 the
  velocity x 0.95; within 100 the shot is flagged 0x8000 and strikes the target. A destroyed target leaves it
  unguided.
- Beams fill a pool of 20 (0x107b7ce0): each tick of their life the ray from the mount along the aim (0x100bcf44) is
  tested; a hit within the weapon's +0x50 counts; kinds 4 and 6 last one tick.

## Debriefing statistics (traced: HGSIM 0x10059e9d / 0x100191d7 / 0x100ba550, HGSHELL 0x100e27c9)

- The sim counts into 0x1028cae0 (0x21c bytes, written as HGCAR.CFG by 0x1001cf2a; the shell keeps it at +0x48 of its
  game object, read by 0x1012f1d8). Offsets: +0x13 the player's shots (one each, 0x100ba550), +0x15 / +0x17 / +0x19
  its shots striking side 1 / 2 / 0 vehicles (0x100191d7, by the struck group's side 0x10042b07), +0x1b shots striking
  the player's vehicle; kills (0x10059e9d, killer the player): +0x46 + kind x 2 side 1, +0x5c side 2, +0x72 side 0;
  by the squad's slots 1 / 2 (0x1000edfa: the player's group's vehicle with +0x10 == n; victim flags +0x18 & 0x1400):
  +0x88 / +0x9e + kind x 2 side 1 (+0xb4 / +0xe0 the others); the slots' deaths +0x142 / +0x144 (not counted while the
  vehicle's state is 5).
- The debriefing (0x100e27c9): Vehicle Kills = +0x48 +0x4a +0x4c +0x4e +0x50 +0x56 +0x5a (kinds 1-5, 8, 10); Hit Rate
  = +0x15 x 100 / max(+0x13, 1), held to 100, with '%'; Hits Taken = +0x1b; Squad-mate Kills = +0x8a +0x8c +0x8e +0x98
  +0x9c and +0xa0 +0xa2 +0xa4 +0xae +0xb2 (kinds 1, 2, 3, 8, 10 of each slot); Squad-mates Killed = +0x142 + +0x144.

## The AI's engage test (traced: 0x100a9020, 0x100ac524, 0x100ac328, 0x100ac1e0, 0x100abf2d)

- An AI node (0x100a9020) sets the vehicle's AI state (+0x1f8): 0x16 (evade) when an enemy shot within
  0x10103708 heads at it (0x100abf2d: its velocity's dot product with the offset above 0x10103660), else 0x17
  (engage) when 0x100ac524 says so, else stays.
- 0x100ac524: 0x100ac328 - in single play the player's vehicle, if of another side, within 15000 (0x1010370c) and
  seeing it (0x1000510e: the player's aim ray, 0x100bcf44, struck first by it or nothing; within an angle when asked);
  in multiplayer every vehicle of kind 0 / 1 likewise - or 0x100ac1e0: the next of its three attackers (+0x208, cycled
  by +0x204) that targets it (+0xec == it | 0x200, or +0x1a6), within 15000 and with its aim ray reaching it
  (0x10004d60).

## The briefing's squad and the launch (traced: HGSHELL 0x1013c185, 0x100ef574, 0x10076b6f, 0x100ff1e6) - src/squad.c

- The BRF record's i32 after the situation text (+0x2316 in the shell's copy) is the squad mask: bit 0 the player may
  go alone, bits 1..10 pilots 0..9 (mission 1 0x6: Janus, Reeves; 2 0x2; 3 0x1; 7..9 0x19; 23 0x7f9 ...).
- Pilots (0x100e7ebe / 0x100e8038): North Janus, Reeves, Warlock, Lucky, Statistic, Eagle, Lynx, Spike, Nihilist,
  Tagger; South Sabre, Viper, Stinger, Hector, Templar, Mouse, Pyro, Gator, Devil, Raven. Alive: the game record's
  +0x25c + k x 0x14 (first i32). Their gears: the record's strings at +0x1bc + k x 0x10 (the file's 10 strings).
- B05's two pilot lists (FUN_10076b6f at (22, 36) and (204, 36), linked so they never show the same pilot; arrows
  B05P1PR / NT, B05P2PR / NT): filled on entering when the game or mask changed (0x100ef574): mask 0 - "No squadmate"
  (-2 / -1); bit 0 - "No squadmate" first, then the living pilots, "No squadmate" chosen; one pilot bit alone - that
  pilot, list 2 "No squadmate"; else the living pilots in both. Each item is added after the list's current one and
  becomes it (0x10026927), so both lists end on their last pilot; then 0x10076f1a runs twice on the second list only
  (0x100ef972 / 0x100ef97d, ecx = +0x14): from its current item on, cyclically, to the first whose pilot isn't the
  first list's. Mission 1: Reeves on the left, Janus on the right, as the original shows (with Reeves dead, Janus in
  both). An item shows
  its portrait (0x11d + k, 0x128 + k North; 0x133 + k, 0x13e + k South: B05N1F01 ...) in (6, 4)-(138, 90) and its name
  (DEVDDEUT, colour 0xfe) from (10, 4) (FUN_1000f034). Choosing a pilot shows his gear (0x100efa6f: his string, or the
  gear list's current entry, stored as his). The gear lists (FUN_1009a07f, 0x1009a38b): the 25 stock GDFs of the
  table at 0x101c56f0 ("GACHEETA.GDF", 13 bytes each) whose record +0xb8 is the side (0 North), that have data after
  their equipment records (+0xd0) and whose tech (0x1005bb6d) is within the game's level (or the list's +0xba set),
  then the game folder's own *.GDF (findfirst / findnext in the current folder, the game's; not USERGEAR or a stock
  name) by the same tests, kept by their file name ("MYGEAR.GDF", the stock ones without the extension); the
  current entry is the last added unless findfirst found a *.GDF, which rewinds the list to its head (see the gear
  editor).
- The player's gear view (FUN_1008f28f at (383, 38)-(506, 127), the editor's view class with the same camera; its
  +0x4c, 0x1008f81d, loads the GDF and rebuilds its design, 0x100c0a61): the model, turning 7 degrees on every pass of
  the shell's loop (idle action 0xf, FUN_100f1446), and over it (FUN_1008f502) the short names (0x101aff98 + id x 0xaa,
  through the language table 0x1018c0e5) of its equipment records of types 5..10, from the rectangle's top left in
  font 9, colour 0xfe, a line each (the font's height, or the rectangle's height / their count when that is less). A
  gear over the game's level (its tech +0xb8 over +0xbc) is drawn with ambient 5 under the message 0x1024db40 (not
  done in the port).
- A gear's tech (0x1005bb6d): the highest over its records of the equipment's (0x101eec48 by id) and its options'
  (0x101ef3a0 by option index): by the equipment type (0x101b0004 + id x 0xaa) - 0 torso: options (o & 15) + 6,
  (o >> 4 & 15) + 9, (o >> 8 & 15) + 0xb and bit 12 option 51; 1: o >> 16; 3: (o & 15) + 0xe x3 (by nibble),
  (o >> 12 & 15) + 0x16, (o >> 16 & 15) + 0x13; 4: + 0x18 x3, + 0x1d, + 0x20; 2 head: bits 1..0x80, 0x200..0x800
  options 34..44; 5..10: o. A new story's North list: Cheetah (0), Hunter (0); the South's Iguana, Jager.
- The game's level (0x1011238e(0)): the experience (record +0x24 = stat 0, a third outside the story) against
  0x10112146's ladder (200, 500, 800, 1100, 1400, 1700, 2000, 2300, 2700, 3100, 3500, 3900, 4400, 4900, 5400, 5900,
  6400, 7000, 7600, 8200, 8800, 9500, 10500, 11000, 11500, 12000: level = thresholds reached, 0..26); the side's
  prestige (+0x8c [2] North / [6] South): <= -201 - 2, <= -101 - 1, > 100 + 1, > 400 + 2, > 800 + 3 (to 26); the
  chapter (+0x40 = stat 7): 0, 10, 11 at most 17; 4 at least 4; 7, 9, 0x12 at least 13; 0xd at least 2; 0xe at
  least 8; 0x11 at least 17.
- Launch (B05 button 0x11): vars 0x13 / 0x14 = the lists' pilots (0x1001c505), then 0x100ff1e6 writes USERSTAR.BWD:
  0x5a-byte GPS records - the player (group 0, +5 leader 1, +6 role 0, +0x18 0x4006, +0x1a 0x400, gear USERGEAR),
  wingman 1 (leader 0, role 2, +0x1a 0x480, voice +0x16 = pilot - 1 North / + 9 South, his gear) and wingman 2 (voice
  = pilot). The sim gives each vehicle of the player's group its slot's reply set (0x1000a223 -> 0x10017766).
- The launch also keeps the two pilots' numbers in the shell's mission parameters (+0x33e / +0x33f of HGPRM, - 10
  past 9). B06's continue (FUN_1013ee54, its second action 0x100df783) reads HGCAR's +0x142 / +0x144 (the slots'
  deaths): a dead wingman's pilot record's first i32 becomes 0, so the lists leave him out from then on.

## The gear editor (traced: HGSHELL FUN_1013fa9f, 0x100f267f, 0x100f4562, 0x100f2d9d) - src/garage.c

- Reached from the briefing's "build" (B05BILD: var 1 = 5, var 5 = 1, state 7); cancel goes to var 1's state
  (FUN_10014cdf), accept too after its actions: FUN_100f08c0 makes the gear name selector's gear the game's (the
  record's +0x14), before it 0x100fc12d, 0x100fb6b8, 0x100f15cf (the design's saving, not traced) - all behind
  0x100f2585, which first asks "Are you sure you want to accept this gear with such a low maximum speed?"
  (0x1023d720) when the design's top speed is too low (not traced). Clear asks "Are you sure you want to clear your
  gear?" (0x1023d828) through the confirmation popup (var 0x2b = 0, the answer in var 0x2a).
- The builder's picture numbers run 0x1e above the picture table's indices (0x16f -> B07TRSO*, 0x19b -> B07CLR_*,
  0x19f -> B07CNCL*, 0x1a3 -> B07ACCT*, 0x1a7 -> B07DETL*, 0x1ab -> B07DEL_*).
- The equipment table, 0x101afffc + id x 0xaa (470 ids, the GDF records' +4): +0 offered, +4 tech level (also
  0x101eec48), +8 type (0 torso, 1 engine, 2 head, 3 arms, 4 legs, 5 rifles, 6 rockets, 7 grenades, 8 missiles,
  9 bazookas, 10 energy), +0xc a code ("GA11", "W031HWRF"); the name at 0x101affa2 + id x 0xaa (the record's last
  0x50 bytes before the next). Ids 0..0x4f: the 16 gears' parts by type (0x00 North torsos, 0x08 South ...);
  0x50.. weapons, with Head / Torso / Arm variants for the Mammoth (0x98..) and Nose / Shoulder for the Naga (0x158..).
- The left list (FUN_100b2dac at B07BLIST, 11 lists): FUN_100b327c puts an item in its type's list; the category
  buttons (FUN_10076573 + FUN_100f11d7) set which shows (+0x24). Its area (5, 0x19)-(0xce, 0x173), the part's
  picture (rendered from its model, 0x1013804f) in the column 5..0x65, its text (font 9, 0xfe) in 0x70..0xce, an
  item as tall as its text or 0x91 (+0x12d).
- An item is added (0x100f267f) when the id is offered and its tech <= the game's level (FUN_1011238e(0)); its text is
  the name, "\n" and its statistics for the part's default options (0x10119e00: torso 6 / 10 / 11; engine option 0x2d
  with the record's HP; head bit 1 (option 35); arms 14 / 14 / 14 / 22 / 19; legs 24 / 24 / 24 / 29 / 32). The record's
  i32s at 0x101b001e + 4k + id x 0xaa, the id's weight at 0x101ef470 + id x 0xc, the options at 0x101c3824 + n x 0x98
  (+0 load, +0xc struct, +0x10 struct factor, +0x14 strength, +0x18 stealth, +0x1c speed, +0x20 max speed, +0x24
  sensors) and 0x101f0a78 + n x 8 (weight, load capacity). The texts (labels from the runtime strings 0x10258888 ...):
  - torso (0x1016c7e7): Strength +0x2e + options; Max Speed (+0x3e + options) x 6 KPH; Speed (+0x3a + options) x 6
    KPH; Armor +0x26; Struct (0x1011a245: +0x2a + the options' adds, times each option's factor, truncated); Stealth
    +0x32 + options + 0x1011a14e(armour, armour) (-3); Load weight + options' weights + armour + option 3's +0;
    Max Load +0x22 + options' capacities;
  - engine (0x1016ffc9): Speed ("+" when positive) (sqrt(HP) + option's speed) x 6 KPH; Armor; Struct HP, over 950
    950 + (HP - 950) / 50 x 10; Stealth the option's + 1 under 400 HP, 0 under 600, -1 / -2 / -3 under 750 / 950 /
    above; HP +0x36; Load weight + option's weight + armour;
  - head (0x1016da41): Armor; Struct +0x2a; Sensors +0x36 + options' +0x24; Stealth +0x32 + options'; Load;
  - arms (0x1016e571): Strength, Armor, Struct (0x1011a81b), Load, Max Load; legs (0x1016f0a1): Speed, Armor, Struct
    (0x1011ab8a), Stealth +0x32, Load, Max Load;
  - weapons (0x10170d53): Range +0x32, Power +0x2e, Ammo +0x3a, Salvo by +0x3e (0x1011b472: 0xf..0x12 4, 0x13..0x15
    3, 0x16 / 0x17 6, 0x19 2, 0x1a 3, else 1), Load +0x1e + armour, Clip the name of +0x36 (Standard, AT, HE, APDS,
    Energy, Particle at 0x102588a8 + n x 0x10).
- The parts (0x100f4562): by FUN_10111766 (game type 1 or 4: South), the side's 8 gears, each gear's torso, engine,
  head, arms, legs; the strider as one torso. The weapons (0x100f2d9d): refilled when the design's model changes
  (+0x6e, 0x10139067: gear.vdf 1, mammoth.vdf 2, naga.vdf 3), a list per model. tools/ref/gen_shell_tables.py reads
  the lists from the calls' pushed ids.
- A design is complete (0x100a703b) with a torso, engine, head, arms and legs, or the torso 7 / 15 (Mammoth / Naga).
- The gear name selector (FUN_100a041c) is the briefing's gear list class (FUN_1009a060).
- The middle panel is a 3D view (FUN_100a6bb5 on the view class FUN_100be960, its rectangle (233, 88)-(469, 461) at
  0x101e175a; created with the place (0, 50, 350) before any design) showing the design. The shell's 3D views take
  their materials from one set loaded at the start (0x1012b93c: 164 TLT files through 0x1012d2e2, then 163 MLT files
  through 0x1012d2fe; the lists in shell_tables.h), resolved as in the sim. Traced and checked against the original
  under Wine (the Hunter unturned and turned 91 degrees: the silhouettes match, the mean brightness within 1%):
  - The camera (0x10204500, the shell's PTR_DAT_102045e0): at the origin, unturned, zoom (+0x18) 1.0, aspect (+0x44)
    480 x 4/3 / 640 = 1.0 (0x10178ff3; read 1.0 from the running original). Per drawing (0x100c4a40 / 0x100c4588) its
    viewport is the view's rectangle; the projection (0x10056736): centre ((x0 + 1 + x1) >> 1, (y0 + 1 + y1) >> 1),
    focal length ((x1 - x0 + 1) >> 1) x zoom, x aspect for y; near = half width x 0.5 + 1.
  - The design's root object (a torso or a strider; FUN_100bec8a) is placed at its item's view place (0x101b0012 /
    0x101b0016, and 0x101b001a x 1.5 (0x101aa918): the Hunter's (0, 0, 330)) by FUN_1011d7ff, and turned 180 degrees
    about y when made (0x1011b5fd / 0x1011d8bd: it faces the camera). A quaternion turn (0x1003dcd4: degrees, half
    angles) becomes the matrix of 0x1003d83f, applied as M v + t (0x1003c8d0).
  - Each item's objects carry the slot-0 model of their ODF (0x1011c3a9 names it from the ODF's +0x15, 0x1011d8bd
    loads that WTB; POF / EYE / SOL ones are flagged 0x1000, not drawn): "gd11llft", where the sim draws "gd12llft".
    A grenade's record in the GDF is POF_HWLH; the view draws the grenade item's own model there (w181hwlh ...).
  - The light, set by the shell at start (0x1012b1e8 on the camera): a point light (+0x28 = 0) at (0, 10000, -3000),
    ambient (+0x2a) 45 (0x102478c8), intensity 128 (0x1020438c); the values read back from the running original.
    Per object (0x1003b3ca, 0x1003ba56) the light's offset from the object's place, light - t, is NOT turned into the
    object's frame (only a directional light is), so it meets the object's own vertices and normals: the light turns
    with the model. i = clamp(dot(n, l - v) / |l - v| x 128, -127, 127) (0x100480ac; the normals as stored, not
    normalized; |x| approximated by 0x1003e364 as the largest component plus a quarter of the other two); L = 45 + i
    when i > 0, clamped to 128; row = L x 255 >> 10 (a textured polygon's draw word, 0x10039900), clamped to 4..31;
    the renderer gets row << 3 (software: no | 7, 0x10035fff's caller) and the luma table FOG.TBL upside down
    (0x10035fff, the name copied by 0x1012d2e2's caller).
  - The shell draws in software (DAT_102595c8 = 0, read from the running original): the textures' XEL at their own
    size (GATOETOP's XEL is 64 x 64, its TEX 32 x 32), through FOG.TBL, in the screen's palette.
  - Turning: the screen's idle actions 0xe / 0xf (message 0x20, sent on every pass of the shell's loop) check the
    keyboard state (GetKeyboardState) and turn the design 7 / -7 degrees about y while Right / Left is held
    (FUN_100f1516 -> 0x100bf3e0 -> 0x1011d6eb -> 0x1004a555), unless a drag from another control is over the view
    (+0x9e: set by event 0xf, the mouse entering while captured, FUN_10027bea; cleared by 0x11) or it is empty.
  - The details and delete-piece buttons are drawn over the view.
- Entering (FUN_1013fa9f's actions run in index order, FUN_10027bea): var 4 = 0; then, var 5 set (the briefing's
  build): string 6 = the game's gear (0x10111eb0: the record's +0x14), the selector refilled (0x1009a38b), its entry
  of that name selected (0x1009b039), the game's gear = the selector's (0x10111e94), strings 5 / 6 = its display and
  file names, and the view loads string 6 (0x100f1706 -> the view's +0x4c, 0x100a9dd4 -> 0x100c0bf3). The category and
  the list's place stay from the last visit (the screens are built once; checked in the original: arms chosen, cancel,
  build again: arms).
- The gear lists (0x1009a38b, the briefing's and the selector): every item is added after the list's current one and
  becomes it (0x10026927); only when findfirst found a "*.GDF" in the folder (any: USERGEAR.GDF and the stock names
  too) is the list rewound to its first (0x10026dd8). So a game without a gear starts on the last stock gear of the
  list when the folder has no GDF (the Hunter, as the original shows), else on its first. The folder's GDFs come in
  findfirst's order (the file system's: by name on NTFS, as the port sorts them, upper-cased).
- The buttons by the design (actions 9 - 0xb and 0x1e, run on entering and after every event, FUN_10027e23; +0x48
  disables, +0x4c enables): details (0x101089a7) with a piece selected on a GEAR.VDF design; delete piece (0x101088de)
  with a piece selected; clear (0x10108b46) with a design; the categories (0x100f2d9d): an empty design leaves only
  the torso's, whose button is clicked (the torso list), a strider (MAMMOTH / NAGA.VDF) has no engine, head, arms or
  legs. The list is refilled (its place reset) when the design's skeleton (+0x6e) or the side changes.
- Building. The models are VDF skeletons in the game folder (GEAR.VDF, MAMMOTH.VDF, NAGA.VDF: text, "#VDF", nodes
  "user" / "pre" / "optional" <kind> <name>, nested; gear.vdf: 0 hips, 1..4 the armour plates, 5 legs group, ...,
  0x10 torso, 0x11..0x18 the torso weapons and their POFs, 0x19 engine, 0x1c head, 0x1e arms group ...). A GDF's
  parts are the skeleton's nodes in order (empty records for the empty optional slots), each a 0x70-byte header (+8
  the segment id, +0xc, +0x10 the zone, +0x14 the name "GA11TRSO", +0x54 next sibling, +0x78 first child) over the
  part's ODF: ODF +0xc..+0xf8 (the detail levels' models, the pivot at ODF +0xec) at +0x7c, and the ODF's links
  (count at ODF +0x124, up to 10, 0x48 bytes from ODF +0x12c: +0 used, +0x24 the offset, +0x30 a kind, +0x34 the
  child's segment id or for kind 2 +0x38) from +0x168. The k-th link places the k-th child: the torso's eight are
  its four weapon slots, the engine, the head and the two shoulders, in the skeleton's order. Checked: 1,348 of the
  archive's 1,506 GDF parts that have an ODF match it byte for byte (the rest: the POF points and two Gila legs).
  A component is the ODFs of the equipment's code (0x101b0008 + id x 0xaa, "GA11") with the part suffixes (TR* the
  torso: TRHP, TRLP, TRRP, TRLR, TRRR, TRSO as 0x1013281e builds its picture; EN* engine; HD* head; LA* / RA* arms;
  LL* / RL* legs). After the parts: the gear block (+0 version 2, +4 the name, +0x24 the equipment count, +0x28 the
  class) and the equipment records (0x6c: +0 zone, +4 id, +8 armour, +0xc options, +0x64 3), then the editor's own
  data (+0xd0 of the template; 146 or 164 bytes, not decoded).
- Saving (0x100fb719, behind accept): a stock design (or none) gets a new file: the design's name (string var 5) cut
  to leave room for a number counted from 0, '_' filling to 8 characters, ".GDF", the first name not in the folder
  (_access); any other design is written over its own file (string var 6). The GDF writer itself is the view's
  vtable +100 (not traced).
- A part's picture (0x1013281e torsos, 0x1013319a engines, 0x10132da5 heads, 0x10133416 arms, 0x10133d1d legs,
  0x10134291 / 0x101348aa weapons) builds a node tree in the skeleton's order from the component's ODFs (empty nodes
  for the rest), hands it to 0x1011c6c3 with the VDF and the type, and renders it with the record's +0x12 / +0x16 /
  +0x1a view (not done in the port).
- INFERRED: the model from the torso (the table's VDF fields are filled at run time); the selector starting at the
  game's gear and the torso list first; the view's camera, light and true-colour textures (src/modelview.h).

## Missile lock (traced: 0x100bc861, 0x10021a8d, 0x100ba550, 0x10006672) - combat_seeker_tick

- Each tick (gear block +0x560 bits, +0x4d8 a timer): unless a kind-2 vehicle (or kind 3 of the player's) with a lock
  skill (+0x1e4) is already locked, 0x80 is cleared; with no guided weapon selected (+0x86 bit 0) or no target
  (+0xec, not 0x1000 / 0x100) 0x8000 and 0x40 clear. The band: the weapon's +0x4c < distance <= +0x50 (the vehicle's
  +0x54.. to the target's position kept at +0xd8..); the cone: the target's bearing (+0xe4) less the heading (+0x64,
  mod 360) less the torso twist (+0x70, for gears scaled by 1.5 / ((the eye's +0x18 - 1) x 1.5 + 1)) within 16
  degrees (wrapped +-180), and the elevation (+0xe8 + the gear's +0x1c, mod 360) within 16; weapons with +0x86 bits
  0x21 need only the band. In: 0x8000; the first such tick sets 0x40 and the timer 0x16a; later ticks count it down
  and at < 1 (or at once with the vehicle record's +0xc0 bit 0x20 - no GDF sets it) set 0x80 (locked). Out: 0x8000
  clears; with 0x40 the timer counts up and past 0x169 0x40 clears.
- Firing (0x100ba550): a guided weapon's slot takes the vehicle's target (+0x18 / +0x1c) only when 0x80; else 0 (the
  shot flies unguided). The AI (0x10005570 / 0x10005a87 / 0x100075e2 / 0x1000784d -> 0x10006672) sets 0x80 when
  rand % 100 < its +0x1e4, always for vehicle kinds 5 and 8.
- Tones (0x10021a8d, the cockpit view only, the gear running, state +0x4f0 == 2): AFXHDMLD (pan -5000) once 0x40 is
  set without 0x80; AFXHDMLG (pan 5000) once 0x80 is set with the selected slot ready (+8 == 1).
- Reticle (0x1005413c): a guided weapon shows HNRMISLE unlocked, the next shape (name index 61) locked.
- Arming (the shot's +0x82): 0, except for weapons with +0x86 bits 0x21 (the guided mortars, 0x61): 0x100af2a0 at the
  firing, the time the launch velocity's parabola (a = -0.0296 x gravity / 2) takes to reach the target's height,
  (-vy - sqrt(vy^2 - 4 a h)) / 2a, the other root if negative. 0x100191d7 steers (0x1001aec9) only while +0x82 < the
  shot's age (+0x30, counted up by the ticks).

## Mission end (traced: 0x1003ad72)

- When the player's group is decided (0x10131968) or its gear is lost (0x10148e1c), "Exiting Mission Area..." shows
  (0x1536 ticks) and the sim ends 0x16a ticks later (0x1014a558); the gear's loss also switches to the outside view.

## Effects (traced: 0x1001b3a1, 0x1001c40d, the loader 0x100c1152) - src/effects.c

- The pool: 0x107b97a0, 0x100 entries of 0x2c bytes (+0 the object, +4 a followed vehicle, +8..+0x10 the place,
  +0x14 ticks left, +0x18 running, +0x24 the effect type, +0x28 the owner), filled at load from gearsht1.BWD's HGXP
  records (included by every mission's scene). A record: data +4 the type (i16, 0..0x4c), a list of object ids, the
  count (i16, 25 bytes from the end) and the model's name (23 bytes from the end). gearsht1's: 0x0b wgchunk0 x15
  (debris), 0x16 wxnukeit x5, 0x17 wxmgnmzl, 0x1a wxrckmzl, 0x1e wxcanmzl, 0x4c wxnagmzl (muzzle flashes, x20 / x20 /
  x20 / x10), 0x24 wxgdeath x10, 0x2a wxexplod x20, 0x2c wxgrexpl x20, 0x2f wxflamob, 0x31 wxbldgex, 0x32 wxbgbldg,
  0x34 wxsmfire, 0x35 wxsmokec (x10 each), 0x49 wxrgntrl, 0x4a wxcnnhit, 0x4b wxlashit (x20).
- The models are one quad (two crossed for the muzzle flashes) in x / y, its base at the origin for explosions,
  centred for others (wxcnnhit +-107, wxgdeath +-520; wxexplod 0..590 high), labelled with gearxpl1's animated materials
  (LASER HIT, CANNON HIT, EXPLOSION -> EXPLSION WXEXPL01..14, GEAR DEATH -> WXGDTH01..27 ...; patch 1.2: below).
- Starting (0x1001b3a1, after the kind's remap): the effect table's +0x10 / +0x14 (every entry has +0x14 set) send it to
  the pool: among the type's entries, a free one is taken; running ones within 500 (0x100eb490) count, unless the
  table's +0x28 has bit 1 or the type is a muzzle flash 0x17..0x21 (0x4c counts): any at the low LOD quality
  (0x101244f8 == 2, F-key 0xab), three at the high (the default 1), and it doesn't start. No free entry: return, no
  sound. Its life = the table's +0. Flags +0x28 bit 0: it follows the struck vehicle when there is one, else its life
  is 0xb5 ticks. Muzzle flashes (0x17..0x21, 0x4c) with a vehicle given (0x1001b372's second argument, from the
  shot step 0x100ba550) are fixed to it (0x100bd13f); others are placed and turned (0x1009556a, the caller's
  angles: 0).
- The vehicle branches of kinds 5, 0x3b, 0x3d..0x48 lower the effect's y by 300 (0x100eb4a8); ground hits (flag
  0x400) of kinds 0x3b and 0x3d..0x48 within 2000 of the eye shake the view (0x100382fb(0xd, 0x5a, 0x32)).
- Splash (0x1001c40d, each frame before the lives count down; the table's +0x18 set, e.g. 0x24, 0x2c, 0x2f, 0x34,
  0x16): a radius (0x16: (life - left) x 4, also a flash the first frame; 0x2c: (life - left) x 16; else the model's
  bounding radius, +0x8c -> +0x50) and a power dt x the table's float +0x20. Vehicles (0x1001c75c, only with the
  option byte 0x1014a564 + 2 or for 0x2c / 0x2d): not flagged +0x560 0x100, origin within their +0x53c (a gear's
  standing height, the sole's -y, 0x100c1152) + the radius: a shot struct with power (u32) __ftol(dt x power),
  multiplier 0.1, chip 0, mode 1 on zones 2, 3, 4, 5, 6, then power >> 2 on zone 1 (0x1005c1f8). Structures
  (0x1001c96a): not destroyed (&4), bounding sphere within its radius + the radius: 0x1001cc6c with the power (its
  hit points less __ftol), destroyed -> the structure's explosion at its centre.
- Ticking (0x1001c40d): a followed vehicle's place, 300 lower, each tick; the life counts down by the ticks gone; at 0
  the object is taken off and the entry freed.
- Kinds' effects when fired (the weapon's +0x18, 0x100ba550): 0x17, 0x1a, 0x1c, 0x1e; a vehicle of kind 9 0x1e,
  one whose model name starts 'S' 0x4c (unless 0x1a).
- A vehicle's death (0x100533d2, state 4 of 0x1002b87a / 0x10060491): 0x24 at its origin, then for 0x712 ticks, each
  tick 21%: at a random offset (the normal table 0x1018e5a0, sigma 1024, halved), 60% kind 0x3e (no pool: nothing),
  else 0x10b (debris, 0x1001ce0a). A structure's (0x1001cc6c): 0x24 (its flags bit 0 or class 0xb0), 0x16 (bit 1),
  else 0x32. Other vehicles (0x1002b87a) start the burning without the 0x24.
- Debris (0x1001ce0a: n chunks, 4 for 0x100 / 0x200, 16 for 0x1000, else 8): free type-0x0b entries, life 1810
  (0x1011bbfc), each given a body (0x1001e690: 0x107f74a0, 0x80 x 0x30; gravity -0.0296 a tick squared) with
  (0x1001e76f) velocities N / 1024 x 4 (y: (N + 0x400) / 1024 x 4) and turn rates N x 0.5 / 1024, N from the normal
  table (0x10089da0: 30 rand()s summed, scaled to deviation 1024). Its flight (0x1001ef21): the bounding sphere's
  centre less half its radius against the ground; meeting the ground while still rising takes the chunk off;
  falling slower than 0.5525 a tick it rests; else it bounces (vy / -4, half the time vy halved again and vx, vz
  reversed and halved; the turn rates reversed and halved).
  Each tick the chunk turns by its rates (0x100955b7, order 0): the turn's quaternion is Z(wz) * (X(wx) * Y(wy))
  (0x10052fcb; 0x10052e3e builds an axis-angle quaternion in degrees, 0x10052ebe is the Hamilton product), multiplied
  in front of the chunk's own (q = E * q). As matrices: m = Rz Rx Ry m.
- Facing the eye (0x10037fcc, once a frame): every object of render type 0x10, 0x20 (the effects: the HGXP loader sets
  it, 0x10095980(obj, 0x20)) or 0x60 that is shown is turned about y to yaw = atan2(eye x - x, eye z - z) (0x1006f4ee,
  degrees), pitch 0 - its +z face towards the eye's position. In view mode 4 (0x1018ea7c) they get yaw 180, pitch -45.
  Muzzle flashes fixed to a vehicle get render word 0xff9f instead (not turned).
- The texture sequence: an HGXP record names a sequence slot (+10); starting an effect resets the slot to frame 0
  (0x1000204c) and sets its mode from the effect table's +0x1c (0x10001ffe): 1 loops (0x2f, 0x34, 0x35), 2 makes
  one pass (the rest). The slots step every 0xb5 / rate ticks (0x10001725; the rate from the MLT, 0x100bef4c, integer
  division); a mode 2 slot that wraps stops (mode 0, frame 0), and a stopped slot hands back no texture (0x100021e1),
  so the effect's polygon is no longer drawn. The slot is the record's: every effect of a type shares it.
- Drawing (MLT type 3 polygons, draw kind 0x3000, any object): the shading step (0x10026610) passes the word through
  unlit (unless the night-vision mask 0x1011fa40 is set, 0xb00: then lit as kind 0x1000); the render switch
  (0x1008bd80) sends it to 0x10086fb0 -> 0x1008700c: u / v each put at 0.01 or 0.99 by which side of 0.5 they are,
  +0x18 = 0xff000000 (no fog), then 0x10001513: colour the view's tint (0x10118198..a0: white, green 0x4b ff 4b in
  night vision), intensity 0x80 in software (table row 16) or 0xff in 3D, alpha byte (+0x17) 200, render mode 2.
- Patch 1.2 retextured the effects (gearxpl1.mlt / .mlu in Patch\: EXPLOSION -> FXBOOM00..04 and so on, rate 12),
  TEX only. The software renderer loads XEL only (0x1000189d / 0x100021e1, type string 0x1011fd48 "XEL") and draws
  nothing for a texture without one; the port draws the TEX there instead.

## Structures (GT chunks; the GT branch of 0x100c1152, 0x1001cc6c)

- GT, inside a BTAG: `+0` the OBJ it is, `+2` the OBJ shown once destroyed (the 'oa' model), `+0x10` hit points
  (32000 or 0: can't be destroyed; negative: |hp| and a flag), `+0x12` the target mask, `+0x18` its name. Kept in the
  object table at 0x107f2540 (0x50 bytes each: `+0` flags, `+0xc` hit points).
- A shell striking it takes the shell's power off its hit points (polygons flagged 0xb0 destroy it outright); at 0
  it's destroyed (0x1001caf9 -> 0x1009db64 swaps the model).

## Running the original for reference (findings from Windows 98 / Wine attempts)

- **Direct mission launch.** HG.EXE with any command line skips the shell and starts the sim directly (0x402710). The sim
  parses the command line at 0x100201d0:
  - Switches: `/d3d`, `/3dfx`, `/8bit`, `/16bit`, `/high` (vesa480.dll), `/low` (mcga.dll), `/grid`, `/nogrid`,
    `/griddraw`, `/onegrid`, `/lutdump`, plus single-letter options.
  - A word without a switch is the scene, e.g. `HG.EXE m01_scn1 /16bit`.
- **CD check** (0x10087460). The first CD drive must hold disc 2: the sim looks for `\video\B51MOVIE.AVI`. Without CD
  audio tracks it warns "Cannot acquire audio CD device" and carries on.
- **Files the shell writes at run time:**
  - `USERSTAR.BWD`, the player's squad (shell 0x100ff1e6).
  - `inputw.map`, copied from `SIMx\INPUTW.MAP`.
  - `hgsnd.cfg` / `hgdif.cfg` / `hgnet.cfg` / `hgprm.cfg`.
  - `DLLFILE.INI`, which names the renderer plugin for the sim to load (0x1002b336 -> 0x1002afcd).
- **USERSTAR.BWD** holds GPS gear-placement records (the missions' layout, above):
  - `+4` group (0), `+5` 1 for the player's record, `+6` role (0 = the player, 2 = a wingman)
  - `+0x16` index
  - `+0x18` flags: 0x4006
  - `+0x1a` 0x400 for the player, 0x480 for wingmen
  - `+0x1c` gear name (`USERGEAR`, the player's gear file, also written by the shell)
- **Intro movie.** The shell's intro is `B51MOVIE.AVI` (Indeo 5). The disc 1 installer supplies and self-registers
  `Ir50_32.dll`.
- **Wine:**
  - HG.EXE frees ANET2.DLL in a `while (FreeLibrary(h))` loop (0x40133e), and the same pattern runs after the sim
    returns (0x402615). Wine never returns 0 for a statically imported DLL, so the loop never ends; a local test copy
    with those two jumps NOPed gets into the sim.
  - The sim then calls a null pointer early in start-up (not yet located). Wine has no A3D (COM class {d8f1eee0-...}),
    so the sound path is the main suspect.
- **Windows 98 under QEMU:**
  - The 8-bit path hit "fatal exception 06" during the mission load. INFERRED: the VFX VESA driver's BIOS calls on the
    emulated Cirrus card.
  - The 16-bit run didn't finish before the session's time limit.

### Working Wine recipe (reference captures)

The original runs mission 1 in the 8-bit software renderer under Wine 9 (first frame 2026-11-10):
1. Game folder = the 1.2 install. Add `inputw.map` (from `SIME\`), the shell's `hgsnd/hgdif/hgnet.cfg`, and a
   `USERSTAR.BWD` with one player GPS record: `tools/ref/mkuserstar.py` writes it from mission 1's scene record.
   (`CHARINFO.HG1` isn't needed for a direct mission start.)
2. `DLLFILE.INI` = `i76draw.dll`. SimMain asks for RASTER device 0 when neither `/d3d` nor `/3dfx` is given. With
   `renddraw.dll` the plugin's function table stays empty and the sim calls a null `SetState` (0x10070c42,
   `(*0x107ee400)(7, 0)`).
3. Disc 2 mounted as the first CD drive (Wine drive type `cdrom`).
4. A copy of HG.EXE with the `FreeLibrary` loops at 0x40133e and 0x402615 NOPed (Wine never returns 0 there).
5. `HG_TEST.EXE m01_scn1 /8bit` (or `/d3d` with `DLLFILE.INI` = `dx5draw.dll`; Xvfb with GLX, Mesa).
6. Wine relocates HGSIM.DLL (to 0x1810000 or 0x1910000): take the base from `/proc/<pid>/maps`.
   `tools/ref/origstate.py <pid>` prints the player's position, heading and the eyepoint from the running game.
7. The shell (`HG_TEST.EXE` without arguments) in a fresh Wine prefix: drive D: = disc 2's files, typed "cdrom"
   (HKLM\Software\Wine\Drives), HKCU\Software\Wine\Direct3D "renderer" = "gdi", and a 640 x 480 virtual desktop
   (HKCU\Software\Wine\Explorer "Desktop" = "Default", Explorer\Desktops "Default" = "640x480"); otherwise its
   8-bit DirectDraw screen stays black under Xvfb. xdotool clicks at screen coordinates drive it (game selection
   accept, the hub's briefing door at (213, 440), build at (565, 63)); `import -window root` captures it. HGSHELL
   loads at 0x1910000: its data at 0x1910000 + (address - 0x10000000) in /proc/<pid>/mem.

### Gear editor internals (HGSHELL; src/vdf.c, src/design.c)

- VDF (FUN_1005a7b8 / 0x1005ab24 / 0x1005af22): keywords #VDF #VERSION #NAME user pre optional group AND OR ; { }
  (0x1020b358; a ';' inside a token is split off). A node: kind (user 3 / pre 4 / optional 5), class, optional label,
  then ';' or '{' children '}'. Every node but class "group" becomes a 0x438 record in preorder (a group's children
  join its parent's): +0xc = (kind == pre), +0x14 the class, +8 the class's segment id in the table at 0x10206a30
  (0x42 x 0x44: none, torso, hips, head, arm, ... , group; -1 when absent), +0x78 first child, +0x54 next sibling.
  Checked against the archive's 45 editor GDFs: every record's links, and every empty record's +8 / +0xc.
- An equipment item's node tree (the picture builders 0x1013281e torso, 0x1013319a engine, 0x10132da5 head,
  0x10133416 arms, 0x10133d1d legs, 0x10134291 weapons, 0x101348aa a left / right pair): ODF nodes (code +
  suffix, or a literal name: EYE_HDHD, SOL_LLFT ...) and empty nodes (attach points, numbered in preorder); arms and
  legs have an empty root whose children are the two instances. 0x1011c6c3 makes the item object.
- ODF in memory (0x1005c21d): 9 bytes of name, the file's first 300 bytes from +9 (+0xd the file's +4 type, +0x11 its
  +8, +0x15 the models, +0xd5 0x2c bytes, +0x12d the link count), the links, then the file's 0x6c-byte tail at +0x135
  (its first word a slot class: 0x48 / 0x49 torso weapons and 0x28 arm weapons get a specific slot type by id range
  in 0x10134291).
- The design (FUN_100bec8a drops an item: per instance a node attached to the parent instance's n-th attach point;
  0x100c0a61 rebuilds a design from the GDF's trailing data). Writing (FUN_100c04e5 -> 0x1005b80c): the VDF's
  records cleared (0x100c01c9) and filled per instance (0x100bfa60: a part record takes its ODF's type, the zone by
  type, name, models, 0x2c block and links; weapons go by their slot type with a POF child from the table of
  POF_AWLA, AWRA, EWEN, HWLH, HWRH, TWLL, TWLU, TWRL, TWRU (0x101366b1, each POF's models named after it); grenades
  0x79..0x7b put POF_HWLH in their slot); one equipment record per instance in preorder (0x100bf681: kind by type
  and slot, id, armour, options, +0x64 / +0x68 the design's two bytes); striders add six fixed records; then the
  gear block (version 2, name, count, class 1, side) and the trailing data: the two bytes, then per item 0x12 bytes
  (id << 1 | variant bit, the parent instance's preorder index or -1, a pointer, four attach-point bytes, armour,
  options).
- Dropping (0x100c515d): a dragged torso / strider (its item object has a skeleton, 0x1011d33e) clears the design and
  becomes its root; anything else is offered to every item of the design (0x1011d0d2): each of its instances takes
  the target's first attach point (0x1011c3a9 numbers them: the empty leaves in preorder; their classes are the
  parent part's link +0x30 count, +0x34 the classes) listing its root type (type 0x49 also takes 0x22 / 0x24); a pair
  tries both variants and keeps one by the points' occupants; the target whose point projects nearest the mouse wins
  (0x100c5659), and what was on the point goes (0x100bf1dd). Clicking the model selects the item whose polygon is
  nearest under the mouse (FUN_100c0cb2, 0x100c0efb); delete piece removes it (0x100a9db6 -> 0x100bf2c0); clear asks
  (0x1023d828) and on yes empties the design (0x100a8bbf -> 0x100c0c6a).
- The equipment table has 470 ids (0..469; the last the Naga's shoulder weapons), in the shell and in the sim.
