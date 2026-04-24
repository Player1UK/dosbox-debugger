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
	if( ( cpu.pmode || seg_value < dbg.segment[SEG_ENV] ) && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( seg_value, desc ) )
			return desc.GetBase( ) >> 4;
	}
	return seg_value;
}

uint32_t GetPhysicalAddress( const ADDRESS_PAIR &address_pair ) {
	if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( address_pair.segment, desc ) )
			return desc.GetBase( ) + address_pair.offset;
	}
	return address_pair.address( );
}

void ResetHexValueSizeType( ) {
	hex_value_size_type = SIZE_BYTE;
}

uint32_t GetHexValue( char *&hex, SIZE_TYPE &size_type ) {
	while( *hex == ' ' )
		++hex;
	switch( *hex ) {
	case ']':
	case ':':
	case ',':
	case 0:
		return 0U;
	}
	bool fReg = true;
	bool extended = false;
	uint32_t reg_value = 0U;
	SIZE_TYPE reg_size_type = SIZE_WORD;
	if( ( *hex & CASE_MASK ) == 'E' ) {
		reg_size_type = SIZE_DWORD;
		extended = true;
		++hex;
	}
	switch( ( *hex++ & CASE_MASK ) ) {
	case 'A':
		if( ( *hex & CASE_MASK ) == 'X' )
			reg_value = extended ? reg_eax : reg_ax;
		else if( ( *hex & CASE_MASK ) == 'H' ) {
			reg_value = reg_ah; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'L' ) {
			reg_value = reg_al; reg_size_type = SIZE_BYTE;
		} else
			fReg = false;
		break;
	case 'B':
		if( ( *hex & CASE_MASK ) == 'X' )
			reg_value = extended ? reg_ebx : reg_bx;
		else if( ( *hex & CASE_MASK ) == 'H' ) {
			reg_value = reg_bh; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'L' ) {
			reg_value = reg_bl; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'P' )
			reg_value = extended ? reg_ebp : reg_bp;
		else
			fReg = false;
		break;
	case 'C':
		if( ( *hex & CASE_MASK ) == 'X' )
			reg_value = extended ? reg_ecx : reg_cx;
		else if( ( *hex & CASE_MASK ) == 'H' ) {
			reg_value = reg_ch; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'L' ) {
			reg_value = reg_cl; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'S' )
			reg_value = RealSegValue( cs );
		else
			fReg = false;
		break;
	case 'D':
		if( ( *hex & CASE_MASK ) == 'X' )
			reg_value = extended ? reg_edx : reg_dx;
		else if( ( *hex & CASE_MASK ) == 'H' ) {
			reg_value = reg_dh; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'L' ) {
			reg_value = reg_dl; reg_size_type = SIZE_BYTE;
		} else if( ( *hex & CASE_MASK ) == 'S' )
			reg_value = RealSegValue( ds );
		else if( ( *hex & CASE_MASK ) == 'I' )
			reg_value = extended ? reg_edi : reg_di;
		else
			fReg = false;
		break;
	case 'F':
		if( ( *hex & CASE_MASK ) == 'S' )
			reg_value = RealSegValue( fs );
		else
			fReg = false;
		break;
	case 'G':
		if( ( *hex & CASE_MASK ) == 'S' )
			reg_value = RealSegValue( gs );
		else
			fReg = false;
		break;
	case 'S':
		if( ( *hex & CASE_MASK ) == 'I' )
			reg_value = extended ? reg_esi : reg_si;
		else if( ( *hex & CASE_MASK ) == 'P' )
			reg_value = extended ? reg_esp : reg_sp;
		else if( extended ) {
			reg_value = RealSegValue( es );
			reg_size_type = SIZE_WORD;
			--hex;
		} else if( ( *hex & CASE_MASK ) == 'S' )
			reg_value = RealSegValue( ss );
		else
			fReg = false;
		break;
	case 'I':
		if( ( *hex & CASE_MASK ) == 'P' )
			reg_value = extended ? reg_eip : reg_ip;
		else
			fReg = false;
		break;
	default:
		fReg = false;
	}
	if( fReg ) {
		while( *++hex == ' ' );
		if( reg_size_type > size_type )
			size_type = reg_size_type;
	} else {
		--hex;
		if( extended )
			--hex;
	}
	uint32_t value = 0U;
	uint8_t count = 0U;
	bool fDecodeHex = true;
	while( *hex && fDecodeHex ) {
		switch( *hex ) {
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
			value <<= 4U;
			value += *hex - 'A';
			value += 10U;
			++hex;
			++count;
			break;
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
			value <<= 4U;
			value += *hex - 'a';
			value += 10U;
			++hex;
			++count;
			break;
		case '0':
			if( ( hex[1] & CASE_MASK ) == 'X' ) { // Skip 0x
				++hex;
				++hex;
				break;
			}
		case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			value <<= 4U;
			value += *hex - '0';
			++hex;
			++count;
			break;
		case 'h': case 'H':
			if( count )
				++hex;
		default:
			fDecodeHex = false;
		}
	}
	if( count ) {
		SIZE_TYPE value_size_type = count > 4U ? SIZE_DWORD : count > 2U ? SIZE_WORD : SIZE_BYTE;
		if( value_size_type > size_type )
			size_type = value_size_type;
		while( *hex == ' ' )
			++hex;
	}
	if( *hex == '+' ) {
		++hex;
		value += GetHexValue( hex, size_type );
	} else if( *hex == '-' ) {
		++hex;
		value -= GetHexValue( hex, size_type );
	}
	hex_value = reg_value + value;
	return hex_value;
}

typedef enum Operand_Position : uint8_t {
	OP_NONE		= 1u,
	OP_LEFT		= 2u,
	OP_RIGHT	= 4u
} OPERAND_POSITION;

static PTR_TYPE last_ptr_type = PTR_NONE;
static char * AnalyzeOperand( char *, char *, char *, OPERAND_POSITION = OP_NONE );
static char * AnalyzeOperand( char *result, char *OPS, char *selector, OPERAND_POSITION position ) {
	if( position != OP_RIGHT )
		last_ptr_type = PTR_NONE;
	char *pEnd = result;
	char *pos = strchr( OPS, '[' );
	if( pos ) {
		PTR_TYPE ptr_type = PTR_WORD;
		switch( *OPS ) {
		case 'B':
			ptr_type = PTR_BYTE;
			break;
		case 'W':
			ptr_type = PTR_WORD;
			break;
		case 'D':
			ptr_type = PTR_DWORD;
			break;
		case 'P':
			ptr_type = PTR;
			break;
		}
		if( position != OP_RIGHT )
			last_ptr_type = ptr_type;
		ADDRESS_PAIR address_pair;
		if( pos[-1] == ':' ) { // Segment prefix ?
			char *segpos = &pos[-3];
			if( cpu.pmode && selector ) {
				selector[0] = tolower( segpos[0] );
				selector[1] = tolower( segpos[1] );
				selector[2] = 0;
			}
			address_pair.segment = static_cast<uint16_t>( GetHexValue( segpos ) );
		} else {
			if( strstr( pos, "SP" ) || strstr( pos, "BP" ) ) {
				address_pair.segment = SegValue( ss );
				if( cpu.pmode && selector )
					strcpy( selector, "ss" );
			} else {
				address_pair.segment = SegValue( ds );
				if( cpu.pmode && selector )
					strcpy( selector, "ds" );
			}
		}
		++pos;
		address_pair.offset = GetHexValue( pos );
		while( *pos != ']' ) {
			if( *pos == '+' ) {
				++pos;
				address_pair.offset += GetHexValue( pos );
			} else if( *pos == '-' ) {
				++pos;
				address_pair.offset -= GetHexValue( pos );
			} else
				++pos;
		}
		uint32_t address = GetPhysicalAddress( address_pair );
		if( !( get_tlb_readhandler( address )->flags & PFLAG_INIT ) ) {
			pEnd += sprintf( result, "%04X:%04X", address_pair.segment, address_pair.offset );
			if( position != OP_LEFT ) {
				switch( ptr_type ) {
				case PTR_BYTE:
					pEnd += sprintf( pEnd, "=%02X", mem_readb<MemOpMode::SkipBreakpoints>( address ) );
					break;
				case PTR_WORD:
					pEnd += sprintf( pEnd, "=%04X", mem_readw<MemOpMode::SkipBreakpoints>( address ) );
					break;
				case PTR_DWORD:
					pEnd += sprintf( pEnd, "=%08X", mem_readd<MemOpMode::SkipBreakpoints>( address ) );
					break;
				case PTR:
					pEnd += sprintf( pEnd, "=%04X:%04X", mem_readw<MemOpMode::SkipBreakpoints>( address + 2U ), mem_readw<MemOpMode::SkipBreakpoints>( address ) );
					break;
				default:
					break;
				}
			}
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
			ResetHexValueSizeType( );
			uint32_t value = GetHexValue( OPS );
			switch( *OPS ) {
			case ' ':
			case 'h':
			case ']':
			case ':':
			case ',':
			case 0:
				if( hex_value_size_type == SIZE_DWORD || ( position == OP_RIGHT && last_ptr_type == PTR_DWORD ) )
					pEnd += sprintf( result, "%08X", value );
				else if( hex_value_size_type == SIZE_WORD || ( position == OP_RIGHT && last_ptr_type == PTR_WORD ) )
					pEnd += sprintf( result, "%04X", value );
				else
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
	if( pOperands && *pOperands ) {
		const char *cpos = strchr( inst, ' ' );
		if( cpos ) {
			strncpy( INST, inst, cpos - inst );
			INST[cpos - inst] = 0;
		}  else
			strcpy( INST, inst );
		upcase( INST );
		strcpy( OPS, pOperands );
		upcase( OPS );
		char *pos = strchr( OPS, ',' );
		if( pos ) {
			*pos++ = 0;
			if( *pos == ' ' )
				++pos;
			pEnd = AnalyzeOperand( result, OPS, selector, OP_LEFT );
			*pEnd++ = ',';
			*pEnd++ = ' ';
			pEnd = AnalyzeOperand( pEnd, pos, selector, OP_RIGHT );
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
				if( pos[1] == '+' || pos[2] == '+' )
					strcpy( pEnd, " ->" );
				else
					strcpy( pEnd, " <-" );
			} else if( hex_value > reg_ip )
				strcpy( pEnd, " ->" );
			else
				strcpy( pEnd, " <-" );
		} else
			strcpy( pEnd, " ><" );
	}
		break;
	case 'L':
		if( !strncmp( &INST[1], "OOP", 3U ) )
			sprintf( result, "%X", reg_cx );
		break;
	case 'P':
		if( INST[1] == 'O' && INST[2] == 'P' ) {
			uint32_t address = GetPhysicalAddress( { SegValue( ss ), reg_esp } );
			if( pEnd == result ) {
				uint16_t value = mem_readw<MemOpMode::SkipBreakpoints>( address );
				sprintf( pEnd, "%04X", value );
			} else if( hex_value_size_type == SIZE_BYTE ) {
				uint8_t value = mem_readb<MemOpMode::SkipBreakpoints>( address );
				sprintf( pEnd, " -> %02X", value );
			} else if( hex_value_size_type == SIZE_WORD ) {
				uint16_t value = mem_readw<MemOpMode::SkipBreakpoints>( address );
				sprintf( pEnd, " -> %04X", value );
			} else {
				uint32_t value = mem_readd<MemOpMode::SkipBreakpoints>( address );
				sprintf( pEnd, " -> %08X", value );
			}
		} else if( !strncmp( &INST[1], "USHF", 4U ) )
			sprintf( pEnd, "%04X", reg_flags );
		break;
	}
	return result;
}
#endif // C_DEBUGGER