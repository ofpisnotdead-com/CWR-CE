#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Network/NetTransportTermination.hpp>

TEST_CASE("Net transport termination preserves version rejection reason", "[network][termination]")
{
    const Poseidon::TerminateSessionPacket packet = Poseidon::BuildNetTransportTerminateSessionPacket(NTRVersion);

    REQUIRE(Poseidon::ParseNetTransportTerminateReason(&packet, sizeof(packet)) == NTRVersion);
}
