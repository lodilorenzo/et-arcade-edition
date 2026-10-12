// Generated limbo-style art for the unified shell (tools/ui_art.py, ui/art/*.tga).
// Include after ui/menumacros.h. Every piece is a decoration: focus order is unchanged.
// Button focus shows a brass set named "bf"<text> (group "artfocus"); menus hide that group on open.

#define ART_PIECE( P_X, P_Y, P_W, P_H, P_SHADER, P_NAME, P_GROUP, P_VISIBLE )	\
	itemDef {																\
		name		P_NAME													\
		group		P_GROUP													\
		rect		$evalfloat(P_X) $evalfloat(P_Y) $evalfloat(P_W) $evalfloat(P_H)	\
		style		WINDOW_STYLE_SHADER										\
		background	P_SHADER												\
		visible		P_VISIBLE												\
		decoration															\
	}

// Shown only while a cvar is 1 (e.g. art for a conditional button).
#define ART_PIECE_IF( P_X, P_Y, P_W, P_H, P_SHADER, P_NAME, P_GROUP, P_VISIBLE, P_CVAR )	\
	itemDef {																\
		name		P_NAME													\
		group		P_GROUP													\
		rect		$evalfloat(P_X) $evalfloat(P_Y) $evalfloat(P_W) $evalfloat(P_H)	\
		style		WINDOW_STYLE_SHADER										\
		background	P_SHADER												\
		visible		P_VISIBLE												\
		cvarTest	P_CVAR													\
		showCvar	{ "1" }												\
		decoration															\
	}

// Nine pieces: fixed corners/edges of width B, stretched center.
#define ART_NINE( N_X, N_Y, N_W, N_H, N_B, N_ART, N_NAME )						\
	ART_PIECE( N_X, N_Y, N_B, N_B, N_ART##"_tl", N_NAME, "art", 1 )			\
	ART_PIECE( (N_X)+(N_B), N_Y, (N_W)-2*(N_B), N_B, N_ART##"_t", N_NAME, "art", 1 )	\
	ART_PIECE( (N_X)+(N_W)-(N_B), N_Y, N_B, N_B, N_ART##"_tr", N_NAME, "art", 1 )	\
	ART_PIECE( N_X, (N_Y)+(N_B), N_B, (N_H)-2*(N_B), N_ART##"_l", N_NAME, "art", 1 )	\
	ART_PIECE( (N_X)+(N_B), (N_Y)+(N_B), (N_W)-2*(N_B), (N_H)-2*(N_B), N_ART##"_c", N_NAME, "art", 1 )	\
	ART_PIECE( (N_X)+(N_W)-(N_B), (N_Y)+(N_B), N_B, (N_H)-2*(N_B), N_ART##"_r", N_NAME, "art", 1 )	\
	ART_PIECE( N_X, (N_Y)+(N_H)-(N_B), N_B, N_B, N_ART##"_bl", N_NAME, "art", 1 )	\
	ART_PIECE( (N_X)+(N_B), (N_Y)+(N_H)-(N_B), (N_W)-2*(N_B), N_B, N_ART##"_b", N_NAME, "art", 1 )	\
	ART_PIECE( (N_X)+(N_W)-(N_B), (N_Y)+(N_H)-(N_B), N_B, N_B, N_ART##"_br", N_NAME, "art", 1 )

// Three pieces: end caps scale with the height (cap = height * ratio of the source art).
#define ART_THREE( T_X, T_Y, T_W, T_H, T_CAP, T_ART, T_NAME, T_GROUP, T_VISIBLE )	\
	ART_PIECE( T_X, T_Y, (T_H)*(T_CAP), T_H, T_ART##"_l", T_NAME, T_GROUP, T_VISIBLE )	\
	ART_PIECE( (T_X)+(T_H)*(T_CAP), T_Y, (T_W)-2*(T_H)*(T_CAP), T_H, T_ART##"_c", T_NAME, T_GROUP, T_VISIBLE )	\
	ART_PIECE( (T_X)+(T_W)-(T_H)*(T_CAP), T_Y, (T_H)*(T_CAP), T_H, T_ART##"_r", T_NAME, T_GROUP, T_VISIBLE )

#define ART_THREE_IF( T_X, T_Y, T_W, T_H, T_CAP, T_ART, T_NAME, T_GROUP, T_VISIBLE, T_CVAR )	\
	ART_PIECE_IF( T_X, T_Y, (T_H)*(T_CAP), T_H, T_ART##"_l", T_NAME, T_GROUP, T_VISIBLE, T_CVAR )	\
	ART_PIECE_IF( (T_X)+(T_H)*(T_CAP), T_Y, (T_W)-2*(T_H)*(T_CAP), T_H, T_ART##"_c", T_NAME, T_GROUP, T_VISIBLE, T_CVAR )	\
	ART_PIECE_IF( (T_X)+(T_W)-(T_H)*(T_CAP), T_Y, (T_H)*(T_CAP), T_H, T_ART##"_r", T_NAME, T_GROUP, T_VISIBLE, T_CVAR )

#define PANEL( PANEL_X, PANEL_Y, PANEL_W, PANEL_H )	ART_NINE( PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 12, "ui/art/panel", "panel" )
#define INSET( INSET_X, INSET_Y, INSET_W, INSET_H )	ART_NINE( INSET_X, INSET_Y, INSET_W, INSET_H, 6, "ui/art/inset", "inset" )
#define FIELD( FIELD_X, FIELD_Y, FIELD_W, FIELD_H )	ART_THREE( FIELD_X, FIELD_Y, FIELD_W, FIELD_H, .066, "ui/art/field3", "field", "art", 1 )

// Screwed title strip with left-aligned heavy title text.
#define TITLEBAR( TB_X, TB_Y, TB_W, TB_H, TB_TEXT, TB_SCALE )				\
	ART_PIECE( TB_X, TB_Y, (TB_H)*.5, TB_H, "ui/art/header_l", "titlebar", "art", 1 )	\
	ART_PIECE( (TB_X)+(TB_H)*.5, TB_Y, (TB_W)-(TB_H), TB_H, "ui/art/header_c", "titlebar", "art", 1 )	\
	ART_PIECE( (TB_X)+(TB_W)-(TB_H)*.5, TB_Y, (TB_H)*.5, TB_H, "ui/art/header_r", "titlebar", "art", 1 )	\
	itemDef {																\
		name		"title"##TB_TEXT										\
		rect		$evalfloat((TB_X)+(TB_H)*.6) $evalfloat(TB_Y) $evalfloat((TB_W)-(TB_H)*1.2) $evalfloat(TB_H)	\
		type		ITEM_TYPE_TEXT											\
		text		TB_TEXT													\
		textfont	UI_FONT_ARIBLK_16										\
		textstyle	ITEM_TEXTSTYLE_SHADOWED									\
		textscale	TB_SCALE												\
		textalignx	4														\
		textaligny	$evalfloat((TB_H)*.5+(TB_SCALE)*24)						\
		forecolor	.86 .84 .76 1											\
		visible		1														\
		decoration															\
	}

// A framed window: steel plate plus title strip inside its border.
#define ARTWINDOW( AW_X, AW_Y, AW_W, AW_H, AW_TEXT )						\
	PANEL( AW_X, AW_Y, AW_W, AW_H )											\
	TITLEBAR( (AW_X)+10, (AW_Y)+10, (AW_W)-20, 24, AW_TEXT, .22 )

// Reserved, wrapping help area. The shell paints focused control tips here, never over controls.
#define CONTEXTHELP( CH_X, CH_Y, CH_W, CH_H, CH_TEXT ) \
 itemDef { \
  name "contextHelp" \
  rect $evalfloat(CH_X) $evalfloat(CH_Y) $evalfloat(CH_W) $evalfloat(CH_H) \
  type ITEM_TYPE_TEXT \
  text CH_TEXT \
  textfont UI_FONT_COURBD_21 \
  textscale .21 \
  textalignx 5 \
  textaligny 12 \
  forecolor .82 .81 .74 1 \
  autowrapped \
  visible 1 \
  decoration \
 }

// Push button: steel face, brass face while focused (mouse or pad), warm text.
#define ARTBUTTON( AB_X, AB_Y, AB_W, AB_H, AB_TEXT, AB_SCALE, AB_ALIGN_Y, AB_ACTION )	\
	ART_THREE( AB_X, AB_Y, AB_W, AB_H, .088, "ui/art/button3", "bn"##AB_TEXT, "art", 1 )	\
	ART_THREE( AB_X, AB_Y, AB_W, AB_H, .088, "ui/art/button_focus3", "bf"##AB_TEXT, "artfocus", 0 )	\
	itemDef {																\
		name		"bttn"##AB_TEXT											\
		group		GROUP_NAME												\
		rect		$evalfloat(AB_X) $evalfloat(AB_Y) $evalfloat(AB_W) $evalfloat(AB_H)	\
		type		ITEM_TYPE_BUTTON										\
		text		AB_TEXT													\
		textfont	UI_FONT_COURBD_30										\
		textstyle	ITEM_TEXTSTYLE_SHADOWED									\
		textscale	AB_SCALE												\
		textalign	ITEM_ALIGN_CENTER										\
		textalignx	$evalfloat(0.5*(AB_W))									\
		textaligny	AB_ALIGN_Y												\
		forecolor	.78 .77 .7 1											\
		visible		1														\
		mouseEnter { show "bf"##AB_TEXT ; setitemcolor "bttn"##AB_TEXT forecolor 1 .86 .58 1 }	\
		mouseExit { hide "bf"##AB_TEXT ; setitemcolor "bttn"##AB_TEXT forecolor .78 .77 .7 1 }	\
		action { hide "bf"##AB_TEXT ; setitemcolor "bttn"##AB_TEXT forecolor .78 .77 .7 1 ; play "sound/menu/select.wav" ; AB_ACTION }	\
	}
