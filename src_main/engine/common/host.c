/*
host.c - dedicated and normal host
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "netchan.h"
#include "protocol.h"
#include "mod_local.h"
#include "mathlib.h"
#include "input.h"

static const char *show_credits = "\n\n\n\n\tCopyright XashXT Group %s ?\n\t\
          All Rights Reserved\n\n\t           Visit hlfx.ru/forum\n";

typedef void (*pfnChangeGame)( const char *progname );

pfnChangeGame	pChangeGame = NULL;
HINSTANCE		hCurrent;	// hinstance of current .dll
host_parm_t	host;	// host parms
sysinfo_t		SI;

convar_t	*host_serverstate;
convar_t	*host_gameloaded;
convar_t	*host_limitlocal;
convar_t	*host_cheats;
convar_t	*host_maxfps;
convar_t	*host_framerate;
convar_t	*con_gamemaps;

static int num_decals;
BOOL console_ui = 1;

// these cvars will be duplicated on each client across network
int Host_ServerState( void )
{
	return Cvar_VariableInteger( "host_serverstate" );
}

int Host_CompareFileTime( long ft1, long ft2 )
{
	if( ft1 < ft2 )
	{
		return -1;
	}
	else if( ft1 > ft2 )
	{
		return 1;
	}
	return 0;
}

void Host_ShutdownServer( void )
{
	if( !SV_Active()) return;
	Q_strncpy( host.finalmsg, "Server was killed", MAX_STRING );
	SV_Shutdown( false );
}

/*
================
Host_NewGame
================
*/
qboolean Host_NewGame( const char *mapName, qboolean loadGame )
{
	qboolean	iRet;

	iRet = SV_NewGame( mapName, loadGame );

	return iRet;
}
/*
================
Host_EndGame
================
*/
void Host_EndGame( const char *message, ... )
{
	va_list		argptr;
	static char	string[MAX_SYSPATH];
	
	va_start( argptr, message );
	Q_vsprintf( string, message, argptr );
	va_end( argptr );

	MsgDev( D_INFO, "Host_EndGame: %s\n", string );
	
	if( SV_Active())
	{
		Q_snprintf( host.finalmsg, sizeof( host.finalmsg ), "Host_EndGame: %s", string );
		SV_Shutdown( false );
	}
	
	if( host.type == HOST_DEDICATED )
		Sys_Break( "Host_EndGame: %s\n", string ); // dedicated servers exit
	
	if( CL_NextDemo( ));
	else CL_Disconnect();

	Host_AbortCurrentFrame ();
}

/*
================
Host_AbortCurrentFrame

aborts the current host frame and goes on with the next one
================
*/
void Host_AbortCurrentFrame( void )
{
	longjmp( host.abortframe, 1 );
}

/*
==================
Host_SetServerState
==================
*/
void Host_SetServerState( int state )
{
	Cvar_FullSet( "host_serverstate", va( "%i", state ), CVAR_INIT );
}

void Host_NewInstance( const char *name, const char *finalmsg )
{
	if( !pChangeGame ) return;

	host.change_game = true;
	Q_strncpy( host.finalmsg, finalmsg, sizeof( host.finalmsg ));
	pChangeGame( name ); // call from hl.exe
}

/*
=================
Host_ChangeGame_f

Change game modification
=================
*/
void Host_ChangeGame_f( void )
{
	int	i;

	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: game <directory>\n" );
		return;
	}

	// validate gamedir
	for( i = 0; i < SI.numgames; i++ )
	{
		if( !Q_stricmp( SI.games[i]->gamefolder, Cmd_Argv( 1 )))
			break;
	}

	if( i == SI.numgames )
	{
		Msg( "%s not exist\n", Cmd_Argv( 1 ));
	}
	else if( !Q_stricmp( GI->gamefolder, Cmd_Argv( 1 )))
	{
		Msg( "%s already active\n", Cmd_Argv( 1 ));	
	}
	else
	{
		Host_NewInstance( Cmd_Argv( 1 ), va( "change game to '%s'", SI.games[i]->title ));
	}
}

/*
===============
Host_Exec_f
===============
*/
void Host_Exec_f( void )
{
	string	cfgpath;
	size_t	len;
	char	*f; 

	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: exec <filename>\n" );
		return;
	}

	// HACKHACK: don't execute listenserver.cfg in singleplayer
	if( !Q_stricmp( Cvar_VariableString( "lservercfgfile" ),  Cmd_Argv( 1 )))
	{
		if( Cvar_VariableValue( "maxplayers" ) == 1.0f )
			return;
	}

	Q_strncpy( cfgpath, Cmd_Argv( 1 ), sizeof( cfgpath )); 
	FS_DefaultExtension( cfgpath, ".cfg" ); // append as default

	f = FS_LoadFile( cfgpath, &len, false );
	if( !f )
	{
		MsgDev( D_NOTE, "couldn't exec %s\n", Cmd_Argv( 1 ));
		return;
	}

	MsgDev( D_INFO, "execing %s\n", Cmd_Argv( 1 ));
	Cbuf_InsertText( f );
	Mem_Free( f );
}

/*
===============
Host_MemStats_f
===============
*/
void Host_MemStats_f( void )
{
	switch( Cmd_Argc( ))
	{
	case 1:
		Mem_PrintList( 1<<30 );
		Mem_PrintStats();
		break;
	case 2:
		Mem_PrintList( Q_atoi( Cmd_Argv( 1 )) * 1024 );
		Mem_PrintStats();
		break;
	default:
		Msg( "Usage: memlist <all>\n" );
		break;
	}
}

void Host_Minimize_f( void )
{
	if( host.hWnd ) ShowWindow( host.hWnd, SW_MINIMIZE );
}

qboolean Host_IsLocalGame( void )
{
	if( CL_Active() && SV_Active() && CL_GetMaxClients() == 1 )
		return true;
	return false;
}

/*
=================
Host_RegisterDecal
=================
*/
qboolean Host_RegisterDecal( const char *name )
{
	char	shortname[CS_SIZE];
	int	i;

	if( !name || !name[0] )
		return 0;

	FS_FileBase( name, shortname );

	for( i = 1; i < MAX_DECALS && host.draw_decals[i][0]; i++ )
	{
		if( !Q_stricmp( host.draw_decals[i], shortname ))
			return true;
	}

	if( i == MAX_DECALS )
	{
		MsgDev( D_ERROR, "Host_RegisterDecal: MAX_DECALS limit exceeded\n" );
		return false;
	}

	// register new decal
	Q_strncpy( host.draw_decals[i], shortname, sizeof( host.draw_decals[i] ));
	num_decals++;

	return true;
}

/*
=================
Host_InitDecals
=================
*/
void Host_InitDecals( void )
{
	search_t	*t;
	int	i;

	Q_memset( host.draw_decals, 0, sizeof( host.draw_decals ));
	num_decals = 0;

	// lookup all decals in decals.wad
	t = FS_Search( "decals.wad/*.*", true, false );

	for( i = 0; t && i < t->numfilenames; i++ )
	{
		if( !Host_RegisterDecal( t->filenames[i] ))
			break;
	}

	if( t ) Mem_Free( t );
	MsgDev( D_NOTE, "InitDecals: %i decals\n", num_decals );
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands( void )
{
	char	*cmd;

	if( host.type == HOST_DEDICATED )
	{
		cmd = Con_Input();
		if( cmd ) Cbuf_AddText( cmd );
	}
}

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime( float time )
{
	static double	oldtime;
	float		fps;

	host.realtime += time;

	// dedicated's tic_rate regulates server frame rate.  Don't apply fps filter here.
	fps = host_maxfps->value;

	if( fps != 0 )
	{
		float	minframetime;

		// limit fps to withing tolerable range
		fps = bound( MIN_FPS, fps, MAX_FPS );

		minframetime = 1.0f / fps;

		if(( host.realtime - oldtime ) < minframetime )
		{
			// framerate is too high
			return false;		
		}
	}

	host.frametime = host.realtime - oldtime;
	host.realframetime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );
	oldtime = host.realtime;

	if( host_framerate->value > 0 && ( Host_IsLocalGame() || CL_IsPlaybackDemo() ))
	{
		float fps = host_framerate->value;
		if( fps > 1 ) fps = 1.0f / fps;
		host.frametime = fps;
	}
	else
	{	// don't allow really long or short frames
		host.frametime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );
	}
	
	return true;
}

/*
=================
Host_Frame
=================
*/
void Host_Frame( float time )
{
	if( setjmp( host.abortframe ))
		return;

	Host_InputFrame ();	// input frame

	// decide the simulation time
	if( !Host_FilterTime( time ))
		return;

	Host_GetConsoleCommands ();

	Host_ServerFrame (); // server frame
	Host_ClientFrame (); // client frame

	host.framecount++;
}

/*
================
Host_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void Host_Print( const char *txt )
{
	if( host.rd.target )
	{
		if(( Q_strlen( txt ) + Q_strlen( host.rd.buffer )) > ( host.rd.buffersize - 1 ))
		{
			if( host.rd.flush )
			{
				host.rd.flush( host.rd.address, host.rd.target, host.rd.buffer );
				*host.rd.buffer = 0;
			}
		}
		Q_strcat( host.rd.buffer, txt );
		return;
	}
	Con_Print( txt ); // echo to client console
}

/*
=================
Host_Error
=================
*/
void Host_Error( const char *error, ... )
{
	static char	hosterror1[MAX_SYSPATH];
	static char	hosterror2[MAX_SYSPATH];
	static qboolean	recursive = false;
	va_list		argptr;

	if( host.mouse_visible )
	{
		// hide mouse
		while( ShowCursor( false ) >= 0 );
		host.mouse_visible = false;
	}

	va_start( argptr, error );
	Q_vsprintf( hosterror1, error, argptr );
	va_end( argptr );

	CL_WriteMessageHistory (); // before Q_error call

	if( host.framecount < 3 )
	{
		Sys_Error( "Host_InitError: %s", hosterror1 );
	}
	else if( host.framecount == host.errorframe )
	{
		Sys_Error( "Host_MultiError: %s", hosterror2 );
		return;
	}
	else
	{
		Msg( "Host_Error: %s", hosterror1 );
	}

	// host is shutting down. don't invoke infinite loop
	if( host.state == HOST_SHUTDOWN ) return;

	if( recursive )
	{ 
		Msg( "Host_RecursiveError: %s", hosterror2 );
		Sys_Error( hosterror1 );
		return; // don't multiple executes
	}

	recursive = true;
	Q_strncpy( hosterror2, hosterror1, MAX_SYSPATH );
	host.errorframe = host.framecount; // to avoid multply calls per frame
	Q_sprintf( host.finalmsg, "Server crashed: %s", hosterror1 );

	CL_Drop(); // drop clients
	SV_Shutdown( false );

	recursive = false;
	Host_AbortCurrentFrame();
	host.state = HOST_ERROR;
}

void Host_Error_f( void )
{
	const char *error = Cmd_Argv( 1 );

	if( !*error ) error = "Invoked host error";
	Host_Error( "%s\n", error );
}

void Sys_Error_f( void )
{
	const char *error = Cmd_Argv( 1 );

	if( !*error ) error = "Invoked sys error";
	Sys_Error( "%s\n", error );
}

void Net_Error_f( void )
{
	Q_strncpy( host.finalmsg, Cmd_Argv( 1 ), sizeof( host.finalmsg ));
	SV_ForceError();
}

/*
=================
Host_Crash_f
=================
*/
static void Host_Crash_f( void )
{
	*(int *)0 = 0xffffffff;
}

void Host_InitCommon( const char *progname, qboolean bChangeGame, BOOL dedicated )
{
	MEMORYSTATUS	lpBuffer;
	char		dev_level[4];
	char		szTemp[MAX_SYSPATH];

	lpBuffer.dwLength = sizeof( MEMORYSTATUS );
	GlobalMemoryStatus( &lpBuffer );

	if( !GetCurrentDirectory( sizeof( host.rootdir ), host.rootdir ))
		Sys_Error( "couldn't determine current directory" );

	if( host.rootdir[Q_strlen( host.rootdir ) - 1] == '/' )
		host.rootdir[Q_strlen( host.rootdir ) - 1] = 0;

	host.oldFilter = SetUnhandledExceptionFilter( Sys_Crash );
	host.hInst = GetModuleHandle( NULL );
	host.change_game = bChangeGame;
	host.state = HOST_INIT; // initialzation started
	host.developer = 0;

	CRT_Init(); // init some CRT functions

	// some commands may turn engine into infinity loop,
	// e.g. xash.exe +game xash -game xash
	// so we clearing all cmd_args, but leave dbg states as well
	Sys_ParseCommandLine( GetCommandLine( ));
	SetErrorMode( SEM_FAILCRITICALERRORS );	// no abort/retry/fail errors

	host.mempool = Mem_AllocPool( "Zone Engine" );

	if( Sys_CheckParm( "-console" )) host.developer = 1;
	if( Sys_CheckParm( "-dev" ))
	{
		if( Sys_GetParmFromCmdLine( "-dev", dev_level ))
		{
			if( Q_isdigit( dev_level ))
				host.developer = abs( Q_atoi( dev_level ));
			else host.developer++; // -dev == 1, -dev -console == 2
		}
		else host.developer++; // -dev == 1, -dev -console == 2
	}

	if( Sys_CheckParm( "-noui" ))
	{
		console_ui = 0;
	}

	host.type = HOST_NORMAL; // predict state
	host.con_showalways = true;

	// we can specified custom name, from Sys_NewInstance
	if( GetModuleFileName( NULL, szTemp, sizeof( szTemp )) && !host.change_game )
		FS_FileBase( szTemp, SI.ModuleName );

	if( SI.ModuleName[0] == '#' ) host.type = HOST_DEDICATED; 

	// determine host type
	if( progname[0] == '#' )
	{
		Q_strncpy( SI.ModuleName, progname + 1, sizeof( SI.ModuleName ));
		host.type = HOST_DEDICATED;
	}
	else Q_strncpy( SI.ModuleName, progname, sizeof( SI.ModuleName )); 

	if( Sys_CheckParm( "-dedicated" )) host.type = HOST_DEDICATED;
	if( dedicated ) host.type = HOST_DEDICATED;

	if( host.type == HOST_DEDICATED )
	{
		// check for duplicate dedicated server
		host.hMutex = CreateMutex( NULL, 0, "Sing Dedicated Server" );
		if( !host.hMutex )
		{
			MSGBOX( "Dedicated server already running" );
			Sys_Quit();
			return;
		}

		Sys_MergeCommandLine( GetCommandLine( ));

		CloseHandle( host.hMutex );
		host.hMutex = CreateSemaphore( NULL, 0, 1, "Sing Dedicated Server" );
		if( host.developer < 3 ) host.developer = 3; // otherwise we see empty console
		host.change_game = false;
	}
	else
	{
		// don't show console as default
		if( host.developer < D_WARN ) host.con_showalways = false;
	}

	if( GetModuleFileName( hCurrent, szTemp, sizeof( szTemp )))
		FS_FileBase( szTemp, szTemp );

	// protect to rename engine.dll
/*	if( Q_stricmp( szTemp, "engine" ) && Com_RandomLong( 0, 1 ))
	{
		host.type = HOST_CREDITS;
		host.con_showalways = true;
		Con_CreateConsole();
		Sys_Break( show_credits, Q_timestamp( TIME_YEAR_ONLY ));
	}*/

	Con_CreateConsole();

	// first text message into console or log 
	MsgDev( D_NOTE, "Sys_LoadLibrary: Loading engine.dll - ok\n" );

	// startup cmds and cvars subsystem
	Cmd_Init();
	Cvar_Init();

	// share developer level across all dlls
	Q_snprintf( dev_level, sizeof( dev_level ), "%i", host.developer );
	Cvar_Get( "developer", dev_level, CVAR_INIT, "current developer level" );
	Cmd_AddCommand( "exec", Host_Exec_f, "execute a script file" );
	Cmd_AddCommand( "memlist", Host_MemStats_f, "prints memory pool information" );

	FS_Init();
	Image_Init();
	Sound_Init();

	FS_LoadGameInfo( NULL );
	Q_strncpy( host.gamefolder, GI->gamefolder, sizeof( host.gamefolder ));

	HPAK_Init();

	IN_Init();
	Key_Init();
}

void Host_FreeCommon( void )
{
	Image_Shutdown();
	Sound_Shutdown();
	Netchan_Shutdown();
	FS_Shutdown();

	Mem_FreePool( &host.mempool );
}

/*
=================
Host_Main
=================
*/
int EXPORT Host_Main( const char *progname, int bChangeGame, pfnChangeGame func, BOOL dedicated )
{
	static double	oldtime, newtime;

	pChangeGame = func;

	Host_InitCommon( progname, bChangeGame, dedicated );

	// init commands and vars
	if( host.developer >= 3 )
	{
		Cmd_AddCommand ( "sys_error", Sys_Error_f, "just throw a fatal error to test shutdown procedures");
		Cmd_AddCommand ( "host_error", Host_Error_f, "just throw a host error to test shutdown procedures");
		Cmd_AddCommand ( "crash", Host_Crash_f, "a way to force a bus error for development reasons");
		Cmd_AddCommand ( "net_error", Net_Error_f, "send network bad message from random place");
          }

	host_cheats = Cvar_Get( "sv_cheats", "0", CVAR_LATCH, "allow cheat variables to enable" );
	host_maxfps = Cvar_Get( "fps_max", "72", CVAR_ARCHIVE, "host fps upper limit" );
	host_framerate = Cvar_Get( "host_framerate", "0", 0, "locks frame timing to this value in seconds" );  
	host_serverstate = Cvar_Get( "host_serverstate", "0", CVAR_INIT, "displays current server state" );
	host_gameloaded = Cvar_Get( "host_gameloaded", "0", CVAR_INIT, "inidcates a loaded game.dll" );
	host_limitlocal = Cvar_Get( "host_limitlocal", "0", 0, "apply cl_cmdrate and rate to loopback connection" );
	con_gamemaps = Cvar_Get( "con_gamemaps", "1", CVAR_ARCHIVE, "when true show only maps in game folder" );

	// content control
	Cvar_Get( "violence_hgibs", "1", CVAR_ARCHIVE, "show human gib entities" );
	Cvar_Get( "violence_agibs", "1", CVAR_ARCHIVE, "show alien gib entities" );
	Cvar_Get( "violence_hblood", "1", CVAR_ARCHIVE, "draw human blood" );
	Cvar_Get( "violence_ablood", "1", CVAR_ARCHIVE, "draw alien blood" );

	if( host.type != HOST_DEDICATED )
	{
		// when we in developer-mode automatically turn cheats on
		if( host.developer > 1 ) Cvar_SetFloat( "sv_cheats", 1.0f );
		Cbuf_AddText( "exec video.cfg\n" );
	}

	Mod_Init();
	NET_Init();
	Netchan_Init();

	// allow to change game from the console
	if( pChangeGame != NULL )
	{
		Cmd_AddCommand( "game", Host_ChangeGame_f, "change game" );
		Cvar_Get( "host_allow_changegame", "1", CVAR_READ_ONLY, "allows to change games" );
	}
	else
	{
		Cvar_Get( "host_allow_changegame", "0", CVAR_READ_ONLY, "allows to change games" );
	}

	SV_Init();
	CL_Init();

	if( host.type == HOST_DEDICATED )
	{
		Cmd_AddCommand( "quit", Sys_Quit, "quit the game" );
		Cmd_AddCommand( "exit", Sys_Quit, "quit the game" );

		// dedicated servers using settings from server.cfg file
		Cbuf_AddText( va( "exec %s\n", Cvar_VariableString( "servercfgfile" )));
		Cbuf_Execute();

		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "defaultmap" )));
	}
	else
	{
		Cmd_AddCommand( "minimize", Host_Minimize_f, "minimize main window to tray" );
		Cbuf_AddText( "exec config.cfg\n" );
	}

	host.errorframe = 0;
	Cbuf_Execute();

	// post initializations
	switch( host.type )
	{
	case HOST_NORMAL:
		Con_ShowConsole( false ); // hide console
		// execute startup config and cmdline
		Cbuf_AddText( va( "exec %s.rc\n", SI.ModuleName ));
		// intentional fallthrough
	case HOST_DEDICATED:
		// if stuffcmds wasn't run, then init.rc is probably missing, use default
		if( !host.stuffcmdsrun ) Cbuf_AddText( "stuffcmds\n" );

		Cbuf_Execute();
		break;
	}

	SCR_CheckStartupVids();	// must be last

	host.change_game = false;	// done
	Cmd_RemoveCommand( "setr" );	// remove potentially backdoor for change render settings
	Cmd_RemoveCommand( "setgl" );

	// we need to execute it again here
	Cmd_ExecuteString( "exec config.cfg\n", src_command );
	oldtime = Sys_DoubleTime();

	// main window message loop
	while( 1 )
	{
		newtime = Sys_DoubleTime ();
		Host_Frame( newtime - oldtime );
		oldtime = newtime;
	}

	// never reached
	return 0;
}

/*
=================
Host_Shutdown
=================
*/
void EXPORT Host_Shutdown( void )
{
	if( host.shutdown_issued ) return;
	host.shutdown_issued = true;

	if( host.state != HOST_ERR_FATAL ) host.state = HOST_SHUTDOWN; // prepare host to normal shutdown
	if( !host.change_game ) Q_strncpy( host.finalmsg, "Server shutdown", sizeof( host.finalmsg ));

	if( host.type == HOST_NORMAL )
		Host_WriteConfig();

	SV_Shutdown( false );
	CL_Shutdown();

	Mod_Shutdown();
	NET_Shutdown();
	Host_FreeCommon();
	Con_DestroyConsole();

	// restore filter	
	if( host.oldFilter ) SetUnhandledExceptionFilter( host.oldFilter );
}

// main DLL entry point
#ifndef LINUX
BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved )
{
	hCurrent = hinstDLL;
	return TRUE;
}
#else
int main()
{
	hCurrent = NULL;
	Host_Main("valve", false, NULL, false);
}
#endif
