/* Online panes keep the existing shared-memory/GDI transport. PBOs only remove
 * the GPU wait from the producer; unsupported drivers retain direct readback. */
#include "../engine/src/renderer/tr_local.h"
#include "../engine/src/renderer/glext.h"
#include "../engine/src/win32/win_local.h"
#include <stdint.h>

#define PIXEL_PACK_BUFFER 0x88EB
#define STREAM_READ 0x88E1
#define SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define ALREADY_SIGNALED 0x911A
#define CONDITION_SATISFIED 0x911C
#define TIMEOUT_EXPIRED 0x911B
#define READBACK_SLOTS 3

static PFNGLGENBUFFERSARBPROC genBuffers;
static PFNGLDELETEBUFFERSARBPROC deleteBuffers;
static PFNGLBINDBUFFERARBPROC bindBuffer;
static PFNGLBUFFERDATAARBPROC bufferData;
static PFNGLMAPBUFFERARBPROC mapBuffer;
static PFNGLUNMAPBUFFERARBPROC unmapBuffer;
static void *(APIENTRY *fenceSync)(GLenum, GLbitfield);
static GLenum (APIENTRY *clientWaitSync)(void *, GLbitfield, uint64_t);
static void (APIENTRY *deleteSync)(void *);
static GLuint buffers[READBACK_SLOTS];
static void *fences[READBACK_SLOTS];
static int nextBuffer, pending, asyncMode = -1;
static qboolean attempted, failed;
static unsigned rendered, published, skipped;
static double captureMs;
static LARGE_INTEGER clockFrequency;

static PROC ReadbackProc(const char *name) {
    PROC proc = qwglGetProcAddress(name);
    return (uintptr_t)proc <= 3 || (uintptr_t)proc == (uintptr_t)-1 ? NULL : proc;
}
static void FreeReadback(void) {
    int i;
    for (i = 0; i < READBACK_SLOTS; ++i) {
        if (fences[i]) deleteSync(fences[i]);
        fences[i] = NULL;
    }
    if (buffers[0] || buffers[1] || buffers[2]) deleteBuffers(READBACK_SLOTS, buffers);
    memset(buffers, 0, sizeof(buffers));
    pending = nextBuffer = 0;
}
void GLimp_OnlineReadbackShutdown(void) {
    FreeReadback(); attempted = failed = qfalse; asyncMode = -1;
}
static qboolean NativeBGRA(void) {
    int major = 0, minor = 0;
    sscanf(glConfig.version_string, "%d.%d", &major, &minor);
    return major > 1 || (major == 1 && minor >= 2) || strstr(glConfig.extensions_string, "GL_EXT_bgra") != NULL;
}
static qboolean InitReadback(void) {
    int i;
    if (attempted) return buffers[0] && !failed;
    attempted = qtrue;
    if (!NativeBGRA() || !strstr(glConfig.extensions_string, "GL_ARB_pixel_buffer_object") ||
        !strstr(glConfig.extensions_string, "GL_ARB_sync")) return qfalse;
    genBuffers = (PFNGLGENBUFFERSARBPROC)ReadbackProc("glGenBuffersARB");
    deleteBuffers = (PFNGLDELETEBUFFERSARBPROC)ReadbackProc("glDeleteBuffersARB");
    bindBuffer = (PFNGLBINDBUFFERARBPROC)ReadbackProc("glBindBufferARB");
    bufferData = (PFNGLBUFFERDATAARBPROC)ReadbackProc("glBufferDataARB");
    mapBuffer = (PFNGLMAPBUFFERARBPROC)ReadbackProc("glMapBufferARB");
    unmapBuffer = (PFNGLUNMAPBUFFERARBPROC)ReadbackProc("glUnmapBufferARB");
    fenceSync = (void *(APIENTRY *)(GLenum, GLbitfield))ReadbackProc("glFenceSync");
    clientWaitSync = (GLenum (APIENTRY *)(void *, GLbitfield, uint64_t))ReadbackProc("glClientWaitSync");
    deleteSync = (void (APIENTRY *)(void *))ReadbackProc("glDeleteSync");
    if (!genBuffers || !deleteBuffers || !bindBuffer || !bufferData || !mapBuffer || !unmapBuffer ||
        !fenceSync || !clientWaitSync || !deleteSync) return qfalse;
    genBuffers(READBACK_SLOTS, buffers);
    for (i = 0; i < READBACK_SLOTS; ++i) {
        bindBuffer(PIXEL_PACK_BUFFER, buffers[i]);
        bufferData(PIXEL_PACK_BUFFER, (GLsizeiptrARB)glConfig.vidWidth * glConfig.vidHeight * 4, NULL, STREAM_READ);
    }
    bindBuffer(PIXEL_PACK_BUFFER, 0);
    if (qglGetError() != GL_NO_ERROR || !buffers[0] || !buffers[1] || !buffers[2]) {
        FreeReadback(); failed = qtrue; return qfalse;
    }
    ri.Printf(PRINT_ALL, "ONLINEFRAME asynchronous PBO readback enabled\n");
    return qtrue;
}
static qboolean AsyncReadback(void) {
    int latest = -1;
    unsigned char *destination;
    /* Retire completed transfers, but copy only the newest completed image.
     * Zero-timeout fences: a busy GPU never blocks a shared-buffer mutex. */
    while (pending) {
        int oldest = (nextBuffer + READBACK_SLOTS - pending) % READBACK_SLOTS;
        GLenum result = clientWaitSync(fences[oldest], 0, 0);
        if (result == TIMEOUT_EXPIRED) break;
        if (result != ALREADY_SIGNALED && result != CONDITION_SATISFIED) return qfalse;
        deleteSync(fences[oldest]); fences[oldest] = NULL;
        latest = oldest; --pending;
    }
    if (latest >= 0) {
        destination = CL_OnlinePaneFrameBuffer(glConfig.vidWidth, glConfig.vidHeight);
        if (destination) {
            const void *source;
            GLboolean intact;
            bindBuffer(PIXEL_PACK_BUFFER, buffers[latest]);
            source = mapBuffer(PIXEL_PACK_BUFFER, GL_READ_ONLY_ARB);
            if (!source) { bindBuffer(PIXEL_PACK_BUFFER, 0); CL_OnlinePaneFrameReady(qfalse); return qfalse; }
            memcpy(destination, source, (size_t)glConfig.vidWidth * glConfig.vidHeight * 4);
            intact = unmapBuffer(PIXEL_PACK_BUFFER);
            bindBuffer(PIXEL_PACK_BUFFER, 0);
            CL_OnlinePaneFrameReady(intact ? qtrue : qfalse);
            if (!intact) return qfalse;
            ++published;
        } else ++skipped;
    }
    if (pending < READBACK_SLOTS) {
        bindBuffer(PIXEL_PACK_BUFFER, buffers[nextBuffer]);
        qglReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        fences[nextBuffer] = fenceSync(SYNC_GPU_COMMANDS_COMPLETE, 0);
        bindBuffer(PIXEL_PACK_BUFFER, 0);
        if (!fences[nextBuffer] || qglGetError() != GL_NO_ERROR) return qfalse;
        nextBuffer = (nextBuffer + 1) % READBACK_SLOTS; ++pending;
    } else ++skipped;
    qglFlush();
    return qtrue;
}
void GLimp_OnlineReadback(void) {
    LARGE_INTEGER start, end;
    int mode = ri.Cvar_Get("r_onlineAsync", "1", CVAR_ARCHIVE)->integer != 0 &&
        !ri.Cvar_Get("et_onlineMap", "0", CVAR_INIT | CVAR_ROM | CVAR_NORESTART)->integer;
    if (mode != asyncMode) {
        GLimp_OnlineReadbackShutdown(); asyncMode = mode;
        rendered = published = skipped = 0; captureMs = 0;
    }
    if (!clockFrequency.QuadPart) QueryPerformanceFrequency(&clockFrequency);
    QueryPerformanceCounter(&start);
    ++rendered;
    qglReadBuffer(GL_BACK);
    qglPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (!(mode && InitReadback() && AsyncReadback())) {
        unsigned char *destination;
        if (buffers[0]) {
            bindBuffer(PIXEL_PACK_BUFFER, 0); FreeReadback(); failed = qtrue;
            ri.Printf(PRINT_WARNING, "ONLINEFRAME PBO failed; using direct readback\n");
        }
        destination = CL_OnlinePaneFrameBuffer(glConfig.vidWidth, glConfig.vidHeight);
        if (destination) {
            qboolean bgra = NativeBGRA();
            qglReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, destination);
            if (!bgra) {
                size_t i, bytes = (size_t)glConfig.vidWidth * glConfig.vidHeight * 4;
                for (i = 0; i < bytes; i += 4) {
                    unsigned char red = destination[i]; destination[i] = destination[i + 2]; destination[i + 2] = red;
                }
            }
            CL_OnlinePaneFrameReady(qtrue); ++published;
        } else ++skipped;
    }
    QueryPerformanceCounter(&end);
    captureMs += (end.QuadPart - start.QuadPart) * 1000.0 / clockFrequency.QuadPart;
}
void GLimp_OnlineFrameStats(void) {
    ri.Printf(PRINT_ALL, "ONLINEFRAME_STATS mode=%s rendered=%u published=%u skipped=%u capture_ms=%.3f\n",
        buffers[0] && !failed ? "async" : "direct", rendered, published, skipped, captureMs);
}
