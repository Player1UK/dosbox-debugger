// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER

#if C_HEAVY_DEBUGGER
#include <fstream>
#endif

#include "breakpoint.h"
#include "cpu/paging.h"
#include "debugvar.h"
#include "dos/dos.h"
#include "gui/common.h"
#include "hardware/pic.h"

// Heavy Debugging Vars for logging
#if C_HEAVY_DEBUGGER
extern std::ofstream cpuLogFile;
extern bool cpuLog;
extern int cpuLogCounter;
extern int cpuLogType;
extern bool zeroProtect;
extern bool logHeavy;
#endif

extern void SaveMemory( ADDRESS_PAIR &, uint32_t );
extern void SaveMemoryBin( ADDRESS_PAIR &, uint32_t );
extern void LogMCBS( void );
extern void LogGDT( void );
extern void LogLDT( void );
extern void LogIDT( void );
extern void LogPages( char * );
extern void LogCPUInfo( void );
extern void OutputVecTable( char * );

bool ChangeRegister( char *str ) {
	char *hex = str;
	while( *hex == ' ' ) {
		hex++;
	}
	if( strncmp( hex, "EAX", 3 ) == 0 ) {
		hex += 3;
		reg_eax = GetHexValue( hex );
	} else if( strncmp( hex, "EBX", 3 ) == 0 ) {
		hex += 3;
		reg_ebx = GetHexValue( hex );
	} else if( strncmp( hex, "ECX", 3 ) == 0 ) {
		hex += 3;
		reg_ecx = GetHexValue( hex );
	} else if( strncmp( hex, "EDX", 3 ) == 0 ) {
		hex += 3;
		reg_edx = GetHexValue( hex );
	} else if( strncmp( hex, "ESI", 3 ) == 0 ) {
		hex += 3;
		reg_esi = GetHexValue( hex );
	} else if( strncmp( hex, "EDI", 3 ) == 0 ) {
		hex += 3;
		reg_edi = GetHexValue( hex );
	} else if( strncmp( hex, "EBP", 3 ) == 0 ) {
		hex += 3;
		reg_ebp = GetHexValue( hex );
	} else if( strncmp( hex, "ESP", 3 ) == 0 ) {
		hex += 3;
		reg_esp = GetHexValue( hex );
	} else if( strncmp( hex, "EIP", 3 ) == 0 ) {
		hex += 3;
		reg_eip = GetHexValue( hex );
	} else if( strncmp( hex, "AX", 2 ) == 0 ) {
		hex += 2;
		reg_ax = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "BX", 2 ) == 0 ) {
		hex += 2;
		reg_bx = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "CX", 2 ) == 0 ) {
		hex += 2;
		reg_cx = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "DX", 2 ) == 0 ) {
		hex += 2;
		reg_dx = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "SI", 2 ) == 0 ) {
		hex += 2;
		reg_si = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "DI", 2 ) == 0 ) {
		hex += 2;
		reg_di = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "BP", 2 ) == 0 ) {
		hex += 2;
		reg_bp = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "SP", 2 ) == 0 ) {
		hex += 2;
		reg_sp = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "IP", 2 ) == 0 ) {
		hex += 2;
		reg_ip = static_cast<uint16_t>( GetHexValue( hex ) );
	} else if( strncmp( hex, "CS", 2 ) == 0 ) {
		hex += 2;
		SegSet16( cs, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "DS", 2 ) == 0 ) {
		hex += 2;
		SegSet16( ds, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "ES", 2 ) == 0 ) {
		hex += 2;
		SegSet16( es, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "FS", 2 ) == 0 ) {
		hex += 2;
		SegSet16( fs, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "GS", 2 ) == 0 ) {
		hex += 2;
		SegSet16( gs, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "SS", 2 ) == 0 ) {
		hex += 2;
		SegSet16( ss, static_cast<uint16_t>( GetHexValue( hex ) ) );
	} else if( strncmp( hex, "AF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( AF, GetHexValue( hex ) );
	} else if( strncmp( hex, "CF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( CF, GetHexValue( hex ) );
	} else if( strncmp( hex, "DF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( DF, GetHexValue( hex ) );
	} else if( strncmp( hex, "IF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( IF, GetHexValue( hex ) );
	} else if( strncmp( hex, "OF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( OF, GetHexValue( hex ) );
	} else if( strncmp( hex, "ZF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( ZF, GetHexValue( hex ) );
	} else if( strncmp( hex, "PF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( PF, GetHexValue( hex ) );
	} else if( strncmp( hex, "SF", 2 ) == 0 ) {
		hex += 2;
		SETFLAGBIT( SF, GetHexValue( hex ) );
	} else {
		return false;
	}
	return true;
}

static void DEBUG_RaiseTimerIrq( void ) {
	PIC_ActivateIRQ( 0 );
}

bool ParseCommand( char *str ) {
	char *found = str;
	for( char *idx = found; *idx != 0; ++idx )
		*idx = ascii_to_upper( *idx );

	found = trim( found );
	std::string s_found( found );
	std::istringstream stream( s_found );
	std::string command;
	stream >> command;
	std::string::size_type next = s_found.find_first_not_of( ' ', command.size( ) );
	if( next == std::string::npos ) {
		next = command.size( );
	}
	( s_found.erase )( 0, next );
	found = const_cast<char *>( s_found.c_str( ) );

	if( command == "MEMDUMP" ) { // Dump memory to file
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) };
		auto num = GetHexValue( ++found );
		SaveMemory( address_pair, num );
		return true;
	}

	if( command == "MEMDUMPBIN" ) { // Dump memory to file binary
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) };
		auto num = GetHexValue( ++found );
		SaveMemoryBin( address_pair, num );
		return true;
	}

	if( command == "IV" ) { // Insert variable
		// Do not truncate offset; IV must support 32-bit addresses like SV/LV.
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) };
		++found;
		char name[16];
		for( int i = 0; i < 16; ++i ) {
			if( found[i] && found[i] != ' ' )
				name[i] = found[i];
			else {
				name[i] = 0;
				break;
			}
		}
		name[15] = 0;

		if( !*name )
			return false;
		DEBUG_ShowMsg( "DEBUG: Created debug var %s at %04X:%04X\n", name, address_pair.segment, address_pair.offset );
		CDebugVar::InsertVariable( name, GetPhysicalAddress( address_pair ) );
		return true;
	}

	if( command == "SV" ) { // Save variables
		char name[13];
		for( int i = 0; i < 12; ++i ) {
			if( found[i] && found[i] != ' ' )
				name[i] = found[i];
			else {
				name[i] = 0;
				break;
			}
		}
		name[12] = 0;
		if( !*name )
			return false;
		DEBUG_ShowMsg( "DEBUG: Variable list save (%s) : %s.\n", name, ( CDebugVar::SaveVars( name ) ? "ok" : "failure" ) );
		return true;
	}

	if( command == "LV" ) { // load variables
		char name[13];
		for( int i = 0; i < 12; ++i ) {
			if( found[i] && found[i] != ' ' )
				name[i] = found[i];
			else {
				name[i] = 0;
				break;
			}
		}
		name[12] = 0;
		if( !*name )
			return false;
		DEBUG_ShowMsg( "DEBUG: Variable list load (%s) : %s.\n", name, ( CDebugVar::LoadVars( name ) ? "ok" : "failure" ) );
		return true;
	}

	if( command == "ADDLOG" ) {
		if( found && *found )
			DEBUG_ShowMsg( "NOTICE: %s\n", found );
		return true;
	}

	if( command == "SR" ) { // Set register value
		DEBUG_ShowMsg( "DEBUG: Set Register %s.\n", ( ChangeRegister( found ) ? "success" : "failure" ) );
		return true;
	}

	if( command == "SM" ) { // Set memory with following values
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		++found;
		while( *found ) {
			while( *found == ' ' )
				++found;
			if( *found ) {
				auto value = static_cast<uint8_t>( GetHexValue( found ) );
				if( *found )
					++found;
				mem_writeb_checked( GetPhysicalAddress( address_pair ), value );
				++address_pair.offset;
			}
		}
		DEBUG_ShowMsg( "DEBUG: Memory changed.\n" );
		return true;
	}

	if( command == "BP" ) { // Add new breakpoint
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		CBreakpoint::AddBreakpoint( address_pair, false );
		DEBUG_ShowMsg( "DEBUG: Set breakpoint at %04X:%04X\n", address_pair.segment, address_pair.offset );
		return true;
	}

#if C_HEAVY_DEBUGGER
	if( command == "BPM" ) { // Add new breakpoint
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		CBreakpoint::AddMemBreakpoint( address_pair );
		DEBUG_ShowMsg( "DEBUG: Set memory breakpoint at %04X:%04X\n", address_pair.segment, address_pair.offset );
		return true;
	}

	if( command == "BPMR" ) { // Add new breakpoint
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		CBreakpoint *bp = CBreakpoint::AddMemBreakpoint( address_pair );
		bp->SetType( BKPNT_MEMORY_READ );
		bp->FlagMemoryAsUnread( );
		DEBUG_ShowMsg( "DEBUG: Set memory read breakpoint at %04X:%04X\n", address_pair.segment, address_pair.offset );
		return true;
	}

	if( command == "BPPM" ) { // Add new breakpoint
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		CBreakpoint *bp = CBreakpoint::AddMemBreakpoint( address_pair );
		if( bp ) {
			bp->SetType( BKPNT_MEMORY_PROT );
			DEBUG_ShowMsg( "DEBUG: Set prot-mode memory breakpoint at %04X:%08X\n", address_pair.segment, address_pair.offset );
		}
		return true;
	}

	if( command == "BPLM" ) { // Add new breakpoint
		ADDRESS_PAIR address_pair = { 0U,  GetHexValue( found ) };
		CBreakpoint *bp = CBreakpoint::AddMemBreakpoint( address_pair );
		if( bp )
			bp->SetType( BKPNT_MEMORY_LINEAR );
		DEBUG_ShowMsg( "DEBUG: Set linear memory breakpoint at %08X\n", address_pair.offset );
		return true;
	}
#endif

	if( command == "BPINT" ) { // Add Interrupt Breakpoint
		auto intNr = static_cast<uint8_t>( GetHexValue( found ) );
		bool all = !( *found );
		auto valAH = static_cast<uint8_t>( GetHexValue( found ) );
		if( ( valAH == 0x00 ) && ( *found == '*' || all ) ) {
			CBreakpoint::AddIntBreakpoint( intNr, BPINT_ALL, BPINT_ALL, false );
			DEBUG_ShowMsg( "DEBUG: Set interrupt breakpoint at INT %02X\n",
				intNr );
		} else {
			all = !( *found );
			auto valAL = static_cast<uint8_t>( GetHexValue( found ) );
			if( ( valAL == 0x00 ) && ( *found == '*' || all ) ) {
				CBreakpoint::AddIntBreakpoint( intNr,
					valAH,
					BPINT_ALL,
					false );
				DEBUG_ShowMsg( "DEBUG: Set interrupt breakpoint at INT %02X AH=%02X\n",
					intNr,
					valAH );
			} else {
				CBreakpoint::AddIntBreakpoint( intNr, valAH, valAL, false );
				DEBUG_ShowMsg( "DEBUG: Set interrupt breakpoint at INT %02X AH=%02X AL=%02X\n",
					intNr,
					valAH,
					valAL );
			}
		}
		return true;
	}

	if( command == "BPLIST" ) {
		DEBUG_ShowMsg( "Breakpoint list:\n" );
		DEBUG_ShowMsg( "-------------------------------------------------------------------------\n" );
		CBreakpoint::ShowList( );
		return true;
	}

	if( command == "BPDEL" ) { // Delete Breakpoints
		auto bpNr = static_cast<uint8_t>( GetHexValue( found ) );
		if( ( bpNr == 0x00 ) && ( *found == '*' ) ) { // Delete all
			CBreakpoint::DeleteAll( );
			DEBUG_ShowMsg( "DEBUG: Breakpoints deleted.\n" );
		} else {
			// delete single breakpoint
			DEBUG_ShowMsg( "DEBUG: Breakpoint deletion %s.\n",
				( CBreakpoint::DeleteByIndex( bpNr ) ? "success"
					: "failure" ) );
		}
		return true;
	}

	if( command == "C" ) { // Set code overview
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		DEBUG_ShowMsg( "DEBUG: Set code overview to %04X:%04X\n", address_pair.segment, address_pair.offset );
		codeView.Set( address_pair, CV_UPDATE_ALL );
		return true;
	}

	if( command == "D" ) { // Set data overview
		ADDRESS_PAIR address_pair = { static_cast<uint16_t>( GetHexValue( found ) ),  GetHexValue( ++found ) }; // skip ":"
		if( address_pair.segment != dataAddress[dbg.active_data_view].segment || address_pair.offset > 0xFFFF )
			dbg.update_win[win_data_view[dbg.active_data_view]] = true;
		dbg.update_win_scroll[win_data_view[dbg.active_data_view]] = true;
		dataAddress[dbg.active_data_view] = address_pair;
		DEBUG_ShowMsg( "DEBUG: Set data overview to %04X:%04X\n", dataAddress[dbg.active_data_view].segment, dataAddress[dbg.active_data_view].offset );
		return true;
	}

#if C_HEAVY_DEBUGGER
	if( command == "LOG" ) { // Create Cpu normal log file
		cpuLogType = 1;
		command = "logcode";
	}
	if( command == "LOGS" ) { // Create Cpu short log file
		cpuLogType = 0;
		command = "logcode";
	}
	if( command == "LOGL" ) { // Create Cpu long log file
		cpuLogType = 2;
		command = "logcode";
	}
	if( command == "LOGC" ) { // Create Cpu coverage log file
		cpuLogType = 3;
		command = "logcode";
	}
	if( command == "logcode" ) { // Shared code between all logs
		DEBUG_ShowMsg( "DEBUG: Starting log\n" );
		const std_fs::path log_cpu_txt = "LOGCPU.TXT";
		cpuLogFile.open( log_cpu_txt.string( ) );
		if( !cpuLogFile.is_open( ) ) {
			DEBUG_ShowMsg( "DEBUG: Logfile couldn't be created.\n" );
			return false;
		}
		DEBUG_ShowMsg( "DEBUG: Logfile '%s' created.\n",
			std_fs::absolute( log_cpu_txt ).string( ).c_str( ) );
		// Initialize log object
		cpuLogFile << std::hex << std::noshowbase << std::setfill( '0' )
			<< std::uppercase;
		cpuLog = true;
		cpuLogCounter = GetHexValue( found );

		debugging = false;
		CBreakpoint::ActivateBreakpointsExceptAt( SegPhys( cs ) + reg_eip );
		DOSBOX_SetNormalLoop( );
		return true;
	}
#endif

	if( command == "INTT" ) { // trace int.
		auto intNr = static_cast<uint8_t>( GetHexValue( found ) );
		DEBUG_ShowMsg( "DEBUG: Tracing INT %02X\n", intNr );
		CPU_HW_Interrupt( intNr );
		codeView.SetToEIP( );
		return true;
	}

	if( command == "INT" ) { // start int.
		auto intNr = static_cast<uint8_t>( GetHexValue( found ) );
		DEBUG_ShowMsg( "DEBUG: Starting INT %02X\n", intNr );
		CBreakpoint::AddBreakpoint( { SegValue( cs ), reg_eip }, true );
		CBreakpoint::ActivateBreakpointsExceptAt( SegPhys( cs ) + reg_eip - 1 );
		debugging = false;
		DOSBOX_SetNormalLoop( );
		CPU_HW_Interrupt( intNr );
		return true;
	}

	if( command == "SELINFO" ) {
		while( found[0] == ' ' ) {
			found++;
		}
		char out1[200], out2[200];
		GetDescriptorInfo( found, out1, out2 );
		DEBUG_ShowMsg( "SelectorInfo %s:\n%s\n%s\n", found, out1, out2 );
		return true;
	}

	if( command == "DOS" ) {
		stream >> command;
		if( command == "MCBS" ) {
			LogMCBS( );
		}
		return true;
	}

	if( command == "GDT" ) {
		LogGDT( );
		return true;
	}

	if( command == "LDT" ) {
		LogLDT( );
		return true;
	}

	if( command == "IDT" ) {
		LogIDT( );
		return true;
	}

	if( command == "PAGING" ) {
		LogPages( found );
		return true;
	}

	if( command == "CPU" ) {
		LogCPUInfo( );
		return true;
	}

	if( command == "INTVEC" ) {
		if( found[0] != 0 ) {
			OutputVecTable( found );
			return true;
		}
	}

	if( command == "INTHAND" ) {
		if( found[0] != 0 ) {
			auto intNr = static_cast<uint8_t>( GetHexValue( found ) );
			DEBUG_ShowMsg( "DEBUG: Set code overview to interrupt handler %X\n", intNr );
			codeView.Set( { mem_readw( intNr * 4 + 2 ), mem_readw( intNr * 4 ) } );
			return true;
		}
	}

	if( command == "EXTEND" ) { // Toggle additional data.
		showExtend = !showExtend;
		return true;
	}

	if( command == "TIMERIRQ" ) { // Start a timer irq
		DEBUG_RaiseTimerIrq( );
		DEBUG_ShowMsg( "Debug: Timer Int started.\n" );
		return true;
	}

#if C_HEAVY_DEBUGGER
	if( command == "HEAVYLOG" ) { // Create Cpu log file
		logHeavy = !logHeavy;
		DEBUG_ShowMsg( "DEBUG: Heavy cpu logging %s.\n",
			logHeavy ? "on" : "off" );
		return true;
	}

	if( command == "ZEROPROTECT" ) { // toggle zero protection
		zeroProtect = !zeroProtect;
		DEBUG_ShowMsg( "DEBUG: Zero code execution protection %s.\n",
			zeroProtect ? "on" : "off" );
		return true;
	}

#endif
	if( command == "X" || command == "TERMINATE" ) {
		auto psp_seg = dos.psp( );
		uint32_t *dwPSP = reinterpret_cast<uint32_t *>( &MemBase[psp_seg << 4] );
		uint32_t *dwEnd = reinterpret_cast<uint32_t *>( &MemBase[RealSegValue( ss ) << 4] );
		for( uint32_t *dw = &dwPSP[0x40]; dw < dwEnd; ++dw ) // clear program memory
			*dw = 0U;
		DOS_Terminate( psp_seg, false, 2 );
		dwEnd = &dwPSP[0x40];
		for( uint32_t *dw = dwPSP; dw < dwEnd; ++dw ) // clear PSP
			*dw = 0U;
		reg_ip = 0;
		SegSet16( cs, dos.psp( ) + 0x10 );
		exitDebugLoop = true;
	}

	if( command == "?" || command == "HELP" ) {
		//DEBUG_ShowMsg("Debugger commands (enter all values in hex or as register):\n");
		DEBUG_ShowMsg( "Commands ----------------------------------------------------------------------\n" );
		DEBUG_ShowMsg( "BP     [segment]:[offset] - Set breakpoint.\n" );
		DEBUG_ShowMsg( "BPINT  [intNr] *          - Set interrupt breakpoint.\n" );
		DEBUG_ShowMsg( "BPINT  [intNr] [ah] *     - Set interrupt breakpoint with ah.\n" );
		DEBUG_ShowMsg( "BPINT  [intNr] [ah] [al]  - Set interrupt breakpoint with ah and al.\n" );
#if C_HEAVY_DEBUGGER
		DEBUG_ShowMsg( "BPM    [segment]:[offset] - Set memory breakpoint (memory change).\n" );
		DEBUG_ShowMsg( "BPMR   [segment]:[offset] - Set memory breakpoint (memory read).\n" );
		DEBUG_ShowMsg( "BPPM   [selector]:[offset]- Set pmode-memory breakpoint (memory change).\n" );
		DEBUG_ShowMsg( "BPLM   [linear address]   - Set linear memory breakpoint (memory change).\n" );
#endif
		DEBUG_ShowMsg( "BPLIST                    - List breakpoints.\n" );
		DEBUG_ShowMsg( "BPDEL  [bpNr] / *         - Delete breakpoint nr / all.\n" );
		DEBUG_ShowMsg( "C / D  [segment]:[offset] - Set code / data view address.\n" );
		DEBUG_ShowMsg( "DOS MCBS                  - Show Memory Control Block chain.\n" );
		DEBUG_ShowMsg( "INT [nr] / INTT [nr]      - Execute / Trace into interrupt.\n" );
#if C_HEAVY_DEBUGGER
		DEBUG_ShowMsg( "LOG [num]                 - Write cpu log file.\n" );
		DEBUG_ShowMsg( "LOGS/LOGL/LOGC [num]      - Write short/long/cs:ip-only cpu log file.\n" );
		DEBUG_ShowMsg( "HEAVYLOG                  - Enable/Disable automatic cpu log when DOSBox exits.\n" );
		DEBUG_ShowMsg( "ZEROPROTECT               - Enable/Disable zero code execution detection.\n" );
#endif
		DEBUG_ShowMsg( "SR [reg] [value]          - Set register value.\n" );
		DEBUG_ShowMsg( "SM [seg]:[off] [val] [.]..- Set memory with following values.\n" );

		DEBUG_ShowMsg( "IV [seg]:[off] [name]     - Create var name for memory address.\n" );
		DEBUG_ShowMsg( "SV [filename]             - Save var list in file.\n" );
		DEBUG_ShowMsg( "LV [filename]             - Load var list from file.\n" );

		DEBUG_ShowMsg( "ADDLOG [message]          - Add message to the log file.\n" );

		DEBUG_ShowMsg( "MEMDUMP [seg]:[off] [len] - Write memory to file memdump.txt.\n" );
		DEBUG_ShowMsg( "MEMDUMPBIN [s]:[o] [len]  - Write memory to file memdump.bin.\n" );
		DEBUG_ShowMsg( "SELINFO [segName]         - Show selector info.\n" );

		DEBUG_ShowMsg( "INTVEC [filename]         - Writes interrupt vector table to file.\n" );
		DEBUG_ShowMsg( "INTHAND [intNum]          - Set code view to interrupt handler.\n" );

		DEBUG_ShowMsg( "CPU                       - Display CPU status information.\n" );
		DEBUG_ShowMsg( "GDT                       - Lists descriptors of the GDT.\n" );
		DEBUG_ShowMsg( "LDT                       - Lists descriptors of the LDT.\n" );
		DEBUG_ShowMsg( "IDT                       - Lists descriptors of the IDT.\n" );
		DEBUG_ShowMsg( "PAGING [page]             - Display content of page table.\n" );
		DEBUG_ShowMsg( "EXTEND                    - Toggle additional info.\n" );
		DEBUG_ShowMsg( "TIMERIRQ                  - Run the system timer.\n" );

		DEBUG_ShowMsg( "TERMINATE / X             - Terminate the current executable.\n" );

		//DEBUG_ShowMsg("HELP                      - Help\n");
		DEBUG_ShowMsg( "Keys --------------------------------------------------------------------------\n" );
		DEBUG_ShowMsg( "[SHIFT +] F1              - Expand [previous/]next code line.\n" );
		DEBUG_ShowMsg( "F5                        - Run.\n" );
		DEBUG_ShowMsg( "F6/F7/F8                  - Run to cursor.\n" );
		DEBUG_ShowMsg( "F9                        - Set/Remove breakpoint.\n" );
		DEBUG_ShowMsg( "F10/F11                   - Step over / trace into instruction.\n" );
		DEBUG_ShowMsg( "ALT + C/D/E/S/X/B         - Set data to CS:IP/DS:SI/ES:DI/SS:SP/DS:DX/ES:BX.\n" );
		DEBUG_ShowMsg( "Escape                    - Clear input line.\n" );

		return true;
	}
	return false;
}
#endif // C_DEBUGGER