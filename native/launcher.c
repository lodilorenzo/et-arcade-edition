/* Portable launcher (Play-ET.exe). Everything lives next to this executable:
 * it checks the player's original ET 2.60b files (copying them from a chosen
 * installation if missing), seeds first-run video from the current monitor,
 * writes autoexec.cfg and starts et.exe in unified mode. Static settings are
 * generated into play-*.cfg by tools/package.py. Extra arguments go to et.exe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* The exact files from the 2.60b release; pure servers check the same contents. */
static const struct { const wchar_t *name; LONGLONG size; const char *sha256; } assets[] = {
    {L"pak0.pk3", 228138631, "712966b20e06523fe81419516500e499c86b2b4fec823856ddbd333fcb3d26e5"},
    {L"pak1.pk3", 51616, "5610fd749024405b4425a7ce6397e58187b941d22092ef11d4844b427df53e5d"},
    {L"pak2.pk3", 89910, "a48ab749a1a12ab4d9137286b1f23d642c29da59845b2bafc8f64e052cf06f3e"},
    {L"mp_bin.pk3", 1638102, "cf0a7ce662421c766f93cc196841849eb66905b047d209dd5f3ed0b1396cd42e"},
};
#define ASSETS (sizeof(assets) / sizeof(assets[0]))
static const wchar_t *title = L"Enemy Territory: Arcade Edition";
static wchar_t root[MAX_PATH];
static char rootA[MAX_PATH]; /* The engine uses ANSI paths. */
static BOOL quiet; /* --import: no dialogs (scripts/tests); messages still go to launcher.log. */

static void Report(UINT icon, const wchar_t *format, ...) {
    wchar_t text[4096], path[MAX_PATH];
    char utf8[8192];
    va_list args;
    FILE *log;
    va_start(args, format); _vsnwprintf_s(text, 4096, _TRUNCATE, format, args); va_end(args);
    swprintf_s(path, MAX_PATH, L"%s\\launcher.log", root);
    if (*root && !_wfopen_s(&log, path, L"ab") && log) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
        fprintf(log, "%s\r\n", utf8); fclose(log);
    }
    if (!quiet) MessageBoxW(NULL, text, title, icon);
}

static BOOL Exists(const wchar_t *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }
static LONGLONG Size(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data) || data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return (LONGLONG)data.nFileSizeHigh << 32 | data.nFileSizeLow;
}
static BOOL Sha256(const wchar_t *path, char hex[65]) {
    BCRYPT_ALG_HANDLE algorithm = NULL; BCRYPT_HASH_HANDLE hash = NULL;
    static unsigned char buffer[1 << 20];
    unsigned char digest[32]; DWORD got; BOOL ok = FALSE; int i;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) && !BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0)) {
        while ((ok = ReadFile(file, buffer, sizeof(buffer), &got, NULL)) && got)
            if (BCryptHashData(hash, buffer, got, 0)) { ok = FALSE; break; }
        ok = ok && !BCryptFinishHash(hash, digest, sizeof(digest), 0);
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    for (i = 0; ok && i < 32; ++i) sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    return ok;
}
/* Fast startup check: size only. Contents were verified when the files were imported. */
static BOOL AssetsPresent(void) {
    wchar_t path[MAX_PATH]; unsigned i;
    for (i = 0; i < ASSETS; ++i) {
        swprintf_s(path, MAX_PATH, L"%s\\etmain\\%s", root, assets[i].name);
        if (Size(path) != assets[i].size) return FALSE;
    }
    return TRUE;
}
/* Accepts the installation folder or its etmain folder. Copies only verified files. */
static BOOL Import(const wchar_t *source) {
    wchar_t found[ASSETS][MAX_PATH], problems[2048] = L"", path[MAX_PATH], temporary[MAX_PATH];
    char hex[65]; unsigned i;
    for (i = 0; i < ASSETS; ++i) {
        swprintf_s(found[i], MAX_PATH, L"%s\\etmain\\%s", source, assets[i].name);
        if (!Exists(found[i])) swprintf_s(found[i], MAX_PATH, L"%s\\%s", source, assets[i].name);
        if (!Exists(found[i])) { wcscat_s(problems, 2048, L"\n  missing: "); wcscat_s(problems, 2048, assets[i].name); }
        else if (Size(found[i]) != assets[i].size || !Sha256(found[i], hex) || strcmp(hex, assets[i].sha256)) {
            wcscat_s(problems, 2048, L"\n  not the 2.60b version: "); wcscat_s(problems, 2048, assets[i].name);
        }
    }
    if (*problems) {
        Report(MB_ICONWARNING, L"These Enemy Territory files could not be used from\n%s\n%s\n\n"
            L"Choose the folder where Wolfenstein: Enemy Territory 2.60b is installed "
            L"(the one containing etmain), or update it to 2.60b.", source, problems);
        return FALSE;
    }
    for (i = 0; i < ASSETS; ++i) {
        swprintf_s(path, MAX_PATH, L"%s\\etmain\\%s", root, assets[i].name);
        swprintf_s(temporary, MAX_PATH, L"%s.tmp", path);
        if (!CopyFileW(found[i], temporary, FALSE) || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(temporary);
            Report(MB_ICONERROR, L"Could not copy %s into\n%s\\etmain\n(Windows error %lu).", assets[i].name, root, GetLastError());
            return FALSE;
        }
    }
    Report(MB_ICONINFORMATION, L"Copied the original Enemy Territory files from\n%s", source);
    return TRUE;
}
static void DetectInstall(wchar_t *path, DWORD size) {
    static const wchar_t *keys[] = {L"SOFTWARE\\WOW6432Node\\Activision\\Wolfenstein - Enemy Territory",
                                    L"SOFTWARE\\Activision\\Wolfenstein - Enemy Territory"};
    DWORD bytes; unsigned i;
    for (i = 0; i < 2; ++i) {
        bytes = size * sizeof(wchar_t);
        if (!RegGetValueW(HKEY_LOCAL_MACHINE, keys[i], L"InstallPath", RRF_RT_REG_SZ, NULL, path, &bytes) && Exists(path)) return;
    }
    *path = 0;
}
static BOOL PickFolder(wchar_t *out, DWORD size) {
    IFileOpenDialog *dialog; IShellItem *item; PWSTR picked; wchar_t detected[MAX_PATH];
    DWORD options; BOOL ok = FALSE;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dialog))) return FALSE;
    IFileOpenDialog_GetOptions(dialog, &options);
    IFileOpenDialog_SetOptions(dialog, options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    IFileOpenDialog_SetTitle(dialog, L"Select your Wolfenstein: Enemy Territory folder");
    DetectInstall(detected, MAX_PATH);
    if (*detected && SUCCEEDED(SHCreateItemFromParsingName(detected, NULL, &IID_IShellItem, (void **)&item))) {
        IFileOpenDialog_SetFolder(dialog, item); IShellItem_Release(item);
    }
    if (SUCCEEDED(IFileOpenDialog_Show(dialog, NULL)) && SUCCEEDED(IFileOpenDialog_GetResult(dialog, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &picked))) {
            ok = !wcscpy_s(out, size, picked); CoTaskMemFree(picked);
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dialog);
    return ok;
}

/* Mirrors the engine: profiles/defaultprofile.dat selects the profile's etconfig.cfg. */
static void ProfileConfig(const wchar_t *folder, wchar_t *config) {
    wchar_t path[MAX_PATH], name[64]; char text[128] = ""; FILE *file; size_t n = 0; char *start, *end;
    swprintf_s(config, MAX_PATH, L"%s\\etconfig.cfg", folder);
    swprintf_s(path, MAX_PATH, L"%s\\profiles\\defaultprofile.dat", folder);
    if (_wfopen_s(&file, path, L"rb") || !file) return;
    n = fread(text, 1, sizeof(text) - 1, file); fclose(file); text[n] = 0;
    start = text + strspn(text, " \t\r\n");
    if (*start++ != '"' || !(end = strchr(start, '"')) || end == start || end - start >= 64) return;
    *end = 0;
    if (strpbrk(start, "/\\:\r\n") || !strcmp(start, ".") || !strcmp(start, "..")) return;
    MultiByteToWideChar(CP_ACP, 0, start, -1, name, 64);
    swprintf_s(config, MAX_PATH, L"%s\\profiles\\%s\\etconfig.cfg", folder, name);
}
static BOOL Contains(const wchar_t *path, const char *needle) {
    static char text[1 << 20]; FILE *file; size_t n;
    if (_wfopen_s(&file, path, L"rb") || !file) return FALSE;
    n = fread(text, 1, sizeof(text) - 1, file); fclose(file); text[n] = 0;
    return strstr(text, needle) != NULL;
}
/* Saved settings stay the player's; first run adopts this monitor's mode. */
static BOOL WriteAutoexec(const wchar_t *game, const DEVMODEW *mode) {
    wchar_t folder[MAX_PATH], config[MAX_PATH], path[MAX_PATH], root_config[MAX_PATH];
    double aspect = mode->dmPelsWidth / (double)mode->dmPelsHeight;
    FILE *file; BOOL ok;
    swprintf_s(folder, MAX_PATH, L"%s\\%s", root, game);
    ProfileConfig(folder, config);
    swprintf_s(root_config, MAX_PATH, L"%s\\etconfig.cfg", folder);
    swprintf_s(path, MAX_PATH, L"%s\\autoexec.cfg", folder);
    if (_wfopen_s(&file, path, L"wb") || !file) return FALSE;
    /* Native ET only loads profile configs; a profile-less root config needs an explicit exec. */
    if (!wcscmp(config, root_config) && Exists(config)) fprintf(file, "exec etconfig.cfg\n");
    fprintf(file, "exec play-settings.cfg\n");
    if (!Contains(config, "seta et_rustVideoVersion \"1\"")) {
        /* Keep the original 4:3 / 90-degree vertical view on any aspect ratio. */
        fprintf(file, "exec play-video.cfg\nset r_mode \"-1\"\nset r_customwidth \"%lu\"\nset r_customheight \"%lu\"\n"
            "set r_customaspect \"%f\"\nset r_displayRefresh \"%lu\"\nset cg_fov \"%f\"\nseta et_rustVideoVersion \"1\"\n",
            mode->dmPelsWidth, mode->dmPelsHeight, aspect, mode->dmDisplayFrequency, 2 * atan(aspect * .75) * 180 / 3.14159265358979);
    }
    if (!wcscmp(game, L"et-rust"))
        fprintf(file, "set omnibot_path \"%s\\omnibot\"\nset ui_rustMonitorWidth \"%lu\"\nset ui_rustMonitorHeight \"%lu\"\n"
            "set ui_rustMonitorRefresh \"%lu\"\n", rootA, mode->dmPelsWidth, mode->dmPelsHeight, mode->dmDisplayFrequency);
    ok = !ferror(file);
    return !fclose(file) && ok;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR line, int show) {
    wchar_t **argv, *slash, path[MAX_PATH], source[MAX_PATH] = L"", *command;
    const wchar_t *import = NULL;
    size_t length = 0;
    int argc, i, first = 1;
    BOOL lossy = FALSE;
    DEVMODEW mode = {0};
    STARTUPINFOW startup = {sizeof(startup)}; PROCESS_INFORMATION process; DWORD code = 1; HANDLE probe;
    char commandA[2048];

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && !wcscmp(argv[1], L"--import")) { import = argv[2]; quiet = TRUE; first = 3; }
    i = GetModuleFileNameW(NULL, root, MAX_PATH);
    if (!i || i >= MAX_PATH - 64 || !(slash = wcsrchr(root, L'\\'))) { Report(MB_ICONERROR, L"The game folder path is too long."); return 1; }
    *slash = 0;
    /* ET's command line/config parser and ANSI file APIs need a plain path. */
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, root, -1, rootA, MAX_PATH, NULL, &lossy) || lossy || wcspbrk(root, L"+\";%")) {
        *root = 0; /* No log inside an unusable folder. */
        Report(MB_ICONERROR, L"Move this game folder to a path with only plain letters, digits and spaces "
            L"(no + ; %% or quotes), for example C:\\Games\\Enemy Territory.");
        return 1;
    }
    swprintf_s(path, MAX_PATH, L"%s\\etmain", root);
    CreateDirectoryW(path, NULL);
    swprintf_s(path, MAX_PATH, L"%s\\etmain\\.write-test", root);
    probe = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (probe == INVALID_HANDLE_VALUE) {
        *root = 0;
        Report(MB_ICONERROR, L"This portable game stores settings, downloads and profiles in its own folder, "
            L"but the folder is read-only. Move it somewhere writable (not Program Files), for example C:\\Games.");
        return 1;
    }
    CloseHandle(probe);
    swprintf_s(path, MAX_PATH, L"%s\\et.exe", root);
    if (!Exists(path)) { Report(MB_ICONERROR, L"et.exe is missing from\n%s\nExtract the complete package again.", root); return 1; }

    if (import && !Import(import)) return 2;
    if (!AssetsPresent()) {
        if (quiet) { Report(MB_ICONERROR, L"Original Enemy Territory files are missing from %s\\etmain.", root); return 2; }
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        do {
            if (MessageBoxW(NULL, L"This game uses the data files of the original Wolfenstein: Enemy Territory 2.60b "
                L"(freeware). Four files are needed from its etmain folder:\n\n"
                L"    pak0.pk3   pak1.pk3   pak2.pk3   mp_bin.pk3\n\n"
                L"You can copy them yourself into the etmain folder next to Play-ET.exe, "
                L"or select your Enemy Territory folder now and they will be copied for you.\n\n"
                L"Select your Enemy Territory folder now?", title, MB_ICONINFORMATION | MB_YESNO) != IDYES ||
                !PickFolder(source, MAX_PATH)) return 2;
        } while (!Import(source) || !AssetsPresent());
    }

    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode) || mode.dmPelsWidth < 640 || mode.dmPelsHeight < 480) {
        mode.dmPelsWidth = 1280; mode.dmPelsHeight = 720; mode.dmDisplayFrequency = 60;
    }
    if (mode.dmDisplayFrequency <= 1) mode.dmDisplayFrequency = 0; /* "Hardware default": use the desktop rate. */
    if (!WriteAutoexec(L"et-rust", &mode) || !WriteAutoexec(L"etmain", &mode)) {
        Report(MB_ICONERROR, L"Could not write the game settings in\n%s", root); return 1;
    }

    /* Same arguments as the development launcher; extra launcher arguments follow. */
    for (i = first; i < argc; ++i) length += wcslen(argv[i]) + 3;
    command = calloc(length + 2 * MAX_PATH + 256, sizeof(wchar_t));
    if (!command) return 1;
    swprintf_s(command, length + 2 * MAX_PATH + 256, L"\"%s\\et.exe\" +set fs_basepath \"%s\" +set fs_homepath \"%s\" +set fs_game et-rust +set et_unified 1",
        root, root, root);
    for (i = first; i < argc; ++i) {
        wcscat_s(command, length + 2 * MAX_PATH + 256, wcschr(argv[i], L' ') ? L" \"" : L" ");
        wcscat_s(command, length + 2 * MAX_PATH + 256, argv[i]);
        if (wcschr(argv[i], L' ')) wcscat_s(command, length + 2 * MAX_PATH + 256, L"\"");
    }
    /* Stock WinMain keeps only 1023 bytes after the executable name. */
    if (!WideCharToMultiByte(CP_ACP, 0, wcschr(command + 1, L'"') + 1, -1, commandA, sizeof(commandA), NULL, NULL) || strlen(commandA) > 1023) {
        Report(MB_ICONERROR, L"The game folder path is too long; move it to a shorter path such as C:\\Games\\ET."); return 1;
    }
    swprintf_s(path, MAX_PATH, L"%s\\et.exe", root);
    if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, root, &startup, &process)) {
        Report(MB_ICONERROR, L"Could not start et.exe (Windows error %lu).", GetLastError()); return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}
