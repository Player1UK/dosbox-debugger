#ifndef DOSBOX_BREAKPOINT_H
#define DOSBOX_BREAKPOINT_H

#include "dosbox.h"

#if C_DEBUGGER
#include "debugtypes.h"
#include "hardware/memory.h"

enum EBreakpoint {
	BKPNT_UNKNOWN,
	BKPNT_PHYSICAL,
	BKPNT_INTERRUPT,
	BKPNT_MEMORY,
	BKPNT_MEMORY_READ,
	BKPNT_MEMORY_PROT,
	BKPNT_MEMORY_LINEAR
};

#define BPINT_ALL 0x100

class CBreakpoint {
public:
	CBreakpoint( void );
	void SetAddress( const ADDRESS_PAIR & );
	void SetAddress( const PhysPt adr ) {
		location = adr;
		type = BKPNT_PHYSICAL;
	}
	void SetInt( const uint8_t _intNr, const uint16_t ah, const uint16_t al ) {
		intNr = _intNr, ahValue = ah;
		alValue = al;
		type = BKPNT_INTERRUPT;
	}
	void SetOnce( const bool _once ) {
		once = _once;
	}
	void SetType( const EBreakpoint _type ) {
		type = _type;
	}
	void SetValue( const uint8_t value ) {
		ahValue = value;
	}
	void SetOther( const uint8_t other ) {
		alValue = other;
	}

	bool IsActive( void ) const {
		return active;
	}
	void Activate( const bool _active );

	EBreakpoint GetType( ) const noexcept {
		return type;
	}
	bool GetOnce( ) const noexcept {
		return once;
	}
	PhysPt GetLocation( ) const noexcept {
		return location;
	}
	ADDRESS_PAIR GetAddress( ) const noexcept {
		return real_address;
	}
	uint16_t GetSegment( ) const noexcept {
		return real_address.segment;
	}
	uint32_t GetOffset( ) const noexcept {
		return real_address.offset;
	}
	uint8_t GetIntNr( ) const noexcept {
		return intNr;
	}
	uint16_t GetValue( ) const noexcept {
		return ahValue;
	}
	uint16_t GetOther( ) const noexcept {
		return alValue;
	}
#if C_HEAVY_DEBUGGER
	void FlagMemoryAsRead( ) {
		memory_was_read = true;
	}
	void FlagMemoryAsUnread( ) {
		memory_was_read = false;
	}
	bool WasMemoryRead( ) const {
		return memory_was_read;
	}
#endif
	// statics
	static CBreakpoint * AddBreakpoint( const ADDRESS_PAIR &, const bool once );
	static CBreakpoint * AddIntBreakpoint( const uint8_t intNum, const uint16_t ah, const uint16_t al, const bool once );
	static CBreakpoint * AddMemBreakpoint( const ADDRESS_PAIR & );
	static void DeactivateBreakpoints( );
	static void ActivateBreakpoints( );
	static void ActivateBreakpointsExceptAt( const PhysPt adr );
	static bool CheckBreakpoint( const PhysPt adr );
	static bool CheckBreakpoint( const ADDRESS_PAIR & );
	static bool CheckIntBreakpoint( const PhysPt adr, const uint8_t intNr, const uint16_t ahValue, const uint16_t alValue );
	static CBreakpoint * FindPhysBreakpoint( const ADDRESS_PAIR &, const bool once );
	static CBreakpoint * FindOtherActiveBreakpoint( const PhysPt adr, const CBreakpoint *skip );
	static bool IsBreakpoint( const ADDRESS_PAIR &, const bool temporary = false );
	static bool DeleteBreakpoint( const ADDRESS_PAIR &, const bool temporary = false );
	static bool DeleteByIndex( const uint16_t index );
	static void DeleteAll( void );
	static void ShowList( void );

private:
	EBreakpoint type = {};
	// Physical
	PhysPt location = 0;
	uint8_t oldData = 0;
	ADDRESS_PAIR real_address;
	// Int
	uint8_t intNr = 0;
	uint16_t ahValue = 0;
	uint16_t alValue = 0;
	// Shared
	bool active = 0;
	bool once = 0;
#if C_HEAVY_DEBUGGER
	bool memory_was_read = false;

	friend bool DEBUG_HeavyIsBreakpoint( void );
#endif
};
#endif // C_DEBUGGER

#endif // DOSBOX_BREAKPOINT_H
