#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>

using namespace Poseidon;

static constexpr float kEncTol = 0.001f;

TEST_CASE("EncodedMatrix3 identity round-trip", "[network][encoded_matrix3]")
{
    Matrix3 m;
    m.SetIdentity();

    EncodedMatrix3 enc;
    enc.Encode(m);

    Matrix3 decoded;
    enc.Decode(decoded);

    REQUIRE(decoded.DirectionUp().X() == Catch::Approx(m.DirectionUp().X()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Y() == Catch::Approx(m.DirectionUp().Y()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Z() == Catch::Approx(m.DirectionUp().Z()).margin(kEncTol));

    REQUIRE(decoded.Direction().X() == Catch::Approx(m.Direction().X()).margin(kEncTol));
    REQUIRE(decoded.Direction().Y() == Catch::Approx(m.Direction().Y()).margin(kEncTol));
    REQUIRE(decoded.Direction().Z() == Catch::Approx(m.Direction().Z()).margin(kEncTol));
}

TEST_CASE("EncodedMatrix3 rotation round-trip", "[network][encoded_matrix3]")
{
    // 1-radian rotation around Y axis — non-trivial dir/up components
    Matrix3 m;
    m.SetRotationY(1.0f);

    EncodedMatrix3 enc;
    enc.Encode(m);

    Matrix3 decoded;
    enc.Decode(decoded);

    REQUIRE(decoded.DirectionUp().X() == Catch::Approx(m.DirectionUp().X()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Y() == Catch::Approx(m.DirectionUp().Y()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Z() == Catch::Approx(m.DirectionUp().Z()).margin(kEncTol));

    REQUIRE(decoded.Direction().X() == Catch::Approx(m.Direction().X()).margin(kEncTol));
    REQUIRE(decoded.Direction().Y() == Catch::Approx(m.Direction().Y()).margin(kEncTol));
    REQUIRE(decoded.Direction().Z() == Catch::Approx(m.Direction().Z()).margin(kEncTol));
}

TEST_CASE("EncodedMatrix3 DirectionUp and Direction helpers", "[network][encoded_matrix3]")
{
    Matrix3 m;
    m.SetRotationY(0.5f);

    EncodedMatrix3 enc;
    enc.Encode(m);

    Vector3 up = enc.DirectionUp();
    Vector3 dir = enc.Direction();

    REQUIRE(up.X() == Catch::Approx(m.DirectionUp().X()).margin(kEncTol));
    REQUIRE(up.Y() == Catch::Approx(m.DirectionUp().Y()).margin(kEncTol));
    REQUIRE(up.Z() == Catch::Approx(m.DirectionUp().Z()).margin(kEncTol));

    REQUIRE(dir.X() == Catch::Approx(m.Direction().X()).margin(kEncTol));
    REQUIRE(dir.Y() == Catch::Approx(m.Direction().Y()).margin(kEncTol));
    REQUIRE(dir.Z() == Catch::Approx(m.Direction().Z()).margin(kEncTol));
}

TEST_CASE("EncodedMatrix3 negative Z sign preserved", "[network][encoded_matrix3]")
{
    // dir pointing mostly -Z to exercise the negative _22sign path
    Vector3 dir(0.0f, 0.0f, -1.0f);
    Vector3 up(0.0f, 1.0f, 0.0f);

    Matrix3 m;
    m.SetDirectionAndUp(dir, up);

    EncodedMatrix3 enc;
    enc.Encode(m);

    Matrix3 decoded;
    enc.Decode(decoded);

    REQUIRE(decoded.Direction().Z() < 0.0f);
    REQUIRE(decoded.Direction().X() == Catch::Approx(0.0f).margin(kEncTol));
    REQUIRE(decoded.Direction().Y() == Catch::Approx(0.0f).margin(kEncTol));
}

TEST_CASE("EncodedMatrix3 negative sign preserved", "[network][encoded_matrix3]")
{
    // Exercise matrices that produce negative _21sign and _22sign values.
    Matrix3 m;

    m.SetRotationY(H_PI);
    EncodedMatrix3 enc;
    enc.Encode(m);

    Matrix3 decoded;
    enc.Decode(decoded);

    REQUIRE(decoded.Direction().X() == Catch::Approx(m.Direction().X()).margin(kEncTol));
    REQUIRE(decoded.Direction().Y() == Catch::Approx(m.Direction().Y()).margin(kEncTol));
    REQUIRE(decoded.Direction().Z() == Catch::Approx(m.Direction().Z()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().X() == Catch::Approx(m.DirectionUp().X()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Y() == Catch::Approx(m.DirectionUp().Y()).margin(kEncTol));
    REQUIRE(decoded.DirectionUp().Z() == Catch::Approx(m.DirectionUp().Z()).margin(kEncTol));
}
