// GPL-3.0-or-later with additional terms: engine/COPYING.txt.
// Replay the actual shared movement, animation-condition and collision code.
#ifdef CGAMEDLL
#include "../engine/src/cgame/cg_local.h"
cg_t cg;
vmCvar_t cg_gameType, cg_movespeed;
#else
#include "../engine/src/game/q_shared.h"
#include "../engine/src/game/bg_public.h"
vmCvar_t g_gametype, g_movespeed, g_developer;
#endif
#include "../engine/src/game/bg_local.h"
#include "../engine/src/qcommon/qcommon.h"
#include <stdint.h>
#include <stddef.h>
#include <io.h>
#include <fcntl.h>

#define FRAMES 128
static const char *cases[] = {"idle", "walk", "strafe", "jump", "crouch", "wall", "stairs", "ramp", "water", "fall", "spectator", "noclip", "freeze", "dead", "stuck", "timing"};
static const int modes[] = {0, 8, 16};

// Test-only engine services; movement/trace/snap/animation logic is upstream.
void QDECL Com_Error(int level, const char *fmt, ...) {
    va_list args; (void)level;
    va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
    fputc('\n', stderr); exit(1);
}
static void check(int condition, const char *message) {
    if (!condition) Com_Error(ERR_FATAL, "movement replay: %s", message);
}
void QDECL Com_Printf(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
}
void QDECL Com_DPrintf(const char *fmt, ...) { (void)fmt; }
// Unused VM/debug services must fail loudly if a replay starts depending on them.
int trap_PC_LoadSource(const char *path) { Com_Error(ERR_FATAL, "unexpected PC source: %s", path); return 0; }
int trap_PC_FreeSource(int handle) { Com_Error(ERR_FATAL, "unexpected PC free: %d", handle); return 0; }
int trap_PC_ReadToken(int handle, pc_token_t *token) { (void)token; Com_Error(ERR_FATAL, "unexpected PC read: %d", handle); return 0; }
int trap_PC_SourceFileAndLine(int handle, char *filename, int *line) { (void)filename; (void)line; Com_Error(ERR_FATAL, "unexpected PC lookup: %d", handle); return 0; }
void trap_Cvar_Set(const char *name, const char *value) { (void)value; Com_Error(ERR_FATAL, "unexpected cvar set: %s", name); }
void BotDrawDebugPolygons(void (*draw)(int, int, float *), int value) { (void)draw; (void)value; Com_Error(ERR_FATAL, "unexpected debug drawing"); }
const char *String_Alloc(const char *text) { (void)text; Com_Error(ERR_FATAL, "unexpected UI string allocation"); return NULL; }
void Com_Memset(void *dest, int value, size_t size) { memset(dest, value, size); }
void Com_Memcpy(void *dest, const void *src, size_t size) { memcpy(dest, src, size); }
void *Z_Malloc(int size) {
    void *block;
    check(size >= 0, "negative allocation");
    block = calloc(1, size ? size : 1); check(block != NULL, "allocation failed"); return block;
}
void Z_Free(void *block) { free(block); }
static void **hunks;
static size_t num_hunks, capacity_hunks;
void *Hunk_Alloc(int size, ha_pref preference) {
    void *block; (void)preference;
    if (num_hunks == capacity_hunks) {
        size_t capacity = capacity_hunks ? capacity_hunks * 2 : 64;
        void **grown = realloc(hunks, capacity * sizeof(*hunks));
        check(grown != NULL, "allocation registry failed"); hunks = grown; capacity_hunks = capacity;
    }
    block = Z_Malloc(size); hunks[num_hunks++] = block; return block;
}
void Hunk_ClearHigh(void) {
    while (num_hunks) free(hunks[--num_hunks]);
}
cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    static cvar_t vars[16]; static int count;
    int i;
    for (i = 0; i < count; ++i) if (!strcmp(vars[i].name, name)) return &vars[i];
    check(count < 16, "too many default cvars");
    vars[count].name = (char *)name; vars[count].string = (char *)value;
    vars[count].integer = atoi(value); vars[count].value = (float)atof(value);
    vars[count].flags = flags; return &vars[count++];
}
void trap_SnapVector(float *vector) { Sys_SnapVector(vector); }
#ifdef GAMEDLL
void ClientStoreSurfaceFlags(int client, int flags) { (void)client; (void)flags; }
#endif

// Small valid BSPs exercise real CM capsule/brush collision, not a replacement solver.
static union { dheader_t alignment; byte bytes[8192]; } fixture;
static int fixture_size;
int FS_ReadFile(const char *path, void **buffer) {
    FILE *file; long size; dheader_t *header; int i;
    if (!strcmp(path, "__movement_fixture__")) {
        *buffer = Z_Malloc(fixture_size); memcpy(*buffer, fixture.bytes, fixture_size); return fixture_size;
    }
    file = fopen(path, "rb"); check(file != NULL, "cannot open BSP");
    check(fseek(file, 0, SEEK_END) == 0, "cannot seek BSP"); size = ftell(file);
    check(size >= sizeof(dheader_t) && size <= 128 * 1024 * 1024, "invalid BSP size");
    rewind(file); *buffer = Z_Malloc((int)size + 1);
    check(fread(*buffer, 1, size, file) == (size_t)size, "cannot read BSP"); fclose(file);
    header = *buffer;
    check(header->ident == BSP_IDENT && header->version == BSP_VERSION, "invalid BSP header");
    for (i = 0; i < HEADER_LUMPS; ++i) {
        lump_t lump = header->lumps[i];
        check(lump.fileofs >= 0 && lump.filelen >= 0 && lump.fileofs <= size && lump.filelen <= size - lump.fileofs, "BSP lump outside file");
    }
    return (int)size;
}
void FS_FreeFile(void *buffer) { free(buffer); }
static dplane_t planes[128];
static dbrushside_t sides[64];
static dbrush_t brushes[8];
static int num_planes, num_sides, num_brushes;
static void plane_side(const vec3_t normal, float distance, int shader) {
    int j;
    check(num_planes + 2 <= 128 && num_sides < 64, "fixture capacity exceeded");
    sides[num_sides].planeNum = num_planes; sides[num_sides++].shaderNum = shader;
    for (j = 0; j < 3; ++j) {
        planes[num_planes].normal[j] = normal[j]; planes[num_planes + 1].normal[j] = -normal[j];
    }
    planes[num_planes].dist = distance; planes[num_planes + 1].dist = -distance; num_planes += 2;
}
static void box(float x0, float y0, float z0, float x1, float y1, float z1, int shader) {
    vec3_t low = {x0, y0, z0}, high = {x1, y1, z1}; int axis;
    check(num_brushes < 8, "too many fixture brushes");
    brushes[num_brushes].firstSide = num_sides; brushes[num_brushes].numSides = 6;
    brushes[num_brushes++].shaderNum = shader;
    for (axis = 0; axis < 3; ++axis) {
        vec3_t normal = {0, 0, 0}; normal[axis] = -1; plane_side(normal, -low[axis], shader);
        normal[axis] = 1; plane_side(normal, high[axis], shader);
    }
}
static void lump(dheader_t *header, int index, const void *data, size_t size) {
    fixture_size = (fixture_size + 3) & ~3;
    check(size <= sizeof(fixture.bytes) - fixture_size, "fixture too large");
    header->lumps[index].fileofs = fixture_size; header->lumps[index].filelen = (int)size;
    memcpy(fixture.bytes + fixture_size, data, size); fixture_size += (int)size;
}
static void build_world(int scenario) {
    dheader_t header = {0}; dnode_t node = {0}; dleaf_t leaf = {0}; dmodel_t model = {0};
    dshader_t shaders[2] = {0}; int leafbrushes[8], i;
    memset(&fixture, 0, sizeof(fixture)); memset(planes, 0, sizeof(planes));
    memset(sides, 0, sizeof(sides)); memset(brushes, 0, sizeof(brushes));
    num_planes = num_sides = num_brushes = 0; fixture_size = sizeof(header);
    box(-4096, -4096, -64, 4096, 4096, 0, 0);
    if (scenario == 5 || scenario == 10 || scenario == 11) box(128, -512, 0, 160, 512, 256, 0);
    if (scenario == 4) box(-64, -128, 50, 192, 128, 90, 0);
    if (scenario == 6) {
        box(96, -128, 0, 128, 128, 16, 0); box(128, -128, 0, 160, 128, 32, 0);
    }
    if (scenario == 7) {
        vec3_t normal = {-0.4472136f, 0, 0.8944272f};
        box(0, -128, -64, 256, 128, 128, 0); plane_side(normal, 0, 0);
        brushes[num_brushes - 1].numSides++;
    }
    if (scenario == 8) box(-64, -128, 0, 256, 128, 80, 1);
    for (i = 0; i < num_brushes; ++i) leafbrushes[i] = i;
    strcpy(shaders[0].shader, "test/solid"); shaders[0].contentFlags = CONTENTS_SOLID;
    strcpy(shaders[1].shader, "test/water"); shaders[1].contentFlags = CONTENTS_WATER;
    node.children[0] = node.children[1] = -1; leaf.numLeafBrushes = num_brushes;
    VectorSet(model.mins, -4096, -4096, -64); VectorSet(model.maxs, 4096, 4096, 4096); model.numBrushes = num_brushes;
    header.ident = BSP_IDENT; header.version = BSP_VERSION;
    lump(&header, LUMP_SHADERS, shaders, sizeof(shaders)); lump(&header, LUMP_PLANES, planes, num_planes * sizeof(*planes));
    lump(&header, LUMP_BRUSHSIDES, sides, num_sides * sizeof(*sides)); lump(&header, LUMP_BRUSHES, brushes, num_brushes * sizeof(*brushes));
    lump(&header, LUMP_LEAFBRUSHES, leafbrushes, num_brushes * sizeof(*leafbrushes));
    lump(&header, LUMP_LEAFS, &leaf, sizeof(leaf)); lump(&header, LUMP_NODES, &node, sizeof(node));
    lump(&header, LUMP_MODELS, &model, sizeof(model)); lump(&header, LUMP_ENTITIES, "{}", 3);
    memcpy(fixture.bytes, &header, sizeof(header));
}

static uint32_t query_hash, trace_calls, contents_calls, hits, startsolid;
static void hash(const void *data, size_t size) {
    const byte *bytes = data; size_t i;
    for (i = 0; i < size; ++i) query_hash = (query_hash ^ bytes[i]) * 16777619u;
}
static void trace_world(trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int ignore, int mask) {
    memset(result, 0, sizeof(*result));
    CM_BoxTrace(result, start, end, mins, maxs, 0, mask, qtrue);
    result->entityNum = result->fraction < 1 || result->startsolid ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
    ++trace_calls; hits += result->fraction < 1; startsolid += result->startsolid != 0;
    hash(start, sizeof(vec3_t)); hash(end, sizeof(vec3_t)); hash(mins, sizeof(vec3_t)); hash(maxs, sizeof(vec3_t));
    hash(&ignore, sizeof(ignore)); hash(&mask, sizeof(mask)); hash(result, sizeof(*result));
}
static int contents_world(const vec3_t point, int ignore) {
    int result = CM_PointContents(point, 0); ++contents_calls;
    hash(point, sizeof(vec3_t)); hash(&ignore, sizeof(ignore)); hash(&result, sizeof(result)); return result;
}

typedef struct {
    playerState_t ps;
    pmoveExt_t ext;
    usercmd_t cmd;
    int result, numtouch, touchents[MAXTOUCH];
    vec3_t mins, maxs;
    int waterlevel, watertype;
    float xyspeed;
    uint32_t traces, contents, hits, startsolid, query_hash;
} frame_t;
static animScriptData_t animations;
static frame_t records[FRAMES];
static usercmd_t commands[FRAMES];
static void output(const void *data, size_t size) { check(fwrite(data, 1, size, stdout) == size, "output failed"); }
static void run(pmove_t *move, frame_t *frame, int index, int scenario) {
    query_hash = 2166136261u; trace_calls = contents_calls = hits = startsolid = 0;
    move->cmd = commands[index];
    if (scenario == 15 && index == 80) move->ps->pm_flags |= PMF_TIME_LOAD;
    memset(frame, 0, sizeof(*frame)); frame->result = Pmove(move);
    frame->ps = *move->ps; frame->ext = *move->pmext; frame->cmd = move->cmd;
    frame->numtouch = move->numtouch; memcpy(frame->touchents, move->touchents, sizeof(frame->touchents));
    VectorCopy(move->mins, frame->mins); VectorCopy(move->maxs, frame->maxs);
    frame->waterlevel = move->waterlevel; frame->watertype = move->watertype; frame->xyspeed = move->xyspeed;
    frame->traces = trace_calls; frame->contents = contents_calls; frame->hits = hits;
    frame->startsolid = startsolid; frame->query_hash = query_hash;
    move->oldcmd = commands[index];
}
static int spawn_origin(vec3_t origin) {
    char *text = CM_EntityString(), key[MAX_TOKEN_CHARS], *token;
    int spawn = 0, has_origin = 0;
    COM_BeginParseSession("BSP spawn");
    while ((token = COM_Parse(&text))[0]) {
        if (!strcmp(token, "{")) { spawn = has_origin = 0; continue; }
        if (!strcmp(token, "}")) { if (spawn && has_origin) return 1; continue; }
        Q_strncpyz(key, token, sizeof(key)); token = COM_Parse(&text);
        if (!strcmp(key, "classname")) spawn = !strcmp(token, "team_CTF_redspawn") || !strcmp(token, "team_CTF_bluespawn") || !strcmp(token, "info_player_deathmatch");
        if (!strcmp(key, "origin")) has_origin = sscanf(token, "%f %f %f", &origin[0], &origin[1], &origin[2]) == 3;
    }
    return 0;
}
static void replay(int scenario, int fixed, const char *map) {
    playerState_t ps = {0}, checkpoint_ps;
    pmoveExt_t ext = {0}, checkpoint_ext;
    pmove_t move = {0}, checkpoint_move;
    pml_t checkpoint_pml;
    byte checkpoint_conditions[sizeof(animations.clientConditions[0])];
    int skill[SK_NUM_SKILLS] = {0}, checksum, i, j, time = 1000;
    float max_height = -FLT_MAX, max_x = -FLT_MAX;
    unsigned collisions = 0, solid = 0; int ducked = 0, submerged = 0;
    char empty_script[] = "";
    static int stock_loaded;
    if (!map || !stock_loaded) {
        CM_ClearMap(); Hunk_ClearHigh();
        if (!map) build_world(scenario);
        CM_LoadMap(map ? map : "__movement_fixture__", qfalse, &checksum);
        stock_loaded = map != NULL;
    }
    memset(&animations, 0, sizeof(animations));
    move.character = BG_GetCharacter(TEAM_AXIS, PC_SOLDIER);
    move.character->animModelInfo = &animations.modelInfo[0];
    // ponytail: empty animation scripts isolate physics; load real animation assets before testing animation/weapon fidelity.
    BG_AnimParseAnimScript(move.character->animModelInfo, &animations, "empty replay animations", empty_script);
    ps.commandTime = 1000; ps.pm_type = PM_NORMAL; ps.stats[STAT_HEALTH] = 100;
    ps.persistant[PERS_TEAM] = TEAM_AXIS; ps.aiState = AISTATE_COMBAT;
    ps.gravity = 800; ps.speed = 320; ps.friction = 1; ps.groundEntityNum = ENTITYNUM_NONE;
    VectorSet(ps.mins, -18, -18, -24); VectorSet(ps.maxs, 18, 18, 48);
    ps.crouchMaxZ = 24; ps.crouchViewHeight = CROUCH_VIEWHEIGHT; ps.standViewHeight = DEFAULT_VIEWHEIGHT;
    ps.deadViewHeight = DEAD_VIEWHEIGHT; ps.viewheight = DEFAULT_VIEWHEIGHT;
    ps.runSpeedScale = 0.8f; ps.sprintSpeedScale = 1.1f; ps.crouchSpeedScale = 0.25f;
    ps.weapon = WP_LUGER; ps.ammoclip[WP_LUGER] = 8; ps.ammo[WP_LUGER] = 24; COM_BitSet(ps.weapons, WP_LUGER);
    ps.origin[2] = 24; ext.sprintTime = 20000;
    if (map) {
        trace_t trace;
        check(spawn_origin(ps.origin), "BSP has no supported player spawn"); ps.origin[2] += 9;
        CM_BoxTrace(&trace, ps.origin, ps.origin, ps.mins, ps.maxs, 0, MASK_PLAYERSOLID, qtrue);
        check(!trace.startsolid, "BSP spawn is solid");
    }
    if (scenario == 7) ps.origin[0] -= 64;
    if (scenario == 9) ps.origin[2] += 232;
    if (scenario == 10) ps.pm_type = PM_SPECTATOR;
    if (scenario == 11) ps.pm_type = PM_NOCLIP;
    if (scenario == 12) ps.pm_type = PM_FREEZE;
    if (scenario == 13) { ps.pm_type = PM_DEAD; ps.stats[STAT_HEALTH] = 0; ps.eFlags |= EF_DEAD; ps.origin[2] += 40; ps.maxs[2] = -8; }
    if (scenario == 14) ps.origin[2] = 0;
    if (scenario == 15) ps.weapon = WP_NONE; // Equal-time commands otherwise expose upstream aim-spread's division by zero.
    move.ps = &ps; move.pmext = &ext; move.skill = skill; move.trace = trace_world; move.pointcontents = contents_world;
    move.tracemask = MASK_PLAYERSOLID; move.gametype = GT_WOLF; move.pmove_fixed = fixed != 0; move.pmove_msec = fixed;
    move.oldcmd.serverTime = time; move.oldcmd.weapon = ps.weapon;
    memset(commands, 0, sizeof(commands)); memset(&pml, 0, sizeof(pml)); srand(1);
    for (i = 0; i < FRAMES; ++i) {
        static const int deltas[] = {0, -4, 1, 8, 16, 33, 50, 66, 200, 1250};
        usercmd_t *cmd = &commands[i];
        time += scenario == 15 ? deltas[i % 10] : 16;
        cmd->serverTime = fixed ? ((time + fixed - 1) / fixed) * fixed : time;
        cmd->weapon = ps.weapon; cmd->forwardmove = scenario == 0 ? 0 : 127;
        if (scenario == 2) { cmd->forwardmove = 64; cmd->rightmove = 127; }
        if (scenario == 3 && (i == 8 || i == 80)) cmd->upmove = 127;
        if (scenario == 4 && i < 90) cmd->upmove = -127;
        if (scenario == 1 && i >= 64 && i < 96) cmd->buttons = BUTTON_SPRINT;
        if (scenario == 1 && i >= 96) cmd->angles[YAW] = ANGLE2SHORT((i - 95) * 7.03125f);
        if (scenario == 1 && i >= 112) cmd->buttons = BUTTON_TALK;
    }
    for (i = 0; i < FRAMES; ++i) {
        if (i % 32 == 0) {
            checkpoint_ps = ps; checkpoint_ext = ext; checkpoint_move = move; checkpoint_pml = pml;
            memcpy(checkpoint_conditions, animations.clientConditions[0], sizeof(checkpoint_conditions));
        }
        run(&move, &records[i], i, scenario);
        check(isfinite(ps.origin[0]) && isfinite(ps.origin[1]) && isfinite(ps.origin[2]), "non-finite origin");
        if (ps.origin[2] > max_height) max_height = ps.origin[2];
        if (ps.origin[0] > max_x) max_x = ps.origin[0];
        collisions += records[i].hits; solid += records[i].startsolid;
        ducked |= (ps.pm_flags & PMF_DUCKED) != 0; submerged |= move.waterlevel >= 2;
        if (i % 32 == 31) {
            ps = checkpoint_ps; ext = checkpoint_ext; move = checkpoint_move; pml = checkpoint_pml;
            memcpy(animations.clientConditions[0], checkpoint_conditions, sizeof(checkpoint_conditions));
            for (j = i - 31; j <= i; ++j) {
                frame_t repeated;
                run(&move, &repeated, j, scenario);
                if (memcmp(&records[j], &repeated, sizeof(repeated))) Com_Error(ERR_FATAL, "snapshot replay mismatch: %s/%d frame %d", cases[scenario], fixed, j);
            }
        }
        output(&records[i], sizeof(records[i]));
    }
    if (!map) {
        if (scenario == 0 || scenario == 12) check(ps.origin[0] == 0 && fabs(ps.origin[2] - 24) <= 0.25f, "idle/freeze moved");
        if (scenario == 1 || scenario == 2) check(max_x > 100, "walking/strafe did not move");
        if (scenario == 3) check(max_height > 60 && collisions > 0, "jump/landing not exercised");
        if (scenario == 4) check(ducked, "crouch not exercised");
        if (scenario == 5 || scenario == 10) check(max_x <= 111 && collisions > 0, "wall/spectator collision failed");
        if (scenario == 6) check(max_height > 50 && max_x > 160, "stairs not traversed");
        if (scenario == 7) check(max_height > 80, "ramp not climbed");
        if (scenario == 8) check(submerged, "water not exercised");
        if (scenario == 9 || scenario == 13) check(ps.origin[2] < 30 && collisions > 0, "fall/dead landing failed");
        if (scenario == 11) check(max_x > 160 && collisions == 0, "noclip did not bypass wall");
        if (scenario == 14) check(solid > 0, "startsolid not exercised");
    }
}
void Replay_Accelerate(pmove_t *move, const pml_t *local, vec3_t direction, float speed, float acceleration);
static uint32_t probe_seed = 1;
static float probe_random(void) {
    // Independent unsigned generator: do not use the RNG under test for inputs.
    probe_seed = probe_seed * 1664525u + 1013904223u;
    return (probe_seed >> 8) * (1.0f / 16777216.0f);
}
static void acceleration_probes(void) {
    uint32_t count = 100000, i;
    static const float frictions[] = {0.0f, -0.0f, 0.125f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f, -0.5f};
    output("ETACCEL1", 8); output(&count, sizeof(count));
    for (i = 0; i < count; ++i) {
        playerState_t ps = {0}, original;
        pmove_t move = {0}; pml_t local = {0};
        vec3_t direction, saved_direction; float *wishdir;
        float speed = probe_random() * 400.0f, acceleration = probe_random() * 20.0f;
        int axis;
        move.ps = &ps;
        for (axis = 0; axis < 3; ++axis) {
            ps.velocity[axis] = (probe_random() * 2.0f - 1.0f) * 450.0f;
            direction[axis] = probe_random() * 2.0f - 1.0f;
        }
        local.frametime = probe_random() * 0.2f;
        ps.groundEntityNum = i & 1 ? ENTITYNUM_NONE : ENTITYNUM_WORLD;
        ps.friction = i % 3 ? 0.125f + probe_random() * 4.0f : frictions[(i / 3) % 9];
        // Explicit branch boundaries before the generated cases.
        if (i < 16) {
            VectorSet(ps.velocity, 0, 0, 0); VectorSet(direction, 1, 0, 0);
            speed = 320; acceleration = 100; local.frametime = 0.2f;
            ps.groundEntityNum = ENTITYNUM_WORLD; ps.friction = 2;
            switch (i) {
            case 0: ps.velocity[0] = 400; break; // Already faster: return unchanged.
            case 1: break; // First cap must happen before AI friction scaling.
            case 2: ps.velocity[0] = 300; ps.friction = 0.25f; break; // Second cap.
            case 3: ps.velocity[0] = speed; break; // Exactly zero addspeed.
            case 4: local.frametime = 0; break;
            case 5: acceleration = 0; break;
            case 6: speed = 0; break;
            case 7: ps.friction = 0; break;
            case 8: ps.friction = -0.0f; break;
            case 9: ps.friction = -0.5f; break;
            case 10: ps.groundEntityNum = ENTITYNUM_NONE; ps.friction = 0; break;
            case 11: direction[0] = -1; break;
            case 12: ps.velocity[0] = -0.0f; direction[1] = -0.0f; break;
            case 13: speed = FLT_MIN; acceleration = 1; local.frametime = 1; break;
            case 14: ps.friction = FLT_MIN; break;
            case 15: ps.friction = FLT_MAX; break;
            }
        }
        if (i >= 16 && i % 8 == 0) {
            for (axis = 0; axis < 3; ++axis) ps.velocity[axis] /= 450.0f;
        }
        wishdir = i >= 16 && i % 8 == 0 ? ps.velocity : direction; // Exercise writes with in-place direction.
        memcpy(saved_direction, direction, sizeof(direction)); original = ps;
        Replay_Accelerate(&move, &local, wishdir, speed, acceleration);
        // Only velocity may be written, including its surrounding state fields.
        memcpy(original.velocity, ps.velocity, sizeof(ps.velocity));
        check(!memcmp(&original, &ps, sizeof(ps)), "acceleration wrote outside velocity");
        check(!memcmp(saved_direction, direction, sizeof(direction)), "acceleration modified separate direction");
        output(ps.velocity, sizeof(ps.velocity));
    }
}
int main(int argc, char **argv) {
    uint32_t header[] = {sizeof(frame_t), sizeof(playerState_t), sizeof(pmoveExt_t), 16, 3, FRAMES,
        offsetof(frame_t, ps.origin), offsetof(frame_t, ps.velocity), offsetof(frame_t, waterlevel),
        offsetof(frame_t, traces), offsetof(frame_t, ps.commandTime)};
    int scenario, mode;
    check(argc == 1 || argc == 2, "usage: movement_replay [trusted-stock-map.bsp | --accelerate]");
    _Static_assert(sizeof(void *) == 4 && sizeof(float) == 4, "replay ABI is Windows/x86");
    check(_setmode(_fileno(stdout), _O_BINARY) != -1, "binary output setup failed"); Swap_Init();
    if (argc == 2 && !strcmp(argv[1], "--accelerate")) {
        acceleration_probes(); check(fflush(stdout) == 0, "output flush failed"); return 0;
    }
#ifdef CGAMEDLL
    cg_gameType.integer = GT_WOLF; cg_movespeed.integer = 127;
#else
    g_gametype.integer = GT_WOLF; g_movespeed.integer = 127;
#endif
    if (argc == 2) header[3] = 4; // Stock BSPs: only walk, strafe, jump and crouch; no synthetic-world expectations.
    output("ETMOVE01", 8); output(header, sizeof(header));
    for (scenario = 0; scenario < (int)header[3]; ++scenario) {
        for (mode = 0; mode < 3; ++mode) replay(argc == 2 ? scenario + 1 : scenario, modes[mode], argc == 2 ? argv[1] : NULL);
    }
    check(fflush(stdout) == 0, "output flush failed"); CM_ClearMap(); Hunk_ClearHigh(); free(hunks); return 0;
}
