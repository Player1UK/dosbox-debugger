// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER

#include "cbreakpoint.h"
#include "cpu/paging.h"
#include "debugger.h"
#include "debugger_inc.h"
#include "gui/common.h"
#include "hardware/pic.h"
#include "shell/shell.h"

extern uint32_t GetAddress( uint16_t, uint32_t );
extern bool ParseCommand( char* );
extern void SetCodeWinStart( );

extern bool exitLoop;

extern DBGBlock dbg;
extern bool debugging;

extern SCodeViewData codeViewData;

extern bool showPrintable;

uint16_t dataSeg[NUM_WIN_DATA] = { 0 };//, 0, 0, 0 };
uint32_t dataOfs[NUM_WIN_DATA] = { 0 };//, 0, 0, 0 };

bool skipFirstInstruction = false;

// History stuff
#define MAX_HIST_BUFFER 50
static std::list<std::string> histBuff = {};
static auto histBuffPos = histBuff.end( );

static bool StepOver( ) {
	exitLoop = false;
	PhysPt start = GetAddress( SegValue( cs ), reg_eip );
	char dline[200];
	Bitu size;
	size = DasmI386( dline, start, reg_eip, cpu.code.big, cpu.pmode );

	if( strstr( dline, "call" ) || strstr( dline, "int" ) ||
		strstr( dline, "loop" ) || strstr( dline, "rep" ) ) {
		// Don't add a temporary breakpoint if there's already one here
		if( !CBreakpoint::FindPhysBreakpoint( SegValue( cs ), reg_eip + size, true ) ) {
			CBreakpoint::AddBreakpoint( SegValue( cs ), reg_eip + size, true );
		}
		debugging = false;
		return true;
	}
	return false;
}

static int32_t DEBUG_Run( int32_t amount, bool quickexit ) {
	skipFirstInstruction = true;
	CPU_CycleLeft += CPU_Cycles - amount;
	CPU_Cycles = amount;
	int32_t ret = ( *cpudecoder )( );
	dbg.update_win[WIN_CODE] = true;
	dbg.update_win[WIN_REG] = true;
	if( quickexit ) {
		SetCodeWinStart( );
	} else {
		// ensure all breakpoints are activated
		CBreakpoint::ActivateBreakpoints( );

		const auto graphics_window = GFX_GetWindow( );
		SDL_RaiseWindow( graphics_window );

		DOSBOX_SetNormalLoop( );
	}
	return ret;
}

uint32_t DEBUG_ProcessKey( SDL_KeyboardEvent key ) {
	Bits ret = 0;

	switch( key.key ) {
	case SDLK_C: // ALT - C: CS:IP
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( cs );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_eip;
		} else {
			dataOfs[dbg.active_win_data] = reg_ip;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_D: // ALT - D: DS:SI
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( ds );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_esi;
		} else {
			dataOfs[dbg.active_win_data] = reg_si;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_E: // ALT - E: es:di
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( es );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_edi;
		} else {
			dataOfs[dbg.active_win_data] = reg_di;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_X: // ALT - X: ds:dx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( ds );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_edx;
		} else {
			dataOfs[dbg.active_win_data] = reg_dx;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_B: // ALT -B: es:bx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( es );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_ebx;
		} else {
			dataOfs[dbg.active_win_data] = reg_bx;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_S: // ALT - S: ss:sp
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue( ss );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_win_data] = reg_esp;
		} else {
			dataOfs[dbg.active_win_data] = reg_sp;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_F6: // previous command (f1-f4 generate rubbish at my place)
	case SDLK_F3: // previous command
		if( histBuffPos == histBuff.begin( ) ) {
			break;
		}
		if( histBuffPos == histBuff.end( ) ) {
			// copy inputStr to suspInputStr so we can restore it
			safe_strcpy( codeViewData.suspInputStr,
				codeViewData.inputStr );
		}
		safe_strcpy( codeViewData.inputStr, ( --histBuffPos )->c_str( ) );
		break;
	case SDLK_F7: // next command (f1-f4 generate rubbish at my place)
	case SDLK_F4: // next command
		if( histBuffPos == histBuff.end( ) ) {
			break;
		}
		if( ++histBuffPos != histBuff.end( ) ) {
			safe_strcpy( codeViewData.inputStr,
				histBuffPos->c_str( ) );
		} else {
			// copy suspInputStr back into inputStr
			safe_strcpy( codeViewData.inputStr,
				codeViewData.suspInputStr );
		}
		break;
	case SDLK_F5: // Run Program
		debugging = false;
		// Redraw screen to show "(Running)" before entering normal loop
		DBGUI_NewFrame( );
		DEBUG_DrawScreen( );
		DBGUI_Render( );
		ret = DEBUG_Run( 1, false );
		break;
	case SDLK_F8: // Toggle printable characters
		showPrintable = !showPrintable;
		break;
	case SDLK_F9: // Set/Remove Breakpoint
		if( CBreakpoint::IsBreakpoint( codeViewData.cursorSeg,
			codeViewData.cursorOfs ) ) {
			if( CBreakpoint::DeleteBreakpoint( codeViewData.cursorSeg,
				codeViewData.cursorOfs ) ) {
				DEBUG_ShowMsg( "DEBUG: Breakpoint deletion success.\n" );
			} else {
				DEBUG_ShowMsg( "DEBUG: Failed to delete breakpoint.\n" );
			}
		} else {
			CBreakpoint::AddBreakpoint( codeViewData.cursorSeg,
				codeViewData.cursorOfs,
				false );
			DEBUG_ShowMsg( "DEBUG: Set breakpoint at %04X:%04X\n",
				codeViewData.cursorSeg,
				codeViewData.cursorOfs );
		}
		break;
	case SDLK_F10: // Step over inst
		if( StepOver( ) ) {
			ret = DEBUG_Run( 1, false );
			break;
		}
		// If we aren't stepping over something, do a normal step.
		[[fallthrough]];
	case SDLK_F11: // trace into
		exitLoop = false;
		ret = DEBUG_Run( 1, true );
		break;
	case SDLK_RETURN: // Parse typed Command
		codeViewData.inputStr[MAXCMDLEN] = '\0';
		if( ParseCommand( codeViewData.inputStr ) ) {
			char* cmd = ltrim( codeViewData.inputStr );
			if( histBuff.empty( ) || *--histBuff.end( ) != cmd ) {
				histBuff.emplace_back( cmd );
			}
			if( histBuff.size( ) > MAX_HIST_BUFFER ) {
				histBuff.pop_front( );
			}
			histBuffPos = histBuff.end( );
			//ClearInputLine( );
		}
		break;
	default:
		break;
	}
	if( ret < 0 ) {
		return ret;
	}
	if( ret > 0 ) {
		if( ret >= CB_MAX ) {
			ret = 0;
		} else {
			ret = ( *Callback_Handlers[ret] )( );
		}
		if( ret ) {
			exitLoop = true;
			CPU_Cycles = CPU_CycleLeft = 0;
			return ret;
		}
	}
	ret = 0;
	return ret;
}
#endif // C_DEBUGGER