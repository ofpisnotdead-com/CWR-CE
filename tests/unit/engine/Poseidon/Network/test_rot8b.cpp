#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/Network/Network.hpp>

using namespace Poseidon;

static constexpr float kEncTol = 0.001f;

TEST_CASE("EncodeRot8b full range", "[network][rot8b]")
{
    // At -PI should clamp to -127
    signed char lo = EncodeRot8b(-static_cast<float>(H_PI) * 2.0f);
    REQUIRE(lo == -127);

    // At +PI should clamp to +127
    signed char hi = EncodeRot8b(static_cast<float>(H_PI) * 2.0f);
    REQUIRE(hi == 127);

    // At 0 should encode to 0
    signed char zero = EncodeRot8b(0.0f);
    REQUIRE(zero == 0);
}

TEST_CASE("DecodeRot8b round-trip endpoints", "[network][rot8b]")
{
    // -127 and +127 decode back to the expected range
    float loDecoded = DecodeRot8b(-127);
    float hiDecoded = DecodeRot8b(127);

    REQUIRE(loDecoded == Catch::Approx(-static_cast<float>(H_PI)).margin(kEncTol));
    REQUIRE(hiDecoded == Catch::Approx(static_cast<float>(H_PI)).margin(kEncTol));

    float zeroDecoded = DecodeRot8b(0);
    REQUIRE(zeroDecoded == Catch::Approx(0.0f).margin(kEncTol));
}

TEST_CASE("CompareRot8b consistency", "[network][rot8b]")
{
    // CompareRot8b(a, b) == DecodeRot8b(a) - DecodeRot8b(b)
    signed char a = 64;
    signed char b = -32;

    float diff = CompareRot8b(a, b);
    float expected = DecodeRot8b(a) - DecodeRot8b(b);

    REQUIRE(diff == Catch::Approx(expected).margin(kEncTol));

    // Symmetry: CompareRot8b(a, a) == 0
    REQUIRE(CompareRot8b(a, a) == Catch::Approx(0.0f).margin(kEncTol));
    REQUIRE(CompareRot8b(b, b) == Catch::Approx(0.0f).margin(kEncTol));
}
