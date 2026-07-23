#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Core/DynEnum.hpp>
#include <Poseidon/Core/IdString.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

TEST_CASE("dynEnum: case-sensitive add and lookup", "[core][dynEnum]")
{
    Poseidon::DynEnumCS e;
    int v0 = e.AddValue("Alpha");
    int v1 = e.AddValue("Beta");
    e.Close();
    REQUIRE(v0 == 0);
    REQUIRE(v1 == 1);
    REQUIRE(e.GetValue("Alpha") == 0);
    REQUIRE(e.GetValue("Beta") == 1);
    REQUIRE(e.GetValue("Gamma") == -1);
    REQUIRE(e.GetName(0) == RStringB("Alpha"));
    REQUIRE(e.FirstInvalidValue() == 2);
}

TEST_CASE("dynEnum: case-insensitive add and lookup", "[core][dynEnum]")
{
    Poseidon::DynEnum e;
    int v0 = e.AddValue("Hello");
    int v1 = e.AddValue("World");
    REQUIRE(v0 == 0);
    REQUIRE(v1 == 1);
    REQUIRE(e.GetValue("hello") == 0);
    REQUIRE(e.GetValue("HELLO") == 0);
    REQUIRE(e.GetValue("World") == 1);
    REQUIRE(e.GetValue("missing") == -1);
}

TEST_CASE("idString: table construction and lookup", "[core][idString]")
{
    RStringB strings[] = {"foo", "bar", "baz"};
    Poseidon::IdStringTable table(strings, 3);
    REQUIRE(table.GetId("foo") == 0);
    REQUIRE(table.GetId("bar") == 1);
    REQUIRE(table.GetId("baz") == 2);
    REQUIRE(table.GetId("qux") == -1);
    REQUIRE(table.GetString(0) == RStringB("foo"));
}

TEST_CASE("idString: reverse lookup preserves table order", "[core][idString]")
{
    RStringB strings[] = {"objectId", "objectPosition", "objectCreator", "vehicle", "name", "id"};
    Poseidon::IdStringTable table(strings, 6);

    REQUIRE(table.GetId("name") == 4);
    REQUIRE(table.GetId("id") == 5);
    REQUIRE(table.GetString(4) == RStringB("name"));
    REQUIRE(table.GetString(5) == RStringB("id"));
    REQUIRE(table.GetString(6) == RStringB());
}

TEST_CASE("idString: GetIdString round-trip", "[core][idString]")
{
    RStringB strings[] = {"alpha", "beta"};
    Poseidon::IdStringTable table(strings, 2);
    Poseidon::IdString id = table.GetIdString("alpha");
    REQUIRE(id.GetID() == 0);
    REQUIRE(id.GetValue() == RStringB("alpha"));
    Poseidon::IdString unknown = table.GetIdString("unknown");
    REQUIRE(unknown.GetID() == -1);
    REQUIRE(unknown.GetValue() == RStringB("unknown"));
}
