// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugvar.h"

#if C_DEBUGGER
#include "utils/string_utils.h"

CDebugVar::CDebugVar( const char *vname, PhysPt address ) : adr( address ) {
	safe_strcpy( name, vname );
}
std::vector<CDebugVar *> varList = {};

void CDebugVar::InsertVariable( char *name, PhysPt adr ) {
	varList.push_back( new CDebugVar( name, adr ) );
}

void CDebugVar::DeleteAll( ) {
	std::vector<CDebugVar *>::iterator i;
	CDebugVar *bp;
	for( i = varList.begin( ); i != varList.end( ); i++ ) {
		bp = static_cast<CDebugVar *>( *i );
		delete bp;
	}
	( varList.clear )( );
}

CDebugVar *CDebugVar::FindVar( PhysPt pt ) {
	if( varList.empty( ) ) {
		return nullptr;
	}

	std::vector<CDebugVar *>::size_type s = varList.size( );
	CDebugVar *bp;
	for( std::vector<CDebugVar *>::size_type i = 0; i != s; i++ ) {
		bp = static_cast<CDebugVar *>( varList[i] );
		if( bp->GetAdr( ) == pt ) {
			return bp;
		}
	}
	return nullptr;
}

bool CDebugVar::SaveVars( char *name ) {
	if( varList.size( ) > 65535 )
		return false;
	const std_fs::path vars_file = name;
	FILE *f = fopen( vars_file.string( ).c_str( ), "wb+" );
	if( !f ) {
		DEBUG_ShowMsg( "DEBUG: Output of vars failed.\n" );
		return false;
	}
	DEBUG_ShowMsg( "DEBUG: vars file '%s' created.\n", std_fs::absolute( vars_file ).string( ).c_str( ) );
	// write number of vars
	auto num = static_cast<uint16_t>( varList.size( ) );
	fwrite( &num, 1, sizeof( num ), f );

	std::vector<CDebugVar *>::iterator i;
	CDebugVar *bp;
	for( i = varList.begin( ); i != varList.end( ); i++ ) {
		bp = static_cast<CDebugVar *>( *i );
		fwrite( bp->GetName( ), 1, 16, f ); // name
		PhysPt adr = bp->GetAdr( );
		fwrite( &adr, 1, sizeof( adr ), f ); // adr
	}
	fclose( f );
	return true;
}

bool CDebugVar::LoadVars( char *name ) {
	const std_fs::path vars_file = name;
	FILE *f = fopen( vars_file.string( ).c_str( ), "rb" );
	if( !f ) {
		DEBUG_ShowMsg( "DEBUG: Load of vars from %s failed.\n", name );
		return false;
	}
	DEBUG_ShowMsg( "DEBUG: vars file '%s' loaded.\n", std_fs::absolute( vars_file ).string( ).c_str( ) );
	
	uint16_t num;
	if( fread( &num, sizeof( num ), 1, f ) != 1 ) { // read number of vars
		fclose( f );
		return false;
	}
	for( uint16_t i = 0; i < num; ++i ) {
		char name[16];
		if( fread( name, 16, 1, f ) != 1 ) // name
			break;
		PhysPt adr;
		if( fread( &adr, sizeof( adr ), 1, f ) != 1 ) // adr
			break;
		InsertVariable( name, adr ); // insert
	}
	fclose( f );
	return true;
}
#endif // C_DEBUGGER