// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER

#include "cpu/registers.h"
#include "debug.h"
#include "shell/shell.h"

DEBUG* pDebugcom = nullptr;
// DEBUG.COM stuff
DEBUG::DEBUG( ) : active( false ) {
	pDebugcom = this;
}
DEBUG::~DEBUG( ) {
	pDebugcom = nullptr;
}

bool DEBUG::IsActive( ) const {
	return active;
}

void DEBUG::Run( ) {
	if( cmd->FindExist( "/NOMOUSE", false ) ) {
		real_writed( 0, 0x33 << 2, 0 );
		return;
	}

	uint16_t commandNr = 1;
	if( !cmd->FindCommand( commandNr++, temp_line ) ) {
		return;
	}
	// Get filename
	safe_strcpy( filename, temp_line.c_str( ) );
	// Setup commandline
	char args[256 + 1];
	args[0] = 0;
	bool found = cmd->FindCommand( commandNr++, temp_line );
	while( found ) {
		if( safe_strlen( args ) + temp_line.length( ) + 1 > 256 ) {
			break;
		}
		strcat( args, temp_line.c_str( ) );
		found = cmd->FindCommand( commandNr++, temp_line );
		if( found ) {
			strcat( args, " " );
		}
	}
	// Start new shell and execute prog
	active = true;
	// Save cpu state....
	uint16_t oldcs = SegValue( cs );
	uint32_t oldeip = reg_eip;
	uint16_t oldss = SegValue( ss );
	uint32_t oldesp = reg_esp;
	// Save interrupt vectors required by Debug
	RealPt   int_00 = RealGetVec( 0x00 ); // Program terminate
	RealPt   int_08 = RealGetVec( 0x08 ); // Console input without echo
	RealPt   int_09 = RealGetVec( 0x09 ); // Display string
	RealPt   int_0F = RealGetVec( 0x0F ); // Open file
	RealPt   int_21 = RealGetVec( 0x21 ); // Random read
	RealPt   int_3F = RealGetVec( 0x3F ); // Read file or device
	RealPt   int_66 = RealGetVec( 0x66 ); // Get or set code page

	// Start shell
	DOS_Shell shell;
	if( !shell.ExecuteProgram( filename, args ) ) {
		WriteOut( MSG_Get( "PROGRAM_EXECUTABLE_MISSING" ), filename );
	}
	// Restore saved interrupt vectors required by Debug
	RealSetVec( 0x66, int_66 );
	RealSetVec( 0x3F, int_3F );
	RealSetVec( 0x21, int_21 );
	RealSetVec( 0x0F, int_0F );
	RealSetVec( 0x09, int_09 );
	RealSetVec( 0x08, int_08 );
	RealSetVec( 0x00, int_00 );

	// set old reg values
	SegSet16( ss, oldss );
	reg_esp = oldesp;
	SegSet16( cs, oldcs );
	reg_eip = oldeip;
}
#endif // C_DEBUGGER
