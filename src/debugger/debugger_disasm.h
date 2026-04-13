#ifndef DOSBOX_DEBUG_DISASM_H
#define DOSBOX_DEBUG_DISASM_H

#include "dosbox.h"

#if C_DEBUGGER
#include "debugtypes.h"
#include <capstone/capstone.h>
#include "Zydis/Zydis.h"
#include <set>

template <typename T1, typename T2>
struct Pair {
	T1 value; T2 extra;
	bool operator==( const Pair &other ) const noexcept { // Equality operator for comparisons
		return value == other.value;
	}
	bool operator<( const Pair &other ) const noexcept { // Less than operator for comparisons
		return value < other.value;
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
	MM_LOOP				= 0x0080,
	MM_REP				= 0x0100,
	MM_Logical	        = 0x0200,
	MM_Math				= 0x0400,
	MM_Stack	        = 0x0800,
	MM_Call_Label		= 0x1000,
	MM_Jump_Label		= 0x2000,
	MM_Has_Segment		= 0x4000,
	MM_Branch		    = MM_ConditionalJump | MM_JMP | MM_CALL | MM_LOOP,
	MM_Label			= MM_Call_Label | MM_Jump_Label,
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
	ADDRESS_PAIR address;
    uint32_t base_offset;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	cs_insn *cs_instruction;
	MNEMONIC_MASK mnemonicMask = MM_NONE;
	char szOpcode[25];
	char szInstruction[32];
	char szComment[128] = "";
	char const *pOperands = nullptr;

	static const DecodedLine * find( const uint32_t );
	static const DecodedLine * find( const ADDRESS_PAIR & );
	static const DecodedLine & first( );
	static const DecodedLine & last( );
	static bool isStart( );
	static bool isEnd( );
	static bool isEmpty( );
};
const DecodedLine & operator++( DecodedLine const & ); // Prefix increment
const DecodedLine operator++( DecodedLine const &, int ); // Postfix increment
const DecodedLine & operator--( DecodedLine const & ); // Prefix decrement
const DecodedLine operator--( DecodedLine const &, int ); // Postfix decrement

extern bool AddressVisited( uint32_t );
extern bool AddressVisited( const ADDRESS_PAIR & );

extern uint8_t DasmI386( char *, char *&, const uint32_t, const uint32_t, const bool, const bool );

extern void DasmReset( );
extern void DasmRecursiveDisassemble( const uint32_t, const uint32_t, const bool, const bool );

struct SegmentInfo {
	const SEGTYPE type;
	uint8_t	index = 0U;
};

struct LabelInfo {
	LABEL_MASK type;
	const uint16_t segment;
	std::set<Pair<uint32_t, uint16_t>> &callers;
};

extern std::set<Pair<uint16_t, SegmentInfo>> ordered_segments;
extern std::set<Pair<uint32_t, LabelInfo>> labels;

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUG_DISASM_H