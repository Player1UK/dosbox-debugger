#ifndef DOSBOX_DEBUGTYPES_H
#define DOSBOX_DEBUGTYPES_H

#include "dosbox.h"

#if C_DEBUGGER
typedef struct Address_Pair {
	uint16_t segment = 0U;
	uint32_t offset = 0U;

	bool operator==( const Address_Pair &other ) const {
		return this->segment == other.segment && this->offset == other.offset;
	}
	bool operator>=( const Address_Pair &other ) const {
		return this->segment >= other.segment && this->offset >= other.offset;
	}
	bool operator<=( const Address_Pair &other ) const {
		return this->segment <= other.segment && this->offset <= other.offset;
	}
	bool operator>( const Address_Pair &other ) const {
		return this->segment > other.segment && this->offset > other.offset;
	}
	bool operator<( const Address_Pair &other ) const {
		return this->segment < other.segment && this->offset < other.offset;
	}
	Address_Pair &operator++( ) { // Prefix increment
		++offset;
		return *this;
	}
	Address_Pair &operator+=( const uint32_t value ) {
		offset += value;
		return *this;
	}
	uint32_t address( ) const {
		return offset + ( segment << 4U );
	}
	static Address_Pair RealAddress( const uint32_t address, const uint16_t segment ) {
		return { segment, address - ( segment << 4U ) };
	}
	static uint32_t Address( const Address_Pair &other ) {
		return other.offset + ( other.segment << 4U );
	}
	static uint32_t Address( const uint16_t segment ) {
		return ( segment << 4U );
	}
	static uint32_t Offset( const uint32_t address, const uint16_t segment ) {
		return address - ( segment << 4U );
	}
	static uint16_t Segment( const uint32_t address ) {
		return ( address >> 4U );
	}
} ADDRESS_PAIR;

typedef struct Mem_Access {
	uint8_t		size = 8U;
	uint16_t	segment_id = 0U;
	uint16_t	base_id = 0U;
	struct Mem_Access_Disp {
		bool	has_displacement = false;
		int		value = 0;
	} disp;
} MEM_ACCESS;

typedef enum Mem_Op : uint8_t {
	MEM_OP0 = 0U,
	MEM_OP1,
	NUM_MEM_OPS
} MEM_OP;

typedef enum Label_Mask : unsigned __int8 {
	LABEL_CALL		= 0x1u,
	LABEL_JUMP		= 0x2u,
	LABEL_DATA		= 0x4u,
	LABEL_BOTH		= LABEL_CALL | LABEL_JUMP,
	LABEL_ALL		= LABEL_BOTH | LABEL_DATA
} LABEL_MASK;
constexpr LABEL_MASK operator|( LABEL_MASK lhs, LABEL_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<LABEL_MASK>;
	return static_cast<LABEL_MASK>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );
}
inline LABEL_MASK & operator|=( LABEL_MASK &lhs, LABEL_MASK rhs ) noexcept {
	lhs = lhs | rhs;
	return lhs;
}
constexpr LABEL_MASK operator&( LABEL_MASK lhs, LABEL_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<LABEL_MASK>;
	return static_cast<LABEL_MASK>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );
}
inline LABEL_MASK & operator&=( LABEL_MASK &lhs, LABEL_MASK rhs ) noexcept {
	lhs = lhs & rhs;
	return lhs;
}

typedef enum Ptr_Type : uint8_t {
	PTR_NONE		= 0U,
	PTR_BYTE		= 1u,
	PTR_WORD		= 2u,
	PTR_DWORD		= 4u,
	PTR				= 8u
} PTR_TYPE;

typedef enum Run_Type : uint8_t {
	RUN_STEP		= 0U,
	RUN_STEP_OVER,
	RUN_TO_TBP,
	RUN_OUT,
	RUN_FOREVER,
} RUN_TYPE;

typedef enum SegType : uint8_t {
	SEG_BASE = 0U,
	SEG_ENV,
	SEG_PSP,
	SEG_CODE,
	SEG_STACK,
	SEG_STACK_END,
	SEG_HEAP,
	SEG_DATA,
	SEG_MAX,
	NUM_SEG_TYPES
} SEGTYPE;
SEGTYPE &operator++( SEGTYPE & ); // Prefix increment
SEGTYPE operator++( SEGTYPE &, int ); // Postfix increment

typedef enum Size_Type : uint8_t {
	SIZE_BYTE		= 1U,
	SIZE_WORD		= 2u,
	SIZE_DWORD		= 4u
} SIZE_TYPE;
#endif // C_DEBUGGER

#endif // DOSBOX_DEBUGTYPES_H
