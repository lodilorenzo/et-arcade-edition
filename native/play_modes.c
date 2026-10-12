/* One source-built engine, a local shell, server-selected modules while online. */
#include "../engine/src/client/client.h"
#include "../engine/src/qcommon/unzip.h"
#include "../engine/src/win32/win_local.h"

static const char *videoNames[] = {
    "r_mode", "r_customwidth", "r_customheight", "r_customaspect", "r_fullscreen", "r_displayRefresh"
};
static char videoValues[6][MAX_CVAR_VALUE_STRING];
static qboolean videoSaved;

void CL_SavePlayVideo(void) {
    int i;
    if (!Cvar_VariableIntegerValue("et_unified") ||
        Q_stricmp(Cvar_VariableString("fs_game"), "et-rust")) return;
    for (i = 0; i < 6; ++i)
        Cvar_LatchedVariableStringBuffer(videoNames[i], videoValues[i], sizeof(videoValues[i]));
    videoSaved = qtrue;
}

void CL_RestorePlayVideo(void) {
    int i;
    if (CL_OnlinePane()) { CL_OnlinePaneVideo(); return; }
    if (!Cvar_VariableIntegerValue("et_unified")) return;
    /* ui_rustSlice is archived, but mode ownership is not a profile preference. */
    Cvar_Set("ui_rustSlice", !Q_stricmp(Cvar_VariableString("fs_game"), "et-rust") ? "1" : "0");
    if (videoSaved) for (i = 0; i < 6; ++i) Cvar_Set(videoNames[i], videoValues[i]); /* Also discard foreign latched values. */
}

void CL_RestorePlayMenu(void) {
    if (CL_OnlinePane() || !Cvar_VariableIntegerValue("et_unified") || cls.state != CA_DISCONNECTED ||
        com_sv_running->integer || cls.bWWWDlDisconnected ||
        !Q_stricmp(Cvar_VariableString("fs_game"), "et-rust")) return;
    Cvar_Set("fs_game", "et-rust");
    Cvar_Set("ui_rustSlice", "1");
    Cvar_Set("ui_connecting", "0");
    Cvar_Set("net_ip", "0.0.0.0");
    NET_Restart();
    CL_RestorePlayVideo();
    CL_Vid_Restart_f();
    VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN);
    Com_Printf("PLAYMODE returned to unified menu\n");
}

/* Unpure stock servers load loose etmain modules (pure ones are extracted by Sys_LoadDll).
 * Copy them once from the player's original mp_bin.pk3; never from downloaded content. */
void CL_ExtractStockModules(void) {
    static const char *names[] = {"cgame_mp_x86.dll", "ui_mp_x86.dll"};
    char pak[MAX_OSPATH], path[MAX_OSPATH], temporary[MAX_OSPATH];
    unzFile zip;
    int i;
    if (!Cvar_VariableIntegerValue("et_unified") || CL_OnlinePane()) return;
    Q_strncpyz(pak, FS_BuildOSPath(Cvar_VariableString("fs_basepath"), BASEGAME, "mp_bin.pk3"), sizeof(pak));
    if (!(zip = unzOpen(pak))) return;
    for (i = 0; i < 2; ++i) {
        unz_file_info info;
        unsigned char *data;
        FILE *file;
        qboolean ok;
        Q_strncpyz(path, FS_BuildOSPath(Cvar_VariableString("fs_homepath"), BASEGAME, names[i]), sizeof(path));
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) continue;
        if (unzLocateFile(zip, names[i], 2) != UNZ_OK || unzGetCurrentFileInfo(zip, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
            !info.uncompressed_size || info.uncompressed_size > 16 << 20 || unzOpenCurrentFile(zip) != UNZ_OK) continue;
        data = Z_Malloc(info.uncompressed_size);
        ok = unzReadCurrentFile(zip, data, info.uncompressed_size) == (int)info.uncompressed_size;
        unzCloseCurrentFile(zip);
        Com_sprintf(temporary, sizeof(temporary), "%s.tmp", path);
        FS_CreatePath(path);
        ok = ok && (file = fopen(temporary, "wb")) != NULL;
        if (ok) { ok = fwrite(data, 1, info.uncompressed_size, file) == info.uncompressed_size; ok = !fclose(file) && ok; }
        ok = ok && MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING);
        if (!ok) DeleteFileA(temporary);
        Z_Free(data);
        Com_Printf("PLAYMODE stock %s %s\n", names[i], ok ? "extracted" : "extraction failed");
    }
    unzClose(zip);
}

/* Resolution selector: the primary monitor's distinct modes (>= 640x480, 32-bit), largest first, and
 * its desktop mode. Test fixtures may substitute a fixed list (ui_rustDisplayModesTest). */
void CL_PublishDisplayModes(void) {
    DEVMODEA mode;
    char list[MAX_CVAR_VALUE_STRING] = "", entry[24];
    int sizes[128][2], n = 0, i, j;
    DWORD index;
    memset(&mode, 0, sizeof(mode)); mode.dmSize = sizeof(mode);
    for (index = 0; n < 128 && EnumDisplaySettingsA(NULL, index, &mode); ++index) {
        int w = (int)mode.dmPelsWidth, h = (int)mode.dmPelsHeight;
        if (w < 640 || h < 480 || mode.dmBitsPerPel < 32) continue;
        for (i = 0; i < n && (sizes[i][0] != w || sizes[i][1] != h); ++i) {}
        if (i == n) { sizes[n][0] = w; sizes[n][1] = h; ++n; }
    }
    for (i = 1; i < n; ++i) for (j = i; j > 0; --j) {  /* Largest area first, then wider first. */
        int *a = sizes[j - 1], *b = sizes[j], t;
        if ((double)a[0] * a[1] > (double)b[0] * b[1] || ((double)a[0] * a[1] == (double)b[0] * b[1] && a[0] >= b[0])) break;
        t = a[0]; a[0] = b[0]; b[0] = t; t = a[1]; a[1] = b[1]; b[1] = t;
    }
    for (i = 0; i < n; ++i) {
        Com_sprintf(entry, sizeof(entry), "%s%dx%d", *list ? " " : "", sizes[i][0], sizes[i][1]);
        if (strlen(list) + strlen(entry) >= sizeof(list)) break;  /* Cvar size: the smallest modes drop off. */
        Q_strcat(list, sizeof(list), entry);
    }
    if (Cvar_VariableIntegerValue("ui_rustMenuTest") && *Cvar_VariableString("ui_rustDisplayModesTest"))
        Q_strncpyz(list, Cvar_VariableString("ui_rustDisplayModesTest"), sizeof(list));
    Cvar_Set("ui_rustDisplayModes", list);
    memset(&mode, 0, sizeof(mode)); mode.dmSize = sizeof(mode);
    Cvar_Set("ui_rustDesktopMode", EnumDisplaySettingsA(NULL, ENUM_REGISTRY_SETTINGS, &mode) ?
        va("%lux%lu", mode.dmPelsWidth, mode.dmPelsHeight) : "");
    Com_DPrintf("Display modes: %s\n", list);
}
