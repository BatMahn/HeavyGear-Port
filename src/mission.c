#include "mission.h"

#include "bwd.h"
#include "bytes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHUNKS 8192
#define MAX_DEPTH  8

/* parent >= 0: the offset is from the parent node (MW2's skeleton rule; INFERRED for HG). idxmap: OBJ index -> obj
 * slot within the current record (-1 none). */
static void add_obj(mission *m, const uint8_t *c, int ground, int in_block, const int32_t *bpos, int *idxmap)
{
    m->obj = realloc(m->obj, (size_t)(m->nobj + 1) * sizeof *m->obj);
    mission_obj *o = &m->obj[m->nobj++];
    memset(o, 0, sizeof *o);
    int k;
    for (k = 0; k < 10 && c[0x32 + k]; k++) o->model[k] = (char)tolower(c[0x32 + k]);
    o->model[k] = 0;
    for (int i = 0; i < 3; i++) o->rot[i] = (float)rd_i32(c + 0x12 + i * 4) / 65536.0f;
    o->x = (float)rd_i32(c + 0x1e);
    o->y = (float)rd_i32(c + 0x22);
    o->z = (float)rd_i32(c + 0x26);
    o->oy = o->y;
    int index = rd_i16(c), parent = rd_i16(c + 2);
    o->kind = rd_i16(c + 4);
    o->parent = -1;
    if (parent >= 0 && parent < 4096 && idxmap[parent] >= 0) {
        const mission_obj *p = &m->obj[idxmap[parent]];
        o->parent = idxmap[parent];
        o->x += p->x; o->y += p->y; o->z += p->z;
        in_block = 0;   /* already placed through the parent */
        o->child = 1;
    } else if (in_block) {
        o->x += (float)bpos[0]; o->y += (float)bpos[1]; o->z += (float)bpos[2];
    }
    o->ground = ground;
    o->in_block = in_block;
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
    int is_grnd = strstr(name, "grnd") != NULL;
    int *idxmap = malloc(4096 * sizeof *idxmap);
    for (int k = 0; k < 4096; k++) idxmap[k] = -1;
    int in_blkx = 0;
    char btag[17] = "";
    int32_t bpos[3] = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        const char *t = cc[i].tag;
        if (!strcmp(t, "INCL") && cc[i].size >= 3) {
            char inc[16];
            int k;
            for (k = 0; k < 10 && cc[i].data[2 + k]; k++) inc[k] = (char)tolower(cc[i].data[2 + k]);
            inc[k] = 0;
            if (load_rec(m, a, inc, depth + 1, err, errlen) < 0) { free(idxmap); free(cc); free(d); return -1; }
        } else if (!strcmp(t, "BLKX") && cc[i].size >= 36) {
            in_blkx = 1;
            for (int k = 0; k < 3; k++) bpos[k] = rd_i32(cc[i].data + 0x18 + k * 4);
        } else if (!strcmp(t, "ENDB")) {
            in_blkx = 0;
        } else if (!strcmp(t, "OBJ") && cc[i].size >= 60) {
            add_obj(m, cc[i].data, is_grnd && !in_blkx, in_blkx, bpos, idxmap);
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
        } else if (!strcmp(t, "PALG") && cc[i].size >= 16 && !m->palette[0]) {
            snprintf(m->palette, sizeof m->palette, "%.16s", (const char *)cc[i].data);
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
        } else if (!strcmp(t, "ETAG")) {
            btag[0] = 0;
        } else if (!strcmp(t, "NAVP") && cc[i].size >= 0x1c) {
            /* 0x100c3718 (NAVP branch of 0x100c1152): the nav table holds at most 0x80 entries */
            if (m->nnav < 0x80) {
                const uint8_t *q = cc[i].data;
                m->nav = realloc(m->nav, (size_t)(m->nnav + 1) * sizeof *m->nav);
                mission_nav *v = &m->nav[m->nnav++];
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
            snprintf(g->tag, sizeof g->tag, "%s", btag);
            g->group = q[4]; g->leader = q[5]; g->role = q[6];
            g->mask = widen(rd_u16(q + 0x18));
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
            snprintf(g->tag, sizeof g->tag, "%s", btag);
            int o = rd_i16(q), al = rd_i16(q + 2);
            g->obj = o >= 0 && o < 4096 ? idxmap[o] : -1;
            g->alt = al >= 0 && al < 4096 ? idxmap[al] : -1;
            g->hp = rd_i16(q + 0x10);
            g->mask = widen(rd_u16(q + 0x12));
            cpy(g->name, sizeof g->name, q + 0x18, 0x16);
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
                    cpy(ob->ok_voice, sizeof ob->ok_voice, x + 0x34, 11);
                    cpy(ob->fail_voice, sizeof ob->fail_voice, x + 0x3f, 11);
                    cpy(ob->tag, sizeof ob->tag, x + 0x4a, 9);
                    ob->arg_group = rd_i16(x + 0x53);
                    ob->arg_obj = rd_i16(x + 0x55);
                    cpy(ob->text, sizeof ob->text, x + 0x57, 0x40);
                }
            }
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
    snprintf(m->name, sizeof m->name, "%s", name);
    for (char *p = m->name; *p; p++) *p = (char)tolower((unsigned char)*p);
    /* "m01" or "m01_scn1" -> m01_scn1 */
    char scn[24];
    if (strstr(m->name, "scn")) snprintf(scn, sizeof scn, "%s", m->name);
    else snprintf(scn, sizeof scn, "%.*s_scn1", (int)strcspn(m->name, "_"), m->name);
    snprintf(m->scn, sizeof m->scn, "%s", scn);
    if (load_rec(m, a, scn, 0, err, errlen) < 0) { mission_free(m); return -1; }
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
}
