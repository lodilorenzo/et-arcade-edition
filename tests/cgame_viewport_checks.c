/* Actual damage/fade/fire coverage versus HUD safe-area scaling. */
#undef NDEBUG
#include <assert.h>
#include "../engine/src/cgame/cg_drawtools.c"

cgs_t cgs;
cg_t cg;
void trap_TranslateString(const char *text, char *out) { strcpy(out, text); }
void trap_R_DrawRotatedPic(float x,float y,float w,float h,float s0,float t0,float s1,float t1,qhandle_t shader,float angle) { assert(0); }
void trap_R_DrawStretchPicGradient(float x,float y,float w,float h,float s0,float t0,float s1,float t1,qhandle_t shader,const float *color,int type) { assert(0); }
static float rect[4], uv[4];
static qhandle_t drawnShader;
void trap_R_SetColor(const float *color) { (void)color; }
void trap_R_DrawStretchPic(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, qhandle_t shader) {
    rect[0] = x; rect[1] = y; rect[2] = w; rect[3] = h;
    uv[0] = s0; uv[1] = t0; uv[2] = s1; uv[3] = t1;
    drawnShader = shader;
}
int main(void) {
    const int modes[][2] = {{640,480},{1920,1080},{3440,1440},{640,720},{1720,1440}};
    const float fades[][4] = {{0,0,640,480},{-10,-10,650,490}};
    int i, j;
    for (i = 0; i < sizeof(modes)/sizeof(*modes); ++i) {
        cgs.glconfig.vidWidth = modes[i][0]; cgs.glconfig.vidHeight = modes[i][1];
        UI_Viewport(modes[i][0], modes[i][1], &cgs.screenXScale, &cgs.screenYScale, &cgs.screenXBias);
        cgs.screenYBias = UI_ViewportYBias(modes[i][1], cgs.screenYScale, .5f);
        for (j = 0; j < sizeof(fades)/sizeof(*fades); ++j) {
            CG_FillRect(fades[j][0], fades[j][1], fades[j][2], fades[j][3], colorRed);
            assert(rect[0] == 0 && rect[1] == 0);
            assert(rect[2] == modes[i][0] && rect[3] == modes[i][1]);
            // CG_DrawFlashFire uses the same overscan rectangle through CG_DrawPic.
            CG_DrawPic(fades[j][0], fades[j][1], fades[j][2], fades[j][3], 123);
            assert(rect[0] == 0 && rect[1] == 0);
            assert(rect[2] == modes[i][0] && rect[3] == modes[i][1]);
            assert(uv[0] == 0 && uv[1] == 0 && uv[2] == 1 && uv[3] == 1);
            assert(drawnShader == 123);
        }
        CG_FillRect(10,20,100,30,colorRed);
        assert(rect[0] == 10*cgs.screenXScale+cgs.screenXBias);
        assert(rect[1] == 20*cgs.screenYScale+cgs.screenYBias);
        assert(rect[2] == 100*cgs.screenXScale && rect[3] == 30*cgs.screenYScale);
        CG_DrawPic(10,20,-100,-30,123);
        assert(rect[0] == 10*cgs.screenXScale+cgs.screenXBias);
        assert(rect[1] == 20*cgs.screenYScale+cgs.screenYBias);
        assert(rect[2] == 100*cgs.screenXScale && rect[3] == 30*cgs.screenYScale);
        assert(uv[0] == 1 && uv[1] == 1 && uv[2] == 0 && uv[3] == 0);
    }
    return 0;
}
vec4_t colorRed = {1,0,0,1};
