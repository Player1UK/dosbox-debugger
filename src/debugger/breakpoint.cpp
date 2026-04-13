#include "breakpoint.h"

#if C_DEBUGGER
#include "cpu/cpu.h"
#include "cpu/paging.h"

extern uint32_t GetPhysicalAddress( const ADDRESS_PAIR & );

std::list<CBreakpoint *> BPoints = {};

CBreakpoint::CBreakpoint( void )
	: type( BKPNT_UNKNOWN ),
	location( 0 ),
	oldData( 0xCC ),
	real_address( 0U, 0U ),
	intNr( 0 ),
	ahValue( 0 ),
	alValue( 0 ),
	active( false ),
	once( false ) {
}

void CBreakpoint::SetAddress( const ADDRESS_PAIR &address_pair ) {
	location = GetPhysicalAddress( address_pair );
	type = BKPNT_PHYSICAL;
	real_address = address_pair;
}

void CBreakpoint::Activate( const bool _active ) {
#if !C_HEAVY_DEBUGGER
	if( GetType( ) == BKPNT_PHYSICAL ) {
		if( _active ) {
			// Set 0xCC and save old value
			uint8_t data = mem_readb( location );
			if( data != 0xCC ) {
				oldData = data;
				mem_writeb( location, 0xCC );
			} else if( !active ) {
				// Another activate breakpoint is already here.
				// Find it, and copy its oldData value
				CBreakpoint *bp = FindOtherActiveBreakpoint( location,
					this );

				if( !bp || bp->oldData == 0xCC ) {
					// This might also happen if there is a
					// real 0xCC instruction here
					DEBUG_ShowMsg( "DEBUG: Internal error while activating breakpoint.\n" );
					oldData = 0xCC;
				} else {
					oldData = bp->oldData;
				}
			}
		} else {
			if( mem_readb( location ) == 0xCC ) {
				if( oldData == 0xCC ) {
					DEBUG_ShowMsg( "DEBUG: Internal error while deactivating breakpoint.\n" );
				}

				// Check if we are the last active breakpoint at
				// this location
				bool otherActive = ( FindOtherActiveBreakpoint( location,
					this ) !=
					nullptr );

				// If so, remove 0xCC and set old value
				if( !otherActive ) {
					mem_writeb( location, oldData );
				}
			}
		}
	}
#endif
	active = _active;
}

CBreakpoint *CBreakpoint::AddBreakpoint( const ADDRESS_PAIR &address_pair, const bool once ) {
	auto bp = new CBreakpoint( );
	bp->SetAddress( address_pair );
	bp->SetOnce( once );
	BPoints.push_front( bp );
	return bp;
}

CBreakpoint *CBreakpoint::AddIntBreakpoint( const uint8_t intNum, const uint16_t ah, const uint16_t al, const bool once ) {
	auto bp = new CBreakpoint( );
	bp->SetInt( intNum, ah, al );
	bp->SetOnce( once );
	BPoints.push_front( bp );
	return bp;
}

CBreakpoint *CBreakpoint::AddMemBreakpoint( const ADDRESS_PAIR &address_pair ) {
	auto bp = new CBreakpoint( );
	bp->SetAddress( address_pair );
	bp->SetOnce( false );
	bp->SetType( BKPNT_MEMORY );
	BPoints.push_front( bp );
	return bp;
}

void CBreakpoint::ActivateBreakpoints( ) { // activate all breakpoints
	std::list<CBreakpoint *>::iterator i;
	for( auto &bp : BPoints )
		bp->Activate( true );
}

void CBreakpoint::DeactivateBreakpoints( ) { // deactivate all breakpoints
	for( auto &bp : BPoints )
		bp->Activate( false );
}

void CBreakpoint::ActivateBreakpointsExceptAt( const PhysPt adr ) { // activate all breakpoints, except those at adr
	std::list<CBreakpoint *>::iterator i;
	for( auto &bp : BPoints ) {
		if( bp->GetType( ) == BKPNT_PHYSICAL && bp->GetLocation( ) == adr ) // Do not activate breakpoints at adr
			continue;
		bp->Activate( true );
	}
}

// Checks if breakpoint is valid and should stop execution
bool CBreakpoint::CheckBreakpoint( const ADDRESS_PAIR &address_pair ) {
	// Quick exit if there are no breakpoints
	if( BPoints.empty( ) )
		return false;
	// Search matching breakpoint
	for( auto i = BPoints.begin( ); i != BPoints.end( ); ++i ) {
		auto bp = ( *i );

		if( ( bp->GetType( ) == BKPNT_PHYSICAL ) && bp->IsActive( ) && ( bp->GetLocation( ) == GetPhysicalAddress( address_pair ) ) ) {
			if( bp->GetOnce( ) ) { // Found
				// delete it, if it should only be used once
				BPoints.erase( i );
				bp->Activate( false );
				delete bp;
			} else { // Also look for once-only breakpoints at this address
				bp = FindPhysBreakpoint( address_pair, true );
				if( bp ) {
					BPoints.remove( bp );
					bp->Activate( false );
					delete bp;
				}
			}
			return true;
		}
#if C_HEAVY_DEBUGGER
		// Memory breakpoint support
		else if( bp->IsActive( ) ) {
			if( ( bp->GetType( ) == BKPNT_MEMORY ) ||
				( bp->GetType( ) == BKPNT_MEMORY_PROT ) ||
				( bp->GetType( ) == BKPNT_MEMORY_LINEAR ) ) {
				// Watch Protected Mode Memoryonly in pmode
				if( bp->GetType( ) == BKPNT_MEMORY_PROT ) {
					// Check if pmode is active
					if( !cpu.pmode )
						return false;
					// Check if descriptor is valid
					Descriptor desc;
					if( !cpu.gdt.GetDescriptor( bp->GetSegment( ), desc ) )
						return false;
					if( desc.GetLimit( ) == 0 )
						return false;
				}
				Bitu address;
				if( bp->GetType( ) == BKPNT_MEMORY_LINEAR )
					address = bp->GetOffset( );
				else
					address = GetPhysicalAddress( { bp->GetSegment( ), bp->GetOffset( ) } );
				uint8_t value = 0;
				if( mem_readb_checked( address, &value ) )
					return false;
				if( bp->GetValue( ) != value ) { // Yup, memory value changed
					DEBUG_ShowMsg( "DEBUG: Memory breakpoint %s: %04X:%04X - %02X -> %02X\n",
						( bp->GetType( ) == BKPNT_MEMORY_PROT ) ? "(Prot)" : "", bp->GetSegment( ), bp->GetOffset( ), bp->GetValue( ), value );
					bp->SetValue( value );
					return true;
				}
			} else if( bp->GetType( ) == BKPNT_MEMORY_READ ) {
				if( bp->WasMemoryRead( ) ) { // Yup, memory value was read
					DEBUG_ShowMsg( "DEBUG: Memory read breakpoint: %04X:%04X\n", bp->GetSegment( ), bp->GetOffset( ) ); bp->FlagMemoryAsUnread( );
					return true;
				}
			}
		}
#endif
	}
	return false;
}

bool CBreakpoint::CheckIntBreakpoint( [[maybe_unused]] const PhysPt adr, const uint8_t intNr, const uint16_t ahValue, const uint16_t alValue ) {
	// Checks if interrupt breakpoint is valid and should stop execution
	if( BPoints.empty( ) )
		return false;

	// Search matching breakpoint
	for( auto i = BPoints.begin( ); i != BPoints.end( ); ++i ) {
		auto bp = ( *i );
		if( ( bp->GetType( ) == BKPNT_INTERRUPT ) && bp->IsActive( ) && ( bp->GetIntNr( ) == intNr ) ) {
			if( ( ( bp->GetValue( ) == BPINT_ALL ) || ( bp->GetValue( ) == ahValue ) ) && ( ( bp->GetOther( ) == BPINT_ALL ) || ( bp->GetOther( ) == alValue ) ) ) {
				// Ignore it once ? Found
				if( bp->GetOnce( ) ) {
					// delete it, if it should only be used once
					BPoints.erase( i );
					bp->Activate( false );
					delete bp;
				}
				return true;
			}
		}
	}
	return false;
}

void CBreakpoint::DeleteAll( ) {
	for( auto &bp : BPoints ) {
		bp->Activate( false );
		delete bp;
	}
	BPoints.clear( );
}

bool CBreakpoint::DeleteByIndex( const uint16_t index ) {
	// Request is past the end
	if( index >= BPoints.size( ) )
		return false;

	auto it = BPoints.begin( );
	std::advance( it, index );
	auto bp = *it;

	BPoints.erase( it );
	bp->Activate( false );
	delete bp;
	return true;
}

CBreakpoint * CBreakpoint::FindPhysBreakpoint( const ADDRESS_PAIR &address_pair, const bool once ) {
	if( BPoints.empty( ) )
		return nullptr;
#if !C_HEAVY_DEBUGGER
	PhysPt adr = GetPhysicalAddress( address_pair );
#endif
	// Search for matching breakpoint
	for( auto &bp : BPoints ) {
#if C_HEAVY_DEBUGGER
		// Heavy debugging breakpoints are triggered by matching seg:off
		bool atLocation = bp->GetSegment( ) == address_pair.segment && bp->GetOffset( ) == address_pair.offset;
#else
		// Normal debugging breakpoints are triggered at an address
		bool atLocation = bp->GetLocation( ) == adr;
#endif
		if( bp->GetType( ) == BKPNT_PHYSICAL && atLocation && bp->GetOnce( ) == once )
			return bp;
	}
	return nullptr;
}

CBreakpoint * CBreakpoint::FindOtherActiveBreakpoint( const PhysPt adr, const CBreakpoint *skip ) {
	for( auto &bp : BPoints ) {
		if( bp != skip && bp->GetType( ) == BKPNT_PHYSICAL &&
			bp->GetLocation( ) == adr && bp->IsActive( ) ) {
			return bp;
		}
	}
	return nullptr;
}

// is there a permanent breakpoint at address ?
bool CBreakpoint::IsBreakpoint( const ADDRESS_PAIR &address_pair, const bool temporary ) {
	return FindPhysBreakpoint( address_pair, temporary ) != nullptr;
}

bool CBreakpoint::DeleteBreakpoint( const ADDRESS_PAIR &address_pair, const bool temporary ) {
	CBreakpoint *bp = FindPhysBreakpoint( address_pair, temporary );
	if( bp ) {
		BPoints.remove( bp );
		delete bp;
		return true;
	}
	return false;
}

void CBreakpoint::ShowList( void ) {
	// iterate list
	int nr = 0;
	for( auto &bp : BPoints ) {
		if( bp->GetType( ) == BKPNT_PHYSICAL )
			DEBUG_ShowMsg( "%02X. BP %04X:%04X\n", nr, bp->GetSegment( ), bp->GetOffset( ) );
		else if( bp->GetType( ) == BKPNT_INTERRUPT ) {
			if( bp->GetValue( ) == BPINT_ALL )
				DEBUG_ShowMsg( "%02X. BPINT %02X\n", nr, bp->GetIntNr( ) );
			else if( bp->GetOther( ) == BPINT_ALL )
				DEBUG_ShowMsg( "%02X. BPINT %02X AH=%02X\n", nr, bp->GetIntNr( ), bp->GetValue( ) );
			else
				DEBUG_ShowMsg( "%02X. BPINT %02X AH=%02X AL=%02X\n", nr, bp->GetIntNr( ), bp->GetValue( ), bp->GetOther( ) );
		} else if( bp->GetType( ) == BKPNT_MEMORY )
			DEBUG_ShowMsg( "%02X. BPMEM %04X:%04X (%02X)\n", nr, bp->GetSegment( ), bp->GetOffset( ), bp->GetValue( ) );
		else if( bp->GetType( ) == BKPNT_MEMORY_READ )
			DEBUG_ShowMsg( "%02X. BPMR %04X:%04X\n", nr, bp->GetSegment( ), bp->GetOffset( ) );
		else if( bp->GetType( ) == BKPNT_MEMORY_PROT )
			DEBUG_ShowMsg( "%02X. BPPM %04X:%08X (%02X)\n", nr, bp->GetSegment( ), bp->GetOffset( ), bp->GetValue( ) );
		else if( bp->GetType( ) == BKPNT_MEMORY_LINEAR )
			DEBUG_ShowMsg( "%02X. BPLM %08X (%02X)\n", nr, bp->GetOffset( ), bp->GetValue( ) );
		++nr;
	}
}
#endif // C_DEBUGGER