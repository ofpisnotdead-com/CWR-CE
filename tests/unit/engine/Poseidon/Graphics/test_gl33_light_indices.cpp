#include <catch2/catch_test_macros.hpp>

#include <PoseidonGL33/GL33LightIndices.hpp>

TEST_CASE("GL33 packs eight local-light indices", "[Graphics][GL33][Instancing]")
{
    const int indices[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto packed = Poseidon::GL33LightIndices::Pack(indices, 8);

    CHECK(packed[0] == 0x04030201u);
    CHECK(packed[1] == 0x08070605u);
    CHECK(packed[2] == 8u);
    CHECK(packed[3] == 0u);
}

TEST_CASE("GL33 clamps packed local-light indices", "[Graphics][GL33][Instancing]")
{
    const int indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

    CHECK(Poseidon::GL33LightIndices::Pack(indices, -1) == std::array<std::uint32_t, 4>{});
    CHECK(Poseidon::GL33LightIndices::Pack(indices, 9)[2] == 8u);
}
