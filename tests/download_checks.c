/* Run the actual downloader against the HTTP fixture in download_checks.py. */
#undef NDEBUG
#include <assert.h>
#include <windows.h>
#include <stdio.h>
#include "../engine/src/game/q_shared.h"
#include "../engine/src/qcommon/qcommon.h"
#include "../engine/src/qcommon/dl_public.h"

void Cvar_Set(const char *name, const char *value) { (void)name; (void)value; }
void Cvar_SetValue(const char *name, float value) { (void)name; assert(value >= 0); }
void QDECL Com_Printf(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args);
}
int Q_stricmpn(const char *a, const char *b, int n) { return _strnicmp(a, b, n); }
int FS_CreatePath(const char *path) { (void)path; return 0; }

int main(int argc, char **argv) {
    dlStatus_t status;
    DWORD started;
    assert(argc == 4);
    assert(!DL_BeginDownload(argv[2], "file:///etc/passwd", 0));
    assert(!DL_BeginDownload(argv[2], "ftp://localhost/file", 0));
    assert(DL_BeginDownload(argv[2], argv[1], 0));
    assert(!DL_BeginDownload(argv[2], argv[1], 0));
    if (!strcmp(argv[3], "cancel")) {
        DL_Shutdown();
        assert(DL_DownloadLoop() == DL_DONE);
    } else {
        started = GetTickCount();
        do {
            status = DL_DownloadLoop();
            assert(GetTickCount() - started < 45000);
            Sleep(1);
        } while (status == DL_CONTINUE);
        assert(status == (!strcmp(argv[3], "ok") ? DL_DONE : DL_FAILED));
    }
    DL_Shutdown(); DL_Shutdown();
    puts("PASS native download");
    return 0;
}
