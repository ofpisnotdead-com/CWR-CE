#include <catch2/catch_test_macros.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>

#include <cstddef>

extern "C" std::size_t NoPchLandscapeSize();

TEST_CASE("Landscape layout is identical with and without the Poseidon PCH", "[World][Terrain][Layout]")
{
    REQUIRE(sizeof(Poseidon::Landscape) == NoPchLandscapeSize());
}
