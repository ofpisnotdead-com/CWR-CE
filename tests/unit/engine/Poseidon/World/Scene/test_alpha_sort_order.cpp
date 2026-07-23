#include <catch2/catch_test_macros.hpp>

#include <Poseidon/World/Scene/AlphaSortOrder.hpp>

// The alpha pass draws cloudlets (dust) and objects' blend sections together and must
// sort them far-to-near by camera-space depth, so nearer entries composite on top.

using Poseidon::AlphaSort::AlphaObjectDepth;
using Poseidon::AlphaSort::CompareAlphaDepth;

TEST_CASE("AlphaObjectDepth sorts objects by their far extent", "[scene][alpha]")
{
    // far extent = centre depth + radius, so a co-located object sorts behind a cloudlet
    // at the centre and draws first (its depth-writing blend sections precede the dust).
    REQUIRE(AlphaObjectDepth(50.0f, 3.0f) == 53.0f);
    REQUIRE(CompareAlphaDepth(AlphaObjectDepth(50.0f, 3.0f), 50.0f) < 0);
}

TEST_CASE("CompareAlphaDepth orders far-to-near", "[scene][alpha]")
{
    // larger depth = farther from camera = drawn first (negative)
    REQUIRE(CompareAlphaDepth(100.0f, 10.0f) < 0);
    REQUIRE(CompareAlphaDepth(10.0f, 100.0f) > 0);
}

TEST_CASE("CompareAlphaDepth is a strict, antisymmetric order", "[scene][alpha]")
{
    REQUIRE(CompareAlphaDepth(42.0f, 42.0f) == 0);
    REQUIRE(CompareAlphaDepth(1.0f, 2.0f) == -CompareAlphaDepth(2.0f, 1.0f));
}
