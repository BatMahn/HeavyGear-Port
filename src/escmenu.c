#include "escmenu.h"

#include <math.h>
#include <stddef.h>

static const char *const onoff[2] = {"Off", "On"};   /* the choices' strings (0x10118490: 0x10187ab0, 0x10187964) */

void escmenu_fill(hud_state *hs, int m, const escmenu_vals *v)
{
    hs->box_open = 1;
    hs->menu_style = 1;
    hs->menu_sel = v ? v->sel : 0;
    for (int i = 0; i < HUD_MENU_MAX; i++) { hs->menu[i].kind = 0; hs->menu[i].value = 0; hs->menu[i].vtext = NULL; }
    if (m == ESCMENU_MAIN) {
        static const char *const items[5] = {"Abort Mission", "Monitor Brightness", "Audio Ctrl", "Combat Variables",
                                             "Flee to Windows"};
        hs->menu_title = "MAIN MENU";
        hs->nmenu = 6;
        for (int i = 0; i < 5; i++) { hs->menu[i].type = HUD_MENU_ITEM; hs->menu[i].text = items[i]; }
        hs->menu[5].type = HUD_MENU_EXIT;
        hs->menu[5].text = "Accept";
    } else if (m == ESCMENU_ABORT) {
        hs->menu_title = "ABORT MISSION";
        hs->nmenu = 2;
        hs->menu[0].type = HUD_MENU_SPACER; hs->menu[0].text = "Confirmation requested";
        hs->menu[1].type = HUD_MENU_ITEM; hs->menu[1].text = "Accept";
    } else if (m == ESCMENU_FLEE) {
        hs->menu_title = "FLEE TO WINDOWS";
        hs->nmenu = 1;
        hs->menu[0].type = HUD_MENU_ITEM; hs->menu[0].text = "Accept";
    } else if (m == ESCMENU_BRIGHT) {
        hs->menu_title = "BRIGHTNESS";
        hs->nmenu = 2;
        hs->menu[0].type = HUD_MENU_ITEM; hs->menu[0].text = "Monitor Brightness";
        hs->menu[0].kind = HUD_MENU_SLIDER; hs->menu[0].value = v ? v->bright : 0;
        hs->menu[1].type = HUD_MENU_EXIT; hs->menu[1].text = "Accept";
    } else if (m == ESCMENU_AUDIO) {
        static const char *const items[3] = {"Music", "Sound Effects", "Voice"};
        hs->menu_title = "AUDIO VOLUME";
        hs->nmenu = 5;
        for (int i = 0; i < 3; i++) {
            hs->menu[i].type = HUD_MENU_ITEM; hs->menu[i].text = items[i]; hs->menu[i].kind = HUD_MENU_SLIDER;
            hs->menu[i].value = !v ? 0 : i == 0 ? v->music : i == 1 ? v->fx : v->voice;
        }
        hs->menu[3].type = HUD_MENU_SPACER; hs->menu[3].text = "3Dsound";
        hs->menu[3].kind = HUD_MENU_CHOICE; hs->menu[3].vtext = onoff[v && v->sound3d ? 1 : 0];
        hs->menu[4].type = HUD_MENU_EXIT; hs->menu[4].text = "Accept";
    } else {
        static const char *const items[4] = {"Particles", "Shadows", "Multiple Lights", "Joystick Auto Center"};
        hs->menu_title = "COMBAT VARIABLES";
        hs->nmenu = 5;
        for (int i = 0; i < 4; i++) {
            int on = !v ? 0 : i == 0 ? v->particles : i == 1 ? v->shadows : i == 2 ? v->lights : v->joycenter;
            hs->menu[i].type = HUD_MENU_ITEM; hs->menu[i].text = items[i];
            hs->menu[i].kind = HUD_MENU_CHOICE; hs->menu[i].vtext = onoff[on ? 1 : 0];
        }
        hs->menu[4].type = HUD_MENU_EXIT; hs->menu[4].text = "Accept";
    }
}

/* a slider's step (0x100645c0: +0x199a, held to 0 .. 0x10000) */
static void step(int *x) { *x += 0x199a; if (*x > 0x10000) *x = 0x10000; if (*x < 0) *x = 0; }

int escmenu_pick(int m, int digit, escmenu_vals *v)
{
    if (m == ESCMENU_MAIN) {
        if (v) v->sel = digit >= 1 && digit <= 5 ? digit - 1 : 5;
        switch (digit) {
        case 0: return ESCMENU_CLOSE;
        case 1: return ESCMENU_OPEN_ABORT;
        case 2: return ESCMENU_OPEN_BRIGHT;
        case 3: return ESCMENU_OPEN_AUDIO;
        case 4: return ESCMENU_OPEN_COMBAT;
        case 5: return ESCMENU_OPEN_FLEE;
        default: return ESCMENU_NONE;
        }
    }
    if (m == ESCMENU_ABORT || m == ESCMENU_FLEE) {
        if (digit == 1) return m == ESCMENU_ABORT ? ESCMENU_DO_ABORT : ESCMENU_DO_FLEE;
        return ESCMENU_NONE;
    }
    if (!v) return ESCMENU_NONE;
    if (digit == 0) { v->sel = 0; return ESCMENU_COMMIT; }
    if (m == ESCMENU_BRIGHT) {
        if (digit != 1) return ESCMENU_NONE;
        v->sel = 0;
        step(&v->bright);
        return ESCMENU_STEP;
    }
    if (m == ESCMENU_AUDIO) {
        if (digit < 1 || digit > 3) return ESCMENU_NONE;   /* 3Dsound is unnumbered (type 3) */
        v->sel = digit - 1;
        step(digit == 1 ? &v->music : digit == 2 ? &v->fx : &v->voice);
        return ESCMENU_STEP;
    }
    if (digit < 1 || digit > 4) return ESCMENU_NONE;
    v->sel = digit - 1;
    int *o = digit == 1 ? &v->particles : digit == 2 ? &v->shadows : digit == 3 ? &v->lights : &v->joycenter;
    *o = (*o + 1) % 2;   /* 0x10064deb: the next of its strings */
    return ESCMENU_STEP;
}

int escmenu_level(int value)
{
    /* 0x10018650: value x 15 (16.16), rounded */
    int64_t p = (int64_t)value * 15;
    return (int)(p >> 16) + (int)((p >> 15) & 1);
}

int escmenu_value(int level)
{
    /* 0x100185e0: ftol(level / 15 x 65536 + 0.5) */
    return (int)((float)level / 15.0f * 65536.0f + 0.5f);
}

void escmenu_gamma(int level, unsigned char out[64])
{
    double e = 1.0 / ((double)level * 0.0625 + 0.5);
    for (int c = 0; c < 64; c++) out[c] = (unsigned char)(int)(pow((double)c / 63.0, e) * 63.0);
}
