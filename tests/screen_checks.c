/* Actual screen dispatcher: no half-loaded cgame or menu repaint during split startup. */
#undef NDEBUG
#include <assert.h>
#include "../engine/src/client/cl_scrn.c"

clientStatic_t cls;
clientActive_t cl;
clientConnection_t clc;
refexport_t re;
vm_t *uivm = (vm_t *)1;
static cvar_t zero;
cvar_t *cl_debugMove = &zero, *com_speeds = &zero;
int time_frontend, time_backend;
char *QDECL va(const char *format, ...) { (void)format; return ""; }
cvar_t *Cvar_Get(const char *name,const char *value,int flags) { (void)name; (void)value; (void)flags; return &zero; }
void Cvar_Set(const char *name,const char *value) { (void)name; (void)value; }
vec4_t g_color_table[32];
static int split, ready, renders, connects, refreshes, begins;
qboolean CL_SplitEnabled(void) { return split; }
qboolean CL_SplitReady(void) { return ready; }
int QDECL VM_Call(vm_t *vm, int call, ...) {
    assert(vm == uivm);
    if (call == UI_REFRESH) ++refreshes;
    if (call == UI_DRAW_CONNECT_SCREEN) {
        va_list args; va_start(args, call); assert(va_arg(args,int) == qfalse); va_end(args);
        ++connects;
    }
    return 0;
}
void CL_CGameRendering(stereoFrame_t stereo) { (void)stereo; ++renders; }
void CL_RadialDraw(void) {}
void CL_OnlineMapDraw(void) {}
void SCR_DrawCinematic(void) {}
void Con_DrawConsole(void) {}
void S_StopAllSounds(void) {}
void QDECL Com_Error(int code, const char *format, ...) { (void)code; (void)format; assert(0); }
void QDECL Com_DPrintf(const char *format, ...) { (void)format; }
void QDECL Com_sprintf(char *dest, int size, const char *format, ...) { (void)size; (void)format; *dest = 0; }
int FS_FTell(fileHandle_t file) { (void)file; return 0; }
static void Begin(stereoFrame_t stereo) { (void)stereo; ++begins; }
int main(void) {
    int state;
    re.BeginFrame = Begin;
    cl_debuggraph = cl_timegraph = cl_graphheight = cl_graphscale = cl_graphshift = &zero;
    split = 1;
    cls.keyCatchers = KEYCATCH_UI; /* A stale menu must not paint over loading. */
    for (state = CA_LOADING; state <= CA_ACTIVE; ++state) {
        cls.state = state; ready = 0;
        SCR_DrawScreenField(STEREO_CENTER);
        assert(connects == state-CA_LOADING+1 && refreshes == connects && renders == 0);
    }
    ready = 1; cls.keyCatchers = 0;
    SCR_DrawScreenField(STEREO_CENTER);
    assert(renders == 1 && connects == 3 && begins == 4);
    split = 0;
    SCR_DrawScreenField(STEREO_CENTER);
    assert(renders == 2 && connects == 3); /* Single-player still uses normal rendering. */
    return 0;
}
