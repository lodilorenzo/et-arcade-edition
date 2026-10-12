/* Private compatibility glue for ET: Legacy's Omni-bot adapter over stock ET 2.60.
   No native weapon IDs, wire structures or gameplay rules are changed. */
#include <cassert>
#define Com_Memset memset
#define WP_FG42_SCOPE WP_FG42SCOPE
#define GAME_INIT_FRAMES 6
/* Unreachable ET:L-only switch labels. Bot IDs are mapped to stock weapons. */
static constexpr weapon_t WP_BAZOOKA = (weapon_t)-100, WP_MORTAR2 = (weapon_t)-101,
    WP_MORTAR2_SET = (weapon_t)-102, WP_MOBILE_BROWNING = (weapon_t)-103,
    WP_MOBILE_BROWNING_SET = (weapon_t)-104, WP_KNIFE_KABAR = (weapon_t)-105,
    WP_MP34 = (weapon_t)-106, WP_AIRSTRIKE = (weapon_t)-107;
#define SK_SOLDIER_PROJECTILE_STAMINA 1
#define SK_COVERTOPS_STAMINA 2
#define SK_ENGINEER_PLIERS_DEXTERITY 2
#define SK_ENGINEER_STAMINA 3
#define SK_MEDIC_RESOURCES 2
#define SK_FIELDOPS_RESOURCES 1
#define SK_FIELDOPS_FIRE_SUPPORT_STAMINA 2
#define BG_IsSkillAvailable(skills, category, level) ((skills)[category] >= (level))
struct OmniWeaponInfo { int ammoIndex, clipIndex, maxClip, maxHeat; };
static const OmniWeaponInfo *GetWeaponTableData(int weapon) {
    static OmniWeaponInfo data;
    if (weapon < WP_NONE || weapon >= WP_NUM_WEAPONS) weapon = WP_NONE;
    const ammotable_t *native = GetAmmoTableData(weapon);
    data = { BG_FindAmmoForWeapon((weapon_t)weapon), BG_FindClipForWeapon((weapon_t)weapon), native->maxclip, native->maxHeat };
    return &data;
}
static qboolean G_IsWeaponDisabled(gentity_t *ent, int weapon) {
    return (weapon < WP_NONE || weapon >= WP_NUM_WEAPONS || ::G_IsWeaponDisabled(ent, (weapon_t)weapon)) ? qtrue : qfalse;
}
static qboolean OmniWeaponDisabledForTeam(gentity_t *ent, weapon_t weapon, team_t team) {
    // Stock ET rejects every weapon for spectators, including pre-join loadout queries.
    const team_t saved = ent->client->sess.sessionTeam;
    ent->client->sess.sessionTeam = team;
    const qboolean disabled = ::G_IsWeaponDisabled(ent, weapon);
    ent->client->sess.sessionTeam = saved;
    return disabled;
}
static void Cmd_Kill_f(gentity_t *ent, int, int) { ::Cmd_Kill_f(ent); }
static qboolean botSuicide[MAX_CLIENTS];
static qboolean twoMinuteWarning, thirtySecondWarning;
