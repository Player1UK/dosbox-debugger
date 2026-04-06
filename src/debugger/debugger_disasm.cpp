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
const DecodedLine &DecodedLine::last( ) {
    currentLine = ordered_code.end( );
    --currentLine;
    return currentLine->extra;
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

bool AddressVisited( uint16_t segment, uint32_t offset ) {
    return AddressVisited( offset + ( segment << 4 ) );
}

uint32_t DasmI386( char *buffer, const uint32_t pc, const uint32_t ip, const bool f32bit, const bool fProtected ) {
	ZydisDisassembledInstruction instruction;
	if( ZYAN_SUCCESS( ZydisDisassembleIntel(
		( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
		ip, MemBase + pc, ZYDIS_MAX_OPERAND_COUNT + 1, &instruction ) ) ) {
		sprintf( buffer, "%s", instruction.text );
	} else { // invalid instruction, use db xx
		sprintf( buffer, "db %02X", MemBase[pc] );
		return 1U;
	}
	return instruction.info.length;
}

static uint32_t lastProcessedCount = 0U;

void DasmReset( ) {
    labels.clear( );
    ordered_segments.clear( );
    visited.clear( );
    ordered_code.clear( );
    currentLine = ordered_code.end( );
    lastProcessedCount = 0U;
}

static ZydisDecodedInstruction callback_instruction = {
    ZYDIS_MACHINE_MODE_REAL_16,         // machine_mode
    ZYDIS_MNEMONIC_INVALID,             // mnemonic
    4U,                                 // length
    ZYDIS_INSTRUCTION_ENCODING_LEGACY,  // encoding
    ZYDIS_OPCODE_MAP_DEFAULT,           // opcode_map
    0xFE,                               // opcode
    0U,                                 // stack_width
    2U,                                 // operand_width
    4U,                                 // address_width
    1U,                                 // operand_count
    1U,                                 // operand_count_visible
    0U,                                 // attributes
    nullptr,                            // cpu_flags
    nullptr,                            // fpu_flags
    { },                                // avx
    { },                                // meta
    { }                                 // raw
};
static ZydisDecodedOperand callback_operand = {
    0U,                                 // id
    ZYDIS_OPERAND_VISIBILITY_EXPLICIT,  // visibility
    0U,                                 // actions
    ZYDIS_OPERAND_ENCODING_UIMM16,      // encoding
    16U,                                // size
    ZYDIS_ELEMENT_TYPE_UINT,            // element_type
    2U,                                 // element_size
    1U,                                 // element_count
    0U,                                 // attributes
    ZYDIS_OPERAND_TYPE_IMMEDIATE,       // type
    { }
};

// Recursive disassembly function (credit: CoPilot)
void DasmRecursiveDisassemble( const uint32_t startOffset, const uint32_t ip, const bool f32bit, const bool fProtected ) {
    ZydisDecoder decoder;
    ZydisFormatter formatter;
    ZydisDecoderInit( &decoder,
        ( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
        ( f32bit ? ZYDIS_STACK_WIDTH_32 : ZYDIS_STACK_WIDTH_16 ) );
    ZydisFormatterInit( &formatter, ZYDIS_FORMATTER_STYLE_INTEL );

    const auto binary = MemBase;
    const size_t binarySize = MEM_TotalPages( ) * 4096; // DosPageSize

    uint32_t segment0 = startOffset - ip;
    std::vector<ZydisDecodedOperandPtr> toVisit{ { static_cast<ZyanU16>( ( segment0 ) >> 4 ), ip } };
    std::unordered_set<uint32_t> added;

    uint8_t ah = 0U; // for int 21h tracking

    while( !toVisit.empty( ) ) {
        ZydisDecodedOperandPtr address = toVisit.back( );
        toVisit.pop_back( );

        if( !ordered_segments.count( { address.segment, {} } ) )
            ordered_segments.insert( { address.segment, { SEG_CODE, 0U } } );

        auto base_offset = address.offset + ( address.segment << 4 );
        while( base_offset < binarySize ) {
            if( visited.count( base_offset ) )
                break;
            visited.insert( base_offset );

            if( binary[base_offset] == 0 && binary[base_offset + 1] == 0 ) // 00 00
                break;

            auto entry = ordered_code.insert( { base_offset, DecodedLine( address, base_offset ) } );
            if( !entry.second ) // insert failed
                break;
            DecodedLine &dline = const_cast<DecodedLine &>( entry.first->extra );
            if( ZYAN_SUCCESS( ZydisDecoderDecodeFull( &decoder, &binary[base_offset], binarySize - base_offset, &dline.instruction, dline.operands ) ) ) {
                char *pOpCode = dline.szOpcode;
                for( auto i = 0; i < dline.instruction.length; ++i )
                    pOpCode += sprintf( pOpCode, "%02X ", binary[base_offset + i] );

                ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, dline.szInstruction, sizeof( dline.szInstruction ), address.offset, ZYAN_NULL );
                char *cptr = dline.szInstruction;
                while( *cptr && *cptr != ' ' ) ++cptr;
                if( cptr ) {
                    *cptr++ = 0;
                    dline.szOperands = cptr;
                }
                address.offset += dline.instruction.length;
                base_offset += dline.instruction.length;

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
                default:
                    break;
                }
                if( dline.mnemonicMask & MM_Branch ) {
                    ZydisDecodedOperandPtr ptr = { static_cast<ZyanU16>( -1 ), static_cast<ZyanU32>( -1 ) };
                    for( uint8_t i = 0; i < dline.instruction.operand_count; ++i ) {
                        auto& op = dline.operands[i];
                        switch( op.type ) {
                        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                            ptr.segment = address.segment;
                            if( op.imm.is_relative )
                                ptr.offset = ( op.imm.is_signed ? op.imm.value.s : op.imm.value.u ) + address.offset;
                            else
                                ptr.offset = ( op.imm.is_signed ? op.imm.value.s : op.imm.value.u );
                            break;
                        case ZYDIS_OPERAND_TYPE_POINTER:
                            ptr = op.ptr;
                            break;
                        default:
                            break;
                        }
                    }
                    if( ptr.segment != static_cast<ZyanU16>( -1 ) && ptr.offset != static_cast<ZyanU32>( -1 ) ) {
                        uint32_t addr = ptr.offset + ( ptr.segment << 4 );
                        if( addr < segment0 || addr >= binarySize ) { // Skip invalid
                            Descriptor desc;
                            if( cpu.gdt.GetDescriptor( ptr.segment, desc ) )
                                addr = ptr.offset + ( desc.GetBase( ) << 4 );
                            if( addr < segment0 || addr >= binarySize ) // Still invalid, skip
                                continue;
                        }
                        bool existingLabel = true;
                        if( !added.count( addr ) ) {
                            added.insert( addr );
                            if( !visited.count( addr ) )
                                toVisit.push_back( ptr );
                            auto label = labels.insert( { addr, LabelInfo( ( dline.mnemonicMask & MM_CALL ) ? LABEL_CALL : LABEL_JUMP, ptr.segment, *new std::set<Pair<uint32_t, uint16_t>> ) } );
                            if( label.second ) { // insert success
                                label.first->extra.callers.insert( { dline.base_offset, dline.address.segment } );
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
                            if( label != labels.end( ) )
                                label->extra.callers.insert( { dline.base_offset, dline.address.segment } );
                        }
                    }
                    if( dline.mnemonicMask & MM_JMP )
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
                } else if( dline.mnemonicMask & MM_INT ) {
                    if( dline.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && dline.operands[0].imm.value.u == 0x21 && ah == 0x4C ) {
                        sprintf( dline.szComment, "DOS Exit" );
                        break;
                    }
                } else if( dline.mnemonicMask & MM_RET ) {
                    break;
                }
            } else { // Decode failed...
                if( binary[base_offset] == 0xFE && ( ( binary[base_offset + 1] >> 3 ) == 0x07 ) ) { // DOSBox internal callback
                    const uint16_t &dw = *reinterpret_cast<uint16_t *>( &binary[base_offset + 2] );
                    dline.instruction = callback_instruction;
                    dline.operands[0] = callback_operand;
                    dline.operands[0].imm = { false, false, { dw } };
                    char *pOpCode = dline.szOpcode;
                    for( auto i = 0; i < dline.instruction.length; ++i )
                        pOpCode += sprintf( pOpCode, "%02X ", binary[base_offset + i] );
                    sprintf_s( dline.szInstruction, sizeof( dline.szInstruction ), "callback 0x%02X", dw );
                    strcat( dline.szComment, CALLBACK_GetDescription( dw ) );
                    address.offset += dline.instruction.length;
                    base_offset += dline.instruction.length;
                } else { // Treat as data; step forward 1 byte
                    sprintf( dline.szOpcode, "%02X", binary[base_offset] );
                    ++base_offset;
                    ++address.offset;
                }
            }
        }
    }
    for( const auto &[address, info] : labels ) {
        auto it = ordered_code.find( { address, DecodedLine( ) } );
        if( it != ordered_code.end( ) ) {
            auto &dline = it->extra;
            const_cast<DecodedLine &>( dline ).mnemonicMask |= ( info.type & LABEL_CALL ? MM_Proc : MM_NONE ) | ( info.type & LABEL_JUMP ? MM_Label : MM_NONE );
        }
    }
    DEBUG_ShowMsg( "DEBUG: Disassembly finished, processed %llu instruction offsets.", visited.size( ) - lastProcessedCount );
    lastProcessedCount = visited.size( );
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
        if( dline.address.segment != currentSegment ) {
            if( currentSegment != static_cast<uint16_t>( -1 ) )
                out << setw( sizeof( indent ) ) << szSegment << "ends" << std::endl << std::endl;
            currentSegment = dline.address.segment;
            sprintf( szSegment, "seg%04X", currentSegment );
            out << setw( sizeof( indent ) ) << szSegment << "segment para public 'CODE' use16" << std::endl \
                << indent << "assume cs:seg000, ds:@DATA" << std::endl << std::endl;
        }
        char szFormattedInstruction[128];
        char const *szOperator = szFormattedInstruction;
        char const *szOperands = nullptr;
        ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, szFormattedInstruction, sizeof( szFormattedInstruction ), dline.address.offset, ZYAN_NULL );
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
