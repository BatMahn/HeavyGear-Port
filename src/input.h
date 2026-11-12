/*
 * input.h - the sim's analog controls ("sinks") driven by keys (traced: HGSIM 0x10047850, 0x100489a8, 0x10048c58).
 *
 * The control table (0x10114028, 0x20 bytes a record: name, the variable, type 0 axis / 1 momentary / 2 toggle,
 * +0xc the axis's rest value, +0x10 min, +0x14 max, +0x18 the rate's shift, +0x1c the value's shift) gives every
 * axis (zoom_factor, pilot_tilt, track_distance_delta ...) a sink (0x10047850, 0x32 bytes): range = max - min, min
 * << 16, its rest position ((rest << 16) - (min << 16)) x 2 / range - 0x10000 (a position runs -0x10000 .. 0x10000),
 * and its <name>_plus / _minus / _reset / _set switches. An axis whose name ends in "_delta" steps its position
 * itself, the others a speed (+0x16) added to the position.
 *
 * Each frame (0x10048c58 -> 0x100489a8, the frame's ticks t): _reset puts the position at its rest and the speed at
 * 0; _plus held: speed = max(speed, 0) + (t x 3 << rate), at most 0x8000, position += speed, at most 0x10000;
 * _minus the same downwards (at least -0x8000 / -0x10000); neither: speed 0. Then the variable = (float)((((pos +
 * 0x10000) x range) / 2 + (min << 16)) >> shift) / 65536.
 */
#ifndef HG_INPUT_H
#define HG_INPUT_H

typedef struct {
    int range, min16, rest, rate, shift, delta;
    int pos, speed;
} hg_axis;

/* from the control table's record: rest value, min, max, rate shift (+0x18), value shift (+0x1c), "_delta" name */
void  hg_axis_init(hg_axis *a, int rest, int min, int max, int rate, int shift, int delta);
/* one frame of t ticks with the switches held; returns the variable's value */
float hg_axis_step(hg_axis *a, int t, int plus, int minus, int reset);
float hg_axis_value(const hg_axis *a);

/* the records the port drives (0x10114c88 zoom_factor, 0x101148e8 pilot_tilt, 0x10114968 pilot_pan, 0x10114a68
 * eyepoint_tilt, 0x10114ae8 eyepoint_pan_delta, 0x10114bc8 track_distance_delta, 0x10114c28 track_height_delta) */
enum { AX_ZOOM, AX_PILOT_TILT, AX_PILOT_PAN, AX_EYE_TILT, AX_EYE_PAN, AX_TRACK_DIST, AX_TRACK_HEIGHT, AX_COUNT };
void  hg_axes_init(hg_axis ax[AX_COUNT]);

#endif
