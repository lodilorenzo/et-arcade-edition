/* Native 640x480 canvas: uniform scale, centred menus, explicit HUD anchors.
 * No engine/glConfig/player ABI changes. */
#ifndef UI_VIEWPORT_H
#define UI_VIEWPORT_H
static void UI_Viewport( int width, int height, float *xs, float *ys, float *bias ) {
	float sx = width / 640.f, sy = height / 480.f;
	*xs = *ys = sx < sy ? sx : sy;
	*bias = ( width - 640.f * *xs ) * .5f;
}
static void UI_ViewportRect( float *x, float *y, float *w, float *h, float scale, float bias ) {
	*x = *x * scale + bias;
	*y *= scale;
	*w *= scale;
	*h *= scale;
}
/* Portrait viewports (including side-by-side play): top=0, centre=.5, bottom=1. */
static float UI_ViewportYBias( int height, float scale, float anchor ) {
	return (height - 480.f * scale) * anchor;
}
/* Full-view artwork: crop the excess rather than stretching either axis. */
static void UI_ViewportCover( int width, int height, float aspect, float *s0, float *t0, float *s1, float *t1 ) {
	float view = (float)width / height;
	*s0 = *t0 = 0.f;
	*s1 = *t1 = 1.f;
	if ( view < aspect ) {
		*s0 = ( 1.f - view / aspect ) * .5f;
		*s1 = 1.f - *s0;
	} else {
		*t0 = ( 1.f - aspect / view ) * .5f;
		*t1 = 1.f - *t0;
	}
}
/* Native cursor storage is integer: retain sub-canvas-pixel mouse motion. */
static int UI_ViewportMouse( int delta, float scale, float *remainder ) {
	float motion = delta / scale + *remainder;
	int step = (int)motion;
	*remainder = motion - step;
	return step;
}
#endif
