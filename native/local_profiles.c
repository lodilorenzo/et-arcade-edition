/* Local player profiles (game module). Each local player (1-4) may choose a profile in the
 * player-setup menu (cvar ui_localProfile<N> = profile id). In local matches the profile supplies the
 * player's name and its saved experience (skill points) and medals; progress is saved back on round /
 * map end, on leaving and every 30 seconds. Files: <game dir>/localprofiles/<id>.dat. Online and
 * dedicated servers are unaffected: the cvars only exist in this client's local session. */
#include "g_local.h"
#include "local_profiles.h"

static char profileOf[MAX_CLIENTS][MAX_QPATH]; /* Loaded profile id per client, "" = none. */
static float savedTotal[MAX_CLIENTS];
static int nextSave;

static qboolean LocalMatch(void) {
    return !g_dedicated.integer && trap_Cvar_VariableIntegerValue("ui_rustSlice");
}

/* 1..4 for this client's local player, 0 for bots and remote players. */
static int LocalSlot(int clientNum) {
    gentity_t *ent = g_entities + clientNum;
    char userinfo[MAX_INFO_STRING];
    int slot;
    if (!LocalMatch() || !ent->client || (ent->r.svFlags & SVF_BOT)) return 0;
    for (slot = 2; slot <= 4; ++slot)
        if (trap_Cvar_VariableIntegerValue(va("et_localClient%d", slot)) == clientNum) return slot;
    /* pers.localClient is only set later in ClientConnect: read the address itself. */
    trap_GetUserinfo(clientNum, userinfo, sizeof(userinfo));
    return !strcmp(Info_ValueForKey(userinfo, "ip"), "localhost") ? 1 : 0;
}

qboolean G_LocalProfileId(const char *id) {
    int i;
    if (!id[0] || strlen(id) >= 32) return qfalse;
    for (i = 0; id[i]; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_' || id[i] == '-')) return qfalse;
    return qtrue;
}

/* Profile file: name "..." / xp <7 floats> / medals <7 ints> / matches N. */
qboolean G_LocalProfileRead(const char *id, localProfile_t *profile) {
    fileHandle_t f;
    char text[4096], *p, *token;
    int length, i;
    memset(profile, 0, sizeof(*profile));
    if (!G_LocalProfileId(id)) return qfalse;
    length = trap_FS_FOpenFile(va("localprofiles/%s.dat", id), &f, FS_READ);
    if (length <= 0 || length >= (int)sizeof(text)) { if (length >= 0) trap_FS_FCloseFile(f); return qfalse; }
    trap_FS_Read(text, length, f); text[length] = 0; trap_FS_FCloseFile(f);
    p = text;
    while ((token = COM_Parse(&p))[0]) {
        if (!Q_stricmp(token, "name")) Q_strncpyz(profile->name, COM_Parse(&p), sizeof(profile->name));
        else if (!Q_stricmp(token, "xp")) for (i = 0; i < SK_NUM_SKILLS; ++i) profile->skillpoints[i] = (float)atof(COM_Parse(&p));
        else if (!Q_stricmp(token, "medals")) for (i = 0; i < SK_NUM_SKILLS; ++i) profile->medals[i] = atoi(COM_Parse(&p));
        else if (!Q_stricmp(token, "matches")) profile->matches = atoi(COM_Parse(&p));
    }
    for (i = 0; i < SK_NUM_SKILLS; ++i) if (!(profile->skillpoints[i] >= 0.f) || profile->skillpoints[i] > 1e7f) profile->skillpoints[i] = 0.f;
    if (!profile->name[0]) Q_strncpyz(profile->name, id, sizeof(profile->name));
    return qtrue;
}

static void Write(const char *id, const localProfile_t *profile) {
    fileHandle_t f;
    char text[1024], name[MAX_NETNAME];
    int i;
    Q_strncpyz(name, profile->name, sizeof(name));
    for (i = 0; name[i]; ++i) if (name[i] == '"' || name[i] == '\n' || name[i] == '\r') name[i] = ' ';
    Com_sprintf(text, sizeof(text), "name \"%s\"\nxp", name);
    for (i = 0; i < SK_NUM_SKILLS; ++i) Q_strcat(text, sizeof(text), va(" %.2f", profile->skillpoints[i]));
    Q_strcat(text, sizeof(text), "\nmedals");
    for (i = 0; i < SK_NUM_SKILLS; ++i) Q_strcat(text, sizeof(text), va(" %d", profile->medals[i]));
    Q_strcat(text, sizeof(text), va("\nmatches %d\n", profile->matches));
    if (trap_FS_FOpenFile(va("localprofiles/%s.dat", id), &f, FS_WRITE) < 0) return;
    trap_FS_Write(text, strlen(text), f);
    trap_FS_FCloseFile(f);
}

static float Total(const gclient_t *client) {
    float total = 0.f; int i;
    for (i = 0; i < SK_NUM_SKILLS; ++i) total += client->sess.skillpoints[i];
    return total;
}

/* After ClientConnect's session read: objective mode clears XP on every map and restart, so the
 * profile's saved total is restored each time (it is set, never added, so nothing is counted twice). */
void G_LocalProfileConnect(int clientNum) {
    gclient_t *client = g_entities[clientNum].client;
    char id[MAX_QPATH];
    localProfile_t profile;
    int slot = LocalSlot(clientNum), i;
    profileOf[clientNum][0] = 0;
    if (!slot) return;
    trap_Cvar_VariableStringBuffer(va("ui_localProfile%d", slot), id, sizeof(id));
    if (!G_LocalProfileRead(id, &profile)) return;
    Q_strncpyz(profileOf[clientNum], id, sizeof(profileOf[0]));
    for (i = 0; i < SK_NUM_SKILLS; ++i) {
        client->sess.skillpoints[i] = profile.skillpoints[i];
        client->sess.startskillpoints[i] = profile.skillpoints[i];
        client->sess.medals[i] = profile.medals[i];
    }
    client->sess.startxptotal = savedTotal[clientNum] = Total(client);
    G_CalcRank(client);
    G_Printf("LOCALPROFILE loaded player=%d client=%d profile=%s xp=%.0f\n", slot, clientNum, id, savedTotal[clientNum]);
}

/* ClientUserinfoChanged: a profiled local player plays under the profile's name. */
void G_LocalProfileName(int clientNum, char *netname, int size) {
    localProfile_t profile;
    if (profileOf[clientNum][0] && G_LocalProfileRead(profileOf[clientNum], &profile)) {
        char clean[MAX_NETNAME];
        Q_strncpyz(clean, profile.name, sizeof(clean));
        if (clean[0]) Q_strncpyz(netname, clean, size);
    }
}

static void Save(int clientNum, qboolean finishedMatch) {
    gclient_t *client = g_entities[clientNum].client;
    localProfile_t profile;
    int i;
    if (!profileOf[clientNum][0] || !client || !G_LocalProfileRead(profileOf[clientNum], &profile)) return;
    for (i = 0; i < SK_NUM_SKILLS; ++i) { profile.skillpoints[i] = client->sess.skillpoints[i]; profile.medals[i] = client->sess.medals[i]; }
    if (finishedMatch) ++profile.matches;
    Write(profileOf[clientNum], &profile);
    savedTotal[clientNum] = Total(client);
    G_Printf("LOCALPROFILE saved client=%d profile=%s xp=%.0f matches=%d\n", clientNum, profileOf[clientNum], savedTotal[clientNum], profile.matches);
}

void G_LocalProfileDisconnect(int clientNum) {
    if (clientNum < 0 || clientNum >= MAX_CLIENTS) return;
    Save(clientNum, qfalse);
    profileOf[clientNum][0] = 0;
}

/* finishedMatch: intermission (counts a played match); otherwise map end/restart. */
void G_LocalProfileSaveAll(qboolean finishedMatch) {
    int i;
    for (i = 0; i < level.maxclients; ++i)
        if (profileOf[i][0] && g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED) Save(i, finishedMatch);
}

/* Every 30 seconds while experience changed: progress survives a crash or a closed window. */
void G_LocalProfileFrame(void) {
    int i;
    if (level.time < nextSave) return;
    nextSave = level.time + 30000;
    for (i = 0; i < level.maxclients; ++i)
        if (profileOf[i][0] && g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED &&
            Total(g_entities[i].client) != savedTotal[i]) Save(i, qfalse);
}

void G_LocalProfileInit(void) {
    memset(profileOf, 0, sizeof(profileOf));
    nextSave = 0;
}

/* Regression hook (local match + ui_rustMenuTest): localprofile_test xp <client> <skill> <points> | state. */
void G_LocalProfileTest(void) {
    char arg[MAX_TOKEN_CHARS];
    int i;
    if (!LocalMatch() || !trap_Cvar_VariableIntegerValue("ui_rustMenuTest")) return; /* developer resets on round restarts. */
    trap_Argv(1, arg, sizeof(arg));
    if (!Q_stricmp(arg, "xp") && trap_Argc() == 5) {
        int clientNum, skill;
        trap_Argv(2, arg, sizeof(arg)); clientNum = atoi(arg);
        trap_Argv(3, arg, sizeof(arg)); skill = atoi(arg);
        trap_Argv(4, arg, sizeof(arg));
        if (clientNum >= 0 && clientNum < level.maxclients && g_entities[clientNum].client && skill >= 0 && skill < SK_NUM_SKILLS)
            G_AddSkillPoints(g_entities + clientNum, skill, (float)atof(arg));
    }
    for (i = 0; i < level.maxclients; ++i) if (g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED)
        G_Printf("LOCALPROFILE_TEST client=%d slot=%d profile=%s name=%s xp=%.0f\n", i, LocalSlot(i), profileOf[i],
                 g_entities[i].client->pers.netname, Total(g_entities[i].client));
}
