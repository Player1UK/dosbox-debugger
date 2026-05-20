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

typedef enum Mnemonic_Mask : uint32_t {
	MM_NONE	            = 0x00000000,
	MM_Instruction		= 0x00000001,
	MM_ConditionalJump  = 0x00000002,
	MM_JMP		        = 0x00000004,
	MM_CALL		        = 0x00000008,
	MM_INT		        = 0x00000010,
	MM_MOV		        = 0x00000020,
	MM_RET		        = 0x00000040,
	MM_CMP		        = 0x00000080,
	MM_LOOP				= 0x00000100,
	MM_REP				= 0x00000200,
	MM_IO				= 0x00000400,
	MM_ALIGN	        = 0x00000800,
	MM_Logical	        = 0x00001000,
	MM_Math				= 0x00002000,
	MM_String	        = 0x00004000,
	MM_Stack	        = 0x00008000,
	MM_Call_Label		= 0x00010000,
	MM_Jump_Label		= 0x00020000,
	MM_Data_Label		= 0x00040000,
	MM_Data_Segment		= 0x00080000,
	MM_Has_Segment		= 0x00100000,
	MM_Memory_Access	= 0x00200000,
	MM_DOSBox_internal	= 0x00400000,
	MM_Branch		    = MM_ConditionalJump | MM_JMP | MM_CALL | MM_LOOP,
	MM_Label			= MM_Call_Label | MM_Jump_Label,
} MNEMONIC_MASK;
constexpr MNEMONIC_MASK operator|( MNEMONIC_MASK lhs, MNEMONIC_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<MNEMONIC_MASK>;
	return static_cast<MNEMONIC_MASK>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );
}
inline MNEMONIC_MASK & operator|=( MNEMONIC_MASK &lhs, MNEMONIC_MASK rhs ) noexcept {
	lhs = lhs | rhs;
	return lhs;
}
constexpr MNEMONIC_MASK operator&( MNEMONIC_MASK lhs, MNEMONIC_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<MNEMONIC_MASK>;
	return static_cast<MNEMONIC_MASK>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );
}
inline MNEMONIC_MASK & operator&=( MNEMONIC_MASK &lhs, MNEMONIC_MASK rhs ) noexcept {
	lhs = lhs & rhs;
	return lhs;
}

struct DecodedLine {
	ADDRESS_PAIR realAddress;
    uint32_t address;
	uint8_t length;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	cs_insn *cs_instruction = nullptr;
	MNEMONIC_MASK mnemonicMask = MM_NONE;
	uint8_t opCode[256];
	char szOpcode[25] = "";
	char szInstruction[272] = "";
	char szComment[192] = "";
	char const *pMnemonic = nullptr;
	char const *pOperands = nullptr;
	MEM_ACCESS mem_access[NUM_MEM_OPS];

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

extern bool CallerLabelRealAddress( const uint32_t, ADDRESS_PAIR & );

extern uint8_t DasmI386( char *, char *&, const uint32_t, const uint32_t, const bool, const bool );

extern void DasmReset( );
extern void DasmRecursiveDisassemble( const uint32_t, const uint32_t, const bool, const bool );
extern void DasmUnDisassemble( const uint32_t );
extern void DasmShutdown( );

extern void DasmAnalyzeInstruction( const uint32_t );

struct SegmentInfo {
	const SEGTYPE type;
	uint8_t	index = 0U;
	const uint16_t partner = 0U;
};

struct LabelInfo {
	LABEL_MASK type;
	const uint16_t segment;
	uint32_t address_max;
	std::set<Pair<uint32_t, uint16_t>> &callers;
};

extern std::set<Pair<uint16_t, SegmentInfo>> ordered_segments;
extern std::set<Pair<uint32_t, LabelInfo>> labels;

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUG_DISASM_H