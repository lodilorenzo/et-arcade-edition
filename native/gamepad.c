/* Windows XInput transport and native ET usercmd/menu integration. */
#include "../engine/src/client/client.h"
#include "../engine/src/win32/win_local.h"
#include <Xinput.h>
#include <errno.h>
#include "gamepad_math.h"

extern qboolean in_appactive;
static HMODULE xinput;
static DWORD (WINAPI *getState)(DWORD, XINPUT_STATE *);
typedef struct {
    XINPUT_GAMEPAD pad;
    qboolean isConnected, stickBlocked, resetAim, pointerBlocked;
    int deviceIndex, context, lastRepeat, menuEpoch, stickDirection;
    int wheelPressed[2], wheelButton;
    unsigned blocked, previous, menuKeys, holding;
    float peggedTime;
} gamepadState_t;
static gamepadState_t players[ET_LOCAL_PLAYERS], *activePad = &players[0];
static int menuOwner, pendingOwner = -1;
static qboolean menuOpened, uiWasOpen;
#define pad (activePad->pad)
#define connected (activePad->isConnected)
#define stickBlocked (activePad->stickBlocked)
#define controller (activePad->deviceIndex)
#define context (activePad->context)
#define lastRepeat (activePad->lastRepeat)
#define menuEpoch (activePad->menuEpoch)
#define stickDirection (activePad->stickDirection)
#define blocked (activePad->blocked)
#define previous (activePad->previous)
#define menuKeys (activePad->menuKeys)
#define peggedTime (activePad->peggedTime)
static cvar_t *enabled, *indices[ET_LOCAL_PLAYERS], *moveDead, *lookDead, *yawSpeed, *pitchSpeed;
static cvar_t *invert, *accelTime, *accelScale, *friction, *adhesion, *assistStrength, *trigger, *wheelHold;

/* Extra bits keep triggers separate from XInput's sixteen digital buttons. */
#define PAD_FIRE 0x10000u
#define PAD_ZOOM 0x20000u
/* Deterministic regression input: explicitly enabled local split-screen or
 * loopback-IP online fixtures only; never public remote servers. */
static qboolean testMode, testOnline[ET_LOCAL_PLAYERS];
static XINPUT_STATE testDevices[ET_LOCAL_PLAYERS];
static qboolean TestAllowed(void) {
    return Cvar_VariableIntegerValue("ui_rustMenuTest") &&
        ((CL_SplitEnabled() && Cvar_VariableIntegerValue("developer")) ||
         (clc.serverAddress.type==NA_IP && clc.serverAddress.ip[0]==127 && !clc.serverAddress.ip[1] && !clc.serverAddress.ip[2] && clc.serverAddress.ip[3]==1));
}
/* Synthetic local regression input must not depend on desktop focus. Real pads still do. */
static qboolean InputActive(void) { return (CL_OnlinePane() ? CL_OnlinePaneInputActive() : in_appactive) || (testMode && TestAllowed()); }
static void GamepadTest_f(void) {
    long value[7]; int i, slot;
    if (!TestAllowed() || Cmd_Argc() != 8) return;
    for (i = 0; i < 7; ++i) {
        char *end; const char *arg = Cmd_Argv(i + 1);
        errno = 0; value[i] = strtol(arg, &end, 0);
        if (!*arg || *end || errno == ERANGE) return;
    }
    if (value[0] < 0 || value[0] >= ET_LOCAL_PLAYERS || value[5] < 0 || value[5] > 0x3ffff || value[6] < 0 || value[6] > 1) return;
    for (i = 1; i <= 4; ++i) if (value[i] < -32768 || value[i] > 32767) return;
    slot = value[0]; testMode = qtrue; testOnline[slot] = value[6];
    testDevices[slot].Gamepad.sThumbLX = value[1]; testDevices[slot].Gamepad.sThumbLY = value[2];
    testDevices[slot].Gamepad.sThumbRX = value[3]; testDevices[slot].Gamepad.sThumbRY = value[4];
    testDevices[slot].Gamepad.wButtons = value[5] & 0xffff;
    testDevices[slot].Gamepad.bRightTrigger = value[5] & PAD_FIRE ? 255 : 0;
    testDevices[slot].Gamepad.bLeftTrigger = value[5] & PAD_ZOOM ? 255 : 0;
}
static DWORD ReadState(DWORD slot, XINPUT_STATE *state) {
    if (testMode && TestAllowed()) {
        if (slot >= ET_LOCAL_PLAYERS || !testOnline[slot]) return ERROR_DEVICE_NOT_CONNECTED;
        *state = testDevices[slot]; return ERROR_SUCCESS;
    }
    return getState ? getState(slot, state) : ERROR_DEVICE_NOT_CONNECTED;
}
static unsigned Buttons(void) {
    unsigned buttons = pad.wButtons;
    float threshold = GP_Clamp(trigger->value, .01f, .95f) * 255.f;
    if (pad.bRightTrigger > threshold) buttons |= PAD_FIRE;
    if (pad.bLeftTrigger > threshold) buttons |= PAD_ZOOM;
    return buttons;
}

/* Distinct joystick keys let native UI distinguish digit editing from keyboard input. */
static const int menuCodes[] = {K_JOY1, K_JOY2, K_JOY3, K_JOY4, K_JOY5, K_JOY6, K_JOY7};

static unsigned MenuDirection(float x, float y) {
    float largest = fmaxf(fabsf(x), fabsf(y));
    if (stickBlocked) {
        if (largest < .35f) stickBlocked = qfalse;
        stickDirection = 0;
        return 0;
    }
    if (largest < .35f) stickDirection = 0;
    else if (largest >= .6f) {
        if (fabsf(y) >= fabsf(x)) stickDirection = y > 0.f ? 4 : 8;
        else stickDirection = x < 0.f ? 16 : 32;
    }
    return stickDirection;
}
#define LOCAL_MENU 0x20000
static void MenuKeys(unsigned keys, qboolean repeat) {
    unsigned i;
    for (i = 0; i < sizeof(menuCodes) / sizeof(menuCodes[0]); ++i) {
        unsigned bit = 1u << i;
        if (((keys ^ menuKeys) & bit) || (repeat && (keys & bit) && i >= 2 && i < 6)) {
            qboolean down = (keys & bit) != 0;
            if (context & LOCAL_MENU)
                CL_LocalLoadoutEventForPlayer((int)(activePad - players), CG_KEY_EVENT, menuCodes[i], down);
            else if (context & KEYCATCH_UI)
                Sys_QueEvent(0, SE_KEY, menuCodes[i], down, 0, NULL);
            else if (context & KEYCATCH_CGAME) {
                static const int cgameCodes[]={K_MOUSE1,K_ESCAPE,K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,K_MOUSE2};
                Sys_QueEvent(0,SE_KEY,cgameCodes[i],down,0,NULL);
            }
        }
    }
    menuKeys = keys;
}

/* Dispatch in the owner's cgame now: queued press/release pairs can collapse or
 * wait behind scripts, hiding the map and delaying rapid weapon taps. */
static void PadCommand(int localPlayer, const char *command) {
    CL_LocalCgameCommand(localPlayer, command);
}
static const struct { unsigned button; const char *down, *up; } holdCommands[] = {
    {XINPUT_GAMEPAD_RIGHT_THUMB, "+mapexpand", "-mapexpand"},
    {XINPUT_GAMEPAD_LEFT_THUMB, "+scores", "-scores"}, /* Original scoreboard while held. */
};
static void ReleaseHeld(void) {
    unsigned i;
    for (i = 0; i < sizeof(holdCommands) / sizeof(holdCommands[0]); ++i)
        if (activePad->holding & holdCommands[i].button) PadCommand((int)(activePad - players), holdCommands[i].up);
    activePad->holding = 0;
}

static void RefreshContext(void) {
    int local = (int)(activePad - players);
    int next = cls.keyCatchers | (cls.state == CA_ACTIVE && !clc.demoplaying ? 0 : 0x10000);
    int epoch = Cvar_VariableIntegerValue("ui_menuInputEpoch") + CL_LocalLoadoutEpoch(local);
    if (!cls.keyCatchers && CL_LocalLoadoutOpen(local)) next |= LOCAL_MENU;
    if (context == next && menuEpoch == epoch) return;
    MenuKeys(0, qfalse);
    ReleaseHeld();
    CL_RadialCancel(local);
    memset(activePad->wheelPressed, 0, sizeof(activePad->wheelPressed)); activePad->wheelButton = 0;
    blocked |= Buttons(); previous = 0; peggedTime = 0.f;
    stickDirection = 0; stickBlocked = qtrue; activePad->resetAim = activePad->pointerBlocked = qtrue;
    context = next; menuEpoch = epoch;
}

static void ClearPlayer(void) {
    CL_RadialCancel((int)(activePad - players));
    memset(activePad->wheelPressed, 0, sizeof(activePad->wheelPressed)); activePad->wheelButton = 0;
    MenuKeys(0, qfalse);
    ReleaseHeld();
    blocked = connected ? Buttons() : 0;
    previous = 0;
    context = -1;
    peggedTime = 0.f;
    stickDirection = 0;
    stickBlocked = qtrue;
    activePad->resetAim = activePad->pointerBlocked = qtrue;
}

void IN_GamepadClear(void) {
    int i;
    for (i = 0; i < ET_LOCAL_PLAYERS; ++i) { activePad = &players[i]; ClearPlayer(); }
    activePad = &players[0];
}
qboolean CL_GamepadConnected(int localPlayer) { return localPlayer >= 0 && localPlayer < ET_LOCAL_PLAYERS && players[localPlayer].isConnected && enabled->integer && InputActive(); }

void IN_GamepadInit(void) {
    static const char *libraries[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    unsigned i;
    Cmd_RemoveCommand("gamepad_test"); Cmd_AddCommand("gamepad_test", GamepadTest_f);
    enabled = Cvar_Get("in_gamepad", "1", CVAR_ARCHIVE);
    indices[0] = Cvar_Get("in_gamepadIndex", "-1", CVAR_ARCHIVE);
    indices[1] = Cvar_Get("in_gamepadIndex2", "-1", CVAR_ARCHIVE);
    indices[2] = Cvar_Get("in_gamepadIndex3", "-1", CVAR_ARCHIVE);
    indices[3] = Cvar_Get("in_gamepadIndex4", "-1", CVAR_ARCHIVE);
    Cvar_Get("cl_splitScreen", "0", CVAR_ARCHIVE);
    Cvar_Get("cl_keyboardPlayer", "1", CVAR_ARCHIVE); /* 1..4: who uses keyboard and mouse. */
    for (i = 0; i < ET_LOCAL_PLAYERS; ++i) { activePad = &players[i]; controller = context = -1; }
    activePad = &players[0];
    moveDead = Cvar_Get("cl_gamepadMoveDeadzone", "0.20", CVAR_ARCHIVE);
    lookDead = Cvar_Get("cl_gamepadLookDeadzone", "0.20", CVAR_ARCHIVE);
    yawSpeed = Cvar_Get("cl_gamepadYawSpeed", "120", CVAR_ARCHIVE);
    pitchSpeed = Cvar_Get("cl_gamepadPitchSpeed", "90", CVAR_ARCHIVE);
    invert = Cvar_Get("cl_gamepadInvert", "0", CVAR_ARCHIVE);
    accelTime = Cvar_Get("cl_gamepadAccelTime", "0.5", CVAR_ARCHIVE);
    accelScale = Cvar_Get("cl_gamepadAccelScale", "2", CVAR_ARCHIVE);
    friction = Cvar_Get("cl_gamepadFriction", "0.65", CVAR_ARCHIVE);
    adhesion = Cvar_Get("cl_gamepadAdhesion", "0.45", CVAR_ARCHIVE);
    assistStrength = Cvar_Get("cl_gamepadAssistStrength", "1", CVAR_ARCHIVE);
    trigger = Cvar_Get("cl_gamepadTrigger", "0.12", CVAR_ARCHIVE);
    wheelHold = Cvar_Get("cl_gamepadWheelHold", "150", CVAR_ARCHIVE);
    /* Migrate the old default once, but retain custom timings and future edits. */
    if (!Cvar_Get("cl_gamepadWheelVersion", "0", CVAR_ARCHIVE)->integer) {
        if (wheelHold->integer == 250) Cvar_Set("cl_gamepadWheelHold", "150");
        Cvar_Set("cl_gamepadWheelVersion", "1");
    }
    Cvar_Get("cl_gamepadRadialDeadzone", "0.35", CVAR_ARCHIVE);
    CL_RadialInit();
    Cvar_Get("et_gamepadAPI", "1", CVAR_ROM);
    Cvar_Get("et_splitClientNum", "-1", CVAR_ROM);
    Cvar_Get("ui_menuPlayer", "1", 0); /* 1..4: ownership of shared settings/pause only. */
    Cvar_Get("cg_gamepadAimAssist", "1", CVAR_ARCHIVE); /* Main-menu defaults before cgame loads. */
    if (!xinput) {
        for (i = 0; i < sizeof(libraries) / sizeof(libraries[0]); ++i) {
            /* System-only search: never load an arbitrary DLL from the mod. */
            xinput = LoadLibraryExA(libraries[i], NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (xinput) {
                getState = (DWORD (WINAPI *)(DWORD, XINPUT_STATE *))GetProcAddress(xinput, "XInputGetState");
                if (getState) break;
                FreeLibrary(xinput);
                xinput = NULL;
            }
        }
        Com_Printf("Gamepad: %s\n", getState ? "XInput ready (hot-plug enabled)" : "XInput unavailable; keyboard/mouse unchanged");
    }
    IN_GamepadClear();
}

void IN_GamepadShutdown(void) {
    IN_GamepadClear();
    CL_RadialReset();
    memset(players, 0, sizeof(players));
    menuOwner = 0; pendingOwner = -1; uiWasOpen = qfalse;
    if (xinput) FreeLibrary(xinput);
    xinput = NULL;
    getState = NULL;
    controller = -1;
}

static qboolean PlayerFrame(int localPlayer, int wanted) {
    XINPUT_STATE state;
    int i = wanted, now = Sys_Milliseconds();
    unsigned buttons, keys = 0;
    qboolean found = qfalse;
    activePad = &players[localPlayer];

    if ((getState || (testMode && TestAllowed())) && (enabled->integer || (cls.keyCatchers & KEYCATCH_UI) || CL_LocalLoadoutOpen(localPlayer)) && InputActive()) {
        if (wanted >= 0 && wanted < ET_LOCAL_PLAYERS) found = ReadState(i, &state) == ERROR_SUCCESS;
    }
    if (!found) {
        ClearPlayer();
        if (connected) Com_Printf("Gamepad disconnected/inactive\n");
        connected = qfalse;
        controller = -1;
        memset(&pad, 0, sizeof(pad));
        return qfalse;
    }
    if (!connected || controller != i) {
        ClearPlayer();
        controller = i;
        connected = qtrue;
        pad = state.Gamepad;
        blocked = Buttons(); /* A plugged-in, held trigger must not fire. */
        Com_Printf("Gamepad connected: player %d, XInput slot %d\n", localPlayer + 1, i);
    }
    pad = state.Gamepad;
    buttons = Buttons();
    RefreshContext();
    if (!(cls.keyCatchers & KEYCATCH_UI) && stickBlocked) {
        float lx, ly, rx, ry;
        GP_Stick(pad.sThumbLX, pad.sThumbLY, moveDead->value, &lx, &ly);
        GP_Stick(pad.sThumbRX, pad.sThumbRY, lookDead->value, &rx, &ry);
        if (fmaxf(fmaxf(fabsf(lx), fabsf(ly)), fmaxf(fabsf(rx), fabsf(ry))) < .35f) stickBlocked = qfalse;
    }
    blocked &= buttons;
    buttons &= ~blocked;
    if (CL_RadialOpen(localPlayer)) {
        float x, y;
        if (activePad->wheelButton == XINPUT_GAMEPAD_Y)
            GP_Stick(pad.sThumbLX, pad.sThumbLY, moveDead->value, &x, &y);
        else GP_Stick(pad.sThumbRX, pad.sThumbRY, lookDead->value, &x, &y);
        CL_RadialInput(localPlayer, x, y);
        if (!(buttons & activePad->wheelButton)) CL_RadialEnd(localPlayer, qtrue);
        if (!CL_RadialOpen(localPlayer)) {
            /* Permit immediate Y/Up handoff; suppress held fire/actions as before. */
            blocked |= Buttons() & ~(XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_DPAD_UP);
            stickBlocked = qtrue; peggedTime = 0.f; activePad->resetAim = qtrue;
            activePad->wheelPressed[0] = buttons & XINPUT_GAMEPAD_Y ? now : 0;
            activePad->wheelPressed[1] = buttons & XINPUT_GAMEPAD_DPAD_UP ? now : 0;
            activePad->wheelButton = 0;
        }
        MenuKeys(0, qfalse);
    } else if ((cls.keyCatchers & KEYCATCH_UI) && localPlayer != menuOwner) {
        MenuKeys(0, qfalse);
    } else if ((cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME)) || (!cls.keyCatchers && CL_LocalLoadoutOpen(localPlayer))) {
        float x, y;
        unsigned direction;
        GP_Stick(pad.sThumbLX, pad.sThumbLY, moveDead->value, &x, &y);
        direction = MenuDirection(x, y);
        /* One direction at a time; D-pad overrides the stick and opposing buttons cancel. */
        if (buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
            direction = 0;
            if ((buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) == XINPUT_GAMEPAD_DPAD_UP) direction = 4;
            else if ((buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) == XINPUT_GAMEPAD_DPAD_DOWN) direction = 8;
            else if ((buttons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) == XINPUT_GAMEPAD_DPAD_LEFT) direction = 16;
            else if ((buttons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) == XINPUT_GAMEPAD_DPAD_RIGHT) direction = 32;
        }
        keys = direction;
        if (buttons & XINPUT_GAMEPAD_A) keys |= 1;
        if (buttons & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) keys |= 2;
        if (buttons & XINPUT_GAMEPAD_X) keys |= 64;
        if ((keys & 3u) & ~menuKeys) {
            keys &= ~60u; /* Never repeat held navigation into a newly accepted/backed-out panel. */
            blocked |= buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
            stickBlocked = qtrue; stickDirection = 0;
        }
        if (keys != menuKeys) lastRepeat = now + 300;
        MenuKeys(keys, now >= lastRepeat);
        if (now >= lastRepeat) lastRepeat = now + 100;
        {
            int dx, dy, vx, vy, w, h;
            float scale;
            CL_LocalViewport(localPlayer, &vx, &vy, &w, &h);
            /* UI_MOUSE_EVENT uses virtual menu coordinates, not framebuffer pixels. */
            scale = cls.keyCatchers & KEYCATCH_UI ? 1.f : fminf(w / 640.f, h / 512.f);
            GP_Stick(pad.sThumbRX, pad.sThumbRY, lookDead->value, &x, &y);
            if (fabsf(x) < .35f && fabsf(y) < .35f) activePad->pointerBlocked = qfalse;
            dx = (int)(x * cls.frametime * scale);
            dy = (int)(-y * cls.frametime * scale);
            if (!activePad->pointerBlocked && (dx || dy)) {
                if (cls.keyCatchers & (KEYCATCH_CGAME | KEYCATCH_UI)) Sys_QueEvent(0,SE_MOUSE,dx,dy,0,NULL);
                else CL_LocalLoadoutEventForPlayer(localPlayer, CG_MOUSE_EVENT, dx, dy);
            }
        }
    } else {
        MenuKeys(0, qfalse);
        if (!menuOpened && ((buttons & ~previous) & XINPUT_GAMEPAD_START)) {
            menuOpened = qtrue; pendingOwner = localPlayer;
            Sys_QueEvent(0, SE_KEY, K_ESCAPE, qtrue, 0, NULL);
            Sys_QueEvent(0, SE_KEY, K_ESCAPE, qfalse, 0, NULL);
        }
        if (cls.state == CA_ACTIVE && !cls.keyCatchers && !clc.demoplaying) {
            unsigned pressed = buttons & ~previous, i;
            if (pressed & XINPUT_GAMEPAD_BACK) {
                /* Original limbo; inside it Back/B close it like Escape. */
                ReleaseHeld(); CL_OpenLimbo(localPlayer);
                RefreshContext(); previous=Buttons(); return qtrue;
            }
            static const unsigned wheelButtons[] = {XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_DPAD_UP};
            for (i = 0; i < 2; ++i) {
                unsigned button = wheelButtons[i];
                if (pressed & button) activePad->wheelPressed[i] = now;
                if ((buttons & button) && activePad->wheelPressed[i] && now - activePad->wheelPressed[i] >= GP_Clamp(wheelHold->value, 100, 1000)) {
                    if (CL_RadialBegin(localPlayer, i + 1)) { activePad->wheelButton = button; ReleaseHeld(); break; }
                } else if (!(buttons & button) && (previous & button) && activePad->wheelPressed[i]) {
                    if (now - activePad->wheelPressed[i] < GP_Clamp(wheelHold->value, 100, 1000))
                        PadCommand(localPlayer, i ? "weapalt" : "weapnext");
                    activePad->wheelPressed[i] = 0;
                }
            }
            for (i = 0; i < sizeof(holdCommands) / sizeof(holdCommands[0]); ++i) {
                if (CL_RadialOpen(localPlayer)) break;
                if (pressed & holdCommands[i].button) { PadCommand(localPlayer, holdCommands[i].down); activePad->holding |= holdCommands[i].button; }
            }
        }
        {
            unsigned i;
            for (i = 0; i < sizeof(holdCommands) / sizeof(holdCommands[0]); ++i) {
                if ((activePad->holding & holdCommands[i].button) && !(buttons & holdCommands[i].button)) {
                    PadCommand(localPlayer, holdCommands[i].up); activePad->holding &= ~holdCommands[i].button;
                }
            }
        }
    }
    previous = buttons;
    return qtrue;
}

/* Only shared menus have an owner; class panels never hand off. */
void IN_GamepadSetMenuOwner(int local) {
    char value[2] = "1";
    menuOwner = local >= 0 && local < ET_LOCAL_PLAYERS ? local : 0;
    pendingOwner = menuOwner;
    value[0] += menuOwner;
    Cvar_Set("ui_menuPlayer", value);
}

/* Connected XInput slots (bit per slot), polled at most once a second: polling absent slots is slow. */
int IN_GamepadConnectedMask(void) {
    static int mask, polledAt = -100000;
    XINPUT_STATE state;
    int slot, now = Sys_Milliseconds();
    if (!testMode && now - polledAt < 1000) return mask;
    polledAt = now; mask = 0;
    for (slot = 0; slot < ET_LOCAL_PLAYERS; ++slot) if (ReadState(slot, &state) == ERROR_SUCCESS) mask |= 1 << slot;
    return mask;
}
/* One resolver for the menu, local polling and online launches. Keyboard/mouse is a separate
 * split-screen device; inactive players and duplicate fixed choices never steal a controller. */
void IN_GamepadAssignments(int out[ET_LOCAL_PLAYERS], int count) {
    int i, slot, taken = 0, connectedMask = IN_GamepadConnectedMask();
    int keyboard = Cvar_VariableIntegerValue("cl_keyboardPlayer") - 1;
    if (keyboard >= count || keyboard < -1) keyboard = 0;
    for (i = 0; i < ET_LOCAL_PLAYERS; ++i) {
        int wanted = indices[i] ? indices[i]->integer : -1;
        out[i] = -2;
        if (i >= count || (count > 1 && i == keyboard)) continue;
        if (wanted >= 0 && wanted < ET_LOCAL_PLAYERS && !(taken & (1 << wanted))) {
            out[i] = wanted; taken |= 1 << wanted;
        } else if (wanted == -1) out[i] = -1;
    }
    for (i = 0; i < count; ++i) if (out[i] == -1) {
        for (slot = 0; slot < ET_LOCAL_PLAYERS && ((taken >> slot & 1) || !(connectedMask >> slot & 1)); ++slot) {}
        if (slot == ET_LOCAL_PLAYERS) for (slot = 0; slot < ET_LOCAL_PLAYERS && (taken >> slot & 1); ++slot) {}
        out[i] = slot < ET_LOCAL_PLAYERS ? slot : -2;
        if (out[i] >= 0) taken |= 1 << out[i];
    }
}
/* Player-setup menu status lines (ui_rustPad1..4), refreshed while menus are open. */
static void PublishDevices(void) {
    static int publishedAt = -100000;
    int i, assigned[ET_LOCAL_PLAYERS], mask, count = CL_SplitGamepadCount();
    int keyboard = Cvar_VariableIntegerValue("cl_keyboardPlayer");
    if (keyboard < 0 || keyboard > count) keyboard = 1;
    if (Sys_Milliseconds() - publishedAt < 500) return;
    publishedAt = Sys_Milliseconds();
    mask = IN_GamepadConnectedMask();
    IN_GamepadAssignments(assigned, count);
    for (i = 0; i < ET_LOCAL_PLAYERS; ++i) {
        const char *device = assigned[i] < 0 ? "No controller" :
            va("Controller %d %s", assigned[i] + 1, mask >> assigned[i] & 1 ? "connected" : "^3not connected");
        Cvar_Set(va("ui_rustPad%d", i + 1), i >= count ? "Not in this match" :
            count > 1 && keyboard == i + 1 ? "Keyboard & mouse" : va("%s%s", device, keyboard == i + 1 ? "^7 + keyboard & mouse" : ""));
    }
}

qboolean IN_GamepadFrame(void) {
    qboolean found = qfalse;
    int i, assigned[ET_LOCAL_PLAYERS], count = CL_SplitGamepadCount();
    IN_GamepadAssignments(assigned, count);
    menuOpened = qfalse;
    if (cls.keyCatchers & KEYCATCH_UI) PublishDevices();
    /* Whoever opened the menu owns it: a controller's Start, otherwise player one's keyboard/mouse. */
    if ((cls.keyCatchers & KEYCATCH_UI) && !uiWasOpen) {
        IN_GamepadSetMenuOwner(pendingOwner >= 0 && pendingOwner < count ? pendingOwner : CL_KeyboardPlayer());
    }
    if (cls.keyCatchers & KEYCATCH_UI) pendingOwner = -1;
    uiWasOpen = (cls.keyCatchers & KEYCATCH_UI) != 0;
    /* Main-menu navigation remains usable when its previous owner uses keyboard/mouse. */
    if ((cls.keyCatchers & KEYCATCH_UI) && (menuOwner >= count || assigned[menuOwner] < 0)) {
        for (i = 0; i < count; ++i) if (assigned[i] >= 0) { menuOwner = i; break; }
    }
    for (i = 0; i < count; ++i) found |= PlayerFrame(i, assigned[i]);
    for (i = count; i < ET_LOCAL_PLAYERS; ++i) { activePad = &players[i]; ClearPlayer(); connected = qfalse; controller = -1; }
    if (menuOwner >= count) menuOwner = 0;
    activePad = &players[0];
    return found;
}

qboolean CL_GamepadAim(float level, float yaw, float pitch) {
    activePad = &players[CL_SplitPlayer()];
    activePad->resetAim = qfalse;
    if (!isfinite(level) || !isfinite(yaw) || !isfinite(pitch)) level = yaw = pitch = 0.f;
    cl.gamepadAimLevel = GP_Clamp(level, 0.f, 1.f);
    cl.gamepadAimYaw = GP_Clamp(yaw, -180.f, 180.f);
    cl.gamepadAimPitch = GP_Clamp(pitch, -90.f, 90.f);
    cl.gamepadAimTime = cls.realtime;
    return connected && enabled->integer && InputActive() && !cls.keyCatchers && !CL_LocalLoadoutOpen(CL_SplitPlayer()) && !CL_RadialOpen(CL_SplitPlayer()) &&
        cls.state == CA_ACTIVE && !clc.demoplaying && cls.realtime - cl.gamepadMouseTime > 250;
}

void CL_GamepadMove(usercmd_t *cmd, qboolean mouseMoved) {
    float mx, my, lx, ly, dt, yaw, pitch, acceleration;
    unsigned buttons;
    activePad = &players[CL_SplitPlayer()];
    RefreshContext(); /* UI can change after polling but before command creation in this same frame. */
    if (activePad->resetAim) { cl.gamepadAimLevel = 0.f; activePad->resetAim = qfalse; }
    if (!connected || !enabled->integer || !InputActive() || cls.keyCatchers || CL_LocalLoadoutOpen(CL_SplitPlayer()) || CL_RadialOpen(CL_SplitPlayer()) ||
        cls.state != CA_ACTIVE || clc.demoplaying) { peggedTime = 0.f; return; }
    buttons = Buttons() & ~blocked;
    GP_Stick(pad.sThumbLX, pad.sThumbLY, moveDead->value, &mx, &my);
    GP_Stick(pad.sThumbRX, pad.sThumbRY, lookDead->value, &lx, &ly);
    if (stickBlocked) {
        if (fmaxf(fmaxf(fabsf(mx), fabsf(my)), fmaxf(fabsf(lx), fabsf(ly))) < .35f) stickBlocked = qfalse;
        else mx = my = lx = ly = 0.f;
    }
    cmd->rightmove = ClampChar(cmd->rightmove + (int)(mx * 127.f));
    cmd->forwardmove = ClampChar(cmd->forwardmove + (int)(my * 127.f));
    if (mx || my) cmd->buttons &= ~BUTTON_WALKING;
    if (buttons & PAD_FIRE) cmd->buttons |= BUTTON_ATTACK;
    if (buttons & XINPUT_GAMEPAD_A) cmd->upmove = 127;
    else if (buttons & XINPUT_GAMEPAD_B) cmd->upmove = -127;
    if (buttons & XINPUT_GAMEPAD_X) cmd->wbuttons |= WBUTTON_RELOAD;
    if (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) cmd->buttons |= BUTTON_SPRINT;
    if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) cmd->buttons |= BUTTON_ACTIVATE;
    if (buttons & PAD_ZOOM) cmd->wbuttons |= WBUTTON_ZOOM;
    if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) cmd->wbuttons |= WBUTTON_PRONE;
    if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) cmd->wbuttons |= WBUTTON_LEANLEFT;
    if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) cmd->wbuttons |= WBUTTON_LEANRIGHT;
    if (buttons || mx || my || lx || ly) cmd->buttons |= BUTTON_ANY;
    dt = GP_Clamp(frame_msec * .001f, 0.f, .05f);
    acceleration = GP_Acceleration(lx, dt, accelTime->value, accelScale->value, &peggedTime);
    yaw = -GP_LookCurve(lx) * GP_Clamp(yawSpeed->value, 10.f, 720.f) * acceleration;
    pitch = GP_LookCurve(ly) * GP_Clamp(pitchSpeed->value, 10.f, 720.f) * (invert->integer ? 1.f : -1.f);
    yaw *= GP_Clamp(cl.cgameSensitivity, .01f, 2.f);
    pitch *= GP_Clamp(cl.cgameSensitivity, .01f, 2.f);
    /* Only controller look/throttle activates magnetism; mouse always wins. */
    if (!mouseMoved && cls.realtime - cl.gamepadMouseTime > 250 &&
        cls.realtime - cl.gamepadAimTime >= 0 && cls.realtime - cl.gamepadAimTime < 100 &&
        cl.snap.ps.pm_type == PM_NORMAL && cl.snap.ps.stats[STAT_HEALTH] > 0 &&
        (mx || my || lx || ly)) {
        float slowdown = friction->value, tracking = adhesion->value;
        GP_AssistStrength(assistStrength->value, &slowdown, &tracking);
        yaw = GP_Assist(yaw, cl.gamepadAimYaw, cl.gamepadAimLevel, slowdown, tracking, 180.f);
        pitch = GP_Assist(pitch, cl.gamepadAimPitch, cl.gamepadAimLevel, slowdown, tracking, 90.f);
    }
    cl.viewangles[YAW] += yaw * dt;
    cl.viewangles[PITCH] += pitch * dt;
}
