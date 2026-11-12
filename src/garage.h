/*
 * garage.h - the gear editor's catalogue (HGSHELL state 7, screen B07, builder FUN_1013fa9f).
 *
 * Traced:
 *   - the equipment table (0x101afffc + id x 0xaa, 470 ids): whether the id is offered (+0), its tech level (+4, as
 *     0x101eec48), its type (+8: 0 torso, 1 engine, 2 head, 3 arms, 4 legs, 5 rifles, 6 rockets, 7 grenades,
 *     8 missiles, 9 bazookas, 10 energy) and its name (0x101affa2 + id x 0xaa);
 *   - the left list (FUN_100b2dac, B07BLIST) keeps one list per type: an item goes to its type's list
 *     (FUN_100b327c), the category buttons (FUN_10076573 + FUN_100f11d7) choose which one shows (the list's +0x24);
 *   - an item is added (FUN_100f267f) only when the id is offered and its tech is within the game's level
 *     (FUN_1011238e(0), squad_tech_level);
 *   - the parts (0x100f4562): the side's gears (North for the game types other than 1 and 4, FUN_10111766), each
 *     gear's torso, engine, head, arms and legs in turn, the strider (Mammoth / Naga) as one torso;
 *   - the weapons (0x100f2d9d): a list by the design's model (gear.vdf, mammoth.vdf, naga.vdf: 0x10139067), filled
 *     again when the model changes;
 *   - a design is complete (0x100a703b) with a torso, engine, head, arms and legs, or a strider torso (7 / 15).
 */
#ifndef HG_GARAGE_H
#define HG_GARAGE_H

#include <stddef.h>

#define GARAGE_CATS 11

enum { GARAGE_GEAR = 1, GARAGE_MAMMOTH = 2, GARAGE_NAGA = 3 };

/* an equipment id's name, type (-1 for a bad id), tech level */
const char *garage_name(int id);
int  garage_type(int id);
int  garage_tech(int id);
/* the design's model from its torso id: the torso pictures are built on gear.vdf, the Mammoth's on mammoth.vdf
 * (0x101349fe), the Naga's on naga.vdf (0x101358c4), and 0x10139067 names the model by the VDF */
int  garage_model(int torso);
/* an item's text in the left list (0x100f267f): its name, then its statistics by type for the part's default
 * options (0x10119e00), formatted as the original's (0x1016c7e7 torso, 0x1016ffc9 engine, 0x1016da41 head, 0x1016e571
 * arms, 0x1016f0a1 legs, 0x10170d53 weapons). Returns the text's length */
int  garage_item_text(int id, char *out, size_t n);
/* a design's top speed (points; x 6 KPH), load and capacity as the editor's view sums them (0x100a7346: 0x100a752f,
 * 0x100a7802, 0x100a73a0 with the penalties of 0x10119f6d): items' ids, armour, options words and the design's two
 * bytes (+0x30 / +0x31) */
void garage_design_stats(const int *ids, const int *armor, const unsigned *opts, int n, int b30, int b31,
                         int *speed, int *load, int *cap);
/* the items of category `cat` the list shows, in their order: n ids into ids (max), returns n */
/* the statistics panel's text for a design with nothing selected (FUN_100a7b0a -> FUN_1016c0cc) */
int  garage_design_text(const int *ids, const int *armor, const unsigned *opts, int n, int b30, int b31, char *out,
                        size_t len);
int  garage_list(int cat, int north, int model, int level, int *ids, int max);
/* a design's parts: the equipment ids of a GDF's records (n, at most max); -1 if it isn't a GDF */
int  garage_design(const unsigned char *gdf, size_t len, int *ids, int max);
/* 0x100a703b */
int  garage_complete(const int *ids, int n);
/* the design's torso, or -1 */
int  garage_torso(const int *ids, int n);

#endif
