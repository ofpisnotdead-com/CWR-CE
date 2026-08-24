#pragma once

#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>


namespace Poseidon
{
class QIStream;

struct MapInfo
{
	BString<512> name;
	int physAddress; // symbol value
	int logAddress; // logical address
};

typedef int MapInfo::*MapAddressId;


class MapFile
{
	BString<256> _name;
	AutoArray<MapInfo> _map;


	public:
	void ParseMapFile();
	// Parses an already-open linker map; ParseMapFile locates the file and calls this.
	void ParseMapStream( QIStream &in );
	const char *GetName() const {return _name;}
	const char *MapName( int address, MapAddressId id, int *lower=nullptr );
	const char *MapNameFromPhysical( int fAddress, int *lower=nullptr )
	{
		return MapName(fAddress,&MapInfo::physAddress,lower);
	}
	const char *MapNameFromLogical( int lAddress, int *lower=nullptr )
	{
		return MapName(lAddress,&MapInfo::logAddress,lower);
	}

	int Address( const char *name, MapAddressId id ) const ;
	int MinAddress( MapAddressId id ) const ;
	int MaxAddress( MapAddressId id ) const ;

	int PhysicalAddress( const char *name ) const {return Address(name,&MapInfo::physAddress);}
	int LogicalAddress( const char *name ) const {return Address(name,&MapInfo::logAddress);}

	int MinPhysicalAddress() const {return MinAddress(&MapInfo::physAddress);}
	int MaxPhysicalAddress() const {return MaxAddress(&MapInfo::physAddress);}
	int MinLogicalAddress() const {return MinAddress(&MapInfo::logAddress);}
	int MaxLogicalAddress() const {return MaxAddress(&MapInfo::logAddress);}

	bool Empty() const {return _map.Size()<=0;}
};

} // namespace Poseidon
