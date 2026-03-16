#include "dosbox.h"
#if C_DEBUGGER
#include "hardware/memory.h"
#include "Zydis/Zydis.h"
#include <vector>
#include <unordered_set>
#include <set>
//#include <string.h>

static int opsize = 16;

Bitu DasmI386( char *buffer, const PhysPt pc, const Bitu ip, const bool f32bit, const bool fProtected ) {
	if( f32bit ) opsize = 32;
	else opsize = 16;

	// Loop over the instructions in our buffer.
	ZydisDisassembledInstruction instruction;
	if( ZYAN_SUCCESS( ZydisDisassembleIntel(
		/* machine_mode:    */ ( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
		/* runtime_address: */ ip,
		/* buffer:          */ MemBase + pc,
		/* length:          */ 8,
		/* instruction:     */ &instruction
	) ) ) {
		sprintf( buffer, "%s", instruction.text );
	} else {
		/* invalid instruction, use db xx */
		sprintf( buffer, "db %02X", (unsigned) MemBase[pc] );
		return 1;
	}
	return instruction.info.length;
}

int DasmLastOperandSize( ) {
	return opsize;
}

typedef enum Mnemonic_Mask : uint8_t {
	MM_NONE	            = 0x00,
	MM_ConditionalJump  = 0x01,
	MM_JMP		        = 0x02,
	MM_CALL		        = 0x04,
	MM_INT		        = 0x08,
	MM_MOV		        = 0x10,
	MM_RET		        = 0x20,
	MM_Branch		    = MM_ConditionalJump | MM_JMP | MM_CALL,
} MNEMONIC_MASK;

// Recursive disassembly function (credit: CoPilot)
void DasmRecursiveDisassemble( char *buffer, const uint32_t startOffset, const uint32_t ip, const bool f32bit, const bool fProtected ) {
    ZydisDecoder decoder;
    ZydisFormatter formatter;
    ZydisDecoderInit( &decoder,
        ( f32bit ? ZYDIS_MACHINE_MODE_LEGACY_32 : ( fProtected ? ZYDIS_MACHINE_MODE_LEGACY_16 : ZYDIS_MACHINE_MODE_REAL_16 ) ),
        ( f32bit ? ZYDIS_STACK_WIDTH_32 : ZYDIS_STACK_WIDTH_16 ) );
    ZydisFormatterInit( &formatter, ZYDIS_FORMATTER_STYLE_INTEL );
    std::set<std::pair<uint32_t, char *>> orderedCode;

    auto binary = MemBase;
    const size_t binarySize = MEM_TotalPages( ) * 4096; // DosPageSize

    uint32_t segment0 = startOffset - ip;
    std::unordered_set<uint32_t> added, visited;
    std::vector<ZydisDecodedOperandPtr> toVisit{ { static_cast<ZyanU16>( ( segment0 ) >> 4 ), ip } };
    std::vector<ZydisDecodedOperandPtr> toReturn;

    uint8_t ah = 0U;

    while( !toVisit.empty( ) ) {
        ZydisDecodedOperandPtr address = toVisit.back( );
        toVisit.pop_back( );

        auto base_offset = address.offset + ( address.segment << 4 );
        while( base_offset < binarySize ) {
            if( visited.count( base_offset ) ) {
                if( !toReturn.empty( ) ) {
                    address = toReturn.back( );
                    toReturn.pop_back( );
                    base_offset = address.offset + ( address.segment << 4 );
                    continue;
                }
                break;
            }
            visited.insert( base_offset );

            ZydisDecodedInstruction instruction;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            if( ZYAN_SUCCESS( ZydisDecoderDecodeFull( &decoder, &binary[base_offset], binarySize - base_offset, &instruction, operands ) ) ) {
                char *pBuffer = new char[128];
                orderedCode.insert( { base_offset, pBuffer } );
                pBuffer += sprintf( pBuffer, "%04X:%04X ", address.segment, address.offset );
                char *pBufferEnd = &pBuffer[24];
                for( auto i = 0; i < instruction.length; ++i )
                    pBuffer += sprintf( pBuffer, "%02X ", binary[base_offset + i] );
                while( pBuffer < pBufferEnd )
                    *pBuffer++ = ' ';
                ZydisFormatterFormatInstruction( &formatter, &instruction, operands, ZYDIS_MAX_OPERAND_COUNT, pBuffer, 256, address.offset, ZYAN_NULL );
                while( *pBuffer ) ++pBuffer;

                address.offset += instruction.length;
                base_offset += instruction.length;

                // Handle recursion for jumps/calls
                MNEMONIC_MASK mnemonicMask = MM_NONE;
                switch( instruction.mnemonic ) {
                case ZYDIS_MNEMONIC_INT:
                    mnemonicMask = MM_INT;
                    break;
                case ZYDIS_MNEMONIC_CALL:
                    mnemonicMask = MM_CALL;
                    break;
                case ZYDIS_MNEMONIC_JMP:
                    mnemonicMask = MM_JMP;
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
                    mnemonicMask = MM_ConditionalJump;
                    break;
                case ZYDIS_MNEMONIC_MOV:
                    mnemonicMask = MM_MOV;
                    break;
                case ZYDIS_MNEMONIC_RET:
                    mnemonicMask = MM_RET;
                    break;
                default:
                    break;
                }
                if( mnemonicMask & MM_Branch ) {
                    *pBuffer = '\n';
                    *++pBuffer = 0;

                    ZydisDecodedOperandPtr ptr = { static_cast<ZyanU16>( -1 ), static_cast<ZyanU32>( -1 ) };
                    for( uint8_t i = 0; i < instruction.operand_count; ++i ) {
                        auto& op = operands[i];
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
                            if( mnemonicMask & MM_CALL ) {
                                toReturn.push_back( address );
                                break;
                            } else if( mnemonicMask & MM_JMP )
                                break;
                        }
                    }
                    if( mnemonicMask & MM_JMP )
                        break;
                } else if( mnemonicMask & MM_MOV ) {
                    if( instruction.operand_count == 2 ) {
                        if( operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ( operands[0].reg.value == ZYDIS_REGISTER_AH || operands[0].reg.value == ZYDIS_REGISTER_AX ) ) {
                            if( operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ) {
                                if( operands[0].reg.value == ZYDIS_REGISTER_AH )
                                    ah = static_cast<uint8_t>( operands[1].imm.value.u );
                                else
                                    ah = static_cast<uint8_t>( operands[1].imm.value.u >> 8U );
                            }
                        }
                    }
                } else if( mnemonicMask & MM_INT ) {
                    if( operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operands[0].imm.value.u == 0x21 && ah == 0x4C ) {
                        mnemonicMask = static_cast<MNEMONIC_MASK>( mnemonicMask | MM_RET );
                        pBufferEnd = &pBuffer[24];
                        --pBuffer;
                        while( pBuffer < pBufferEnd )
                            *pBuffer++ = ' ';
                        pBuffer += sprintf( pBuffer, "; DOS - Exit\n" );
                        ++pBuffer;
                        if( !toReturn.empty( ) ) {
                            toReturn.pop_back( );
                        }
                        break;
                    }
                    *pBuffer = '\n';
                    *++pBuffer = 0;
                }
                if( mnemonicMask & MM_RET ) {
                    *pBuffer = '\n';
                    *++pBuffer = 0;
                    if( !toReturn.empty( ) ) {
                        address = toReturn.back( );
                        toReturn.pop_back( );
                        base_offset = address.offset + ( address.segment << 4 );
                        continue;
                    }
                    break;
                }
            } else { // Treat as data; step forward 1 byte
                ++base_offset;
                ++address.offset;
            }
        }
    }
    char *pBuffer = buffer;
    for( const auto &line : orderedCode ) {
        strcpy( pBuffer, line.second );
        while( *pBuffer ) ++pBuffer;
        if( pBuffer[-1] == '\n' ) {
            pBuffer[-1] = 0;
            *pBuffer = '\n';
            *++pBuffer = 0;
        }
        ++pBuffer;
        delete[] line.second;
    }
    pBuffer += sprintf( pBuffer, "Disassembly finished. Processed %d instruction offsets.", (unsigned) visited.size( ) );
    *++pBuffer = 0;
}
#endif // C_DEBUGGER
