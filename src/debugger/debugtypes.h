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
} ADDRESS_PAIR;

typedef enum Label_Mask : unsigned __int8 {
	LABEL_CALL		= 0x1u,
	LABEL_JUMP		= 0x2u,
	LABEL_BOTH		= LABEL_CALL | LABEL_JUMP
} LABEL_MASK;

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
	SEG_CODE,
	SEG_DATA,
	SEG_STACK,
	SEG_STACK_END,
	SEG_HEAP,
	SEG_PSP,
	SEG_ENV,
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
