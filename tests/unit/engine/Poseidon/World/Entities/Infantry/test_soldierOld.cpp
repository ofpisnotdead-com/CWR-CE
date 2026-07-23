#include <catch2/catch_test_macros.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>

TEST_CASE("soldierOld.hpp compiles", "[Infantry][soldierOld]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("stealth stance exposure tolerates soldiers without a group", "[Infantry][soldierOld]")
{
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, false, 0, 0) == 0.0f);
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, true, 0, 0) == 0.0f);
}
