#include "dosbox.h"
#if C_DEBUGGER
#include "debugger_disasm.h"

#include "hardware/memory.h"
#include <vector>
#include <unordered_set>
#include <set>

struct CompareFirst {
    bool operator()( const std::pair<uint32_t, DecodedLine> &a,
        const std::pair<uint32_t, DecodedLine> &b ) const {
        return a.first < b.first;
    }
};
static std::set<std::pair<uint32_t, DecodedLine>, CompareFirst> orderedCode;
static std::set<uint16_t> orderedSegments;
static std::vector<uint16_t> code_segments;
static std::unordered_set<uint32_t> visited;

static std::set<std::pair<uint32_t, DecodedLine>, CompareFirst>::iterator currentLine = orderedCode.end( );

const DecodedLine & operator++( DecodedLine const &source ) { // Prefix increment
    if( currentLine != orderedCode.end( ) && ++currentLine != orderedCode.end( ) )
        const_cast<DecodedLine &>( source ) = currentLine->second;
    else
        const_cast<DecodedLine &>( source ) = orderedCode.begin( )->second;
    return source;
}
const DecodedLine operator++( DecodedLine const &source, int ) { // Postfix increment
    const DecodedLine original = source;
    ++source;
    return original;
}

const DecodedLine &operator--( DecodedLine const &source ) { // Prefix decrement
    if( currentLine != orderedCode.begin( ) )
        const_cast<DecodedLine &>( source ) = (--currentLine)->second;
    return source;
}
const DecodedLine operator--( DecodedLine const &source, int ) { // Postfix decrement
    const DecodedLine original = source;
    --source;
    return original;
}

const DecodedLine & DecodedLine::first( ) {
    currentLine = orderedCode.begin( );
    return currentLine->second;
}
const DecodedLine &DecodedLine::last( ) {
    currentLine = orderedCode.end( );
    --currentLine;
    return currentLine->second;
}

bool DecodedLine::isStart( ) {
    return currentLine == orderedCode.begin( );
}
bool DecodedLine::isEnd( ) {
    return currentLine == orderedCode.end( );
}

uint16_t NumCodeSegments( ) {
    return code_segments.size( );
}

uint16_t CodeSegment( uint16_t index ) {
    if( index < code_segments.size( ) )
        return code_segments[index];
    return static_cast<uint16_t>( -1 );
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
		sprintf( buffer, "db %02X", (unsigned) MemBase[pc] );
		return 1U;
	}
	return instruction.info.length;
}

void DasmReset( ) {
    orderedSegments.clear( );
    visited.clear( );
    orderedCode.clear( );
    currentLine = orderedCode.end( );
}

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

        if( !orderedSegments.count( address.segment ) )
            orderedSegments.insert( address.segment );

        auto base_offset = address.offset + ( address.segment << 4 );
        while( base_offset < binarySize ) {
            if( visited.count( base_offset ) )
                break;
            visited.insert( base_offset );

            if( binary[base_offset] == 0 && binary[base_offset + 1] == 0 ) // 00 00
                break;

            auto entry = orderedCode.insert( { base_offset, DecodedLine( address, base_offset ) } );
            if( !entry.second )
                break;
            DecodedLine &dline = const_cast<DecodedLine &>( entry.first->second );
            if( ZYAN_SUCCESS( ZydisDecoderDecodeFull( &decoder, &binary[base_offset], binarySize - base_offset, &dline.instruction, dline.operands ) ) ) {
                char *pBuffer = dline.szFormatted;
                pBuffer += sprintf( pBuffer, "%04X:%04X ", address.segment, address.offset );
                char *pBufferEnd = &pBuffer[24];
                char *pOpCode = dline.szOpcode;
                for( auto i = 0; i < dline.instruction.length; ++i ) {
                    pBuffer += sprintf( pBuffer, "%02X ", binary[base_offset + i] );
                    pOpCode += sprintf( pOpCode, "%02X ", binary[base_offset + i] );
                }
                while( pBuffer < pBufferEnd ) {
                    *pBuffer++ = ' ';
                    *pOpCode++ = ' ';
                }
                *pOpCode = 0;
                ZydisFormatterFormatInstruction( &formatter, &dline.instruction, dline.operands, ZYDIS_MAX_OPERAND_COUNT, pBuffer, 256, address.offset, ZYAN_NULL );
                while( *pBuffer ) ++pBuffer;

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
                    dline.mnemonicMask = MM_RET;
                    break;
                default:
                    break;
                }
                if( dline.mnemonicMask & MM_Branch ) {
                    *pBuffer = '\n';
                    *++pBuffer = 0;

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
                        if( addr < segment0 || addr >= binarySize ) // Skip invalid
                            continue;
                        if( !added.count( addr ) && !visited.count( addr ) ) {
                            added.insert( addr );
                            toVisit.push_back( ptr );
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
                        pBufferEnd = &pBuffer[24];
                        --pBuffer;
                        while( pBuffer < pBufferEnd )
                            *pBuffer++ = ' ';
                        pBuffer += sprintf( pBuffer, "; DOS - Exit\n" );
                        ++pBuffer;
                        break;
                    }
                    *pBuffer = '\n';
                    *++pBuffer = 0;
                } else if( dline.mnemonicMask & MM_RET ) {
                    *pBuffer = '\n';
                    *++pBuffer = 0;
                    break;
                }
            } else { // Treat as data; step forward 1 byte
                ++base_offset;
                ++address.offset;
            }
        }
    }
    DEBUG_ShowMsg( "Disassembly finished. Processed %d instruction offsets.", (unsigned) visited.size( ) );
    code_segments.clear( );
    std::copy( orderedSegments.begin( ), orderedSegments.end( ), std::back_inserter( code_segments ) );
}
#endif // C_DEBUGGER
