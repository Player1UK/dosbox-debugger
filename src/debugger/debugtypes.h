#ifndef DOSBOX_DEBUGTYPES_H
#define DOSBOX_DEBUGTYPES_H

#include "dosbox.h"

#if C_DEBUGGER

typedef enum Size_Type : uint8_t {
	SIZE_BYTE = 0U,
	SIZE_WORD,
	SIZE_DWORD
} SIZE_TYPE;

typedef enum Ptr_Type : uint8_t {
	PTR_NONE = 0U,
	PTR_BYTE,
	PTR_WORD,
	PTR_DWORD,
	PTR
} PTR_TYPE;

typedef enum Label_Mask : unsigned __int8 {
	LABEL_CALL = 0x1u,
	LABEL_JUMP = 0x2u,
	LABEL_BOTH = LABEL_CALL | LABEL_JUMP
} LABEL_MASK;

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

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUGTYPES_H
