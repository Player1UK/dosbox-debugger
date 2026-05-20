#include "debugger_disasm.h"

#if C_DEBUGGER
#include "cpu/callback.h"
#include "cpu/cpu.h"
#include "cpu/lazyflags.h"
#include "hardware/memory.h"
#include <vector>
#include <unordered_set>

#include <fstream>

#include "Zydis/Utils.h"

// Custom hash specialization for Pair<T1, T2>
namespace std {
    template <typename T1, typename T2>
    struct hash<Pair<T1, T2>> {
        std::size_t operator()( const Pair<T1, T2> &p ) const noexcept {
            return std::hash<T1>{}( p.value );
        }
    };
}

static uint16_t data_segment = 0U;
std::set<Pair<uint16_t, SegmentInfo>> ordered_segments;
std::set<Pair<uint32_t, LabelInfo>> labels;
std::set<Pair<uint32_t, uint32_t>> calls;

static std::set<Pair<uint32_t, DecodedLine>> ordered_code;
static std::unordered_set<uint32_t> visited;

static std::set<Pair<uint32_t, DecodedLine>>::iterator currentLine = ordered_code.end( );

const DecodedLine & operator++( DecodedLine const &source ) { // Prefix increment
    if( currentLine != ordered_code.end( ) && ++currentLine != ordered_code.end( ) )
        const_cast<DecodedLine &>( source ) = currentLine->extra;
    else
        const_cast<DecodedLine &>( source ) = ordered_code.begin( )->extra;
    return source;
}
const DecodedLine operator++( DecodedLine const &source, int ) { // Postfix increment
    const DecodedLine original = source;
    ++source;
    return original;
}

const DecodedLine &operator--( DecodedLine const &source ) { // Prefix decrement
    if( currentLine != ordered_code.begin( ) )
        const_cast<DecodedLine &>( source ) = (--currentLine)->extra;
    return source;
}
const DecodedLine operator--( DecodedLine const &source, int ) { // Postfix decrement
    const DecodedLine original = source;
    --source;
    return original;
}

const DecodedLine & DecodedLine::first( ) {
    currentLine = ordered_code.begin( );
    return currentLine->extra;
}
const DecodedLine & DecodedLine::last( ) {
    currentLine = ordered_code.end( );
    --currentLine;
    return currentLine->extra;
}

const DecodedLine * DecodedLine::find( const uint32_t address ) {
    const auto it = ordered_code.find( { address, {} } );
    return ( it != ordered_code.end( ) ) ? &it->extra : nullptr;
}
const DecodedLine * DecodedLine::find( const ADDRESS_PAIR &address_pair ) {
    return find( address_pair.offset + ( address_pair.segment << 4 ) );
}

bool DecodedLine::isStart( ) {
    return currentLine == ordered_code.begin( );
}
bool DecodedLine::isEnd( ) {
    return currentLine == ordered_code.end( );
}
bool DecodedLine::isEmpty( ) {
    return ordered_code.empty( );
}

bool AddressVisited( uint32_t address ) {
    return visited.contains( address );
}

bool AddressVisited( const ADDRESS_PAIR &address_pair ) {
    return AddressVisited( address_pair.offset + ( address_pair.segment << 4 ) );
}

bool CallerLabelRealAddress( const uint32_t address, ADDRESS_PAIR &realAddress ) {
    const auto call = calls.find( { address, {} } );
    if( call != calls.end( ) ) {
        std::set<Pair<uint32_t, uint16_t>> callers;
        auto label = labels.find( { call->extra, { {}, {}, {}, callers } } );
        if( label != labels.end( ) ) {
            realAddress = ADDRESS_PAIR::RealAddress( label->value, label->extra.segment );
            return true;
        }
    }
    return false;
}

uint8_t DasmI386( char *decodedInstruction, char *&pOperands, const uint32_t address, const uint32_t ip_offset, const bool f32bit, const bool fProtected ) {
    pOperands = nullptr;
	ZydisDisassembledInstruction instruction;
	if( ZYAN_SUCCESS( ZydisDisassembleIntel(
		( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
        ip_offset, MemBase + address, ZYDIS_MAX_OPERAND_COUNT + 1, &instruction ) ) ) {
		strcpy( decodedInstruction, instruction.text );
        pOperands = decodedInstruction;
        while( *pOperands && *pOperands != ' ' ) ++pOperands;
	} else { // invalid instruction, use db xx
		sprintf( decodedInstruction, "db %02X", MemBase[address] );
		return 1U;
	}
	return instruction.info.length;
}

static uint32_t lastProcessedCount = 0U;
static uint32_t segment0_phys = 0U;
static uint32_t binarySize = static_cast<uint32_t>( -1 );

void DasmReset( ) {
    calls.clear( );
    labels.clear( );
    ordered_segments.clear( );
    visited.clear( );
    ordered_code.clear( );
    currentLine = ordered_code.end( );
    lastProcessedCount = 0U;
    segment0_phys = 0U;
    binarySize = static_cast<uint32_t>( -1 );
}

static bool ValidateAddress( uint32_t &address, const ADDRESS_PAIR &ptr ) {
    if( address < segment0_phys || address >= binarySize ) {
        Descriptor desc;
        if( cpu.gdt.GetDescriptor( ptr.segment, desc ) )
            address = ptr.offset + ( desc.GetBase( ) << 4 );
        if( address < segment0_phys || address >= binarySize )
            return false;
    }
    return true;
}

static ZydisDecodedInstruction data_instruction = {
    ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_MNEMONIC_INVALID, // machine_mode, mnemonic
    2U,                                 // length
    ZYDIS_INSTRUCTION_ENCODING_LEGACY, ZYDIS_OPCODE_MAP_DEFAULT, // encoding, opcode_map
    0x00, 0U, 0U, 0U, 1U, 1U, 0U, nullptr, nullptr, { }, { }, { } // opcode, stack_width, operand_width, address_width, operand_count, operand_count_visible, attributes, cpu_flags, fpu_flags, avx, meta, raw
};
static ZydisDecodedInstruction callback_instruction = {
    ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_MNEMONIC_INVALID, // machine_mode, mnemonic
    4U,                                 // length
    ZYDIS_INSTRUCTION_ENCODING_LEGACY, ZYDIS_OPCODE_MAP_DEFAULT, // encoding, opcode_map
    0xFE,                               // opcode
    0U,                                 // stack_width
    2U,                                 // operand_width
    4U,                                 // address_width
    1U,                                 // operand_count
    1U,                                 // operand_count_visible
    0U,                                 // attributes
    nullptr, nullptr, { }, { }, { } // cpu_flags, fpu_flags, avx, meta, raw
};
static ZydisDecodedOperand imm_operand = {
    0U, ZYDIS_OPERAND_VISIBILITY_EXPLICIT, // id, visibility
    0U, ZYDIS_OPERAND_ENCODING_UIMM16,  // actions, encoding
    16U, ZYDIS_ELEMENT_TYPE_UINT,       // size, element_type
    2U,                                 // element_size
    1U,                                 // element_count
    0U,                                 // attributes
    ZYDIS_OPERAND_TYPE_IMMEDIATE, { }   // type
};

static csh cs_handle = 0U;
static ZydisDecoder decoder;
static ZydisFormatter formatter;

static void RemoveLabel( const Pair<uint32_t, LabelInfo> &label ) {
    auto it = ordered_code.find( { label.value, {} } );
    if( it != ordered_code.end( ) )
        const_cast<DecodedLine &>( it->extra ).mnemonicMask &= static_cast<MNEMONIC_MASK>( ~( MM_Label | MM_Data_Label ) );
    for( const auto caller : label.extra.callers )
        calls.extract( { caller.value, {} } );
    label.extra.callers.clear( );
    labels.extract( label );
}

static void RemoveCode( const Pair<uint32_t, DecodedLine> &code ) {
    visited.extract( code.extra.address );
    const auto nhc = calls.extract( { code.extra.address, {} } );
    if( !nhc.empty( ) ) {
        std::set<Pair<uint32_t, uint16_t>> callers;
        auto label = labels.find( { nhc.value( ).extra, { {}, {}, {}, callers } } );
        if( label != labels.end( ) )
            label->extra.callers.extract( { code.extra.address, {} } );
    }
    ordered_code.extract( code );
}

static bool CheckCode( const Pair<uint32_t, DecodedLine> &code ) {
    if( code.extra.length ) {
        if( !memcmp( &MemBase[code.extra.address], code.extra.opCode, code.extra.length ) )
            return true;
        if( ( code.extra.mnemonicMask & MM_Data_Label ) && code.extra.pOperands ) {
            auto &dline = const_cast<Pair<uint32_t, DecodedLine> &>( code ).extra;
            memcpy( dline.opCode, &MemBase[dline.address], dline.length );
            char *pOpCode = dline.szOpcode;
            for( uint8_t i = 0U; i < dline.length; ++i )
                pOpCode += sprintf( pOpCode, "%02X ", MemBase[dline.address + i] );
            switch( dline.length ) {
            case 2U:
                sprintf( const_cast<char *>( dline.pOperands ), "0x%04X", *reinterpret_cast<const uint16_t *>( &MemBase[dline.address] ) );
                dline.operands[0].imm.value.u = reinterpret_cast<const uint16_t &>( MemBase[dline.address] );
                break;
            case 4U:
                sprintf( const_cast<char *>( dline.pOperands ), "0x%08X", *reinterpret_cast<const uint32_t *>( &MemBase[dline.address] ) );
                dline.operands[0].imm.value.u = reinterpret_cast<const uint32_t &>( MemBase[dline.address] );
                break;
            default:
                sprintf( const_cast<char *>( dline.pOperands ), "0x%02X", MemBase[dline.address] );
                dline.operands[0].imm.value.u = MemBase[dline.address];
            }
            return true;
        }
    }
    RemoveCode( code );
    return false;
}

static bool CheckAddress( const uint32_t address ) {
    const auto it = ordered_code.find( { address, {} } );
    if( it != ordered_code.end( ) )
        return CheckCode( *it );
    return false;
}

static bool CheckIsData( const uint32_t address ) {
    const auto it = ordered_code.find( { address, {} } );
    return it != ordered_code.end( ) && ( it->extra.mnemonicMask & MM_Data_Label );
}

static void RemoveSegment( std::set<Pair<uint16_t, SegmentInfo>>::iterator segment ) {
    if( segment->extra.partner ) {
        auto partner = ordered_segments.find( { segment->extra.partner, {} } );
        if( partner != ordered_segments.end( ) )
            ordered_segments.extract( partner );
    }
    if( SEG_CODE == segment->extra.type || SEG_DATA == segment->extra.type ) {
        uint32_t segmentAddress = segment->value << 4;
        uint32_t nextSegmentAddress = segmentAddress + 0xFFFF;
        auto nextSegment = segment;
        if( ++nextSegment != ordered_segments.end( ) )
            nextSegmentAddress = nextSegment->value << 4;
        if( SEG_CODE == segment->extra.type ) {
            for( auto code = ordered_code.lower_bound( { segmentAddress, {} } ), next = code, codeEnd = ordered_code.upper_bound( { nextSegmentAddress, {} } ); code != ordered_code.end( ) && code != codeEnd; code = next ) {
                ++next;
                if( segment->value == code->extra.realAddress.segment )
                    RemoveCode( *code );
            }
        }
        std::set<Pair<uint32_t, uint16_t>> callers;
        for( auto label = labels.lower_bound( { segmentAddress, { {}, {}, {}, callers } } ), next = label, codeEnd = labels.upper_bound( { nextSegmentAddress, { {}, {}, {}, callers } } ); label != labels.end( ) && label != codeEnd; label = next ) {
            ++next;
            if( segment->value == label->extra.segment )
                RemoveLabel( *label );
        }
    }
    ordered_segments.extract( segment );
}

static void CheckSegments( const ADDRESS_PAIR &realAddress ) {
    const auto segment_it = ordered_segments.find( { realAddress.segment, {} } );
    if( segment_it != ordered_segments.end( ) ) {
        if( SEG_DATA == segment_it->extra.type && segment_it->value != data_segment )
            const_cast<SEGTYPE &>( segment_it->extra.type ) = SEG_CODE;
        const auto address = realAddress.address( );
        for( auto nextSegment_it = segment_it; ++nextSegment_it != ordered_segments.end( ); nextSegment_it = segment_it ) {
            if( address >= ADDRESS_PAIR::Address( nextSegment_it->value ) )
                RemoveSegment( nextSegment_it );
            else
                break;
        }
    } else
        ordered_segments.insert( { realAddress.segment, { SEG_CODE, 0U } } );
}

static bool FindNextSegment( const uint16_t segment, std::set<Pair<uint16_t, SegmentInfo>>::iterator &nextSegment ) {
    nextSegment = ordered_segments.find( { segment, {} } );
    if( ordered_segments.end( ) == nextSegment )
        return false;
    ++nextSegment;
    if( ordered_segments.end( ) == nextSegment )
        return false;
    return true;
}

static uint32_t CreateLabel( const uint32_t address, const uint16_t segment, const uint32_t call_address, const uint16_t call_segment, const LABEL_MASK type ) {
    uint32_t label_address = address;
    if( LABEL_DATA == type ) {
        const auto call = calls.find( { call_address, {} } );
        if( calls.end( ) != call ) {
            if( address == call->extra )
                return static_cast<uint32_t>( -1 );
            label_address = call->extra;
        }
    }
    std::set<Pair<uint32_t, uint16_t>> callers;
    auto label = labels.find( { label_address, { {}, {}, {}, callers } } );
    if( labels.end( ) == label ) {
        auto new_label = labels.insert( { address, LabelInfo( type, segment, address, *new std::set<Pair<uint32_t, uint16_t>> ) } );
        if( new_label.second ) // insert success
            label = new_label.first;
    }
    if( label != labels.end( ) ) {
        const_cast<LABEL_MASK &>( label->extra.type ) |= type;
        if( label->extra.callers.insert( { call_address, call_segment } ).second )
            calls.insert( { call_address, address } );
        else if( LABEL_DATA == label->extra.type && segment == label->extra.segment && label->value < address )
            const_cast<uint32_t &>( label->extra.address_max ) = address;
        auto code_it = ordered_code.find( { label->value, {} } );
        if( code_it != ordered_code.end( ) )
            const_cast<DecodedLine &>( code_it->extra ).mnemonicMask |= ( ( label->extra.type & LABEL_CALL ) ? MM_Call_Label : MM_NONE ) | ( ( label->extra.type & LABEL_JUMP ) ? MM_Jump_Label : MM_NONE ) | ( ( label->extra.type & LABEL_DATA ) ? MM_Data_Label : MM_NONE );
        return label->extra.address_max - label->value;
    }
    return static_cast<uint32_t>( -1 );
}

static bool IsStackSegment( const uint16_t segment ) {
    const auto segment_it = ordered_segments.find( { segment, {} } );
    if( ordered_segments.end( ) == segment_it || SEG_STACK == segment_it->extra.type )
        return true;
    return false;
}

static bool CreateDataEntry( const uint32_t address, const uint16_t segment, const uint32_t call_address, const uint16_t call_segment, const ZydisDecodedOperand &op ) {
    if( IsStackSegment( segment ) )
        return false;

    if( CreateLabel( address, segment, call_address, call_segment, LABEL_DATA ) >= 256U )
        return false;

    bool result = false;
    DecodedLine *dline = nullptr;
    if( !visited.contains( address ) )
        ordered_code.extract( { address, {} } );
    const auto it = ordered_code.find( { address, {} } );
    const uint8_t num_bits = op.element_size ? op.element_size : op.size ? op.size : 8U;
    if( it == ordered_code.end( ) ) {
        auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
        if( entry.second ) { // insert successful
            result = true;
            visited.insert( address );
            dline = &const_cast<DecodedLine &>( entry.first->extra );
            dline->pMnemonic = dline->szInstruction;
            dline->instruction = data_instruction;
            dline->instruction.operand_width = num_bits;
            dline->instruction.length = dline->length = num_bits >> 3U;
            dline->operands[0] = imm_operand;
            dline->operands[0].imm = { false, false, { 0U } };
            dline->pOperands = &dline->szInstruction[3];
            dline->mnemonicMask = MM_Data_Label | ( segment != call_segment ? MM_Data_Segment : MM_NONE );
        }
    } else if( num_bits != it->extra.instruction.operand_width ) {
        if( it->extra.mnemonicMask & MM_Data_Segment ) {
            if( num_bits < it->extra.instruction.operand_width ) {
                auto &rdline = const_cast<DecodedLine &>( it->extra );
                rdline.instruction.operand_width = num_bits;
                rdline.instruction.length = rdline.length = num_bits >> 3U;
            }
        } else if( num_bits > it->extra.instruction.operand_width ) {
            dline = &const_cast<DecodedLine &>( it->extra );
            dline->instruction.operand_width = num_bits;
            dline->instruction.length = dline->length = num_bits >> 3U;
        }
    }
    if( dline ) {
        memcpy( dline->opCode, &MemBase[address], dline->length );
        char *pOpCode = dline->szOpcode;
        for( uint8_t i = 0U; i < dline->length; ++i ) {
            pOpCode += sprintf( pOpCode, "%02X ", MemBase[address + i] );
            if( i ) {
                ordered_code.extract( { address + i, {} } );
                visited.extract( address + i );
            }
        }
        switch( num_bits ) {
        case 0x10:
            sprintf_s( dline->szInstruction, sizeof( dline->szInstruction ), "dw 0x%04X", *reinterpret_cast<const uint16_t *>( &MemBase[address] ) );
            dline->operands[0].imm.value.u = reinterpret_cast<const uint16_t &>( MemBase[address] );
            break;
        case 0x20:
            sprintf_s( dline->szInstruction, sizeof( dline->szInstruction ), "dd 0x%08X", *reinterpret_cast<const uint32_t *>( &MemBase[address] ) );
            dline->operands[0].imm.value.u = reinterpret_cast<const uint32_t &>( MemBase[address] );
            break;
        default:
            sprintf_s( dline->szInstruction, sizeof( dline->szInstruction ), "db 0x%02X", MemBase[address] );
            dline->operands[0].imm.value.u = MemBase[address];
        }
        dline->szInstruction[2] = 0;
    }
    return result;
}

static void CreateDataWordEntry( const uint32_t address, const uint16_t segment, const bool fData ) {
    auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
    if( entry.second ) { // insert successful
        DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
        dline.pMnemonic = dline.szInstruction;
        dline.instruction = data_instruction;
        dline.instruction.length = dline.length = 2U;
        dline.instruction.operand_count = dline.instruction.operand_count_visible = 0U;
        dline.mnemonicMask = fData ? MM_Data_Label : MM_NONE;

        *dline.opCode = MemBase[address];
        dline.opCode[1] = MemBase[address + 1U];
        sprintf_s( dline.szOpcode, sizeof( dline.szOpcode ), "%02X %02X ", MemBase[address], MemBase[address + 1U] );
    }
}

constexpr const uint8_t MAX_DATA_WORD_SECTION_SIZE = 64U;

static void CreateDataWordSection( uint32_t address, const uint16_t segment ) {
    if( IsStackSegment( segment ) )
        return;

    std::set<Pair<uint16_t, SegmentInfo>>::iterator nextSegment;
    if( FindNextSegment( segment, nextSegment ) ) {
        uint32_t nextSegmentAddress = ADDRESS_PAIR::Address( nextSegment->value );
        if( address < nextSegmentAddress ) { 
            auto code_it = ordered_code.upper_bound( { address, {} } );
            uint32_t address_max = static_cast<uint32_t>( -1 );
            if( ordered_code.end( ) != code_it )
                address_max = code_it->value - 1U;
            uint32_t address_min = 0U;
            if( ordered_code.begin( ) != code_it ) {
                --code_it;
                address_min = code_it->value + code_it->extra.length;
            }
            if( address_min <= address ) {
                uint32_t addresses[MAX_DATA_WORD_SECTION_SIZE] = {};
                auto p_current_address = addresses;
                uint16_t nextSegmentOffset = ADDRESS_PAIR::Address( nextSegment->value ) - ADDRESS_PAIR::Address( segment );
                for( uint16_t *word_offset = reinterpret_cast<uint16_t *>( &MemBase[address] ), count = MAX_DATA_WORD_SECTION_SIZE; count && *word_offset < nextSegmentOffset; ++word_offset, address += 2U, --count, *++p_current_address = 0U ) {
                    if( address < address_max )
                        *p_current_address = address;
                    else {
                        *addresses = 0U;
                        break;
                    }
                }
                p_current_address = addresses;
                while( *p_current_address ) {
                    CreateDataWordEntry( *p_current_address, segment, true );
                    ++p_current_address;
                }
            }
        }
    }
}

extern uint16_t RealSegValue( const SegNames );

static uint32_t GetRegisterValue( const ZydisRegister zreg, const bool fRegZero, const uint16_t cs_segment ) {
    if( fRegZero ) {
        if( ZYDIS_REGISTER_CS == zreg ) return cs_segment;
        if( ZYDIS_REGISTER_DS == zreg ) return data_segment;
        return 0U;
    }
    switch( zreg ) {
    case ZYDIS_REGISTER_AL: return reg_al;
    case ZYDIS_REGISTER_CL: return reg_cl;
    case ZYDIS_REGISTER_DL: return reg_dl;
    case ZYDIS_REGISTER_BL: return reg_bl;
    case ZYDIS_REGISTER_AH: return reg_ah;
    case ZYDIS_REGISTER_CH: return reg_ch;
    case ZYDIS_REGISTER_DH: return reg_dh;
    case ZYDIS_REGISTER_BH: return reg_bh;
    // General purpose registers 16-bit
    case ZYDIS_REGISTER_AX: return reg_ax;
    case ZYDIS_REGISTER_CX: return reg_cx;
    case ZYDIS_REGISTER_DX: return reg_dx;
    case ZYDIS_REGISTER_BX: return reg_bx;
    case ZYDIS_REGISTER_SP: return reg_sp;
    case ZYDIS_REGISTER_BP: return reg_bp;
    case ZYDIS_REGISTER_SI: return reg_si;
    case ZYDIS_REGISTER_DI: return reg_di;
    // General purpose registers 32-bit
    case ZYDIS_REGISTER_EAX: return reg_eax;
    case ZYDIS_REGISTER_ECX: return reg_ecx;
    case ZYDIS_REGISTER_EDX: return reg_edx;
    case ZYDIS_REGISTER_EBX: return reg_ebx;
    case ZYDIS_REGISTER_ESP: return reg_esp;
    case ZYDIS_REGISTER_EBP: return reg_ebp;
    case ZYDIS_REGISTER_ESI: return reg_esi;
    case ZYDIS_REGISTER_EDI: return reg_edi;
    // Flags register
    case ZYDIS_REGISTER_EFLAGS:
    case ZYDIS_REGISTER_FLAGS: return reg_flags;
    // Instruction-pointer registers
    case ZYDIS_REGISTER_IP: return reg_ip;
    case ZYDIS_REGISTER_EIP: return reg_eip;
    // Segment registers
    case ZYDIS_REGISTER_ES: return RealSegValue( es );
    case ZYDIS_REGISTER_CS: return RealSegValue( cs );
    case ZYDIS_REGISTER_SS: return RealSegValue( ss );
    case ZYDIS_REGISTER_DS: return RealSegValue( ds );
    case ZYDIS_REGISTER_FS: return RealSegValue( fs );
    case ZYDIS_REGISTER_GS: return RealSegValue( gs );
    default:
        break;
    }
    return 0U;
}

static ADDRESS_PAIR CalculateRealAddress( const ZydisDecodedInstruction &instruction, const ZydisDecodedOperand &operand, const ADDRESS_PAIR &realAddress, const bool fRegZero ) {
    switch( operand.type ) {
    case ZYDIS_OPERAND_TYPE_POINTER:
        return { operand.ptr.segment, operand.ptr.offset };
    case ZYDIS_OPERAND_TYPE_IMMEDIATE: {
        ZyanU64 z64_result_address;
        if( ZYAN_SUCCESS( ZydisCalcAbsoluteAddress( &instruction, &operand, realAddress.offset, &z64_result_address ) ) )
            return { realAddress.segment, static_cast<uint32_t>( z64_result_address ) };
        const uint8_t num_bits = operand.element_size ? operand.element_size : operand.size ? operand.size : 8U;
        uint32_t imm_value = operand.imm.is_relative ? realAddress.offset + instruction.length : 0U;
        switch( num_bits ) {
        case 0x10: imm_value += ( operand.imm.is_signed ? static_cast<int16_t>( operand.imm.value.s ) : static_cast<uint16_t>( operand.imm.value.u ) ); break;
        case 0x20: imm_value += ( operand.imm.is_signed ? static_cast<int32_t>( operand.imm.value.s ) : static_cast<uint32_t>( operand.imm.value.u ) ); break;
        default: imm_value += ( operand.imm.is_signed ? static_cast<int8_t>( operand.imm.value.s ) : static_cast<uint8_t>( operand.imm.value.u ) ); break;
        }
        return { realAddress.segment, imm_value };
    }
    case ZYDIS_OPERAND_TYPE_MEMORY: {
        ZyanU64 z64_result_address;
        uint16_t segment = GetRegisterValue( operand.mem.segment, fRegZero, realAddress.segment );
        ZydisRegisterContext register_context;
        if( ZYDIS_REGISTER_NONE != operand.mem.base )
            register_context.values[operand.mem.base] = GetRegisterValue( operand.mem.base, fRegZero, realAddress.segment );
        if( ZYDIS_REGISTER_NONE != operand.mem.index )
            register_context.values[operand.mem.index] = GetRegisterValue( operand.mem.index, fRegZero, realAddress.segment );
        if( ZYAN_SUCCESS( ZydisCalcAbsoluteAddressEx( &instruction, &operand, realAddress.offset, &register_context, &z64_result_address ) ) )
            return { segment, static_cast<uint32_t>( z64_result_address ) };
        return { segment, static_cast<uint32_t>( realAddress.offset + operand.mem.disp.value ) };
    }
    default:
        break;
    }
    return { 0U, 0U };
}

extern void UpdateDataSegment( const uint16_t );

static void FindDS( const uint32_t startAddress, const uint32_t startOffset ) {
    if( data_segment )
        return;
    std::vector<ADDRESS_PAIR> toReturn;
    uint16_t dx = 0U; // for DS value tracking
    ADDRESS_PAIR realAddress = { static_cast<uint16_t>( ( startAddress - startOffset ) >> 4 ), startOffset };
    uint32_t address = realAddress.address( );
    while( address < binarySize ) {
        ZydisDecodedInstruction instruction;
        ZydisDecoderContext context;
        if( ZYAN_SUCCESS( ZydisDecoderDecodeInstruction( &decoder, &context, &MemBase[address], binarySize - address, &instruction ) ) ) {
            if( ZYDIS_MNEMONIC_CALL == instruction.mnemonic || ZYDIS_MNEMONIC_JMP == instruction.mnemonic ) {
                if( instruction.operand_count ) {
                    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
                    if( ZYAN_SUCCESS( ZydisDecoderDecodeOperands( &decoder, &context, &instruction, operands, instruction.operand_count ) ) ) {
                        const auto &op = operands[0];
                        if( ZYDIS_OPERAND_TYPE_IMMEDIATE == op.type || ZYDIS_OPERAND_TYPE_POINTER == op.type ) {
                            ADDRESS_PAIR ptr = CalculateRealAddress( instruction, op, realAddress, true );
                            uint32_t addr = ptr.address( );
                            if( ValidateAddress( addr, ptr ) ) {
                                if( ZYDIS_MNEMONIC_CALL == instruction.mnemonic )
                                    toReturn.push_back( realAddress + instruction.length );
                                realAddress = ptr;
                                address = realAddress.address( );
                                continue;
                            }
                        }
                    }
                }
            } else if( ZYDIS_MNEMONIC_RET == instruction.mnemonic ) {
                realAddress = toReturn.back( );
                toReturn.pop_back( );
                address = realAddress.address( );
                continue;
            } else if( ZYDIS_MNEMONIC_MOV == instruction.mnemonic ) {
                if( 2U == instruction.operand_count ) {
                    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
                    if( ZYAN_SUCCESS( ZydisDecoderDecodeOperands( &decoder, &context, &instruction, operands, instruction.operand_count ) ) ) {
                        if( ZYDIS_OPERAND_TYPE_REGISTER == operands[0].type ) {
                            if( ZYDIS_OPERAND_TYPE_IMMEDIATE == operands[1].type ) {
                                switch( operands[0].reg.value ) {
                                case ZYDIS_REGISTER_DX:
                                    dx = static_cast<uint16_t>( operands[1].imm.value.u );
                                    break;
                                case ZYDIS_REGISTER_DS:
                                    data_segment = static_cast<uint16_t>( operands[1].imm.value.u );
                                    ordered_segments.insert( { data_segment, { SEG_DATA, 0U } } );
                                    UpdateDataSegment( data_segment );
                                    return;
                                default:
                                    break;
                                }
                            } else if( !data_segment && dx && ZYDIS_OPERAND_TYPE_REGISTER == operands[1].type && ZYDIS_REGISTER_DS == operands[0].reg.value && ZYDIS_REGISTER_DX == operands[1].reg.value ) {
                                data_segment = dx;
                                ordered_segments.insert( { data_segment, { SEG_DATA, 0U } } );
                                UpdateDataSegment( data_segment );
                                return;
                            }
                        }
                    }
                }
            }
            address += instruction.length;
            realAddress += instruction.length;
        } else
            break;
    }
}

// Recursive disassembly function (initial framework credit: CoPilot)
static uint32_t RecursiveDisassemble( const uint32_t startAddress, const uint32_t startOffset, const bool fKeepUnknownData ) {
    uint32_t primary_length = 0U;
    std::vector<ADDRESS_PAIR> toVisit{ { static_cast<uint16_t>( ( startAddress - startOffset ) >> 4 ), startOffset } };
    std::unordered_set<uint32_t> added;

    uint8_t ah = 0U; // for int 21h tracking

    bool fPrimary = true;
    while( !toVisit.empty( ) ) {
        ADDRESS_PAIR realAddress = toVisit.back( );
        toVisit.pop_back( );

        CheckSegments( realAddress );

        uint32_t address = realAddress.address( );
        while( address < binarySize ) {
            if( visited.contains( address ) ) {
                if( CheckAddress( address ) )
                    break;
            } else {
                if( fKeepUnknownData && CheckIsData( address ) )
                    break;
                visited.insert( address );
            }
            if( MemBase[address] == 0 && MemBase[address + 1] == 0 ) // 00 00
                break;

            ordered_code.extract( { address, {} } );
            auto entry = ordered_code.insert( { address, DecodedLine( realAddress, address ) } );
            if( !entry.second ) // insert failed
                break;
            DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
            dline.pMnemonic = dline.szInstruction;
            if( ZYAN_SUCCESS( ZydisDecoderDecodeFull( &decoder, &MemBase[address], binarySize - address, &dline.instruction, dline.operands ) ) && dline.instruction.length ) {
                dline.length = dline.instruction.length;
                memcpy( dline.opCode, &MemBase[address], dline.length );
                char *pOpCode = dline.szOpcode;
                for( uint8_t i = 0U; i < dline.length; ++i ) {
                    pOpCode += sprintf( pOpCode, "%02X ", MemBase[address + i] );
                    if( i ) {
                        ordered_code.extract( { address + i, {} } );
                        visited.extract( address + i );
                    }
                }
                ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, dline.szInstruction, sizeof( dline.szInstruction ), realAddress.offset, ZYAN_NULL );
                dline.pOperands = dline.szInstruction;
                while( *dline.pOperands && *dline.pOperands != ' ' )
                    ++dline.pOperands;
                if( ' ' == *dline.pOperands ) {
                    *const_cast<char *>( dline.pOperands ) = 0;
                    ++dline.pOperands;
                }
                realAddress += dline.length;
                address += dline.length;

                // Handle recursion for jumps/calls
                switch( dline.instruction.mnemonic ) {
                case ZYDIS_MNEMONIC_INT:
                    dline.mnemonicMask = MM_INT;
                    break;
                case ZYDIS_MNEMONIC_CALL:
                    dline.mnemonicMask = MM_CALL;
                    break;
                case ZYDIS_MNEMONIC_JMP:
                    dline.mnemonicMask = MM_JMP;
                    break;
                case ZYDIS_MNEMONIC_JB:
                case ZYDIS_MNEMONIC_JBE:
                case ZYDIS_MNEMONIC_JCXZ:
                case ZYDIS_MNEMONIC_JECXZ:
                case ZYDIS_MNEMONIC_JL:
                case ZYDIS_MNEMONIC_JLE:
                case ZYDIS_MNEMONIC_JNB:
                case ZYDIS_MNEMONIC_JNBE:
                case ZYDIS_MNEMONIC_JNL:
                case ZYDIS_MNEMONIC_JNLE:
                case ZYDIS_MNEMONIC_JNO:
                case ZYDIS_MNEMONIC_JNP:
                case ZYDIS_MNEMONIC_JNS:
                case ZYDIS_MNEMONIC_JNZ:
                case ZYDIS_MNEMONIC_JO:
                case ZYDIS_MNEMONIC_JP:
                case ZYDIS_MNEMONIC_JS:
                case ZYDIS_MNEMONIC_JZ:
                    dline.mnemonicMask = MM_ConditionalJump;
                    break;
                case ZYDIS_MNEMONIC_MOV:
                    dline.mnemonicMask = MM_MOV;
                    break;
                case ZYDIS_MNEMONIC_RET:
                case ZYDIS_MNEMONIC_IRET:
                    dline.mnemonicMask = MM_RET;
                    break;
                case ZYDIS_MNEMONIC_ENTER:
                case ZYDIS_MNEMONIC_LEAVE:
                case ZYDIS_MNEMONIC_POP:
                case ZYDIS_MNEMONIC_POPA:
                case ZYDIS_MNEMONIC_POPAD:
                case ZYDIS_MNEMONIC_POPF:
                case ZYDIS_MNEMONIC_POPFD:
                case ZYDIS_MNEMONIC_PUSH:
                case ZYDIS_MNEMONIC_PUSHA:
                case ZYDIS_MNEMONIC_PUSHAD:
                case ZYDIS_MNEMONIC_PUSHF:
                case ZYDIS_MNEMONIC_PUSHFD:
                    dline.mnemonicMask = MM_Stack;
                    break;
                case ZYDIS_MNEMONIC_CMP:
                    dline.mnemonicMask = MM_CMP;
                    break;
                case ZYDIS_MNEMONIC_AND:
                case ZYDIS_MNEMONIC_NOT:
                case ZYDIS_MNEMONIC_OR:
                case ZYDIS_MNEMONIC_TEST:
                case ZYDIS_MNEMONIC_XOR:
                case ZYDIS_MNEMONIC_SAR:
                case ZYDIS_MNEMONIC_SARX:
                case ZYDIS_MNEMONIC_SHL:
                case ZYDIS_MNEMONIC_SHR:
                case ZYDIS_MNEMONIC_SHLX:
                case ZYDIS_MNEMONIC_SHRX:
                case ZYDIS_MNEMONIC_ROL:
                case ZYDIS_MNEMONIC_ROR:
                case ZYDIS_MNEMONIC_RORX:
                case ZYDIS_MNEMONIC_XCHG:
                    dline.mnemonicMask = MM_Logical;
                    break;
                case ZYDIS_MNEMONIC_ADD:
                case ZYDIS_MNEMONIC_DEC:
                case ZYDIS_MNEMONIC_DIV:
                case ZYDIS_MNEMONIC_IDIV:
                case ZYDIS_MNEMONIC_IMUL:
                case ZYDIS_MNEMONIC_INC:
                case ZYDIS_MNEMONIC_MUL:
                case ZYDIS_MNEMONIC_SUB:
                    dline.mnemonicMask = MM_Math;
                    break;
                case ZYDIS_MNEMONIC_LOOP:
                case ZYDIS_MNEMONIC_LOOPE:
                case ZYDIS_MNEMONIC_LOOPNE:
                    dline.mnemonicMask = MM_LOOP;
                    break;
                case ZYDIS_MNEMONIC_IN:
                case ZYDIS_MNEMONIC_OUT:
                case ZYDIS_MNEMONIC_OUTSB:
                case ZYDIS_MNEMONIC_OUTSD:
                case ZYDIS_MNEMONIC_OUTSW:
                    dline.mnemonicMask = MM_IO;
                    break;
                case ZYDIS_MNEMONIC_CMPSB:
                case ZYDIS_MNEMONIC_CMPSD:
                case ZYDIS_MNEMONIC_CMPSW:
                case ZYDIS_MNEMONIC_LODSB:
                case ZYDIS_MNEMONIC_LODSD:
                case ZYDIS_MNEMONIC_LODSW:
                case ZYDIS_MNEMONIC_MOVSB:
                case ZYDIS_MNEMONIC_MOVSD:
                case ZYDIS_MNEMONIC_MOVSW:
                case ZYDIS_MNEMONIC_SCASB:
                case ZYDIS_MNEMONIC_SCASD:
                case ZYDIS_MNEMONIC_SCASW:
                case ZYDIS_MNEMONIC_STOSB:
                case ZYDIS_MNEMONIC_STOSD:
                case ZYDIS_MNEMONIC_STOSW:
                case ZYDIS_MNEMONIC_XLAT:
                    dline.mnemonicMask = MM_String;
                    break;
                default:
                    dline.mnemonicMask = MM_Instruction;
                    break;
                }
                if( dline.instruction.attributes & ( ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE ) )
                    dline.mnemonicMask |= MM_REP;
                if( dline.instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT )
                    dline.mnemonicMask |= MM_Has_Segment;
                if( dline.mnemonicMask & ( MM_Branch | MM_RET | MM_String ) ) {
                    cs_disasm( cs_handle, &MemBase[dline.address], binarySize - dline.address, dline.realAddress.offset, 1, &dline.cs_instruction );
                    dline.pMnemonic = dline.cs_instruction->mnemonic;
                    if( !( dline.mnemonicMask & MM_Branch ) )
                        dline.pOperands = dline.cs_instruction->op_str;
                }
                // check operands for memory references...
                for( uint8_t i = 0U; i < NUM_MEM_OPS && i < dline.instruction.operand_count; ++i ) {
                    const auto &op = dline.operands[i];
                    if( ZYDIS_OPERAND_TYPE_MEMORY == op.type && ZYDIS_REGISTER_SS != op.mem.segment ) {
                        dline.mnemonicMask |= MM_Memory_Access;
                        dline.mem_access[i].size = op.element_size;
                        dline.mem_access[i].segment_id = op.mem.segment;
                        dline.mem_access[i].base_id = op.mem.base;
                        dline.mem_access[i].disp.has_displacement = op.mem.disp.has_displacement;
                        dline.mem_access[i].disp.value = op.mem.disp.value;

                        if( ( ZYDIS_REGISTER_CS == op.mem.segment || ZYDIS_REGISTER_DS == op.mem.segment ) && op.mem.disp.has_displacement && ( ZYDIS_REGISTER_NONE == op.mem.base || ( 0x10 == op.size && ( dline.mnemonicMask & MM_JMP ) ) ) ) {
                            ADDRESS_PAIR ptr = CalculateRealAddress( dline.instruction, op, dline.realAddress, true );
                            uint32_t addr = ptr.address( );
                            if( ValidateAddress( addr, ptr ) ) {
                                if( ZYDIS_REGISTER_NONE == op.mem.base ) {
                                    if( CreateDataEntry( addr, ptr.segment, dline.address, dline.realAddress.segment, op ) )
                                        added.insert( address );
                                } else if( op.mem.disp.value > dline.realAddress.offset )
                                    CreateDataWordSection( addr, ptr.segment );
                            }
                        }
                    } if( i && ZYDIS_OPERAND_TYPE_IMMEDIATE == op.type && ZYDIS_OPERAND_TYPE_REGISTER == dline.operands[0].type && ZYDIS_REGISTER_SI == dline.operands[0].reg.value ) {
                        uint32_t imm_address = ADDRESS_PAIR::Address( CalculateRealAddress( dline.instruction, op, dline.realAddress, true ) );
                        if( imm_address > dline.address )
                            CreateDataWordSection( imm_address, realAddress.segment );
                    }
                }
                if( dline.mnemonicMask & MM_Branch ) {
                    const auto &op = dline.operands[0];
                    if( ZYDIS_OPERAND_TYPE_IMMEDIATE == op.type || ZYDIS_OPERAND_TYPE_POINTER == op.type ) {
                        ADDRESS_PAIR ptr = CalculateRealAddress( dline.instruction, op, dline.realAddress, true );
                        sprintf( const_cast<char *>( dline.pOperands ), "%04X:%04X", ptr.segment, ptr.offset );
                        uint32_t addr = ptr.address( );
                        if( ValidateAddress( addr, ptr ) ) {
                            if( !added.contains( addr ) ) {
                                added.insert( addr );
                                if( !visited.contains( addr ) )
                                    toVisit.push_back( ptr );
                            }
                            CreateLabel( addr, ptr.segment, dline.address, dline.realAddress.segment, ( dline.mnemonicMask & MM_CALL ) ? LABEL_CALL : LABEL_JUMP );
                        }
                    }
                    if( dline.mnemonicMask & MM_JMP )
                        break;
                } else if( dline.mnemonicMask & MM_INT ) {
                    if( dline.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && dline.operands[0].imm.value.u == 0x21 && ah == 0x4C ) {
                        sprintf( dline.szComment, "DOS Exit" );
                        break;
                    }
                } else if( dline.mnemonicMask & MM_RET ) {
                    break;
                } else if( dline.mnemonicMask & MM_MOV ) {
                    if( 2U == dline.instruction.operand_count && ZYDIS_OPERAND_TYPE_REGISTER == dline.operands[0].type ) {
                        if( ZYDIS_OPERAND_TYPE_IMMEDIATE == dline.operands[1].type ) {
                            switch( dline.operands[0].reg.value ) {
                            case ZYDIS_REGISTER_AH:
                                ah = static_cast<uint8_t>( dline.operands[1].imm.value.u );
                                break;
                            case ZYDIS_REGISTER_AX:
                                ah = static_cast<uint8_t>( dline.operands[1].imm.value.u >> 8U );
                            default:
                                break;
                            }
                        }
                    }
                }
            } else { // Decode failed...
                if( MemBase[address] == 0xFE && ( ( MemBase[address + 1] >> 3 ) == 0x07 ) ) { // DOSBox internal callback
                    const uint16_t &dw = *reinterpret_cast<const uint16_t *>( &MemBase[address + 2] );
                    dline.mnemonicMask = MM_DOSBox_internal;
                    dline.instruction = callback_instruction;
                    dline.length = dline.instruction.length;
                    dline.operands[0] = imm_operand;
                    dline.operands[0].imm = { false, false, { dw } };
                    memcpy( dline.opCode, &MemBase[dline.address], dline.length );
                    char *pOpCode = dline.szOpcode;
                    for( auto i = 0; i < dline.length; ++i )
                        pOpCode += sprintf( pOpCode, "%02X ", MemBase[address + i] );
                    sprintf_s( dline.szInstruction, sizeof( dline.szInstruction ), "callback 0x%02X", dw );
                    dline.szInstruction[8] = 0;
                    dline.pOperands = &dline.szInstruction[9];
                    strcat( dline.szComment, CALLBACK_GetDescription( dw ) );
                    realAddress += dline.length;
                    address += dline.length;
                } else { // Treat as data; step forward 1 byte
                    dline.length = 1U;
                    *dline.opCode = MemBase[address];
                    sprintf( dline.szOpcode, "%02X", MemBase[address] );
                    ++address;
                    ++realAddress;
                }
            }
        }
        if( fPrimary ) {
            fPrimary = false;
            primary_length = address - startAddress;
        }
    }
    return primary_length;
}

static const uint16_t DOSBOX_SEGMENT = 0xC887;

static void CheckNextSegment( std::set<Pair<uint16_t, SegmentInfo>>::iterator &nextSegment, uint32_t &nextSegmentAddress ) {
    if( nullptr == ( *( (std::_Iterator_base12 *) &( *( ( std::_Tree_unchecked_const_iterator<std::_Tree_val<std::_Tree_simple_types<Pair<uint16_t, SegmentInfo>>>, std::_Iterator_base12>* ) & nextSegment ) ) ) )._Myproxy ) {
        nextSegment = ordered_segments.lower_bound( { ADDRESS_PAIR::Segment( nextSegmentAddress ), {} } );
        if( nextSegment != ordered_segments.end( ) )
            nextSegmentAddress = ADDRESS_PAIR::Address( nextSegment->value );
    }
}

static bool CheckAdvanceNextSegment( uint32_t &address, uint16_t &segment, uint32_t &nextSegmentAddress, std::set<Pair<uint16_t, SegmentInfo>>::iterator &nextSegment ) {
    CheckNextSegment( nextSegment, nextSegmentAddress );
    if( address >= nextSegmentAddress ) {
        while( SEG_CODE != nextSegment->extra.type ) {
            ++nextSegment;
            if( ordered_segments.end( ) == nextSegment )
                return false;
        }
        if( nextSegment->value >= DOSBOX_SEGMENT )
            return false;
        // Check for existence of a stack segment after next segment...
        auto stackSegment = std::next( nextSegment );
        while( ordered_segments.end( ) != stackSegment && SEG_STACK != stackSegment->extra.type )
            ++stackSegment;
        if( ordered_segments.end( ) == stackSegment || SEG_STACK != stackSegment->extra.type )
            return false;
        segment = nextSegment->value;
        if( ADDRESS_PAIR::Segment( nextSegmentAddress ) != nextSegment->value )
            address = ADDRESS_PAIR::Address( segment );
        ++nextSegment;
        if( ordered_segments.end( ) == nextSegment )
            return false;
        nextSegmentAddress = ADDRESS_PAIR::Address( nextSegment->value );
    }
    return true;
}

static void CreateAlignEntry( const uint32_t address, const uint16_t segment, const uint8_t length, const char *szOpcode, char *pOpcode ) {
    auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
    if( entry.second ) { // insert successful
        DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
        dline.pMnemonic = dline.szInstruction;
        dline.instruction = data_instruction;
        dline.mnemonicMask = MM_ALIGN;
        dline.instruction.length = dline.length = length + 1U;
        memcpy( dline.opCode, &MemBase[address], dline.length );
        dline.instruction.operand_count = dline.instruction.operand_count_visible = 0U;
        strcpy( dline.szInstruction, "align" );
        if( length > 6U ) {
            pOpcode[-4] = '.';
            pOpcode[-3] = '.';
            pOpcode[-2] = '.';
        }
        strcpy( dline.szOpcode, szOpcode );
    }
}

static void CreateUnknownByteEntry( const uint32_t address, const uint16_t segment, const uint8_t value ) {
    auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
    if( entry.second ) { // insert successful
        DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
        dline.pMnemonic = dline.szInstruction;
        dline.instruction = data_instruction;
        dline.instruction.length = dline.length = 1U;
        dline.instruction.operand_count = dline.instruction.operand_count_visible = 0U;
        dline.mnemonicMask = MM_NONE;

        *dline.opCode = value;
        sprintf( dline.szOpcode, "%02X ", value );
        if( isprint( value ) )
            sprintf( dline.szComment, "%c", value );
    }
}

static void CreateUnknownDoubleWordEntry( const uint32_t address, const uint16_t segment ) {
    auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
    if( entry.second ) { // insert successful
        DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
        dline.pMnemonic = dline.szInstruction;
        dline.instruction = data_instruction;
        dline.instruction.length = dline.length = 4U;
        dline.instruction.operand_count = dline.instruction.operand_count_visible = 0U;
        dline.mnemonicMask = MM_NONE;

        memcpy( dline.opCode, &MemBase[address], dline.length );
        sprintf_s( dline.szOpcode, sizeof( dline.szOpcode ), "%02X %02X %02X %02X ", MemBase[address], MemBase[address + 1U], MemBase[address + 2U], MemBase[address + 3U] );
    }
}

static void CreateStringEntry( const uint32_t address, const uint16_t segment, const uint8_t length, const char *szOperand, const char *szOpcode, char *pOpcode ) {
    auto entry = ordered_code.insert( { address, DecodedLine( ADDRESS_PAIR::RealAddress( address, segment ), address ) } );
    if( entry.second ) { // insert successful
        DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
        dline.pMnemonic = dline.szInstruction;
        dline.instruction = data_instruction;
        dline.mnemonicMask = length > 4U ? MM_Data_Label : MM_NONE;
        dline.instruction.length = dline.length = length + 1U;
        memcpy( dline.opCode, &MemBase[address], dline.length );
        dline.instruction.operand_width = 8U;
        dline.operands[0] = imm_operand;
        dline.operands[0].imm = { false, false, { length } };
        sprintf( dline.szInstruction, "db %u,'%s'", length, szOperand );
        dline.szInstruction[2] = 0;
        dline.pOperands = &dline.szInstruction[3];
        if( length > 6U ) {
            pOpcode[-4] = '.';
            pOpcode[-3] = '.';
            pOpcode[-2] = '.';
        }
        strcpy( dline.szOpcode, szOpcode );
    }
}

static bool DisassebleUnknownSection( uint32_t &unknown_address, const uint32_t &next_address, const uint32_t address_limit, const uint16_t segment ) {
    if( unknown_address > next_address || next_address - unknown_address > 0x1000 ) {
        unknown_address = next_address;
        return false;
    }
    while( unknown_address < next_address ) {
        while( !MemBase[unknown_address] ) {
            CreateUnknownByteEntry( unknown_address, segment, MemBase[unknown_address] );
            ++unknown_address;
        }
        if( unknown_address >= next_address )
            break;
        uint32_t test_address = unknown_address;
        uint32_t known_address = 0U;
        while( test_address < address_limit && MemBase[test_address] ) {
            ZydisDecodedInstruction instruction;
            ZydisDecoderContext context;
            if( ZYAN_SUCCESS( ZydisDecoderDecodeInstruction( &decoder, &context, &MemBase[test_address], binarySize - test_address, &instruction ) ) ) {
                test_address += instruction.length;
                switch( instruction.mnemonic ) {
                case ZYDIS_MNEMONIC_JMP:
                case ZYDIS_MNEMONIC_RET:
                case ZYDIS_MNEMONIC_IRET:
                    known_address = test_address;
                default:
                    break;
                }
                if( known_address || test_address == next_address )
                    break;
            } else
                break;
        }
        auto end_address = test_address == next_address ? next_address : known_address ? known_address : test_address == address_limit ? address_limit : 0U;
        if( end_address ) {
            while( unknown_address < end_address ) {
                const auto run_length = RecursiveDisassemble( unknown_address, unknown_address - ( segment << 4U ), true );
                if( !run_length )
                    break;
                unknown_address += run_length;
            }
            if( unknown_address < next_address ) {
                for( auto code_it = ordered_code.find( { unknown_address, {} } ); unknown_address < next_address && code_it != ordered_code.end( ) && code_it->extra.length; ++code_it ) {
                    if( unknown_address != code_it->extra.address )
                        break;
                    unknown_address += code_it->extra.length;
                }
            }
        } else {
            while( unknown_address < next_address ) {
                if( ordered_code.contains( { unknown_address, {} } ) ) {
                    for( auto code_it = ordered_code.find( { unknown_address, {} } ); unknown_address < next_address && code_it != ordered_code.end( ) && code_it->extra.length; ++code_it ) {
                        if( unknown_address != code_it->extra.address )
                            break;
                        unknown_address += code_it->extra.length;
                    }
                    break;
                }
                if( ( 0xCB == MemBase[unknown_address] || 0xC3 == MemBase[unknown_address] ) && !( 0xCB == MemBase[unknown_address + 1U] || 0xC3 == MemBase[unknown_address + 1U] ) )
                    break;
                CreateUnknownByteEntry( unknown_address, segment, MemBase[unknown_address] );
                ++unknown_address;
            }
        }
    }
    if( next_address < address_limit ) {
        for( auto code_it = ordered_code.find( { next_address, {} } ); next_address < address_limit && code_it != ordered_code.end( ) && code_it->extra.length; ++code_it ) {
            if( next_address != code_it->extra.address )
                break;
            const_cast<uint32_t &>( next_address ) += code_it->extra.length;
        }
        unknown_address = next_address;
    }
    return true;
}

static bool IsUnknownData( uint32_t unknown_address, const uint32_t next_address, const uint16_t segment ) {
    const auto it = ordered_code.find( { next_address, {} } );
    if( it != ordered_code.end( ) && ( it->extra.mnemonicMask & MM_Data_Label ) ) {
        if( it != ordered_code.begin( ) && ( std::prev( it )->extra.mnemonicMask & MM_Data_Label ) ) {
            switch( next_address - unknown_address ) {
            case 1U:
                CreateUnknownByteEntry( unknown_address, segment, MemBase[unknown_address] );
                return true;
            case 2U:
                CreateDataWordEntry( unknown_address, segment, false );
                return true;
            case 4U:
                CreateUnknownDoubleWordEntry( unknown_address, segment );
                return true;
            }
        }
    }
    return false;
}

static std::set<Pair<uint32_t, DecodedLine>>::iterator & CodeNext( std::set<Pair<uint32_t, DecodedLine>>::iterator &code_it, uint32_t &address ) {
    if( nullptr != ( *( (std::_Iterator_base12 *) &( *( ( std::_Tree_unchecked_const_iterator<std::_Tree_val<std::_Tree_simple_types<Pair<unsigned int, DecodedLine>>>, std::_Iterator_base12>* ) & code_it ) ) ) )._Myproxy )
        return ++code_it;
    code_it = ordered_code.lower_bound( { address, {} } );
    if( code_it != ordered_code.end( ) ) {
        auto prev = std::prev( code_it );
        address = prev->value + prev->extra.length;
    }
    return code_it;
}

static void CheckUnknown( ) { // attempt to identify non-disassembled parts...
    uint16_t start_segment = static_cast<uint16_t>( segment0_phys >> 4 );
    std::set<Pair<uint16_t, SegmentInfo>>::iterator nextSegment;
    if( !FindNextSegment( start_segment, nextSegment ) )
        return;
    uint32_t nextSegmentAddress = ADDRESS_PAIR::Address( nextSegment->value );
    uint32_t address = ADDRESS_PAIR::Address( start_segment );
    uint32_t address_max = ADDRESS_PAIR::Address( data_segment );
    uint16_t segment = start_segment;
    for( auto code_it = ordered_code.begin( ); code_it != ordered_code.end( ) && address < address_max; code_it = CodeNext( code_it, address ), CheckNextSegment( nextSegment, nextSegmentAddress ) ) {
        if( !CheckCode( *code_it ) )
            continue;
        if( code_it->extra.mnemonicMask & MM_Data_Segment )
            continue;
        if( code_it->extra.realAddress.segment > segment && ( SEG_CODE != nextSegment->extra.type || code_it->extra.realAddress.segment >= DOSBOX_SEGMENT ) )
            address = nextSegmentAddress;
        uint32_t unknown_address = address;
        uint32_t code_address = code_it->value;
        while( code_address > address && address < address_max ) {
            auto prior_address = address;
            if( address >= nextSegmentAddress && SEG_CODE != nextSegment->extra.type && unknown_address < nextSegmentAddress ) {
                address = nextSegmentAddress;
                DisassebleUnknownSection( unknown_address, address, nextSegmentAddress, segment );
                address = prior_address;
            }
            if( !CheckAdvanceNextSegment( address, segment, nextSegmentAddress, nextSegment ) )
                break;
            if( prior_address != address )
                unknown_address = address;
            if( address >= code_it->value )
                break;
            if( code_it->value - address > 0xFFFF ) {
                address = code_it->value + code_it->extra.length;
                break;
            }
            uint8_t count = MemBase[address];
            char szOpcode[25] = "";
            auto pOpcode = szOpcode;
            pOpcode += sprintf( pOpcode, "%02X ", count );
            if( count ) {
                if( count > 2U && count < 48U ) {
                    uint8_t increment = count;
                    auto pOpcodeEnd = &szOpcode[sizeof( szOpcode ) - ( pOpcode - szOpcode ) - 3U];
                    auto pBin = &MemBase[address + 1U];
                    char szOperand[256] = "";
                    auto pOperand = szOperand;
                    while( count ) {
                        if( isprint( *pBin ) ) {
                            pOperand += sprintf( pOperand, "%c", *pBin );
                            if( pOpcode < pOpcodeEnd )
                                pOpcode += sprintf( pOpcode, "%02X ", *pBin );
                            ++pBin;
                        } else
                            break;
                        --count;
                    }
                    if( !count ) {
                        CreateStringEntry( address, segment, increment, szOperand, szOpcode, pOpcode );
                        if( unknown_address < address ) {
                            if( !IsUnknownData( unknown_address, address, segment ) ) {
                                DisassebleUnknownSection( unknown_address, address, ( code_it->value < nextSegmentAddress ? code_it->value : nextSegmentAddress ), segment );
                                if( nullptr == ( *( (std::_Iterator_base12 *) &( *( ( std::_Tree_unchecked_const_iterator<std::_Tree_val<std::_Tree_simple_types<Pair<unsigned int, DecodedLine>>>, std::_Iterator_base12>* ) & code_it ) ) ) )._Myproxy )
                                    code_address = address;
                                continue;
                            }
                        }
                        address += increment;
                        unknown_address = address + 1U;
                    }
                }
            } else {
                if( ( nextSegmentAddress - address ) < 0x10 ) {
                    count = static_cast<uint8_t>( nextSegmentAddress - address );
                    uint8_t increment = count - 1U;
                    auto pOpcodeEnd = &szOpcode[sizeof( szOpcode ) - ( pOpcode - szOpcode ) - 3U];
                    auto pBin = &MemBase[address + 1U];
                    while( --count ) {
                        if( !*pBin ) {
                            if( pOpcode < pOpcodeEnd )
                                pOpcode += sprintf( pOpcode, "%02X ", *pBin );
                            ++pBin;
                        } else
                            break;
                    }
                    if( !count ) {
                        CreateAlignEntry( address, segment, increment, szOpcode, pOpcode );
                        if( unknown_address < address ) {
                            DisassebleUnknownSection( unknown_address, address, nextSegmentAddress, segment );
                            continue;
                        }
                        address += increment;
                        unknown_address = address + 1U;
                    }
                }
            }
            ++address;
        }
        if( unknown_address < address && address < address_max )
            DisassebleUnknownSection( unknown_address, address, ( code_it->value < nextSegmentAddress ? code_it->value : nextSegmentAddress ), segment );
        if( nullptr != ( *( (std::_Iterator_base12 *) &( *( ( std::_Tree_unchecked_const_iterator<std::_Tree_val<std::_Tree_simple_types<Pair<unsigned int, DecodedLine>>>, std::_Iterator_base12>* ) & code_it ) ) ) )._Myproxy && code_it->value == address )
            address += code_it->extra.length;
        if( !CheckAdvanceNextSegment( address, segment, nextSegmentAddress, nextSegment ) )
            break;
    }
}

void DasmRecursiveDisassemble( const uint32_t startAddress, const uint32_t startOffset, const bool f32bit, const bool fProtected ) {
    ZydisDecoderInit( &decoder,
        ( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
        ( f32bit ? ZYDIS_STACK_WIDTH_32 : ZYDIS_STACK_WIDTH_16 ) );
    ZydisFormatterInit( &formatter, ZYDIS_FORMATTER_STYLE_INTEL );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_FORCE_SEGMENT, ZYAN_TRUE );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_HEX_PREFIX, ZYAN_FALSE );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_UPPERCASE_PREFIXES, ZYAN_TRUE );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_IMM_PADDING, ZYDIS_PADDING_AUTO );
    ZydisFormatterSetProperty( &formatter, ZYDIS_FORMATTER_PROP_PRINT_BRANCH_SIZE, ZYAN_TRUE );
    if( cs_handle )
        cs_option( cs_handle, CS_OPT_MODE, ( f32bit ? CS_MODE_32 : CS_MODE_16 ) );
    else
        cs_open( CS_ARCH_X86, ( f32bit ? CS_MODE_32 : CS_MODE_16 ), &cs_handle );

    binarySize = MEM_TotalPages( ) * 4096; // DOS page size

    if( ordered_code.empty( ) )
        segment0_phys = startAddress - startOffset;
    FindDS( startAddress, startOffset );
    RecursiveDisassemble( startAddress, startOffset, false );
    CheckUnknown( );
    for( auto label = labels.begin( ), next = label; label != labels.end( ); label = next ) {
        ++next;
        if( label->extra.callers.empty( ) )
            RemoveLabel( *label );
        else {
            auto code_it = ordered_code.find( { label->value, {} } );
            if( code_it != ordered_code.end( ) )
                const_cast<DecodedLine &>( code_it->extra ).mnemonicMask |= ( ( label->extra.type & LABEL_CALL ) ? MM_Call_Label : MM_NONE ) | ( ( label->extra.type & LABEL_JUMP ) ? MM_Jump_Label : MM_NONE ) | ( ( label->extra.type & LABEL_DATA ) ? MM_Data_Label : MM_NONE );
        }
    }
    uint32_t processedCount = visited.size( );
    if( processedCount > lastProcessedCount )
        DEBUG_ShowMsg( "DEBUG: Disassembly finished, processed %u instruction offsets.", processedCount - lastProcessedCount );
    else if( processedCount < lastProcessedCount )
        DEBUG_ShowMsg( "DEBUG: Disassembly finished, removed %u instruction offsets.", lastProcessedCount - processedCount );
    lastProcessedCount = processedCount;
}

void DasmAnalyzeInstruction( const uint32_t address ) {
    auto code_it = ordered_code.find( { address, {} } );
    if( ordered_code.end( ) == code_it || !code_it->extra.length )
        return;
    const uint32_t offset = code_it->extra.realAddress.offset;
    const bool fRemoved = !CheckCode( *code_it );
    if( fRemoved || !code_it->extra.mnemonicMask || ( code_it->extra.mnemonicMask & MM_Data_Label ) ) {
        if( !fRemoved )
            ordered_code.extract( code_it );
        RecursiveDisassemble( address, offset, false );
        code_it = ordered_code.find( { address, {} } );
        if( ordered_code.end( ) == code_it || !code_it->extra.length || !code_it->extra.mnemonicMask || ( code_it->extra.mnemonicMask & MM_Data_Label ) )
            return;
    }
    auto &dline = code_it->extra;
    if( dline.mnemonicMask & MM_DOSBox_internal )
        return;
    auto pComm = const_cast<char *>( dline.szComment );
    auto pCommEnd = sizeof dline.szComment + pComm;
    *pComm = '-';
    pComm[1] = 0;
    ADDRESS_PAIR branch_ptr;
    for( uint8_t i = 0U; i < dline.instruction.operand_count; ++i ) {
        if( i && pComm != dline.szComment ) {
            *pComm++ = ',';
            *pComm++ = ' ';
        }
        const auto &operand = dline.operands[i];
        const uint8_t num_bits = operand.element_size ? operand.element_size : operand.size ? operand.size : 8U;
        switch( operand.type ) {
        case ZYDIS_OPERAND_TYPE_REGISTER: {
            const uint32_t reg_value = GetRegisterValue( operand.reg.value, false, dline.realAddress.segment );
            pComm += sprintf_s( pComm, pCommEnd - pComm, ( 0x10 == num_bits ? "%04X" : 0x20 == num_bits ? "%08X" : "%02X" ), reg_value );
        } break;
        case ZYDIS_OPERAND_TYPE_POINTER:
            if( !i && ( dline.mnemonicMask & MM_Branch ) )
                branch_ptr = { operand.ptr.segment, operand.ptr.offset };
            break;
        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
            switch( num_bits ) {
            case 0x10: pComm += sprintf_s( pComm, pCommEnd - pComm, "%04X", static_cast<uint16_t>( operand.imm.value.u ) ); break;
            case 0x20: pComm += sprintf_s( pComm, pCommEnd - pComm, "%08X", static_cast<uint32_t>( operand.imm.value.u ) ); break;
            default: pComm += sprintf_s( pComm, pCommEnd - pComm, "%02X", static_cast<uint8_t>( operand.imm.value.u ) ); break;
            }
            break;
        case ZYDIS_OPERAND_TYPE_MEMORY: {
            ADDRESS_PAIR ptr = CalculateRealAddress( dline.instruction, operand, dline.realAddress, false );
            uint32_t ptr_address = ptr.address( );
            pComm += sprintf_s( pComm, pCommEnd - pComm, "%04X:%04X=", ptr.segment, ptr.offset );
            const auto *pMemBase = &MemBase[ptr_address];
            switch( num_bits ) {
            case 0x10: pComm += sprintf_s( pComm, pCommEnd - pComm, "%04X", *reinterpret_cast<const uint16_t *>( pMemBase ) ); break;
            case 0x20: pComm += sprintf_s( pComm, pCommEnd - pComm, "%08X", *reinterpret_cast<const uint32_t *>( pMemBase ) ); break;
            default: pComm += sprintf_s( pComm, pCommEnd - pComm, "%02X", *pMemBase ); break;
            }
            if( ValidateAddress( ptr_address, ptr ) )
                CreateDataEntry( ptr_address, ptr.segment, dline.address, dline.realAddress.segment, operand );
            if( !i && ( dline.mnemonicMask & MM_Branch ) ) {
                if( 0x20 == num_bits )
                    branch_ptr.segment = reinterpret_cast<const uint16_t *>( pMemBase )[1];
                else
                    branch_ptr.segment = dline.realAddress.segment;
                branch_ptr.offset = *reinterpret_cast<const uint16_t *>( pMemBase );
            }
        } break;
        default: break;
        }
    }
    if( dline.mnemonicMask & MM_Branch ) {
        bool fBranch = dline.mnemonicMask & ( MM_JMP | MM_CALL );
        if( !fBranch ) {
            switch( dline.instruction.mnemonic ) {
            case ZYDIS_MNEMONIC_JB: fBranch = TFLG_B; break;
            case ZYDIS_MNEMONIC_JBE: fBranch = TFLG_BE; break;
            case ZYDIS_MNEMONIC_JCXZ: fBranch = reg_cx == 0U; break;
            case ZYDIS_MNEMONIC_JECXZ: fBranch = reg_ecx == 0U; break;
            case ZYDIS_MNEMONIC_JL: fBranch = TFLG_L; break;
            case ZYDIS_MNEMONIC_JLE: fBranch = TFLG_LE; break;
            case ZYDIS_MNEMONIC_JNB: fBranch = TFLG_NB; break;
            case ZYDIS_MNEMONIC_JNBE: fBranch = TFLG_NBE; break;
            case ZYDIS_MNEMONIC_JNL: fBranch = TFLG_NL; break;
            case ZYDIS_MNEMONIC_JNLE: fBranch = TFLG_NLE; break;
            case ZYDIS_MNEMONIC_JNO: fBranch = TFLG_NO; break;
            case ZYDIS_MNEMONIC_JNP: fBranch = TFLG_NP; break;
            case ZYDIS_MNEMONIC_JNS: fBranch = TFLG_NS; break;
            case ZYDIS_MNEMONIC_JNZ: fBranch = TFLG_NZ; break;
            case ZYDIS_MNEMONIC_JO: fBranch = TFLG_O; break;
            case ZYDIS_MNEMONIC_JP: fBranch = TFLG_P; break;
            case ZYDIS_MNEMONIC_JS: fBranch = TFLG_S; break;
            case ZYDIS_MNEMONIC_JZ: fBranch = TFLG_Z; break;
            case ZYDIS_MNEMONIC_LOOP: fBranch = ( cpu.code.big ? reg_ecx : reg_cx ) != 0U; break;
            case ZYDIS_MNEMONIC_LOOPE: fBranch = ( cpu.code.big ? reg_ecx : reg_cx ) != 0U && TFLG_Z; break;
            case ZYDIS_MNEMONIC_LOOPNE: fBranch = ( cpu.code.big ? reg_ecx : reg_cx ) != 0U && TFLG_NZ; break;
            default: break;
            }
        }
        uint32_t branch_address = branch_ptr.address( );
        if( fBranch ) {
            if( address > branch_address )
                strcpy_s( pComm, pCommEnd - pComm, " <-" );
            else
                strcpy_s( pComm, pCommEnd - pComm, " ->" );
        } else
            strcpy_s( pComm, pCommEnd - pComm, " ><" );
        pComm += 3;
        if( ValidateAddress( branch_address, branch_ptr ) ) {
            CreateLabel( branch_address, branch_ptr.segment, dline.address, dline.realAddress.segment, ( dline.mnemonicMask & MM_CALL ) ? LABEL_CALL : LABEL_JUMP );
            RecursiveDisassemble( branch_address, branch_ptr.offset, false );
        }
    } else if( ZYDIS_MNEMONIC_POPA == dline.instruction.mnemonic || ZYDIS_MNEMONIC_POPAD == dline.instruction.mnemonic ) {
        const uint32_t sp_address = ADDRESS_PAIR::Address( { RealSegValue( ss ), ( cpu.code.big ? reg_esp : reg_sp ) } );
        if( ZYDIS_MNEMONIC_POPA == dline.instruction.mnemonic ) {
            const uint16_t *pMemBase = reinterpret_cast<const uint16_t *>( &MemBase[sp_address] );
            pComm += sprintf_s( pComm, pCommEnd - pComm, " -> AX=%04X, CX=%04X, DX=%04X, BX=%04X, BP=%04X, SI=%04X, DI=%04X", pMemBase[7], pMemBase[6], pMemBase[5], pMemBase[4], pMemBase[2], pMemBase[1], *pMemBase );
        } else {
            const uint32_t *pMemBase = reinterpret_cast<const uint32_t *>( &MemBase[sp_address] );
            pComm += sprintf_s( pComm, pCommEnd - pComm, " -> EAX=%08X, ECX=%08X, EDX=%08X, EBX=%08X, EBP=%08X, ESI=%08X, EDI=%08X", pMemBase[7], pMemBase[6], pMemBase[5], pMemBase[4], pMemBase[2], pMemBase[1], *pMemBase );
        }
    }
}

void DasmUnDisassemble( uint32_t address ) {
    if( visited.extract( address ).empty( ) )
        return;
    const auto nhc = calls.extract( { address, {} } );
    if( !nhc.empty( ) ) {
        std::set<Pair<uint32_t, uint16_t>> callers;
        auto label = labels.find( { nhc.value( ).extra, { {}, {}, {}, callers } } );
        if( label != labels.end( ) )
            label->extra.callers.extract( { address, {} } );
    }
    const auto nh = ordered_code.extract( { address, {} } );
    if( !nh.empty( ) ) {
        ADDRESS_PAIR address_pair = nh.value( ).extra.realAddress;
        for( uint8_t count = nh.value( ).extra.length; count; --count ) {
            auto entry = ordered_code.insert( { address, DecodedLine( address_pair, address ) } );
            if( entry.second ) { // insert successful
                DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
                dline.pMnemonic = dline.szInstruction;
                dline.instruction = data_instruction;
                dline.instruction.length = dline.length = 1U;
                dline.instruction.operand_count = dline.instruction.operand_count_visible = 0U;
                dline.mnemonicMask = MM_NONE;
                *dline.opCode = MemBase[address];
                sprintf( dline.szOpcode, "%02X ", MemBase[address] );
            }
            ++address;
            ++address_pair.offset;
        }
    }
}

void DasmShutdown( ) {
    if( cs_handle ) {
        cs_close( &cs_handle );
        cs_handle = 0U;
    }
}
#endif // C_DEBUGGER
