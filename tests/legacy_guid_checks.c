/* Vectors produced by the unmodified v2.86.0 md5.c CalculateGUID routine.
   Reference commit: 9345fc881f5ea20da2e4259c65e2e84082ef45fc. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../native/legacy_paths.h"
void ET_GUIDFromKey(const unsigned char key[28], char guid[33]);
void Com_Memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void Com_Memset(void *d, int v, size_t n) { memset(d, v, n); }
int main(int argc, char **argv) {
    const char *keys[] = {"0000001002202610091500001234", "0000001002200301010000000000", "0000001002000000000000000000"};
    const char *guids[] = {"EA2B3709F021C51681E1C1DADF8397E9", "7478CE0FCA53880FB69D2B416796743F", "8EC772DF30ABFB4E32949751ECD0C4A5"};
    const char *bad[] = {"../etmain/pak", "etmain/../etkey", "C:/etmain/pak", "/etmain/pak", "etmain/pak@x", "etmain\\pak", "etmain/pak\nquit", "etmain/sub/pak", "etmain./pak", "etmain/..", "legacy/NuL", "COM1/pak", "legacy/con.pk3", "legacy/LPT9", "legacy/CONOUT$"};
    char guid[33];
    int i;
    for (i = 0; i < 3; ++i) { ET_GUIDFromKey((const unsigned char *)keys[i], guid); assert(!strcmp(guid, guids[i])); }
    assert(!ET_InvalidGameDir("") && !ET_InvalidGameDir("legacy") && !ET_InvalidGameDir("et-rust"));
    assert(ET_InvalidGameDir("..") && ET_InvalidGameDir("legacy/sub") && ET_InvalidGameDir("C:") && ET_InvalidGameDir("legacy."));
    assert(ET_InvalidGameDir("NUL") && ET_InvalidGameDir("com1") && ET_InvalidGameDir("LPT9") && ET_InvalidGameDir("CONIN$") && ET_InvalidGameDir("COM\xb9") && ET_InvalidGameDir("LPT\xc2\xb2"));
    assert(!ET_InvalidGameDir("combat") && !ET_InvalidPakName("legacy/combat1"));
    assert(!ET_InvalidPakName("legacy/legacy_v2.86.0") && !ET_InvalidPakName("etmain/radar_summer_101"));
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) assert(ET_InvalidPakName(bad[i]));
    if (argc == 2) {
        unsigned char key[28];
        FILE *file = fopen(argv[1], "rb");
        assert(file && fread(key, 1, sizeof(key), file) == sizeof(key));
        fclose(file); ET_GUIDFromKey(key, guid); puts(guid);
    } else puts("PASS Legacy GUID vectors and path boundaries");
    return 0;
}
