/* Deterministic driver/mutex checks for the actual readback implementation. */
#undef NDEBUG
#include "../native/online_readback.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

refimport_t ri;
glconfig_t glConfig;
static cvar_t enabled, mapEnabled;
static unsigned char shared[16], gpu[3][16];
static GLuint bound;
static int queued, completed, locked, notifications, reads, maps, deleted, failMap, corrupt, unavailable, badProc;
static GLenum waitResult = CONDITION_SATISFIED;
static cvar_t *Get(const char *name, const char *value, int flags) { return !strcmp(name, "et_onlineMap") ? &mapEnabled : &enabled; }
static void QDECL Print(int level, const char *format, ...) { }
static void APIENTRY Gen(GLsizei n, GLuint *ids) { int i; for (i=0;i<n;++i) ids[i]=i+1; }
static void APIENTRY Delete(GLsizei n, const GLuint *ids) { deleted += n; }
static void APIENTRY Bind(GLenum target, GLuint id) { assert(target==PIXEL_PACK_BUFFER); bound=id; }
static void APIENTRY Data(GLenum target, GLsizeiptrARB size, const void *data, GLenum usage) { assert(size==16); }
static void *APIENTRY Map(GLenum target, GLenum access) { ++maps; return failMap ? NULL : gpu[bound-1]; }
static GLboolean APIENTRY Unmap(GLenum target) { return corrupt ? GL_FALSE : GL_TRUE; }
static void *APIENTRY Fence(GLenum condition, GLbitfield flags) { return (void *)(uintptr_t)++queued; }
static GLenum APIENTRY Wait(void *sync, GLbitfield flags, uint64_t timeout) {
    assert(!timeout && !flags && !locked); /* GPU polling never owns the shared mutex. */
    return waitResult;
}
static void APIENTRY DeleteFence(void *sync) { ++completed; }
static void APIENTRY ReadBuffer(GLenum mode) { assert(mode==GL_BACK); }
static void APIENTRY PixelStore(GLenum name, GLint value) { assert(value==4); }
static void APIENTRY Read(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) {
    assert(width==2 && height==2 && type==GL_UNSIGNED_BYTE);
    assert(format == (NativeBGRA() ? GL_BGRA : GL_RGBA));
    ++reads;
    memset(bound ? gpu[bound-1] : pixels, reads, 16);
    if (format==GL_RGBA) { unsigned char *rgb=pixels; rgb[0]=1; rgb[1]=2; rgb[2]=3; rgb[3]=4; }
}
static GLenum APIENTRY Error(void) { return GL_NO_ERROR; }
static void APIENTRY Flush(void) { }
void (APIENTRY *qglReadBuffer)(GLenum) = ReadBuffer;
void (APIENTRY *qglPixelStorei)(GLenum, GLint) = PixelStore;
void (APIENTRY *qglReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) = Read;
GLenum (APIENTRY *qglGetError)(void) = Error;
void (APIENTRY *qglFlush)(void) = Flush;
static PROC WINAPI Proc(LPCSTR name) {
    if (badProc) return (PROC)1;
    if (!strcmp(name,"glGenBuffersARB")) return (PROC)Gen;
    if (!strcmp(name,"glDeleteBuffersARB")) return (PROC)Delete;
    if (!strcmp(name,"glBindBufferARB")) return (PROC)Bind;
    if (!strcmp(name,"glBufferDataARB")) return (PROC)Data;
    if (!strcmp(name,"glMapBufferARB")) return (PROC)Map;
    if (!strcmp(name,"glUnmapBufferARB")) return (PROC)Unmap;
    if (!strcmp(name,"glFenceSync")) return (PROC)Fence;
    if (!strcmp(name,"glClientWaitSync")) return (PROC)Wait;
    if (!strcmp(name,"glDeleteSync")) return (PROC)DeleteFence;
    return NULL;
}
PROC (WINAPI *qwglGetProcAddress)(LPCSTR) = Proc;
unsigned char *CL_OnlinePaneFrameBuffer(int w, int h) { if (unavailable) return NULL; assert(!locked); locked=1; return shared; }
void CL_OnlinePaneFrameReady(qboolean published) { assert(locked); locked=0; notifications += published; }
int main(void) {
    ri.Cvar_Get=Get; ri.Printf=Print; glConfig.vidWidth=glConfig.vidHeight=2;
    strcpy(glConfig.version_string,"4.6");
    strcpy(glConfig.extensions_string,"GL_ARB_pixel_buffer_object GL_ARB_sync");
    enabled.integer=1; mapEnabled.integer=1;
    GLimp_OnlineReadback(); assert(asyncMode == 0 && notifications == 1 && !buffers[0]);
    GLimp_OnlineReadbackShutdown(); reads = notifications = 0; mapEnabled.integer=0;
    enabled.integer=1; waitResult=TIMEOUT_EXPIRED;
    GLimp_OnlineReadback(); GLimp_OnlineReadback(); GLimp_OnlineReadback(); GLimp_OnlineReadback();
    assert(reads==3 && pending==3 && maps==0 && !notifications && skipped==1);
    waitResult=CONDITION_SATISFIED; GLimp_OnlineReadback();
    assert(pending==1 && completed==3 && maps==1 && shared[0]==3 && notifications==1); /* Drop old completed images. */
    unavailable=1; GLimp_OnlineReadback(); assert(!locked && notifications==1);
    unavailable=0; failMap=1; GLimp_OnlineReadback();
    assert(failed && !buffers[0] && !locked && bound==0 && notifications==2); /* Direct fallback on map failure. */
    failMap=0; GLimp_OnlineReadbackShutdown(); assert(!pending && asyncMode==-1);
    corrupt=1; GLimp_OnlineReadback(); GLimp_OnlineReadback();
    assert(failed && !locked && notifications==3); /* Corrupt image isn't published; replacement is. */
    corrupt=0; enabled.integer=0; GLimp_OnlineReadback();
    assert(!buffers[0] && !bound && asyncMode==0 && notifications==4);
    enabled.integer=1; badProc=1; GLimp_OnlineReadback();
    assert(!buffers[0] && notifications==5); /* Windows' non-NULL invalid-proc sentinels. */
    badProc=0; GLimp_OnlineReadbackShutdown(); glConfig.extensions_string[0]=0;
    GLimp_OnlineReadback(); assert(!buffers[0] && notifications==6); /* Old driver. */
    GLimp_OnlineReadbackShutdown(); strcpy(glConfig.version_string,"1.1");
    GLimp_OnlineReadback(); assert(shared[0]==3 && shared[1]==2 && shared[2]==1 && shared[3]==4);
    GLimp_OnlineReadbackShutdown();
    puts("PASS nonblocking ring, newest completed frame, busy mutex, failure/fallback, cleanup and old-driver color conversion");
    return 0;
}
