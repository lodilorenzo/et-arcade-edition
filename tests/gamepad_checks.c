/* One runnable check: actual transport/usercmd code with a fake XInput device. */
#undef NDEBUG
#include <assert.h>
#include "../native/gamepad.c"

static int radialKinds[ET_LOCAL_PLAYERS], radialCommits;
static float radialX, radialY;
static qboolean radialReady = qtrue;
void CL_RadialInit(void) {}
qboolean CL_RadialOpen(int local) { return radialKinds[local] != 0; }
qboolean CL_RadialBegin(int local,int kind) { if (!radialReady) return qfalse; radialKinds[local]=kind; return qtrue; }
void CL_RadialCancel(int local) { radialKinds[local]=0; }
void CL_RadialReset(void) { memset(radialKinds,0,sizeof(radialKinds)); }
void CL_RadialInput(int local,float x,float y) { (void)local; radialX=x; radialY=y; }
void CL_RadialEnd(int local,qboolean commit) { if (commit) ++radialCommits; CL_RadialCancel(local); }
clientActive_t cl;
clientStatic_t cls;
clientConnection_t clc;
unsigned frame_msec;
qboolean in_appactive;
static int paneRole;
static qboolean paneFocus;
qboolean CL_OnlinePaneInputActive(void) { return paneFocus; }
int CL_OnlinePane(void) { return paneRole; }
static XINPUT_STATE device, device2, device3, device4;
static int online2, online3, online4, splitCount = 1, localPlayer;
static int localMasks, loadoutEpochs[ET_LOCAL_PLAYERS], localEvents[ET_LOCAL_PLAYERS], limboOpened[ET_LOCAL_PLAYERS];
/* Limbo stand-in: per-view panel that B/Back (K_JOY2) closes, like the stock limbo. */
void CL_OpenLimbo(int local) { ++limboOpened[local]; localMasks|=1<<local; ++loadoutEpochs[local]; }
qboolean CL_LocalLoadoutOpen(int local) { return (localMasks & (1 << local)) != 0; }
int CL_LocalLoadoutEpoch(int local) { return loadoutEpochs[local]; }
void CL_LocalViewport(int local, int *x, int *y, int *w, int *h) {
    *w = cls.glconfig.vidWidth / (splitCount > 1 ? 2 : 1);
    *h = cls.glconfig.vidHeight / (splitCount == 4 ? 2 : 1);
    *x = local % 2 * *w; *y = local / 2 * *h;
}
qboolean CL_LocalLoadoutEventForPlayer(int local, int command, int a, int b) {
    if (!CL_LocalLoadoutOpen(local)) return qfalse;
    ++localEvents[local];
    if (command==CG_KEY_EVENT && a==K_JOY2 && b) { localMasks&=~(1<<local); ++loadoutEpochs[local]; }
    return qtrue;
}
int CL_SplitPlayer(void) { return localPlayer; }
int CL_SplitGamepadCount(void) { return splitCount; }
int CL_KeyboardPlayer(void) {
    int owner = Cvar_VariableIntegerValue("cl_keyboardPlayer") - 1;
    return owner == -1 ? -1 : owner >= 0 && owner < splitCount ? owner : 0;
}
qboolean CL_SplitEnabled(void) { return splitCount > 1; }
void Cmd_AddCommand(const char *name, xcommand_t command) { (void)name; (void)command; }
void Cmd_RemoveCommand(const char *name) { (void)name; }
int Cmd_Argc(void) { return 0; }
char *Cmd_Argv(int i) { (void)i; return ""; }
static int online = 1, clockTime = 1000, eventCount, inputEpoch, localLoadout, regressionAllowed, foreignMenu;
static struct {int type, key, down;} events[128];
static cvar_t cvars[64];
static int cvarCount;

cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    int i;
    for (i = 0; i < cvarCount; ++i) if (!strcmp(cvars[i].name, name)) return &cvars[i];
    assert(cvarCount < 64);
    cvars[i].name = _strdup(name); /* Names may come from va(). */
    cvars[i].value = (float)atof(value);
    cvars[i].integer = atoi(value);
    cvars[i].flags = flags;
    ++cvarCount;
    return &cvars[i];
}
void QDECL Com_Printf(const char *fmt, ...) {(void)fmt;}
int Sys_Milliseconds(void) {return clockTime;}
int Cvar_VariableIntegerValue(const char *name) {
    if (!strcmp(name, "developer") || !strcmp(name, "ui_rustMenuTest")) return regressionAllowed;
    if (!strcmp(name, "ui_localLoadout")) return localLoadout;
    if (!strcmp(name, "ui_rustSlice")) return !foreignMenu;
    if (!strcmp(name, "ui_menuInputEpoch")) return inputEpoch;
    return Cvar_Get(name, "0", 0)->integer; /* Player-setup cvars. */
}
void Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int length, void *ptr) {
    (void)time; (void)length; (void)ptr;
    assert(eventCount < 128);
    events[eventCount].type = type; events[eventCount].key = value; events[eventCount++].down = value2;
}
static char sent[32][64], menuPlayer[4];
static int sentCount;
qboolean CL_LocalCgameCommand(int local,const char *command) {
    char text[64];
    if (local) snprintf(text,sizeof(text),"split_cgame %d %s\n",local+1,command);
    else snprintf(text,sizeof(text),"%s\n",command);
    strcpy(sent[sentCount++ % 32],text); return qtrue;
}
void Cbuf_AddText(const char *text) { (void)text; assert(0); } /* Pad commands must never wait in the queue. */
static int Sent(const char *text) {
    int i;
    for (i = 0; i < sentCount && i < 32; ++i) if (!strcmp(sent[i], text)) return 1;
    return 0;
}
static void ClearSent(void) { sentCount = 0; }
void Cvar_Set(const char *name, const char *value) {
    if (!strcmp(name,"ui_menuPlayer")) { assert(strlen(value)<4); strcpy(menuPlayer,value); }
    else { cvar_t *cv=Cvar_Get(name,value,CVAR_ARCHIVE); cv->integer=atoi(value); cv->value=(float)atof(value); }
}
char *va(const char *format, ...) {
    static char text[1024]; va_list args;
    va_start(args, format); _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args); va_end(args);
    return text;
}
signed char ClampChar(int x) {return (signed char)(x < -128 ? -128 : x > 127 ? 127 : x);}
static DWORD WINAPI DeviceState(DWORD slot, XINPUT_STATE *out) {
    if ((slot == 0 && !online) || (slot == 1 && !online2) || (slot == 2 && !online3) || (slot == 3 && !online4) || slot > 3) return ERROR_DEVICE_NOT_CONNECTED;
    *out = slot == 0 ? device : slot == 1 ? device2 : slot == 2 ? device3 : device4;
    return ERROR_SUCCESS;
}
static void Poll(void) {clockTime += 16; cls.realtime = clockTime; IN_GamepadFrame();}
static usercmd_t Move(qboolean mouse) {
    usercmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    CL_GamepadMove(&cmd, mouse);
    return cmd;
}

int main(void) {
    float x, y, held = 0.f, yaw, pitch, point[3];
    float p[] = {100.f, 0.f, 0.f}, v[] = {0.f, 10.f, 10.f};
    float eye[] = {0.f, 0.f, 10.f}, forward[] = {1.f, 0.f, 0.f}, origin[] = {100.f, 20.f, 0.f};
    int raw;
    usercmd_t cmd;
    paneRole = 4; in_appactive = qtrue; paneFocus = qfalse;
    assert(!InputActive()); /* An offscreen pane must not retain input after host focus loss. */
    in_appactive = qfalse; paneFocus = qtrue; assert(InputActive());
    paneRole = 0; paneFocus = qfalse;
    assert(GP_Axis(-32768, .2f) == -1.f && GP_Axis(32767, .2f) == 1.f);
    assert(GP_Axis(5000, .2f) == 0.f && GP_Axis(-5000, .2f) == 0.f);
    for (raw = -32768; raw < 32767; ++raw) assert(GP_Axis(raw, .2f) <= GP_Axis(raw + 1, .2f));
    GP_Stick(32767, 32767, .2f, &x, &y); assert(x == 1.f && y == 1.f);
    GP_Stick(0, 0, .2f, &x, &y); assert(x == 0.f && y == 0.f);
    assert(GP_LookCurve(0.f) == 0.f && GP_LookCurve(1.f) == 1.f && GP_LookCurve(-.5f) == -.25f);
    assert(GP_Acceleration(1.f, .25f, .5f, 2.f, &held) == 1.f);
    assert(GP_Acceleration(1.f, .25f, .5f, 2.f, &held) == 1.5f);
    assert(GP_Acceleration(1.f, .25f, .5f, 2.f, &held) == 2.f);
    assert(GP_Acceleration(0.f, .25f, .5f, 2.f, &held) == 1.f && held == 0.f);
    assert(GP_Attenuation(2.f, 8.f) == 1.f && GP_Attenuation(6.f, 8.f) == .5f && GP_Attenuation(8.f, 8.f) == 0.f);
    assert(GP_Attenuation(NAN, 8.f) == 0.f && GP_Clamp(NAN, 0.f, 1.f) == 0.f);
    GP_CapsulePoint(eye, forward, origin, 0.f, 40.f, 5.f, point);
    assert(point[0] == 100.f && point[1] == 15.f && point[2] == 10.f);
    GP_TargetVelocity(p, v, &yaw, &pitch); assert(fabsf(yaw - 5.729578f) < .0001f && fabsf(pitch + 5.729578f) < .0001f);
    v[0] = NAN; GP_TargetVelocity(p, v, &yaw, &pitch); assert(yaw == 0.f && pitch == 0.f); v[0] = 0.f;
    p[0] = 0.f; GP_TargetVelocity(p, v, &yaw, &pitch); assert(yaw == 0.f && pitch == 0.f);
    assert(GP_Assist(100.f, 500.f, 1.f, .5f, .5f, 180.f) == 140.f);
    {   /* Strength scales both terms; slowdown never reaches a full stop; out-of-range strengths clamp. */
        float f = .65f, a = .45f; GP_AssistStrength(1.f, &f, &a); assert(f == .65f && a == .45f);
        f = .65f; a = .45f; GP_AssistStrength(.4f, &f, &a); assert(fabsf(f - .26f) < 1e-5f && fabsf(a - .18f) < 1e-5f);
        f = .65f; a = .45f; GP_AssistStrength(2.f, &f, &a); assert(f == .9f && fabsf(a - .9f) < 1e-5f);
        f = .65f; a = .45f; GP_AssistStrength(9.f, &f, &a); assert(f == .9f && fabsf(a - .9f) < 1e-5f);
        f = .65f; a = .45f; GP_AssistStrength(-1.f, &f, &a); assert(f == 0.f && a == 0.f);
    }

    Cvar_Get("cl_gamepadWheelHold","250",CVAR_ARCHIVE); /* Existing profile's old default. */
    IN_GamepadInit(); /* It is OK if this machine has no physical controller. */
    assert(wheelHold->integer==150);
    Cvar_Set("cl_gamepadWheelHold","250"); IN_GamepadInit(); assert(wheelHold->integer==250); /* Later tuning survives. */
    Cvar_Set("cl_gamepadWheelHold","150");
    getState = DeviceState;
    {   /* Player setup: fixed controllers first, Auto takes the first free connected one, None (-2) stays empty;
           with nothing connected Auto still gets distinct slots (online views). */
        int assigned[ET_LOCAL_PLAYERS], saved = online;
        splitCount = 4; Cvar_Set("cl_keyboardPlayer", "0");
        online = 0; online2 = 1; online3 = 0; online4 = 1; clockTime += 2000;
        Cvar_Set("in_gamepadIndex", "-1"); Cvar_Set("in_gamepadIndex2", "3"); Cvar_Set("in_gamepadIndex3", "-2"); Cvar_Set("in_gamepadIndex4", "-1");
        IN_GamepadAssignments(assigned, splitCount);
        assert(IN_GamepadConnectedMask() == 10 && assigned[0] == 1 && assigned[1] == 3 && assigned[2] == -2 && assigned[3] == 0);
        online2 = online4 = 0; clockTime += 2000;
        Cvar_Set("in_gamepadIndex2", "-1"); Cvar_Set("in_gamepadIndex3", "-1");
        IN_GamepadAssignments(assigned, splitCount);
        assert(assigned[0] == 0 && assigned[1] == 1 && assigned[2] == 2 && assigned[3] == 3);
        Cvar_Set("in_gamepadIndex", "-1"); Cvar_Set("in_gamepadIndex2", "-1"); Cvar_Set("in_gamepadIndex3", "-1"); Cvar_Set("in_gamepadIndex4", "-1");
        /* Every player can be the separate keyboard/mouse owner; three pads cover the others. */
        online = online2 = online3 = online4 = 1; clockTime += 2000;
        for (int owner = 1; owner <= 4; ++owner) {
            int used = 0;
            Cvar_Set("cl_keyboardPlayer", va("%d", owner)); IN_GamepadAssignments(assigned, 4);
            assert(assigned[owner - 1] == -2);
            for (int p = 0; p < 4; ++p) if (p != owner - 1) {
                assert(assigned[p] >= 0 && !(used & (1 << assigned[p]))); used |= 1 << assigned[p];
            }
        }
        /* Online launch after the browser closes: current client is single, launch still has four devices. */
        splitCount = 1; Cvar_Set("cl_keyboardPlayer", "4"); IN_GamepadAssignments(assigned, 4);
        assert(assigned[0] == 0 && assigned[1] == 1 && assigned[2] == 2 && assigned[3] == -2);
        Cvar_Set("cl_keyboardPlayer", "0"); splitCount = 3;
        Cvar_Set("in_gamepadIndex", "-1"); Cvar_Set("in_gamepadIndex2", "0");
        Cvar_Set("in_gamepadIndex3", "0"); Cvar_Set("in_gamepadIndex4", "1");
        IN_GamepadAssignments(assigned, splitCount);
        assert(assigned[0] == 1 && assigned[1] == 0 && assigned[2] == -2 && assigned[3] == -2);
        /* Polling must use exactly the same fixed-first mapping (Auto cannot steal slot zero). */
        in_appactive = qtrue; Poll(); Poll();
        assert(players[0].deviceIndex == 1 && players[1].deviceIndex == 0 && !players[2].isConnected);
        Cvar_Set("in_gamepadIndex", "-1"); Cvar_Set("in_gamepadIndex2", "-1");
        Cvar_Set("in_gamepadIndex3", "-1"); Cvar_Set("in_gamepadIndex4", "-1");
        splitCount = 1;
        online = saved; online2 = online3 = online4 = 0; clockTime += 2000; IN_GamepadClear();
    }
    in_appactive = qtrue; cls.state = CA_ACTIVE; frame_msec = 16;
    cl.cgameSensitivity = 1.f; cl.snap.ps.pm_type = PM_NORMAL; cl.snap.ps.stats[STAT_HEALTH] = 100;
    device.Gamepad.bRightTrigger = 255;
    Poll(); cmd = Move(qfalse); assert(!(cmd.buttons & BUTTON_ATTACK)); /* hot-plug suppression */
    device.Gamepad.bRightTrigger = 0; Poll();
    device.Gamepad.bRightTrigger = 255;
    device.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_LEFT_SHOULDER |
        XINPUT_GAMEPAD_RIGHT_SHOULDER;
    device.Gamepad.bLeftTrigger = 255; /* LT zooms */
    device.Gamepad.sThumbLY = 32767; device.Gamepad.sThumbRX = 32767;
    Poll(); cmd = Move(qfalse);
    assert(cmd.forwardmove == 127 && cmd.upmove == 127 && (cmd.buttons & BUTTON_ATTACK) &&
        (cmd.buttons & BUTTON_SPRINT) && (cmd.buttons & BUTTON_ACTIVATE) && (cmd.wbuttons & WBUTTON_RELOAD) &&
        (cmd.wbuttons & WBUTTON_ZOOM) && cl.viewangles[YAW] < 0.f);
    device.Gamepad.wButtons &= ~XINPUT_GAMEPAD_RIGHT_SHOULDER; device.Gamepad.bLeftTrigger = 0;
    device.Gamepad.sThumbRX = 0; Poll();
    CL_GamepadAim(1.f, 30.f, 0.f); cl.viewangles[YAW] = 0.f;
    Move(qfalse); assert(cl.viewangles[YAW] > 0.f); /* strafe/throttle adhesion */
    cl.viewangles[YAW] = 0.f; Move(qtrue); assert(cl.viewangles[YAW] == 0.f);
    cl.gamepadMouseTime = cls.realtime; Move(qfalse); assert(cl.viewangles[YAW] == 0.f);
    cl.gamepadMouseTime = 0; cl.gamepadAimTime -= 101; Move(qfalse); assert(cl.viewangles[YAW] == 0.f);
    CL_GamepadAim(NAN, INFINITY, NAN); assert(cl.gamepadAimLevel == 0.f && isfinite(cl.gamepadAimYaw));

    /* D-pad: prone/lean are held wbuttons, up is the edge-triggered alternate weapon mode. */
    memset(&device, 0, sizeof(device)); Poll(); Poll(); ClearSent();
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT; Poll(); cmd = Move(qfalse);
    assert((cmd.wbuttons & WBUTTON_PRONE) && (cmd.wbuttons & WBUTTON_LEANLEFT) && !(cmd.wbuttons & WBUTTON_LEANRIGHT));
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_UP; Poll(); cmd = Move(qfalse);
    assert((cmd.wbuttons & WBUTTON_LEANRIGHT) && !(cmd.wbuttons & WBUTTON_PRONE) && !Sent("weapalt\n"));
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_RIGHT; Poll(); assert(Sent("weapalt\n"));
    ClearSent(); Poll(); assert(!Sent("weapalt\n"));
    /* Back opens the original limbo once (held Back does not close it); a second Back closes it. */
    device.Gamepad.wButtons = XINPUT_GAMEPAD_BACK; Poll(); Poll(); cmd = Move(qfalse);
    assert(limboOpened[0]==1 && CL_LocalLoadoutOpen(0) && !sentCount && !cmd.buttons && !cmd.forwardmove);
    device.Gamepad.wButtons=0; Poll(); assert(CL_LocalLoadoutOpen(0));
    device.Gamepad.wButtons=XINPUT_GAMEPAD_BACK; Poll(); assert(!CL_LocalLoadoutOpen(0) && limboOpened[0]==1);
    device.Gamepad.wButtons=0; Poll(); Poll(); ClearSent();
    /* L3 holds the original scoreboard and no longer crouches. */
    device.Gamepad.wButtons=XINPUT_GAMEPAD_LEFT_THUMB; Poll(); cmd=Move(qfalse);
    assert(Sent("+scores\n") && cmd.upmove==0);
    ClearSent(); device.Gamepad.wButtons=0; Poll(); assert(Sent("-scores\n"));
    Poll(); ClearSent();
    /* Right-stick still holds the command map; shared menus release it safely. */
    device.Gamepad.wButtons=XINPUT_GAMEPAD_RIGHT_THUMB; Poll(); cmd=Move(qfalse);
    assert(Sent("+mapexpand\n") && !(cmd.wbuttons & WBUTTON_ZOOM));
    ClearSent(); device.Gamepad.wButtons=0; Poll(); assert(Sent("-mapexpand\n"));
    device.Gamepad.wButtons=XINPUT_GAMEPAD_RIGHT_THUMB; Poll(); ClearSent();
    cls.keyCatchers=KEYCATCH_UI; Poll(); assert(Sent("-mapexpand\n") && !strcmp(menuPlayer,"1"));
    cls.keyCatchers=0; device.Gamepad.wButtons=0; Poll(); Poll(); ClearSent();

    cls.keyCatchers = KEYCATCH_UI; Poll(); cmd = Move(qfalse); assert(cmd.buttons == 0 && cmd.forwardmove == 0);
    device.Gamepad.wButtons = 0; device.Gamepad.bRightTrigger = 0; device.Gamepad.sThumbLY = 0; Poll();
    eventCount = 0; device.Gamepad.wButtons = XINPUT_GAMEPAD_A; Poll();
    assert(eventCount == 1 && events[0].key == K_JOY1 && events[0].down);
    cls.keyCatchers = 0; cmd = Move(qfalse); assert(cmd.upmove == 0); /* Resume can happen AFTER polling, in the same frame. */
    Poll();
    assert(eventCount == 2 && events[1].key == K_JOY1 && !events[1].down);
    cmd = Move(qfalse); assert(cmd.upmove == 0); /* menu accept cannot jump on resume */
    device.Gamepad.wButtons = 0; Poll();
    cls.keyCatchers = KEYCATCH_UI; Poll();
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN; Poll();
    clockTime += 400; Poll(); /* D-pad repeats, but accept/back never repeat. */
    device.Gamepad.wButtons = 0; Poll(); eventCount = 0;
    device.Gamepad.wButtons = XINPUT_GAMEPAD_A; Poll();
    assert(eventCount == 1 && events[0].key == K_JOY1 && events[0].down);
    device.Gamepad.wButtons = 0; Poll();
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN; Poll(); eventCount=0;
    device.Gamepad.wButtons |= XINPUT_GAMEPAD_A; Poll();
    assert(eventCount==2 && events[0].key==K_JOY1 && events[0].down && events[1].key==K_JOY4 && !events[1].down);
    eventCount=0; clockTime+=400; Poll(); assert(!eventCount); /* No held direction into next foreign menu (no epoch API). */
    memset(&device,0,sizeof(device)); Poll();
    cls.frametime=16; cls.glconfig.vidWidth=3440; cls.glconfig.vidHeight=1440;
    /* Right-stick pointer works in both custom and stock UI, without held aim leaking into a new panel. */
    for (foreignMenu=0; foreignMenu<=1; ++foreignMenu) {
        ++inputEpoch; device.Gamepad.sThumbRX=32767; Poll(); eventCount=0;
        Poll(); assert(eventCount==0);
        device.Gamepad.sThumbRX=0; Poll(); eventCount=0;
        device.Gamepad.sThumbRX=32767; device.Gamepad.sThumbRY=32767; Poll();
        assert(eventCount==1 && events[0].type==SE_MOUSE && events[0].key==16 && events[0].down==-16);
        cmd=Move(qfalse); assert(!cmd.buttons && !cmd.forwardmove && !cmd.rightmove);
        memset(&device,0,sizeof(device)); Poll(); eventCount=0;
        device.Gamepad.sThumbRX=5000; device.Gamepad.sThumbRY=-5000; Poll(); assert(eventCount==0); /* Drift stays inside the deadzone. */
        memset(&device,0,sizeof(device)); Poll();
    }
    foreignMenu=0; cls.keyCatchers = 0; Poll();
    device.Gamepad.wButtons = XINPUT_GAMEPAD_B; Poll(); cmd = Move(qfalse); assert(cmd.upmove == -127);
    in_appactive = qfalse; Poll(); cmd = Move(qfalse); assert(cmd.upmove == 0);
    in_appactive = qtrue; Poll(); cmd = Move(qfalse); assert(cmd.upmove == 0); /* held on refocus */
    online = 0; Poll(); cmd = Move(qfalse); assert(cmd.buttons == 0 && cmd.upmove == 0);
    online = 1; memset(&device, 0, sizeof(device)); Poll();
    device.Gamepad.wButtons = XINPUT_GAMEPAD_START; eventCount = 0; Poll();
    assert(eventCount == 2 && events[0].key == K_ESCAPE && events[0].down && !events[1].down);
    device.Gamepad.wButtons = 0; Poll(); cls.keyCatchers = KEYCATCH_UI; Poll(); Poll();
    eventCount = 0; device.Gamepad.sThumbLX = 32767; Poll();
    assert(eventCount == 1 && events[0].type == SE_KEY && events[0].key == K_JOY6 && events[0].down);
    eventCount = 0; inputEpoch++; Poll();
    assert(eventCount == 1 && events[0].key == K_JOY6 && !events[0].down);
    eventCount = 0; clockTime += 400; Poll(); assert(eventCount == 0); /* Held stick cannot repeat into a new panel. */
    device.Gamepad.sThumbLX = 0; Poll(); device.Gamepad.sThumbLY = -32768; Poll();
    assert(eventCount == 1 && events[0].key == K_JOY4 && events[0].down);
    device.Gamepad.sThumbLY = 0; Poll(); eventCount = 0;
    device.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN; Poll(); assert(eventCount == 0);
    enabled->integer = 0; Poll(); assert(connected); /* Menu remains operable so the pad can be re-enabled. */
    cls.keyCatchers = 0; Poll(); assert(!connected);
    enabled->integer = 1; splitCount = 2; online2 = 1;
    memset(&device, 0, sizeof(device)); memset(&device2, 0, sizeof(device2));
    Poll(); Poll();
    device.Gamepad.sThumbLY = 32767;
    device2.Gamepad.sThumbLX = -32768; device2.Gamepad.sThumbRX = -32768; device2.Gamepad.bRightTrigger = 255;
    Poll(); localPlayer = 0; cmd = Move(qfalse);
    assert(cmd.forwardmove == 127 && !cmd.rightmove && !(cmd.buttons & BUTTON_ATTACK));
    localPlayer = 1; cmd = Move(qfalse);
    assert(cmd.rightmove == -127 && !cmd.forwardmove && (cmd.buttons & BUTTON_ATTACK));
    assert(players[0].isConnected && players[1].isConnected);
    online2 = 0; Poll(); cmd = Move(qfalse); assert(!cmd.buttons && !cmd.rightmove);
    localPlayer = 0; cmd = Move(qfalse); assert(cmd.forwardmove == 127); /* Unplugging player two does not reassign/mute player one. */
    online2 = 1; memset(&device2, 0, sizeof(device2)); Poll();
    device2.Gamepad.wButtons = XINPUT_GAMEPAD_START; eventCount = 0; Poll(); assert(eventCount == 2 && events[0].key == K_ESCAPE);
    cls.keyCatchers = KEYCATCH_UI; Poll(); assert(!strcmp(menuPlayer, "2")); device2.Gamepad.wButtons = 0; Poll();
    eventCount = 0; device2.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN; Poll();
    assert(eventCount == 1 && events[0].key == K_JOY4 && events[0].down); /* Opener owns the shared menu. */
    device.Gamepad.sThumbLY=0; device.Gamepad.sThumbRX=0; device2.Gamepad.wButtons=0; Poll(); eventCount=0;
    device.Gamepad.sThumbRX=32767; Poll(); assert(eventCount==0); /* Another player's stick cannot move the shared cursor. */
    device2.Gamepad.sThumbRX=-32768; Poll();
    assert(eventCount==1 && events[0].type==SE_MOUSE && events[0].key==-16 && !events[0].down);
    device.Gamepad.sThumbRX=device2.Gamepad.sThumbRX=0;
    cls.keyCatchers = 0;
    device.Gamepad.wButtons = device2.Gamepad.wButtons = 0; Poll();
    ClearSent(); device2.Gamepad.wButtons = XINPUT_GAMEPAD_Y; Poll(); assert(!Sent("split_cgame 2 weapnext\n"));
    device2.Gamepad.wButtons = 0; Poll(); assert(Sent("split_cgame 2 weapnext\n") && !Sent("weapnext\n"));
    ClearSent(); device.Gamepad.wButtons = XINPUT_GAMEPAD_Y; Poll(); device.Gamepad.wButtons=0; Poll(); assert(Sent("weapnext\n") && !Sent("split_cgame 2 weapnext\n"));
    /* Only the limbo owner gets right-stick pointer events; held aim is suppressed. */
    memset(&device, 0, sizeof(device)); memset(&device2, 0, sizeof(device2)); Poll();
    localLoadout = 1; cls.frametime = 16; cls.glconfig.vidWidth = 1280; cls.glconfig.vidHeight = 720;
    localMasks = 2; cls.keyCatchers = 0; memset(localEvents,0,sizeof(localEvents));
    device2.Gamepad.sThumbRX = 32767; Poll(); eventCount = 0; Poll(); assert(eventCount == 0);
    device2.Gamepad.sThumbRX = 0; Poll(); eventCount = 0;
    device.Gamepad.sThumbRX = 32767; Poll(); assert(eventCount == 0);
    device2.Gamepad.sThumbRX = 32767; Poll();
    assert(eventCount == 0 && localEvents[0] == 0 && localEvents[1] == 1);
    /* Four devices stay unique; simultaneous class input never reaches another player. */
    localMasks = 0; splitCount = 4; online3 = online4 = 1;
    memset(&device, 0, sizeof(device)); memset(&device2, 0, sizeof(device2)); Poll(); Poll();
    assert(players[0].deviceIndex == 0 && players[1].deviceIndex == 1 && players[2].deviceIndex == 2 && players[3].deviceIndex == 3);
    localMasks = 10; Poll(); memset(localEvents, 0, sizeof(localEvents));
    device2.Gamepad.wButtons = device4.Gamepad.wButtons = XINPUT_GAMEPAD_DPAD_RIGHT;
    device3.Gamepad.sThumbLY = 32767; Poll();
    assert(localEvents[0] == 0 && localEvents[1] == 1 && localEvents[2] == 0 && localEvents[3] == 1);
    localPlayer = 1; cmd = Move(qfalse); assert(!cmd.forwardmove && !cmd.buttons);
    localPlayer = 2; cmd = Move(qfalse); assert(cmd.forwardmove == 127);
    localPlayer = 3; cmd = Move(qfalse); assert(!cmd.forwardmove && !cmd.buttons);
    device4.Gamepad.wButtons = 0; localMasks = 0; Poll(); ClearSent();
    device4.Gamepad.wButtons = XINPUT_GAMEPAD_Y; Poll(); assert(!Sent("split_cgame 4 weapnext\n"));
    memset(&device4, 0, sizeof(device4)); Poll(); assert(Sent("split_cgame 4 weapnext\n"));
    ClearSent(); device4.Gamepad.wButtons=XINPUT_GAMEPAD_Y; Poll(); clockTime+=300; Poll();
    assert(CL_RadialOpen(3) && !CL_RadialOpen(0));
    localPlayer=3; device4.Gamepad.sThumbLY=32767; device4.Gamepad.sThumbRX=-32768; device4.Gamepad.bRightTrigger=255; Poll();
    assert(radialX==0 && radialY==1); /* Equipment ignores the right stick. */
    cmd=Move(qfalse); assert(!cmd.buttons && !cmd.forwardmove);
    device4.Gamepad.wButtons |= XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_X;
    eventCount=0; Poll(); assert(CL_RadialOpen(3) && !eventCount && !radialCommits);
    device4.Gamepad.wButtons=0; Poll(); assert(!CL_RadialOpen(3) && radialCommits==1 && !Sent("split_cgame 4 weapnext\n"));
    assert(radialX==0 && radialY==1); /* Direction survives button release. */
    cmd=Move(qfalse); assert(!(cmd.buttons & BUTTON_ATTACK)); /* Held trigger after commit. */
    memset(&device4,0,sizeof(device4)); Poll();
    device4.Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_UP; Poll(); clockTime+=300; Poll();
    device4.Gamepad.sThumbLX=32767; device4.Gamepad.sThumbRY=-32768; Poll();
    assert(CL_RadialOpen(3) && radialX==0 && radialY==-1); /* Commands ignore the left stick. */
    device4.Gamepad.wButtons=XINPUT_GAMEPAD_Y; Poll(); assert(radialCommits==2 && !Sent("split_cgame 4 weapalt\n"));
    clockTime+=160; Poll(); assert(CL_RadialOpen(3) && radialKinds[3]==1); /* Immediate held Up -> Y handoff. */
    device4.Gamepad.wButtons=0; Poll(); assert(radialCommits==3);
    memset(&device4,0,sizeof(device4)); Poll();
    radialReady=qfalse; ClearSent(); device4.Gamepad.wButtons=XINPUT_GAMEPAD_Y; Poll(); clockTime+=300; Poll();
    device4.Gamepad.wButtons=0; Poll(); assert(!Sent("split_cgame 4 weapnext\n")); /* A rejected hold is not a tap. */
    radialReady=qtrue;
    device4.Gamepad.wButtons = XINPUT_GAMEPAD_START; Poll();
    cls.keyCatchers = KEYCATCH_UI; Poll(); assert(!strcmp(menuPlayer, "4"));
    /* Keyboard/mouse plus three pads: leave player one's controller unassigned. */
    indices[0]->integer = -2; online4 = 0; Poll(); Poll();
    assert(!players[0].isConnected && players[1].isConnected && players[2].isConnected && players[3].isConnected);
    assert(players[1].deviceIndex != players[2].deviceIndex && players[1].deviceIndex != players[3].deviceIndex && players[2].deviceIndex != players[3].deviceIndex);
    indices[0]->integer = -1;
    /* Focus loss affects real devices, not the explicitly enabled local test transport. */
    cls.keyCatchers = 0; localLoadout = 0; in_appactive = qfalse;
    testMode = qtrue; regressionAllowed = 1; testOnline[0] = testOnline[1] = qtrue;
    memset(testDevices, 0, sizeof(testDevices)); Poll(); Poll();
    testDevices[1].Gamepad.wButtons = XINPUT_GAMEPAD_RIGHT_SHOULDER; Poll(); localPlayer = 1;
    cmd = Move(qfalse); assert(cmd.buttons & BUTTON_ACTIVATE);
    regressionAllowed = 0; Poll(); cmd = Move(qfalse); assert(!cmd.buttons);
    regressionAllowed=1; splitCount=1; clc.serverAddress.type=NA_IP; clc.serverAddress.ip[0]=203;
    assert(!TestAllowed()); /* Explicit flag does not enable synthetic public-server controls. */
    clc.serverAddress.ip[0]=127; clc.serverAddress.ip[3]=1; assert(TestAllowed());
    regressionAllowed=0; assert(!TestAllowed());
    IN_GamepadShutdown();
    puts("PASS gamepad: four isolated controllers; stick math, transport, usercmd, mouse/stale gating, held actions, menus, ownership, focus and unplug");
    return 0;
}
