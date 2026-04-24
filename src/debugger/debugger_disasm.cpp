#include "debugger_disasm.h"

#if C_DEBUGGER
#include "cpu/callback.h"
#include "cpu/cpu.h"
#include "hardware/memory.h"
#include <vector>
#include <unordered_set>

#include <fstream>

// Custom hash specialization for Pair<T1, T2>
namespace std {
    template <typename T1, typename T2>
    struct hash<Pair<T1, T2>> {
        std::size_t operator()( const Pair<T1, T2> &p ) const noexcept {
            return std::hash<T1>{}( p.value );
        }
    };
}

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
    return visited.count( address );
}

bool AddressVisited( const ADDRESS_PAIR &address_pair ) {
    return AddressVisited( address_pair.offset + ( address_pair.segment << 4 ) );
}

uint8_t DasmI386( char *decodedInstruction, char *&pOperands, const uint32_t pc, const uint32_t ip, const bool f32bit, const bool fProtected ) {
    pOperands = nullptr;
	ZydisDisassembledInstruction instruction;
	if( ZYAN_SUCCESS( ZydisDisassembleIntel(
		( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
		ip, MemBase + pc, ZYDIS_MAX_OPERAND_COUNT + 1, &instruction ) ) ) {
		strcpy( decodedInstruction, instruction.text );
        pOperands = decodedInstruction;
        while( *pOperands && *pOperands != ' ' ) ++pOperands;
	} else { // invalid instruction, use db xx
		sprintf( decodedInstruction, "db %02X", MemBase[pc] );
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

static bool InvalidAddress( uint32_t &address, const ADDRESS_PAIR &ptr ) {
    if( address < segment0_phys || address >= binarySize ) { // Skip invalid
        Descriptor desc;
        if( cpu.gdt.GetDescriptor( ptr.segment, desc ) )
            address = ptr.offset + ( desc.GetBase( ) << 4 );
        if( address < segment0_phys || address >= binarySize ) // Still invalid, skip
            return true;
    }
    return false;
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
        const_cast<DecodedLine &>( it->extra ).mnemonicMask &= static_cast<MNEMONIC_MASK>( ~( MM_Call_Label | MM_Jump_Label | MM_Data_Label ) );
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
        auto label = labels.find( { nhc.value( ).extra, { LABEL_ALL, 0U, callers } } );
        if( label != labels.end( ) )
            label->extra.callers.extract( { code.extra.address, {} } );
    }
    ordered_code.extract( code );
}

static bool CheckCode( const Pair<uint32_t, DecodedLine> &code ) {
    if( !memcmp( &MemBase[code.extra.address], code.extra.opCode, code.extra.length ) )
        return true;
    if( code.extra.mnemonicMask & MM_Data_Label ) {
        auto &dline = const_cast<Pair<uint32_t, DecodedLine> &>( code ).extra;
        memcpy( dline.opCode, &MemBase[dline.address], dline.length );
        char *pOpCode = dline.szOpcode;
        for( uint8_t i = 0U; i < dline.length; ++i )
            pOpCode += sprintf( pOpCode, "%02X ", MemBase[dline.address + i] );
        switch( dline.length ) {
        case 2U:
            sprintf( const_cast<char *>( dline.pOperands ), "0x%02X%02X", MemBase[dline.address + 1U], MemBase[dline.address] );
            dline.operands[0].imm.value.u = reinterpret_cast<const uint16_t &>( MemBase[dline.address] );
            break;
        case 4U:
            sprintf( const_cast<char *>( dline.pOperands ), "0x%02X%02X%02X%02X", MemBase[dline.address + 3U], MemBase[dline.address + 2U], MemBase[dline.address + 1U], MemBase[dline.address] );
            dline.operands[0].imm.value.u = reinterpret_cast<const uint32_t &>( MemBase[dline.address] );
            break;
        default:
            sprintf( const_cast<char *>( dline.pOperands ), "0x%02X", MemBase[dline.address] );
            dline.operands[0].imm.value.u = MemBase[dline.address];
        }
        return true;
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
        for( auto label = labels.lower_bound( { segmentAddress, { LABEL_ALL, 0U, callers } } ), next = label, codeEnd = labels.upper_bound( { nextSegmentAddress, { LABEL_ALL, 0U, callers } } ); label != labels.end( ) && label != codeEnd; label = next ) {
            ++next;
            if( segment->value == label->extra.segment )
                RemoveLabel( *label );
        }
    }
    ordered_segments.extract( segment );
}

static void CheckSegments( const ADDRESS_PAIR &realAddress ) {
    const auto segment = ordered_segments.find( { realAddress.segment, {} } );
    if( segment != ordered_segments.end( ) ) {
        const auto address = realAddress.address( );
        for( auto nextSegment = segment; ++nextSegment != ordered_segments.end( ); nextSegment = segment ) {
            if( address >= static_cast<uint32_t>( nextSegment->value ) << 4 )
                RemoveSegment( nextSegment );
            else
                break;
        }
    }
}

static uint32_t RecursiveDisassemble( const uint32_t startOffset, const uint32_t ip ) {
    uint32_t primary_length = 0U;
    std::vector<ADDRESS_PAIR> toVisit{ { static_cast<uint16_t>( ( startOffset - ip ) >> 4 ), ip } };
    std::unordered_set<uint32_t> added;

    const auto binary = MemBase;
    uint8_t ah = 0U; // for int 21h tracking

    bool fPrimary = true;
    while( !toVisit.empty( ) ) {
        ADDRESS_PAIR realAddress = toVisit.back( );
        toVisit.pop_back( );

        if( !ordered_segments.count( { realAddress.segment, {} } ) )
            ordered_segments.insert( { realAddress.segment, { SEG_CODE, 0U } } );

        CheckSegments( realAddress );

        uint32_t address = realAddress.address( );
        while( address < binarySize ) {
            if( visited.count( address ) && CheckAddress( address ) )
                break;
            visited.insert( address );

            if( binary[address] == 0 && binary[address + 1] == 0 ) // 00 00
                break;

            ordered_code.extract( { address, {} } );
            auto entry = ordered_code.insert( { address, DecodedLine( realAddress, address ) } );
            if( !entry.second ) // insert failed
                break;
            DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
            if( ZYAN_SUCCESS( ZydisDecoderDecodeFull( &decoder, &binary[address], binarySize - address, &dline.instruction, dline.operands ) ) ) {
                dline.length = dline.instruction.length;
                memcpy( dline.opCode, &binary[address], dline.length );
                char *pOpCode = dline.szOpcode;
                for( auto i = 0; i < dline.length; ++i ) {
                    pOpCode += sprintf( pOpCode, "%02X ", binary[address + i] );
                    if( i ) {
                        ordered_code.extract( { address + i, {} } );
                        visited.extract( address + i );
                    }
                }
                cs_disasm( cs_handle, &binary[address], binarySize - address, realAddress.offset, 1, &dline.cs_instruction );
                //ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, dline.szInstruction, sizeof( dline.szInstruction ), realAddress.offset, ZYAN_NULL );
                strcpy( dline.szInstruction, dline.cs_instruction->mnemonic );
                dline.pOperands = dline.cs_instruction->op_str;
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
                case ZYDIS_MNEMONIC_JKNZD:
                case ZYDIS_MNEMONIC_JKZD:
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
                case ZYDIS_MNEMONIC_JRCXZ:
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
                case ZYDIS_MNEMONIC_POPFQ:
                case ZYDIS_MNEMONIC_PUSH:
                case ZYDIS_MNEMONIC_PUSHA:
                case ZYDIS_MNEMONIC_PUSHAD:
                case ZYDIS_MNEMONIC_PUSHF:
                case ZYDIS_MNEMONIC_PUSHFD:
                case ZYDIS_MNEMONIC_PUSHFQ:
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
                default:
                    break;
                }
                if( dline.instruction.attributes & ( ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE ) )
                    dline.mnemonicMask |= MM_REP;
                if( dline.instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT )
                    dline.mnemonicMask |= MM_Has_Segment;
                // check operands for memory references...
                for( uint8_t i = 0U; i < NUM_MEM_OPS && i < dline.instruction.operand_count; ++i ) {
                    const auto &op = dline.operands[i];
                    if( ZYDIS_OPERAND_TYPE_MEMORY == op.type ) {
                        dline.mnemonicMask |= MM_Memory_Access;
                        dline.mem_access[i].size = op.element_size;
                        dline.mem_access[i].segment_id = op.mem.segment;
                        dline.mem_access[i].base_id = op.mem.base;
                        dline.mem_access[i].disp.has_displacement = op.mem.disp.has_displacement;
                        dline.mem_access[i].disp.value = op.mem.disp.value;

                        if( ZYDIS_REGISTER_CS == op.mem.segment && ZYDIS_REGISTER_NONE == op.mem.base && op.mem.disp.has_displacement ) {
                            ADDRESS_PAIR ptr = { dline.realAddress.segment, static_cast<uint32_t>( op.mem.disp.value ) };
                            uint32_t addr = ptr.offset + ( ptr.segment << 4 );
                            if( InvalidAddress( addr, ptr ) )
                                continue;
                            std::set<Pair<uint32_t, uint16_t>> callers, *pcallers = nullptr;
                            auto label = labels.find( { addr, { LABEL_ALL, ptr.segment, callers } } );
                            if( label != labels.end( ) )
                                pcallers = &label->extra.callers;
                            else {
                                auto label = labels.insert( { addr, LabelInfo( LABEL_DATA, ptr.segment, *new std::set<Pair<uint32_t, uint16_t>> ) } );
                                if( label.second ) // insert success
                                    pcallers = &label.first->extra.callers;
                            }
                            if( pcallers ) {
                                pcallers->insert( { dline.address, dline.realAddress.segment } );
                                calls.insert( { dline.address, addr } );
                            }

                            DecodedLine *dlineData = nullptr;
                            if( !visited.count( addr ) )
                                ordered_code.extract( { addr, {} } );
                            const auto it = ordered_code.find( { addr, {} } );
                            if( it == ordered_code.end( ) ) {
                                auto entry = ordered_code.insert( { addr, DecodedLine( ptr, addr ) } );
                                if( entry.second ) { // insert successful
                                    added.insert( addr );
                                    visited.insert( addr );
                                    dlineData = &const_cast<DecodedLine &>( entry.first->extra );
                                    dlineData->instruction = data_instruction;
                                    dlineData->instruction.length = dlineData->length = op.element_size >> 3U;
                                    dlineData->instruction.operand_width = op.element_size;
                                    dlineData->operands[0] = imm_operand;
                                    dlineData->operands[0].imm = { false, false, { 0U } };
                                    dlineData->pOperands = &dlineData->szInstruction[3];
                                    dlineData->mnemonicMask = MM_Data_Label;
                                }
                            } else if( op.element_size > it->extra.instruction.operand_width ) {
                                dlineData = &const_cast<DecodedLine &>( it->extra );
                                dlineData->instruction.length = dlineData->length = op.element_size >> 3U;
                                dlineData->instruction.operand_width = op.element_size;
                            }
                            if( dlineData ) {
                                memcpy( dlineData->opCode, &binary[addr], dlineData->length );
                                char *pOpCode = dlineData->szOpcode;
                                for( uint8_t i = 0U; i < dlineData->length; ++i ) {
                                    pOpCode += sprintf( pOpCode, "%02X ", binary[addr + i] );
                                    if( i ) {
                                        ordered_code.extract( { addr + i, {} } );
                                        visited.extract( addr + i );
                                    }
                                }
                                switch( op.element_size ) {
                                case 0x10:
                                    sprintf_s( dlineData->szInstruction, sizeof( dlineData->szInstruction ), "dw 0x%02X%02X", binary[addr + 1U], binary[addr] );
                                    dlineData->operands[0].imm.value.u = reinterpret_cast<const uint16_t &>( binary[addr] );
                                    break;
                                case 0x20:
                                    sprintf_s( dlineData->szInstruction, sizeof( dlineData->szInstruction ), "dd 0x%02X%02X%02X%02X", binary[addr + 3U], binary[addr + 2U], binary[addr + 1U], binary[addr] );
                                    dlineData->operands[0].imm.value.u = reinterpret_cast<const uint32_t &>( binary[addr] );
                                    break;
                                default:
                                    sprintf_s( dlineData->szInstruction, sizeof( dlineData->szInstruction ), "db 0x%02X", binary[addr] );
                                    dlineData->operands[0].imm.value.u = binary[addr];
                                }
                                dlineData->szInstruction[2] = 0;
                            }
                        }
                    }
                }
                if( dline.mnemonicMask & MM_Branch ) {
                    ADDRESS_PAIR ptr = { static_cast<uint16_t>( -1 ), static_cast<uint32_t>( -1 ) };
                    for( uint8_t i = 0U; i < dline.instruction.operand_count; ++i ) {
                        const auto &op = dline.operands[i];
                        switch( op.type ) {
                        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                            ptr.segment = realAddress.segment;
                            if( op.imm.is_relative )
                                ptr.offset = ( op.imm.is_signed ? op.imm.value.s : op.imm.value.u ) + realAddress.offset;
                            else
                                ptr.offset = ( op.imm.is_signed ? op.imm.value.s : op.imm.value.u );
                            break;
                        case ZYDIS_OPERAND_TYPE_POINTER:
                            ptr = { op.ptr.segment, op.ptr.offset };
                            break;
                        default:
                            break;
                        }
                    }
                    if( ptr.segment != static_cast<uint16_t>( -1 ) && ptr.offset != static_cast<uint32_t>( -1 ) ) {
                        uint32_t addr = ptr.offset + ( ptr.segment << 4 );
                        if( InvalidAddress( addr, ptr ) )
                            continue;
                        bool existingLabel = true;
                        if( !added.count( addr ) ) {
                            added.insert( addr );
                            if( !visited.count( addr ) )
                                toVisit.push_back( ptr );
                            auto label = labels.insert( { addr, LabelInfo( ( dline.mnemonicMask & MM_CALL ) ? LABEL_CALL : LABEL_JUMP, ptr.segment, *new std::set<Pair<uint32_t, uint16_t>> ) } );
                            if( label.second ) { // insert success
                                label.first->extra.callers.insert( { dline.address, dline.realAddress.segment } );
                                calls.insert( { dline.address, addr } );
                                existingLabel = false;
                            } else {
                                std::set<Pair<uint32_t, uint16_t>> callers;
                                auto label = labels.find( { addr, { LABEL_BOTH, ptr.segment, callers } } );
                                if( label != labels.end( ) )
                                    const_cast<LABEL_MASK &>( label->extra.type ) = LABEL_BOTH;
                            }
                        }
                        if( existingLabel ) {
                            std::set<Pair<uint32_t, uint16_t>> callers;
                            auto label = labels.find( { addr, { LABEL_BOTH, ptr.segment, callers } } );
                            if( label != labels.end( ) ) {
                                label->extra.callers.insert( { dline.address, dline.realAddress.segment } );
                                calls.insert( { dline.address, addr } );
                            }
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
                    if( dline.instruction.operand_count == 2 ) {
                        if( dline.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ( dline.operands[0].reg.value == ZYDIS_REGISTER_AH || dline.operands[0].reg.value == ZYDIS_REGISTER_AX ) ) {
                            if( dline.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ) {
                                if( dline.operands[0].reg.value == ZYDIS_REGISTER_AH )
                                    ah = static_cast<uint8_t>( dline.operands[1].imm.value.u );
                                else
                                    ah = static_cast<uint8_t>( dline.operands[1].imm.value.u >> 8U );
                            }
                        }
                    }
                }
            } else { // Decode failed...
                if( binary[address] == 0xFE && ( ( binary[address + 1] >> 3 ) == 0x07 ) ) { // DOSBox internal callback
                    const uint16_t &dw = *reinterpret_cast<const uint16_t *>( &binary[address + 2] );
                    dline.instruction = callback_instruction;
                    dline.operands[0] = imm_operand;
                    dline.operands[0].imm = { false, false, { dw } };
                    char *pOpCode = dline.szOpcode;
                    for( auto i = 0; i < dline.length; ++i )
                        pOpCode += sprintf( pOpCode, "%02X ", binary[address + i] );
                    sprintf_s( dline.szInstruction, sizeof( dline.szInstruction ), "callback 0x%02X", dw );
                    strcat( dline.szComment, CALLBACK_GetDescription( dw ) );
                    realAddress += dline.length;
                    address += dline.length;
                } else { // Treat as data; step forward 1 byte
                    sprintf( dline.szOpcode, "%02X", binary[address] );
                    ++address;
                    ++realAddress;
                }
            }
        }
        if( fPrimary ) {
            fPrimary = false;
            primary_length = address - startOffset;
        }
    }
    return primary_length;
}

struct Unknown {
    uint32_t address;
    uint32_t next_address;
    uint16_t segment;
};

static bool FindNextSegment( uint16_t segment, std::set<Pair<uint16_t, SegmentInfo>>::iterator &nextSegment ) {
    nextSegment = ordered_segments.find( { segment, {} } );
    if( ordered_segments.end( ) == nextSegment )
        return false;
    ++nextSegment;
    if( ordered_segments.end( ) == nextSegment )
        return false;
    return true;
}

static const uint16_t DOSBOX_SEGMENT = 0xC887;

static bool CheckAdvanceNextSegment( uint32_t &address, ADDRESS_PAIR &realAddress, uint32_t &nextSegmentAddress, std::set<Pair<uint16_t, SegmentInfo>>::iterator &nextSegment ) {
    if( address >= nextSegmentAddress ) {
        while( SEG_CODE != nextSegment->extra.type ) {
            ++nextSegment;
            if( ordered_segments.end( ) == nextSegment )
                return false;
        }
        if( nextSegment->value >= DOSBOX_SEGMENT )
            return false;
        realAddress = { nextSegment->value, 0U };
        address = realAddress.address( );
        ++nextSegment;
        if( ordered_segments.end( ) == nextSegment )
            return false;
        nextSegmentAddress = nextSegment->value << 4U;
    }
    return true;
}

static void CheckUnknown( const bool fIdentify ) { // attempt to identify non-disassembled parts...
    for( bool fIdentifyLoop = true; fIdentifyLoop; ) {
        fIdentifyLoop = false;
        const auto binary = MemBase;
        std::set<Pair<uint16_t, SegmentInfo>>::iterator nextSegment;
        if( !FindNextSegment( static_cast<uint16_t>( segment0_phys >> 4 ), nextSegment ) )
            break;
        uint32_t nextSegmentAddress = nextSegment->value << 4U;
        ADDRESS_PAIR realAddress = { static_cast<uint16_t>( segment0_phys >> 4 ), 0U };
        uint32_t address = realAddress.address( );
        std::vector<Unknown> unknowns;
        for( const auto &code : ordered_code ) {
            if( code.extra.realAddress.segment > realAddress.segment && ( SEG_STACK == nextSegment->extra.type || code.extra.realAddress.segment >= DOSBOX_SEGMENT ) )
                address = nextSegmentAddress;
            uint32_t unknown_address = address;
            while( code.value > address && nextSegmentAddress > address ) {
                uint8_t count = binary[address];
                char szOpcode[25] = "";
                auto pOpcode = szOpcode;
                pOpcode += sprintf( pOpcode, "%02X ", count );
                if( count ) {
                    if( fIdentify ? count > 3U : count ) {
                        uint8_t increment = count;
                        auto pOpcodeEnd = &szOpcode[sizeof( szOpcode ) - ( pOpcode - szOpcode ) - 3U];
                        auto pBin = &binary[address + 1U];
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
                            auto entry = ordered_code.insert( { address, DecodedLine( realAddress, address ) } );
                            if( entry.second ) { // insert successful
                                DecodedLine &dlineData = const_cast<DecodedLine &>( entry.first->extra );
                                dlineData.instruction = data_instruction;
                                dlineData.mnemonicMask = MM_Data_Label;
                                dlineData.instruction.length = dlineData.length = increment + 1U;
                                memcpy( dlineData.opCode, &binary[address], dlineData.length );
                                dlineData.instruction.operand_width = 8U;
                                dlineData.operands[0] = imm_operand;
                                dlineData.operands[0].imm = { false, false, { increment } };
                                sprintf( dlineData.szInstruction, "db %u,'%s'", increment, szOperand );
                                dlineData.szInstruction[2] = 0;
                                dlineData.pOperands = &dlineData.szInstruction[3];
                                if( increment > 6U ) {
                                    pOpcode[-4] = '.';
                                    pOpcode[-3] = '.';
                                    pOpcode[-2] = '.';
                                }
                                strcpy( dlineData.szOpcode, szOpcode );
                            }
                            if( fIdentify ) {
                                visited.insert( address );
                                if( unknown_address < address )
                                    unknowns.push_back( { unknown_address, address, realAddress.segment } );
                            }
                            address += increment;
                            realAddress += increment;
                            unknown_address = address + 1U;
                        }
                    }
                } else if( fIdentify ) {
                    if( ( nextSegmentAddress - address ) < 0x10 ) {
                        count = static_cast<uint8_t>( nextSegmentAddress - address );
                        uint8_t increment = count - 1U;
                        auto pOpcodeEnd = &szOpcode[sizeof( szOpcode ) - ( pOpcode - szOpcode ) - 3U];
                        auto pBin = &binary[address + 1U];
                        while( --count ) {
                            if( !*pBin ) {
                                if( pOpcode < pOpcodeEnd )
                                    pOpcode += sprintf( pOpcode, "%02X ", *pBin );
                                ++pBin;
                            } else
                                break;
                        }
                        if( !count ) {
                            //visited.insert( address );
                            auto entry = ordered_code.insert( { address, DecodedLine( realAddress, address ) } );
                            if( entry.second ) { // insert successful
                                DecodedLine &dlineData = const_cast<DecodedLine &>( entry.first->extra );
                                dlineData.instruction = data_instruction;
                                dlineData.mnemonicMask = MM_ALIGN;
                                dlineData.instruction.length = dlineData.length = increment + 1U;
                                memcpy( dlineData.opCode, &binary[address], dlineData.length );
                                dlineData.instruction.operand_count = dlineData.instruction.operand_count_visible = 0U;
                                strcpy( dlineData.szInstruction, "align" );
                                if( increment > 6U ) {
                                    pOpcode[-4] = '.';
                                    pOpcode[-3] = '.';
                                    pOpcode[-2] = '.';
                                }
                                strcpy( dlineData.szOpcode, szOpcode );
                            }
                            if( unknown_address < address )
                                unknowns.push_back( { unknown_address, address, realAddress.segment } );
                            address += increment;
                            realAddress += increment;
                            unknown_address = address + 1U;
                        }
                    }
                }
                if( !fIdentify && address == unknown_address ) {
                    auto entry = ordered_code.insert( { address, DecodedLine( realAddress, address ) } );
                    if( entry.second ) { // insert successful
                        DecodedLine &dlineData = const_cast<DecodedLine &>( entry.first->extra );
                        dlineData.instruction = data_instruction;
                        dlineData.instruction.length = dlineData.length = 1U;
                        dlineData.instruction.operand_count = dlineData.instruction.operand_count_visible = 0U;
                        dlineData.mnemonicMask = MM_NONE;

                        *dlineData.opCode = binary[address];
                        auto pOpcode = dlineData.szOpcode;
                        pOpcode += sprintf( pOpcode, "%02X ", binary[address] );
                        if( isprint( binary[address] ) )
                            sprintf( dlineData.szComment, "%c", binary[address] );
                    }
                }
                ++address;
                ++realAddress;
            }
            if( fIdentify && unknown_address < address )
                unknowns.push_back( { unknown_address, address, realAddress.segment } );
            if( code.value == address ) {
                address += code.extra.length;
                realAddress += code.extra.length;
            }
            if( !CheckAdvanceNextSegment( address, realAddress, nextSegmentAddress, nextSegment ) )
                break;
        }
        if( !fIdentify )
            break;
        for( const auto &unknown : unknowns ) {
            auto unknown_address = unknown.address;
            while( !binary[unknown_address] )
                ++unknown_address;
            if( unknown_address >= unknown.next_address )
                continue;
            address = unknown_address;
            while( address < unknown.next_address ) {
                ZydisDecodedInstruction instruction;
                ZydisDecoderContext context;
                if( ZYAN_SUCCESS( ZydisDecoderDecodeInstruction( &decoder, &context, &binary[address], binarySize - address, &instruction ) ) )
                    address += instruction.length;
                else
                    break;
            }
            if( address == unknown.next_address ) {
                while( unknown_address < unknown.next_address ) {
                    const auto run_length = RecursiveDisassemble( unknown_address, unknown_address - ( unknown.segment << 4U ) );
                    if( !run_length )
                        break;
                    unknown_address += run_length;
                }
                if( unknown_address == unknown.next_address )
                    fIdentifyLoop = true;
            }
        }
        unknowns.clear( );
    }
}

// Recursive disassembly function (credit: CoPilot)
void DasmRecursiveDisassemble( const uint32_t startOffset, const uint32_t ip, const bool f32bit, const bool fProtected ) {
    ZydisDecoderInit( &decoder,
        ( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
        ( f32bit ? ZYDIS_STACK_WIDTH_32 : ZYDIS_STACK_WIDTH_16 ) );
    ZydisFormatterInit( &formatter, ZYDIS_FORMATTER_STYLE_INTEL );
    if( cs_handle )
        cs_option( cs_handle, CS_OPT_MODE, ( f32bit ? CS_MODE_32 : CS_MODE_16 ) );
    else
        cs_open( CS_ARCH_X86, ( f32bit ? CS_MODE_32 : CS_MODE_16 ), &cs_handle );

    binarySize = MEM_TotalPages( ) * 4096; // DOS page size

    if( ordered_code.empty( ) )
        segment0_phys = startOffset - ip;
    RecursiveDisassemble( startOffset, ip );
    for( auto code = ordered_code.begin( ), next = code; code != ordered_code.end( ); code = next ) {
        ++next;
        CheckCode( *code );
    }
    CheckUnknown( true );
    CheckUnknown( false );
    for( auto label = labels.begin( ), next = label; label != labels.end( ); label = next ) {
        ++next;
        if( label->extra.callers.empty( ) )
            RemoveLabel( *label );
        else {
            auto it = ordered_code.find( { label->value, {} } );
            if( it != ordered_code.end( ) )
                const_cast<DecodedLine &>( it->extra ).mnemonicMask |= ( label->extra.type & LABEL_CALL ? MM_Call_Label : MM_NONE ) | ( label->extra.type & LABEL_JUMP ? MM_Jump_Label : MM_NONE ) | ( label->extra.type & LABEL_DATA ? MM_Data_Label : MM_NONE );
        }
    }
    DEBUG_ShowMsg( "DEBUG: Disassembly finished, processed %llu instruction offsets.", visited.size( ) - lastProcessedCount );
    lastProcessedCount = visited.size( );
}

void DasmUnDisassemble( uint32_t address ) {
    if( visited.extract( address ).empty( ) )
        return;
    const auto nhc = calls.extract( { address, {} } );
    if( !nhc.empty( ) ) {
        std::set<Pair<uint32_t, uint16_t>> callers;
        auto label = labels.find( { nhc.value( ).extra, { LABEL_ALL, 0U, callers } } );
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

struct Proc {
    uint32_t base_offset;
    char szProc[13];
};
struct Label {
    uint32_t base_offset;
    char szLabel[13];
};

void Dasm_WriteFile( ) {
    DEBUG_ShowMsg( "DEBUG: Creating asm file Code.asm\n" );

    std::ofstream out( "Code.asm" );
    if( !out.is_open( ) ) {
        DEBUG_ShowMsg( "DEBUG: Failed.\n" );
        return;
    }
    char indent[] = "                ";
    out << indent << ".model large" << std::endl << indent << ".486p" << std::endl << std::endl;

    ZydisFormatter formatter;
    ZydisFormatterInit( &formatter, ZYDIS_FORMATTER_STYLE_INTEL_MASM );

    uint16_t currentSegment = -1;
    char szSegment[8] = "";
    using std::setw;
    for( const auto &entry : ordered_code ) {
        const auto &dline = entry.extra;
        if( dline.realAddress.segment != currentSegment ) {
            if( currentSegment != static_cast<uint16_t>( -1 ) )
                out << setw( sizeof( indent ) ) << szSegment << "ends" << std::endl << std::endl;
            currentSegment = dline.realAddress.segment;
            sprintf( szSegment, "seg%04X", currentSegment );
            out << setw( sizeof( indent ) ) << szSegment << "segment para public 'CODE' use16" << std::endl \
                << indent << "assume cs:seg000, ds:@DATA" << std::endl << std::endl;
        }
        char szFormattedInstruction[128];
        char const *szOperator = szFormattedInstruction;
        char const *szOperands = nullptr;
        ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, szFormattedInstruction, sizeof( szFormattedInstruction ), dline.realAddress.offset, ZYAN_NULL );
        char *cptr = szFormattedInstruction;
        while( *cptr && *cptr != ' ' ) ++cptr;
        if( cptr ) {
            *cptr++ = 0;
            szOperands = cptr;
        }
        /*if( calls.count( { dline.base_offset, { dline.address.segment, { } } } ) ) {
            auto entry = procs.insert( { dline.base_offset, Proc( dline.base_offset ) } );
            Proc &sub = const_cast<Proc &>( entry.first->second );
            sprintf( sub.szProc, "sub_%08X", dline.base_offset );
        }*/
        out << indent << setw( 8 ) << szOperator << szOperands << std::endl;
    }
    out.close( );
    DEBUG_ShowMsg( "DEBUG: Done.\n" );
}
#endif // C_DEBUGGER
