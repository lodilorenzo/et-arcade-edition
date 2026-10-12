/* Generated limbo-style UI art (tools/ui_art.py) for engine-drawn pages and wheels.
 * Read from the shipped <fs_basepath>/et-rust/ui/art, not the game filesystem: pure servers hide
 * loose files, and these frames are this client's own overlay, not server content. Missing art
 * returns 0 so callers keep their flat fallback. */
#include "../engine/src/client/client.h"

#define ART_SLOTS 64
static struct { char name[32]; qhandle_t shader; } art[ART_SLOTS];
static int artCount;

/* Every renderer restart frees images: forget handles, reload lazily. */
void CL_UIArtReset(void) { artCount = 0; }

/* Uncompressed 32-bit TGA (what tools/ui_art.py writes); anything else is rejected. */
static qhandle_t Load(const char *name) {
    unsigned char header[18], *data = NULL, *rgba = NULL;
    int width, height, x, y;
    qhandle_t shader = 0;
    FILE *file = fopen(FS_BuildOSPath(Cvar_VariableString("fs_basepath"), "et-rust", va("ui/art/%s.tga", name)), "rb");
    if (!file) return 0;
    if (fread(header, 1, 18, file) == 18 && header[2] == 2 && header[16] == 32 && !header[0] && !header[1]) {
        width = header[12] | header[13] << 8; height = header[14] | header[15] << 8;
        if (width > 0 && height > 0 && width <= 1024 && height <= 1024) {
            data = Z_Malloc(width * height * 4); rgba = Z_Malloc(width * height * 4);
            if (fread(data, 4, width * height, file) == (size_t)(width * height)) {
                for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
                    const unsigned char *in = data + 4 * ((header[17] & 0x20 ? y : height - 1 - y) * width + x);
                    unsigned char *out = rgba + 4 * (y * width + x);
                    out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3];
                }
                shader = RE_RegisterRawShader(va("*etui/%s", name), rgba, width, height);
            }
        }
    }
    fclose(file);
    if (data) Z_Free(data);
    if (rgba) Z_Free(rgba);
    return shader;
}
qhandle_t CL_UIArt(const char *name) {
    int i;
    for (i = 0; i < artCount; ++i) if (!strcmp(art[i].name, name)) return art[i].shader;
    if (artCount == ART_SLOTS || strlen(name) >= sizeof(art[0].name)) return 0;
    Q_strncpyz(art[artCount].name, name, sizeof(art[0].name));
    art[artCount].shader = Load(name); /* Cache misses too: no per-frame file access. */
    return art[artCount++].shader;
}

/* Fixed-width border, stretched edges/center. center=qfalse draws a frame only. */
qboolean CL_ArtNine(const char *asset, float x, float y, float w, float h, float border, qboolean center) {
    static const char *names[3][3] = {{"tl", "l", "bl"}, {"t", "c", "b"}, {"tr", "r", "br"}};
    qhandle_t shaders[3][3];
    float xs[4], ys[4];
    int i, j;
    border = fminf(border, fminf(w, h) * .5f);
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j)
        if (!(shaders[i][j] = CL_UIArt(va("%s_%s", asset, names[i][j]))) && (center || i != 1 || j != 1)) return qfalse;
    xs[0] = x; xs[1] = x + border; xs[2] = x + w - border; xs[3] = x + w;
    ys[0] = y; ys[1] = y + border; ys[2] = y + h - border; ys[3] = y + h;
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) if (center || i != 1 || j != 1)
        re.DrawStretchPic(xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], 0, 0, 1, 1, shaders[i][j]);
    return qtrue;
}

/* End caps of width cap, stretched middle (title strips). */
qboolean CL_ArtThree(const char *asset, float x, float y, float w, float h, float cap) {
    qhandle_t l = CL_UIArt(va("%s_l", asset)), c = CL_UIArt(va("%s_c", asset)), r = CL_UIArt(va("%s_r", asset));
    if (!l || !c || !r) return qfalse;
    cap = fminf(cap, w * .5f);
    re.DrawStretchPic(x, y, cap, h, 0, 0, 1, 1, l);
    re.DrawStretchPic(x + cap, y, w - 2 * cap, h, 0, 0, 1, 1, c);
    re.DrawStretchPic(x + w - cap, y, cap, h, 0, 0, 1, 1, r);
    return qtrue;
}
