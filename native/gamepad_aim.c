/* Snapshot-visible targeting; native collision remains the visibility oracle. */
#include "../engine/src/cgame/cg_local.h"
#include "gamepad_math.h"

static qboolean apiAvailable;
/* Hip-fired SMGs and rifles; scoped, heavy and explosive weapons keep raw aim. */
static qboolean AssistWeapon(int weapon) {
    switch (weapon) {
    case WP_MP40: case WP_THOMPSON: case WP_STEN: case WP_FG42:
    case WP_KAR98: case WP_CARBINE: case WP_GARAND: case WP_K43: return qtrue;
    default: return qfalse;
    }
}
static vmCvar_t assist, angleCvar, rangeCvar;

void CG_GamepadInit(void) {
    char value[16];
    trap_Cvar_VariableStringBuffer("et_gamepadAPI", value, sizeof(value));
    apiAvailable = !strcmp(value, "1");
    trap_Cvar_Register(&assist, "cg_gamepadAimAssist", "1", CVAR_ARCHIVE);
    /* These ET-unit cone/range defaults are estimates, not recovered Halo tags. */
    trap_Cvar_Register(&angleCvar, "cg_gamepadAssistAngle", "8", CVAR_ARCHIVE);
    trap_Cvar_Register(&rangeCvar, "cg_gamepadAssistRange", "1800", CVAR_ARCHIVE);
}

void CG_GamepadAim(qboolean compute) {
    float best = 0.f, bestDistance = 1e30f, yaw = 0.f, pitch = 0.f;
    if (!apiAvailable) return; /* New DLLs also run on an unextended engine. */
    if (!trap_GamepadAim(0.f, 0.f, 0.f) || !compute) return; /* No target scans for mouse-only play. */
    {
        playerState_t *ps = &cg.predictedPlayerState;
        int i;
        float maxAngle, maxRange;
        trap_Cvar_Update(&assist);
        trap_Cvar_Update(&angleCvar);
        trap_Cvar_Update(&rangeCvar);
        maxAngle = GP_Clamp(angleCvar.value, .1f, 20.f);
        maxRange = GP_Clamp(rangeCvar.value, 64.f, 4096.f);
        if (assist.integer && cg.snap && !cg.demoPlayback &&
            !cg.renderingThirdPerson && !cg.showGameView && !cg.hyperspace &&
            !cg.thisFrameTeleport && !cg.nextFrameTeleport && !cg.mvCurrentMainview &&
            !trap_Key_GetCatcher() && ps->pm_type == PM_NORMAL && ps->stats[STAT_HEALTH] > 0 &&
            !(ps->pm_flags & PMF_FOLLOW) &&
            AssistWeapon(ps->weapon) &&
            cg.time - cg.snap->serverTime >= 0 && cg.time - cg.snap->serverTime < 200 &&
            (ps->persistant[PERS_TEAM] == TEAM_AXIS || ps->persistant[PERS_TEAM] == TEAM_ALLIES)) {
            /* ponytail: scan snapshot players; spatial indexing only if client counts demand it. */
            for (i = 0; i < cg.snap->numEntities; ++i) {
                entityState_t *es = &cg.snap->entities[i];
                centity_t *cent;
                vec3_t target, relative, velocity;
                float distance, angle, level, radius, down, up;
                trace_t trace;
                if (es->eType != ET_PLAYER || es->number < 0 || es->number >= MAX_CLIENTS ||
                    es->number == ps->clientNum || (es->eFlags & EF_DEAD) ||
                    (es->powerups & ((1 << PW_INVULNERABLE) | (1 << PW_OPS_DISGUISED))) ||
                    !cgs.clientinfo[es->number].infoValid ||
                    cgs.clientinfo[es->number].team == ps->persistant[PERS_TEAM] ||
                    (cgs.clientinfo[es->number].team != TEAM_AXIS && cgs.clientinfo[es->number].team != TEAM_ALLIES)) continue;
                cent = &cg_entities[es->number];
                if (!cent->currentValid || !es->solid || es->solid == SOLID_BMODEL) continue;
                radius = es->solid & 255;
                down = (es->solid >> 8) & 255;
                up = ((es->solid >> 16) & 255) - 32;
                if (radius <= 0.f || up + down < radius * 2.f) continue;
                GP_CapsulePoint(cg.refdef.vieworg, cg.refdef.viewaxis[0], cent->lerpOrigin,
                    -down + radius, up - radius, radius, target);
                VectorSubtract(target, cg.refdef.vieworg, relative);
                distance = VectorLength(relative);
                if (distance < 1.f || distance >= maxRange) continue;
                angle = acosf(GP_Clamp(DotProduct(relative, cg.refdef.viewaxis[0]) / distance, -1.f, 1.f)) * 57.2957795f;
                level = GP_Attenuation(distance, maxRange) * GP_Attenuation(angle, maxAngle);
                if (level <= 0.f || level < best || (level == best && distance >= bestDistance)) continue;
                CG_Trace(&trace, cg.refdef.vieworg, vec3_origin, vec3_origin, target, ps->clientNum, MASK_SHOT);
                if (trace.startsolid || trace.allsolid || (trace.fraction < 1.f && trace.entityNum != es->number)) continue;
                BG_EvaluateTrajectoryDelta(&cent->currentState.pos, cg.time, velocity, qfalse, cent->currentState.effect2Time);
                VectorSubtract(velocity, ps->velocity, velocity);
                GP_TargetVelocity(relative, velocity, &yaw, &pitch);
                best = level;
                bestDistance = distance;
            }
        }
    }
    trap_GamepadAim(best, yaw, pitch);
}
