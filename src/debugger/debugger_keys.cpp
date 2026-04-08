// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER

#include "breakpoint.h"
#include "cpu/paging.h"
#include "debugger.h"
#include "debugger_disasm.h"
#include "gui/common.h"
#include "hardware/pic.h"
#include "shell/shell.h"

extern bool forceDraw;

bool skipFirstInstruction = false;

static bool SetRedirectBreakpoint( ) {
	bool fResult = false;
	uint8_t size = 0U;
	auto address = GetAddress( SegValue( cs ), reg_eip );
	auto dline = DecodedLine::find( address );
	if( dline ) {
		size = dline->instruction.length;
		fResult = dline->mnemonicMask & ( MM_CALL | MM_INT | MM_LOOP | MM_REP ) ? true : false;
	} else {
		char instruction[128], *pOperands;
		size = DasmI386( instruction, pOperands, address, reg_eip, cpu.code.big, cpu.pmode );
		if( strstr( instruction, "call" ) || strstr( instruction, "int" ) ||
			strstr( instruction, "loop" ) || strstr( instruction, "rep" ) )
			fResult = true;
	}
	if( fResult ) {
		// Don't add a temporary breakpoint if there's already one here
		if( !CBreakpoint::FindPhysBreakpoint( SegValue( cs ), reg_eip + size, true ) )
			CBreakpoint::AddBreakpoint( SegValue( cs ), reg_eip + size, true );
	}
	return fResult;
}

static int32_t DEBUG_Run( int32_t amount, bool quickexit ) {
	DEBUG_SaveCurrentState( );
	skipFirstInstruction = true;
	CPU_CycleLeft += CPU_Cycles - amount;
	CPU_Cycles = amount;
	int32_t ret = ( *cpudecoder )( );
	if( quickexit )
		DEBUG_NewInstruction( );
	else {
		CBreakpoint::ActivateBreakpoints( ); // ensure all breakpoints are activated
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
		dataSeg[dbg.active_data_view] = RealSegValue( cs );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_eip;
		} else {
			dataOfs[dbg.active_data_view] = reg_ip;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_D: // ALT - D: DS:SI
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_data_view] = RealSegValue( ds );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_esi;
		} else {
			dataOfs[dbg.active_data_view] = reg_si;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_E: // ALT - E: es:di
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_data_view] = RealSegValue( es );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_edi;
		} else {
			dataOfs[dbg.active_data_view] = reg_di;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_X: // ALT - X: ds:dx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_data_view] = RealSegValue( ds );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_edx;
		} else {
			dataOfs[dbg.active_data_view] = reg_dx;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_B: // ALT -B: es:bx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_data_view] = RealSegValue( es );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_ebx;
		} else {
			dataOfs[dbg.active_data_view] = reg_bx;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_S: // ALT - S: ss:sp
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_data_view] = RealSegValue( ss );
		if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
			dataOfs[dbg.active_data_view] = reg_esp;
		} else {
			dataOfs[dbg.active_data_view] = reg_sp;
		}
		dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		break;
	case SDLK_F6: case SDLK_F7: case SDLK_F8: // Run to cursor
		// Don't add a temporary breakpoint if there's already one here
		if( !CBreakpoint::FindPhysBreakpoint( codeView.cursorSegment, codeView.cursorOffset, true ) )
			CBreakpoint::AddBreakpoint( codeView.cursorSegment, codeView.cursorOffset, true );
	case SDLK_F5: // Run Program
		debugging = false;
		forceDraw = true;
		ret = DEBUG_Run( 1, false );
		DEBUG_ShowDOSBox( );
		break;
	case SDLK_F9: { // Set/Remove Breakpoint. Hold SHIFT for permanent breakpoint
		const bool ftemp = ( key.mod & SDL_KMOD_SHIFT ) ? false : true;
		if( CBreakpoint::IsBreakpoint( codeView.cursorSegment, codeView.cursorOffset, ftemp ) ) {
			if( CBreakpoint::DeleteBreakpoint( codeView.cursorSegment, codeView.cursorOffset, ftemp ) )
				DEBUG_ShowMsg( "DEBUG: %sreakpoint deletion success.\n", ftemp ? "Temporary b" : "B" );
			else
				DEBUG_ShowMsg( "DEBUG: Failed to delete%sbreakpoint.\n", ftemp ? " temporary " : " " );
		} else {
			CBreakpoint::AddBreakpoint( codeView.cursorSegment, codeView.cursorOffset, ftemp );
			DEBUG_ShowMsg( "DEBUG: Set%sbreakpoint at %04X:%04X\n", ftemp ? " temporary " : " ", codeView.cursorSegment, codeView.cursorOffset );
		}
	}
		break;
	case SDLK_F11: // trace into
		if( ( key.mod & SDL_KMOD_SHIFT ) ) { // exit trace into
			debugging = false;
			ret = DEBUG_Run( 1, false );
			break;
		}
	case SDLK_F10: // Step over instruction
		if( SetRedirectBreakpoint( ) && key.key == SDLK_F10 ) {
			debugging = false;
			ret = DEBUG_Run( 1, false );
		} else
			ret = DEBUG_Run( 1, true );
		break;
	default:
		break;
	}
	if( ret > 0 ) {
		if( ret >= CB_MAX )
			ret = 0;
		else
			ret = ( *Callback_Handlers[ret] )( );
		if( ret ) {
			exitNormalLoop = true;
			CPU_Cycles = CPU_CycleLeft = 0;
		}
	}
	return ret;
}
#endif // C_DEBUGGER