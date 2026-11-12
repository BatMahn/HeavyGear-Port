/* ai_rules.h - the AI's mode rule sets, dumped from HGSIM.DLL by tools/ref/gen_ai_rules.py (do not edit).
 * The 12 sets at 0x101072d8 (the table 0x10834e80 + 4 x k, 0x1000ed62): each a count, then (mode, rules) pairs
 * and the rules, 12 bytes each: i16 condition (0x10107dd0), i16 a1, i16 a2, u8 action (0x10107df0), i16 the mode
 * set, i16 its argument. The selectors (0x1000fc1a): per class, 16 entries of (the leader's set, the others').
 */
#ifndef HG_AI_RULES_H
#define HG_AI_RULES_H

typedef struct ai_rule_s { short cond, a1, a2; unsigned char act; short mode, arg; } ai_rule;
typedef struct { short mode, n; const ai_rule *r; } ai_mode_rules;
typedef struct { int n; const ai_mode_rules *m; } ai_rule_set;

static const ai_rule ai_set1_m0[1] = {{2, 0x40, 500, 0, 0, 0x0}};   /* 0x10106fa0 */
static const ai_rule ai_set1_m1[1] = {{7, 0x4201, -2, 4, 5, 0x2201}};   /* 0x10106fac */
static const ai_rule ai_set1_m2[1] = {{3, 0x40, 0, 0, 5, 0x2201}};   /* 0x10106fb8 */
static const ai_rule ai_set1_m3[1] = {{7, 0x4201, -2, 4, 0, 0x0}};   /* 0x10106fc4 */
static const ai_rule ai_set1_m4[3] = {{3, 0x40, 350, 0, 3, 0x25}, {6, 0x40, 0, 3, 5, 0x2201}, {2, 0x2101, -4, 3, 8, 0x25}};   /* 0x10106fd0 */
static const ai_rule ai_set1_m5[3] = {{6, 0x40, 0, 3, 5, 0x2201}, {2, 0x2101, -4, 3, 8, 0x25}, {2, 0x40, 350, 0, 2, 0x25}};   /* 0x10106ff4 */
static const ai_rule ai_set1_m6[2] = {{7, 0x4201, -2, 2, 2, 0x25}, {3, 0x2a, 0, 0, 8, 0x40}};   /* 0x10107018 */
static const ai_rule ai_set1_m7[1] = {{7, 0x4201, -1, 4, 10, 0x0}};   /* 0x10107030 */
static const ai_rule ai_set1_m8[1] = {{5, 0x0, 0, 0, 11, 0x0}};   /* 0x1010703c */
static const ai_rule ai_set1_m9[1] = {{3, 0x2a, 0, 0, 3, 0x40}};   /* 0x10107048 */
static const ai_rule ai_set1_m10[1] = {{3, 0x40, 0, 0, 0, 0x4201}};   /* 0x10107054 */
static const ai_rule ai_set1_m11[1] = {{3, 0x2a, 10, 0, 0, 0x4201}};   /* 0x10107060 */
static const ai_mode_rules ai_set1[12] = {{4, 1, ai_set1_m0}, {5, 1, ai_set1_m1}, {8, 1, ai_set1_m2}, {0, 1, ai_set1_m3}, {2, 3, ai_set1_m4}, {3, 3, ai_set1_m5}, {7, 2, ai_set1_m6}, {10, 1, ai_set1_m7}, {11, 1, ai_set1_m8}, {13, 1, ai_set1_m9}, {14, 1, ai_set1_m10}, {15, 1, ai_set1_m11}};
static const ai_rule ai_set2_m0[1] = {{5, 0x0, 0, 0, -1, 0x0}};   /* 0x10107080 */
static const ai_mode_rules ai_set2[1] = {{-1, 1, ai_set2_m0}};
static const ai_rule ai_set3_m0[1] = {{5, 0x0, 0, 0, -1, 0x0}};   /* 0x101070a0 */
static const ai_mode_rules ai_set3[1] = {{-1, 1, ai_set3_m0}};
static const ai_rule ai_set4_m0[1] = {{2, 0x40, 0, 0, 0, 0x0}};   /* 0x101070c0 */
static const ai_mode_rules ai_set4[1] = {{4, 1, ai_set4_m0}};
static const ai_rule ai_set5_m0[2] = {{2, 0x2201, 300, 0, 5, 0x25}, {7, 0x4201, -2, 4, 0, 0x0}};   /* 0x101070f0 */
static const ai_rule ai_set5_m1[1] = {{7, 0x4201, -2, 4, 0, 0x0}};   /* 0x10107108 */
static const ai_rule ai_set5_m2[1] = {{3, 0x40, 250, 0, 3, 0x25}};   /* 0x10107114 */
static const ai_rule ai_set5_m3[1] = {{6, 0x40, 0, 3, 5, 0x2201}};   /* 0x10107120 */
static const ai_mode_rules ai_set5[4] = {{0, 2, ai_set5_m0}, {5, 1, ai_set5_m1}, {2, 1, ai_set5_m2}, {3, 1, ai_set5_m3}};
static const ai_rule ai_set6_m0[1] = {{5, 0x0, 0, 0, -1, 0x0}};   /* 0x10107140 */
static const ai_mode_rules ai_set6[1] = {{-1, 1, ai_set6_m0}};
static const ai_rule ai_set7_m0[2] = {{7, 0x4201, -2, 2, 2, 0x25}, {3, 0x2a, 0, 0, 8, 0x40}};   /* 0x10107160 */
static const ai_mode_rules ai_set7[1] = {{7, 2, ai_set7_m0}};
static const ai_rule ai_set8_m0[1] = {{7, 0x4201, -2, 4, 0, 0x0}};   /* 0x10107188 */
static const ai_mode_rules ai_set8[1] = {{8, 1, ai_set8_m0}};
static const ai_rule ai_set9_m0[1] = {{2, 0x40, 0, 0, 0, 0x0}};   /* 0x101071a8 */
static const ai_mode_rules ai_set9[1] = {{4, 1, ai_set9_m0}};
static const ai_rule ai_set10_m0[2] = {{3, 0x40, 500, 0, 8, 0x40}, {3, 0x40, 0, 0, 0, 0x0}};   /* 0x101071d8 */
static const ai_rule ai_set10_m1[1] = {{3, 0x40, 0, 0, 0, 0x0}};   /* 0x101071f0 */
static const ai_rule ai_set10_m2[2] = {{3, 0x40, 0, 0, 0, 0x0}, {7, 0x4201, -2, 4, 0, 0x0}};   /* 0x101071fc */
static const ai_rule ai_set10_m3[2] = {{6, 0x40, 0, 3, 8, 0x2100}, {2, 0x40, 250, 0, 2, 0x40}};   /* 0x10107214 */
static const ai_mode_rules ai_set10[4] = {{6, 2, ai_set10_m0}, {0, 1, ai_set10_m1}, {8, 2, ai_set10_m2}, {3, 2, ai_set10_m3}};
static const ai_rule ai_set11_m0[1] = {{7, 0x4201, -2, 4, 0, 0x0}};   /* 0x10107188 */
static const ai_mode_rules ai_set11[1] = {{8, 1, ai_set11_m0}};
static const ai_rule ai_set12_m0[2] = {{3, 0x40, 500, 0, 8, 0x40}, {3, 0x40, 0, 0, 0, 0x0}};   /* 0x10107250 */
static const ai_rule ai_set12_m1[1] = {{3, 0x40, 0, 0, 0, 0x0}};   /* 0x10107268 */
static const ai_rule ai_set12_m2[2] = {{3, 0x40, 1, 0, 0, 0x0}, {7, 0x4201, -2, 4, 0, 0x0}};   /* 0x10107274 */
static const ai_rule ai_set12_m3[2] = {{6, 0x40, 0, 3, 8, 0x2100}, {2, 0x40, 250, 0, 2, 0x40}};   /* 0x1010728c */
static const ai_mode_rules ai_set12[4] = {{6, 2, ai_set12_m0}, {0, 1, ai_set12_m1}, {8, 2, ai_set12_m2}, {3, 2, ai_set12_m3}};
/* index 0 none; k: set k (0x10834e84 + (k - 1) x 4) */
static const ai_rule_set ai_sets[13] = {{0, 0}, {12, ai_set1}, {1, ai_set2}, {1, ai_set3}, {1, ai_set4}, {4, ai_set5}, {1, ai_set6}, {1, ai_set7}, {1, ai_set8}, {1, ai_set9}, {4, ai_set10}, {1, ai_set11}, {4, ai_set12}};
/* the selectors by class (0x1000fc1a): 1 / 9 0x10107c10, 2 0x10107cd0, 3 0x10107c50, 4 / 10 0x10107d10, 5 / 8 0x10107d50, 6 0x10107c90 */
static const short ai_sel_c10[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};
static const short ai_sel_c50[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};
static const short ai_sel_c90[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};
static const short ai_sel_cd0[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};
static const short ai_sel_d10[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};
static const short ai_sel_d50[16][2] = {{1, 1}, {8, 3}, {8, 3}, {7, 2}, {10, 5}, {1, 1}, {10, 5}, {1, 1}, {1, 1}, {12, 5}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {9, 4}, {11, 6}};

#endif
