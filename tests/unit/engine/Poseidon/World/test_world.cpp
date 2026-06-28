#include <Poseidon/World/World.hpp>
#include <Poseidon/World/WorldChatInput.hpp>
#include <catch2/catch_test_macros.hpp>

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
