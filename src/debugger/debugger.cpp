// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger.h"

#if C_DEBUGGER

#include "breakpoint.h"
#include "cpu/paging.h"
#include "debug.h"
#include "debugger_inc.h"
#include "debugvar.h"
#include "gui/common.h"
#include "gui/mapper.h"
#include "hardware/input/keyboard.h"
#include "hardware/pic.h"
#include "hardware/timer.h"
#include "shell/shell.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

extern uint32_t DEBUG_ProcessKey( SDL_KeyboardEvent );
extern void SetCodeWinToEIP( );

DBGBlock dbg = {};
bool debugging = false;
bool forceDraw = false;
bool exitNormalLoop = false;
bool exitDebugLoop = false;

// Event queue
std::queue<DebuggerInputEvent> debugger_event_queue = {};
const int64_t frameInterval = 1000 / 60;

Bitu DEBUG_Loop( void ) {
	if( !GFX_PollAndHandleEvents( ) )
		return -1;

	// Interrupt started ? - then skip it
	uint16_t oldCS = SegValue( cs );
	uint32_t oldEIP = reg_eip;
	PIC_runIRQs( );
	Delay( 1 );
	if( ( oldCS != SegValue( cs ) ) || ( oldEIP != reg_eip ) ) {
		CBreakpoint::AddBreakpoint( oldCS, oldEIP, true );
		CBreakpoint::ActivateBreakpointsExceptAt( SegPhys( cs ) + reg_eip );
		debugging = false;
		DOSBOX_SetNormalLoop( );
	}
	Bitu ret = 0;
	// Check event queue
	while( debugging && !debugger_event_queue.empty( ) ) {
		DebuggerInputEvent event = debugger_event_queue.front( );
		debugger_event_queue.pop( );

		// Process ImGui events
		ImGui_ImplSDL3_ProcessEvent( &event.ev );

		switch( event.ev.type ) {
		case SDL_EVENT_KEY_DOWN:
			ret = DEBUG_ProcessKey( event.ev.key );
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			DEBUG_Close( );
			return ret;
			break;
		default:
			break;
		}
	}
	static auto tickCounter = GetTicks( );
	if( forceDraw || ( debugging && GetTicksSince( tickCounter ) > frameInterval ) ) {
		forceDraw = false;
		DBGUI_NewFrame( ); // Start a new ImGui frame
		DBGUI_DrawScreen( ); // Draw debugger windows
		DBGUI_Render( ); // Render ImGui
		tickCounter += frameInterval;
	}
	if( exitDebugLoop ) {
		exitDebugLoop = false;
		exitNormalLoop = true;
		DBGUI_Reset( );
		DBGUI_SaveCPUstate( );
		return -1;
	}
	return ret;
}

void DEBUG_Enable( bool pressed ) {
	if( !pressed )
		return;

	static bool was_ui_started = false;
	if( !was_ui_started )
		was_ui_started = DBGUI_StartUp( );
	else
		SDL_ShowWindow( dbg.win_main );

	if( !was_ui_started ) { // The debugger is run in release mode so cannot use asserts
		LOG_ERR( "DEBUG: Failed to start up the debug window" );
		return;
	}

	GFX_LosingFocus( );					// Defocus the graphical UI...
	SDL_RaiseWindow( dbg.win_main );	// ...and bring the debugger UI into focus
	SetCodeWinToEIP( );
	DBGUI_UpdateOrderedSegments( );

	static bool was_help_shown = false;
	if( !was_help_shown ) { // Show first time help
		DEBUG_ShowMsg( "           TYPE ? or HELP (+ENTER) TO GET AN OVERVIEW OF ALL COMMANDS           \n" );
		was_help_shown = true;
	}

	debugging = true;
	DOSBOX_SetLoop( &DEBUG_Loop ); // Start the debugging loop

	KEYBOARD_ClrBuffer( );
}

void DEBUG_Close( ) {
	SDL_HideWindow( dbg.win_main );
	debugging = false;
	DOSBOX_SetNormalLoop( );
}

Bitu DEBUG_EnableDebugger( ) {
	exitNormalLoop = true;
	DEBUG_Enable( true );
	CPU_Cycles = CPU_CycleLeft = 0;
	return 0;
}

extern SCodeViewData codeViewData;

Bitu debugCallback;

void DEBUG_Init( ) {
	// Add some keyhandlers
	MAPPER_AddHandler( DEBUG_Enable, SDL_SCANCODE_PAUSE, MMOD2, "debugger", "Debugger" );

	// Reset code overview and input line
	codeViewData = {};

	// setup debug.com
	PROGRAMS_MakeFile( "DEBUG.COM", ProgramCreate<DEBUG> );
	PROGRAMS_MakeFile( "DBXDEBUG.COM", ProgramCreate<DEBUG> );

	// Setup callback
	debugCallback = CALLBACK_Allocate( );

	CALLBACK_Setup( debugCallback, DEBUG_EnableDebugger, CB_RETF, "debugger" );
}

void DEBUG_Destroy( ) {
	CBreakpoint::DeleteAll( );
	CDebugVar::DeleteAll( );

	DBGUI_Shutdown( );
}

void DEBUG_AddConfigSection( const ConfigPtr& conf ) {
	assert( conf );

	// TODO the [debug] section has no settings, so what's the point?
	conf->AddSection( "debug" );
}

void DEBUG_CheckExecuteBreakpoint( uint16_t seg, uint32_t off ) {
	if( pDebugcom && pDebugcom->IsActive( ) ) {
		CBreakpoint::AddBreakpoint( seg, off, true );
		CBreakpoint::ActivateBreakpointsExceptAt( SegPhys( cs ) + reg_eip );
		pDebugcom = nullptr;
	}
}

bool DEBUG_ExitNormalLoop( void ) {
	if( exitNormalLoop ) {
		exitNormalLoop = false;
		return true;
	}
	return false;
}
#endif // C_DEBUGGER
