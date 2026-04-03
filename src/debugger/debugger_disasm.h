#ifndef DOSBOX_DEBUG_DISASM_H
#define DOSBOX_DEBUG_DISASM_H

#include "dosbox.h"

#if C_DEBUGGER
#include "Zydis/Zydis.h"
#include <set>

template <typename T1, typename T2>
struct Pair {
	T1 first; T2 second;
	bool operator==( const Pair &other ) const noexcept { // Equality operator for comparisons
		return first == other.first;
	}
	bool operator<( const Pair &other ) const noexcept { // Less than operator for comparisons
		return first < other.first;
	}
};

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
	MM_Proc				= 0x0400,
	MM_Label			= 0x0800,
	MM_Branch		    = MM_ConditionalJump | MM_JMP | MM_CALL,
} MNEMONIC_MASK;
constexpr MNEMONIC_MASK operator|( MNEMONIC_MASK lhs, MNEMONIC_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<MNEMONIC_MASK>;
	return static_cast<MNEMONIC_MASK>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );
}
inline MNEMONIC_MASK &operator|=( MNEMONIC_MASK &lhs, MNEMONIC_MASK rhs ) noexcept {
	lhs = lhs | rhs;
	return lhs;
}

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

extern bool AddressVisited( uint32_t address );
extern bool AddressVisited( uint16_t segment, uint32_t offset );

extern uint32_t DasmI386( char *buffer, const uint32_t pc, const uint32_t ip, const bool f32bit, const bool fProtected );

extern void DasmReset( );
extern void DasmRecursiveDisassemble( const uint32_t startOffset, const uint32_t ip, const bool f32bit, const bool fProtected );

struct LabelInfo {
	uint16_t segment;
	std::set<Pair<uint32_t, uint16_t>> &callers;
};

extern std::set<uint16_t> ordered_segments;
extern std::set<Pair<uint32_t, LabelInfo>> calls;
extern std::set<Pair<uint32_t, uint16_t>> jumps;

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUG_DISASM_H