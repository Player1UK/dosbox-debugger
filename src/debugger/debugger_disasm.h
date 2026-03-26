#ifndef DOSBOX_DEBUG_DISASM_H
#define DOSBOX_DEBUG_DISASM_H

#include "dosbox.h"

#if C_DEBUGGER
#include "Zydis/Zydis.h"

typedef enum Mnemonic_Mask : uint16_t {
	MM_NONE	            = 0x0000,
	MM_ConditionalJump  = 0x0001,
	MM_JMP		        = 0x0002,
	MM_CALL		        = 0x0004,
	MM_INT		        = 0x0008,
	MM_MOV		        = 0x0010,
	MM_RET		        = 0x0020,
	MM_CMP		        = 0x0040,
	MM_Logical	        = 0x0080,
	MM_Math				= 0x0100,
	MM_Stack	        = 0x0200,
	MM_Branch		    = MM_ConditionalJump | MM_JMP | MM_CALL,
} MNEMONIC_MASK;

struct DecodedLine {
    ZydisDecodedOperandPtr address;
    uint32_t base_offset;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	MNEMONIC_MASK mnemonicMask = MM_NONE;
	char szOpcode[25];
	char szInstruction[128];
	char szComment[64] = "";
	char const *szOperands = nullptr;

	static const DecodedLine &first( );
	static const DecodedLine &last( );
	static bool isStart( );
	static bool isEnd( );
	static bool isEmpty( );
};
const DecodedLine & operator++( DecodedLine const & ); // Prefix increment
const DecodedLine operator++( DecodedLine const &, int ); // Postfix increment
const DecodedLine & operator--( DecodedLine const & ); // Prefix decrement
const DecodedLine operator--( DecodedLine const &, int ); // Postfix decrement

extern uint16_t NumCodeSegments( );
extern uint16_t CodeSegment( uint16_t index );

extern bool AddressVisited( uint32_t address );
extern bool AddressVisited( uint16_t segment, uint32_t offset );

extern uint32_t DasmI386( char *buffer, const uint32_t pc, const uint32_t ip, const bool f32bit, const bool fProtected );

extern void DasmReset( );
extern void DasmRecursiveDisassemble( const uint32_t startOffset, const uint32_t ip, const bool f32bit, const bool fProtected );

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUG_DISASM_H