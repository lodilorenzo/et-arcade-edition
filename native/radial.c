/* Controller wheels over the stock ET HUD; server cgame DLLs stay untouched. */
#include "../engine/src/client/client.h"
#include "../engine/src/game/bg_public.h"
#include "gamepad_math.h"

#define SLIDE_MS 100
#define EXIT_MS 180 /* Icon pulse and exit run together, never delay the command. */
static const struct { const char *id, *label, *request, *icon; } assistance[] = {
    {"Medic", "Medic", "Need a Medic!", "gfx/limbo/ic_medic"},
    {"NeedEngineer", "Engineer", "We need an engineer!", "gfx/limbo/ic_engineer"},
    {"NeedAmmo", "Field Ops", "I need ammo!", "gfx/limbo/ic_fieldops"},
    {"NeedOps", "Covert Ops", "We need Covert Ops!", "gfx/limbo/ic_covertops"},
    {"NeedBackup", "Soldier", "I need backup!", "gfx/limbo/ic_soldier"}
};
typedef struct {
    int kind, selected, openedAt, closedAt, hoveredAt;
    qboolean closing, feedback;
    float closeFrom;
} wheel_t;
static wheel_t wheels[ET_LOCAL_PLAYERS];
static qboolean loaded;
static fontInfo_t font;
static cvar_t *deadzone, *centerHelp;
static int Slots(const wheel_t *w) { return w->kind == 1 ? 8 : sizeof(assistance)/sizeof(assistance[0]); }
/* Fraction outside the viewport: 1 = below the screen, 0 = centered. */
static float Slide(const wheel_t *w, int now) {
    if (!w->closing) return 1.f - GP_Clamp((now - w->openedAt)/(float)SLIDE_MS, 0, 1);
    return w->closeFrom + (1.f - w->closeFrom) * GP_Clamp((now - w->closedAt)/(float)EXIT_MS, 0, 1);
}
/* ponytail: stock ET plus Legacy inventory/banks; other remapped/custom IDs need a mod adapter. */
static const struct { int weapon, bank; const char *label, *icon; } gear[] = {
    {WP_KNIFE,1,"Knife","icons/iconw_knife_1"},
    {WP_LUGER,2,"Luger","icons/iconw_luger_1"}, {WP_COLT,2,"Colt","icons/iconw_colt_1"},
    {WP_AKIMBO_COLT,2,"Dual Colts","icons/iconw_colt_1"}, {WP_AKIMBO_LUGER,2,"Dual Lugers","icons/iconw_luger_1"},
    {WP_SILENCER,2,"Silenced Luger","icons/iconw_luger_1"}, {WP_SILENCED_COLT,2,"Silenced Colt","icons/iconw_colt_1"},
    {WP_AKIMBO_SILENCEDCOLT,2,"Dual silenced Colts","icons/iconw_colt_1"}, {WP_AKIMBO_SILENCEDLUGER,2,"Dual silenced Lugers","icons/iconw_luger_1"},
    {WP_MP40,3,"MP40","icons/iconw_mp40_1"}, {WP_THOMPSON,3,"Thompson","icons/iconw_thompson_1"},
    {WP_STEN,3,"Sten","icons/iconw_sten_1"}, {WP_GARAND,3,"Garand","icons/iconw_m1_garand_1"},
    {WP_KAR98,3,"Kar98","icons/iconw_kar98_1"}, {WP_CARBINE,3,"Carbine","icons/iconw_m1_garand_1"},
    {WP_PANZERFAUST,3,"Panzerfaust","icons/iconw_panzerfaust_1"}, {WP_FLAMETHROWER,3,"Flamethrower","icons/iconw_flamethrower_1"},
    {WP_FG42,3,"FG42","icons/iconw_fg42_1"}, {WP_K43,3,"K43","icons/iconw_kar98_1"},
    {WP_MOBILE_MG42,3,"Mobile MG42","icons/iconw_mg42_1"}, {WP_MORTAR,3,"Mortar","icons/iconw_mortar_1"},
    {WP_GARAND_SCOPE,3,"Scoped Garand","icons/iconw_m1_garand_1"}, {WP_K43_SCOPE,3,"Scoped K43","icons/iconw_kar98_1"},
    {WP_FG42SCOPE,3,"Scoped FG42","icons/iconw_fg42_1"}, {WP_GPG40,3,"Rifle grenade","icons/iconw_kar98_1"}, {WP_M7,3,"Rifle grenade","icons/iconw_m1_garand_1"},
    {WP_MORTAR_SET,3,"Deployed mortar","icons/iconw_mortar_1"}, {WP_MOBILE_MG42_SET,3,"Deployed MG42","icons/iconw_mg42_1"},
    {WP_GRENADE_LAUNCHER,4,"Grenade","icons/iconw_grenade_1"}, {WP_GRENADE_PINEAPPLE,4,"Grenade","icons/iconw_pineapple_1"},
    {WP_MEDIC_SYRINGE,5,"Syringe","icons/iconw_syringe_1"}, {WP_PLIERS,5,"Pliers","icons/iconw_pliers_1"},
    {WP_SMOKE_MARKER,5,"Airstrike marker","icons/iconw_smokegrenade_1"}, {WP_SMOKE_BOMB,5,"Smoke bomb","icons/iconw_smokegrenade_1"},
    {WP_DYNAMITE,6,"Dynamite","icons/iconw_dynamite_1"}, {WP_MEDKIT,6,"Medkit","icons/iconw_medheal_1"},
    {WP_AMMO,6,"Ammo pack","icons/iconw_ammopack_1"}, {WP_SATCHEL,6,"Satchel","icons/iconw_satchel_1"}, {WP_SATCHEL_DET,6,"Detonator","icons/iconw_radio_1"},
    {WP_LANDMINE,7,"Landmine","icons/iconw_landmine_1"}, {WP_MEDIC_ADRENALINE,7,"Adrenaline","icons/iconw_syringe_1"},
    {WP_BINOCULARS,8,"Binoculars","icons/iconw_binoculars_1"},
    /* Legacy 2.86 additions (bg_public.h); enabled only for its game directory. */
    {48,1,"Ka-Bar knife","icons/iconw_knife_kbar_1"},
    {49,3,"Mobile Browning","icons/iconw_mg42_1"}, {50,3,"Deployed Browning","icons/iconw_mg42_1"},
    {51,3,"Axis mortar","icons/iconw_mortar_1"}, {52,3,"Deployed Axis mortar","icons/iconw_mortar_1"},
    {53,3,"Bazooka","icons/iconw_bazooka_1"}, {54,3,"MP34","icons/iconw_mp34_1"}
};
int CL_WeaponID(int stock) {
    if (Q_stricmp(Cvar_VariableString("fs_game"),"legacy")) return stock;
    /* Legacy removed tripmine and lockpick before adding its seven weapons. */
    if (stock==WP_TRIPMINE || stock==WP_LOCKPICK) return 0;
    return stock>=WP_AKIMBO_COLT ? stock-2 : stock>=WP_SMOKE_BOMB ? stock-1 : stock;
}
static int GearID(int item) {
    int baseCount=sizeof(gear)/sizeof(gear[0])-7;
    if (item<baseCount) return CL_WeaponID(gear[item].weapon);
    return !Q_stricmp(Cvar_VariableString("fs_game"),"legacy") ? gear[item].weapon : 0;
}
static qboolean Owned(const playerState_t *ps, int weapon) {
    return weapon > 0 && weapon < MAX_WEAPONS && ((unsigned)ps->weapons[weapon / 32] & (1u << (weapon % 32))) != 0;
}
static int GearForBank(const playerState_t *ps, int bank) {
    int i, found = -1;
    for (i = 0; i < sizeof(gear)/sizeof(gear[0]); ++i) if (gear[i].bank == bank && Owned(ps, GearID(i))) {
        if (GearID(i) == ps->weapon) return i;
        if (found < 0) found = i;
    }
    return found;
}
static qboolean Ready(int local, playerState_t *ps) {
    return local >= 0 && local < CL_SplitGamepadCount() && cls.state == CA_ACTIVE && !cls.keyCatchers && !clc.demoplaying &&
        !CL_LocalLoadoutOpen(local) && CL_LocalPlayerState(local, ps) && ps->stats[STAT_HEALTH] > 0 &&
        ps->pm_type == PM_NORMAL && !(ps->pm_flags & PMF_FOLLOW) && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR;
}
qboolean CL_RadialOpen(int local) { return local >= 0 && local < ET_LOCAL_PLAYERS && wheels[local].kind && !wheels[local].closing; }
void CL_RadialCancel(int local) { if (local >= 0 && local < ET_LOCAL_PLAYERS) memset(&wheels[local], 0, sizeof(wheels[local])); }
void CL_RadialReset(void) {
    int i; for (i = 0; i < ET_LOCAL_PLAYERS; ++i) CL_RadialCancel(i);
    loaded = qfalse;
}
qboolean CL_RadialBegin(int local, int kind) {
    playerState_t ps; wheel_t *w;
    if ((kind != 1 && kind != 2) || !Ready(local, &ps)) return qfalse;
    if (!deadzone) deadzone = Cvar_Get("cl_gamepadRadialDeadzone", "0.35", CVAR_ARCHIVE);
    if (!loaded) { re.RegisterFont("courbd", 21, &font); loaded = qtrue; }
    w = &wheels[local]; memset(w, 0, sizeof(*w)); w->kind = kind; w->selected = -1; w->openedAt = cls.realtime;
    Com_DPrintf("RADIAL opened player=%d kind=%d\n", local + 1, kind);
    return qtrue;
}
/* Clockwise sectors, north first; releasing at center sends nothing. */
static int Sector(float x, float y, float dead, int slots) {
    float step = 2.f * (float)M_PI / slots, angle;
    if (!isfinite(x) || !isfinite(y) || x*x + y*y < dead*dead) return -1;
    angle = atan2f(x, y) + step*.5f;
    if (angle < 0.f) angle += 2.f * (float)M_PI;
    return ((int)(angle / step)) % slots;
}
void CL_RadialInput(int local, float x, float y) {
    wheel_t *w; playerState_t ps;
    if (!CL_RadialOpen(local)) return;
    if (!Ready(local, &ps)) { CL_RadialCancel(local); return; }
    w = &wheels[local];
    {   int next = Sector(x, y, GP_Clamp(deadzone->value, .15f, .8f), Slots(w));
        if (next != w->selected) w->hoveredAt = cls.realtime; /* Restart the icon's grow animation. */
        w->selected = next;
    }
}
void CL_RadialEnd(int local, qboolean commit) {
    playerState_t ps; wheel_t *w; char command[64]; int selected;
    if (!CL_RadialOpen(local)) return;
    w = &wheels[local]; selected = w->selected;
    if (commit && selected >= 0 && Ready(local, &ps)) {
        if (w->kind == 1 && GearForBank(&ps, selected + 1) >= 0) {
            snprintf(command, sizeof(command), "weaponbank %d", selected + 1);
            w->feedback = CL_LocalCgameCommand(local, command);
            if (w->feedback) Com_Printf("RADIAL selected player=%d equipment bank=%d\n", local + 1, selected + 1);
        } else if (w->kind == 2 && selected < Slots(w)) {
            snprintf(command, sizeof(command), "vsay_team %s", assistance[selected].id);
            w->feedback = CL_LocalServerCommand(local, command);
            if (w->feedback) Com_Printf("RADIAL selected player=%d voice=%s audience=0\n", local + 1, assistance[selected].id);
        }
    }
    w->closeFrom = Slide(w, cls.realtime); w->closing = qtrue; w->closedAt = cls.realtime;
}
static void Label(float x, float y, float scale, const char *text, float maxWidth, const float *color) {
    const unsigned char *p; float width = 0;
    scale *= font.glyphScale;
    for (p = (const unsigned char *)text; *p; ++p) if (*p >= 32) width += font.glyphs[*p].xSkip * scale;
    if (width > maxWidth && width > 0) scale *= maxWidth / width;
    width = 0;
    for (p = (const unsigned char *)text; *p; ++p) if (*p >= 32) width += font.glyphs[*p].xSkip * scale;
    x -= width * .5f;
    {   /* Dark drop shadow first: text stays readable over brass, steel and stamps. */
        static const float shadow[] = {0,0,0,.85f};
        float offset = fmaxf(1.f, font.glyphs['A'].imageHeight * scale * .07f), sx = x;
        re.SetColor(shadow);
        for (p = (const unsigned char *)text; *p; ++p) if (*p >= 32) {
            glyphInfo_t *g = &font.glyphs[*p];
            re.DrawStretchPic(sx + offset + g->pitch*scale, y + offset - g->top*scale, g->imageWidth*scale, g->imageHeight*scale, g->s,g->t,g->s2,g->t2,g->glyph);
            sx += g->xSkip*scale;
        }
    }
    re.SetColor(color);
    for (p = (const unsigned char *)text; *p; ++p) if (*p >= 32) {
        glyphInfo_t *g = &font.glyphs[*p];
        re.DrawStretchPic(x + g->pitch*scale, y - g->top*scale, g->imageWidth*scale, g->imageHeight*scale, g->s,g->t,g->s2,g->t2,g->glyph);
        x += g->xSkip*scale;
    }
}
/* One segment. With the generated dial (tex != 0) radii inner..outer map onto texture radii
 * texInner..texOuter (fractions of its half-size), so hub and ring keep the art's proportions. */
static void Wedge(float cx, float cy, float inner, float outer, int slot, int slots, const float *color,
                  qhandle_t tex, float texInner, float texOuter) {
    int i, j;
    for (i = 0; i < 12; ++i) {
        polyVert_t p[4] = {0};
        float a = (slot-.5f + i/12.f)*(2.f*(float)M_PI/slots) + (i == 0 ? .012f : 0);
        float b = (slot-.5f + (i+1)/12.f)*(2.f*(float)M_PI/slots) - (i == 11 ? .012f : 0);
        float angles[] = {a,a,b,b}, radii[] = {inner,outer,outer,inner};
        for (j = 0; j < 4; ++j) {
            float r = texInner + (texOuter - texInner)*(radii[j] - inner)/(outer - inner);
            p[j].xyz[0] = cx + sinf(angles[j])*radii[j]; p[j].xyz[1] = cy - cosf(angles[j])*radii[j];
            p[j].st[0] = tex ? .5f + .5f*r*sinf(angles[j]) : .5f; p[j].st[1] = tex ? .5f - .5f*r*cosf(angles[j]) : .5f;
            p[j].modulate[0] = color[0]*255; p[j].modulate[1] = color[1]*255; p[j].modulate[2] = color[2]*255; p[j].modulate[3] = color[3]*255;
        }
        re.Add2dPolys(p, 4, tex ? tex : cls.whiteShader);
    }
}
void CL_RadialDraw(void) {
    int local;
    static const float dim[] = {.05f,.06f,.05f,.88f}, grey[] = {.6f,.6f,.6f,1}, white[] = {.95f,.95f,.9f,1};
    /* Dial art in one even tint: the choice is shown by its icon alone. The center help darkens the
     * stamped shell center behind its captions; without it the center keeps the plate tint. */
    static const float plate[] = {.78f,.78f,.76f,.96f}, hub[] = {.55f,.55f,.53f,.97f};
    qhandle_t dial;
    if (cls.state != CA_ACTIVE || cls.keyCatchers || !loaded) return;
    for (local = 0; local < ET_LOCAL_PLAYERS; ++local) if (wheels[local].kind) {
        int vx,vy,vw,vh,i,slots; float u,s,t,cx,cy,radius,pulse = 0,hintWidth; playerState_t ps; wheel_t *w = &wheels[local];
        const char *caption = "Release at center: none";
        if (!Ready(local, &ps) || (w->closing && cls.realtime - w->closedAt >= EXIT_MS)) {
            CL_RadialCancel(local); continue;
        }
        /* Team shell base: stamped center, plain ring for icons, raised rim (same radii as the dial). */
        dial = CL_UIArt(ps.persistant[PERS_TEAM] == TEAM_AXIS ? "dial_axis" : ps.persistant[PERS_TEAM] == TEAM_ALLIES ? "dial_allies" : "dial");
        if (!dial) dial = CL_UIArt("dial");
        if (w->closing && w->feedback) pulse = sinf((float)M_PI * GP_Clamp((cls.realtime - w->closedAt)/(float)EXIT_MS,0,1));
        CL_LocalViewport(local, &vx,&vy,&vw,&vh); RE_SplitClip(vx,vy,vw,vh);
        /* u: viewport scale; s: wheel geometry (72% size); t: text, kept a little larger for legibility. */
        u = fminf(vw/640.f, vh/480.f); s = u*.72f; t = u*.85f; radius = 180*s; slots = Slots(w);
        /* Equipment on the left of the view, class requests on the right; slightly low, clear of the compass. */
        cx = w->kind == 1 ? vx + 28*u + radius : vx + vw - 28*u - radius;
        cy = vy + vh*.5f - 8*u + (vh*.5f + radius)*Slide(w,cls.realtime); /* Above the bottom HUD in short views. */
        /* Shell center first: the selected icon stays on top while the ring exits. */
        if (dial || centerHelp->integer)
            for (i = 0; i < slots; ++i) Wedge(cx,cy,0,64*s,i,slots,!dial ? dim : centerHelp->integer ? hub : plate,dial,0,.48f);
        for (i = 0; i < slots; ++i) {
            const char *label = ""; int item = -1; qhandle_t icon = 0;
            float a = i*2.f*(float)M_PI/slots, x = cx + sinf(a)*124*s, y = cy - cosf(a)*124*s; /* Icon and label inside the plain ring. */
            qboolean chosen = i == w->selected;
            /* Hovered icon eases up to 1.3x in 80 ms; a committed choice pulses to ~1.7x while the wheel exits. */
            float grow = chosen ? GP_Clamp((cls.realtime - w->hoveredAt)/80.f, 0, 1) : 0;
            float boost = 1.f + .3f*grow*(2.f - grow) + .4f*pulse;
            if (dial) Wedge(cx,cy,65*s,180*s,i,slots,plate,dial,.52f,.93f);
            else Wedge(cx,cy,65*s,180*s,i,slots,dim,0,0,0);
            if (w->kind == 1) {
                item = GearForBank(&ps,i+1);
                if (item >= 0) { icon = re.RegisterShaderNoMip(va("%s_select",gear[item].icon)); label = gear[item].label; }
            } else {
                label = assistance[i].label; icon = re.RegisterShaderNoMip(assistance[i].icon);
            }
            if (icon) {
                float width = (w->kind == 1 ? 50 : 36)*s*boost, height = (w->kind == 1 ? 28 : 36)*s*boost;
                re.SetColor(white); re.DrawStretchPic(x-width*.5f,y-10*s-height*.5f,width,height,0,0,1,1,icon);
            }
            if (chosen && *label) caption = w->kind == 1 ? label : assistance[i].request;
            Label(x,y+22*s,.21f*t,CL_TranslateStringBuf(label),112*s,chosen ? white : grey);
        }
        if (centerHelp->integer) {
            Label(cx,cy-20*s,.25f*t,w->kind == 1 ? "EQUIPMENT" : "ASSISTANCE",122*s,white);
            Label(cx,cy+2*s,.19f*t,CL_TranslateStringBuf(caption),124*s,white);
            if (w->feedback) Label(cx,cy+22*s,.20f*t,w->kind == 1 ? "SELECTED" : "REQUEST SENT",120*s,white);
        }
        hintWidth = 2*fminf(cx - vx, vx + vw - cx) - 8; /* Centered under the wheel, inside the view. */
        Label(cx,cy+radius+22*u,.18f*t,w->kind == 1 ? "Left stick select / release Y equip" : "Right stick select / release Up request",hintWidth,white);
    }
    RE_SplitClip(0,0,0,0); re.SetColor(NULL);
}
/* Regression hooks only on explicitly test-enabled loopback fixtures. */
static void RadialTest_f(void) {
    int local = atoi(Cmd_Argv(2)) - 1;
    const char *action = Cmd_Argv(1);
    if (!Cvar_VariableIntegerValue("ui_rustMenuTest") ||
        !(clc.serverAddress.type == NA_LOOPBACK || (clc.serverAddress.type == NA_IP &&
          clc.serverAddress.ip[0] == 127 && !clc.serverAddress.ip[1] && !clc.serverAddress.ip[2] && clc.serverAddress.ip[3] == 1)) ||
        local < 0 || local >= ET_LOCAL_PLAYERS) return;
    if (!Q_stricmp(action,"open")) CL_RadialBegin(local,!Q_stricmp(Cmd_Argv(3),"voice") ? 2 : 1);
    else if (!Q_stricmp(action,"input") && Cmd_Argc() == 5)
        CL_RadialInput(local,atof(Cmd_Argv(3)),atof(Cmd_Argv(4)));
    else if (!Q_stricmp(action,"release")) CL_RadialEnd(local,qtrue);
    else if (!Q_stricmp(action,"cancel")) CL_RadialCancel(local);
    Com_Printf("RADIAL_TEST player=%d kind=%d selected=%d closing=%d feedback=%d\n",local+1,wheels[local].kind,wheels[local].selected,wheels[local].closing,wheels[local].feedback);
    if (!Q_stricmp(Cmd_Argv(1),"state")) {
        playerState_t ps = {0}; CL_LocalPlayerState(local,&ps);
        Com_Printf("RADIAL_STATE health=%d pm=%d flags=%d team=%d catch=%d weapon=%d weapons=%08x,%08x\n",ps.stats[STAT_HEALTH],ps.pm_type,ps.pm_flags,ps.persistant[PERS_TEAM],cls.keyCatchers,ps.weapon,ps.weapons[0],ps.weapons[1]);
    }
}
void CL_RadialInit(void) {
    CL_RadialReset();
    deadzone = Cvar_Get("cl_gamepadRadialDeadzone","0.35",CVAR_ARCHIVE);
    centerHelp = Cvar_Get("cl_gamepadRadialHelp","1",CVAR_ARCHIVE); /* 0 hides the center disc and captions. */
    Cmd_RemoveCommand("radial_test"); Cmd_AddCommand("radial_test",RadialTest_f);
}
