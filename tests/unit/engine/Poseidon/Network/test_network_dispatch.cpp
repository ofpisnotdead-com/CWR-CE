#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Network/NetworkServerDispatch.hpp>
#include <Poseidon/Network/NetworkMsgFormat.hpp>

using namespace Poseidon;

// The table-driven dispatcher's foundation (strangler-fig migration of the
// ~1935-line NetworkServer::OnMessage switch). These guard the seam itself:
// a broken lookup index, a table/enum size drift, or a missing default contract
// would silently mis-route or skip authorization for whole message groups.
//
// Broken-state delta: if LookupMsgDescriptor mis-indexed (e.g. off-by-one or
// used the wrong array bound), descriptor->type would not equal the queried
// type for some inputs; the round-trip check below reports the first mismatch.

TEST_CASE("dispatch table covers every NetworkMessageType", "[network][dispatch]")
{
    for (int t = 0; t < static_cast<int>(NMTN); ++t)
    {
        const NetworkMessageType type = static_cast<NetworkMessageType>(t);
        const MsgDescriptor* d = LookupMsgDescriptor(type);
        REQUIRE(d != nullptr);
        // lookup must round-trip: the descriptor at index t describes type t.
        REQUIRE(d->type == type);
        REQUIRE(d->name != nullptr);
    }
}

TEST_CASE("dispatch lookup rejects out-of-range types", "[network][dispatch]")
{
    REQUIRE(LookupMsgDescriptor(NMTNone) == nullptr);
    REQUIRE(LookupMsgDescriptor(NMTN) == nullptr);
    REQUIRE(LookupMsgDescriptor(static_cast<NetworkMessageType>(static_cast<int>(NMTN) + 1)) == nullptr);
    REQUIRE(LookupMsgDescriptor(static_cast<NetworkMessageType>(-100)) == nullptr);
}

TEST_CASE("dispatch table classifies server-origin messages", "[network][dispatch]")
{
    // Server -> client only messages a client must never originate. The central
    // NetworkServer::Authorize rejects these when they arrive from a client.
    const NetworkMessageType serverOrigin[] = {NMTPlayer,     NMTLogout, NMTSquad, NMTPlayerState, NMTIntegrityQuestion,
                                               NMTChangeOwner};
    for (NetworkMessageType t : serverOrigin)
    {
        const MsgDescriptor* d = LookupMsgDescriptor(t);
        REQUIRE(d != nullptr);
        REQUIRE(d->auth == MsgAuth::ServerOrigin);
    }

    // A representative client-originated message stays Public, and no handler is
    // registered yet (dispatch still flows through the OnMessage switch).
    const MsgDescriptor* chat = LookupMsgDescriptor(NMTChat);
    REQUIRE(chat->auth == MsgAuth::Public);
    REQUIRE(chat->routing == MsgRouting::Local);
    REQUIRE(chat->handle == nullptr);
}
