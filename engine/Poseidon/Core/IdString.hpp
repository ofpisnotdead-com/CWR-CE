#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Core/DynEnum.hpp>



namespace Poseidon
{
class IdStringTable;

class IdString
{
	friend class IdStringTable;

	// if there is an id, use it; otherwise (id<0) use the full string
	int _id;
	// string is always valid (even for id>=0)
	RStringB _string;

	public:
	IdString(){}
	~IdString(){}

	__forceinline int GetID() const {return _id;}
	__forceinline const RStringB &GetValue() const {return _string;}

};

// Case-sensitive table.
class IdStringTable
{
	DynEnumCS _enum;
	AutoArray<RStringB> _strings;

	public:
	IdStringTable();
	~IdStringTable();
	IdStringTable(const RStringB *strings, int count);

	IdString GetIdString(const char *string);
	int GetId(const char *string);
	const RStringB &GetString(int id) const; 
};
} // namespace Poseidon
