#include "mission.h"

#include "bwd.h"
#include "bytes.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHUNKS 8192
#define MAX_DEPTH  8

/* 0x10051fb0 with order 0: yaw y, pitch x, roll z (degrees) -> R = Ry Rx Rz, rows, applied as R v */
void mission_rot(float ax, float ay, float az, float r[9])
{
    const float d = 3.14159265358979f / 180.0f;
    float cx = cosf(ax * d), sx = sinf(ax * d), cy = cosf(ay * d), sy = sinf(ay * d), cz = cosf(az * d), sz = sinf(az * d);
    r[0] = cy * cz + sy * sx * sz; r[1] = -cy * sz + sy * sx * cz; r[2] = sy * cx;
    r[3] = cx * sz;                r[4] = cx * cz;                 r[5] = -sx;
    r[6] = -sy * cz + cy * sx * sz; r[7] = sy * sz + cy * sx * cz; r[8] = cy * cx;
}

static void m3mul(const float *a, const float *b, float *o)
{
    float t[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) t[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    memcpy(o, t, sizeof t);
}

typedef struct { float r[9], t[3]; } blk_xf;

/* idxmap: OBJ index -> obj slot within the current record (-1 none). blk: the enclosing block's matrix, or NULL. */
static void add_obj(mission *m, const uint8_t *c, int ground, const blk_xf *blk, int *idxmap)
{
    m->obj = realloc(m->obj, (size_t)(m->nobj + 1) * sizeof *m->obj);
    mission_obj *o = &m->obj[m->nobj++];
    memset(o, 0, sizeof *o);
    int k;
    for (k = 0; k < 10 && c[0x32 + k]; k++) o->model[k] = (char)tolower(c[0x32 + k]);
    o->model[k] = 0;
    for (int i = 0; i < 3; i++) o->rot[i] = (float)rd_i32(c + 0x12 + i * 4) / 65536.0f;
    for (int i = 0; i < 3; i++) {
        float sv = (float)rd_i32(c + 6 + i * 4);
        o->scale[i] = fabsf(sv) < 1e-7f ? 1.0f : sv;
    }
    for (int i = 0; i < 3; i++) o->lpos[i] = (float)rd_i32(c + 0x1e + i * 4);
    mission_rot(o->rot[0], o->rot[1], o->rot[2], o->lm);
    o->oy = o->lpos[1];
    int index = rd_i16(c), parent = rd_i16(c + 2);
    o->kind = rd_i16(c + 4);
    o->flags = rd_u16(c + 0x2c);
    o->parent = -1;
    if (parent >= 0 && parent < 4096 && idxmap[parent] >= 0) {
        /* under the parent's node: its matrix times the child's own */
        const mission_obj *p = &m->obj[idxmap[parent]];
        o->parent = idxmap[parent];
        o->child = 1;
        m3mul(p->m, o->lm, o->m);
        o->x = p->x + p->m[0] * o->lpos[0] + p->m[1] * o->lpos[1] + p->m[2] * o->lpos[2];
        o->y = p->y + p->m[3] * o->lpos[0] + p->m[4] * o->lpos[1] + p->m[5] * o->lpos[2];
        o->z = p->z + p->m[6] * o->lpos[0] + p->m[7] * o->lpos[1] + p->m[8] * o->lpos[2];
    } else if (blk) {
        m3mul(blk->r, o->lm, o->m);
        o->x = blk->t[0] + blk->r[0] * o->lpos[0] + blk->r[1] * o->lpos[1] + blk->r[2] * o->lpos[2];
        o->y = blk->t[1] + blk->r[3] * o->lpos[0] + blk->r[4] * o->lpos[1] + blk->r[5] * o->lpos[2];
        o->z = blk->t[2] + blk->r[6] * o->lpos[0] + blk->r[7] * o->lpos[1] + blk->r[8] * o->lpos[2];
        o->in_block = !ground;
    } else {
        memcpy(o->m, o->lm, sizeof o->m);
        o->x = o->lpos[0]; o->y = o->lpos[1]; o->z = o->lpos[2];
    }
    o->ground = ground;
    if (index >= 0 && index < 4096) idxmap[index] = m->nobj - 1;
}

/* 0x100c1120 */
static unsigned widen(unsigned mk)
{
    if (mk & 0x730) mk |= 0x730;
    if (mk & 3) mk |= 3;
    return mk;
}

static void cpy(char *d, size_t n, const uint8_t *s, size_t max)
{
    size_t k = 0;
    while (k < max && k + 1 < n && s[k]) { d[k] = (char)s[k]; k++; }
    d[k] = 0;
}

static int load_rec(mission *m, hg_archive *a, const char *name, int depth, char *err, size_t errlen)
{
    if (depth > MAX_DEPTH) return 0;
    size_t len;
    uint8_t *d = hga_load(a, name, "BWD", &len, NULL);
    if (!d) {
        if (depth == 0) { snprintf(err, errlen, "mission record %s not found", name); return -1; }
        return 0;   /* USERSTAR etc. are written by the shell at run time */
    }
    static bwd_chunk c[MAX_CHUNKS];
    int n = bwd_chunks(d, len, c, MAX_CHUNKS);
    if (n < 0) { snprintf(err, errlen, "%s.BWD is malformed", name); free(d); return -1; }
    if (n > MAX_CHUNKS) n = MAX_CHUNKS;
    bwd_chunk *cc = malloc((size_t)n * sizeof *cc);   /* recursion reuses the static array */
    memcpy(cc, c, (size_t)n * sizeof *cc);
    /* the ground tiles' records: Mnn_GRND, and RANDMESH, the random mission generator's copy of one (0x10067752) */
    int is_grnd = strstr(name, "grnd") != NULL || strstr(name, "randmesh") != NULL;
    int *idxmap = malloc(4096 * sizeof *idxmap);
    for (int k = 0; k < 4096; k++) idxmap[k] = -1;
    /* the tag the chunks register under (0x100c1152 / 0x1006663a): the record's name, a BTAG's name to its ETAG
     * (then the record's again), "$" after an MTBL chunk (0x101245ec); each opening is a scope */
    char btag[17];
    snprintf(btag, sizeof btag, "%s", name);
    int rec_scope = m->reg_seq++, scope = rec_scope;
    int affl = -1;   /* 0x100c1152's local: AFFL's team for the GTs after it in this file */
    /* the block stack (0x10186e60) and the next block's parameters (0x10122d00: turn, position) */
    blk_xf bstack[32];
    int bdepth = 0;
    int32_t bturn[3] = {0, 0, 0}, bpos[3] = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        const char *t = cc[i].tag;
        if (!strcmp(t, "INCL") && cc[i].size >= 3) {
            char inc[16];
            int k;
            for (k = 0; k < 10 && cc[i].data[2 + k]; k++) inc[k] = (char)tolower(cc[i].data[2 + k]);
            inc[k] = 0;
            if (!strcmp(inc, "userstar") && m->user_seq < 0) m->user_seq = m->reg_seq++;
            if (load_rec(m, a, inc, depth + 1, err, errlen) < 0) { free(idxmap); free(cc); free(d); return -1; }
        } else if (!strcmp(t, "BLKX") && cc[i].size >= 36) {
            for (int k = 0; k < 3; k++) {
                bturn[k] = rd_i32(cc[i].data + 0x0c + k * 4);
                bpos[k] = rd_i32(cc[i].data + 0x18 + k * 4);
            }
        } else if (!strcmp(t, "BLK") && cc[i].size >= 24) {
            /* the box, its sizes made positive; the centre; the block's matrix T(centre) T(pos) R T(-centre) */
            float lo[3], c3[3];
            for (int k = 0; k < 3; k++) {
                float a0 = (float)rd_i32(cc[i].data + k * 4), sz = (float)rd_i32(cc[i].data + 12 + k * 4);
                if (sz < 0) { a0 += sz; sz = -sz; }
                lo[k] = a0;
                c3[k] = (sz + a0 + lo[k]) / 2.0f;
            }
            blk_xf b;
            mission_rot((float)bturn[0] / 65536.0f, (float)bturn[1] / 65536.0f, (float)bturn[2] / 65536.0f, b.r);
            for (int k = 0; k < 3; k++)
                b.t[k] = c3[k] + (float)bpos[k] - (b.r[k * 3] * c3[0] + b.r[k * 3 + 1] * c3[1] + b.r[k * 3 + 2] * c3[2]);
            if (bdepth > 0) {
                const blk_xf *pb = &bstack[bdepth - 1];
                blk_xf nb;
                m3mul(pb->r, b.r, nb.r);
                for (int k = 0; k < 3; k++)
                    nb.t[k] = pb->t[k] + pb->r[k * 3] * b.t[0] + pb->r[k * 3 + 1] * b.t[1] + pb->r[k * 3 + 2] * b.t[2];
                b = nb;
            }
            if (bdepth < 32) bstack[bdepth++] = b;
            for (int k = 0; k < 3; k++) bturn[k] = bpos[k] = 0;
        } else if (!strcmp(t, "ENDB")) {
            if (bdepth > 0) bdepth--;
        } else if (!strcmp(t, "OBJ") && cc[i].size >= 60) {
            add_obj(m, cc[i].data, is_grnd, bdepth > 0 ? &bstack[bdepth - 1] : NULL, idxmap);
        } else if ((!strcmp(t, "TLUT") || !strcmp(t, "MLUT")) && cc[i].size >= 3) {
            char nm[16];
            snprintf(nm, sizeof nm, "%.10s", (const char *)cc[i].data + 2);
            material_load(&m->mat, a, nm, t[0] == 'T' ? "TLT" : "MLT");
        } else if (!strcmp(t, "HGXP") && cc[i].size >= 32) {
            const uint8_t *q = cc[i].data;
            int ty = (int16_t)rd_u16(q + 4), cnt = (int16_t)rd_u16(q + cc[i].size - 25);
            if (ty >= 0 && ty < 0x4d && cnt > 0) {
                cpy(m->fx_model[ty], sizeof m->fx_model[ty], q + cc[i].size - 23, 16);
                m->fx_count[ty] = cnt;
            }
        } else if (!strcmp(t, "MUSI") && cc[i].size >= 3) {
            snprintf(m->music, sizeof m->music, "%.12s", (const char *)cc[i].data + 2);
        } else if (!strcmp(t, "LTBL") && cc[i].size >= 3 && !m->luma[0]) {
            snprintf(m->luma, sizeof m->luma, "%.10s", (const char *)cc[i].data + 2);
        } else if (!strcmp(t, "PALG") && cc[i].size >= 16) {
            /* the loader (0x100c1152) copies 20 names of 16 bytes into the palette slots (0x100b962a), a later
             * PALG replacing an earlier one's */
            for (int k = 0; k < 20 && (k + 1) * 16 <= (int)cc[i].size; k++)
                snprintf(m->pal_names[k], sizeof m->pal_names[k], "%.16s", (const char *)cc[i].data + k * 16);
        } else if (!strcmp(t, "INIT") && cc[i].size >= 12) {
            m->tod_start = rd_i32(cc[i].data + 8);
        } else if (!strcmp(t, "PLNT") && cc[i].size >= 16) {
            m->day_length = rd_i32(cc[i].data + 12);
        } else if (!strcmp(t, "VIEW") && cc[i].size >= 8) {
            m->view_distance = rd_i32(cc[i].data + 4);
        } else if (!strcmp(t, "LITE") && cc[i].size >= 0x1c && !m->has_lite) {
            const uint8_t *q = cc[i].data;
            m->has_lite = 1;
            for (int k = 0; k < 3; k++) m->lite_dir[k] = rd_i32(q + 8 + k * 4);
            m->lite_ambient = rd_i16(q + 0x14);
            m->lite_mode = rd_i16(q + 0x16);
            m->lite_shadedist = rd_i32(q + 0x18);
        } else if (!strcmp(t, "BTAG") && cc[i].size >= 1) {
            snprintf(btag, sizeof btag, "%.16s", (const char *)cc[i].data);
            scope = m->reg_seq++;
        } else if (!strcmp(t, "ETAG")) {
            snprintf(btag, sizeof btag, "%s", name);
            scope = rec_scope;
        } else if (!strcmp(t, "NAVP") && cc[i].size >= 0x1c) {
            /* 0x100c3718 (NAVP branch of 0x100c1152): the nav table holds at most 0x80 entries */
            if (m->nnav < 0x80) {
                const uint8_t *q = cc[i].data;
                m->nav = realloc(m->nav, (size_t)(m->nnav + 1) * sizeof *m->nav);
                mission_nav *v = &m->nav[m->nnav++];
                v->seq = m->reg_seq++;
                v->scope = scope;
                snprintf(v->tag, sizeof v->tag, "%s", btag);
                v->x = (float)rd_i32(q); v->y = (float)rd_i32(q + 4); v->z = (float)rd_i32(q + 8);
                v->heading = (float)rd_i32(q + 0x0c) * (1.0f / 65536.0f);
                v->group = rd_i16(q + 0x16);
                v->mask = widen(rd_u16(q + 0x1a));
                v->radius = (float)rd_u16(q + 0x18) * 100.0f;
                v->visit = rd_i16(q + 0x10);
                cpy(v->name, sizeof v->name, q + 0x1c, 0x15);
            }
        } else if (!strcmp(t, "GPS") && cc[i].size >= 0x24) {
            const uint8_t *q = cc[i].data;
            m->gps = realloc(m->gps, (size_t)(m->ngps + 1) * sizeof *m->gps);
            mission_gps *g = &m->gps[m->ngps++];
            g->seq = m->reg_seq++;
            g->scope = scope;
            snprintf(g->tag, sizeof g->tag, "%s", btag);
            g->group = q[4]; g->leader = q[5]; g->role = q[6];
            g->react = q[8]; g->skill = q[0x14];
            g->r_1ce = rd_u16(q + 0xa); g->r_1c6 = rd_u16(q + 0xc); g->r_1ca = rd_u16(q + 0xe);
            g->mask = widen(rd_u16(q + 0x18));
            g->vflags = rd_u16(q + 0x1a);
            g->shell = !strcmp(name, "userstar");
            snprintf(g->gear, sizeof g->gear, "%.8s", (const char *)q + 0x1c);
        } else if (!strcmp(t, "GRP") && cc[i].size >= 16 * 0x11) {
            /* 0x100c0005: each group's formation name, 0x11 bytes apiece */
            for (int g = 0; g < 16; g++) snprintf(m->group_form[g], 17, "%.16s", (const char *)cc[i].data + g * 0x11);
        } else if (!strcmp(t, "STAR")) {
            /* 0x100bfeee: per group 0x18 bytes: +4 the group's side (0x107c8ef4, read by 0x10042b07), the formation name at +8 (/OE= and /OF= on the command line override
             * it for the other groups and the player's) */
            for (int g = 0; g < 16 && (size_t)(g + 1) * 0x18 <= cc[i].size; g++) {
                snprintf(m->group_form[g], 17, "%.16s", (const char *)cc[i].data + g * 0x18 + 8);
                m->group_side[g] = rd_i32(cc[i].data + g * 0x18 + 4);
                int tm = rd_i32(cc[i].data + g * 0x18);
                if (tm >= 0 && tm < 64) m->team_side[tm] = m->group_side[g];
            }
        } else if (!strcmp(t, "FTBL") && cc[i].size >= 0x10 && m->nform < MISSION_MAX_FORM) {
            /* 0x100bfca4: a formation: name (16), then slots of x, z, heading (i32 each; heading * 0x10103868) */
            const uint8_t *q = cc[i].data;
            unsigned ns = (unsigned)(cc[i].size - 0x10) / 12;
            if (ns < 9) {
                mission_form *f = &m->form[m->nform++];
                snprintf(f->name, sizeof f->name, "%.16s", (const char *)q);
                f->nslots = (int)ns;
                for (unsigned k = 0; k < ns; k++) {
                    f->x[k] = (float)rd_i32(q + 0x10 + k * 12);
                    f->z[k] = (float)rd_i32(q + 0x14 + k * 12);
                    f->heading[k] = (float)rd_i32(q + 0x18 + k * 12) * (1.0f / 65536.0f);
                }
            }
        } else if (!strcmp(t, "GT") && cc[i].size >= 0x30) {
            const uint8_t *q = cc[i].data;
            m->tgt = realloc(m->tgt, (size_t)(m->ntgt + 1) * sizeof *m->tgt);
            mission_target *g = &m->tgt[m->ntgt++];
            memset(g, 0, sizeof *g);
            g->seq = m->reg_seq++;
            g->scope = scope;
            snprintf(g->tag, sizeof g->tag, "%s", btag);
            int o = rd_i16(q), al = rd_i16(q + 2);
            g->obj = o >= 0 && o < 4096 ? idxmap[o] : -1;
            g->alt = al >= 0 && al < 4096 ? idxmap[al] : -1;
            g->hp = rd_i16(q + 0x10);
            g->mask = widen(rd_u16(q + 0x12));
            cpy(g->name, sizeof g->name, q + 0x18, 0x16);
            g->gflags = rd_u16(q + 0x14);
            g->team = affl;
        } else if (!strcmp(t, "AFFL") && cc[i].size >= 4) {
            affl = rd_i32(cc[i].data);
        } else if (!strcmp(t, "MTBL") && cc[i].size >= 0x26 + 0x97 - 8) {
            /* 0x10066090: +0 group, objectives of 0x97 bytes from +0x1e (chunk +0x26); objective 0's tag at
             * chunk +0x70 (0x10066794 compares it), its mask at chunk +0x26 */
            const uint8_t *q = cc[i].data;
            int g = rd_i32(q);
            if (g >= 0 && g < 16) {
                snprintf(m->obj0_tag[g], sizeof m->obj0_tag[g], "%.16s", (const char *)q + 0x70 - 8);
                m->obj0_mask[g] = rd_u32(q + 0x26 - 8);
                if (g + 1 > m->ngroups) m->ngroups = g + 1;
                /* the whole table (chunk offsets less the 8-byte header) */
                mission_mtbl *t = &m->mtbl[g];
                free(t->obj);
                memset(t, 0, sizeof *t);
                t->present = 1;
                t->seq = m->reg_seq++;
                t->limit = rd_i32(q + 4);
                cpy(t->ok_voice, sizeof t->ok_voice, q + 0xa, 11);
                cpy(t->fail_voice, sizeof t->fail_voice, q + 0x15, 9);
                int n = (int)((cc[i].size + 8 - 0x26) / 0x97);
                t->obj = calloc((size_t)(n ? n : 1), sizeof *t->obj);
                t->nobj = n;
                for (int k = 0; k < n; k++) {
                    const uint8_t *x = q + 0x1e + (size_t)k * 0x97;
                    mission_objective *ob = &t->obj[k];
                    ob->type = rd_u32(x);
                    ob->visible = x[4] == 'V';
                    ob->all = x[6] != 0;
                    for (int c = 0; c < MISSION_MAX_COND; c++) {
                        int kind = x[0xa + c * 4] == 'C' ? 1 : x[0xa + c * 4] == 'S' ? 2 : x[0xa + c * 4] == 'F' ? 3 : 0;
                        if (!kind) break;   /* 0x100683d6 stops at the first empty one */
                        ob->cond[c].kind = kind;
                        ob->cond[c].obj = x[0xb + c * 4];
                        ob->cond[c].group = x[0xc + c * 4] & 15;
                        ob->ncond = c + 1;
                    }
                    ob->limit = rd_i32(x + 0x2a);
                    ob->order = x[0x2e];
                    ob->must = x[0x2f] == 'M';
                    ob->count = x[0x30];
                    ob->aiflag = x[0x31];
                    cpy(ob->ok_voice, sizeof ob->ok_voice, x + 0x34, 11);
                    cpy(ob->fail_voice, sizeof ob->fail_voice, x + 0x3f, 11);
                    cpy(ob->tag, sizeof ob->tag, x + 0x4a, 9);
                    ob->arg_group = rd_i16(x + 0x53);
                    ob->arg_obj = rd_i16(x + 0x55);
                    cpy(ob->text, sizeof ob->text, x + 0x57, 0x40);
                }
            }
            snprintf(btag, sizeof btag, "$");   /* 0x100c1b31: 0x1006663a("$"), the tag "$" */
            scope = m->reg_seq++;
        }
    }
    free(idxmap);
    free(cc);
    free(d);
    return 0;
}

int mission_load(mission *m, hg_archive *a, const char *name, char *err, size_t errlen)
{
    memset(m, 0, sizeof *m);
    m->user_seq = -1;
    snprintf(m->name, sizeof m->name, "%s", name);
    for (char *p = m->name; *p; p++) *p = (char)tolower((unsigned char)*p);
    /* "m01" or "m01_scn1" -> m01_scn1 */
    char scn[24];
    if (strstr(m->name, "scn")) snprintf(scn, sizeof scn, "%s", m->name);
    else snprintf(scn, sizeof scn, "%.*s_scn1", (int)strcspn(m->name, "_"), m->name);
    snprintf(m->scn, sizeof m->scn, "%s", scn);
    if (load_rec(m, a, scn, 0, err, errlen) < 0) { mission_free(m); return -1; }
    /* The palette for the starting time of day: 0x10030611 fades to the slot of 0x10110060[time of day] (dawn 4,
     * day 0, dusk 4, night 8) - from black (slot 16, 0x100b96b7) over 0x16a ticks at the start. The time of day
     * moves on with the mission clock (0x100304f4: hours of day_length / 24; dawn from 5, day 7, dusk 17, night 19),
     * which takes days of play at the data's day lengths. */
    static const int tod_slot[4] = {4, 0, 4, 8};
    int tod = m->tod_start >= 0 && m->tod_start < 4 ? m->tod_start : 1;
    snprintf(m->palette, sizeof m->palette, "%s", m->pal_names[tod_slot[tod]]);
    return 0;
}

mission_start mission_group_start(const mission *m, int group)
{
    mission_start st;
    memset(&st, 0, sizeof st);
    st.nav = -1;
    if (group < 0 || group >= m->ngroups || !m->obj0_tag[group][0]) return st;
    /* 0x10066794 matches the BTAG name case-insensitively (_strcmpi) and needs the masks to meet. It also checks
     * an objective state byte (== 7) whose setter is not traced yet; objective 0 passes it in every mission tried. */
    for (int i = 0; i < m->nnav; i++) {
        const mission_nav *v = &m->nav[i];
        const char *a = v->tag, *b = m->obj0_tag[group];
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (*a || *b) continue;
        if (!(v->mask & m->obj0_mask[group])) continue;
        st.found = 1; st.nav = i;
        st.x = v->x; st.y = v->y; st.z = v->z; st.heading = v->heading;
        break;
    }
    return st;
}

void mission_free(mission *m)
{
    free(m->obj);
    free(m->nav);
    free(m->gps);
    free(m->tgt);
    for (int g = 0; g < 16; g++) free(m->mtbl[g].obj);
    material_free(&m->mat);
    memset(m, 0, sizeof *m);
    m->user_seq = -1;
}
