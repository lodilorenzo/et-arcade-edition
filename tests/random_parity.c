// GPL-3.0-or-later with upstream additional terms: engine/COPYING.txt.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test the Rust implementations directly; C keeps the original names via thin wrappers.
#define Q_rand ET_Q_rand
#define Q_random ET_Q_random
#define Q_crandom ET_Q_crandom
int Q_rand(int *seed);
float Q_random(int *seed);
float Q_crandom(int *seed);
int C_Q_rand(int *seed);
float C_Q_random(int *seed);
float C_Q_crandom(int *seed);

static uint32_t bits(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static void check(int initial, int steps) {
    int c = initial, rust = initial;
    int i;
    for (i = 0; i < steps; ++i) {
        int ci = C_Q_rand(&c), ri = Q_rand(&rust);
        float cf, rf;
        if (ci != ri || c != rust) goto fail;
        cf = C_Q_random(&c); rf = Q_random(&rust);
        if (bits(cf) != bits(rf) || c != rust) goto fail;
        cf = C_Q_crandom(&c); rf = Q_crandom(&rust);
        if (bits(cf) != bits(rf) || c != rust) goto fail;
    }
    return;
fail:
    fprintf(stderr, "RNG mismatch: initial=%d step=%d C=%d Rust=%d\n", initial, i, c, rust);
    exit(1);
}

int main(void) {
    uint32_t i;
    _Static_assert(sizeof(int) == 4, "ET requires a 32-bit C int");
    for (i = 0; i < 65536; ++i) {
        check((int)i, 4);
        check((int)(i | 0xffff0000u), 4);
    }
    check(INT32_MIN, 100000);
    check(INT32_MAX, 100000);
    check(0, 100000);
    check(-1, 100000);
    puts("PASS: C/Rust RNG values, float bits and seed updates (2,772,864 calls).");
    return 0;
}
