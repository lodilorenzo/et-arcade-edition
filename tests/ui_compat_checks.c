#undef NDEBUG
#include "../engine/src/client/client.h"
#include "../native/ui_compat.h"
#include <assert.h>
#include <stdio.h>
void QDECL Com_Error(int level,const char *fmt,...) { (void)level; (void)fmt; assert(0); exit(1); }
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
int Q_vsnprintf(char *dest,int size,const char *fmt,va_list args) { return vsprintf_s(dest,size,fmt,args); }
int main(void) {
    const int mapped[] = {K_ENTER,K_ESCAPE,K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,K_DEL};
    char huge[MAX_STRING_CHARS+40]; int i;
    for(i=0;i<7;++i) {
        assert(ET_UIPadKey(K_JOY1+i,qfalse,qfalse)==mapped[i]);
        assert(ET_UIPadKey(K_JOY1+i,qtrue,qfalse)==K_JOY1+i);
        assert(ET_UIPadKey(K_JOY1+i,qfalse,qtrue)==(i==1 ? K_ESCAPE : K_JOY1+i));
    }
    assert(ET_UIPadKey(K_ENTER,qfalse,qfalse)==K_ENTER);
    assert(ET_UIPadKey('A'|K_CHAR_FLAG,qfalse,qfalse)==('A'|K_CHAR_FLAG));
    assert(ET_OutdatedClientNotice("cp","^1Your ET: Legacy client is outdated. New update is available.",qfalse));
    assert(ET_OutdatedClientNotice("cpm","You are running old software",qfalse));
    assert(ET_OutdatedClientNotice("print","WE DO RECOMMEND TO\nUPGRADE YOUR ET CLIENT",qfalse));
    assert(ET_OutdatedClientNotice("chat","Please update your client",qtrue));
    assert(!ET_OutdatedClientNotice("chat","Please update your client",qfalse)); /* Other players' chat. */
    assert(!ET_OutdatedClientNotice("tchat","Your client is outdated",qtrue));
    assert(!ET_OutdatedClientNotice("disconnect","Your client is outdated",qtrue));
    assert(!ET_OutdatedClientNotice("cs","Your client is outdated",qtrue));
    assert(!ET_OutdatedClientNotice("print","Your client is outdated: connection refused",qtrue));
    assert(!ET_OutdatedClientNotice("cp","Your client is outdated. This server requires a newer protocol.",qtrue));
    assert(!ET_OutdatedClientNotice("print","Bad GUID: Invalid etkey. Use the ET: Legacy client.",qtrue));
    assert(!ET_OutdatedClientNotice("print","Your client is outdated. Pure check failed.",qtrue));
    assert(!ET_OutdatedClientNotice("cp","Server full",qtrue));
    assert(!ET_OutdatedClientNotice("print","Your PunkBuster client is outdated",qtrue));
    memset(huge,'x',sizeof(huge)); memcpy(huge,"Your client is outdated",23); huge[sizeof(huge)-1]=0;
    assert(!ET_OutdatedClientNotice("print",huge,qtrue));
    puts("PASS foreign UI keys/binding/back, narrow cosmetic advisory filtering, unchanged rejection and gameplay commands");
    return 0;
}
