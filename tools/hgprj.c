/*
 * hgprj - list, verify and extract HEAVYG.PRJ.
 *   hgprj <gamedir> list
 *   hgprj <gamedir> verify              unpack every record, check its size
 *   hgprj <gamedir> get NAME.EXT out     (patch folder first, as the game)
 *   hgprj <gamedir> extract outdir       every record to outdir/EXT/NAME.EXT
 */
#include "../src/fsutil.h"
#include "../src/hgprj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: hgprj <gamedir> list|verify|get NAME.EXT out|extract outdir\n"); return 2; }
    char err[256];
    hg_archive *a = hga_open(argv[1], err, sizeof err);
    if (!a) { fprintf(stderr, "%s\n", err); return 1; }
    int n = hga_count(a), bad = 0;
    if (!strcmp(argv[2], "list")) {
        for (int i = 0; i < n; i++) { const hga_entry *e = hga_entry_at(a, i); printf("%-8s.%-3s %-5s %10u\n", e->name, e->ext, e->type, e->offset); }
    } else if (!strcmp(argv[2], "verify") || (!strcmp(argv[2], "extract") && argc > 3)) {
        int ex = argv[2][0] == 'e';
        if (ex) MKDIR(argv[3]);
        for (int i = 0; i < n; i++) {
            const hga_entry *e = hga_entry_at(a, i);
            size_t len;
            uint8_t *d = hga_load_index(a, i, &len);
            if (!d) { printf("BAD %s.%s\n", e->name, e->ext); bad++; continue; }
            if (ex) {
                char dir[1100], path[1200], fn[16];
                fs_join(dir, sizeof dir, argv[3], e->type);
                MKDIR(dir);
                snprintf(fn, sizeof fn, "%s.%s", e->name, e->ext);
                fs_join(path, sizeof path, dir, fn);
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(d, 1, len, f); fclose(f); }
            }
            free(d);
        }
        printf("%d records, %d bad\n", n, bad);
    } else if (!strcmp(argv[2], "get") && argc > 4) {
        char name[16], *dot;
        snprintf(name, sizeof name, "%s", argv[3]);
        if (!(dot = strchr(name, '.'))) return 2;
        *dot = 0;
        size_t len; int fp;
        uint8_t *d = hga_load(a, name, dot + 1, &len, &fp);
        static const char *src[] = {"archive", "Patch folder", "game folder"};
        if (!d) { fprintf(stderr, "not found\n"); return 1; }
        FILE *f = fopen(argv[4], "wb");
        if (f) { fwrite(d, 1, len, f); fclose(f); }
        printf("%zu bytes from the %s\n", len, src[fp]);
        free(d);
    }
    hga_close(a);
    return bad != 0;
}
