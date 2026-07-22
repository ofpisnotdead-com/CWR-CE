#include <Poseidon/Core/IdString.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
IdStringTable::IdStringTable() = default;

IdStringTable::IdStringTable(const RStringB* strings, int count)
{
    _strings.Realloc(count);
    for (int i = 0; i < count; i++)
    {
        _enum.AddValue(strings[i]);
        _strings.Add(strings[i]);
    }
    _enum.Close();
}

IdStringTable::~IdStringTable() = default;

int IdStringTable::GetId(const char* string)
{
    return _enum.GetValue(string);
}

const RStringB& IdStringTable::GetString(int id) const
{
    // Callers may pass an attacker-derived id (network string-table refs decode a
    // wire varint), so keep it inside the populated range.
    if (id < 0 || id >= _strings.Size())
    {
        static const RStringB empty;
        return empty;
    }
    return _strings[id];
}

IdString IdStringTable::GetIdString(const char* string)
{
    IdString id;
    id._id = _enum.GetValue(string);
    id._string = string;
    return id;
}
} // namespace Poseidon
