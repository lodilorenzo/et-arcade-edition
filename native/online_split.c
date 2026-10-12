/* Windows online split host: 2 side-by-side or 4 (2x2) instances of the SAME
 * source-built executable, one per local player (cl_splitScreen).
 * ponytail: isolated processes keep pure DLLs/downloads/network globals intact;
 * a single-process client refactor is justified only if memory cost matters.
 * This is NOT a sandbox for downloaded native mods. */
#include "../engine/src/client/client.h"
#include "../engine/src/win32/win_local.h"
#include <stdint.h>
#include <errno.h>

#define HOST_CLASS "ET Online Split"
#define PANE_STATE (WM_APP + 19)
#define PANE_WINDOW (WM_APP + 20)
#define PANE_FRAME (WM_APP + 21)
/* Host -> player 1: relative mouse motion (w=dx, l=dy) and mouse keys (w=key, l=down). */
#define PANE_MOUSE (WM_APP + 22)
#define PANE_KEY (WM_APP + 23)
/* Host lost focus: release keys whose key-up went to another window (e.g. Alt after Alt-Tab). */
#define PANE_CLEAR (WM_APP + 24)
/* Read-only localhost regression report; avoids console-key injection while testing real host input. */
#define PANE_REPORT (WM_APP + 25)
static void PaneReport(qboolean render);
static HWND host, panes[4];
/* Player-setup choices for this session: controller per view, keyboard/mouse view (0-based). */
static int padFor[4], keyboardPane;
static qboolean keyboardInput;
static HANDLE mappings[4], locks[4];
static unsigned char *pixels[4];
static HANDLE paneMapping[2], paneLock[2];
static unsigned char *panePixels[2];
static qboolean pixelLocked, mapFrame;

static void BufferName(char *name, int size, DWORD owner, HWND window, int slot, const char *kind) {
    Com_sprintf(name, size, "Local\\ETSplit_%lu_%lu_%d_%s", (unsigned long)owner, (unsigned long)(uintptr_t)window, slot, kind);
}
static HANDLE job, sessionLock, processes[4];
static DWORD pids[4];
static int width, height, count, readyMask;
static qboolean closing, paneStarted, paneClosing;
/* Off-screen views cannot capture the OS mouse; the visible host owns it for player 1. */
static qboolean hostMouse, mouseCaptured;
extern qboolean in_appactive;
static unsigned heldMouse;

int CL_OnlinePane(void) { return Cvar_VariableIntegerValue("et_onlinePane"); }
/* Same archived setting as local split-screen. */
static int LocalPlayers(void) {
    int value = Cvar_VariableIntegerValue("cl_splitScreen");
    return ET_LOCAL_PLAYER_COUNT(value);
}
/* Three players leave the fourth quadrant for the live map (no fourth connection). */
static RECT PaneRect(int slot) {
    int rows = count >= 3 ? 2 : 1;
    RECT rect = {(slot % 2) * width / 2, (slot / 2) * height / rows, (slot % 2 + 1) * width / 2, (slot / 2 + 1) * height / rows};
    return rect;
}
qboolean CL_OnlineSplitRunning(void) { return host != NULL; }
static HWND Parent(void) {
    char name[64], *end;
    const char *value = Cvar_VariableString("et_onlineParent");
    unsigned long number;
    DWORD pid;
    HWND window;
    if (CL_OnlinePane() < 1 || CL_OnlinePane() > 4 || !*value) return NULL;
    errno = 0; number = strtoul(value, &end, 10);
    if (errno || *end || !number) return NULL;
    window = (HWND)(uintptr_t)number;
    if (!GetClassNameA(window, name, sizeof(name)) || strcmp(name, HOST_CLASS)) return NULL;
    GetWindowThreadProcessId(window, &pid);
    return pid == (DWORD)Cvar_VariableIntegerValue("et_onlineOwner") ? window : NULL;
}
qboolean CL_OnlinePaneInputActive(void) {
    HWND parent = Parent(), foreground = GetForegroundWindow();
    return parent && (foreground == parent || IsChild(parent, foreground));
}
qboolean CL_OnlinePaneKeyPath(char *path, int size) {
    if (!CL_OnlinePane()) return qfalse;
    Q_strncpyz(path, FS_BuildOSPath(CL_OnlinePane() == 1 ? Cvar_VariableString("et_onlineRoot") : Cvar_VariableString("fs_homepath"), "etmain", "etkey"), size);
    return qtrue;
}
void CL_OnlinePaneVideo(void) {
    if (!CL_OnlinePane()) return;
    if (!Parent() || Cvar_VariableIntegerValue("et_onlineWidth") < 160 || Cvar_VariableIntegerValue("et_onlineWidth") > 8192 ||
        Cvar_VariableIntegerValue("et_onlineHeight") < 120 || Cvar_VariableIntegerValue("et_onlineHeight") > 8192)
        Com_Error(ERR_FATAL, "Invalid online player window parameters");
    Cvar_Set("r_mode", "-1");
    Cvar_Set("r_customwidth", Cvar_VariableString("et_onlineWidth"));
    Cvar_Set("r_customheight", Cvar_VariableString("et_onlineHeight"));
    Cvar_SetValue("r_customaspect", Cvar_VariableIntegerValue("et_onlineWidth") / (float)Cvar_VariableIntegerValue("et_onlineHeight"));
    Cvar_Set("r_fullscreen", "0"); Cvar_Set("r_displayRefresh", "0");
    Cvar_Set("ui_rustSlice", "0");
    Cvar_Set("cl_splitScreen", "0");
    if (CL_OnlinePane() > 1) { Cvar_Set("s_volume", "0"); Cvar_Set("s_musicvolume", "0"); }
    Cvar_SetValue("in_gamepadIndex", Cvar_VariableIntegerValue("et_onlinePad")); /* Player-setup controller, every restart. */
}
/* Reposition after EVERY renderer restart, including pure/mod changes. */
void CL_OnlinePaneWindow(void) {
    HWND parent = Parent();
    int slot = CL_OnlinePane() - 1;
    if (!CL_OnlinePane()) return;
    if (!parent || slot < 0 || slot >= 4) Com_Error(ERR_FATAL, "Online split-screen host is unavailable");
    /* Keep renderers top-level: no cross-process input-queue attachment. */
    SetWindowLongA(g_wv.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongA(g_wv.hWnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(g_wv.hWnd, NULL, -16000 + slot * Cvar_VariableIntegerValue("et_onlineWidth"), -16000, Cvar_VariableIntegerValue("et_onlineWidth"),
        Cvar_VariableIntegerValue("et_onlineHeight"), SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    PostMessage(parent, PANE_WINDOW, slot + 1, (LPARAM)g_wv.hWnd);
}
static BOOL CALLBACK FindPane(HWND window, LPARAM unused) {
    DWORD pid; char name[64]; int i;
    GetWindowThreadProcessId(window, &pid);
    GetClassNameA(window, name, sizeof(name));
    if (strcmp(name, "Enemy Territory")) return TRUE;
    for (i = 0; i < count; ++i) if (pid == pids[i]) panes[i] = window;
    return TRUE;
}
static void MouseKey(int key, qboolean down) {
    unsigned bit = 1u << (key - K_MOUSE1);
    if (key >= K_MOUSE1 && key <= K_MOUSE5) {
        if (down == ((heldMouse & bit) != 0)) return;
        heldMouse ^= bit;
    }
    if (keyboardInput && panes[keyboardPane]) PostMessage(panes[keyboardPane], PANE_KEY, key, down);
}
/* Clip and hide the cursor over the host only while it is the active window. */
static void CaptureMouse(qboolean capture) {
    int key;
    capture = capture && hostMouse && host;
    if (capture) {
        RECT rect; POINT corner = {0, 0};
        GetClientRect(host, &rect); ClientToScreen(host, &corner);
        OffsetRect(&rect, corner.x, corner.y); ClipCursor(&rect);
        if (!mouseCaptured) while (ShowCursor(FALSE) >= 0) {}
    } else if (mouseCaptured) {
        ClipCursor(NULL);
        while (ShowCursor(TRUE) < 0) {}
        for (key = K_MOUSE1; key <= K_MOUSE5; ++key) MouseKey(key, qfalse); /* No stuck fire after Alt-Tab. */
    }
    if (capture != mouseCaptured) Com_Printf("ONLINESPLIT mouse captured=%d\n", capture);
    mouseCaptured = capture;
}
static void RawMouse(HRAWINPUT handle) {
    static const int keys[] = {K_MOUSE1, K_MOUSE2, K_MOUSE3, K_MOUSE4, K_MOUSE5};
    RAWINPUT input; UINT size = sizeof(input); int i;
    unsigned short flags;
    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 ||
        input.header.dwType != RIM_TYPEMOUSE || !mouseCaptured) return;
    if (!(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && (input.data.mouse.lLastX || input.data.mouse.lLastY) && panes[keyboardPane])
        PostMessage(panes[keyboardPane], PANE_MOUSE, (WPARAM)input.data.mouse.lLastX, (LPARAM)input.data.mouse.lLastY);
    flags = input.data.mouse.usButtonFlags;
    for (i = 0; i < 5; ++i) {
        if (flags & (1u << (i * 2))) MouseKey(keys[i], qtrue);
        if (flags & (2u << (i * 2))) MouseKey(keys[i], qfalse);
    }
    if (flags & RI_MOUSE_WHEEL) {
        int key = (short)input.data.mouse.usButtonData > 0 ? K_MWHEELUP : K_MWHEELDOWN;
        MouseKey(key, qtrue); MouseKey(key, qfalse);
    }
}
static void Stop(void) {
    int i;
    if (!host || closing) return;
    closing = qtrue;
    CaptureMouse(qfalse);
    if (hostMouse) {
        RAWINPUTDEVICE device = {1, 2, RIDEV_REMOVE, NULL};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        hostMouse = qfalse;
    }
    heldMouse = 0;
    EnumWindows(FindPane, 0);
    for (i = 0; i < count; ++i) if (panes[i]) PostMessage(panes[i], WM_CLOSE, 0, 0);
    /* One bounded grace period for all players, not serial waits. */
    for (i = 0; i < count && processes[i]; ++i) {}
    if (count && i == count) WaitForMultipleObjects(count, processes, TRUE, 5000);
    if (job) { CloseHandle(job); job = NULL; } /* Kills only our still-hung children. */
    for (i = 0; i < 4; ++i) {
        if (processes[i]) { CloseHandle(processes[i]); processes[i] = NULL; }
        if (pixels[i]) { UnmapViewOfFile(pixels[i]); pixels[i] = NULL; }
        if (mappings[i]) { CloseHandle(mappings[i]); mappings[i] = NULL; }
        if (locks[i]) { CloseHandle(locks[i]); locks[i] = NULL; }
        panes[i] = NULL; pids[i] = 0;
    }
    DestroyWindow(host); host = NULL; readyMask = 0; count = 0;
    if (sessionLock) { CloseHandle(sessionLock); sessionLock = NULL; }
    GLimp_OnlineStandby(qfalse);
    ShowWindow(g_wv.hWnd, SW_SHOW); SetForegroundWindow(g_wv.hWnd); SetFocus(g_wv.hWnd);
    Cvar_Set("ui_connecting", "0");
    if (uivm) VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN);
    Com_Printf("ONLINESPLIT stopped; returned to unified menu\n");
    Cbuf_AddText("map_rescan\n"); /* The views may have downloaded maps. */
    closing = qfalse;
}
static LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_GETMINMAXINFO && width && height) {
        MINMAXINFO *limits = (MINMAXINFO *)l;
        RECT bounds = {0, 0, width, height};
        AdjustWindowRect(&bounds, GetWindowLongA(window, GWL_STYLE), FALSE);
        limits->ptMaxTrackSize.x = max(limits->ptMaxTrackSize.x, bounds.right - bounds.left);
        limits->ptMaxTrackSize.y = max(limits->ptMaxTrackSize.y, bounds.bottom - bounds.top);
        return 0; /* Windows must not shrink a display-sized, fixed client area. */
    }
    if (message == WM_CLOSE || (message == WM_SYSKEYDOWN && w == VK_F4)) { Stop(); return 0; }
    if (message == WM_ACTIVATE) {
        CaptureMouse(LOWORD(w) != WA_INACTIVE && !HIWORD(w));
        if (LOWORD(w) == WA_INACTIVE && panes[keyboardPane]) PostMessage(panes[keyboardPane], PANE_CLEAR, 0, 0);
        return 0;
    }
    if ((message == WM_MOVE || message == WM_SIZE) && mouseCaptured) CaptureMouse(qtrue);
    if (message == WM_INPUT) { RawMouse((HRAWINPUT)l); return DefWindowProc(window, message, w, l); }
    if (message == WM_SETCURSOR && mouseCaptured && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; }
    if (message == WM_SYSKEYDOWN && w == VK_RETURN) return 0; /* Pane size belongs to the host. */
    if (message == PANE_WINDOW && w >= 1 && w <= count) {
        DWORD pid; int slot = w - 1; RECT rect = PaneRect(slot);
        GetWindowThreadProcessId((HWND)l, &pid);
        if (pid != pids[slot]) return 0;
        panes[slot] = (HWND)l;
        Com_Printf("ONLINESPLIT view player=%d source=%lu rect=%ld,%ld,%ld,%ld\n", slot + 1,
            (unsigned long)l, rect.left, rect.top, rect.right, rect.bottom);
        return 0;
    }
    if (message == PANE_STATE && w >= 1 && w <= count) {
        int previous = readyMask, all = (1 << count) - 1;
        if (l == CA_ACTIVE) readyMask |= 1 << (w - 1); else readyMask &= ~(1 << (w - 1));
        if (readyMask == all && previous != all) Com_Printf("ONLINESPLIT ready clients=%d layout=%s\n", count, count >= 3 ? "2x2" : "2x1");
        return 0;
    }
    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_CHAR) {
        EnumWindows(FindPane, 0);
        if (keyboardInput && panes[keyboardPane]) PostMessage(panes[keyboardPane], message, w, l);
        return 0;
    }
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) return 0; /* Raw input supplies player 1's mouse. */
    if (message == PANE_FRAME && w >= 1 && w <= (count == 3 ? 4 : count)) {
        RECT rect = PaneRect(w - 1);
        InvalidateRect(window, &rect, FALSE); return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); int i;
        BITMAPINFO bitmap = {0};
        RECT first = PaneRect(0); /* Every pane has the first pane's size. */
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmap.bmiHeader.biWidth = first.right;
        bitmap.bmiHeader.biHeight = first.bottom; bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB; /* Native BGRA: no channel conversion or scaling. */
        for (i = 0; i < (count == 3 ? 4 : count); ++i) if (pixels[i] && locks[i]) {
            RECT pane = PaneRect(i), intersection;
            DWORD acquired;
            if (!IntersectRect(&intersection, &pane, &paint.rcPaint)) continue;
            acquired = WaitForSingleObject(locks[i], 0);
            if (acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED) {
                SetDIBitsToDevice(dc, pane.left, pane.top, first.right, first.bottom,
                    0, 0, 0, first.bottom, pixels[i], &bitmap, DIB_RGB_COLORS);
                GdiFlush(); ReleaseMutex(locks[i]);
            }
        }
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProc(window, message, w, l);
}
static qboolean SafeArgument(const char *text) { return *text && !strpbrk(text, "\"\r\n"); }
static qboolean PreparePane(int slot, const char *address, char *home, int size) {
    char path[MAX_OSPATH], config[2048], name[MAX_NAME_LENGTH];
    HANDLE file; DWORD written; fileHandle_t seed;
    RECT pane = PaneRect(0);
    int n;
    Q_strncpyz(home, FS_BuildOSPath(Cvar_VariableString("fs_homepath"), "players", va("%d", slot + 1)), size);
    if (!SafeArgument(home)) return qfalse;
    Q_strncpyz(name, Cvar_VariableString("name"), sizeof(name));
    for (n = 0; name[n]; ++n) if (name[n] == '"' || name[n] == '\\' || name[n] < 32) name[n] = ' ';
    Q_strncpyz(path, FS_BuildOSPath(home, "etmain", va("profiles/online%d/etconfig.cfg", slot + 1)), sizeof(path));
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return qfalse;
        /* Initial seed only; never rewrite a player's saved profile. */
        seed = FS_SV_FOpenFileWrite(va("players/%d/etmain/profiles/online%d/etconfig.cfg", slot + 1, slot + 1));
        if (!seed) return qfalse;
        Key_WriteBindings(seed); Cvar_WriteVariables(seed);
        Com_sprintf(config, sizeof(config), "seta name \"%s%s\"\nseta cl_profile online%d\nseta cl_defaultProfile online%d\n",
            name, slot ? va(" P%d", slot + 1) : "", slot + 1, slot + 1);
        FS_Write(config, strlen(config), seed); FS_FCloseFile(seed);
    }
    Q_strncpyz(path, FS_BuildOSPath(home, "etmain", va("profiles/online%d/profile.dat", slot + 1)), sizeof(path));
    FS_CreatePath(path);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        Com_sprintf(config, sizeof(config), "\"%s%s\"", name, slot ? va(" P%d", slot + 1) : "");
        qboolean ok = WriteFile(file, config, strlen(config), &written, NULL) && written == strlen(config) && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok) { DeleteFileA(path); return qfalse; }
    }
    Q_strncpyz(path, FS_BuildOSPath(home, "etmain", "online-pane.cfg"), sizeof(path));
    FS_CreatePath(path);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return qfalse;
    Com_sprintf(config, sizeof(config),
        "set cl_profile online%d\nset cl_defaultProfile online%d\n"
        "set ui_rustSlice 0\nset ui_connecting 0\nset cl_splitScreen 0\n"
        "set in_gamepad 1\nset in_gamepadIndex %d\nset in_mouse %d\nset cl_autoupdate 0\n"
        "set cl_punkbuster 0\nset cl_allowDownload 1\nset cl_wwwDownload 1\nset rate 25000\nset snaps 20\n"
        "set r_mode -1\nset r_customwidth %ld\nset r_customheight %ld\nset r_customaspect %.6f\n"
        "set r_fullscreen 0\nset r_displayRefresh 0\nset r_swapInterval 0\nset r_ignorehwgamma 1\nset r_onlineAsync %d\n"
        "set s_volume %f\nset s_musicvolume %f\nset logfile 2\n"
        /* Wheel/assist choices come from the launcher's menu, which online views cannot open. */
        "set cl_gamepadRadialHelp %d\nset cl_gamepadAssistStrength %g\nconnect %s\n",
        slot + 1, slot + 1, padFor[slot], 0,
        pane.right, pane.bottom, pane.right / (float)pane.bottom, Cvar_VariableIntegerValue("r_onlineAsync"), slot ? 0.f : Cvar_VariableValue("s_volume"),
        slot ? 0.f : Cvar_VariableValue("s_musicvolume"), Cvar_VariableIntegerValue("cl_gamepadRadialHelp"),
        Cvar_VariableValue("cl_gamepadAssistStrength"), address);
    n = strlen(config);
    if (!WriteFile(file, config, n, &written, NULL) || written != n || !FlushFileBuffers(file)) { CloseHandle(file); return qfalse; }
    CloseHandle(file); return qtrue;
}
qboolean CL_OnlineSplitConnect(const char *server) {
    char executable[MAX_OSPATH], home[MAX_OSPATH], base[MAX_OSPATH], root[MAX_OSPATH], args[2048], address[128], lockPath[MAX_OSPATH];
    netadr_t target; RECT rect, pane; WNDCLASSA wc = {0}; JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    int i, style, players = LocalPlayers(); unsigned qport;
    if (CL_OnlinePane() || players < 2 || !Cvar_VariableIntegerValue("et_unified")) return qfalse;
    /* Local split-screen matches own loopback; only remote servers get one process per player. */
    if (NET_StringToAdr(server, &target) && target.type == NA_LOOPBACK) return qfalse;
    if (host) Stop();
    if (cls.state != CA_DISCONNECTED || com_sv_running->integer || !NET_StringToAdr(server, &target) || target.type != NA_IP) {
        Com_Printf("Online split-screen needs a remote Internet/LAN server and a disconnected local match.\n"); return qtrue;
    }
    if (!target.port) target.port = BigShort(PORT_SERVER);
    Q_strncpyz(address, NET_AdrToString(target), sizeof(address));
    Q_strncpyz(base, Cvar_VariableString("fs_basepath"), sizeof(base));
    Q_strncpyz(root, Cvar_VariableString("fs_homepath"), sizeof(root));
    i = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (!i || i >= sizeof(executable) || strlen(Cvar_VariableString("fs_basepath")) >= sizeof(base) ||
        strlen(Cvar_VariableString("fs_homepath")) >= sizeof(root) || !SafeArgument(base) || !SafeArgument(root) || !SafeArgument(executable)) {
        Com_Printf("Invalid online split-screen launch path.\n"); return qtrue;
    }
    width = cls.glconfig.vidWidth & ~1; height = cls.glconfig.vidHeight & ~1; count = players;
    IN_GamepadAssignments(padFor, players); /* Browser may already have closed its UI; use the launch count, not the current single view. */
    keyboardPane = Cvar_VariableIntegerValue("cl_keyboardPlayer") - 1;
    if (keyboardPane < -1 || keyboardPane >= players) keyboardPane = 0;
    keyboardInput = keyboardPane >= 0;
    if (!keyboardInput) keyboardPane = 0;
    pane = PaneRect(0);
    if (width < 640 || height < 480 || width > 16384 || height > 16384 || pane.bottom > 8192) {
        count = 0; Com_Printf("Online split-screen needs a resolution between 640x480 and 16384x%d.\n", players >= 3 ? 16384 : 8192); return qtrue;
    }
    Q_strncpyz(lockPath, FS_BuildOSPath(root, "players", "session.lock"), sizeof(lockPath));
    FS_CreatePath(lockPath);
    sessionLock = CreateFileA(lockPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (sessionLock == INVALID_HANDLE_VALUE) {
        sessionLock = NULL; count = 0; Com_Printf("An online split session already owns these player profiles, or they are inaccessible.\n"); return qtrue;
    }
    wc.lpfnWndProc = HostProc; wc.hInstance = g_wv.hInstance; wc.lpszClassName = HOST_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto fail;
    style = Cvar_VariableIntegerValue("r_fullscreen") ? WS_POPUP : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    rect.left = rect.top = 0; rect.right = width; rect.bottom = height; AdjustWindowRect(&rect, style, FALSE);
    host = CreateWindowA(HOST_CLASS, va("Enemy Territory: Arcade Edition - %d players online", count), style | WS_CLIPCHILDREN,
        0, 0, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, g_wv.hInstance, NULL);
    job = CreateJobObject(NULL, NULL); limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!host || !job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto fail;
    qport = GetTickCount() & 65535;
    for (i = 0; i < (count == 3 ? 4 : count); ++i) {
        STARTUPINFOA startup = {sizeof(startup)}; PROCESS_INFORMATION process = {0};
        char name[96];
        BufferName(name, sizeof(name), GetCurrentProcessId(), host, i + 1, "pixels");
        mappings[i] = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, pane.right * pane.bottom * 4, name);
        if (!mappings[i] || GetLastError() == ERROR_ALREADY_EXISTS) goto fail;
        pixels[i] = MapViewOfFile(mappings[i], FILE_MAP_ALL_ACCESS, 0, 0, pane.right * pane.bottom * 4);
        BufferName(name, sizeof(name), GetCurrentProcessId(), host, i + 1, "lock");
        locks[i] = CreateMutexA(NULL, FALSE, name);
        if (!pixels[i] || !locks[i]) goto fail;
        if (count == 3 && i == 3) continue; /* Produced by player one's renderer, not another server client. */
        if (!PreparePane(i, address, home, sizeof(home))) goto fail;
        Com_sprintf(args, sizeof(args),
            "\"%s\" +set fs_basepath \"%s\" +set fs_homepath \"%s\" +set fs_game etmain +set et_unified 1 "
            "+set et_onlinePane %d +set et_onlineParent %lu +set et_onlineOwner %lu +set et_onlineRoot \"%s\" "
            "+set et_onlineWidth %d +set et_onlineHeight %d +set et_onlineKeyboard %d +set et_onlinePad %d +set et_onlineMap %d +set net_port 0 +set net_qport %u +set in_mouse 0 "
            "+set com_hunkMegs 256 +set com_recommendedSet 1 +set com_introPlayed 1 +set logfile 2 "
            "+set cl_profile online%d +set cl_defaultProfile online%d +set ui_rustMenuTest %d +exec online-pane.cfg",
            executable, base, home, i + 1, (unsigned long)(uintptr_t)host, (unsigned long)GetCurrentProcessId(), root,
            (int)pane.right, (int)pane.bottom, keyboardInput ? keyboardPane + 1 : 0, padFor[i], count == 3 && i == 0, (qport + i) & 65535, i + 1, i + 1,
            Cvar_VariableIntegerValue("ui_rustMenuTest") && !strncmp(address, "127.0.0.1:", 10));
        if (strlen(args) - strlen(executable) - 3 > 1023) goto fail;
        if (!CreateProcessA(executable, args, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, home, &startup, &process)) goto fail;
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            TerminateProcess(process.hProcess, 1); CloseHandle(process.hProcess); CloseHandle(process.hThread); goto fail;
        }
        processes[i] = process.hProcess; pids[i] = process.dwProcessId;
        if (ResumeThread(process.hThread) == (DWORD)-1) { CloseHandle(process.hThread); goto fail; }
        CloseHandle(process.hThread);
        Com_Printf("ONLINESPLIT spawned player=%d pid=%lu home=%s\n", i + 1, (unsigned long)pids[i], home);
    }
    Cvar_Set("ui_connecting", "0");
    S_StopAllSounds(); Con_Close();
    GLimp_OnlineStandby(qtrue);
    /* Player 1 keeps keyboard/mouse when the launcher had a mouse; raw input arrives only while the host is foreground. */
    if (keyboardInput && Cvar_VariableIntegerValue("in_mouse")) {
        RAWINPUTDEVICE device = {1, 2, 0, host};
        hostMouse = RegisterRawInputDevices(&device, 1, sizeof(device));
        if (!hostMouse) Com_Printf("Online split-screen: mouse unavailable (Windows error %lu).\n", GetLastError());
    }
    ShowWindow(host, SW_SHOW); ShowWindow(g_wv.hWnd, SW_HIDE); SetForegroundWindow(host);
    CaptureMouse(GetForegroundWindow() == host);
    Com_Printf("ONLINESPLIT connecting server=%s players=%d\n", address, count); return qtrue;
fail:
    Com_Printf("Online split-screen launch failed (Windows error %lu).\n", GetLastError());
    Cvar_Set("com_errorMessage", va("Could not start %d online players. Check the console.", count));
    if (!host) {
        count = 0;
        if (job) { CloseHandle(job); job = NULL; }
        if (sessionLock) { CloseHandle(sessionLock); sessionLock = NULL; }
        if (uivm) VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN);
    } else Stop();
    return qtrue;
}
void CL_OnlineSplitFrame(void) {
    int i;
    if (CL_OnlinePane()) {
        HWND parent = Parent();
        static int previous = -1;
        if (cls.state >= CA_CONNECTING && cls.state <= CA_ACTIVE) paneStarted = qtrue;
        /* Something activated this view (focus calls, renderer restarts): pads and mouse belong to the host. */
        if (parent && GetForegroundWindow() == g_wv.hWnd) SetForegroundWindow(parent);
        if (!parent || (paneStarted && cls.state == CA_DISCONNECTED && !cls.bWWWDlDisconnected)) {
            if (!paneClosing) {
                paneClosing = qtrue;
                if (parent) PostMessage(parent, WM_CLOSE, 0, 0);
                Com_Printf("ONLINEPANE ended player=%d reason=%s\n", CL_OnlinePane(), Cvar_VariableString("com_errorMessage"));
                Cbuf_AddText("quit\n");
            }
            return;
        }
        if (previous != cls.state) {
            previous = cls.state; PostMessage(parent, PANE_STATE, CL_OnlinePane(), cls.state);
            if (cls.state == CA_ACTIVE) Com_Printf("ONLINEPANE ready player=%d client=%d guid=%s\n", CL_OnlinePane(), clc.clientNum, Cvar_VariableString("cl_guid"));
        }
    } else if (host && !closing) {
        if (GetUpdateRect(host, NULL, FALSE)) UpdateWindow(host);
        for (i = 0; i < count; ++i) if (processes[i] && WaitForSingleObject(processes[i], 0) == WAIT_OBJECT_0) {
            Com_Printf("ONLINESPLIT player=%d exited; closing session\n", i + 1); Stop(); break;
        }
    }
}
/* ponytail: shared-memory BGRA + GDI retains cross-driver compatibility;
 * GPU shared surfaces only if asynchronous readback still costs too much. */
unsigned char *CL_OnlinePaneFrameBuffer(int w, int h) {
    char name[96]; DWORD acquired;
    HWND parent = Parent();
    int index = mapFrame ? 1 : 0, slot = mapFrame ? 4 : CL_OnlinePane();
    if (!CL_OnlinePane() || !parent || w != Cvar_VariableIntegerValue("et_onlineWidth") || h != Cvar_VariableIntegerValue("et_onlineHeight")) return NULL;
    if (!panePixels[index]) {
        BufferName(name, sizeof(name), Cvar_VariableIntegerValue("et_onlineOwner"), parent, slot, "pixels");
        paneMapping[index] = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!paneMapping[index]) return NULL;
        panePixels[index] = MapViewOfFile(paneMapping[index], FILE_MAP_ALL_ACCESS, 0, 0, (size_t)w * h * 4);
        BufferName(name, sizeof(name), Cvar_VariableIntegerValue("et_onlineOwner"), parent, slot, "lock");
        paneLock[index] = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
        if (!panePixels[index] || !paneLock[index]) Com_Error(ERR_FATAL, "Could not open online frame buffer");
    }
    acquired = WaitForSingleObject(paneLock[index], 0);
    pixelLocked = acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED;
    return pixelLocked ? panePixels[index] : NULL;
}
void CL_OnlinePaneFrameReady(qboolean published) {
    if (pixelLocked) {
        ReleaseMutex(paneLock[mapFrame ? 1 : 0]); pixelLocked = qfalse;
        if (published) PostMessage(Parent(), PANE_FRAME, mapFrame ? 4 : CL_OnlinePane(), 0);
    }
}
qboolean CL_OnlinePaneMessage(unsigned message, unsigned w, long l) {
    if (!CL_OnlinePane()) return qfalse;
    if (message == PANE_REPORT) { PaneReport(qfalse); return qtrue; }
    /* Never activate input in an off-screen view; CL_OnlineSplitFrame returns the foreground to the host. */
    if (message == WM_ACTIVATE && LOWORD(w) != WA_INACTIVE) return qtrue;
    if (message == PANE_MOUSE) {
        if (CL_OnlinePane() == Cvar_VariableIntegerValue("et_onlineKeyboard")) Sys_QueEvent(0, SE_MOUSE, (int)w, (int)l, 0, NULL);
        return qtrue;
    }
    if (message == PANE_CLEAR) { Key_ClearStates(); return qtrue; }
    if (message == PANE_KEY) {
        if (CL_OnlinePane() == Cvar_VariableIntegerValue("et_onlineKeyboard") && ((w >= K_MOUSE1 && w <= K_MOUSE5) || w == K_MWHEELUP || w == K_MWHEELDOWN))
            Sys_QueEvent(0, SE_KEY, w, l != 0, 0, NULL);
        return qtrue;
    }
    return qfalse;
}
void CL_OnlineMapDraw(void) {
    if (CL_OnlinePane() != 1 || !Cvar_VariableIntegerValue("et_onlineMap") || cls.state != CA_ACTIVE) return;
    /* ponytail: a second 2D frame reuses the hidden renderer; direct readback keeps the two streams
       distinct. Add tagged async transfers only if three-player readback becomes a bottleneck. */
    R_SyncRenderThread(); mapFrame = qtrue;
    re.BeginFrame(STEREO_CENTER);
    CL_FullMapDraw(0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight);
    re.EndFrame(NULL, NULL);
    R_SyncRenderThread(); mapFrame = qfalse;
}
void CL_OnlineSplitShutdown(void) {
    int i;
    Stop(); CL_OnlinePaneFrameReady(qfalse);
    for (i = 0; i < 2; ++i) {
        if (panePixels[i]) { UnmapViewOfFile(panePixels[i]); panePixels[i] = NULL; }
        if (paneMapping[i]) { CloseHandle(paneMapping[i]); paneMapping[i] = NULL; }
        if (paneLock[i]) { CloseHandle(paneLock[i]); paneLock[i] = NULL; }
    }
}
static void PaneReport(qboolean render) {
    if (!Cvar_VariableIntegerValue("ui_rustMenuTest") || !CL_OnlinePane() || clc.serverAddress.type != NA_IP ||
        clc.serverAddress.ip[0] != 127 || clc.serverAddress.ip[1] || clc.serverAddress.ip[2] || clc.serverAddress.ip[3] != 1) return;
    if (render) { GLimp_OnlineFrameStats(); return; }
    Com_Printf("ONLINEPANE_TEST player=%d client=%d state=%d parent=%lu controller=%d health=%d pm=%d cmd=%d pos=%.3f,%.3f,%.3f yaw=%.3f input=%d appactive=%d guid=%s\n",
        CL_OnlinePane(), clc.clientNum, cls.state, (unsigned long)(uintptr_t)Parent(),
        Cvar_VariableIntegerValue("in_gamepadIndex"), cl.snap.ps.stats[STAT_HEALTH], cl.snap.ps.pm_type,
        cl.cmdNumber, cl.snap.ps.origin[0], cl.snap.ps.origin[1], cl.snap.ps.origin[2], cl.viewangles[YAW],
        CL_OnlinePaneInputActive(), in_appactive, Cvar_VariableString("cl_guid"));
}
static void PaneTest_f(void) { PaneReport(!Q_stricmp(Cmd_Argv(1), "render")); }
void CL_OnlineSplitInit(void) {
    static const char *names[] = {"et_onlinePane", "et_onlineParent", "et_onlineOwner", "et_onlineRoot", "et_onlineWidth", "et_onlineHeight", "et_onlineKeyboard", "et_onlinePad", "et_onlineMap"};
    int i;
    Cvar_Get("r_onlineAsync", "1", CVAR_ARCHIVE);
    Cmd_AddCommand("onlinepane_test", PaneTest_f);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) Cvar_Get(names[i], "0", CVAR_INIT | CVAR_ROM | CVAR_NORESTART);
    CL_OnlinePaneVideo();
}
