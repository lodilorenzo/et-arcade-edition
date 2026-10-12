// Compile the actual upstream lexer and clamp, without ET_RUST_SHARED.
#define Com_Clamp C_Com_Clamp
#include "../engine/src/game/q_shared.c"

// Formatting/error services used by upstream session setup, not lexer logic.
#undef vsnprintf
int Q_vsnprintf(char *dest, int size, const char *fmt, va_list args) {
    return vsnprintf(dest, size, fmt, args);
}
void QDECL Com_Printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
void QDECL Com_Error(int level, const char *fmt, ...) {
    va_list args;
    (void)level;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}
