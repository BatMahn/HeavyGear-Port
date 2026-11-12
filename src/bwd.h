/*
 * bwd.h - mission / world description records (BWD). Same container as MW2's: "BWD\0", u32 size, u32, then chunks
 * { char tag[4], u32 size (header included), payload }. HG's INCL names its target (u16 0xffff, char name[10]).
 */
#ifndef HG_BWD_H
#define HG_BWD_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char           tag[5];
    const uint8_t *data;
    size_t         size;
} bwd_chunk;

/* Walk the chunks; returns the count (up to max stored) or -1 if malformed. */
int bwd_chunks(const uint8_t *rec, size_t len, bwd_chunk *out, int max);

#endif
