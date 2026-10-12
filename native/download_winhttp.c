/* Windows HTTP/HTTPS downloads. Only the main thread touches engine state. */
#include <windows.h>
#include <winhttp.h>
#include <process.h>
#include <stdio.h>
#include <wchar.h>
#include <limits.h>
#include "../engine/src/game/q_shared.h"
#include "../engine/src/qcommon/qcommon.h"
#include "../engine/src/qcommon/dl_public.h"

static HANDLE worker, cancel;
static FILE *output;
static wchar_t url[MAX_STRING_CHARS];
static volatile LONG downloaded;
static DWORD failure;

static unsigned __stdcall Download(void *unused) {
    URL_COMPONENTS parts = {sizeof(parts)};
    HINTERNET session = NULL, connection = NULL, request = NULL;
    wchar_t host[MAX_STRING_CHARS], path[MAX_STRING_CHARS];
    DWORD status, size = sizeof(status), received, expected = 0, policy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    BOOL hasLength;
    char buffer[16384];
    LONG total = 0;
    (void)unused;
    failure = ERROR_INVALID_PARAMETER;
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
    parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, 0, 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwHostNameLength >= MAX_STRING_CHARS ||
        parts.dwUrlPathLength + parts.dwExtraInfoLength >= MAX_STRING_CHARS) goto done;
    wmemcpy(host, parts.lpszHostName, parts.dwHostNameLength);
    host[parts.dwHostNameLength] = 0;
    wmemcpy(path, parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) wmemcpy(path + parts.dwUrlPathLength, parts.lpszExtraInfo, parts.dwExtraInfoLength);
    path[parts.dwUrlPathLength + parts.dwExtraInfoLength] = 0;
    session = WinHttpOpen(L"EnemyTerritory/2.60", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto failed;
    if (!WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000)) goto failed;
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connection) goto failed;
    request = WinHttpOpenRequest(connection, L"GET", *path ? path : L"/", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request || !WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &policy, sizeof(policy))) goto failed;
    policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) goto failed;
    /* WinHTTP's default certificate validation stays enabled. */
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) goto failed;
    if (status != 200) { failure = ERROR_WINHTTP_INVALID_SERVER_RESPONSE; goto done; }
    size = sizeof(expected);
    hasLength = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                   WINHTTP_HEADER_NAME_BY_INDEX, &expected, &size, WINHTTP_NO_HEADER_INDEX);
    for (;;) {
        if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) { failure = ERROR_CANCELLED; goto done; }
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &received)) goto failed;
        if (!received) break;
        /* ponytail: 2 GiB per PK3; widen the progress counter if larger assets become necessary. */
        if (received > (DWORD)(LONG_MAX - total)) { failure = ERROR_FILE_TOO_LARGE; goto done; }
        if (fwrite(buffer, 1, received, output) != received) { failure = ERROR_WRITE_FAULT; goto done; }
        total += received;
        InterlockedExchange(&downloaded, total);
    }
    failure = hasLength && (DWORD)total != expected ? ERROR_HANDLE_EOF : ERROR_SUCCESS;
    goto done;
failed:
    failure = GetLastError();
done:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (fclose(output) && !failure) failure = ERROR_WRITE_FAULT;
    output = NULL;
    return 0;
}

void DL_InitDownload(void) { }

void DL_Shutdown(void) {
    if (!worker) return;
    SetEvent(cancel);
    /* Synchronous WinHTTP operations have bounded timeouts; never abandon a writer. */
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker); CloseHandle(cancel);
    worker = cancel = NULL;
}

int DL_BeginDownload(const char *localName, const char *remoteName, int debug) {
    (void)debug;
    if (worker || !localName || !*localName || !remoteName ||
        (Q_stricmpn(remoteName, "http://", 7) && Q_stricmpn(remoteName, "https://", 8)) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, remoteName, -1, url, MAX_STRING_CHARS)) return 0;
    FS_CreatePath(localName);
    output = fopen(localName, "wb");
    if (!output) return 0;
    cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
    InterlockedExchange(&downloaded, 0);
    failure = ERROR_SUCCESS;
    if (cancel) worker = (HANDLE)_beginthreadex(NULL, 0, Download, NULL, 0, NULL);
    if (!worker) {
        if (cancel) CloseHandle(cancel);
        cancel = NULL;
        fclose(output); output = NULL;
        return 0;
    }
    Cvar_Set("cl_downloadName", remoteName);
    return 1;
}

dlStatus_t DL_DownloadLoop(void) {
    DWORD error;
    if (!worker) return DL_DONE;
    Cvar_SetValue("cl_downloadCount", (float)InterlockedCompareExchange(&downloaded, 0, 0));
    if (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return DL_CONTINUE;
    error = failure;
    DL_Shutdown();
    Cvar_Set("ui_dl_running", "0");
    if (error) Com_Printf("HTTP download failed (Windows error %lu)\n", error);
    return error ? DL_FAILED : DL_DONE;
}
