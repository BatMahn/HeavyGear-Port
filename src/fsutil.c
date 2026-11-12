#include "fsutil.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

void fs_join(char *out, size_t outlen, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int sep = n && (dir[n - 1] == '/' || dir[n - 1] == '\\');
    snprintf(out, outlen, "%s%s%s", dir, sep || !n ? "" : "/", name);
}

int fs_find_ci(const char *dir, const char *name, char *out, size_t outlen)
{
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d)) != NULL) {
        if (ieq(e->d_name, name)) {
            fs_join(out, outlen, dir, e->d_name);
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

uint8_t *fs_read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    if (len) *len = (size_t)n;
    return b;
}
