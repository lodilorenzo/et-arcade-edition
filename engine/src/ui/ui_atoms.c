/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Wolfenstein: Enemy Territory GPL Source Code (Wolf ET Source Code).  

Wolf ET Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Wolf ET Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wolf ET Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Wolf: ET Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Wolf ET Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/**********************************************************************
	UI_ATOMS.C

	User interface building blocks and support functions.
**********************************************************************/
#include "ui_local.h"
#include "ui_viewport.h"

uiStatic_t uis;
qboolean m_entersound;              // after a frame, so caching won't disrupt the sound

// these are here so the functions in q_shared.c can link
#ifndef UI_HARD_LINKED


// JPW NERVE added Com_DPrintf
#define MAXPRINTMSG 4096
void QDECL Com_DPrintf( const char *fmt, ... ) {
	va_list argptr;
	char msg[MAXPRINTMSG];
	int developer;

	developer = trap_Cvar_VariableValue( "developer" );
	if ( !developer ) {
		return;
	}

	va_start( argptr,fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}
// jpw

void QDECL Com_Error( int level, const char *error, ... ) {
	va_list argptr;
	char text[1024];

	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	trap_Error( va( "%s", text ) );
}

void QDECL Com_Printf( const char *msg, ... ) {
	va_list argptr;
	char text[1024];

	va_start( argptr, msg );
	Q_vsnprintf( text, sizeof( text ), msg, argptr );
	va_end( argptr );

	trap_Print( va( "%s", text ) );
}

#endif

/*
=================
UI_ClampCvar
=================
*/
float UI_ClampCvar( float min, float max, float value ) {
	if ( value < min ) {
		return min;
	}
	if ( value > max ) {
		return max;
	}
	return value;
}

/*
// TTimo: unused
static void NeedCDAction( qboolean result ) {
	if ( !result ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
	}
}

static void NeedCDKeyAction( qboolean result ) {
	if ( !result ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
	}
}
*/

char *UI_Argv( int arg ) {
	static char buffer[MAX_STRING_CHARS];

	trap_Argv( arg, buffer, sizeof( buffer ) );

	return buffer;
}


char *UI_Cvar_VariableString( const char *var_name ) {
	static char buffer[2][MAX_STRING_CHARS];
	static int toggle;

	toggle ^= 1;        // flip-flop to allow two returns without clash

	trap_Cvar_VariableStringBuffer( var_name, buffer[toggle], sizeof( buffer[0] ) );

	return buffer[toggle];
}



void UI_LoadBestScores( const char *map, int game ) {
}

/*
===============
UI_ClearScores
===============
*/
void UI_ClearScores() {
}



static void UI_Cache_f() {
	Display_CacheAll();
}

/*
=======================
UI_CalcPostGameStats
=======================
*/
static void UI_CalcPostGameStats() {
}


/*
=================
UI_ConsoleCommand
=================
*/
qboolean UI_ConsoleCommand( int realTime ) {
	char    *cmd;
	uiClientState_t cstate;

	uiInfo.uiDC.frameTime = realTime - uiInfo.uiDC.realTime;
	uiInfo.uiDC.realTime = realTime;

	cmd = UI_Argv( 0 );

	// ensure minimum menu data is available
	//Menu_Cache();

	if ( Q_stricmp( cmd, "ui_test" ) == 0 ) {
		// Reuse the native test command for deterministic menu captures/checks.
		if ( trap_Argc() == 3 && trap_Cvar_VariableValue( "ui_rustMenuTest" ) && !Q_stricmp(UI_Argv(1), "pad") ) {
			static const char *names[] = { "a", "b", "up", "down", "left", "right", "x" };
			char name[64];
			trap_Argv(2, name, sizeof(name));
			int i;
			menuDef_t *menu;
			itemDef_t *item = NULL;
			char value[1024] = "";
			extern qboolean g_editingField;
			extern void _UI_KeyEvent(int key, qboolean down);
			extern void _UI_SetActiveMenu(uiMenuCommand_t menu);
			if (!Q_stricmp(name, "start")) {
				if (!(trap_Key_GetCatcher() & KEYCATCH_UI)) _UI_SetActiveMenu(UIMENU_INGAME);
				else { _UI_KeyEvent(K_JOY2, qtrue); _UI_KeyEvent(K_JOY2, qfalse); }
			}
			for (i = 0; i < 7; ++i) if (!Q_stricmp(name, names[i])) {
				_UI_KeyEvent(K_JOY1 + i, qtrue);
				_UI_KeyEvent(K_JOY1 + i, qfalse);
				break;
			}
			menu = Menu_GetFocused();
			if (menu) for (i = 0; i < menu->itemCount; ++i) if (menu->items[i]->window.flags & WINDOW_HASFOCUS) item = menu->items[i];
			if (item && item->cvar) trap_Cvar_VariableStringBuffer(item->cvar, value, sizeof(value));
			Com_Printf("%s menu=\"%s\" item=\"%s\" cvar=\"%s\" value=\"%s\" editing=%i\n", !Q_stricmp(name, "state") ? "PAD_CHECK" : "PADUI", menu ? menu->window.name : "", item && item->text ? item->text : "", item && item->cvar ? item->cvar : "", value, g_editingField);
			if ( menu && menu->contextHelp && item && item->toolTipData ) {
				Rectangle *r = &item->toolTipData->window.rect, *h = &menu->contextHelp->window.rect;
				Com_Printf("HELP_CHECK menu=\"%s\" docked=%i rect=%.1f,%.1f,%.1f,%.1f text=\"%s\"\n", menu->window.name,
					r->x == h->x && r->y == h->y && r->w == h->w && r->h == h->h, r->x, r->y, r->w, r->h, item->toolTipData->text);
			}
			return qtrue;
		}
		if ( trap_Argc() == 4 && trap_Cvar_VariableValue( "ui_rustMenuTest" ) && !Q_stricmp(UI_Argv(1), "mouse") ) {
			extern void _UI_MouseEvent( int dx, int dy );
			_UI_MouseEvent( atoi( UI_Argv(2) ), atoi( UI_Argv(3) ) );
			return qtrue;
		}
		if ( trap_Argc() == 2 && trap_Cvar_VariableValue( "ui_rustMenuTest" ) && !Q_stricmp(UI_Argv(1), "browser") ) {
			menuDef_t *menu = Menu_GetFocused();
			int i, start = -1;
			for ( i = 0; menu && i < menu->itemCount; ++i ) {
				itemDef_t *item = menu->items[i];
				if ( item->type == ITEM_TYPE_LISTBOX && item->special == FEEDER_SERVERS ) start = ((listBoxDef_t *)item->typeData)->startPos;
			}
			Com_Printf( "BROWSER_CHECK source=%d count=%d current=%d start=%d sort=%d direction=%d refreshing=%d\n", (int)trap_Cvar_VariableValue( "ui_netSource" ),
				uiInfo.serverStatus.numDisplayServers, uiInfo.serverStatus.currentServer, start, uiInfo.serverStatus.sortKey, uiInfo.serverStatus.sortDir, uiInfo.serverStatus.refreshActive );
			for ( i = 0; i < uiInfo.serverStatus.numDisplayServers; ++i ) {
				char info[MAX_STRING_CHARS];
				trap_LAN_GetServerInfo( (int)trap_Cvar_VariableValue( "ui_netSource" ), uiInfo.serverStatus.displayServers[i], info, sizeof( info ) );
				Com_Printf( "BROWSER_SERVER index=%d name=\"%s\" password=%s\n", i, Info_ValueForKey( info, "hostname" ), Info_ValueForKey( info, "needpass" ) );
			}
			return qtrue;
		}
		if ( trap_Argc() == 3 && trap_Cvar_VariableValue( "ui_rustMenuTest" ) && !Q_stricmp(UI_Argv(1), "defer") ) {
			// Append after menu actions (map/disconnect), rather than exec-inserting ahead of them.
			trap_Cmd_ExecuteText(EXEC_APPEND, va("exec \"%s\"\n", UI_Argv(2)));
			return qtrue;
		}
		if ( trap_Argc() == 3 && ( trap_Cvar_VariableValue( "developer" ) || trap_Cvar_VariableValue( "ui_rustMenuTest" ) ) &&
			 !Q_stricmp( UI_Argv( 1 ), "script" ) ) {
			char *script = UI_Argv( 2 );
			UI_RunMenuScript( &script );
			return qtrue;
		}
		if ( trap_Argc() == 2 && ( trap_Cvar_VariableValue( "developer" ) || trap_Cvar_VariableValue( "ui_rustMenuTest" ) ) ) {
			menuDef_t *menu;
			Menus_CloseAll();
			trap_Key_SetCatcher( KEYCATCH_UI );
			menu = Menus_ActivateByName( UI_Argv( 1 ), qtrue );
			if ( menu ) {
				// Prevent the disconnected screen path from replacing the test menu.
				menu->fullScreen = qtrue;
				menu->window.background = 0;
				Com_Printf( "UI test menu: %s items=%d\n", menu->window.name, menu->itemCount );
				if ( menu->contextHelp ) {
					int i, overlaps = 0;
					Rectangle *h = &menu->contextHelp->window.rect;
					for ( i = 0; i < menu->itemCount; ++i ) {
						itemDef_t *control = menu->items[i];
						Rectangle *r = &control->window.rect;
						if ( control->window.flags & WINDOW_DECORATION ) continue;
						if ( r->x < h->x + h->w && r->x + r->w > h->x && r->y < h->y + h->h && r->y + r->h > h->y ) ++overlaps;
					}
					Com_Printf( "SHELL_CHECK menu=%s helpOverlaps=%d\n", menu->window.name, overlaps );
				}
			}
			return qtrue;
		}
		UI_ShowPostGame( qtrue );
	}

	if ( Q_stricmp( cmd, "ui_report" ) == 0 ) {
		UI_Report();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_load" ) == 0 ) {
		UI_Load();
		return qtrue;
	}

	// Arnout: we DEFINATELY do NOT want this here
	/*if ( Q_stricmp (cmd, "remapShader") == 0 ) {
		if (trap_Argc() == 4) {
			char shader1[MAX_QPATH];
			char shader2[MAX_QPATH];
			Q_strncpyz(shader1, UI_Argv(1), sizeof(shader1));
			Q_strncpyz(shader2, UI_Argv(2), sizeof(shader2));
			trap_R_RemapShader(shader1, shader2, UI_Argv(3));
			return qtrue;
		}
	}*/

	if ( Q_stricmp( cmd, "postgame" ) == 0 ) {
		UI_CalcPostGameStats();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_cache" ) == 0 ) {
		UI_Cache_f();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_teamOrders" ) == 0 ) {
		//UI_TeamOrdersMenu_f();
		return qtrue;
	}


	if ( Q_stricmp( cmd, "ui_cdkey" ) == 0 ) {
		//UI_CDKeyMenu_f();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "iamacheater" ) == 0 ) {
		int i;

		// unlock all available levels and campaigns for SP
		for ( i = 0; i < uiInfo.campaignCount; i++ ) {
			if ( uiInfo.campaignList[i].typeBits & ( 1 << GT_SINGLE_PLAYER ) ) {
				uiInfo.campaignList[i].unlocked = qtrue;
				uiInfo.campaignList[i].progress = uiInfo.campaignList[i].mapCount;
			}
		}
		return qtrue;
	}

	trap_GetClientState( &cstate );
	if ( cstate.connState == CA_DISCONNECTED ) {
		if ( Q_stricmp( cmd, "campaign" ) == 0 ) {
			UI_Campaign_f();
			return qtrue;
		}

		if ( Q_stricmp( cmd, "listcampaigns" ) == 0 ) {
			UI_ListCampaigns_f();
			return qtrue;
		}
	}

	return qfalse;
}

/*
=================
UI_Shutdown
=================
*/
void UI_Shutdown( void ) {
}

/*
================
UI_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void UI_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	// expect valid pointers
#if 0
	*x = *x * uiInfo.uiDC.scale + uiInfo.uiDC.bias;
	*y *= uiInfo.uiDC.scale;
	*w *= uiInfo.uiDC.scale;
	*h *= uiInfo.uiDC.scale;
#endif

	*x = *x * uiInfo.uiDC.xscale + uiInfo.uiDC.bias;
	*y *= uiInfo.uiDC.yscale;
	*w *= uiInfo.uiDC.xscale;
	*h *= uiInfo.uiDC.yscale;

}

void UI_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t hShader;

	hShader = trap_R_RegisterShaderNoMip( picname );
	UI_AdjustFrom640( &x, &y, &width, &height );
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}

/* ponytail: one static backdrop shared by menus; shader animation only if requested. */
void UI_DrawBackdrop( void ) {
	float s0, t0, s1, t1;
	UI_ViewportCover( uiInfo.uiDC.glconfig.vidWidth, uiInfo.uiDC.glconfig.vidHeight,
		2048.f / 856.f, &s0, &t0, &s1, &t1 );
	// Legacy renderer requires power-of-two textures; never sample padded rows.
	t0 = ( 84.f + 856.f * t0 ) / 1024.f;
	t1 = ( 84.f + 856.f * t1 ) / 1024.f;
	trap_R_SetColor( NULL );
	trap_R_DrawStretchPic( 0, 0, uiInfo.uiDC.glconfig.vidWidth, uiInfo.uiDC.glconfig.vidHeight,
		s0, t0, s1, t1, trap_R_RegisterShaderNoMip( "ui/assets/ultrawide_storm.jpg" ) );
}

void UI_DrawHandlePic( float x, float y, float w, float h, qhandle_t hShader ) {
	float s0;
	float s1;
	float t0;
	float t1;

	if ( w < 0 ) {   // flip about vertical
		w  = -w;
		s0 = 1;
		s1 = 0;
	} else {
		s0 = 0;
		s1 = 1;
	}

	if ( h < 0 ) {   // flip about horizontal
		h  = -h;
		t0 = 1;
		t1 = 0;
	} else {
		t0 = 0;
		t1 = 1;
	}

	UI_AdjustFrom640( &x, &y, &w, &h );
	trap_R_DrawStretchPic( x, y, w, h, s0, t0, s1, t1, hShader );
}

/*
================
UI_DrawRotatedPic

Coordinates are 640*480 virtual values
=================
*/
void UI_DrawRotatedPic( float x, float y, float width, float height, qhandle_t hShader, float angle ) {

	UI_AdjustFrom640( &x, &y, &width, &height );

	trap_R_DrawRotatedPic( x, y, width, height, 0, 0, 1, 1, hShader, angle );
}

/*
================
UI_FillRect

Coordinates are 640*480 virtual values
=================
*/
void UI_FillRect( float x, float y, float width, float height, const float *color ) {
	trap_R_SetColor( color );

	if ( x == 0 && y == 0 && width == 640 && height == 480 ) {
		width = uiInfo.uiDC.glconfig.vidWidth;
		height = uiInfo.uiDC.glconfig.vidHeight;
	} else {
		UI_AdjustFrom640( &x, &y, &width, &height );
	}
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );

	trap_R_SetColor( NULL );
}

void UI_DrawSides( float x, float y, float w, float h ) {
	UI_AdjustFrom640( &x, &y, &w, &h );
	trap_R_DrawStretchPic( x, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap_R_DrawStretchPic( x + w - 1, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}

void UI_DrawTopBottom( float x, float y, float w, float h ) {
	UI_AdjustFrom640( &x, &y, &w, &h );
	trap_R_DrawStretchPic( x, y, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap_R_DrawStretchPic( x, y + h - 1, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}
/*
================
UI_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void UI_DrawRect( float x, float y, float width, float height, const float *color ) {
	trap_R_SetColor( color );

	UI_DrawTopBottom( x, y, width, height );
	UI_DrawSides( x, y, width, height );

	trap_R_SetColor( NULL );
}

void UI_SetColor( const float *rgba ) {
	trap_R_SetColor( rgba );
}

void UI_UpdateScreen( void ) {
	trap_UpdateScreen();
}


void UI_DrawTextBox( int x, int y, int width, int lines ) {
	UI_FillRect( x + BIGCHAR_WIDTH / 2, y + BIGCHAR_HEIGHT / 2, ( width + 1 ) * BIGCHAR_WIDTH, ( lines + 1 ) * BIGCHAR_HEIGHT, colorBlack );
	UI_DrawRect( x + BIGCHAR_WIDTH / 2, y + BIGCHAR_HEIGHT / 2, ( width + 1 ) * BIGCHAR_WIDTH, ( lines + 1 ) * BIGCHAR_HEIGHT, colorWhite );
}

qboolean UI_CursorInRect( int x, int y, int width, int height ) {
	if ( uiInfo.uiDC.cursorx < x ||
		 uiInfo.uiDC.cursory < y ||
		 uiInfo.uiDC.cursorx > x + width ||
		 uiInfo.uiDC.cursory > y + height ) {
		return qfalse;
	}

	return qtrue;
}
