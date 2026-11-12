#include "vdf.h"

#include "shell_picture.h"
#include "shell_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const keyword[12] = {"#VDF", "#VERSION", "#NAME", "user", "pre", "optional", "group", "AND", "OR",
                                        ";", "{", "}"};

typedef struct { const char *p, *end; char tok[200]; } lexer;

/* the next token (0x1005a978): a keyword's index, or -1 for a word (in tok); -2 at the end */
static int next(lexer *x)
{
    while (x->p < x->end && (unsigned char)*x->p <= ' ') x->p++;
    if (x->p >= x->end) return -2;
    size_t n = 0;
    while (x->p < x->end && (unsigned char)*x->p > ' ' && n + 1 < sizeof x->tok) x->tok[n++] = *x->p++;
    x->tok[n] = 0;
    for (int k = 0; k < 12; k++) if (!strcmp(keyword[k], x->tok)) return k;
    /* a ';' inside the word: the word ends there, the rest is read again */
    char *s = strchr(x->tok, ';');
    if (s) { x->p -= strlen(s); *s = 0; }
    return -1;
}

static int peek(lexer *x)
{
    lexer y = *x;
    return next(&y);
}

static void wr32(uint8_t *p, int32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int segment(const char *cls)
{
    for (int i = 0; i < SHELL_SEGMENTS; i++) if (!strcmp(shell_segment[i], cls)) return i;
    return -1;
}

/* a node and its children: the records it makes (a group's: its children's), first and last of the chain; -1 error */
static int node(lexer *x, vdf *v, int *first, int *last)
{
    *first = *last = 0;
    int kind = next(x);
    if (kind < 3 || kind > 5) return -1;
    int ck = next(x);
    char cls[200];
    if (ck == -1) snprintf(cls, sizeof cls, "%s", x->tok);
    else if (ck == 6) cls[0] = 0;
    else return -1;
    if (peek(x) == -1) next(x);   /* the label (0x1005ad64) */
    int me = -1;
    if (ck == -1) {
        if (v->n >= VDF_MAX) return -1;
        me = v->n++;
        uint8_t *r = v->rec[me];
        memset(r, 0, 0x438);
        wr32(r + 0xc, kind == 4);
        memcpy(r + 0x14, cls, strlen(cls) < 0x1f ? strlen(cls) : 0x1f);
        wr32(r + 8, segment(cls));
    }
    int t = next(x), cf = 0, cl = 0;
    if (t == 10) {
        while (peek(x) != 11) {
            int f, l;
            if (node(x, v, &f, &l)) return -1;
            if (!f) continue;
            if (!cf) cf = f;
            else wr32(v->rec[cl] + 0x54, f);
            cl = l;
        }
        next(x);
    } else if (t != 9) return -1;
    if (me >= 0) {
        wr32(v->rec[me] + 0x78, cf);
        *first = *last = me;
    } else {
        *first = cf;
        *last = cl;
    }
    return 0;
}

int vdf_parse(const char *text, size_t len, vdf *v)
{
    memset(v, 0, sizeof *v);
    lexer x = {text, text + len, {0}};
    if (next(&x) != 0 || next(&x) != 1 || next(&x) != -1) return -1;
    int version = atoi(x.tok);
    if (next(&x) != 2 || next(&x) != -1) return -1;
    wr32(v->head, version);
    memcpy(v->head + 4, x.tok, strlen(x.tok) < 0x1f ? strlen(x.tok) : 0x1f);
    int f, l;
    if (node(&x, v, &f, &l) || !v->n) return -1;
    wr32(v->head + 0x84, v->n);
    return 0;
}
