/*
 * hgrand.h - the sim's random numbers (traced: HGSIM 0x10089da0, 0x10089e69, 0x10089ea9, 0x10089ee5).
 *
 * InitRandom (0x10089da0, from SimMain with seed 0) fills two tables of 127 from the C library's srand / rand
 * (0x100de850 / 0x100de860, MSVC: x = x * 0x343fd + 0x269ec3, (x >> 16) & 0x7fff): the uniform one (0x1018e7a0) with
 * 127 rand()s, then the normal one (0x1018e5a0), each entry (int)((sum of 30 rand()s x 2 sqrt(90) / 983010 - sqrt(90))
 * x 1024). Every sim caller shares the cursors: 0x10089e69 (uniform, modulo n, cursor 0x1014b674), 0x10089ea9 (normal,
 * cursor 0x1014b678), 0x10089ee5 (uniform, its own cursor 0x1014b67c).
 */
#ifndef HG_HGRAND_H
#define HG_HGRAND_H

void hg_rand_reset(void);   /* a new sim: the tables made, the cursors at 0 (the DLL's fresh globals) */
int  hg_rand(int n);        /* 0x10089e69: the next uniform value modulo n (0 when n <= 0) */
int  hg_nrand(void);        /* 0x10089ea9: the next normal value (mean 0, deviation 1024) */
int  hg_rand2(int n);       /* 0x10089ee5: the uniform table through the second cursor */

#endif
