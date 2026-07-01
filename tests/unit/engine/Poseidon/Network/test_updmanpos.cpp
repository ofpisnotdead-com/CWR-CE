#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>

TEST_CASE("NetworkUpdManPos field assignment and range", "[network][updmanpos]")
{
    NetworkUpdManPos pos;

    // Assign full range values
    pos.gunXRotWantedC = -127;
    pos.gunYRotWantedC = 127;
    pos.headXRotWantedC = 0;
    pos.headYRotWantedC = 64;

    // Verify values are not truncated
    REQUIRE(pos.gunXRotWantedC == -127);
    REQUIRE(pos.gunYRotWantedC == 127);
    REQUIRE(pos.headXRotWantedC == 0);
    REQUIRE(pos.headYRotWantedC == 64);
}
