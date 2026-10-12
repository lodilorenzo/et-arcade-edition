/* ET: Legacy v2.86.0-compatible identity; see native/legacy_md5.c.
   The etkey is an identifier, not a password or authentication token. */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <time.h>
#include "../engine/src/client/client.h"

void ET_GUIDFromKey(const unsigned char key[28], char guid[33]);

static void CreateKey(char *path) {
    char buffer[29];
    HANDLE output;
    DWORD written;
    int random;
    time_t now = time(NULL);
    struct tm local;
    if (localtime_s(&local, &now) || BCryptGenRandom(NULL, (PUCHAR)&random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        Com_Printf("Could not generate ETKEY; cl_guid is unknown\n"); return;
    }
    /* Match Legacy's timestamp + abs(random) % 10000, without INT_MIN overflow. */
    Com_sprintf(buffer, sizeof(buffer), "0000001002%04d%02d%02d%02d%02d%02d%04d",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
        (int)((random < 0 ? -(int64_t)random : random) % 10000));
    FS_CreatePath(path);
    /* Never replace an existing identity, including a malformed/empty file. */
    output = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output != INVALID_HANDLE_VALUE) {
        qboolean ok = WriteFile(output, buffer, 28, &written, NULL) && written == 28 && FlushFileBuffers(output);
        CloseHandle(output);
        if (!ok) { DeleteFileA(path); Com_Printf("Could not write ETKEY\n"); }
        else Com_Printf("ETKEY file generated\n");
    }
}
void CL_InitGUID(void) {
    fileHandle_t file;
    unsigned char key[28];
    char guid[33], path[MAX_OSPATH];
    int length;
    Cvar_Get("cl_guid", "unknown", CVAR_ROM | CVAR_USERINFO | CVAR_NORESTART);
    Cvar_Set("cl_guid", "unknown");
    if (CL_OnlinePaneKeyPath(path, sizeof(path))) {
        DWORD read;
        HANDLE input = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (input == INVALID_HANDLE_VALUE && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) {
            CreateKey(path);
            input = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (input == INVALID_HANDLE_VALUE) { Com_Printf("Could not read online player ETKEY\n"); return; }
        length = ReadFile(input, key, 28, &read, NULL) && read == 28 ? 28 : 0;
        CloseHandle(input);
        if (length < 28) { Com_Printf("ETKEY missing or too short; preserving existing file, cl_guid is unknown\n"); return; }
    } else {
        length = FS_SV_FOpenFileRead("etmain/etkey", &file);
        if (!file) {
            Q_strncpyz(path, FS_BuildOSPath(Cvar_VariableString("fs_homepath"), "etmain", "etkey"), sizeof(path));
            CreateKey(path);
            length = FS_SV_FOpenFileRead("etmain/etkey", &file);
        }
        if (!file || length < 28 || FS_Read(key, 28, file) != 28) {
            if (file) FS_FCloseFile(file);
            Com_Printf("ETKEY missing or too short; preserving existing file, cl_guid is unknown\n"); return;
        }
        FS_FCloseFile(file);
    }
    ET_GUIDFromKey(key, guid); Cvar_Set("cl_guid", guid);
    Com_Printf("ETKEY identity loaded (Legacy-compatible cl_guid)\n");
}
