#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/TankNetworkStabilization.hpp>
#include <Poseidon/World/WorldChatInput.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
struct TestFrame
{
    int value = 0;

    TestFrame operator*(const TestFrame& other) const { return {value * 10 + other.value}; }
};

struct TestTransform
{
    TestFrame frame;
    TestFrame Orientation() const { return frame; }
};

struct QuarterTurnFrame
{
    int turns = 0;

    QuarterTurnFrame operator*(const QuarterTurnFrame& other) const { return {NormalizeTurns(turns + other.turns)}; }

    int RotateForwardToHeading() const { return NormalizeTurns(turns); }

    static int NormalizeTurns(int value)
    {
        value %= 4;
        return value < 0 ? value + 4 : value;
    }
};

struct QuarterTurnTransform
{
    QuarterTurnFrame frame;
    QuarterTurnFrame Orientation() const { return frame; }
};
} // namespace

TEST_CASE("world compiles", "[world]")
{
    REQUIRE(sizeof(World) > 0);
}

TEST_CASE("multiplayer chat shortcuts are suppressed while chat input is active", "[world][chat][network]")
{
    REQUIRE(Poseidon::ShouldHandleMultiplayerChatShortcut(false));
    REQUIRE_FALSE(Poseidon::ShouldHandleMultiplayerChatShortcut(true));
    REQUIRE_FALSE(Poseidon::ShouldSwitchMultiplayerChannelFromChatInput());
}

TEST_CASE("voice chat route setup only uses explicit unit targets for targeted non-direct channels",
          "[world][chat][network]")
{
    constexpr int globalChannel = 0;
    constexpr int directChannel = 5;
    constexpr int groupChannel = 2;

    REQUIRE_FALSE(Poseidon::ShouldUseExplicitMultiplayerVoiceTargets(globalChannel, 3, globalChannel, directChannel));
    REQUIRE_FALSE(Poseidon::ShouldUseExplicitMultiplayerVoiceTargets(directChannel, 3, globalChannel, directChannel));
    REQUIRE_FALSE(Poseidon::ShouldUseExplicitMultiplayerVoiceTargets(groupChannel, 0, globalChannel, directChannel));
    REQUIRE(Poseidon::ShouldUseExplicitMultiplayerVoiceTargets(groupChannel, 3, globalChannel, directChannel));
}

TEST_CASE("tank network turret updates preserve local crew-controlled turrets", "[world][tank][network]")
{
    REQUIRE(Poseidon::ShouldTransferNetworkTankTurretState(true, true));
    REQUIRE(Poseidon::ShouldTransferNetworkTankTurretState(true, false));
    REQUIRE_FALSE(Poseidon::ShouldTransferNetworkTankTurretState(false, true));
    REQUIRE(Poseidon::ShouldTransferNetworkTankTurretState(false, false));
}

TEST_CASE("commander tank turret stabilization uses main turret frame when mounted on it", "[world][tank][network]")
{
    const TestFrame oldBody{3};
    const TestFrame oldMainTurret{9};
    const TestFrame body{2};
    const TestTransform mainTurret{{7}};

    REQUIRE(Poseidon::SelectCommanderTankTurretOldStabilizationFrame(true, oldBody, oldMainTurret).value == 9);
    REQUIRE(Poseidon::BuildCommanderTankTurretStabilizationFrame(true, body, mainTurret).value == 27);
    REQUIRE(Poseidon::SelectCommanderTankTurretOldStabilizationFrame(false, oldBody, oldMainTurret).value == 3);
    REQUIRE(Poseidon::BuildCommanderTankTurretStabilizationFrame(false, body, mainTurret).value == 2);
}

TEST_CASE("tank camera stabilization separates gunner and mounted commander moving frames", "[world][tank][network]")
{
    const QuarterTurnFrame bodyAfterHullTurn{1};
    const QuarterTurnTransform mainTurretCounterTurn{{-1}};

    const QuarterTurnFrame mountedCommanderFrame =
        Poseidon::BuildCommanderTankTurretStabilizationFrame(true, bodyAfterHullTurn, mainTurretCounterTurn);
    const QuarterTurnFrame gunnerOrHullCommanderFrame =
        Poseidon::BuildCommanderTankTurretStabilizationFrame(false, bodyAfterHullTurn, mainTurretCounterTurn);

    REQUIRE(mountedCommanderFrame.RotateForwardToHeading() == 0);
    REQUIRE(gunnerOrHullCommanderFrame.RotateForwardToHeading() == 1);
}
