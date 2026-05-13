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
#include "hardware/timer.h"
#include "shell/shell.h"

extern bool forceDraw;

bool skipFirstInstruction = false;

static bool SetRedirectBreakpoint( ) {
	bool fResult = false;
	uint8_t size = 0U;
	auto address = GetPhysicalAddress( { SegValue( cs ), reg_eip } );
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
		ADDRESS_PAIR address_pair = { SegValue( cs ), reg_eip + size };
		if( !CBreakpoint::FindPhysBreakpoint( address_pair, true ) )
			CBreakpoint::AddBreakpoint( address_pair, true );
	}
	return fResult;
}

int32_t DEBUG_Run( const RUN_TYPE run_type, const ADDRESS_PAIR &breakpoint_address ) {
	bool quickexit = false;
	switch( run_type ) {
	case RUN_TO_TBP:
		if( !CBreakpoint::FindPhysBreakpoint( breakpoint_address, true ) ) // Don't add a temporary breakpoint if there's already one here
			CBreakpoint::AddBreakpoint( breakpoint_address, true );
		break;
	case RUN_STEP:
		SetRedirectBreakpoint( );
		quickexit = true;
		break;
	case RUN_STEP_OVER:
		quickexit = !SetRedirectBreakpoint( );
		break;
	default:
		break;
	}
	DEBUG_SaveCurrentState( );
	skipFirstInstruction = true;
	CPU_CycleLeft += CPU_Cycles - 1;
	CPU_Cycles = 1;
	int32_t ret = ( *cpudecoder )( );
	if( quickexit )
		DEBUG_NewInstruction( );
	else {
		debugging = false;
		CBreakpoint::ActivateBreakpoints( ); // ensure all breakpoints are activated
		normalLoopTickCount = GetTicks( );
		DOSBOX_SetNormalLoop( );
		forceDraw = true;
		if( RUN_FOREVER == run_type )
			DEBUG_ShowDOSBox( );
	}
	return ret;
}

uint32_t DEBUG_ProcessKey( SDL_KeyboardEvent key ) {
	Bits ret = 0;

	switch( key.key ) {
	case SDLK_C: // ALT+C: CS:IP
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( cs ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_eip : reg_ip }, V_UPDATE_ALL );
		break;
	case SDLK_D: // ALT+D: DS:SI
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( ds ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_esi : reg_si }, V_UPDATE_ALL );
		break;
	case SDLK_E: // ALT+E: es:di
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( es ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_edi : reg_di }, V_UPDATE_ALL );
		break;
	case SDLK_X: // ALT+X: ds:dx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( ds ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_edx : reg_dx }, V_UPDATE_ALL );
		break;
	case SDLK_B: // ALT+B: es:bx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( es ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_ebx : reg_bx }, V_UPDATE_ALL );
		break;
	case SDLK_S: // ALT+S: ss:sp
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataView.Set( { RealSegValue( ss ), cpu.pmode && !( reg_flags & FLAG_VM ) ? reg_esp : reg_sp }, V_UPDATE_ALL );
		break;
	case SDLK_F1:
		if( key.mod & SDL_KMOD_SHIFT )
			DasmUnDisassemble( codeView.cursorAddress );
		else
			codeView.Set( codeView.cursorRealAddress, V_UPDATE_VIEW );
		break;
	case SDLK_F6: case SDLK_F7: case SDLK_F8: // Run to cursor
		ret = DEBUG_Run( RUN_TO_TBP, codeView.cursorRealAddress );
		break;
	case SDLK_F5: // Run Program
		ret = DEBUG_Run( RUN_FOREVER );
		break;
	case SDLK_F9: { // Set/Remove Breakpoint. Hold SHIFT for permanent breakpoint
		const bool ftemp = ( key.mod & SDL_KMOD_SHIFT ) ? true : false;
		if( CBreakpoint::IsBreakpoint( codeView.cursorRealAddress, ftemp ) ) {
			if( CBreakpoint::DeleteBreakpoint( codeView.cursorRealAddress, ftemp ) )
				DEBUG_ShowMsg( "DEBUG: %sreakpoint deletion success.\n", ftemp ? "Temporary b" : "B" );
			else
				DEBUG_ShowMsg( "DEBUG: Failed to delete%sbreakpoint.\n", ftemp ? " temporary " : " " );
		} else {
			CBreakpoint::AddBreakpoint( codeView.cursorRealAddress, ftemp );
			DEBUG_ShowMsg( "DEBUG: Set%sbreakpoint at %04X:%04X\n", ftemp ? " temporary " : " ", codeView.cursorRealAddress.segment, codeView.cursorRealAddress.offset );
		}
	}
		break;
	case SDLK_F11: // trace into
		if( key.mod & SDL_KMOD_SHIFT ) // exit trace into
			ret = DEBUG_Run( RUN_OUT );
		else
			ret = DEBUG_Run( RUN_STEP );
		break;
	case SDLK_F10: // Step over instruction
		ret = DEBUG_Run( RUN_STEP_OVER );
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