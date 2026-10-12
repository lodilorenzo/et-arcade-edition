/* Target policy regression, using the actual cgame adapter with engine-service stubs. */
#undef NDEBUG
#include <assert.h>
#include "../native/gamepad_aim.c"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
vec3_t vec3_origin;
static snapshot_t snapshot;
static int api = 1, activePad = 1, hidden = -1, calls;
static float resultLevel, resultYaw, resultPitch;
void trap_Cvar_VariableStringBuffer(const char *name, char *out, int size) {
    assert(!strcmp(name, "et_gamepadAPI") && size > 1); strcpy(out, api ? "1" : "0");
}
void trap_Cvar_Register(vmCvar_t *cvar, const char *name, const char *value, int flags) {
    (void)name; (void)flags; cvar->value = (float)atof(value); cvar->integer = atoi(value);
}
void trap_Cvar_Update(vmCvar_t *cvar) {(void)cvar;}
int trap_Key_GetCatcher(void) {return 0;}
qboolean trap_GamepadAim(float level, float yaw, float pitch) {
    ++calls; resultLevel = level; resultYaw = yaw; resultPitch = pitch;
    return activePad ? qtrue : qfalse;
}
vec_t VectorLength(const vec3_t p) {return sqrtf(DotProduct(p, p));}
void BG_EvaluateTrajectoryDelta(const trajectory_t *tr, int time, vec3_t out, qboolean angle, int spline) {
    (void)time; (void)angle; (void)spline; VectorCopy(tr->trDelta, out);
}
void CG_Trace(trace_t *trace, const vec3_t start, const vec3_t mins, const vec3_t maxs,
    const vec3_t end, int skip, int mask) {
    (void)start; (void)mins; (void)maxs; (void)end;
    assert(skip == 0 && mask == MASK_SHOT);
    memset(trace, 0, sizeof(*trace));
    trace->fraction = hidden >= 0 ? .5f : 1.f;
    trace->entityNum = hidden >= 0 ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
}
int main(void) {
    entityState_t *target;
    api = 0; CG_GamepadInit(); CG_GamepadAim(qtrue); assert(calls == 0);
    api = 1; CG_GamepadInit();
    cg.snap = &snapshot; snapshot.serverTime = cg.time = 1000;
    snapshot.numEntities = 1;
    cg.predictedPlayerState.pm_type = PM_NORMAL;
    cg.predictedPlayerState.stats[STAT_HEALTH] = 100;
    cg.predictedPlayerState.persistant[PERS_TEAM] = TEAM_AXIS;
    cg.predictedPlayerState.weapon = WP_MP40;
    cg.refdef.vieworg[2] = 40.f;
    cg.refdef.viewaxis[0][0] = 1.f;
    target = &snapshot.entities[0];
    target->number = target->clientNum = 1; target->eType = ET_PLAYER;
    target->solid = 18 | (24 << 8) | (80 << 16);
    cgs.clientinfo[1].team = TEAM_ALLIES; cgs.clientinfo[1].infoValid = qtrue;
    cg_entities[1].currentValid = qtrue;
    cg_entities[1].lerpOrigin[0] = 300.f;
    cg_entities[1].currentState.pos.trDelta[1] = 10.f;
    CG_GamepadAim(qtrue); assert(resultLevel == 1.f && resultYaw > 0.f && resultPitch == 0.f);
    CG_GamepadAim(qfalse); assert(resultLevel == 0.f);
    activePad = 0; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); activePad = 1;
    hidden = 1; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); hidden = -1;
    target->eFlags = EF_DEAD; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); target->eFlags = 0;
    target->powerups = 1 << PW_INVULNERABLE; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); target->powerups = 0;
    cgs.clientinfo[1].team = TEAM_AXIS; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cgs.clientinfo[1].team = TEAM_ALLIES;
    cg_entities[1].currentValid = qfalse; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg_entities[1].currentValid = qtrue;
    cg_entities[1].lerpOrigin[1] = 100.f; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg_entities[1].lerpOrigin[1] = 0.f;
    cg_entities[1].lerpOrigin[0] = 2000.f; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg_entities[1].lerpOrigin[0] = 300.f;
    target->powerups = 1 << PW_OPS_DISGUISED; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); target->powerups = 0;
    cg.demoPlayback = qtrue; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.demoPlayback = qfalse;
    cg.predictedPlayerState.stats[STAT_HEALTH] = 0; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.predictedPlayerState.stats[STAT_HEALTH] = 100;
    cg.predictedPlayerState.pm_type = PM_FREEZE; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.predictedPlayerState.pm_type = PM_NORMAL;
    cg.predictedPlayerState.weapon = WP_LUGER; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.predictedPlayerState.weapon = WP_GARAND;
    cg.thisFrameTeleport = qtrue; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.thisFrameTeleport = qfalse;
    cg.time += 200; CG_GamepadAim(qtrue); assert(resultLevel == 0.f); cg.time -= 200;
    CG_GamepadAim(qtrue); assert(resultLevel == 1.f);
    puts("PASS gamepad targets: capability, weapon/lifecycle, disguise, capsule, cone/range, visibility and team policy");
    return 0;
}
