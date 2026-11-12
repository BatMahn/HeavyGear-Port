/*
 * fsutil.h - small file helpers. The game's files keep whatever case the player's copy has (the disc is upper case,
 * the 1.2 patch wrote lower case), so every lookup is case-insensitive.
 */
#ifndef HG_FSUTIL_H
#define HG_FSUTIL_H

#include <stddef.h>
#include <stdint.h>

/* Find `name` in `dir` ignoring case; writes the full path to out. Returns 0 if found. */
int      fs_find_ci(const char *dir, const char *name, char *out, size_t outlen);
/* Read a whole file; malloc'd, NUL-terminated past the end. NULL on failure. */
uint8_t *fs_read_all(const char *path, size_t *len);
/* dir + "/" + name */
void     fs_join(char *out, size_t outlen, const char *dir, const char *name);

#endif
