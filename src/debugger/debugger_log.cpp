// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER

#if C_HEAVY_DEBUGGER
#include <fstream>
#endif

#include "breakpoint.h"
#include "cpu/lazyflags.h"
#include "cpu/paging.h"
#include "debugger_disasm.h"
#include "dos/programs.h"

extern bool skipFirstInstruction;

bool showExtend = false;

struct _LogGroup {
	const char *front = nullptr;
	bool enabled = false;
};

static _LogGroup loggrp[LOG_MAX] = {
		{     "",  true},
		{nullptr, false}
};
FILE *debuglog = nullptr;

void LOG::operator()( const char *format, ... ) {
	char buf[DBGUI::MsgBufferSize];
	va_list msg;
	va_start( msg, format );
	vsnprintf( buf, sizeof( buf ), format, msg );
	va_end( msg );

	if( d_type >= LOG_MAX ) {
		return;
	}
	if( ( d_severity != LOG_ERROR ) && ( !loggrp[d_type].enabled ) ) {
		return;
	}
	DEBUG_ShowMsg( "%10u: %s:%s\n",
		static_cast<uint32_t>( cycle_count ),
		loggrp[d_type].front,
		buf );
}

void LOG_Init( ) {
	auto section = get_section( "log" );
	assert( section );

	std::string logfile = section->GetString( "logfile" );

	if( !logfile.empty( ) && ( debuglog = fopen( logfile.c_str( ), "wt+" ) ) ) {
		;
	} else {
		debuglog = nullptr;
	}

	char buf[DBGUI::LogNameBufferSize];

	// Skip LOG_ALL, it is always enabled
	for( Bitu i = LOG_ALL + 1; i < LOG_MAX; i++ ) {
		safe_strcpy( buf, loggrp[i].front );
		lowcase( buf );
		loggrp[i].enabled = section->GetBool( buf );
	}
}

void LOG_Destroy( ) {
	if( debuglog ) {
		fclose( debuglog );
	}
	debuglog = nullptr;
}

void LOG_StartUp( ) {
	// Setup logging groups
	loggrp[LOG_ALL].front = "ALL";
	loggrp[LOG_VGA].front = "VGA";
	loggrp[LOG_VGAGFX].front = "VGAGFX";
	loggrp[LOG_VGAMISC].front = "VGAMISC";
	loggrp[LOG_INT10].front = "INT10";
	loggrp[LOG_SB].front = "SBLASTER";
	loggrp[LOG_DMACONTROL].front = "DMA_CONTROL";

	loggrp[LOG_FPU].front = "FPU";
	loggrp[LOG_CPU].front = "CPU";
	loggrp[LOG_PAGING].front = "PAGING";

	loggrp[LOG_FCB].front = "FCB";
	loggrp[LOG_FILES].front = "FILES";
	loggrp[LOG_IOCTL].front = "IOCTL";
	loggrp[LOG_EXEC].front = "EXEC";
	loggrp[LOG_DOSMISC].front = "DOSMISC";

	loggrp[LOG_PIT].front = "PIT";
	loggrp[LOG_KEYBOARD].front = "KEYBOARD";
	loggrp[LOG_PIC].front = "PIC";

	loggrp[LOG_MOUSE].front = "MOUSE";
	loggrp[LOG_BIOS].front = "BIOS";
	loggrp[LOG_GUI].front = "GUI";
	loggrp[LOG_MISC].front = "MISC";

	loggrp[LOG_IO].front = "IO";
	loggrp[LOG_PCI].front = "PCI";
	loggrp[LOG_REELMAGIC].front = "REELMAGIC";

	// Register the log section
	auto sect = control->AddSection( "log" );

	PropString *pstring = sect->AddString( "logfile",
		Property::Changeable::Always,
		"" );

	pstring->SetHelp( "Path of the log file." );

	char buf[DBGUI::LogNameBufferSize];
	for( Bitu i = LOG_ALL + 1; i < LOG_MAX; i++ ) {
		safe_strcpy( buf, loggrp[i].front );
		lowcase( buf );
		PropBool *pbool = sect->AddBool( buf, Property::Changeable::Always, true );
		pbool->SetHelp( "Enable/disable logging of this type." );
	}
}

// Display the content of the MCB chain starting with the MCB at the specified
// segment.
static void LogMCBChain( uint16_t mcb_segment ) {
	DOS_MCB mcb( mcb_segment );
	char filename[9]; // 8 characters plus a terminating NUL
	const char *psp_seg_note;
	auto realOffset = static_cast<uint16_t>( dataAddress[dbg.active_data_view].offset ); // Realmode addressing only
	PhysPt dataAddr = PhysicalMake( dataAddress[dbg.active_data_view].segment, realOffset ); // location being viewed in the "Data Overview"

	// loop forever, breaking out of the loop once we've processed the last MCB
	while( true ) {
		if( mcb.GetType( ) != 0x4d && mcb.GetType( ) != 0x5a ) { // verify that the type field is valid
			LOG( LOG_MISC, LOG_ERROR )
				( "MCB chain broken at %04X:0000!", mcb_segment );
			return;
		}
		mcb.GetFileName( filename );

		// some PSP segment values have special meanings
		switch( mcb.GetPSPSeg( ) ) {
		case MCB_FREE: psp_seg_note = "(free)"; break;
		case MCB_DOS: psp_seg_note = "(DOS)"; break;
		default: psp_seg_note = "";
		}
		LOG( LOG_MISC, LOG_ERROR )
			( "   %04X  %12u     %04X %-7s  %s", mcb_segment, mcb.GetSize( ) << 4, mcb.GetPSPSeg( ), psp_seg_note, filename );

		// print a message if dataAddr is within this MCB's memory range
		PhysPt mcbStartAddr = PhysicalMake( mcb_segment + 1, 0 );
		PhysPt mcbEndAddr = PhysicalMake( mcb_segment + 1 + mcb.GetSize( ), 0 );
		if( dataAddr >= mcbStartAddr && dataAddr < mcbEndAddr ) {
			LOG( LOG_MISC, LOG_ERROR )
				( "   (data addr %04hX:%04X is %u bytes past this MCB)", dataAddress[dbg.active_data_view].segment, realOffset, dataAddr - mcbStartAddr );
		}
		// if we've just processed the last MCB in the chain, break out of the loop
		if( mcb.GetType( ) == 0x5a )
			break;
		// else, move to the next MCB in the chain
		mcb_segment += mcb.GetSize( ) + 1;
		mcb.SetPt( mcb_segment );
	}
}

// Display the content of all Memory Control Blocks.
void LogMCBS( void ) {
	LOG( LOG_MISC, LOG_ERROR )
		( "MCB Seg  Size (bytes)  PSP Seg (notes)  Filename" );
	LOG( LOG_MISC, LOG_ERROR )( "Conventional memory:" );
	LogMCBChain( dos.firstMCB );

	LOG( LOG_MISC, LOG_ERROR )( "Upper memory:" );
	LogMCBChain( dos_infoblock.GetStartOfUMBChain( ) );
}

void LogGDT( void ) {
	char out1[512];
	Descriptor desc;
	Bitu length = cpu.gdt.GetLimit( );
	PhysPt address = cpu.gdt.GetBase( );
	PhysPt max = address + length;
	Bitu i = 0;
	LOG( LOG_MISC, LOG_ERROR )
		( "GDT Base:%08X Limit:%08" sBitfs( X ), address, length );
	while( address < max ) {
		desc.Load( address );
		sprintf( out1,
			"%04" sBitfs( X ) ": b:%08X type: %02X parbg",
			( i << 3 ),
			desc.GetBase( ),
			desc.saved.seg.type );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
		sprintf( out1,
			"      l:%08X dpl : %01X  %1X%1X%1X%1X%1X",
			desc.GetLimit( ),
			desc.saved.seg.dpl,
			desc.saved.seg.p,
			desc.saved.seg.avl,
			desc.saved.seg.r,
			desc.saved.seg.big,
			desc.saved.seg.g );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
		address += 8;
		i++;
	}
}

void LogLDT( void ) {
	char out1[512];
	Descriptor desc;
	Bitu ldtSelector = cpu.gdt.SLDT( );
	if( !cpu.gdt.GetDescriptor( ldtSelector, desc ) ) {
		return;
	}
	Bitu length = desc.GetLimit( );
	PhysPt address = desc.GetBase( );
	PhysPt max = address + length;
	Bitu i = 0;
	LOG( LOG_MISC, LOG_ERROR )
		( "LDT Base:%08X Limit:%08" sBitfs( X ), address, length );
	while( address < max ) {
		desc.Load( address );
		sprintf( out1,
			"%04" sBitfs( X ) ": b:%08X type: %02X parbg",
			( i << 3 ) | 4,
			desc.GetBase( ),
			desc.saved.seg.type );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
		sprintf( out1,
			"      l:%08X dpl : %01X  %1X%1X%1X%1X%1X",
			desc.GetLimit( ),
			desc.saved.seg.dpl,
			desc.saved.seg.p,
			desc.saved.seg.avl,
			desc.saved.seg.r,
			desc.saved.seg.big,
			desc.saved.seg.g );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
		address += 8;
		i++;
	}
}

void LogIDT( void ) {
	char out1[512];
	Descriptor desc;
	uint32_t address = 0;
	while( address < 256 * 8 ) {
		if( cpu.idt.GetDescriptor( address, desc ) ) {
			sprintf( out1,
				"%04X: sel:%04X off:%02X",
				address / 8,
				desc.GetSelector( ),
				desc.GetOffset( ) );
			LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
		}
		address += 8;
	}
}

void LogPages( char *selname ) {
	char out1[512];
	if( paging.enabled ) {
		Bitu sel = GetHexValue( selname );
		if( ( sel == 0x00 ) && ( ( *selname == 0 ) || ( *selname == '*' ) ) ) {
			for( int i = 0; i < 0xfffff; i++ ) {
				Bitu table_addr = ( paging.base.page << 12 ) +
					( i >> 10 ) * 4;
				X86PageEntry table;
				table.set( phys_readd( table_addr ) );
				if( table.p ) {
					X86PageEntry entry;
					Bitu entry_addr = ( table.base << 12 ) +
						( i & 0x3ff ) * 4;
					entry.set( phys_readd( entry_addr ) );
					if( entry.p ) {
						sprintf( out1,
							"page %05Xxxx -> %04Xxxx  flags [uw] %x:%x::%x:%x [d=%x|a=%x]",
							i,
							entry.base,
							entry.us,
							table.us,
							entry.wr,
							table.wr,
							entry.d,
							entry.a );
						LOG( LOG_MISC, LOG_ERROR )
							( "%s", out1 );
					}
				}
			}
		} else {
			Bitu table_addr = ( paging.base.page << 12 ) + ( sel >> 10 ) * 4;
			X86PageEntry table;
			table.set( phys_readd( table_addr ) );
			if( table.p ) {
				X86PageEntry entry;
				Bitu entry_addr = ( table.base << 12 ) +
					( sel & 0x3ff ) * 4;
				entry.set( phys_readd( entry_addr ) );
				sprintf( out1,
					"page %05" sBitfs( X ) "xxx -> %04Xxxx  flags [puw] %x:%x::%x:%x::%x:%x",
					sel,
					entry.base,
					entry.p,
					table.p,
					entry.us,
					table.us,
					entry.wr,
					table.wr );
				LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
			} else {
				sprintf( out1,
					"pagetable %03" sBitfs( X ) " not present, flags [puw] %x::%x::%x",
					( sel >> 10 ),
					table.p,
					table.us,
					table.wr );
				LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
			}
		}
	}
}

void LogCPUInfo( void ) {
	char out1[512];
	sprintf( out1,
		"cr0:%08" sBitfs( X ) " cr2:%08u cr3:%08u  cpl=%" sBitfs( x ),
		cpu.cr0,
		paging.cr2,
		paging.cr3,
		cpu.cpl );
	LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
	sprintf( out1,
		"eflags:%08x [vm=%x iopl=%x nt=%x]",
		reg_flags,
		GETFLAG( VM ) >> 17,
		GETFLAG( IOPL ) >> 12,
		GETFLAG( NT ) >> 14 );
	LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
	sprintf( out1,
		"GDT base=%08X limit=%08" sBitfs( X ),
		cpu.gdt.GetBase( ),
		cpu.gdt.GetLimit( ) );
	LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
	sprintf( out1,
		"IDT base=%08X limit=%08" sBitfs( X ),
		cpu.idt.GetBase( ),
		cpu.idt.GetLimit( ) );
	LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );

	Bitu sel = CPU_STR( );
	Descriptor desc;
	if( cpu.gdt.GetDescriptor( sel, desc ) ) {
		sprintf( out1,
			"TR selector=%04" sBitfs( X ) ", base=%08X limit=%08X*%X",
			sel,
			desc.GetBase( ),
			desc.GetLimit( ),
			desc.saved.seg.g ? 0x4000 : 1 );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
	}
	sel = CPU_SLDT( );
	if( cpu.gdt.GetDescriptor( sel, desc ) ) {
		sprintf( out1,
			"LDT selector=%04" sBitfs( X ) ", base=%08X limit=%08X*%X",
			sel,
			desc.GetBase( ),
			desc.GetLimit( ),
			desc.saved.seg.g ? 0x4000 : 1 );
		LOG( LOG_MISC, LOG_ERROR )( "%s", out1 );
	}
}

#if C_HEAVY_DEBUGGER
// Heavy Debugging Vars for logging
std::ofstream cpuLogFile;
bool cpuLog = false;
int cpuLogCounter = 0;
int cpuLogType = 1; // log detail
bool zeroProtect = false;
bool logHeavy = false;
extern std::list<CBreakpoint *> BPoints;

static void LogInstruction( const ADDRESS_PAIR &address_pair, std::ofstream &out ) {
	static char empty[23] = { 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
							 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 0 };
	using std::setw;
	if( cpuLogType == 3 ) { // Log only cs:ip.
		out << setw( 4 ) << SegValue( cs ) << ":" << setw( 8 ) << reg_eip << std::endl;
		return;
	}
	char dline[128], *pOperands;
	uint32_t start = GetPhysicalAddress( address_pair );
	uint32_t size = DasmI386( dline, pOperands, start, reg_eip, cpu.code.big, cpu.pmode );
	char *res = empty;
	if( showExtend && ( cpuLogType > 0 ) ) {
		res = const_cast<char *>( AnalyzeInstruction( dline, pOperands ) );
		if( !res || !( *res ) )
			res = empty;
		Bitu reslen = strlen( res );
		if( reslen < 22 )
			memset( res + reslen, ' ', 22 - reslen );
		res[22] = 0;
	}
	Bitu len = safe_strlen( dline );
	if( len < 30 )
		memset( dline + len, ' ', 30 - len );
	dline[30] = 0;

	// Get register values
	if( cpuLogType == 0 )
		out << setw( 4 ) << SegValue( cs ) << ":" << setw( 4 ) << reg_eip << "  " << dline;
	else if( cpuLogType == 1 )
		out << setw( 4 ) << SegValue( cs ) << ":" << setw( 8 ) << reg_eip << "  " << dline << "  " << res;
	else if( cpuLogType == 2 ) {
		char ibytes[200] = "";
		char tmpc[200];
		for( Bitu i = 0; i < size; ++i ) {
			uint8_t value;
			if( mem_readb_checked( start + i, &value ) )
				sprintf( tmpc, "%s", "?? " );
			else
				sprintf( tmpc, "%02X ", value );
			strcat( ibytes, tmpc );
		}
		len = safe_strlen( ibytes );
		if( len < 21 ) {
			for( Bitu i = 0; i < 21 - len; ++i )
				ibytes[len + i] = ' ';
			ibytes[21] = 0;
		} // NOTE THE BRACKETS
		out << setw( 4 ) << SegValue( cs ) << ":" << setw( 8 ) << reg_eip << "  " << dline << "  " << res << "  " << ibytes;
	}

	out << " EAX:" << setw( 8 ) << reg_eax << " EBX:" << setw( 8 ) << reg_ebx
		<< " ECX:" << setw( 8 ) << reg_ecx << " EDX:" << setw( 8 ) << reg_edx
		<< " ESI:" << setw( 8 ) << reg_esi << " EDI:" << setw( 8 ) << reg_edi
		<< " EBP:" << setw( 8 ) << reg_ebp << " ESP:" << setw( 8 ) << reg_esp
		<< " DS:" << setw( 4 ) << SegValue( ds ) << " ES:" << setw( 4 ) << SegValue( es );

	if( cpuLogType == 0 ) {
		out << " SS:" << setw( 4 ) << SegValue( ss ) << " C" << ( get_CF( ) > 0 )
			<< " Z" << ( get_ZF( ) > 0 ) << " S" << ( get_SF( ) > 0 ) << " O"
			<< ( get_OF( ) > 0 ) << " I" << GETFLAGBOOL( IF );
	} else {
		out << " FS:" << setw( 4 ) << SegValue( fs ) << " GS:" << setw( 4 )
			<< SegValue( gs ) << " SS:" << setw( 4 ) << SegValue( ss )
			<< " CF:" << ( get_CF( ) > 0 ) << " ZF:" << ( get_ZF( ) > 0 )
			<< " SF:" << ( get_SF( ) > 0 ) << " OF:" << ( get_OF( ) > 0 )
			<< " AF:" << ( get_AF( ) > 0 ) << " PF:" << ( get_PF( ) > 0 )
			<< " IF:" << GETFLAGBOOL( IF );
	}
	if( cpuLogType == 2 ) {
		out << " TF:" << GETFLAGBOOL( TF ) << " VM:" << GETFLAGBOOL( VM )
			<< " FLG:" << setw( 8 ) << reg_flags << " CR0:" << setw( 8 ) << cpu.cr0;
	}
	out << std::endl;
}
#endif

void SaveMemory( ADDRESS_PAIR &address_pair, uint32_t num ) {
	const std_fs::path memdump_txt = "MEMDUMP.TXT";
	FILE *f = fopen( memdump_txt.string( ).c_str( ), "wt" );
	if( !f ) {
		DEBUG_ShowMsg( "DEBUG: Memory dump failed.\n" );
		return;
	}
	DEBUG_ShowMsg( "DEBUG: Memory dump file '%s' created.\n", std_fs::absolute( memdump_txt ).string( ).c_str( ) );

	while( num ) {
		char buffer[65];
		auto bufferPos = buffer;
		bufferPos += sprintf_s( buffer, sizeof( buffer ), "%04X:%04X   ", address_pair.segment, address_pair.offset );
		for( uint8_t count = 16U; count && num; --count, --num, ++address_pair.offset ) {
			uint8_t value;
			if( mem_readb_checked( GetPhysicalAddress( address_pair ), &value ) )
				bufferPos += sprintf_s( bufferPos, sizeof( buffer ) - ( bufferPos - buffer ), "%s", "?? " );
			else
				bufferPos += sprintf_s( bufferPos, sizeof( buffer ) - ( bufferPos - buffer ), "%02X ", value );
		}
		fprintf( f, "%s\n", buffer );
	}
	fclose( f );
	DEBUG_ShowMsg( "DEBUG: Memory dump success.\n" );
}

void SaveMemoryBin( ADDRESS_PAIR &address_pair, uint32_t num ) {
	const std_fs::path memdump_bin = "MEMDUMP.BIN";
	FILE *f = fopen( memdump_bin.string( ).c_str( ), "wb" );
	if( !f ) {
		DEBUG_ShowMsg( "DEBUG: Memory binary dump failed.\n" );
		return;
	}
	DEBUG_ShowMsg( "DEBUG: Memory binary dump file '%s' created.\n", std_fs::absolute( memdump_bin ).string( ).c_str( ) );

	for( ; num; --num, ++address_pair.offset ) {
		uint8_t val;
		if( mem_readb_checked( GetPhysicalAddress( address_pair ), &val ) )
			val = 0U;
		fwrite( &val, 1, 1, f );
	}
	fclose( f );
	DEBUG_ShowMsg( "DEBUG: Memory dump binary success.\n" );
}

void OutputVecTable( char *filename ) {
	const std_fs::path vec_table_file = filename;
	FILE *f = fopen( vec_table_file.string( ).c_str( ), "wt" );
	if( !f ) {
		DEBUG_ShowMsg( "DEBUG: Output of interrupt vector table failed.\n" );
		return;
	}
	DEBUG_ShowMsg( "DEBUG: Interrupt vector table file '%s' created.\n",
		std_fs::absolute( vec_table_file ).string( ).c_str( ) );

	for( int i = 0; i < 256; i++ ) {
		fprintf( f,
			"INT %02X:  %04X:%04X\n",
			i,
			mem_readw( i * 4 + 2 ),
			mem_readw( i * 4 ) );
	}

	fclose( f );
	DEBUG_ShowMsg( "DEBUG: Interrupt vector table written to %s.\n",
		vec_table_file.string( ).c_str( ) );
}

bool DEBUG_Breakpoint( void ) {
	/* First get the physical address and check for a set Breakpoint */
	if( !CBreakpoint::CheckBreakpoint( { SegValue( cs ), reg_eip } ) ) {
		return false;
	}
	// Found. Breakpoint is valid
	// PhysPt where=GetPhysicalAddress( {SegValue(cs),reg_eip} ); -- "where" is unused
	CBreakpoint::DeactivateBreakpoints( ); // Deactivate all breakpoints
	return true;
}

bool DEBUG_IntBreakpoint( uint8_t intNum ) {
	/* First get the physical address and check for a set Breakpoint */
	PhysPt where = GetPhysicalAddress( { SegValue( cs ), reg_eip } );
	if( !CBreakpoint::CheckIntBreakpoint( where, intNum, reg_ah, reg_al ) )
		return false;
	// Found. Breakpoint is valid
	CBreakpoint::DeactivateBreakpoints( ); // Deactivate all breakpoints
	return true;
}

#if C_HEAVY_DEBUGGER
const uint32_t LOGCPUMAX = 20000;

static uint32_t logCount = 0;

struct TLogInst {
	uint16_t s_cs;
	uint32_t eip;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ebp;
	uint32_t esp;
	uint16_t s_ds;
	uint16_t s_es;
	uint16_t s_fs;
	uint16_t s_gs;
	uint16_t s_ss;
	bool c;
	bool z;
	bool s;
	bool o;
	bool a;
	bool p;
	bool i;
	char dline[31];
	char res[23];
};

TLogInst logInst[LOGCPUMAX];

void DEBUG_HeavyLogInstruction( ) {
	static char empty[23] = { 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
							 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 0 };

	char dline[128], *pOperands;
	DasmI386( dline, pOperands, GetPhysicalAddress( { SegValue( cs ), reg_eip } ), reg_eip, cpu.code.big, cpu.pmode );
	char *res = empty;
	if( showExtend ) {
		res = const_cast<char *>( AnalyzeInstruction( dline, pOperands ) );
		if( !res || !( *res ) )
			res = empty;
		Bitu reslen = strlen( res );
		if( reslen < 22 )
			memset( res + reslen, ' ', 22 - reslen );
		res[22] = 0;
	}

	Bitu len = safe_strlen( dline );
	if( len < 30 )
		memset( dline + len, ' ', 30 - len );
	dline[30] = 0;

	TLogInst &inst = logInst[logCount];
	strcpy( inst.dline, dline );
	inst.s_cs = SegValue( cs );
	inst.eip = reg_eip;
	strcpy( inst.res, res );
	inst.eax = reg_eax;
	inst.ebx = reg_ebx;
	inst.ecx = reg_ecx;
	inst.edx = reg_edx;
	inst.esi = reg_esi;
	inst.edi = reg_edi;
	inst.ebp = reg_ebp;
	inst.esp = reg_esp;
	inst.s_ds = SegValue( ds );
	inst.s_es = SegValue( es );
	inst.s_fs = SegValue( fs );
	inst.s_gs = SegValue( gs );
	inst.s_ss = SegValue( ss );
	inst.c = get_CF( ) > 0;
	inst.z = get_ZF( ) > 0;
	inst.s = get_SF( ) > 0;
	inst.o = get_OF( ) > 0;
	inst.a = get_AF( ) > 0;
	inst.p = get_PF( ) > 0;
	inst.i = GETFLAGBOOL( IF );

	if( ++logCount >= LOGCPUMAX )
		logCount = 0;
}

void DEBUG_HeavyWriteLogInstruction( ) {
	if( !logHeavy )
		return;
	logHeavy = false;

	DEBUG_ShowMsg( "DEBUG: Creating cpu log LOGCPU_INT_CD.TXT\n" );

	std::ofstream out( "LOGCPU_INT_CD.TXT" );
	if( !out.is_open( ) ) {
		DEBUG_ShowMsg( "DEBUG: Failed.\n" );
		return;
	}
	out << std::hex << std::noshowbase << std::setfill( '0' ) << std::uppercase;
	uint32_t startLog = logCount;
	do {
		// Write Instructions
		TLogInst &inst = logInst[startLog];
		using std::setw;
		out << setw( 4 ) << inst.s_cs << ":" << setw( 8 ) << inst.eip << "  "
			<< inst.dline << "  " << inst.res << " EAX:" << setw( 8 )
			<< inst.eax << " EBX:" << setw( 8 ) << inst.ebx
			<< " ECX:" << setw( 8 ) << inst.ecx << " EDX:" << setw( 8 )
			<< inst.edx << " ESI:" << setw( 8 ) << inst.esi
			<< " EDI:" << setw( 8 ) << inst.edi << " EBP:" << setw( 8 )
			<< inst.ebp << " ESP:" << setw( 8 ) << inst.esp
			<< " DS:" << setw( 4 ) << inst.s_ds << " ES:" << setw( 4 )
			<< inst.s_es << " FS:" << setw( 4 ) << inst.s_fs
			<< " GS:" << setw( 4 ) << inst.s_gs << " SS:" << setw( 4 )
			<< inst.s_ss << " CF:" << inst.c << " ZF:" << inst.z
			<< " SF:" << inst.s << " OF:" << inst.o << " AF:" << inst.a
			<< " PF:" << inst.p << " IF:" << inst.i << std::endl;

		/*		fprintf(f,"%04X:%08X   %s  %s  EAX:%08X EBX:%08X
		   ECX:%08X EDX:%08X ESI:%08X EDI:%08X EBP:%08X ESP:%08X DS:%04X
		   ES:%04X FS:%04X GS:%04X SS:%04X CF:%01X ZF:%01X SF:%01X
		   OF:%01X AF:%01X PF:%01X IF:%01X\n",
								logInst[startLog].s_cs,logInst[startLog].eip,logInst[startLog].dline,logInst[startLog].res,logInst[startLog].eax,logInst[startLog].ebx,logInst[startLog].ecx,logInst[startLog].edx,logInst[startLog].esi,logInst[startLog].edi,logInst[startLog].ebp,logInst[startLog].esp,
								logInst[startLog].s_ds,logInst[startLog].s_es,logInst[startLog].s_fs,logInst[startLog].s_gs,logInst[startLog].s_ss,
								logInst[startLog].c,logInst[startLog].z,logInst[startLog].s,logInst[startLog].o,logInst[startLog].a,logInst[startLog].p,logInst[startLog].i);*/
		if( ++startLog >= LOGCPUMAX )
			startLog = 0;
	} while( startLog != logCount );

	out.close( );
	DEBUG_ShowMsg( "DEBUG: Done.\n" );
}

bool DEBUG_HeavyIsBreakpoint( void ) {
	static Bitu zero_count = 0;
	if( cpuLog ) {
		if( cpuLogCounter > 0 ) {
			LogInstruction( { SegValue( cs ), reg_eip }, cpuLogFile );
			cpuLogCounter--;
		}
		if( cpuLogCounter <= 0 ) {
			cpuLogFile.flush( );
			cpuLogFile.close( );
			DEBUG_ShowMsg( "DEBUG: cpu log LOGCPU.TXT created\n" );
			cpuLog = false;
			DEBUG_EnableDebugger( );
			return true;
		}
	}
	// LogInstruction
	if( logHeavy )
		DEBUG_HeavyLogInstruction( );
	if( zeroProtect ) {
		uint32_t value = 0;
		if( !mem_readd_checked( SegPhys( cs ) + reg_eip, &value ) ) {
			if( value == 0 )
				++zero_count;
			else
				zero_count = 0;
		}
		if( zero_count == 10 )
			E_Exit( "running zeroed code" );
	}

	if( skipFirstInstruction ) {
		skipFirstInstruction = false;
		return false;
	}
	if( BPoints.size( ) && CBreakpoint::CheckBreakpoint( { SegValue( cs ), reg_eip }  ) )
		return true;
	return false;
}

template <typename T>
void DEBUG_UpdateMemoryReadBreakpoints( const PhysPt addr ) {
	static_assert( std::is_unsigned_v<T> );
	static_assert( std::is_integral_v<T> );

	for( CBreakpoint *bp : BPoints ) {
		if( bp->GetType( ) == BKPNT_MEMORY_READ ) {
			const PhysPt location_begin = bp->GetLocation( );
			const PhysPt location_end = location_begin + sizeof( T );
			if( ( addr >= location_begin ) && ( addr < location_end ) ) {
				DEBUG_ShowMsg( "bpmr hit: %04X:%04X, cs:ip = %04X:%04X",
					bp->GetSegment( ),
					bp->GetOffset( ),
					SegValue( cs ),
					reg_eip );
				bp->FlagMemoryAsRead( );
			}
		}
	}
}
// Explicit instantiations
template void DEBUG_UpdateMemoryReadBreakpoints<uint8_t>( const PhysPt addr );
template void DEBUG_UpdateMemoryReadBreakpoints<uint16_t>( const PhysPt addr );
template void DEBUG_UpdateMemoryReadBreakpoints<uint32_t>( const PhysPt addr );
template void DEBUG_UpdateMemoryReadBreakpoints<uint64_t>( const PhysPt addr );
#endif //C_HEAVY_DEBUGGER

#endif // C_DEBUGGER