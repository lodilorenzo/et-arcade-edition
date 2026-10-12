// GPL-3.0-or-later with additional terms: engine/COPYING.txt.
// The runtime's C ABI wrappers retain the original names; compare Rust directly here.
#define ClampChar ET_ClampChar
#define ClampShort ET_ClampShort
#define Q_fabs ET_Q_fabs
#define Q_rsqrt ET_Q_rsqrt
#define LerpAngle ET_LerpAngle
#define Com_Clamp ET_Com_Clamp
#include "../engine/src/game/q_shared.h"
#include <stdint.h>

signed char C_ClampChar(int value);
signed short C_ClampShort(int value);
float C_Q_fabs(float value);
float C_Q_rsqrt(float value);
float C_LerpAngle(float from, float to, float fraction);
float C_Com_Clamp(float min, float max, float value);
char *ET_ParseExt(char **data, int allowLineBreaks, char *token, size_t capacity, int *lines);

static unsigned long math_checks, parse_checks;
static uint32_t bits(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static float number(uint32_t value) {
    float result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static void same_float(const char *name, float c, float rust) {
    ++math_checks;
    if (bits(c) != bits(rust)) {
        fprintf(stderr, "%s mismatch: C=%08x Rust=%08x\n", name, bits(c), bits(rust));
        exit(1);
    }
}
static void math_case(int integer, float value, float from, float to, float fraction) {
    math_checks += 2;
    if (C_ClampChar(integer) != ClampChar(integer) || C_ClampShort(integer) != ClampShort(integer)) {
        fprintf(stderr, "integer clamp mismatch: %d\n", integer);
        exit(1);
    }
    same_float("Q_fabs", C_Q_fabs(value), Q_fabs(value));
    // Inverse sqrt is used on nonnegative squared lengths; include zero/subnormals.
    if (value >= 0 && value <= FLT_MAX) {
        same_float("Q_rsqrt", C_Q_rsqrt(value), Q_rsqrt(value));
    }
    same_float("Com_Clamp", C_Com_Clamp(-1, 1, value), Com_Clamp(-1, 1, value));
    same_float("Com_Clamp reversed", C_Com_Clamp(1, -1, value), Com_Clamp(1, -1, value));
    same_float("LerpAngle", C_LerpAngle(from, to, fraction), LerpAngle(from, to, fraction));
}
static void parse_case(char *input, const char *label) {
    char *c = input, *rust = input;
    int lines = 0;
    unsigned long step;
    char token[MAX_TOKEN_CHARS];
    size_t input_size = strlen(input);
    COM_BeginParseSession(label);
    for (step = 0; c || rust; ++step) {
        char *saved = rust;
        int saved_lines = lines;
        int allow = (step & 1) != 0;
        char *expected = COM_ParseExt(&c, allow);
        ET_ParseExt(&rust, allow, token, sizeof(token), &lines);
        ++parse_checks;
        if (strcmp(expected, token) || c != rust || COM_GetCurrentParseLine() != lines) {
            fprintf(stderr, "lexer mismatch: %s step=%lu C='%s' Rust='%s' lines=%d/%d offsets=%td/%td\n",
                    label, step, expected, token, COM_GetCurrentParseLine(), lines,
                    c ? c-input : -1, rust ? rust-input : -1);
            exit(1);
        }
        // Exercise unget: the real C session restores pointer + line count.
        if (saved && step % 7 == 0) {
            COM_RestoreParseSession(&c);
            rust = saved; lines = saved_lines;
            expected = COM_ParseExt(&c, allow);
            ET_ParseExt(&rust, allow, token, sizeof(token), &lines);
            ++parse_checks;
            if (strcmp(expected, token) || c != rust || COM_GetCurrentParseLine() != lines) {
                fprintf(stderr, "lexer restore mismatch: %s\n", label);
                exit(1);
            }
        }
        if (step > input_size + 2) {
            fprintf(stderr, "lexer did not terminate: %s\n", label);
            exit(1);
        }
    }
}
static void parse_own_token(void) {
    int offset;
    for (offset = 0; offset <= 3; ++offset) {
        char input[] = "abc", token[MAX_TOKEN_CHARS];
        char *c = input, *rust = input, *base, *expected;
        int lines = 0;
        COM_BeginParseSession("own token suffix");
        base = COM_ParseExt(&c, 1);
        ET_ParseExt(&rust, 1, token, sizeof(token), &lines);
        c = base + offset; rust = token + offset;
        expected = COM_ParseExt(&c, 1);
        ET_ParseExt(&rust, 1, token, sizeof(token), &lines);
        parse_checks += 2;
        if (strcmp(expected, token) || (c ? c-base : -1) != (rust ? rust-token : -1) || COM_GetCurrentParseLine() != lines) {
            fprintf(stderr, "own token suffix mismatch: %d\n", offset);
            exit(1);
        }
    }
}
static void parse_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *input;
    if (!file) { perror(path); exit(1); }
    fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
    if (size < 0 || size > 16 * 1024 * 1024) { fprintf(stderr, "bad corpus size: %s\n", path); exit(1); }
    input = malloc((size_t)size + 1);
    if (!input || fread(input, 1, size, file) != (size_t)size) { fprintf(stderr, "corpus read failed: %s\n", path); exit(1); }
    input[size] = 0; fclose(file);
    parse_case(input, path); free(input);
}
int main(int argc, char **argv) {
    uint32_t state = 0x12345678u, i;
    const uint32_t edge_bits[] = {0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x3f800000,
        0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc12345, 0xffc12345};
    const int edge_ints[] = {INT_MIN, INT_MAX, -32769, -32768, -129, -128, -1, 0, 127, 128, 32767, 32768};
    const char *chunks[] = {"word", "\n", "//comment\n", "/*multi\nline*/", "\"quoted words\"",
        "\"a \\\"inner\\\" b\"", "{ } ( 1 2 3 )", "\r\t", "textures/foo.tga", "\"\"", "\xff"};
    char input[4096];
    _Static_assert(sizeof(int) == 4 && sizeof(long) == 4, "ET math oracle requires Windows/x86 widths");
    for (i = 0; i < sizeof(edge_ints)/sizeof(*edge_ints); ++i) math_case(edge_ints[i], -0.0f, 350, 10, 0.5f);
    for (i = 0; i < sizeof(edge_bits)/sizeof(*edge_bits); ++i) math_case(0, number(edge_bits[i]), -180, 180, 1);
    for (i = 0; i < 200000; ++i) {
        float from, to, fraction;
        state = state * 1664525u + 1013904223u;
        from = (int32_t)state / 1000000.0f;
        to = (int32_t)(state * 69069u + 1u) / 1000000.0f;
        fraction = ((int)(state & 255) - 128) / 63.0f;
        math_case((int)state, number(state), from, to, fraction);
    }
    parse_own_token();
    parse_case("", "empty");
    parse_case("a\nb\r\nc", "newline counters");
    parse_case("/* unterminated\n", "unterminated comment");
    for (i = 1; i <= 255; ++i) {
        char word[] = {'a', (char)i, ' ', 'b', 0};
        parse_case(word, "signed bytes in word");
        if (i != '"' && i != '\\') {
            char quoted[] = {'"', (char)i, '"', ' ', 'b', 0};
            parse_case(quoted, "quoted byte");
        }
    }
    for (i = 0; i < 2000; ++i) {
        int j;
        input[0] = 0;
        for (j = 0; j < 40; ++j) {
            state = state * 1664525u + 1013904223u;
            strcat(input, chunks[state % (sizeof(chunks)/sizeof(*chunks))]);
            strcat(input, " ");
        }
        parse_case(input, "generated closed-token corpus");
    }
    memset(input, 'x', MAX_TOKEN_CHARS - 1); input[MAX_TOKEN_CHARS - 1] = 0;
    parse_case(input, "max valid word");
    memset(input, 'x', MAX_TOKEN_CHARS); input[MAX_TOKEN_CHARS] = 0;
    parse_case(input, "overflow word discarded");
    input[0] = '"'; memset(input + 1, 'x', MAX_TOKEN_CHARS - 1);
    input[MAX_TOKEN_CHARS] = '"'; input[MAX_TOKEN_CHARS + 1] = 0;
    parse_case(input, "max valid quoted token");
    for (i = 1; i < (uint32_t)argc; ++i) parse_file(argv[i]);
    printf("PASS: %lu scalar math comparisons; %lu lexer calls including cursor, line counts and unget.\n", math_checks, parse_checks);
    return 0;
}
