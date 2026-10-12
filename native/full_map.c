/* Shared live overview for local and online three-player layouts. Only client-visible data. */
#include "../engine/src/client/client.h"
#include "../engine/src/game/bg_public.h"
#include <errno.h>
#include <limits.h>

static struct {
    qboolean initialized, valid;
    vec2_t mins, maxs;
    int layers, ceilings[MAX_COMMANDMAP_LAYERS];
    struct { int type, data, team, yaw; vec3_t origin; } entities[MAX_GENTITIES];
    entityState_t known[MAX_GENTITIES];
    qboolean seen[MAX_GENTITIES];
    fontInfo_t font;
    int count, time;
} overview;

void CL_FullMapReset(void) { memset(&overview, 0, sizeof(overview)); }
static void InitMap(void) {
    char *text = CM_EntityString(), key[MAX_TOKEN_CHARS];
    int i;
    if (overview.initialized) return;
    overview.initialized = qtrue;
    if (strcmp(COM_Parse(&text), "{")) return;
    while (text) {
        Q_strncpyz(key, COM_Parse(&text), sizeof(key));
        if (!*key || !strcmp(key, "}")) break;
        {
            const char *value = COM_Parse(&text);
            if (!strcmp(key, "mapcoordsmins")) sscanf(value, "%f %f", &overview.mins[0], &overview.mins[1]);
            else if (!strcmp(key, "mapcoordsmaxs")) sscanf(value, "%f %f", &overview.maxs[0], &overview.maxs[1]);
            else if (!strcmp(key, "cclayers")) overview.layers = atoi(value);
            else for (i = 0; i < MAX_COMMANDMAP_LAYERS; ++i)
                if (!strcmp(key, va("cclayerceil%d", i))) overview.ceilings[i] = atoi(value);
        }
    }
    overview.layers = overview.layers < 0 ? 0 : overview.layers > MAX_COMMANDMAP_LAYERS ? MAX_COMMANDMAP_LAYERS : overview.layers;
    overview.valid = isfinite(overview.mins[0]) && isfinite(overview.mins[1]) &&
        isfinite(overview.maxs[0]) && isfinite(overview.maxs[1]) &&
        overview.maxs[0] > overview.mins[0] && overview.mins[1] > overview.maxs[1] &&
        isfinite(overview.maxs[0] - overview.mins[0]) && isfinite(overview.mins[1] - overview.maxs[1]);
}
static qboolean ReadNumber(int *offset, int *out) {
    const char *s;
    char *end;
    long value;
    if (*offset >= Cmd_Argc()) return qfalse;
    s = Cmd_Argv((*offset)++); errno = 0; value = strtol(s, &end, 10);
    if (!*s || *end || errno || value < INT_MIN || value > INT_MAX) return qfalse;
    *out = (int)value; return qtrue;
}
/* entnfo is the native command-map feed. Stock and Legacy use slightly different records. */
void CL_FullMapCommand(void) {
    int axis, allies, i, offset = 1, legacy;
    if (CL_SplitPlayer() || (CL_OnlinePane() && CL_OnlinePane() != 1) ||
        Q_stricmp(Cmd_Argv(0), "entnfo")) return;
    InitMap(); overview.count = 0;
    if (!ReadNumber(&offset, &axis) || !ReadNumber(&offset, &allies) || axis < 0 || allies < 0 ||
        axis > MAX_GENTITIES || allies > MAX_GENTITIES - axis) return;
    legacy = !Q_stricmp(Cvar_VariableString("fs_game"), "legacy");
    for (i = 0; i < axis + allies; ++i) {
        int type, data, x = 0, y = 0, z = 0, yaw = 0;
        qboolean position, heading;
        if (!ReadNumber(&offset, &type) || type < 0 || type > ME_COMMANDMAP_MARKER + legacy) return;
        if (legacy) type = type == 3 ? ME_PLAYER : type > 3 ? type - 1 : type;
        position = legacy || !(type == ME_CONSTRUCT || type == ME_DESTRUCT || type == ME_DESTRUCT_2 || type == ME_COMMANDMAP_MARKER);
        heading = type <= ME_PLAYER_DISGUISED || type == ME_LANDMINE;
        if (position && (!ReadNumber(&offset, &x) || !ReadNumber(&offset, &y) ||
            (overview.layers && !ReadNumber(&offset, &z)))) return;
        if (heading && !ReadNumber(&offset, &yaw)) return;
        if (!ReadNumber(&offset, &data)) return;
        overview.entities[i].type = type; overview.entities[i].data = data; overview.entities[i].yaw = yaw;
        overview.entities[i].team = i < axis ? TEAM_AXIS : TEAM_ALLIES;
        VectorSet(overview.entities[i].origin, x * 128.f, y * 128.f, z * 128.f);
    }
    if (offset != Cmd_Argc()) return; /* Unknown mod format: never draw misparsed intelligence. */
    overview.count = axis + allies; overview.time = cls.realtime;
}
static int MapLayer(float z) {
    int i;
    for (i = 0; i < overview.layers; ++i) if (z <= overview.ceilings[i]) return i;
    return 0;
}
static qboolean Point(float x, float y, float size, const vec3_t origin, float *px, float *py) {
    float u = (origin[0] - overview.mins[0]) / (overview.maxs[0] - overview.mins[0]);
    float v = (origin[1] - overview.mins[1]) / (overview.maxs[1] - overview.mins[1]);
    if (!isfinite(u) || !isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1) return qfalse;
    *px = x + u * size; *py = y + v * size; return qtrue;
}
static const char *Config(int index) {
    return index >= 0 && index < MAX_CONFIGSTRINGS ? cl.gameState.stringData + cl.gameState.stringOffsets[index] : "";
}
/* ponytail: bounded snapshot scans; index by entity number only if profiling shows a hotspot. */
static const entityState_t *SnapshotEntity(int number) {
    int i;
    for (i = 0; i < cl.snap.numEntities; ++i) {
        const entityState_t *ent = &cl.parseEntities[(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
        if (ent->number == number) return ent;
    }
    return NULL;
}
static void Pic(float x, float y, float w, float h, const char *shader) {
    if (*shader) re.DrawStretchPic(x, y, w, h, 0, 0, 1, 1, RE_RegisterMapShader(shader));
}
/* Same courbd font, grid spacing and colours as CG_DrawGrid in the limbo map. */
static void MapText(float x, float y, const char *text, float scale, float *color, qboolean centered) {
    const unsigned char *s;
    float width = 0;
    if (overview.font.glyphScale <= 0) return;
    scale *= overview.font.glyphScale;
    for (s = (const unsigned char *)text; *s; ++s) width += overview.font.glyphs[*s].xSkip * scale;
    if (centered) x -= width * .5f;
    re.SetColor(color);
    for (s = (const unsigned char *)text; *s; ++s) {
        const glyphInfo_t *g = &overview.font.glyphs[*s];
        if (color == colorWhite) { /* Stock spawn text uses ITEM_TEXTSTYLE_SHADOWED. */
            float shadow = scale / (.2f * overview.font.glyphScale);
            re.SetColor(colorBlack);
            re.DrawStretchPic(x + g->pitch * scale + shadow, y - g->top * scale + shadow, g->imageWidth * scale, g->imageHeight * scale,
                              g->s, g->t, g->s2, g->t2, g->glyph);
            re.SetColor(color);
        }
        re.DrawStretchPic(x + g->pitch * scale, y - g->top * scale, g->imageWidth * scale, g->imageHeight * scale,
                         g->s, g->t, g->s2, g->t2, g->glyph);
        x += g->xSkip * scale;
    }
    re.SetColor(NULL);
}
static void Grid(float x, float y, float size) {
    int axis;
    float start[2], step[2], scale = size / 352.f;
    for (axis = 0; axis < 2; ++axis) {
        float span = fabsf(overview.maxs[axis] - overview.mins[axis]);
        float worldStep = fminf(1200.f, floorf(span / 350.f) * 50.f);
        if (worldStep <= 0) worldStep = span / 7.f;
        worldStep = fmaxf(worldStep, span / 64.f); /* Bound work for oversized/untrusted map coordinates. */
        step[axis] = size * worldStep / span;
        start[axis] = .5f * (span / worldStep - floorf(span / worldStep)) * step[axis];
    }
    for (axis = 0; axis < 2; ++axis) {
        int n = -1;
        float p;
        for (p = start[axis]; p < size; p += step[axis], ++n) {
            char text[16];
            re.SetColor(clrBrownLineFull);
            re.DrawStretchPic(x + (axis ? 0 : p), y + (axis ? p : 0), axis ? size : 1, axis ? 1 : size,
                              0, 0, 0, 1, cls.whiteShader);
            if (n < 0) continue;
            if (axis) Com_sprintf(text, sizeof(text), "%d", n);
            else Com_sprintf(text, sizeof(text), "%c", 'A' + n);
            MapText(axis ? x + start[0] + 2 * scale : x + p - step[0] * .5f,
                    axis ? y + p - step[1] * .5f + 3 * scale : y + start[1] + 9 * scale,
                    text, .2f * scale, colorBlack, !axis);
        }
    }
    re.SetColor(NULL);
}
static void DrawPlayer(int type, int data, const vec3_t origin, float yaw, float x, float y, float size) {
    const char *info;
    const entityState_t *ent;
    bg_playerclass_t *classInfo;
    int team, clsnum;
    float px, py, scale = size / 352.f, diameter = 12 * scale;
    if (data < 0 || data >= MAX_CLIENTS || !Point(x, y, size, origin, &px, &py)) return;
    info = Config(CS_PLAYERS + data);
    if (!*info) return;
    team = atoi(Info_ValueForKey(info, "t")); clsnum = atoi(Info_ValueForKey(info, "c"));
    if (team != TEAM_AXIS && team != TEAM_ALLIES) return;
    ent = SnapshotEntity(data);
    if (type == ME_PLAYER_DISGUISED && (!ent || !(ent->powerups & (1 << PW_OPS_DISGUISED)))) return;
    if (ent && (ent->powerups & (1 << PW_OPS_DISGUISED))) {
        team = team == TEAM_AXIS ? TEAM_ALLIES : TEAM_AXIS;
        clsnum = (ent->powerups >> PW_OPS_CLASS_1) & 7;
    }
    if (type == ME_PLAYER_REVIVE) {
        if (!ent || atoi(Info_ValueForKey(info, "t")) != cl.snap.ps.persistant[PERS_TEAM]) return;
        {
            int period = atoi(Info_ValueForKey(Config(CS_SERVERINFO), cl.snap.ps.persistant[PERS_TEAM] == TEAM_AXIS ? "g_redlimbotime" : "g_bluelimbotime"));
            float fraction = period > 0 ? (period - (cl.serverTime % period)) / (float)period : 0;
            vec4_t pulse = {1, 1, 1, .5f + .25f * (sinf(sqrtf(fraction) * 50 * M_PI) + 1)};
            re.SetColor(pulse);
            Pic(px - 3 * scale, py - 3 * scale, 9 * scale, 9 * scale, "sprites/voiceMedic");
            re.SetColor(NULL);
        }
        return;
    }
    if (data == cl.snap.ps.clientNum || type == ME_PLAYER_DISGUISED) {
        re.SetColor(data == cl.snap.ps.clientNum ? colorYellow : colorOrange);
        Pic(px - diameter * .5f, py - diameter * .5f, diameter, diameter, "ui/assets/mp_player_highlight.tga");
        re.SetColor(NULL);
    }
    classInfo = BG_GetPlayerClassInfo(team, clsnum);
    Pic(px - diameter * .5f, py - diameter * .5f, diameter, diameter, classInfo->iconName);
    re.DrawRotatedPic(px - 7 * scale, py - 7 * scale, 14 * scale, 14 * scale, 0, 0, 1, 1,
                     RE_RegisterMapShader(classInfo->iconArrow), .5f - (yaw - 180.f) / 360.f);
}
static void DrawEntity(int i, float x, float y, float size, int layer, int team, qboolean legacy) {
    int type = overview.entities[i].type, data = overview.entities[i].data, oid = -1;
    const entityState_t *ent = SnapshotEntity(data);
    const char *info = "", *shader = "";
    float px, py, width = 32 * size / 352.f, height = width, yaw = overview.entities[i].yaw;
    vec3_t origin;
    if (overview.entities[i].team != team) return; /* Same intelligence/team selection as the limbo map. */
    if ((type <= ME_PLAYER_DISGUISED && (data < 0 || data >= MAX_CLIENTS)) ||
        ((type == ME_CONSTRUCT || type == ME_DESTRUCT || type == ME_DESTRUCT_2) && (data < 0 || data >= MAX_GENTITIES))) return;
    VectorCopy(overview.entities[i].origin, origin);
    if (type <= ME_PLAYER_DISGUISED) {
        if (data == cl.snap.ps.clientNum) { VectorCopy(cl.snap.ps.origin, origin); yaw = cl.snap.ps.viewangles[YAW]; }
        else if (ent && atoi(Info_ValueForKey(Config(CS_PLAYERS + data), "t")) == team) {
            VectorCopy(ent->pos.trBase, origin); yaw = ent->apos.trBase[YAW];
        }
    } else if (type == ME_CONSTRUCT || type == ME_DESTRUCT || type == ME_DESTRUCT_2) {
        if (!ent && data >= 0 && data < MAX_GENTITIES && overview.seen[data]) ent = &overview.known[data];
        if (ent) { VectorCopy(ent->pos.trBase, origin); oid = ent->modelindex2; }
        else if (!legacy) return;
    } else if (type == ME_TANK || type == ME_TANK_DEAD || type == ME_COMMANDMAP_MARKER) {
        oid = data;
        if (oid < 0 || oid >= MAX_OID_TRIGGERS) return;
        if (type == ME_COMMANDMAP_MARKER) {
            info = Config(CS_OID_DATA + oid);
            VectorSet(origin, atof(Info_ValueForKey(info, "x")), atof(Info_ValueForKey(info, "y")), atof(Info_ValueForKey(info, "z")));
        } else {
            int j;
            for (j = 0; j < cl.snap.numEntities; ++j) {
                const entityState_t *trigger = &cl.parseEntities[(cl.snap.parseEntitiesNum + j) & (MAX_PARSE_ENTITIES - 1)];
                if (trigger->eType == ET_OID_TRIGGER && trigger->teamNum == oid) { VectorCopy(trigger->pos.trBase, origin); break; }
            }
        }
    }
    if ((overview.layers && MapLayer(origin[2]) != layer) || !Point(x, y, size, origin, &px, &py)) return;
    if (type <= ME_PLAYER_DISGUISED) { DrawPlayer(type, data, origin, yaw, x, y, size); return; }
    if (oid >= 0 && oid < MAX_OID_TRIGGERS) info = Config(CS_OID_DATA + oid);
    switch (type) {
    case ME_CONSTRUCT: shader = team == TEAM_AXIS ? "gfx/limbo/cm_constaxis" : "gfx/limbo/cm_constallied"; break;
    case ME_DESTRUCT:
        if (!ent || ent->effect1Time < 0 || ent->effect1Time > 2) return;
        shader = ent->effect1Time == 2 ? "gfx/limbo/cm_dynamite" : ent->effect1Time == 1 ? "gfx/limbo/cm_satchel" :
            team == TEAM_AXIS ? "gfx/limbo/cm_axisgren" : "gfx/limbo/cm_alliedgren";
        break;
    case ME_DESTRUCT_2:
        if (atoi(Info_ValueForKey(info, "s")) & (1 << 6)) shader = team == TEAM_AXIS ? "gfx/limbo/cm_bo_axis" : "gfx/limbo/cm_bo_allied";
        break;
    case ME_TANK: case ME_TANK_DEAD: shader = "gfx/limbo/cm_churchill"; height *= .5f; break;
    case ME_LANDMINE: shader = data == TEAM_AXIS ? "sprites/landmine_axis" : "sprites/landmine_allied"; width = height = 12 * size / 352.f; break;
    }
    if (oid >= 0 && oid < MAX_OID_TRIGGERS) {
        int custom = atoi(Info_ValueForKey(info, team == TEAM_AXIS ? "cix" : "cia"));
        if (custom > 0 && custom < MAX_CS_SHADERS && *Config(CS_SHADERS + custom)) shader = Config(CS_SHADERS + custom);
    }
    if (type == ME_TANK_DEAD) re.SetColor(colorRed);
    Pic(px - width * .5f, py - height * .5f, width, height, shader);
    re.SetColor(NULL);
}
void CL_FullMapDraw(int x, int y, int w, int h) {
    int i, pass, layer = 0;
    /* Limbo uses clientinfo's real team; PERS_TEAM can lag a team switch until respawn. */
    int team = cl.snap.ps.clientNum >= 0 && cl.snap.ps.clientNum < MAX_CLIENTS ?
        atoi(Info_ValueForKey(Config(CS_PLAYERS + cl.snap.ps.clientNum), "t")) : TEAM_SPECTATOR;
    qboolean legacy = !Q_stricmp(Cvar_VariableString("fs_game"), "legacy");
    float size = fminf(w - 24.f, h - 48.f), mx = x + (w - size) * .5f, my = y + 32;
    char map[MAX_QPATH], shader[MAX_QPATH];
    InitMap();
    re.SetColor(colorBlack); re.DrawStretchPic(x, y, w, h, 0, 0, 0, 0, cls.whiteShader); re.SetColor(NULL);
    SCR_DrawSmallStringExt(x + 8, y + 8, "LIVE PLAYFIELD MAP - PLAYER 1 INTEL", colorWhite, qtrue);
    if (!overview.valid || size <= 0 || !cl.snap.valid) {
        SCR_DrawSmallStringExt(x + 8, y + 32, "Waiting for map data", colorWhite, qtrue); return;
    }
    if (overview.layers) layer = MapLayer(cl.snap.ps.origin[2]);
    if (CL_SplitMapDraw((int)mx, (int)my, (int)size)) {
        /* Local partners remain visible even on opposite teams; never used online. */
        for (i = 1; i < CL_SplitGamepadCount(); ++i) {
            playerState_t state;
            if (CL_LocalPlayerState(i, &state) && state.persistant[PERS_TEAM] != team &&
                (!overview.layers || MapLayer(state.origin[2]) == layer))
                DrawPlayer(ME_PLAYER, state.clientNum, state.origin, state.viewangles[YAW], mx, my, size);
        }
        return;
    }
    Q_strncpyz(map, Info_ValueForKey(Config(CS_SERVERINFO), "mapname"), sizeof(map));
    if (!*map || strspn(map, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(map)) return;
    if (team != TEAM_AXIS && team != TEAM_ALLIES) team = TEAM_AXIS; /* Stock limbo's default spectator selection. */
    Com_sprintf(shader, sizeof(shader), overview.layers ? "levelshots/%s_%d_cc_trans" : "levelshots/%s_cc_trans", map, layer);
    Pic(mx, my, size, size, shader);
    if (!overview.font.glyphScale) re.RegisterFont("courbd", 30, &overview.font);
    Grid(mx, my, size);
    { vec4_t border = {0, 0, 0, .75f}; re.SetColor(border); Pic(mx, my, size, size, "gfx/limbo/cc_blend"); re.SetColor(NULL); }
    /* Remember only states actually supplied by this connection, as cgame does outside PVS. */
    for (i = 0; i < cl.snap.numEntities; ++i) {
        const entityState_t *ent = &cl.parseEntities[(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
        if (ent->number >= 0 && ent->number < MAX_GENTITIES) { overview.known[ent->number] = *ent; overview.seen[ent->number] = qtrue; }
    }
    for (pass = 0; pass < 2; ++pass) {
        /* Objectives first, then spawn flags, then players (the limbo map's draw order). */
        for (i = 0; cls.realtime - overview.time < 5000 && i < overview.count; ++i)
            if ((overview.entities[i].type <= ME_PLAYER_DISGUISED) == pass) DrawEntity(i, mx, my, size, layer, team, legacy);
        if (pass) break;
        for (i = 0; i < MAX_MULTI_SPAWNTARGETS; ++i) {
            const char *info = Config(CS_MULTI_SPAWNTARGETS + i);
            vec3_t origin;
            float px, py, flag = 32 * size / 352.f;
            int owner = atoi(Info_ValueForKey(info, "t"));
            if (!*Info_ValueForKey(info, legacy ? "s" : "spawn_targ") || owner != team) continue;
            VectorSet(origin, atof(Info_ValueForKey(info, "x")), atof(Info_ValueForKey(info, "y")), atof(Info_ValueForKey(info, "z")));
            if ((overview.layers && MapLayer(origin[2]) != layer) || !Point(mx, my, size, origin, &px, &py)) continue;
            Pic(px - flag * .5f, py - flag * .5f, flag, flag, owner == TEAM_AXIS ? "gfx/limbo/cm_flagaxis" : "gfx/limbo/cm_flagallied");
            MapText(px + flag * .25f - flag * 39 / 128.f, py + flag * 31 / 128.f,
                    va("(Troops: %d)", atoi(Info_ValueForKey(info, "c"))), .2f * size / 352.f, colorWhite, qfalse);
        }
    }
    re.SetColor(NULL);
}
