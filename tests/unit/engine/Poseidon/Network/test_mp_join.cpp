// The MP-join download flow relies on a specific server
// handshake order: the dedicated server checks the client's MOD set BEFORE the
// password. A wrong mod set returns CRVersion (shared with version mismatch)
// and the password is never reached — which is exactly why a wrong password
// can't be caught before the mods are correct, so the join UI treats the
// password as an up-front gate, not a pre-download validation. This test locks
// that order in place.

#include <Poseidon/Foundation/PoseidonPCH.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <Poseidon/Foundation/Platform/VersionNo.h>
#include <Poseidon/Network/NetTransportPlayerCreation.hpp>
#include <Poseidon/Network/NetTransportPlayerValidation.hpp>
#include <Poseidon/Network/NetTransportProtocol.hpp>

using namespace Poseidon;

namespace
{
#pragma pack(push, 1)
struct Legacy302DiscoveryPacket
{
    unsigned32 magic;
    char name[LEN_SESSION_NAME];
    int32 actualVersion;
    int32 requiredVersion;
    char mission[LEN_MISSION_NAME];
    int32 gameState;
    int32 maxPlayers;
    int16 password;
    int16 port;
    int32 numPlayers;
    MsgSerial request;
    char mod[MOD_LENGTH];
    int16 equalModRequired;
    char versionTag[VERSION_TAG_LENGTH];
};
#pragma pack(pop)

struct SideEffectTrackingMap
{
    int lookups = 0;
    int insertions = 0;

    template <class... Args>
    bool get(Args&&...)
    {
        ++lookups;
        return false;
    }

    template <class... Args>
    bool getFirst(Args&&...)
    {
        ++lookups;
        return false;
    }

    template <class... Args>
    bool getNext(Args&&...)
    {
        ++lookups;
        return false;
    }

    template <class... Args>
    void put(Args&&...)
    {
        ++insertions;
    }

    unsigned card() const { return 0; }
};

void SetField(char* dst, std::size_t n, const char* s)
{
    std::memset(dst, 0, n);
    std::snprintf(dst, n, "%s", s);
}
} // namespace

TEST_CASE("MP 3.03 discovery retains the 3.02 packet layout", "[version][mpjoin][network]")
{
    STATIC_REQUIRE(sizeof(Legacy302DiscoveryPacket) == SESSION_PACKET_SIZE);
    STATIC_REQUIRE(offsetof(Legacy302DiscoveryPacket, actualVersion) == offsetof(SessionPacket, actualVersion));
    STATIC_REQUIRE(offsetof(Legacy302DiscoveryPacket, requiredVersion) == offsetof(SessionPacket, requiredVersion));
    STATIC_REQUIRE(offsetof(Legacy302DiscoveryPacket, mod) == SESSION_PACKET_2);
    STATIC_REQUIRE(offsetof(Legacy302DiscoveryPacket, versionTag) == SESSION_PACKET_3);
    STATIC_REQUIRE(SESSION_PACKET_SIZE == 434);

    SessionPacket status{};
    status.magic = MAGIC_ENUM_RESPONSE;
    status.actualVersion = APP_VERSION_NUM;
    status.requiredVersion = APP_VERSION_NUM;
    SetField(status.name, sizeof(status.name), "CWR 3.03 Server");
    SetField(status.versionTag, sizeof(status.versionTag), "rc303");

    Legacy302DiscoveryPacket parsedBy302{};
    std::memcpy(&parsedBy302, &status, sizeof(parsedBy302));

    REQUIRE(parsedBy302.magic == MAGIC_ENUM_RESPONSE);
    REQUIRE(std::strcmp(parsedBy302.name, "CWR 3.03 Server") == 0);
    REQUIRE(parsedBy302.actualVersion == 303);
    REQUIRE(parsedBy302.requiredVersion == 303);
    REQUIRE(std::strcmp(parsedBy302.versionTag, "rc303") == 0);

    constexpr int Legacy302Actual = 302;
    constexpr int Legacy302Required = 302;
    const bool incompatible =
        parsedBy302.actualVersion < Legacy302Required || Legacy302Actual < parsedBy302.requiredVersion;
    REQUIRE(incompatible);
}

TEST_CASE("MP join - 3.02 is rejected before player admission side effects", "[version][mpjoin][network]")
{
    SessionPacket session{};
    session.actualVersion = APP_VERSION_NUM;
    session.requiredVersion = APP_VERSION_NUM;
    session.maxPlayers = 16;
    SetField(session.versionTag, sizeof(session.versionTag), "rc303");

    CreatePlayerPacket request{};
    request.actualVersion = 302;
    request.requiredVersion = 302;
    request.uniqueID = 100;
    SetField(request.versionTag, sizeof(request.versionTag), "rc303");

    SideEffectTrackingMap users;
    SideEffectTrackingMap support;
    AutoArray<CreatePlayerInfo> createPlayers;
    int botId = -1;
    int findChannelCalls = 0;
    int createChannelCalls = 0;
    int supportFactoryCalls = 0;
    struct sockaddr_in distant{};

    REQUIRE(EvaluateNetTransportCreatePlayerRequest(session, "", request) == CRVersion);
    const NetTransportCreatePlayerAttemptResult attempt = ProcessNetTransportCreatePlayerAttempt(
        users, support, createPlayers, botId, session, "", session.maxPlayers, 10, distant, request, nullptr,
        [&]() -> NetChannel*
        {
            ++findChannelCalls;
            return nullptr;
        },
        [&]() -> NetChannel*
        {
            ++createChannelCalls;
            return nullptr;
        },
        [&](int)
        {
            ++supportFactoryCalls;
            return 0;
        });

    REQUIRE(attempt.result == CRVersion);
    REQUIRE(attempt.channel == nullptr);
    REQUIRE(attempt.accepted.info == nullptr);
    REQUIRE(users.lookups == 0);
    REQUIRE(users.insertions == 0);
    REQUIRE(support.insertions == 0);
    REQUIRE(findChannelCalls == 0);
    REQUIRE(createChannelCalls == 0);
    REQUIRE(supportFactoryCalls == 0);
    REQUIRE(createPlayers.Size() == 0);
    REQUIRE(botId == -1);
    // Without a queued CreatePlayerInfo, NetworkServer::OnPlayerCreate is never
    // called, so no player record or per-player dynamic message formats can be
    // registered.
}

TEST_CASE("MP join - server validates mods before password", "[mods][mpjoin][network]")
{
    SessionPacket session{};
    session.actualVersion = 100;
    session.requiredVersion = 100;
    session.equalModRequired = 1;
    SetField(session.mod, sizeof(session.mod), "@csla");

    CreatePlayerPacket req{};
    req.actualVersion = 100;
    req.requiredVersion = 100;

    const char* serverPw = "secret";

    SECTION("mod mismatch → CRVersion even with a wrong password (mods are "
            "checked first)")
    {
        SetField(req.mod, sizeof(req.mod), "@wrong");
        SetField(req.password, sizeof(req.password), "definitely-wrong");
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CRVersion);
    }

    SECTION("mods match (case-insensitively) but wrong password → CRPassword")
    {
        SetField(req.mod, sizeof(req.mod), "@CSLA"); // stricmp in the validator
        SetField(req.password, sizeof(req.password), "nope");
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CRPassword);
    }

    SECTION("mods and password both match → CROK")
    {
        SetField(req.mod, sizeof(req.mod), "@csla");
        SetField(req.password, sizeof(req.password), "secret");
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CROK);
    }

    SECTION("equalModRequired=0 → mods not enforced; only the password gates entry")
    {
        session.equalModRequired = 0;
        SetField(req.mod, sizeof(req.mod), "@totally-different");
        SetField(req.password, sizeof(req.password), "secret");
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CROK);
    }
}

// The version tag (build identity, e.g. "rc1" or the git sha; "-dev" appended
// for
// --dev clients) must match unconditionally — independent of equalModRequired.
// This blocks a --dev client from joining a release server and vice-versa. With
// matching versions, mods, and password but versionTag "rc1" vs "rc2",
// ValidateNetTransportCreatePlayerRequest returns CRVersion, not CROK.
TEST_CASE("MP join - server rejects mismatched version tag", "[version][mpjoin][network]")
{
    SessionPacket session{};
    session.actualVersion = APP_VERSION_NUM;
    session.requiredVersion = APP_VERSION_NUM;
    session.equalModRequired = 0; // mods not enforced — isolates the version-tag gate
    SetField(session.mod, sizeof(session.mod), "@csla");
    SetField(session.versionTag, sizeof(session.versionTag), "rc1");

    CreatePlayerPacket req{};
    req.actualVersion = APP_VERSION_NUM;
    req.requiredVersion = APP_VERSION_NUM;
    SetField(req.mod, sizeof(req.mod), "@csla");

    const char* serverPw = "";
    SetField(req.password, sizeof(req.password), "");

    SECTION("identical version tag → CROK")
    {
        SetField(req.versionTag, sizeof(req.versionTag), "rc1");
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CROK);
    }

    SECTION("differing version tag → CRVersion even when versions, mods, and "
            "password match")
    {
        SetField(req.versionTag, sizeof(req.versionTag), "rc2");
        REQUIRE(IsNetTransportCreatePlayerVersionOrModMismatch(session, req));
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CRVersion);
    }

    SECTION("a --dev client (tag suffixed '-dev') cannot join a release server")
    {
        SetField(session.versionTag, sizeof(session.versionTag), "rc1");
        SetField(req.versionTag, sizeof(req.versionTag), "rc1-dev");
        REQUIRE(IsNetTransportCreatePlayerVersionOrModMismatch(session, req));
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CRVersion);
    }

    SECTION("version-tag match is case-insensitive (stricmp), like the mod compare")
    {
        SetField(session.versionTag, sizeof(session.versionTag), "RC1");
        SetField(req.versionTag, sizeof(req.versionTag), "rc1");
        REQUIRE_FALSE(IsNetTransportCreatePlayerVersionOrModMismatch(session, req));
        REQUIRE(ValidateNetTransportCreatePlayerRequest(session, serverPw, req) == CROK);
    }
}
