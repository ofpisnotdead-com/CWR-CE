#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/MapFile.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>

#include <string>

using namespace Poseidon;

namespace
{
// A linker map is a header line the parser seeks to, then one entry per line:
// section:offset, symbol, absolute address, then anything.
std::string MapText(const std::string& symbol)
{
    return "Some preamble\r\n"
           "  Address         Publics by Value              Rva+Base     Lib:Object\r\n"
           "\r\n"
           " 0001:00000010       Alpha                      00401010 f   a.obj\r\n"
           " 0001:00000020       " +
           symbol + "                      00401020 f   b.obj\r\n";
}

MapFile ParseMap(const std::string& text)
{
    MapFile map;
    QIStream in(text.data(), static_cast<int>(text.size()));
    map.ParseMapStream(in);
    return map;
}
} // namespace

TEST_CASE("MapFile reads symbols and addresses from a linker map", "[IO][mapFile]")
{
    MapFile map = ParseMap(MapText("Beta"));

    REQUIRE_FALSE(map.Empty());
    REQUIRE(map.PhysicalAddress("Alpha") == 0x401010);
    REQUIRE(map.PhysicalAddress("Beta") == 0x401020);
    REQUIRE(map.LogicalAddress("Alpha") == 0x10);
}

TEST_CASE("MapFile keeps a line running past a 0xff byte", "[IO][mapFile]")
{
    // 0xff is the one data byte that collides with EOF once a stream value is
    // narrowed to char, which would cut the line short and lose the address.
    const std::string symbol = "Sym\xff"
                               "Tail";
    MapFile map = ParseMap(MapText(symbol));

    REQUIRE(map.PhysicalAddress(symbol.c_str()) == 0x401020);
    REQUIRE(map.PhysicalAddress("Sym") == 0);
}
