/* Unified online in-game menu. On a remote server, Escape / controller Start opens this
 * engine-drawn menu instead of the server UI's in-game menu. The server's own menu (its options,
 * votes, server information) stays one entry away; no server module is replaced or modified. */
#include "../engine/src/client/client.h"

enum { RESUME, LIMBO, SERVER, LEAVE, QUIT, ITEMS };
static const char *labels[ITEMS] = {"RESUME", "TEAM / CLASS", "SERVER MENU", "LEAVE MATCH", "QUIT GAME"};
static const char *hints[ITEMS] = {
    "Back to the match", "Original limbo: command map, team, class and weapon",
    "This server's own menu: options, votes, server information",
    "Disconnect and return to the main menu", "Close Enemy Territory: Arcade Edition"};
static const float white[] = {.95f,.95f,.88f,1}, dim[] = {.63f,.66f,.61f,1}, gold[] = {1,.86f,.58f,1},
    steel[] = {.78f,.77f,.7f,1}, shade[] = {0,0,0,.45f}, plate[] = {.06f,.07f,.06f,.96f}, flat[] = {.16f,.2f,.17f,1},
    lit[] = {.3f,.37f,.28f,1}, shadow[] = {0,0,0,.85f};
static qboolean menuOpen, cursorShown;
static int row, epoch;
static float mouseX, mouseY, originX, originY, uiScale;
static fontInfo_t font;

/* Split-screen online views leave together through Leave match; Quit stays in the launcher. */
static int Count(void) { return CL_OnlinePane() ? QUIT : ITEMS; }
static qboolean Online(void) {
    return Cvar_VariableIntegerValue("et_unified") && !Cvar_VariableIntegerValue("ui_rustSlice") &&
        cls.state == CA_ACTIVE && !clc.demoplaying;
}
qboolean CL_OnlineMenuOpen(void) { return menuOpen; }
int CL_OnlineMenuEpoch(void) { return epoch; }
void CL_OnlineMenuClose(void) {
    if (!menuOpen) return;
    menuOpen = qfalse; ++epoch;
    Com_Printf("ONLINEMENU closed\n");
}

/* Escape with no menu/console/limbo open while connected to a remote server. */
qboolean CL_OnlineMenuEscape(void) {
    if (!Online() || cls.keyCatchers) return qfalse;
    /* Menu keys are consumed; release game buttons (+forward, +attack) held before opening. */
    Key_ClearStates();
    memset(cl.mouseDx, 0, sizeof(cl.mouseDx)); memset(cl.mouseDy, 0, sizeof(cl.mouseDy));
    menuOpen = qtrue; ++epoch; row = RESUME; cursorShown = qfalse; mouseX = 320; mouseY = 240;
    Com_Printf("ONLINEMENU opened items=%d\n", Count());
    return qtrue;
}

static void Activate(int item) {
    Com_Printf("ONLINEMENU selected item=%s\n", labels[item]);
    CL_OnlineMenuClose();
    if (item == LIMBO) CL_OpenLimbo(0);
    else if (item == SERVER) VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_INGAME);
    else if (item == LEAVE) Cbuf_AddText("disconnect\n");
    else if (item == QUIT) Cbuf_AddText("quit\n");
}

/* Layout in 640x480 units, centered and uniformly scaled in this view. */
static float Top(void) { return 240 - (112 + Count() * 32) * .5f; }
static int RowAt(float x, float y) {
    int i;
    for (i = 0; i < Count(); ++i)
        if (x >= 190 && x <= 450 && y >= Top() + 66 + i * 32 && y <= Top() + 92 + i * 32) return i;
    return -1;
}

/* Keyboard, mouse and (via the local-menu route) controller input while open. */
qboolean CL_OnlineMenuEvent(int command, int a, int b) {
    int hovered;
    if (!menuOpen) return qfalse;
    if (!Online()) { CL_OnlineMenuClose(); return qfalse; }
    if (command == CG_MOUSE_EVENT) {
        float scale = fminf(cls.glconfig.vidWidth / 640.f, cls.glconfig.vidHeight / 480.f);
        mouseX = Com_Clamp(0, 640, mouseX + a / scale); mouseY = Com_Clamp(0, 480, mouseY + b / scale);
        cursorShown = qtrue;
        if ((hovered = RowAt(mouseX, mouseY)) >= 0) row = hovered;
    } else if (command == CG_KEY_EVENT && b) {
        if (a == K_ESCAPE || a == K_JOY2) CL_OnlineMenuClose();  /* B, Start, Back */
        else if (a == K_UPARROW || a == K_KP_UPARROW || a == K_JOY3 || a == K_MWHEELUP) row = (row + Count() - 1) % Count();
        else if (a == K_DOWNARROW || a == K_KP_DOWNARROW || a == K_JOY4 || a == K_MWHEELDOWN || a == K_TAB) row = (row + 1) % Count();
        else if (a == K_ENTER || a == K_KP_ENTER || a == K_JOY1) Activate(row);
        else if (a == K_MOUSE1 && cursorShown && (hovered = RowAt(mouseX, mouseY)) >= 0) Activate(hovered);
    }
    return qtrue; /* Everything else, including typed characters, stays in the menu. */
}

static void Box(float x, float y, float w, float h, const float *color) {
    re.SetColor(color);
    re.DrawStretchPic(originX + x * uiScale, originY + y * uiScale, w * uiScale, h * uiScale, 0, 0, 1, 1, cls.whiteShader);
}
static float Measure(const char *text, float scale) {
    const unsigned char *p; float width = 0;
    for (p = (const unsigned char *)text; *p; ++p) {
        if (Q_IsColorString((const char *)p)) { ++p; continue; }
        if (*p >= 32) width += font.glyphs[*p].xSkip * scale;
    }
    return width;
}
/* Shadowed courier text; color codes skipped; truncated at width; optional centering. */
static void Text(float x, float y, float size, const char *text, float width, const float *color, qboolean center) {
    float scale = size / 48.f * font.glyphScale * uiScale, offset = fmaxf(1.f, uiScale), pass;
    if (center) x += (width - fminf(Measure(text, scale) / uiScale, width)) * .5f;
    for (pass = 0; pass < 2; ++pass) {
        const unsigned char *p = (const unsigned char *)text;
        float used = 0, shift = pass ? 0 : offset;
        re.SetColor(pass ? color : shadow);
        while (*p) {
            glyphInfo_t *g;
            if (Q_IsColorString((const char *)p)) { p += 2; continue; }
            if (*p < 32) { ++p; continue; }
            g = &font.glyphs[*p++];
            if (used + g->xSkip * scale > width * uiScale) break;
            re.DrawStretchPic(originX + x * uiScale + used + g->pitch * scale + shift, originY + y * uiScale - g->top * scale + shift,
                g->imageWidth * scale, g->imageHeight * scale, g->s, g->t, g->s2, g->t2, g->glyph);
            used += g->xSkip * scale;
        }
    }
}

void CL_OnlineMenuDraw(void) {
    float top, height;
    const char *info;
    int i;
    if (!menuOpen) return;
    if (!Online()) { CL_OnlineMenuClose(); return; }
    if (cls.keyCatchers & KEYCATCH_UI) return;
    re.RegisterFont("courbd", 21, &font); /* Cached by the renderer; safe across restarts. */
    uiScale = fminf(cls.glconfig.vidWidth / 640.f, cls.glconfig.vidHeight / 480.f);
    originX = (cls.glconfig.vidWidth - 640 * uiScale) * .5f; originY = (cls.glconfig.vidHeight - 480 * uiScale) * .5f;
    re.SetColor(shade);
    re.DrawStretchPic(0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 1, 1, cls.whiteShader);
    top = Top(); height = 112 + Count() * 32;
    re.SetColor(NULL);
    if (!CL_ArtNine("panel", originX + 170 * uiScale, originY + top * uiScale, 300 * uiScale, height * uiScale, 12 * uiScale, qtrue))
        Box(170, top, 300, height, plate);
    re.SetColor(NULL);
    if (!CL_ArtThree("header", originX + 180 * uiScale, originY + (top + 10) * uiScale, 280 * uiScale, 26 * uiScale, 13 * uiScale))
        Box(180, top + 10, 280, 26, flat);
    Text(196, top + 29, 19, "MATCH MENU", 250, white, qfalse);
    info = CL_LocalConfigString(0, CS_SERVERINFO);
    Text(186, top + 54, 13, va("%s^7  /  %s", Info_ValueForKey(info, "sv_hostname"), Info_ValueForKey(info, "mapname")), 268, dim, qtrue);
    for (i = 0; i < Count(); ++i) {
        float y = top + 66 + i * 32;
        re.SetColor(NULL);
        if (!CL_ArtNine(i == row ? "button_focus" : "button", originX + 190 * uiScale, originY + y * uiScale,
                        260 * uiScale, 26 * uiScale, 6 * uiScale, qtrue))
            Box(190, y, 260, 26, i == row ? lit : flat);
        Text(190, y + 18, 18, labels[i], 260, i == row ? gold : steel, qtrue);
    }
    Text(180, top + height - 30, 12, hints[row], 280, dim, qtrue);
    Text(180, top + height - 14, 12, "A: select    B / Start / Esc: resume", 280, white, qtrue);
    if (cursorShown) {
        re.SetColor(NULL);
        re.DrawStretchPic(originX + mouseX * uiScale, originY + mouseY * uiScale, 20 * uiScale, 20 * uiScale,
            0, 0, 1, 1, re.RegisterShaderNoMip("ui/assets/3_cursor3"));
    }
    re.SetColor(NULL);
}
