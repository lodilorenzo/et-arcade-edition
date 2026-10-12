/* Windows-strengthened ET: Legacy v2.86.0 FS_InvalidGameDir / traversal checks.
   GPLv3-or-later, derived from src/qcommon/files.c (ET: Legacy team). */
#ifndef ET_LEGACY_PATHS_H
#define ET_LEGACY_PATHS_H
#include <string.h>
#include <ctype.h>

static int ET_WindowsDeviceName(const char *name) {
    size_t n = strcspn(name, "./");
    char prefix[8];
    size_t i;
    if (n != 3 && n != 4 && n != 5 && n != 6 && n != 7) return 0;
    for (i = 0; i < n; ++i) prefix[i] = (char)toupper((unsigned char)name[i]);
    prefix[n] = 0;
    if (n != 4 && n != 5) return !strcmp(prefix, "CON") || !strcmp(prefix, "PRN") || !strcmp(prefix, "AUX") ||
        !strcmp(prefix, "NUL") || !strcmp(prefix, "CONIN$") || !strcmp(prefix, "CONOUT$");
    if (n == 5 && (unsigned char)name[3] != 0xc2) return 0; /* UTF-8 superscript digits. */
    return (!strncmp(prefix, "COM", 3) || !strncmp(prefix, "LPT", 3)) &&
        ((n == 4 && name[3] >= '1' && name[3] <= '9') || (unsigned char)name[n-1] == 0xb9 ||
         (unsigned char)name[n-1] == 0xb2 || (unsigned char)name[n-1] == 0xb3);
}

static int ET_InvalidGameDir(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    size_t n = strlen(name);
    if (ET_WindowsDeviceName(name) || !strcmp(name, ".") || !strcmp(name, "..") || (n && (name[n-1] == '.' || name[n-1] == ' '))) return 1;
    for (; *p; ++p)
        if (*p <= 32 || strchr("/\\:\"<>|?*@;", *p)) return 1;
    return 0; /* Empty is the stock etmain default. */
}

static int ET_InvalidPakName(const char *name) {
    const unsigned char *p;
    const char *slash;
    if (!name || !*name || name[0] == '/' || name[0] == '\\' ||
        strstr(name, "../") || strstr(name, "..\\") || strstr(name, "/./")) return 1;
    slash = strchr(name, '/');
    if (!slash || slash == name || !slash[1] || strchr(slash + 1, '/')) return 1;
    for (p = (const unsigned char *)name; *p; ++p)
        if (*p <= 32 || strchr("\\:\"<>|?*@;", *p)) return 1;
    if (ET_WindowsDeviceName(name) || ET_WindowsDeviceName(slash + 1) || slash[-1] == '.' ||
        !strcmp(slash + 1, ".") || !strcmp(slash + 1, "..")) return 1;
    return 0;
}
#endif
