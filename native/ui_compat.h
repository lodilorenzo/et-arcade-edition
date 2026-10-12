/* Cosmetic/input bridge only: never advertise Legacy versions/extensions or
 * alter authentication, pure checks, gameplay commands or disconnect reasons. */
#ifndef ET_UI_COMPAT_H
#define ET_UI_COMPAT_H

static int ET_UIPadKey(int key, qboolean nativeMenu, qboolean binding) {
    static const int keys[] = {K_ENTER, K_ESCAPE, K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW, K_DEL};
    if (key < K_JOY1 || key > K_JOY7 || nativeMenu) return key;
    if (binding && key != K_JOY2) return key;
    return keys[key - K_JOY1];
}
static qboolean ET_OutdatedClientNotice(const char *command, const char *message, qboolean serverChat) {
    char text[MAX_STRING_CHARS];
    static const char *failures[] = {"disconnect", "reject", "refus", "kick", "banned", "denied", "mismatch",
        "required", "requires", "cannot connect", "can't connect", "bad guid", "invalid etkey", "incompatible", "unsupported",
        "pure", "error", "fail", "password", "server full", "server is full", "not allowed", "anti-cheat", "anticheat", "punkbuster"};
    /* ponytail: known English advisories; add exact mod/localized phrases when observed. */
    static const char *notices[] = {"your client is outdated", "your client version is outdated", "your et client is outdated",
        "your et: legacy client is outdated", "your client is out of date", "your et: legacy client is out of date",
        "you are running old software", "please update your client", "please upgrade your client",
        "we do recommend to upgrade your et client", "we do recommend to upgrade your legacy client"};
    int i, out = 0;
    if (strcmp(command,"cp") && strcmp(command,"cpm") && strcmp(command,"print") &&
        !(serverChat && !strcmp(command,"chat"))) return qfalse;
    if (strlen(message) >= sizeof(text)) return qfalse; /* Don't hide a truncated compound failure. */
    Q_strncpyz(text,message,sizeof(text));
    for (i=0;text[i];++i) if ((unsigned char)text[i]<=' ') text[i]=' ';
    Q_CleanStr(text); Q_strlwr(text);
    for (i=0;text[i];++i) {
        char c = (unsigned char)text[i] <= ' ' ? ' ' : text[i];
        if (c!=' ' || (out && text[out-1]!=' ')) text[out++]=c;
    }
    text[out]=0;
    for (i=0;i<sizeof(failures)/sizeof(failures[0]);++i) if (strstr(text,failures[i])) return qfalse;
    for (i=0;i<sizeof(notices)/sizeof(notices[0]);++i) if (strstr(text,notices[i])) return qtrue;
    return qfalse;
}
#endif
