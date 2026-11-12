/*
 * vdf.h - the gear editor's skeletons (GEAR.VDF, MAMMOTH.VDF, NAGA.VDF in the game folder; HGSHELL FUN_1005a7b8).
 *
 * A text file: "#VDF", "#VERSION" n, "#NAME" name, then one node tree. The tokens are whitespace separated, a ';'
 * inside one is split off (0x1005a978); the keywords (0x1020b358) are #VDF #VERSION #NAME user pre optional group AND
 * OR ; { }. A node (0x1005ab24): its kind (user / pre / optional), its class (a word, or the keyword group), an
 * optional label, then ';' or '{' its children '}'.
 * The GDF's parts are built from it (0x1005af22): every node whose class isn't "group" becomes a 0x438-byte record in
 * preorder (a group's children join its parent's children): +0xc 1 for a "pre" node, +0x14 the class, +8 the class's
 * segment id (shell_segment, -1 if absent), +0x54 the next sibling, +0x78 the first child (record indices, 0 none).
 * The GDF's 0x88-byte header is the skeleton's: +0 the version, +4 the name (0x20), three empty 0x20 strings, +0x84
 * the record count.
 */
#ifndef HG_VDF_H
#define HG_VDF_H

#include <stddef.h>
#include <stdint.h>

#define VDF_MAX 64

typedef struct {
    uint8_t  head[0x88];          /* the GDF header */
    int      n;
    uint8_t  rec[VDF_MAX][0x438]; /* the records, the fields above set */
} vdf;

/* parse a VDF's text; 0 on success */
int vdf_parse(const char *text, size_t len, vdf *v);

#endif
