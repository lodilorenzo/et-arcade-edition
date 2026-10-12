/* Actual overview parser/projection: stock, Legacy, malformed input, layers and reset. */
#undef NDEBUG
#include <assert.h>
#include "../native/full_map.c"

clientActive_t cl;
clientStatic_t cls;
refexport_t re;
static int role, argc;
static char input[2048], *argv[128];
static char shaders[64][MAX_QPATH];
static int shaderCount, draws[64], localMap;
static float arrowAngle, lastColor[4];
static qboolean redTank;
static const char *mod = "etmain";
static char world[] = "{ \"classname\" \"worldspawn\" \"mapcoordsmins\" \"-1000 2000\" \"mapcoordsmaxs\" \"3000 -2000\" \"cclayers\" \"2\" \"cclayerceil0\" \"100\" \"cclayerceil1\" \"2000\" }";
char *CM_EntityString(void) { return world; }
int CL_SplitPlayer(void) { return role; }
int CL_SplitGamepadCount(void) { return 3; }
qboolean CL_SplitEnabled(void) { return qtrue; }
qboolean CL_SplitMapDraw(int x, int y, int size) { assert(size > 0); return localMap; }
static playerState_t partner;
qboolean CL_LocalPlayerState(int local, playerState_t *state) { if (localMap && local == 2) { *state = partner; return qtrue; } return qfalse; }
void SCR_DrawSmallStringExt(int x, int y, const char *text, float *color, qboolean force) {}
int Q_vsnprintf(char *dest, int size, const char *format, va_list args) { return vsprintf_s(dest, size, format, args); }
int CL_OnlinePane(void) { return 0; }
int Cmd_Argc(void) { return argc; }
char *Cmd_Argv(int i) { return i < argc ? argv[i] : ""; }
char *Cvar_VariableString(const char *name) { return (char *)mod; }
void QDECL Com_Error(int code, const char *format, ...) { assert(0); }
void QDECL Com_Printf(const char *format, ...) {}
static void Feed(const char *text) {
    char *token;
    strcpy(input, text); argc = 0;
    for (token = strtok(input, " "); token; token = strtok(NULL, " ")) argv[argc++] = token;
    CL_FullMapCommand();
}
static void ConfigSet(int index, const char *text) {
    if (!cl.gameState.dataCount) cl.gameState.dataCount = 1;
    cl.gameState.stringOffsets[index] = cl.gameState.dataCount;
    strcpy(cl.gameState.stringData + cl.gameState.dataCount, text);
    cl.gameState.dataCount += strlen(text) + 1;
}
static qhandle_t Register(const char *name) {
    int i;
    for (i = 1; i <= shaderCount; ++i) if (!strcmp(shaders[i], name)) return i;
    assert(++shaderCount < 64); strcpy(shaders[shaderCount], name); return shaderCount;
}
qhandle_t RE_RegisterMapShader(const char *name) { return Register(name); }
static void Color(const float *color) { Vector4Copy(color ? color : colorWhite, lastColor); }
static void Draw(float x, float y, float w, float h, float s, float t, float s2, float t2, qhandle_t shader) {
    assert(isfinite(x) && isfinite(y) && w >= 0 && h >= 0);
    if (shader > 0 && shader < 64) {
        ++draws[shader];
        if (!strcmp(shaders[shader], "gfx/limbo/cm_churchill") && lastColor[0] == 1 && !lastColor[1]) redTank = qtrue;
    }
}
static void Rotated(float x, float y, float w, float h, float s, float t, float s2, float t2, qhandle_t shader, float angle) {
    Draw(x, y, w, h, s, t, s2, t2, shader); arrowAngle = angle;
}
static void Font(const char *name, int size, fontInfo_t *font) { assert(!strcmp(name, "courbd") && size == 30); font->glyphScale = 1; }
static int Drawn(const char *shader) { return draws[Register(shader)]; }
static void Rendering(void) {
    int i;
    role = 0; mod = "etmain"; CL_FullMapReset();
    cls.realtime = 100; cl.snap.valid = qtrue; cl.snap.ps.clientNum = 0;
    cl.snap.ps.persistant[PERS_TEAM] = TEAM_ALLIES; /* Old snapshot team; real clientinfo has switched to Axis. */
    ConfigSet(CS_SERVERINFO, "\\mapname\\oasis");
    ConfigSet(CS_PLAYERS, "\\t\\1\\c\\2");
    ConfigSet(CS_PLAYERS + 1, "\\t\\1\\c\\1");
    ConfigSet(CS_PLAYERS + 2, "\\t\\2\\c\\0");
    ConfigSet(CS_OID_DATA, "\\cix\\1");
    ConfigSet(CS_OID_DATA + 4, "\\s\\64");
    ConfigSet(CS_OID_DATA + 5, "\\cix\\2\\x\\500\\y\\500\\z\\0");
    ConfigSet(CS_SHADERS + 1, "gfx/test/custom_objective");
    ConfigSet(CS_SHADERS + 2, "gfx/test/marker_icon");
    ConfigSet(CS_MULTI_SPAWNTARGETS, "\\spawn_targ\\Axis spawn\\t\\1\\x\\0\\y\\0\\c\\3");
    ConfigSet(CS_MULTI_SPAWNTARGETS + 1, "\\spawn_targ\\Hidden spawn\\t\\2\\x\\0\\y\\0");
    cl.snap.numEntities = 4;
    for (i = 0; i < 3; ++i) {
        entityState_t *ent = &cl.parseEntities[i];
        ent->number = 20 + i; ent->modelindex2 = i == 2 ? 4 : i;
        ent->effect1Time = 2; VectorSet(ent->pos.trBase, i * 500, 0, 0);
    }
    cl.parseEntities[3].number = 1; VectorSet(cl.parseEntities[3].pos.trBase, 1000, 1000, 0);
    cl.parseEntities[3].apos.trBase[YAW] = 30;
    Feed("entnfo 9 1 0 1 2 0 90 0 0 2 2 0 90 1 3 20 4 21 5 22 7 3 4 0 2 8 3 4 0 3 6 4 0 0 90 1 9 5 0 3 4 0 90 2");
    assert(overview.count == 10 && overview.entities[0].yaw == 90);
    re.RegisterShaderNoMip = Register; re.SetColor = Color; re.DrawStretchPic = Draw;
    re.DrawRotatedPic = Rotated; re.RegisterFont = Font;
    CL_FullMapDraw(640, 360, 640, 360);
    assert(Drawn("levelshots/oasis_0_cc_trans") && Drawn("gfx/limbo/cc_blend"));
    assert(Drawn("ui/assets/mp_wrench_red.tga") && Drawn("ui/assets/mp_health_red.tga"));
    assert(!Drawn("ui/assets/mp_gun_blue.tga") && fabsf(arrowAngle - (1.f - 30.f / 360.f)) < .001f);
    assert(Drawn("gfx/test/custom_objective") && Drawn("gfx/test/marker_icon"));
    assert(Drawn("gfx/limbo/cm_dynamite") && Drawn("gfx/limbo/cm_bo_axis"));
    assert(Drawn("gfx/limbo/cm_churchill") == 2 && redTank && Drawn("sprites/landmine_axis"));
    assert(Drawn("gfx/limbo/cm_flagaxis") && !Drawn("gfx/limbo/cm_flagallied"));
    memset(draws, 0, sizeof(draws)); cl.snap.ps.persistant[PERS_TEAM] = TEAM_AXIS;
    overview.entities[1].type = ME_PLAYER_REVIVE;
    CL_FullMapDraw(640, 360, 640, 360);
    assert(Drawn("sprites/voiceMedic") && !Drawn("ui/assets/mp_health_red.tga"));
    memset(draws, 0, sizeof(draws)); cl.snap.numEntities = 0; mod = "legacy";
    ConfigSet(CS_MULTI_SPAWNTARGETS, "\\s\\Axis spawn\\t\\1\\x\\0\\y\\0\\c\\3");
    CL_FullMapDraw(640, 360, 640, 360);
    assert(Drawn("gfx/limbo/cm_flagaxis") && Drawn("gfx/test/custom_objective") && !Drawn("sprites/voiceMedic"));
    memset(draws, 0, sizeof(draws)); cls.realtime += 5001;
    CL_FullMapDraw(640, 360, 640, 360); assert(!Drawn("ui/assets/mp_wrench_red.tga") && !Drawn("gfx/test/custom_objective"));
    /* Local matches use the real limbo renderer, never the online fallback. */
    memset(draws, 0, sizeof(draws)); localMap = 1;
    partner.clientNum = 2; partner.persistant[PERS_TEAM] = TEAM_ALLIES;
    CL_FullMapDraw(640, 360, 640, 360);
    assert(!Drawn("levelshots/oasis_0_cc_trans") && Drawn("ui/assets/mp_gun_blue.tga"));
}
int main(void) {
    float x, y;
    vec3_t origin = {1000, 0, 0};
    CL_FullMapReset(); InitMap(); assert(overview.valid && overview.layers == 2);
    assert(Point(20, 30, 400, origin, &x, &y) && x == 220 && y == 230);
    origin[0] = NAN; assert(!Point(0, 0, 400, origin, &x, &y));
    assert(MapLayer(50) == 0 && MapLayer(500) == 1 && MapLayer(9999) == 0);
    Feed("entnfo 1 1 0 1 2 0 90 3 3 20"); /* Stock player with layer Z; construct has only entity id. */
    assert(overview.count == 2 && overview.entities[0].origin[0] == 128 && overview.entities[1].data == 20);
    assert(overview.entities[0].team == TEAM_AXIS && overview.entities[1].team == TEAM_ALLIES);
    Feed("entnfo -1 0"); assert(!overview.count);
    Feed("entnfo 2147483647 1"); assert(!overview.count);
    Feed("entnfo 1 0 0 999999999999 0 0 0 0"); assert(!overview.count);
    Feed("entnfo 1 0 0 1 2"); assert(!overview.count);
    Feed("entnfo 1 0 99 1 2 0 0 0"); assert(!overview.count);
    Feed("entnfo 1 0 0 1 2 0 90 3 garbage"); assert(!overview.count);
    mod = "legacy";
    Feed("entnfo 1 1 3 1 2 0 90 3 4 2 3 0 20");
    assert(overview.count == 2 && overview.entities[0].type == ME_PLAYER && overview.entities[1].type == ME_CONSTRUCT);
    assert(overview.entities[1].origin[1] == 384);
    Feed("entnfo 0 10 0 5 17 90 2 0 5 17 0 1 10 32 58 10 4 18 36 313 5 75 30 306 5 75 36 304 4 26 56 302 4 35 51 300 4 25 24 298 5 40 57 295");
    assert(!overview.count); /* Z is mandatory on this layered test map. */
    overview.layers = 0;
    Feed("entnfo 0 10 0 5 17 90 2 0 5 17 0 1 10 32 58 10 4 18 36 313 5 75 30 306 5 75 36 304 4 26 56 302 4 35 51 300 4 25 24 298 5 40 57 295");
    assert(overview.count == 10 && overview.entities[2].type == ME_COMMANDMAP_MARKER);
    Feed("entnfo 1 1 3 1 2 90 3 4 2 3 20");
    assert(overview.count == 2);
    role = 1; Feed("entnfo 0 0"); assert(overview.count == 2); /* Other views don't overwrite player one's intel. */
    CL_FullMapReset(); assert(!overview.count && !overview.initialized);
    assert(ET_LOCAL_PLAYER_COUNT(0) == 1 && ET_LOCAL_PLAYER_COUNT(1) == 2 && ET_LOCAL_PLAYER_COUNT(2) == 2 &&
           ET_LOCAL_PLAYER_COUNT(3) == 3 && ET_LOCAL_PLAYER_COUNT(4) == 4);
    Rendering();
    puts("PASS limbo map: stock/Legacy feeds, layers, bounds, objective/custom/class/heading/spawn icons, team visibility, expiry and native-renderer routing");
    return 0;
}
