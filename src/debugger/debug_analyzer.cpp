// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER
#include "cpu/callback.h"
#include "cpu/lazyflags.h"
#include "cpu/paging.h"
#include "debugger_disasm.h"
#include "debugvar.h"
#include "dos/programs.h"

constexpr uint8_t CASE_MASK = 0xDF;
static char empty_sel[] = { ' ', ' ', 0 };

bool GetDescriptorInfo( char *selname, char *out1, char *out2 ) {
	Bitu sel;
	Descriptor desc;
	bool fReg = true;

	if( *selname && ( selname[1] & CASE_MASK ) == 'S' ) {
		switch( *selname ) {
		case 'C':
			sel = SegValue( cs );
			break;
		case 'D':
			sel = SegValue( ds );
			break;
		case 'E':
			sel = SegValue( es );
			break;
		case 'F':
			sel = SegValue( fs );
			break;
		case 'G':
			sel = SegValue( gs );
			break;
		case 'S':
			sel = SegValue( ss );
			break;
		default:
			fReg = false;
		}
	} else
		fReg = false;
	if( !fReg ) {
		sel = GetHexValue( selname );
		if( *selname == 0 )
			selname = empty_sel;
	}
	if( cpu.gdt.GetDescriptor( sel, desc ) ) {
		switch( desc.Type( ) ) {
		case DESC_TASK_GATE:
			sprintf( out1, "%s: s:%08X type:%02X p", selname, desc.GetSelector( ), desc.saved.gate.type );
			sprintf( out2, "    TaskGate   dpl : %01X %1X", desc.saved.gate.dpl, desc.saved.gate.p );
			return true;
		case DESC_LDT:
		case DESC_286_TSS_A:
		case DESC_286_TSS_B:
		case DESC_386_TSS_A:
		case DESC_386_TSS_B:
			sprintf( out1, "%s: b:%08X type:%02X pag", selname, desc.GetBase( ), desc.saved.seg.type );
			sprintf( out2, "    l:%08X dpl : %01X %1X%1X%1X", desc.GetLimit( ), desc.saved.seg.dpl, desc.saved.seg.p, desc.saved.seg.avl, desc.saved.seg.g );
			return true;
		case DESC_286_CALL_GATE:
		case DESC_386_CALL_GATE:
			sprintf( out1, "%s: s:%08X type:%02X p params: %02X", selname, desc.GetSelector( ), desc.saved.gate.type, desc.saved.gate.paramcount );
			sprintf( out2, "    o:%08X dpl : %01X %1X", desc.GetOffset( ), desc.saved.gate.dpl, desc.saved.gate.p );
			return true;
		case DESC_286_INT_GATE:
		case DESC_286_TRAP_GATE:
		case DESC_386_INT_GATE:
		case DESC_386_TRAP_GATE:
			sprintf( out1, "%s: s:%08X type:%02X p", selname, desc.GetSelector( ), desc.saved.gate.type );
			sprintf( out2, "    o:%08X dpl : %01X %1X", desc.GetOffset( ), desc.saved.gate.dpl, desc.saved.gate.p );
			return true;
		}
		sprintf( out1, "%s: b:%08X type:%02X parbg", selname, desc.GetBase( ), desc.saved.seg.type );
		sprintf( out2, "    l:%08X dpl : %01X %1X%1X%1X%1X%1X", desc.GetLimit( ), desc.saved.seg.dpl, desc.saved.seg.p, desc.saved.seg.avl, desc.saved.seg.r, desc.saved.seg.big, desc.saved.seg.g );
		return true;
	} else {
		strcpy( out1, "                                     " );
		strcpy( out2, "                                     " );
	}
	return false;
}

uint16_t RealSegValue( const SegNames index ) {
	uint16_t seg_value = SegValue( index );
	if( ( cpu.pmode || seg_value < 32U ) && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( seg_value, desc ) )
			return desc.GetBase( ) >> 4;
	}
	return seg_value;
}

uint32_t GetAddress( uint16_t seg, uint32_t offset ) {
	if( seg == SegValue( cs ) )
		return SegPhys( cs ) + offset;
	if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( seg, desc ) )
			return desc.GetBase( ) + offset;
	}
	return ( seg << 4 ) + offset;
}

uint32_t GetHexValue( char *&hex ) {
	uint32_t value = 0U;
	uint32_t regval = 0U;
	bool fReg = true;
	bool extended = false;
	while( *hex == ' ' )
		++hex;
	
	if( ( *hex & CASE_MASK ) == 'E' ) {
		extended = true;
		++hex;
	}
	switch( ( *hex++ & CASE_MASK ) ) {
	case 'A':
		if( ( *hex & CASE_MASK ) == 'X' )
			regval = extended ? reg_eax : reg_ax;
		else if( ( *hex & CASE_MASK ) == 'H' )
			regval = reg_ah;
		else if( ( *hex & CASE_MASK ) == 'L' )
			regval = reg_al;
		else
			fReg = false;
		break;
	case 'B':
		if( ( *hex & CASE_MASK ) == 'X' )
			regval = extended ? reg_ebx : reg_bx;
		else if( ( *hex & CASE_MASK ) == 'H' )
			regval = reg_bh;
		else if( ( *hex & CASE_MASK ) == 'L' )
			regval = reg_bl;
		else if( ( *hex & CASE_MASK ) == 'P' )
			regval = extended ? reg_ebp : reg_bp;
		else
			fReg = false;
		break;
	case 'C':
		if( ( *hex & CASE_MASK ) == 'X' )
			regval = extended ? reg_ecx : reg_cx;
		else if( ( *hex & CASE_MASK ) == 'H' )
			regval = reg_ch;
		else if( ( *hex & CASE_MASK ) == 'L' )
			regval = reg_cl;
		else if( ( *hex & CASE_MASK ) == 'S' )
			regval = RealSegValue( cs );
		else
			fReg = false;
		break;
	case 'D':
		if( ( *hex & CASE_MASK ) == 'X' )
			regval = extended ? reg_edx : reg_dx;
		else if( ( *hex & CASE_MASK ) == 'H' )
			regval = reg_dh;
		else if( ( *hex & CASE_MASK ) == 'L' )
			regval = reg_dl;
		else if( ( *hex & CASE_MASK ) == 'S' )
			regval = RealSegValue( ds );
		else if( ( *hex & CASE_MASK ) == 'I' )
			regval = extended ? reg_edi : reg_di;
		else
			fReg = false;
		break;
	case 'F':
		if( ( *hex & CASE_MASK ) == 'S' )
			regval = RealSegValue( fs );
		else
			fReg = false;
		break;
	case 'G':
		if( ( *hex & CASE_MASK ) == 'S' )
			regval = RealSegValue( gs );
		else
			fReg = false;
		break;
	case 'S':
		if( ( *hex & CASE_MASK ) == 'I' )
			regval = extended ? reg_esi : reg_si;
		else if( ( *hex & CASE_MASK ) == 'P' )
			regval = extended ? reg_esp : reg_sp;
		else if( extended ) {
			regval = RealSegValue( es );
			--hex;
		} else if( ( *hex & CASE_MASK ) == 'S' )
			regval = RealSegValue( ss );
		else
			fReg = false;
		break;
	case 'I':
		if( ( *hex & CASE_MASK ) == 'P' )
			regval = extended ? reg_eip : reg_ip;
		else
			fReg = false;
		break;
	default:
		fReg = false;
	}
	if( fReg )
		++hex;
	else {
		--hex;
		if( extended )
			--hex;
		while( *hex ) {
			switch( *hex ) {
			case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
				value <<= 4U;
				value += *hex - 'A';
				value += 10U;
				break;
			case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
				value <<= 4U;
				value += *hex - 'a';
				value += 10U;
				break;
			case '0':
				if( ( hex[1] & CASE_MASK ) == 'X' ) { // Skip 0x
					++hex;
					break;
				}
			case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
				value <<= 4U;
				value += *hex - '0';
				break;
			case '+':
				++hex;
				return regval + value + GetHexValue( hex );
			case '-':
				++hex;
				return regval + value - GetHexValue( hex );
			default:
				return regval + value;
			}
			++hex;
		}
	}
	return regval + value;
}

static char * AnalyzeOperand( char *, char *, char *, bool = false );
static char * AnalyzeOperand( char *result, char *OPS, char *selector, bool RHS ) {
	char *pEnd = result;
	char *pos = strchr( OPS, '[' );
	if( pos ) {
		uint16_t segment;
		if( pos[-1] == ':' ) { // Segment prefix ?
			char *segpos = &pos[-3];
			if( cpu.pmode && selector ) {
				selector[0] = tolower( segpos[0] );
				selector[1] = tolower( segpos[1] );
				selector[2] = 0;
			}
			segment = static_cast<uint16_t>( GetHexValue( segpos ) );
		} else {
			if( strstr( pos, "SP" ) || strstr( pos, "BP" ) ) {
				segment = SegValue( ss );
				if( cpu.pmode && selector )
					strcpy( selector, "ss" );
			} else {
				segment = SegValue( ds );
				if( cpu.pmode && selector )
					strcpy( selector, "ds" );
			}
		}
		++pos;
		uint32_t offset = GetHexValue( pos );
		while( *pos != ']' ) {
			if( *pos == '+' ) {
				++pos;
				offset += GetHexValue( pos );
			} else if( *pos == '-' ) {
				++pos;
				offset -= GetHexValue( pos );
			} else
				++pos;
		}
		uint32_t address = GetAddress( segment, offset );
		if( !( get_tlb_readhandler( address )->flags & PFLAG_INIT ) ) {
			static char outmask[] = "%04X:%04X=%02X";
			if( cpu.pmode )
				outmask[7] = '8';
			uint32_t val = 0U;
			if( !RHS )
				outmask[9] = 0;
			else if( cpu.code.big ) {
				val = mem_readd<MemOpMode::SkipBreakpoints>( address );
				outmask[12] = '8';
			} else {
				val = mem_readw<MemOpMode::SkipBreakpoints>( address );
				outmask[12] = '4';
			}
			pEnd += sprintf( result, outmask, segment, offset, val );
			if( !RHS )
				outmask[9] = '=';
		} else
			pEnd += sprintf( result, "[illegal]" );

		CDebugVar *var = CDebugVar::FindVar( address ); // Variable found?
		if( var )
			pEnd += sprintf( pEnd, " (%s)", var->GetName( ) );
	} else {
		pos = strchr( OPS, ':' );
		if( pos ) {
			char *pLHS = pos;
			while( pLHS[-1] && pLHS[-1] != ' ' )
				--pLHS;
			++pos;
			uint32_t valueLHS = GetHexValue( pLHS );
			uint32_t valueRHS = GetHexValue( pos );
			pEnd += sprintf( result, "%04X:%04X", valueLHS, valueRHS );
		} else {
			uint32_t value = GetHexValue( OPS );
			switch( *OPS ) {
			case ' ':
			case ']':
			case ':':
			case ',':
			case 0:
				pEnd += sprintf( result, "%02X", value );
				break;
			}
		}
	}
	return pEnd;
}

const char * AnalyzeInstruction( const char *inst, const char *pOperands, char *selector ) {
	static char result[128];
	char *pEnd = result;
	char INST[16] = "", OPS[128] = "";
	if( selector )
		*selector = 0;

	*result = 0;
	if( pOperands ) {
		strncpy( INST, inst, pOperands - inst );
		INST[pOperands - inst - 1] = 0;
		upcase( INST );
		strcpy( OPS, pOperands );
		upcase( OPS );
		char *pos = strchr( OPS, ',' );
		if( pos ) {
			*pos++ = 0;
			if( *pos == ' ' )
				++pos;
			pEnd = AnalyzeOperand( result, OPS, selector );
			*pEnd++ = ',';
			*pEnd++ = ' ';
			pEnd = AnalyzeOperand( pEnd, pos, selector, true );
		} else
			pEnd = AnalyzeOperand( result, OPS, selector );
	} else {
		strcpy( INST, inst );
		upcase( INST );
	}
	char *pos = strstr( INST, "CALLBACK" );
	if( pos ) { // If it is a callback add additional info
		pos += 9;
		Bitu nr = GetHexValue( pos );
		const char *descr = CALLBACK_GetDescription( nr );
		if( descr )
			strcpy( result, descr );
	}
	switch( *INST ) {
	case 'J': { // Must be a jump
		bool jmp = false;
		switch( INST[1] ) {
		case 'A':
			jmp = ( get_CF( ) ? false : true ) && ( get_ZF( ) ? false : true ); // JA
			break;
		case 'B':
			if( INST[2] == 'E' )
				jmp = ( get_CF( ) ? true : false ) || ( get_ZF( ) ? true : false ); // JBE
			else
				jmp = get_CF( ) ? true : false; // JB
			break;
		case 'C':
			if( INST[2] == 'X' )
				jmp = reg_cx == 0; // JCXZ
			else
				jmp = get_CF( ) ? true : false; // JC
			break;
		case 'E':
			jmp = get_ZF( ) ? true : false; // JE
			break;
		case 'G':
			if( INST[2] == 'E' )
				jmp = ( get_SF( ) ? true : false ) == ( get_OF( ) ? true : false ); // JGE
			else
				jmp = ( get_ZF( ) ? false : true ) && ( ( get_SF( ) ? true : false ) == ( get_OF( ) ? true : false ) ); // JG
			break;
		case 'L':
			if( INST[2] == 'E' )
				jmp = ( get_ZF( ) ? true : false ) || ( ( get_SF( ) ? true : false ) != ( get_OF( ) ? true : false ) ); // JLE
			else
				jmp = ( get_SF( ) ? true : false ) != ( get_OF( ) ? true : false ); // JL
			break;
		case 'M':
			jmp = true; // JMP
			break;
		case 'N':
			switch( INST[2] ) {
			case 'B':
			case 'C':
				jmp = get_CF( ) ? false : true; // JNB / JNC
				break;
			case 'E':
				jmp = get_ZF( ) ? false : true; // JNE
				break;
			case 'O':
				jmp = get_OF( ) ? false : true; // JNO
				break;
			case 'P':
				jmp = get_PF( ) ? false : true; // JNP
				break;
			case 'S':
				jmp = get_SF( ) ? false : true; // JNS
				break;
			case 'Z':
				jmp = get_ZF( ) ? false : true; // JNZ
				break;
			}
			break;
		case 'O':
			jmp = get_OF( ) ? true : false; // JO
			break;
		case 'P':
			if( INST[2] == 'O' )
				jmp = get_PF( ) ? false : true; // JPO
			else
				jmp = get_SF( ) ? true : false; // JP / JPE
			break;
		case 'S':
			jmp = get_SF( ) ? true : false; // JS
			break;
		case 'Z':
			jmp = get_ZF( ) ? true : false; // JZ
			break;
		}
		if( jmp ) {
			pos = strchr( OPS, '$' );
			if( pos ) {
				pos = strchr( OPS, '+' );
				if( pos )
					strcpy( pEnd, " ->" );
				else
					strcpy( pEnd, " <-" );
			} else
				strcpy( pEnd, " <>" );
		} else
			strcpy( pEnd, " ><" );
	}
		break;
	case 'L':
		if( !strncmp( &INST[1], "ODS", 3U ) ) {
			switch( INST[4] ) {
			case 'B': {
				uint32_t address = GetAddress( SegValue( ds ), reg_si );
				uint8_t value = mem_readb<MemOpMode::SkipBreakpoints>( address );
				pEnd += sprintf( pEnd, "al <- %04X:%04X=%02X", RealSegValue( ds ), reg_si, value );
			}
				break;
			case 'D': {
				uint32_t address = GetAddress( SegValue( ds ), reg_esi );
				uint32_t value = mem_readd<MemOpMode::SkipBreakpoints>( address );
				pEnd += sprintf( pEnd, "eax <- %04X:%04X=%08X", RealSegValue( ds ), reg_esi, value );
			}
				break;
			default: {
				uint32_t address = GetAddress( SegValue( ds ), reg_si );
				uint16_t value = mem_readw<MemOpMode::SkipBreakpoints>( address );
				pEnd += sprintf( pEnd, "ax <- %04X:%04X=%04X", RealSegValue( ds ), reg_si, value );
			}
			}
		} else if( !strncmp( &INST[1], "OOP", 3U ) )
			sprintf( result, "%X", reg_cx );
		break;
	case 'P':
		if( INST[1] == 'O' && INST[2] == 'P' ) {
			uint32_t address = GetAddress( SegValue( ss ), reg_esp );
			uint16_t value = mem_readw<MemOpMode::SkipBreakpoints>( address );
			sprintf( pEnd, pEnd == result ? "%04X" : " -> %04X", value );
		} else if( !strncmp( &INST[1], "USHF", 4U ) )
			sprintf( pEnd, "%04X", reg_flags );
		break;
	case 'R':
		if( INST[1] == 'E' && INST[2] == 'P' ) {
			switch( *OPS ) {
			case 'L':
				if( !strncmp( &OPS[1], "ODS", 3U ) ) {
					bool f16bit = true;
					uint8_t multiplier = 1U;
					switch( OPS[4] ) {
					case 'B':
						pEnd += sprintf( pEnd, "al" );
						multiplier = 0U;
						break;
					case 'D':
						pEnd += sprintf( pEnd, "eax" );
						f16bit = false;
						break;
					default:
						pEnd += sprintf( pEnd, "ax" );
					}
					pEnd += sprintf( pEnd, " <- %04X:%04X-%04X", RealSegValue( ds ), f16bit ? reg_si : reg_esi, f16bit ? ( reg_si + ( reg_cx << multiplier ) ) : ( reg_esi + ( reg_ecx << 2U ) ) );
				}
				break;
			case 'S':
				if( !strncmp( &OPS[1], "TOS", 3U ) ) {
					bool f16bit = true;
					uint8_t multiplier = 1U;
					switch( OPS[4] ) {
					case 'B':
						pEnd += sprintf( pEnd, "%02X", reg_al );
						multiplier = 0U;
						break;
					case 'D':
						pEnd += sprintf( pEnd, "%08X", reg_eax );
						f16bit = false;
						break;
					default:
						pEnd += sprintf( pEnd, "%04X", reg_ax );
					}
					pEnd += sprintf( pEnd, " -> %04X:%04X-%04X", RealSegValue( es ), f16bit ? reg_di : reg_edi, f16bit ? ( reg_di + ( reg_cx << multiplier ) ) : ( reg_edi + ( reg_ecx << 2U ) ) );
				}
				break;
			}
		}
		break;
	}
	return result;
}
#endif // C_DEBUGGER