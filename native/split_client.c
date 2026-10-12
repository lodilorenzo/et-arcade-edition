/* Up to four human clients, one local server/renderer. No wire-format changes. */
#include "../engine/src/client/client.h"
#include "../engine/src/server/server.h"
#include "../engine/src/game/bg_public.h"

extern int CL_CgameSystemCalls(int *args);
/* ponytail: bounded to four players; copy native client state rather than refactor
   every engine global. Use selected-context pointers if profiling warrants it. */
static struct {
    clientActive_t active;
    clientConnection_t connection;
    vm_t *vm;
    int client;
    char join[64]; /* Last confirmed loadout survives renderer/cgame reloads. */
} locals[ET_LOCAL_PLAYERS];
static int player, localCount = 1, loadouts, loadoutEpoch[ET_LOCAL_PLAYERS];
static qboolean selectionPrompted, loadoutDrawing, mapDrawing;
static int mapX, mapY;

static int ConfiguredCount(void) {
    int value = Cvar_VariableIntegerValue("cl_splitScreen");
    return ET_LOCAL_PLAYER_COUNT(value);
}
int CL_SplitPlayer(void) { return player; }
/* Keyboard & mouse owner: cl_keyboardPlayer when that player is in the match, else player one. */
int CL_KeyboardPlayer(void) {
    int owner = Cvar_VariableIntegerValue("cl_keyboardPlayer") - 1;
    return owner == -1 ? -1 : owner >= 0 && owner < localCount ? owner : 0;
}
/* Game mouse motion for a keyboard owner other than player one goes to that player's view. */
qboolean CL_SplitMouseEvent(int dx, int dy) {
    int owner = CL_KeyboardPlayer();
    clientActive_t *target;
    if (owner < 0) return qtrue;
    if (!owner || owner == player || !locals[owner].vm) return qfalse;
    target = &locals[owner].active;
    if (dx || dy) target->gamepadMouseTime = cls.realtime;
    target->mouseDx[target->mouseIndex] += dx; target->mouseDy[target->mouseIndex] += dy;
    return qtrue;
}
qboolean CL_SplitEnabled(void) { return localCount > 1; }
qboolean CL_SplitReady(void) { return localCount > 1 && locals[localCount - 1].vm && !player; }
int CL_SplitGamepadCount(void) { return cls.keyCatchers & KEYCATCH_UI ? ConfiguredCount() : localCount; }
qboolean CL_LocalLoadoutOpen(int local) {
    return local >= 0 && local < localCount && ((loadouts & (1 << local)) != 0 || (!local && CL_OnlineMenuOpen()));
}
int CL_LocalLoadoutEpoch(int local) { return local >= 0 && local < ET_LOCAL_PLAYERS ? loadoutEpoch[local] + (local ? 0 : CL_OnlineMenuEpoch()) : 0; }
const char *CL_LocalConfigString(int local,int index) {
    const gameState_t *state;
    if (local<0 || local>=localCount || index<0 || index>=MAX_CONFIGSTRINGS) return "";
    state=local ? &locals[local].active.gameState : &cl.gameState;
    return state->stringData+state->stringOffsets[index];
}
void CL_LocalViewport(int local, int *x, int *y, int *w, int *h) {
    int columns = localCount > 1 ? 2 : 1, rows = localCount >= 3 ? 2 : 1;
    *x = (local % columns) * (cls.glconfig.vidWidth / columns);
    *y = (local / columns) * (cls.glconfig.vidHeight / rows);
    *w = local % columns == columns - 1 ? cls.glconfig.vidWidth - *x : cls.glconfig.vidWidth / columns;
    *h = local / columns == rows - 1 ? cls.glconfig.vidHeight - *y : cls.glconfig.vidHeight / rows;
}
static void SelectPlayer(int next) {
    if (next == player) return;
    locals[player].active = cl; locals[player].connection = clc; locals[player].vm = cgvm;
    cl = locals[next].active; clc = locals[next].connection; cgvm = locals[next].vm;
    player = next;
    Cvar_SetValue("et_cgamePlayer", next + 1);
}
static client_t *LocalClient(int local) {
    int id = local ? locals[local].client : clc.clientNum;
    if (!svs.clients || sv.state != SS_GAME || !gvm || id < (local ? 1 : 0) || id >= sv_maxclients->integer) return NULL;
    return svs.clients[id].state == CS_ACTIVE ? &svs.clients[id] : NULL;
}
static void GameState(gameState_t *state) {
    int i;
    memset(state, 0, sizeof(*state)); state->dataCount = 1;
    for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
        const char *value = sv.configstrings[i];
        int size;
        if (!value || !*value) continue;
        size = strlen(value) + 1;
        if (size + state->dataCount > MAX_GAMESTATE_CHARS) Com_Error(ERR_DROP, "Split-screen gamestate overflow");
        state->stringOffsets[i] = state->dataCount;
        memcpy(state->stringData + state->dataCount, value, size); state->dataCount += size;
    }
}
static void Snapshot(int local) {
    client_t *client = LocalClient(local);
    clientActive_t *active = &locals[local].active;
    clientConnection_t *connection = &locals[local].connection;
    clientSnapshot_t *frame;
    clSnapshot_t *snap = &active->snap;
    int n;
    if (!client || (snap->valid && snap->serverTime == svs.time)) return;
    SV_BuildClientSnapshot(client); /* Native PVS, portals, areamask and callbacks. */
    frame = &client->frames[client->netchan.outgoingSequence & PACKET_MASK];
    memset(snap, 0, sizeof(*snap));
    snap->valid = qtrue; snap->messageNum = ++connection->serverMessageSequence;
    snap->serverTime = svs.time; snap->ps = frame->ps; snap->snapFlags = svs.snapFlagServerBit;
    memcpy(snap->areamask, frame->areabits, sizeof(snap->areamask));
    snap->parseEntitiesNum = active->parseEntitiesNum; snap->numEntities = frame->num_entities;
    for (n = 0; n < frame->num_entities; ++n)
        active->parseEntities[active->parseEntitiesNum++ & (MAX_PARSE_ENTITIES - 1)] = svs.snapshotEntities[(frame->first_entity + n) % svs.numSnapshotEntities];
    for (n = connection->serverCommandSequence + 1; n <= client->reliableSequence; ++n) {
        if (n <= client->reliableSequence - MAX_RELIABLE_COMMANDS) Com_Error(ERR_DROP, "Split-screen reliable command overflow");
        Q_strncpyz(connection->serverCommands[n & (MAX_RELIABLE_COMMANDS - 1)], client->reliableCommands[n & (MAX_RELIABLE_COMMANDS - 1)], MAX_TOKEN_CHARS);
    }
    connection->serverCommandSequence = client->reliableSequence;
    snap->serverCommandNum = client->reliableSequence;
    client->reliableAcknowledge = client->reliableSequence; client->lastPacketTime = svs.time;
    ++client->netchan.outgoingSequence;
    active->snapshots[snap->messageNum & PACKET_MASK] = *snap;
}
static qboolean ObjectiveLocal(void) {
    return com_sv_running->integer && clc.serverAddress.type == NA_LOOPBACK && !clc.demoplaying && Cvar_VariableIntegerValue("ui_rustSlice");
}
int CL_ClassWeapon(int team, int type, int index) {
    static const int heavy[] = {WP_MOBILE_MG42, WP_FLAMETHROWER, WP_PANZERFAUST, WP_MORTAR};
    int smg = type == PC_COVERTOPS ? WP_STEN : team == TEAM_AXIS ? WP_MP40 : WP_THOMPSON;
    if (type == PC_SOLDIER && index >= 1 && index <= 4) return heavy[index - 1];
    if (type == PC_ENGINEER && index == 1) return team == TEAM_AXIS ? WP_KAR98 : WP_CARBINE;
    if (type == PC_COVERTOPS && index == 1) return WP_FG42;
    if (type == PC_COVERTOPS && index == 2) return team == TEAM_AXIS ? WP_K43 : WP_GARAND;
    return smg;
}
static void JoinObjective(client_t *client, int local) {
    int team = Cvar_VariableIntegerValue(va("ui_oasisTeam%d", local + 1));
    int type = Cvar_VariableIntegerValue(va("ui_oasisClass%d", local + 1));
    int weapon;
    char command[64];
    if (team < TEAM_AXIS || team > TEAM_SPECTATOR) team = TEAM_ALLIES;
    if (type < PC_SOLDIER || type > PC_COVERTOPS) type = PC_ENGINEER;
    weapon = CL_ClassWeapon(team, type, Cvar_VariableIntegerValue(va("ui_oasisWeapon%d", local + 1)));
    Com_sprintf(command, sizeof(command), "team %s %d %d %d", team == TEAM_AXIS ? "r" : team == TEAM_ALLIES ? "b" : "s", type, weapon, team == TEAM_AXIS ? WP_LUGER : WP_COLT);
    SV_ExecuteClientCommand(client, command, qtrue, qfalse);
    Q_strncpyz(locals[local].join, command, sizeof(locals[local].join));
    Com_Printf("OASIS human player=%d client=%d team=%d class=%d native_rules=1 weapon=%d\n", local + 1, (int)(client - svs.clients), team, type, weapon);
}
static int Loadout(int local, int action, int a, int b) {
    int status, x, y, w, h;
    CL_LocalViewport(local, &x, &y, &w, &h);
    SelectPlayer(local);
    loadoutDrawing = qtrue;
    status = VM_Call(cgvm, ET_CGAME_LOADOUT, action, a, b, w, h);
    loadoutDrawing = qfalse;
    SelectPlayer(0);
    return status;
}
static void CloseLoadout(int local) {
    if (loadouts & (1 << local)) Loadout(local, 0, 0, 0);
    loadouts &= ~(1 << local); ++loadoutEpoch[local];
    Cvar_SetValue("ui_localLoadout", loadouts);
}
static void OpenClass(int local) {
    if (!ObjectiveLocal() || cls.state != CA_ACTIVE || !cgvm || local < 0 || local >= localCount || !LocalClient(local)) return;
    CloseLoadout(local);
    if (local == CL_KeyboardPlayer()) {
        Key_ClearStates();
        memset(cl.mouseDx, 0, sizeof(cl.mouseDx)); memset(cl.mouseDy, 0, sizeof(cl.mouseDy));
    }
    loadouts |= 1 << local; ++loadoutEpoch[local];
    Loadout(local, 1, local + 1, 0);
    Cvar_SetValue("ui_localLoadout", loadouts);
    Com_Printf("LOADOUT opened player=%d stock_limbo=1 viewport=local\n", local + 1);
}
qboolean CL_LocalLoadoutEventForPlayer(int local, int command, int a, int b) {
    if (!local && CL_OnlineMenuEvent(command, a, b)) return qtrue;
    if (!CL_LocalLoadoutOpen(local)) return qfalse;
    if (command == CG_MOUSE_EVENT) { if (a || b) Loadout(local, 4, a, b); }
    else if (command == CG_KEY_EVENT) Loadout(local, 3, a, b);
    return qtrue;
}
qboolean CL_LocalLoadoutEvent(int command, int a, int b) {
    if (cls.keyCatchers) return qfalse; /* Shared console/settings take precedence. */
    /* Pads route directly: queued shared-menu releases must not reach player one's panel. */
    if (command == CG_KEY_EVENT && a >= K_JOY1 && a <= K_JOY7) return qfalse;
    return CL_LocalLoadoutEventForPlayer(CL_KeyboardPlayer(), command, a, b);
}
void CL_LocalLoadoutDraw(void) {
    int i, x, y, w, h;
    if (cls.keyCatchers & KEYCATCH_UI) return;
    for (i = 0; i < localCount; ++i) if (loadouts & (1 << i)) {
        CL_LocalViewport(i, &x, &y, &w, &h); RE_SplitClip(x, y, w, h);
        Loadout(i, 2, 0, 0);
    }
    RE_SplitClip(0, 0, 0, 0);
    CL_OnlineMenuDraw();
}
static int MenuPlayer(void) {
    int local = Cvar_VariableIntegerValue("ui_menuPlayer") - 1;
    return local >= 0 && local < localCount ? local : 0;
}
static void LoadoutTest_f(void) {
    static const char *keys[] = { "a", "b", "up", "down", "left", "right", "x" };
    int i, position = !Q_stricmp(Cmd_Argv(1), "key") ? 3 : (!Q_stricmp(Cmd_Argv(1), "click") || !Q_stricmp(Cmd_Argv(1), "mouse")) ? 4 : 2;
    int local = Cmd_Argc() > position ? atoi(Cmd_Argv(position)) - 1 : MenuPlayer();
    if (!Cvar_VariableIntegerValue("ui_rustMenuTest") || !CL_LocalLoadoutOpen(local)) return;
    if (!Q_stricmp(Cmd_Argv(1), "key")) {
        for (i = 0; i < 7; ++i) if (!Q_stricmp(Cmd_Argv(2), keys[i])) {
            CL_LocalLoadoutEventForPlayer(local, CG_KEY_EVENT, K_JOY1 + i, qtrue);
            CL_LocalLoadoutEventForPlayer(local, CG_KEY_EVENT, K_JOY1 + i, qfalse);
        }
    } else if (!Q_stricmp(Cmd_Argv(1), "click") && Cmd_Argc() >= 4) {
        Loadout(local, 7, atoi(Cmd_Argv(2)), atoi(Cmd_Argv(3)));
        CL_LocalLoadoutEventForPlayer(local, CG_KEY_EVENT, K_MOUSE1, qtrue);
        CL_LocalLoadoutEventForPlayer(local, CG_KEY_EVENT, K_MOUSE1, qfalse);
    } else if (!Q_stricmp(Cmd_Argv(1), "mouse") && Cmd_Argc() >= 4)
        CL_LocalLoadoutEventForPlayer(local, CG_MOUSE_EVENT, atoi(Cmd_Argv(2)), atoi(Cmd_Argv(3)));
    Loadout(local, 6, 0, 0);
}
/* Controller Back: the per-view stock limbo in local matches, the server cgame's own limbo online. */
void CL_OpenLimbo(int local) {
    if (ObjectiveLocal()) OpenClass(local);
    else CL_LocalCgameCommand(local, "openlimbomenu");
}
static void LocalClass_f(void) { OpenClass(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) - 1 : MenuPlayer()); }
static void LocalJoin_f(void) {
    int local = Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) - 1 : MenuPlayer();
    client_t *client;
    if (!ObjectiveLocal() || cls.state != CA_ACTIVE || local < 0 || local >= localCount) return;
    client = LocalClient(local);
    if (client) {
        CloseLoadout(local); JoinObjective(client, local);
        Cvar_SetValue("ui_classPending", Cvar_VariableIntegerValue("ui_classPending") & ~(1 << local));
    }
}
qboolean CL_LocalPlayerState(int local, playerState_t *state) {
    if (local < 0 || local >= localCount) return qfalse;
    if (!local) { if (!cl.snap.valid) return qfalse; *state = cl.snap.ps; }
    else { if (!locals[local].active.snap.valid) return qfalse; *state = locals[local].active.snap.ps; }
    return qtrue;
}
qboolean CL_LocalServerCommand(int local, const char *command) {
    client_t *client;
    if (local < 0 || local >= localCount || cls.state != CA_ACTIVE || cls.keyCatchers || (loadouts & (1 << local))) return qfalse;
    if (!local) CL_AddReliableCommand(command);
    else {
        client = LocalClient(local); if (!client) return qfalse;
        SV_ExecuteClientCommand(client, command, qtrue, qfalse);
    }
    return qtrue; /* Dispatch accepted; remote voice limits remain server-owned. */
}
qboolean CL_LocalCgameCommand(int local, const char *command) {
    qboolean handled;
    if (local < 0 || local >= localCount || !cgvm || (local && !locals[local].vm) || cls.state != CA_ACTIVE ||
        (((cls.keyCatchers & ~KEYCATCH_CONSOLE) || CL_LocalLoadoutOpen(local)) && command[0] != '-')) return qfalse;
    Cmd_TokenizeString(command);
    SelectPlayer(local); handled = VM_Call(cgvm, CG_CONSOLE_COMMAND); SelectPlayer(0);
    return handled;
}
static void WeaponNext_f(void) { CL_LocalCgameCommand(1, "weapnext"); }
static void SplitCgame_f(void) {
    int local = 1, first = 1;
    char command[MAX_STRING_CHARS];
    if (Cmd_Argc() < 2) return;
    if (Cmd_Argv(1)[0] >= '1' && Cmd_Argv(1)[0] <= '4' && !Cmd_Argv(1)[1]) { local = atoi(Cmd_Argv(1)) - 1; first = 2; }
    Q_strncpyz(command, Cmd_ArgsFrom(first), sizeof(command)); CL_LocalCgameCommand(local, command);
}
static void StartLocal(int local) {
    client_t *client;
    clientActive_t *active = &locals[local].active;
    clientConnection_t *connection = &locals[local].connection;
    const char *error;
    usercmd_t command = {0};
    int id = SV_BotAllocateClient(-1);
    if (id < 1) Com_Error(ERR_DROP, "Split-screen needs a free player %d slot", local + 1);
    locals[local].client = id; client = &svs.clients[id];
    Cvar_SetValue(va("et_localClient%d", local + 1), id);
    if (local == 1) Cvar_SetValue("et_splitClientNum", id); /* Legacy diagnostic alias. */
    client->gentity->r.svFlags &= ~SVF_BOT;
    Q_strncpyz(client->userinfo, va("\\name\\Player %d\\ip\\localhost\\rate\\25000\\snaps\\20\\cg_uinfo\\13 0 30", local + 1), sizeof(client->userinfo));
    error = VM_ExplicitArgPtr(gvm, VM_Call(gvm, GAME_CLIENT_CONNECT, id, qtrue, qfalse));
    if (error) { SV_BotFreeClient(id); locals[local].client = -1; Com_Error(ERR_DROP, "Split-screen connect: %s", error); }
    SV_UserinfoChanged(client); client->pureAuthentic = qtrue; SV_ClientEnterWorld(client, &command);
    if (client->gentity->r.svFlags & SVF_BOT) Com_Error(ERR_DROP, "Split-screen player incorrectly classified as AI");
    if (ObjectiveLocal() && !(Cvar_VariableIntegerValue("ui_classPending") & (1 << local)) && locals[local].join[0])
        SV_ExecuteClientCommand(client, locals[local].join, qtrue, qfalse);
    memset(active, 0, sizeof(*active)); memset(connection, 0, sizeof(*connection));
    GameState(&active->gameState); Q_strncpyz(active->mapname, cl.mapname, sizeof(active->mapname));
    active->serverId = sv.serverId; active->cgameSensitivity = 1.f; active->gamepadMouseTime = cls.realtime - 1000;
    connection->clientNum = id;
    /* These contexts consume systeminfo too (notably on the round restart). */
    connection->serverAddress = clc.serverAddress;
    connection->checksumFeed = clc.checksumFeed;
    connection->serverCommandSequence = connection->lastExecutedServerCommand = client->reliableSequence;
    client->reliableAcknowledge = client->reliableSequence;
    Snapshot(local); VectorCopy(active->snap.ps.viewangles, active->viewangles);
    { int i; for (i = 0; i < 3; ++i) active->viewangles[i] -= SHORT2ANGLE(active->snap.ps.delta_angles[i]); }
    active->serverTime = cl.serverTime;
    locals[local].vm = VM_Create(va("cgame%d", local + 1), CL_CgameSystemCalls, VMI_NATIVE);
    if (!locals[local].vm) Com_Error(ERR_DROP, "Split-screen requires cgame%d_mp_x86.dll", local + 1);
    SelectPlayer(local);
    VM_Call(cgvm, CG_INIT, clc.serverMessageSequence - 1, clc.lastExecutedServerCommand, clc.clientNum, qfalse, 0, 0, 0, 0, 0, 0, 0, 0);
    SelectPlayer(0);
    Com_Printf("SPLITCLIENT ready player=%d client=%d ai=0\n", local + 1, id);
}
void CL_SplitShutdown(void) {
    int i;
    CL_RadialReset(); CL_OnlineMenuClose();
    loadoutDrawing = qfalse; loadouts = 0; Cvar_Set("ui_localLoadout", "0"); Cvar_Set("et_localClientsReady", "0");
    if (player) SelectPlayer(0);
    for (i = 1; i < ET_LOCAL_PLAYERS; ++i) {
        client_t *client;
        if (locals[i].vm) {
            SelectPlayer(i); VM_Call(cgvm, CG_SHUTDOWN); SelectPlayer(0);
            VM_Free(locals[i].vm); locals[i].vm = NULL;
        }
        client = LocalClient(i);
        if (client) SV_DropClient(client, "Local split-screen ended");
        Cvar_Set(va("et_localClient%d", i + 1), "-1");
        locals[i].client = -1;
        memset(&locals[i].active, 0, sizeof(locals[i].active)); memset(&locals[i].connection, 0, sizeof(locals[i].connection));
    }
    for (i = 0; i < ET_LOCAL_PLAYERS; ++i) ++loadoutEpoch[i];
    Cvar_Set("et_splitClientNum", "-1"); Cvar_Set("et_cgamePlayer", "1");
    localCount = 1; selectionPrompted = qfalse;
}
/* Regression hook: game mouse motion as from the OS mouse (developer + ui_rustMenuTest). */
static void MouseTest_f(void) {
    if (Cvar_VariableIntegerValue("developer") && Cvar_VariableIntegerValue("ui_rustMenuTest") && Cmd_Argc() == 3)
        CL_MouseEvent(atoi(Cmd_Argv(1)), atoi(Cmd_Argv(2)), 0);
}
static void State_f(void) {
    playerState_t prediction[ET_LOCAL_PLAYERS];
    int i, id[ET_LOCAL_PLAYERS];
    if (!CL_SplitReady() || !Cvar_VariableIntegerValue("developer")) return;
    for (i = 0; i < localCount; ++i) {
        SelectPlayer(i); id[i] = VM_Call(cgvm, ET_CGAME_LOCAL_STATE, &prediction[i]);
        Com_Printf("SPLITPLAYER player=%d client=%d ai=%d cmd=%d health=%d clip=%d generation=%d pos=%.3f,%.3f,%.3f predict=%.3f,%.3f,%.3f yaw=%.3f weapon=%d menu=%d\n",
            i + 1, id[i], LocalClient(i) ? !!(LocalClient(i)->gentity->r.svFlags & SVF_BOT) : -1, cl.cmdNumber,
            cl.snap.ps.stats[STAT_HEALTH], cl.snap.ps.ammoclip[cl.snap.ps.weapon], cl.snap.ps.persistant[PERS_SPAWN_COUNT],
            cl.snap.ps.origin[0], cl.snap.ps.origin[1], cl.snap.ps.origin[2], prediction[i].origin[0], prediction[i].origin[1], prediction[i].origin[2], cl.viewangles[YAW], cl.snap.ps.weapon, CL_LocalLoadoutOpen(i));
        SelectPlayer(0);
    }
    if (localCount == 2) {
        clientActive_t *second = &locals[1].active;
        Com_Printf("SPLITSTATE ids=%d,%d ai2=%d cmd=%d,%d health=%d,%d clip=%d,%d generation=%d,%d pos1=%.3f,%.3f,%.3f pos2=%.3f,%.3f,%.3f predict1=%.3f,%.3f,%.3f predict2=%.3f,%.3f,%.3f yaw=%.3f,%.3f weapon=%d,%d\n",
            id[0], id[1], LocalClient(1) ? !!(LocalClient(1)->gentity->r.svFlags & SVF_BOT) : -1, cl.cmdNumber, second->cmdNumber,
            cl.snap.ps.stats[STAT_HEALTH], second->snap.ps.stats[STAT_HEALTH], cl.snap.ps.ammoclip[cl.snap.ps.weapon], second->snap.ps.ammoclip[second->snap.ps.weapon],
            cl.snap.ps.persistant[PERS_SPAWN_COUNT], second->snap.ps.persistant[PERS_SPAWN_COUNT], cl.snap.ps.origin[0], cl.snap.ps.origin[1], cl.snap.ps.origin[2], second->snap.ps.origin[0], second->snap.ps.origin[1], second->snap.ps.origin[2],
            prediction[0].origin[0], prediction[0].origin[1], prediction[0].origin[2], prediction[1].origin[0], prediction[1].origin[1], prediction[1].origin[2], cl.viewangles[YAW], second->viewangles[YAW], cl.snap.ps.weapon, second->snap.ps.weapon);
    }
}
void CL_SplitPrepare(void) {
    int i;
    CL_SplitShutdown(); CL_FullMapReset(); Cvar_Get("cl_splitScreen", "0", CVAR_ARCHIVE);
    Cvar_Get("et_cgamePlayer", "1", CVAR_ROM); Cvar_Get("et_localClientsReady", "0", CVAR_ROM);
    for (i = 1; i < ET_LOCAL_PLAYERS; ++i) Cvar_Get(va("et_localClient%d", i + 1), "-1", CVAR_ROM);
    if (com_sv_running->integer && clc.serverAddress.type == NA_LOOPBACK && !clc.demoplaying) localCount = ConfiguredCount();
    Cmd_RemoveCommand("split_state"); Cmd_AddCommand("split_state", State_f);
    Cmd_RemoveCommand("split_mouse_test"); Cmd_AddCommand("split_mouse_test", MouseTest_f);
    Cmd_RemoveCommand("split_weapnext"); Cmd_AddCommand("split_weapnext", WeaponNext_f);
    Cmd_RemoveCommand("split_cgame"); Cmd_AddCommand("split_cgame", SplitCgame_f);
    Cmd_RemoveCommand("local_join"); Cmd_AddCommand("local_join", LocalJoin_f);
    Cmd_RemoveCommand("local_class"); Cmd_AddCommand("local_class", LocalClass_f);
    Cmd_RemoveCommand("loadout_test"); Cmd_AddCommand("loadout_test", LoadoutTest_f);
}
void CL_SplitFrame(void) {
    int i;
    if (cls.state != CA_ACTIVE) return;
    if (localCount > 1 && !locals[localCount - 1].vm) {
        for (i = 1; i < localCount; ++i) StartLocal(i);
        Com_Printf("SPLITSCREEN ready players=%d", clc.clientNum);
        for (i = 1; i < localCount; ++i) Com_Printf(",%d", locals[i].client);
        Com_Printf(" ai2=0 layout=%s\n", localCount >= 3 ? "2x2" : "side-by-side"); Con_ClearNotify();
    }
    Cvar_SetValue("et_localClientsReady", localCount);
    if (!selectionPrompted && ObjectiveLocal() && cl.snap.valid && Cvar_VariableIntegerValue("ui_classPending")) {
        selectionPrompted = qtrue;
        for (i = 0; i < localCount; ++i) if (Cvar_VariableIntegerValue("ui_classPending") & (1 << i)) OpenClass(i);
    }
    for (i = 0; i < localCount; ++i) if (loadouts & (1 << i)) {
        int status = Loadout(i, 5, 0, 0);
        if (status != 1) {
            if (status == 2) {
                Cvar_SetValue("ui_classPending", Cvar_VariableIntegerValue("ui_classPending") & ~(1 << i));
                Cvar_VariableStringBuffer(va("ui_localJoin%d", i + 1), locals[i].join, sizeof(locals[i].join));
                Com_Printf("LOADOUT confirmed player=%d\n", i + 1);
            }
            CloseLoadout(i);
        }
    }
    for (i = 1; i < localCount; ++i) {
        client_t *client = LocalClient(i);
        if (!client) { Com_Error(ERR_DROP, "Local split-screen player %d disconnected", i + 1); return; }
        Snapshot(i); locals[i].active.serverTime = cl.serverTime; locals[i].active.serverId = sv.serverId;
        SelectPlayer(i); cl.cmds[++cl.cmdNumber & CMD_MASK] = CL_CreateCmd(); SV_ClientThink(client, &cl.cmds[cl.cmdNumber & CMD_MASK]); SelectPlayer(0);
    }
}
qboolean CL_SplitMapDraw(int x, int y, int size) {
    int drawn;
    if (localCount != 3 || !CL_SplitReady()) return qfalse;
    mapX = x; mapY = y; mapDrawing = qtrue;
    drawn = VM_Call(cgvm, ET_CGAME_COMMAND_MAP, size);
    mapDrawing = qfalse;
    return drawn != 0;
}
void CL_SplitDraw(stereoFrame_t stereo) {
    int i, x, y, w, h;
    if (!CL_SplitReady() || cls.state != CA_ACTIVE) return;
    for (i = 0; i < localCount; ++i) {
        CL_LocalViewport(i, &x, &y, &w, &h); RE_SplitClip(x, y, w, h);
        SelectPlayer(i); VM_Call(cgvm, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying); SelectPlayer(0);
    }
    if (localCount == 3) {
        CL_LocalViewport(3, &x, &y, &w, &h); RE_SplitClip(x, y, w, h);
        CL_FullMapDraw(x, y, w, h);
    }
    RE_SplitClip(0, 0, 0, 0); re.SetColor(NULL);
    re.DrawStretchPic(cls.glconfig.vidWidth / 2 - 1, 0, 2, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader);
    if (localCount >= 3) re.DrawStretchPic(0, cls.glconfig.vidHeight / 2 - 1, cls.glconfig.vidWidth, 2, 0, 0, 0, 0, cls.whiteShader);
    for (i = 0; i < localCount; ++i) {
        CL_LocalViewport(i, &x, &y, &w, &h);
        SCR_DrawSmallStringExt(x + 8, y + (int)(32 * fminf(w / 640.f, h / 480.f)), va("PLAYER %d%s", i + 1, i != CL_KeyboardPlayer() && !CL_GamepadConnected(i) ? " - CONNECT CONTROLLER" : ""), colorWhite, qtrue);
    }
}
/* All cgame rendering stays local; apply both viewport offsets at the renderer boundary. */
qboolean CL_SplitTrap(int *args, int *result) {
    int x, y, w, h;
    #define PTR(n) VM_ArgPtr(args[n])
    #define FLOAT(n) (((float *)args)[n])
    *result = 0;
    if (localCount == 1) return qfalse;
    CL_LocalViewport(player, &x, &y, &w, &h);
    if (mapDrawing) { x = mapX; y = mapY; }
    if (player && loadoutDrawing && (args[0] == CG_S_STARTLOCALSOUND || args[0] == CG_S_STARTSOUND || args[0] == CG_S_STARTSOUNDEX || args[0] == CG_S_STARTSTREAMINGSOUND || args[0] == CG_S_STOPSTREAMINGSOUND)) return qfalse;
    if (player) switch (args[0]) {
        /* ponytail: shared speakers use player one's world mix; multi-listener audio only if needed. */
        case CG_S_STARTSOUND: case CG_S_STARTSOUNDEX: case CG_S_STARTLOCALSOUND:
        case CG_S_CLEARLOOPINGSOUNDS: case CG_S_CLEARSOUNDS: case CG_S_ADDLOOPINGSOUND:
        case CG_S_ADDREALLOOPINGSOUND: case CG_S_STOPSTREAMINGSOUND: case CG_S_UPDATEENTITYPOSITION:
        case CG_S_RESPATIALIZE: case CG_S_STARTBACKGROUNDTRACK: case CG_S_STOPBACKGROUNDTRACK:
        case CG_S_FADESTREAMINGSOUND: case CG_S_STARTSTREAMINGSOUND: case CG_S_FADEALLSOUNDS:
        case CG_R_LOADWORLDMAP: case CG_ADDCOMMAND: case CG_REMOVECOMMAND:
        case CG_UPDATESCREEN: case CG_INGAME_POPUP: case CG_KEY_SETCATCHER: return qtrue;
        case CG_SENDCONSOLECOMMAND:
            if (!strcmp(PTR(1), va("local_class %d\n", player + 1))) Cbuf_AddText(PTR(1));
            else if (loadoutDrawing && !strncmp(PTR(1), "setspawnpt ", 11) && LocalClient(player)) SV_ExecuteClientCommand(LocalClient(player), PTR(1), qtrue, qfalse);
            return qtrue;
        case CG_SENDCLIENTCOMMAND:
            if (LocalClient(player)) SV_ExecuteClientCommand(LocalClient(player), PTR(1), qtrue, qfalse);
            return qtrue;
        case CG_CVAR_SET:
            if (!strcmp(PTR(1), "cg_recoilPitch")) { Cvar_Set(va("cg_recoilPitch%d", player + 1), PTR(2)); return qtrue; }
            break;
        case CG_CVAR_VARIABLESTRINGBUFFER:
            if (!strcmp(PTR(1), "cg_recoilPitch")) { Cvar_VariableStringBuffer(va("cg_recoilPitch%d", player + 1), PTR(2), args[3]); return qtrue; }
            break;
    }
    switch (args[0]) {
        case CG_GETGLCONFIG: {
            glconfig_t *config = PTR(1); *config = cls.glconfig; config->vidWidth = w; config->vidHeight = h;
            config->windowAspect = (float)w / h; return qtrue;
        }
        case CG_R_RENDERSCENE: {
            refdef_t view = *(refdef_t *)PTR(1); view.x += x; view.y += y; re.RenderScene(&view); return qtrue;
        }
        case CG_R_DRAWSTRETCHPIC:
            re.DrawStretchPic(FLOAT(1) + x, FLOAT(2) + y, FLOAT(3), FLOAT(4), FLOAT(5), FLOAT(6), FLOAT(7), FLOAT(8), args[9]); return qtrue;
        case CG_R_DRAWROTATEDPIC:
            re.DrawRotatedPic(FLOAT(1) + x, FLOAT(2) + y, FLOAT(3), FLOAT(4), FLOAT(5), FLOAT(6), FLOAT(7), FLOAT(8), args[9], FLOAT(10)); return qtrue;
        case CG_R_DRAWSTRETCHPIC_GRADIENT:
            re.DrawStretchPicGradient(FLOAT(1) + x, FLOAT(2) + y, FLOAT(3), FLOAT(4), FLOAT(5), FLOAT(6), FLOAT(7), FLOAT(8), args[9], PTR(10), args[11]); return qtrue;
        case CG_R_DRAW2DPOLYS:
            if (args[2] > 0 && args[2] <= 8192) RE_2DPolyiesOffset(PTR(1), args[2], args[3], x, y);
            return qtrue;
    }
    return qfalse;
    #undef PTR
    #undef FLOAT
}
