#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/Network/Network.hpp>

using namespace Poseidon;

static constexpr float kEncTol = 0.001f;

// The encode truncates towards zero, so a round-trip lands within one H_PI/127 step.
static constexpr float kRoundTripTol = static_cast<float>(H_PI) / 127.0f;

TEST_CASE("EncodeRot8b saturates at the range ends", "[network][rot8b]")
{
    REQUIRE(EncodeRot8b(-static_cast<float>(H_PI) * 2.0f) == -127);
    REQUIRE(EncodeRot8b(static_cast<float>(H_PI) * 2.0f) == 127);
    REQUIRE(EncodeRot8b(0.0f) == 0);
}

TEST_CASE("DecodeRot8b maps the range ends back to -PI and +PI", "[network][rot8b]")
{
    REQUIRE(DecodeRot8b(-127) == Catch::Approx(-static_cast<float>(H_PI)).margin(kEncTol));
    REQUIRE(DecodeRot8b(127) == Catch::Approx(static_cast<float>(H_PI)).margin(kEncTol));
    REQUIRE(DecodeRot8b(0) == Catch::Approx(0.0f).margin(kEncTol));
}

TEST_CASE("EncodeRot8b and DecodeRot8b round-trip signed angles", "[network][rot8b]")
{
    const float angles[] = {-0.25f, -1.0f, -2.0f, -3.0f, 0.25f, 1.0f, 2.0f, 3.0f};
    for (float angle : angles)
    {
        float decoded = DecodeRot8b(EncodeRot8b(angle));
        REQUIRE(decoded == Catch::Approx(angle).margin(kRoundTripTol));
    }
}

TEST_CASE("CompareRot8b returns the decoded difference across zero", "[network][rot8b]")
{
    REQUIRE(CompareRot8b(64, -32) == Catch::Approx(DecodeRot8b(64) - DecodeRot8b(-32)).margin(kEncTol));
    REQUIRE(CompareRot8b(-127, 127) == Catch::Approx(-2.0f * static_cast<float>(H_PI)).margin(kEncTol));
    REQUIRE(CompareRot8b(64, 64) == Catch::Approx(0.0f).margin(kEncTol));
}
