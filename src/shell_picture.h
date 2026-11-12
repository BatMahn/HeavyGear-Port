/* shell_picture.h - one record of the shell's picture table (see shell.h) */
#ifndef HG_SHELL_PICTURE_H
#define HG_SHELL_PICTURE_H
typedef struct {
    const char *name, *ext;
    int lang, parent, kind;
    int x0, y0, x1, y1;
} shell_picture;
/* one entry of the campaign table (0x101ace60): the mission's scenario and briefing records, the cutscene played
 * before it, its title */
typedef struct {
    const char *scenario, *briefing, *movie, *title;
} shell_campaign_entry;
#endif
