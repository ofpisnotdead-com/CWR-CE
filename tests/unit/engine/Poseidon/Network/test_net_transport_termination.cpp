#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Network/Legacy/NetApi.hpp>
#include <Poseidon/Network/NetTransportClientSession.hpp>
#include <Poseidon/Network/NetTransportTermination.hpp>

TEST_CASE("remote channel drop terminates the client after the configured timeout", "[network][termination][timeout]")
{
    REQUIRE(defaultNetworkParams.dropGap == 90);

    bool sessionTerminated = false;
    NetTerminationReason reason = NTROther;

    REQUIRE(Poseidon::UpdateNetTransportClientDroppedState(false, true, sessionTerminated, reason));
    REQUIRE(sessionTerminated);
    REQUIRE(reason == NTRTimeout);
}

TEST_CASE("Net transport termination preserves version rejection reason", "[network][termination]")
{
    const Poseidon::TerminateSessionPacket packet = Poseidon::BuildNetTransportTerminateSessionPacket(NTRVersion);

    REQUIRE(Poseidon::ParseNetTransportTerminateReason(&packet, sizeof(packet)) == NTRVersion);
}
