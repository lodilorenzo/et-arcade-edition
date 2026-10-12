/* One runnable check: real wheel selection, fixed requests, animation and icon feedback. */
#undef NDEBUG
#include <assert.h>
#include "../native/radial.c"
clientStatic_t cls;
clientConnection_t clc;
refexport_t re;
static playerState_t current;
static cvar_t dz, help;
static char *gameDir = "et-rust";
static char commandSent[128];
static int commandPlayer, serverCommand, drawnIcons;
static qboolean dispatched = qtrue;
static float firstWidth, firstY, firstX;
static int brightestSegment; /* Highest red modulate among wheel polygons: a tinted segment raises it. */
void QDECL Com_Printf(const char *format, ...) { (void)format; }
void QDECL Com_Error(int code,const char *format,...) { (void)code; (void)format; assert(0); }
void QDECL Com_DPrintf(const char *format, ...) { (void)format; }
int Q_vsnprintf(char *dest, int size, const char *fmt, va_list args) { return vsprintf_s(dest,size,fmt,args); }
void Cmd_AddCommand(const char *name, xcommand_t f) { (void)name; (void)f; }
void Cmd_RemoveCommand(const char *name) { (void)name; }
int Cmd_Argc(void) { return 0; }
char *Cmd_Argv(int i) { (void)i; return ""; }
cvar_t *Cvar_Get(const char *name,const char *value,int flags) { (void)name; (void)value; (void)flags; return &dz; }
int Cvar_VariableIntegerValue(const char *name) { (void)name; return 0; }
char *Cvar_VariableString(const char *name) { (void)name; return gameDir; }
const char *CL_TranslateStringBuf(const char *text) { return text; }
void CL_LocalViewport(int local,int *x,int *y,int *w,int *h) { (void)local; *x=*y=0; *w=640; *h=480; }
qboolean CL_LocalPlayerState(int local,playerState_t *ps) { (void)local; *ps=current; return qtrue; }
qboolean CL_LocalCgameCommand(int local,const char *text) { strcpy(commandSent,text); commandPlayer=local; serverCommand=0; return dispatched; }
qboolean CL_LocalServerCommand(int local,const char *text) { strcpy(commandSent,text); commandPlayer=local; serverCommand=1; return dispatched; }
qboolean CL_LocalLoadoutOpen(int local) { (void)local; return qfalse; }
int CL_SplitGamepadCount(void) { return 4; }
/* No shipped art here: exercise the flat fallback. */
qhandle_t CL_UIArt(const char *name) { (void)name; return 0; }
void RE_SplitClip(int x,int y,int w,int h) { (void)x; (void)y; (void)w; (void)h; }
static qhandle_t Shader(const char *name) { assert(*name); return 1; }
static void Color(const float *color) { (void)color; }
static void Polys(polyVert_t *p,int n,qhandle_t shader) { int i; assert(n==4); (void)shader; for (i=0;i<n;++i) if (p[i].modulate[0]>brightestSegment) brightestSegment=p[i].modulate[0]; }
static void Pic(float x,float y,float w,float h,float s,float t,float s2,float t2,qhandle_t shader) {
    (void)h; (void)s; (void)t; (void)s2; (void)t2;
    if (shader==1 && !drawnIcons++) { firstWidth=w; firstY=y; firstX=x; }
}
int main(void) {
    int i; float y0, y1;
    assert(Sector(0,0,.35f,8)==-1 && Sector(.1f,.1f,.35f,8)==-1);
    assert(Sector(NAN,1,.35f,8)==-1 && Sector(INFINITY,1,.35f,8)==-1);
    for (i=0;i<8;++i) assert(Sector(sinf(i*M_PI/4),cosf(i*M_PI/4),.35f,8)==i);
    for (i=0;i<5;++i) assert(Sector(sinf(i*2*M_PI/5),cosf(i*2*M_PI/5),.35f,5)==i);
    assert(!Owned(&current,-1) && !Owned(&current,MAX_WEAPONS) && GearForBank(&current,3)==-1);
    current.weapons[0]=(1u<<WP_THOMPSON)|(1u<<WP_PLIERS);
    current.weapons[1]=1u<<(48-32);
    assert(gear[GearForBank(&current,5)].weapon==WP_PLIERS);
    assert(GearForBank(&current,1)==-1);
    gameDir="legacy"; assert(gear[GearForBank(&current,1)].weapon==48);
    assert(CL_WeaponID(WP_MOBILE_MG42)==30 && CL_WeaponID(WP_MORTAR)==34 && CL_WeaponID(WP_AKIMBO_SILENCEDCOLT)==45);
    assert(GearForBank(&current,2)==-1); /* Ka-Bar bit 48 is not a stock silenced pistol. */
    gameDir="et-rust"; assert(CL_WeaponID(WP_AKIMBO_SILENCEDLUGER)==48 && GearForBank(&current,2)>=0);
    loaded=qtrue; deadzone=&dz; dz.value=.35f; centerHelp=&help; help.integer=1;
    current.stats[STAT_HEALTH]=100; current.persistant[PERS_TEAM]=TEAM_ALLIES; cls.state=CA_ACTIVE;
    cls.realtime=1000;
    assert(CL_RadialBegin(2,1));
    CL_RadialInput(2,1,0); assert(wheels[2].selected==2); /* No neutral/confirmation detour. */
    CL_RadialEnd(2,1);
    assert(!strcmp(commandSent,"weaponbank 3") && commandPlayer==2 && !serverCommand);
    assert(!CL_RadialOpen(2) && wheels[2].closing && wheels[2].feedback);
    CL_RadialEnd(2,1); /* Never dispatch twice. */
    for (i=0;i<5;++i) {
        char expected[64];
        assert(CL_RadialBegin(1,2));
        CL_RadialInput(1,sinf(i*2*M_PI/5),cosf(i*2*M_PI/5));
        CL_RadialEnd(1,1); snprintf(expected,sizeof(expected),"vsay_team %s",assistance[i].id);
        assert(!strcmp(commandSent,expected) && commandPlayer==1 && serverCommand && wheels[1].feedback);
        assert(strstr(assistance[i].icon,"gfx/limbo/ic_")==assistance[i].icon);
    }
    commandSent[0]=0; assert(CL_RadialBegin(0,2)); CL_RadialInput(0,0,0); CL_RadialEnd(0,1);
    assert(!*commandSent && !wheels[0].feedback && !CL_RadialOpen(0));
    assert(CL_RadialBegin(0,1)); CL_RadialInput(0,-1,0); CL_RadialEnd(0,1); /* Empty bank. */
    assert(!*commandSent && !wheels[0].feedback);
    dispatched=qfalse; assert(CL_RadialBegin(0,2)); CL_RadialInput(0,0,1); CL_RadialEnd(0,1);
    assert(!wheels[0].feedback); dispatched=qtrue;
    CL_RadialReset(); loaded=qtrue;
    re.RegisterShaderNoMip=Shader; re.SetColor=Color; re.Add2dPolys=Polys; re.DrawStretchPic=Pic;
    /* 100 ms entrance; dispatch and 180 ms exit/pulse start together on release. */
    assert(CL_RadialBegin(0,2));
    assert(Slide(&wheels[0],1000)==1 && Slide(&wheels[0],1050)==.5f && Slide(&wheels[0],1100)==0);
    drawnIcons=0; CL_RadialDraw(); y0=firstY; assert(drawnIcons==5 && y0>480);
    cls.realtime=1050; drawnIcons=0; CL_RadialDraw(); y1=firstY; assert(y1<y0 && y1>240);
    cls.realtime=1100; drawnIcons=0; CL_RadialDraw(); assert(firstY<y1 && fabsf(firstWidth-36*.72f)<.01f);
    assert(firstX>320); /* Class requests: right side of the view, 72% size. */
    help.integer=0; drawnIcons=0; CL_RadialDraw(); assert(drawnIcons==5); help.integer=1; /* Center help off keeps every segment icon. */
    commandSent[0]=0; CL_RadialInput(0,0,1); CL_RadialEnd(0,1);
    assert(!strcmp(commandSent,"vsay_team Medic") && wheels[0].closedAt==1100);
    assert(Slide(&wheels[0],1145)==.25f); /* No feedback pause before exit. */
    cls.realtime=1190; drawnIcons=0; CL_RadialDraw(); assert(firstWidth>36*.72f*1.5f && !CL_RadialOpen(0)); /* Icon pulse. */
    assert(Slide(&wheels[0],1190)==.5f);
    /* Another selection can replace feedback immediately without waiting for exit. */
    assert(CL_RadialBegin(0,1)); CL_RadialInput(0,1,0); CL_RadialEnd(0,1);
    assert(!strcmp(commandSent,"weaponbank 3") && wheels[0].closedAt==1190);
    cls.realtime=1370; CL_RadialDraw(); assert(!wheels[0].kind);
    cls.realtime=1500; assert(CL_RadialBegin(0,1)); cls.realtime=1600; drawnIcons=0; CL_RadialDraw();
    assert(drawnIcons>0 && firstX+firstWidth<320); /* Equipment: left side of the view. */
    CL_RadialCancel(0);
    /* The hovered choice is shown by its icon alone: it eases to 1.3x, and no segment is tinted. */
    cls.realtime=1700; assert(CL_RadialBegin(0,2)); CL_RadialInput(0,0,1); assert(wheels[0].selected==0);
    cls.realtime=1780; drawnIcons=0; brightestSegment=0; CL_RadialDraw();
    assert(fabsf(firstWidth-36*.72f*1.3f)<.01f && brightestSegment<=13);
    CL_RadialCancel(0);
    /* Early release exits from its current position, rather than jumping to center. */
    cls.realtime=2000; assert(CL_RadialBegin(0,1)); cls.realtime=2050; CL_RadialEnd(0,0);
    assert(Slide(&wheels[0],2050)==.5f && Slide(&wheels[0],2140)==.75f);
    current.stats[STAT_HEALTH]=0; assert(!CL_RadialBegin(0,1));
    current.stats[STAT_HEALTH]=100; cls.keyCatchers=KEYCATCH_UI; assert(!CL_RadialBegin(0,1));
    return 0;
}
