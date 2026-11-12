/*
 * unpack.c - deflate (zlib) and PKWARE DCL implode decoders. See unpack.h.
 *
 * Both are canonical-Huffman bit readers; written for this port from RFC 1951 and the published description of the
 * DCL implode format.
 */
#include "unpack.h"

#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t         inlen, pos;
    uint32_t       bitbuf;
    int            bitcnt;
    int            err;
} bitreader;

/* LSB-first bits (both formats) */
static int getbits(bitreader *b, int n)
{
    while (b->bitcnt < n) {
        if (b->pos >= b->inlen) { b->err = 1; return 0; }
        b->bitbuf |= (uint32_t)b->in[b->pos++] << b->bitcnt;
        b->bitcnt += 8;
    }
    int v = (int)(b->bitbuf & ((1u << n) - 1));
    b->bitbuf >>= n;
    b->bitcnt -= n;
    return v;
}

/* ---- canonical Huffman tables ---- */

#define MAXBITS 15

typedef struct {
    short count[MAXBITS + 1];  /* codes of each length */
    short symbol[320];         /* symbols ordered by code */
} huff;

static int huff_build(huff *h, const unsigned char *len, int n)
{
    short offs[MAXBITS + 1];
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++) h->count[len[s]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= MAXBITS; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0) return -1;   /* over-subscribed */
    }
    offs[1] = 0;
    for (int l = 1; l < MAXBITS; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int s = 0; s < n; s++)
        if (len[s]) h->symbol[offs[len[s]]++] = (short)s;
    return left;   /* > 0: incomplete (allowed for single-code distance sets) */
}

/* deflate: codes are sent MSB-first within the LSB-first stream */
static int huff_decode(bitreader *b, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= MAXBITS; l++) {
        code |= getbits(b, 1);
        int c = h->count[l];
        if (code - c < first) return h->symbol[index + (code - first)];
        index += c;
        first += c;
        first <<= 1;
        code <<= 1;
        if (b->err) return -1;
    }
    return -1;
}

/* ---- inflate (RFC 1951) ---- */

static const short len_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short len_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                    4097,6145,8193,12289,16385,24577};
static const short dist_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inflate_codes(bitreader *b, const huff *lit, const huff *dist, uint8_t *out, size_t cap, size_t *op)
{
    for (;;) {
        int sym = huff_decode(b, lit);
        if (sym < 0 || b->err) return -1;
        if (sym < 256) {
            if (*op >= cap) return -1;
            out[(*op)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = len_base[sym] + getbits(b, len_extra[sym]);
            int ds = huff_decode(b, dist);
            if (ds < 0 || ds >= 30) return -1;
            size_t d = (size_t)dist_base[ds] + (size_t)getbits(b, dist_extra[ds]);
            if (b->err || d > *op || *op + (size_t)len > cap) return -1;
            for (int i = 0; i < len; i++, (*op)++) out[*op] = out[*op - d];
        }
    }
}

long unpack_zlib(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap)
{
    if (inlen < 2 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    bitreader b = { in, inlen, 2, 0, 0, 0 };
    size_t op = 0;
    int last;
    do {
        last = getbits(&b, 1);
        int type = getbits(&b, 2);
        if (b.err) return -1;
        if (type == 0) {                       /* stored */
            b.bitbuf = 0; b.bitcnt = 0;
            if (b.pos + 4 > inlen) return -1;
            unsigned len = in[b.pos] | (in[b.pos + 1] << 8);
            unsigned nlen = in[b.pos + 2] | (in[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xffff) != nlen || b.pos + len > inlen || op + len > outcap) return -1;
            memcpy(out + op, in + b.pos, len);
            b.pos += len; op += len;
        } else if (type == 1) {                /* fixed codes */
            static huff flit, fdist;
            static int ready = 0;
            if (!ready) {
                unsigned char l[288];
                int s = 0;
                for (; s < 144; s++) l[s] = 8;
                for (; s < 256; s++) l[s] = 9;
                for (; s < 280; s++) l[s] = 7;
                for (; s < 288; s++) l[s] = 8;
                huff_build(&flit, l, 288);
                for (s = 0; s < 30; s++) l[s] = 5;
                huff_build(&fdist, l, 30);
                ready = 1;
            }
            if (inflate_codes(&b, &flit, &fdist, out, outcap, &op)) return -1;
        } else if (type == 2) {                /* dynamic codes */
            static const unsigned char order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            unsigned char lens[320];
            huff lc, lit, dist;
            int nlen = getbits(&b, 5) + 257, ndist = getbits(&b, 5) + 1, ncode = getbits(&b, 4) + 4;
            if (b.err || nlen > 286 || ndist > 30) return -1;
            memset(lens, 0, 19);
            for (int i = 0; i < ncode; i++) lens[order[i]] = (unsigned char)getbits(&b, 3);
            if (huff_build(&lc, lens, 19) != 0) return -1;
            int i = 0;
            while (i < nlen + ndist) {
                int sym = huff_decode(&b, &lc);
                if (sym < 0) return -1;
                if (sym < 16) { lens[i++] = (unsigned char)sym; continue; }
                int rep, v = 0;
                if (sym == 16) { if (!i) return -1; v = lens[i - 1]; rep = 3 + getbits(&b, 2); }
                else if (sym == 17) rep = 3 + getbits(&b, 3);
                else rep = 11 + getbits(&b, 7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) lens[i++] = (unsigned char)v;
            }
            if (b.err || lens[256] == 0) return -1;
            int e = huff_build(&lit, lens, nlen);
            if (e < 0 || (e > 0 && nlen - lit.count[0] != 1)) return -1;
            e = huff_build(&dist, lens + nlen, ndist);
            if (e < 0 || (e > 0 && ndist - dist.count[0] != 1)) return -1;
            if (inflate_codes(&b, &lit, &dist, out, outcap, &op)) return -1;
        } else {
            return -1;
        }
    } while (!last);
    return (long)op;
}

/* ---- PKWARE DCL explode ---- */

/* The format's fixed code tables, given as run-length pairs: (count-1) << 4 | bit length. */
static const unsigned char ex_litlen[] = {
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11,
    7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12, 8, 12, 5, 38, 5,
    38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61,
    12, 45, 44, 173};
static const unsigned char ex_lenlen[] = {2, 35, 36, 53, 38, 23};
static const unsigned char ex_distlen[] = {2, 20, 53, 230, 247, 151, 248};
static const short ex_base[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
static const char ex_extra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

static void ex_build(huff *h, const unsigned char *rep, int n)
{
    unsigned char len[256];
    int s = 0;
    for (int i = 0; i < n; i++) {
        int k = (rep[i] >> 4) + 1, l = rep[i] & 15;
        while (k--) len[s++] = (unsigned char)l;
    }
    huff_build(h, len, s);
}

/* DCL codes are stored bit-inverted, so read each bit inverted and walk the canonical table as deflate does. */
static int ex_decode(bitreader *b, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 13; l++) {
        code |= getbits(b, 1) ^ 1;
        int c = h->count[l];
        if (code - c < first) return h->symbol[index + (code - first)];
        index += c;
        first += c;
        first <<= 1;
        code <<= 1;
        if (b->err) return -1;
    }
    return -1;
}

long unpack_explode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap)
{
    static huff lit, len, dist;
    static int ready = 0;
    if (!ready) {
        ex_build(&lit, ex_litlen, (int)sizeof ex_litlen);
        ex_build(&len, ex_lenlen, (int)sizeof ex_lenlen);
        ex_build(&dist, ex_distlen, (int)sizeof ex_distlen);
        ready = 1;
    }
    bitreader b = { in, inlen, 0, 0, 0, 0 };
    int coded = getbits(&b, 8);   /* 0 = literals stored as raw bytes, 1 = Huffman-coded */
    int dict = getbits(&b, 8);    /* dictionary size: 4, 5 or 6 (1K, 2K, 4K) */
    if (b.err || coded > 1 || dict < 4 || dict > 6) return -1;
    size_t op = 0;
    for (;;) {
        if (getbits(&b, 1)) {
            int s = ex_decode(&b, &len);
            if (s < 0) return -1;
            int n = ex_base[s] + getbits(&b, ex_extra[s]);
            if (n == 519) break;                     /* end of stream */
            int sh = n == 2 ? 2 : dict;
            int ds = ex_decode(&b, &dist);
            if (ds < 0) return -1;
            size_t d = ((size_t)ds << sh) + (size_t)getbits(&b, sh) + 1;
            if (b.err || d > op || op + (size_t)n > outcap) return -1;
            for (int i = 0; i < n; i++, op++) out[op] = out[op - d];
        } else {
            int c = coded ? ex_decode(&b, &lit) : getbits(&b, 8);
            if (c < 0 || b.err || op >= outcap) return -1;
            out[op++] = (uint8_t)c;
        }
        if (b.err) return -1;
    }
    return (long)op;
}
