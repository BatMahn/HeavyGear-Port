#include "anim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { int32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* 0x10012dc5's walk over the tracks */
int anim_parse(const uint8_t *d, size_t len, anim3di *an)
{
    memset(an, 0, sizeof *an);
    if (!d || len < 0x10) return -1;
    an->frames = rd32(d + 4);
    an->fps = rd32(d + 8);
    int n = rd32(d + 0xc);
    size_t o = 0x10;
    for (int t = 0; t < n; t++) {
        if (o + 0x24 > len) return -1;
        int np = rd32(d + o + 0x20);
        if (np < 0 || o + 0x24 + (size_t)np * 0x10 + 4 > len) return -1;
        size_t ro = o + 0x24 + (size_t)np * 0x10;
        int nr = rd32(d + ro);
        if (nr < 0 || ro + 4 + (size_t)nr * 0x14 > len) return -1;
        if (an->ntracks < ANIM_MAX_TRACKS) {
            anim_track *k = &an->track[an->ntracks++];
            char name[0x21];
            memcpy(name, d + o, 0x20);
            name[0x20] = 0;
            if (strlen(name) < 8) strcpy(name, "++++SKIP");
            memcpy(k->tag, name + 4, 4);
            k->tag[4] = 0;
            k->nrot = nr;
            k->q0[0] = k->q0[1] = k->q0[2] = 0; k->q0[3] = 1;
            k->key = nr > 0 ? malloc((size_t)nr * sizeof *k->key) : NULL;
            for (int j = 0; j < nr && k->key; j++) {
                const uint8_t *q = d + ro + 4 + (size_t)j * 0x14;
                k->key[j].frame = rd32(q);
                for (int c = 0; c < 4; c++) k->key[j].q[c] = rdf(q + 4 + c * 4);
            }
            /* 0x1001325d on frame 0: the key on it (keys run by frame from 0) */
            for (int j = 0; j < nr; j++) {
                const uint8_t *q = d + ro + 4 + (size_t)j * 0x14;
                if (rd32(q) != 0) continue;
                for (int c = 0; c < 4; c++) k->q0[c] = rdf(q + 4 + c * 4);
                k->has_q0 = 1;
                break;
            }
        }
        o = ro + 4 + (size_t)nr * 0x14;
    }
    /* the control track (0x10012dc5's end): i32 count, 0x39-byte events */
    if (o + 4 <= len) {
        int ne = rd32(d + o);
        if (ne > 0 && o + 4 + (size_t)ne * 0x39 <= len) {
            an->ev = calloc((size_t)ne, sizeof *an->ev);
            an->nev = an->ev ? ne : 0;
            for (int e = 0; e < an->nev; e++) {
                const uint8_t *q = d + o + 4 + (size_t)e * 0x39;
                an->ev[e].frame = rd32(q);
                memcpy(an->ev[e].name, q + 4, 9);
                an->ev[e].name[9] = 0;
                an->ev[e].foot = rd32(q + 0xd);
                an->ev[e].stance = rd32(q + 0x11);
            }
        }
    }
    return 0;
}

void anim_free(anim3di *an)
{
    for (int t = 0; t < an->ntracks; t++) { free(an->track[t].key); an->track[t].key = NULL; }
    free(an->ev);
    an->ev = NULL;
    an->nev = 0;
}

int anim_load(hg_archive *a, const char *name, anim3di *an)
{
    size_t len;
    uint8_t *d = hga_load(a, name, "3DI", &len, NULL);
    int r = anim_parse(d, d ? len : 0, an);
    if (r) anim_free(an);
    free(d);
    return d ? r : -1;
}

/* 0x1004e3db */
static int set_of(int type, int set)
{
    switch (type) {
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 0x22: case 0x28: case 0x21: case 0x23: case 0x24:
    case 0xd: case 0xe: case 0x25:
        return 0x100;
    case 0x1d: case 0x1e: case 0x1f: case 0x20:
        return 0x200;
    }
    return set;
}

/* +0x5ff: 0 at first (0x1004e7bf / 0x1004f94c); version 2 (0x1004e6a0, 0x1004f1a6) each weapon of kinds 8..16 by
 * 0x1004e3db; version 1 (0x1004f86d, 0x1005054d ..) each weapon record: with "infa" in the GDF's block name (the
 * infantry) type 0 gives 0x100 and 0x29 0x200, else 0x1004e3db */
int anim_weapon_set(const int *types, const int *kinds, int n, int version, int infantry)
{
    int set = 0;
    for (int i = 0; i < n; i++) {
        if (version == 2) { if (kinds[i] >= 8 && kinds[i] <= 16) set = set_of(types[i], set); }
        else if (infantry) { if (types[i] == 0) set = 0x100; if (types[i] == 0x29) set = 0x200; }
        else set = set_of(types[i], set);
    }
    return set;
}

/* 0x100147b7's names: the set (0, 0x100, 0x200) plus 0x1000 for class 9 (0x1003f7b0 ..: infantry's ids) */
const char *anim_stand_name(int id)
{
    switch (id) {
    case 0x100: return "grfstand";
    case 0x200: return "gbzstand";
    case 0x1000: return "dnwstand";
    case 0x1100: return "drfstand";
    case 0x1200: return "dbzstand";
    }
    return "gnwstand";
}

void anim_quat_matrix(const float q[4], float m[9])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}

/* 0x10052ebe */
void anim_qmul(const float a[4], const float b[4], float o[4])
{
    float t[4];
    t[3] = a[3] * b[3] - (a[2] * b[2] + a[1] * b[1] + a[0] * b[0]);
    t[0] = (a[1] * b[2] + b[3] * a[0] + a[3] * b[0]) - a[2] * b[1];
    t[1] = (a[2] * b[0] + b[3] * a[1] + a[3] * b[1]) - a[0] * b[2];
    t[2] = (a[0] * b[1] + b[3] * a[2] + a[3] * b[2]) - a[1] * b[0];
    memcpy(o, t, sizeof t);
}

/* 0x1005267f (1.0 at 0x101011a8, 1e-5 at 0x101011b0, pi / 2 at 0x101011b8) */
void anim_slerp(const float a[4], const float b[4], float t, float out[4])
{
    double c = (double)(a[2] * b[2] + a[1] * b[1] + a[0] * b[0] + a[3] * b[3]);
    float o[4];
    if (1.0 + c <= 1e-5) {
        o[0] = -a[1]; o[1] = a[0]; o[2] = -a[3]; o[3] = a[2];
        float s0 = (float)sin((double)((1.0f - t) * 1.5707963f)), s1 = (float)sin((double)(t * 1.5707963f));
        o[0] = s1 * o[0] + s0 * a[0];
        o[1] = s1 * o[1] + s0 * a[1];
        o[2] = s1 * o[2] + s0 * a[2];
    } else {
        float s0, s1;
        if (1.0 - c <= 1e-5) { s0 = 1.0f - t; s1 = t; }
        else {
            double om = acos(c), so = sin(om);
            s0 = (float)(sin((double)((1.0f - t) * (float)om)) / so);
            s1 = (float)(sin((double)(t * (float)om)) / so);
        }
        for (int i = 0; i < 4; i++) o[i] = s1 * b[i] + s0 * a[i];
    }
    memcpy(out, o, sizeof o);
}

int anim_key(const anim_track *tr, int f, int len, float q[4])
{
    int n = tr->nrot;
    if (n < 1 || !tr->key || len < 1) return -1;
    f %= len;
    if (f < 0) f += len;
    int k0 = 0, f1 = 0;
    const float *q0 = NULL, *q1 = NULL;
    for (int i = 0; i < n; i++) {
        int kf = tr->key[i].frame;
        if (f < kf) {
            if (i == 0) { k0 = tr->key[n - 1].frame; q0 = tr->key[n - 1].q; f1 = kf + len; }
            else { k0 = tr->key[i - 1].frame; q0 = tr->key[i - 1].q; f1 = kf; }
            q1 = tr->key[i].q;
            break;
        }
        if (f == kf) { memcpy(q, tr->key[i].q, 4 * sizeof *q); return 0; }
        if (i + 1 == n) { k0 = kf; q0 = tr->key[i].q; f1 = tr->key[0].frame + len; q1 = tr->key[0].q; break; }
    }
    k0 += 1;
    float t = (float)(f - k0) / (float)(f1 - k0);
    anim_slerp(q0, q1, t, q);
    return 0;
}

const anim_event *anim_event_at(const anim3di *an, int frame)
{
    if (!an || an->nev < 1 || an->frames < 1) return NULL;
    int f = frame % an->frames;
    int c;
    for (c = 0; c < an->nev; c++) {
        if (f == an->ev[c].frame) return &an->ev[c];
        if (f < an->ev[c].frame) break;
    }
    if (c >= an->nev) return &an->ev[an->nev - 1];
    return &an->ev[(c - 1 + an->nev) % an->nev];
}

static const struct { const char *name; int id, loop; } names[] = {
    {"GNWSTAND", 0, 1},
    {"GNWSTEPL", 1, 1},
    {"GNWSTEPR", 2, 1},
    {"GNWCRWCH", 3, 1},
    {"GNWFALLF", 4, 0},
    {"GNWFALLB", 5, 0},
    {"GNWGRNAD", 8, 1},
    {"GNWSKATE", 9, 1},
    {"GNWWALKF", 10, 1},
    {"GNWWALKB", 0xe, 1},
    {"GNWWFSSL", 0xb, 1},
    {"GNWWFSSR", 0xc, 1},
    {"GNWWBSSL", 0xf, 1},
    {"GNWWBSSR", 0x10, 1},
    {"GNWWFGRN", 0xd, 1},
    {"GNWWBGRN", 0x11, 1},
    {"GNWRUNIN", 0x12, 1},
    {"GNWRNSSL", 0x13, 1},
    {"GNWRNSSR", 0x14, 1},
    {"GNWRNGRN", 0x15, 1},
    {"GNWLANDN", 0x16, 1},
    {"GNWSKGRN", 0x19, 1},
    {"GRFSTAND", 0x100, 1},
    {"GRFSTEPL", 0x101, 1},
    {"GRFSTEPR", 0x102, 1},
    {"GRFCRWCH", 0x103, 1},
    {"GRFFALLF", 0x104, 0},
    {"GRFFALLB", 0x105, 0},
    {"GRFGRNAD", 0x108, 1},
    {"GRFSKATE", 0x109, 1},
    {"GRFWALKF", 0x10a, 1},
    {"GRFWALKB", 0x10e, 1},
    {"GRFWFSSL", 0x10b, 1},
    {"GRFWFSSR", 0x10c, 1},
    {"GRFWBSSL", 0x10f, 1},
    {"GRFWBSSR", 0x110, 1},
    {"GRFWFGRN", 0x10d, 1},
    {"GRFWBGRN", 0x111, 1},
    {"GRFRUNIN", 0x112, 1},
    {"GRFRNSSL", 0x113, 1},
    {"GRFRNSSR", 0x114, 1},
    {"GRFRNGRN", 0x115, 1},
    {"GRFLANDN", 0x116, 1},
    {"GRFSKGRN", 0x119, 1},
    {"GBZSTAND", 0x200, 1},
    {"GBZSTEPL", 0x201, 1},
    {"GBZSTEPR", 0x202, 1},
    {"GBZCRWCH", 0x203, 1},
    {"GBZFALLF", 0x204, 0},
    {"GBZFALLB", 0x205, 0},
    {"GBZGRNAD", 0x208, 1},
    {"GBZSKATE", 0x209, 1},
    {"GBZWALKF", 0x20a, 1},
    {"GBZWALKB", 0x20e, 1},
    {"GBZWFSSL", 0x20b, 1},
    {"GBZWFSSR", 0x20c, 1},
    {"GBZWBSSL", 0x20f, 1},
    {"GBZWBSSR", 0x210, 1},
    {"GBZWFGRN", 0x20d, 1},
    {"GBZWBGRN", 0x211, 1},
    {"GBZRUNIN", 0x212, 1},
    {"GBZRNSSL", 0x213, 1},
    {"GBZRNSSR", 0x214, 1},
    {"GBZRNGRN", 0x215, 1},
    {"GBZLANDN", 0x216, 1},
    {"GBZSKGRN", 0x219, 1},
    {"V63SPINN", 0x4017, 0},
    {"V13SPINN", 0x5017, 0},
    {"SASTANDN", 0x3000, 0},
    {"SAWALKNF", 0x300a, 0},
    {"SAWALKNB", 0x300e, 0},
    {"SARUNNIN", 0x3012, 0},
    {"SNFALEFT", 0x3004, 0},
    {"SNFARIGH", 0x3005, 0},
    {"SNSTANDN", 0x2000, 0},
    {"SNWALKNF", 0x200a, 0},
    {"SNWALKNB", 0x200e, 0},
    {"SNRUNNIN", 0x2012, 0},
    {"SNSKATEN", 0x2009, 0},
    {"SNFALEFT", 0x2004, 0},
    {"SNFARIGH", 0x2005, 0},
    {"DNWSTAND", 0x1000, 1},
    {"DNWSTEPL", 0x1001, 1},
    {"DNWSTEPR", 0x1002, 1},
    {"DNWCRWCH", 0x1003, 1},
    {"DNWFALLF", 0x1004, 0},
    {"DNWFALLB", 0x1005, 0},
    {"DNWWALKF", 0x100a, 1},
    {"DNWWALKB", 0x100e, 1},
    {"DNWWFSSL", 0x100b, 1},
    {"DNWWFSSR", 0x100c, 1},
    {"DNWWBSSL", 0x100f, 1},
    {"DNWWBSSR", 0x1010, 1},
    {"DNWRUNIN", 0x1012, 1},
    {"DNWRNSSL", 0x1013, 1},
    {"DNWRNSSR", 0x1014, 1},
    {"DNWDEADS", 0x1018, 1},
    {"DRFSTAND", 0x1100, 1},
    {"DRFSTEPL", 0x1101, 1},
    {"DRFSTEPR", 0x1102, 1},
    {"DRFCRWCH", 0x1103, 1},
    {"DRFFALLF", 0x1104, 0},
    {"DRFFALLB", 0x1105, 0},
    {"DRFWALKF", 0x110a, 1},
    {"DRFWALKB", 0x110e, 1},
    {"DRFWFSSL", 0x110b, 1},
    {"DRFWFSSR", 0x110c, 1},
    {"DRFWBSSL", 0x110f, 1},
    {"DRFWBSSR", 0x1110, 1},
    {"DRFRUNIN", 0x1112, 1},
    {"DRFRNSSL", 0x1113, 1},
    {"DRFRNSSR", 0x1114, 1},
    {"DRFDEADS", 0x1118, 1},
    {"DBZSTAND", 0x1200, 1},
    {"DBZSTEPL", 0x1201, 1},
    {"DBZSTEPR", 0x1202, 1},
    {"DBZCRWCH", 0x1203, 1},
    {"DBZFALLF", 0x1204, 0},
    {"DBZFALLB", 0x1205, 0},
    {"DBZWALKF", 0x120a, 1},
    {"DBZWALKB", 0x120e, 1},
    {"DBZWFSSL", 0x120b, 1},
    {"DBZWFSSR", 0x120c, 1},
    {"DBZWBSSL", 0x120f, 1},
    {"DBZWBSSR", 0x1210, 1},
    {"DBZRUNIN", 0x1212, 1},
    {"DBZRNSSL", 0x1213, 1},
    {"DBZRNSSR", 0x1214, 1},
    {"DBZDEADS", 0x1218, 1},
};

const char *anim_name(int id, int *loop)
{
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (names[i].id == id) { if (loop) *loop = names[i].loop; return names[i].name; }
    return NULL;
}

/* 0x1001459c: HDHD's keys = the inverse of TRHP's x TRSO's, key by key (all three with the same count) */
static void level_head(anim3di *an)
{
    anim_track *hp = NULL, *so = NULL, *hd = NULL;
    for (int t = 0; t < an->ntracks; t++) {
        if (!memcmp(an->track[t].tag, "TRHP", 4)) { if (!hp) hp = &an->track[t]; }
        else if (!memcmp(an->track[t].tag, "TRSO", 4)) { if (!so) so = &an->track[t]; }
        else if (!memcmp(an->track[t].tag, "HDHD", 4)) { if (!hd) hd = &an->track[t]; }
    }
    if (!hp || !so || !hd || hp->nrot != so->nrot || hp->nrot != hd->nrot || !hp->key || !so->key || !hd->key) return;
    for (int i = 0; i < hp->nrot; i++) {
        float r[4];
        anim_qmul(hp->key[i].q, so->key[i].q, r);
        hd->key[i].q[0] = -r[0]; hd->key[i].q[1] = -r[1]; hd->key[i].q[2] = -r[2]; hd->key[i].q[3] = r[3];
        if (hd->key[i].frame == 0) memcpy(hd->q0, hd->key[i].q, sizeof hd->q0);
    }
}

#define ANIM_CACHE 160
static struct { int id, state; anim3di an; } cache[ANIM_CACHE];
static int ncache;

const anim3di *anim_get(hg_archive *a, int id)
{
    for (int i = 0; i < ncache; i++) if (cache[i].id == id) return cache[i].state == 1 ? &cache[i].an : NULL;
    int loop = 0;
    const char *nm = anim_name(id, &loop);
    if (!nm || ncache >= ANIM_CACHE) return NULL;
    char lower[16];
    size_t k;
    for (k = 0; nm[k] && k < 15; k++) lower[k] = (char)(nm[k] >= 'A' && nm[k] <= 'Z' ? nm[k] + 32 : nm[k]);
    lower[k] = 0;
    int i = ncache++;
    cache[i].id = id;
    cache[i].state = a && !anim_load(a, lower, &cache[i].an) && cache[i].an.frames > 0 ? 1 : 0;
    if (cache[i].state == 1 && loop) level_head(&cache[i].an);
    return cache[i].state == 1 ? &cache[i].an : NULL;
}

void anim_cache_clear(void)
{
    for (int i = 0; i < ncache; i++) if (cache[i].state == 1) anim_free(&cache[i].an);
    ncache = 0;
}

void anim_player_init(anim_player *p)
{
    memset(p, 0, sizeof *p);
    p->cur = p->next = p->prev = -1;
    p->foot = -2;
}

/* 0x1003fb3f: the group of a state (the grenade variant it throws in) */
static int grenade_of(int cur)
{
    unsigned b = (unsigned)cur & 0xff;
    int g = b < 9 ? 0 : (b >= 10 && b <= 0xd) ? 1 : (b >= 0xe && b <= 0x11) ? 2 : (b >= 0x12 && b <= 0x15) ? 3 : 4;
    static const int st[5] = {8, 0xd, 0x11, 0x15, 0x19};
    return st[g];
}

void anim_pick(anim_player *p, const anim_in *in, unsigned *flags)
{
    unsigned f = *flags;
    int free_ = !(f & ANIM_BUSY) && !in->stun;
    int gren = (f & ANIM_GRENADE) && free_;
    int sl = in->straffe_l && free_, sr = in->straffe_r && free_;
    int set = in->set;
    int moving = in->thr > 0.01611328125f;
    if (moving) {
        float t = in->thr - 0.015625f;
        if (t < 1.0000000116860974e-07f) t = 0;
        if (gren) p->next = set | grenade_of(p->cur);
        else if (sl) { p->rate = 3; p->next = set | 0xb; }
        else if (sr) { p->rate = 3; p->next = set | 0xc; }
        else p->next = set | 0xa;
        int run = 0;
        if (t < 0.00390625f) p->rate = 1;
        else if (t < 0.0078125f) p->rate = 2;
        else if (t < 0.01171875f) p->rate = 3;
        else if (t < 0.012968749739229679f) { p->rate = 1; run = 1; }
        else if (t < 0.014218750409781933f) { p->rate = 2; run = 1; }
        else if (t < 0.015625f) { p->rate = 2; run = 1; }
        if (run) {
            if (gren) p->next = set | grenade_of(p->cur);
            else p->next = set | (sl ? 0x13 : sr ? 0x14 : 0x12);
        }
        if (p->cur == 0xe && p->rate > 2) p->rate = 2;
        if (in->reverse) {
            if (gren) p->next = set | grenade_of(p->cur);
            else p->next = set | (sl ? 0xf : sr ? 0x10 : 0xe);
        }
        if ((f & ANIM_SKATE) && free_ && !(f & ANIM_GRENADE)) p->next = set | 9;
        if (in->cls == 4 || in->cls == 10) p->cur = p->next;
    } else {
        p->rate = 3;
        if (gren) p->next = set | grenade_of(p->cur);
        else if (sl) p->next = set | 1;
        else if (sr) p->next = set | 2;
        else if ((f & ANIM_CROUCH) && free_) { p->rate = 1; p->next = set | 3; }
        else if (f & ANIM_FALLF) { p->rate = 2; p->next = set | 4; }
        else if (f & ANIM_FALLB) { p->rate = 2; p->next = set | 5; }
        else { p->rate = 0; p->next = set; }   /* 0x1003fab8 */
    }
    if (in->cls == 5) { p->rate = 2; p->next = 0x17; }
    /* 0x1003f7b0 */
    if (in->cls == 1) {
        if (in->prefix) {
            p->next |= in->prefix;
            int s = p->next & 0xff;
            if (s == 4 || s == 5) p->next = (p->next & ~0xff) | ((f & ANIM_LSOLE) ? 4 : 5);
        }
    } else if (in->cls == 5) p->next |= in->prefix;
    else if (in->cls == 9) {
        p->next |= 0x1000;
        if (in->dying) p->next = (p->next & ~0xff) | 0x18;
    }
}

void anim_advance(anim_player *p, hg_archive *a, int ticks, unsigned *flags, int legs_gone)
{
    if (p->next != p->cur && p->next >= 0) {
        const anim3di *nw = anim_get(a, p->next);
        if (!p->an) {
            if (!nw) p->next = p->cur;
            else { p->an = nw; p->cur = p->next; p->frame = 0; }
        } else if (nw) {
            p->old = p->an;
            p->blending = 1;
            p->old_frame = p->frame;
            p->start = 0;
            p->frame = 0;
            p->an = nw;
            p->prev = p->cur;
            p->cur = p->next;
        }
    }
    if (!p->an) return;
    const anim3di *an = p->an;
    /* the footstep / stance event on the frame posed (0x1001386b's end) */
    const anim_event *ev = anim_event_at(an, p->blending ? p->start : p->frame);
    if (ev) {
        if ((ev->foot == 0 || ev->foot == 1 || ev->foot == 2) && p->foot != ev->foot) {
            p->foot = ev->foot;
            if (ev->foot == 0 || ev->foot == 1) p->footstep = 1;
        }
        if (ev->stance == 1 && (*flags & 0xffff)) *flags |= ANIM_STANCE;
    }
    if (p->blending) {
        int step = (int)((float)(ticks * 30) / 181.0f + 0.5f);
        if (step < 1 && ticks) step = 1;
        p->frame += step;
        if (p->frame >= 10) { p->blending = 0; p->frame = p->start; }
        return;
    }
    float r = p->rate == 1 ? (float)an->fps * 0.75f * (float)ticks : p->rate == 3 ? (float)an->fps * 1.5f * (float)ticks
            : (float)(an->fps * ticks);
    int step = (int)(r / 181.0f + 0.5f);
    if (step < 1 && ticks) step = 1;
    int len = an->frames, s = p->cur & 0xff, half = len / 2;
    if (s == 1 || s == 2 || s == 3) {
        if (half < p->frame + step) { p->held = 1; p->frame = half; }
        else p->frame = (p->frame + step) % len;
    } else if (s == 4 || s == 5) {
        if (!(*flags & ANIM_RISE)) {
            if (p->frame + step < half) p->frame = (p->frame + step) % len;
            else { p->held = 1; p->frame = half; *flags |= ANIM_DOWN; }
        } else if (!legs_gone) {
            p->frame = (p->frame + step) % len;
            if (p->frame <= half) p->frame = len;
            if (p->frame == len) *flags &= ~(unsigned)(ANIM_FALLF | ANIM_FALLB | ANIM_RISE);
        } else {
            *flags &= ~(unsigned)ANIM_RISE;
            p->knock = 1;
        }
    } else if (s == 0xd || s == 0x11 || s == 8 || s == 0x19 || s == 0x15) {
        if (p->frame + step < len) p->frame = (p->frame + step) % len;
        else { *flags &= ~(unsigned)ANIM_GRENADE; p->next = p->prev; }
    } else if (s == 0x18) {
        if (p->frame + step < len) p->frame = (p->frame + step) % len;
        else p->dead_done = 1;
    } else p->frame = (p->frame + step) % len;
}

int anim_track_q(const anim_player *p, int t, float q[4])
{
    static const float id[4] = {0, 0, 0, 1};
    if (!p->an || t < 0 || t >= p->an->ntracks) return -1;
    const anim_track *tr = &p->an->track[t];
    int f = p->blending ? p->start : p->frame;
    if (p->blending && p->old && t == p->old->ntracks) return -1;
    if (tr->nrot == 0 && f != 0) return -1;
    if (anim_key(tr, f, p->an->frames, q)) memcpy(q, id, sizeof id);
    if (p->blending && p->old) {
        float qo[4];
        const anim_track *to = t < p->old->ntracks ? &p->old->track[t] : NULL;
        if (!to || anim_key(to, p->old_frame, p->old->frames, qo)) memcpy(qo, id, sizeof id);
        anim_slerp(qo, q, (float)p->frame / 10.0f, q);
    }
    return 0;
}
