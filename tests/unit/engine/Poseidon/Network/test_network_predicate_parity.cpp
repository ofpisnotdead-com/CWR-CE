#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>

// Behaviour-preservation tests for the server-side network predicates that were
// extracted out of the legacy inline command / vote / role logic. Each case
// transcribes the original expression and asserts the extracted Poseidon::
// predicate matches it across a matrix of inputs, so the extraction can be shown
// to change no behaviour.

// Vote tallies extracted from Voting::Check.
namespace
{
bool VoteThresholdMet_original(int sum, int n, float threshold)
{
    return sum > threshold * n;
}
bool VoteSelectionComplete_original(int first, int second, int rest)
{
    return first >= second + rest;
}
} // namespace

TEST_CASE("Refactor: VoteThresholdMet equals the original Voting::Check tally", "[refactor][network][vote]")
{
    struct Case
    {
        int sum, n;
        float threshold;
    };
    const Case cases[] = {
        {1, 1, 0.5f}, {1, 2, 0.5f},  {2, 2, 0.5f},  {0, 0, 0.5f},    {0, 4, 0.5f},    {2, 4, 0.5f},
        {3, 4, 0.5f}, {5, 10, 0.5f}, {6, 10, 0.5f}, {1, 1, 0.9999f}, {1, 2, 0.9999f},
    };
    for (const auto& c : cases)
    {
        INFO("sum=" << c.sum << " n=" << c.n << " threshold=" << c.threshold);
        REQUIRE(Poseidon::VoteThresholdMet(c.sum, c.n, c.threshold) ==
                VoteThresholdMet_original(c.sum, c.n, c.threshold));
    }
}

TEST_CASE("Refactor: VoteSelectionComplete equals the original Voting::Check selection rule",
          "[refactor][network][vote]")
{
    struct Case
    {
        int first, second, rest;
    };
    const Case cases[] = {
        {1, 0, 0}, {1, 0, 1}, {2, 1, 1}, {3, 1, 1}, {0, 0, 0}, {2, 2, 0}, {5, 2, 2}, {4, 2, 3},
    };
    for (const auto& c : cases)
    {
        INFO("first=" << c.first << " second=" << c.second << " rest=" << c.rest);
        REQUIRE(Poseidon::VoteSelectionComplete(c.first, c.second, c.rest) ==
                VoteSelectionComplete_original(c.first, c.second, c.rest));
    }
}

// Transcriptions of the original NCMT* command gates (the exact `if (...)`
// expressions in NetworkServerMsgOnMessage.cpp). The extracted predicates in
// NetworkServerAuth.hpp must match these for all combinations.
namespace
{
bool CommandFromGameMaster_original(bool dedicated, int from, int gameMaster)
{
    return dedicated && from == gameMaster;
}
bool CommandFromPasswordAdmin_original(bool dedicated, int from, int gameMaster, bool votedAdmin)
{
    return dedicated && from == gameMaster && !votedAdmin;
}
bool CommandFromAdminOrBot_original(int from, int gameMaster, int botClient)
{
    return from == botClient || from == gameMaster;
}
bool VotingOpen_original(bool dedicated, int gameMaster, int noGameMaster)
{
    return dedicated && gameMaster == noGameMaster;
}
} // namespace

TEST_CASE("Refactor: command-auth predicates equal the original NCMT gates", "[refactor][network][admin]")
{
    const int kNone = -2; // stands in for AI_PLAYER (no game master)
    for (int dedicated = 0; dedicated <= 1; dedicated++)
    {
        for (int from = 0; from <= 3; from++)
        {
            for (int gm = 0; gm <= 3; gm++)
            {
                const bool d = dedicated != 0;
                INFO("d=" << d << " from=" << from << " gm=" << gm);
                REQUIRE(Poseidon::CommandFromGameMaster(d, from, gm) == CommandFromGameMaster_original(d, from, gm));
                REQUIRE(Poseidon::CommandFromPasswordAdmin(d, from, gm, false) ==
                        CommandFromPasswordAdmin_original(d, from, gm, false));
                REQUIRE(Poseidon::CommandFromPasswordAdmin(d, from, gm, true) ==
                        CommandFromPasswordAdmin_original(d, from, gm, true));
                REQUIRE(Poseidon::CommandFromAdminOrBot(from, gm, /*bot*/ 3) ==
                        CommandFromAdminOrBot_original(from, gm, 3));
                REQUIRE(Poseidon::VotingOpen(d, gm, kNone) == VotingOpen_original(d, gm, kNone));
            }
        }
    }
}

// Transcriptions of the original UpdateAdminState rights math and the login
// precondition. PR* bit values mirror Network.hpp (PRSideCommander=1,
// PRVotedAdmin=2, PRAdmin=4, PRServer=8) so the test exercises the real masks
// without including it.
namespace
{
constexpr int kPRSideCommander = 1, kPRVotedAdmin = 2, kPRAdmin = 4, kPRServer = 8;

int ComputeAdminRights_original(int rights, bool isGameMaster, bool admin, int adminBit, int votedBit)
{
    rights &= ~(adminBit | votedBit);
    if (isGameMaster)
    {
        rights |= admin ? votedBit : adminBit;
    }
    return rights;
}
bool AdminLoginAllowed_original(bool dedicated, int gameMaster, bool admin, int noGameMaster)
{
    return dedicated && (gameMaster == noGameMaster || admin);
}
} // namespace

TEST_CASE("Refactor: ComputeAdminRights equals the original UpdateAdminState math", "[refactor][network][admin]")
{
    // Other rights bits (SideCommander/Server) must be preserved across the update.
    const int rightsSeeds[] = {0,
                               kPRAdmin,
                               kPRVotedAdmin,
                               kPRSideCommander,
                               kPRServer,
                               kPRSideCommander | kPRServer | kPRAdmin,
                               kPRSideCommander | kPRVotedAdmin};
    for (int seed : rightsSeeds)
    {
        for (int gm = 0; gm <= 1; gm++)
        {
            for (int adm = 0; adm <= 1; adm++)
            {
                const bool isGM = gm != 0;
                const bool admin = adm != 0;
                INFO("seed=" << seed << " isGM=" << isGM << " admin=" << admin);
                REQUIRE(Poseidon::ComputeAdminRights(seed, isGM, admin, kPRAdmin, kPRVotedAdmin) ==
                        ComputeAdminRights_original(seed, isGM, admin, kPRAdmin, kPRVotedAdmin));
            }
        }
    }
}

TEST_CASE("Refactor: AdminLoginAllowed equals the original login precondition", "[refactor][network][admin]")
{
    const int kNone = -2; // AI_PLAYER stand-in
    for (int dedicated = 0; dedicated <= 1; dedicated++)
    {
        for (int gm = -2; gm <= 2; gm++)
        {
            for (int adm = 0; adm <= 1; adm++)
            {
                const bool d = dedicated != 0;
                const bool admin = adm != 0;
                INFO("d=" << d << " gm=" << gm << " admin=" << admin);
                REQUIRE(Poseidon::AdminLoginAllowed(d, gm, admin, kNone) ==
                        AdminLoginAllowed_original(d, gm, admin, kNone));
            }
        }
    }
}

// Transcriptions of the original OnMessagePlayerRole decision logic (the
// unlocked-path accept condition and the roleLocked computation).
namespace
{
bool RoleSwapAllowed_original(int cur, int neu, int from, int ai, int no)
{
    return (cur == ai || cur == no) && neu == from || cur == from && (neu == ai || neu == no) ||
           cur == ai && neu == no || cur == no && neu == ai;
}
bool ShouldLockRole_original(int player, int from, int ai, int no)
{
    return player != ai && player != no && player != from;
}
} // namespace

TEST_CASE("Refactor: player-role predicates equal the original OnMessagePlayerRole logic", "[refactor][network][role]")
{
    const int kAI = -1, kNo = -3;           // AI_PLAYER / NO_PLAYER stand-ins
    const int vals[] = {kAI, kNo, 0, 1, 2}; // sentinels + real player ids
    for (int cur : vals)
    {
        for (int neu : vals)
        {
            for (int from : vals)
            {
                INFO("cur=" << cur << " neu=" << neu << " from=" << from);
                REQUIRE(Poseidon::RoleSwapAllowed(cur, neu, from, kAI, kNo) ==
                        RoleSwapAllowed_original(cur, neu, from, kAI, kNo));
                REQUIRE(Poseidon::ShouldLockRole(neu, from, kAI, kNo) == ShouldLockRole_original(neu, from, kAI, kNo));
            }
        }
    }
}

TEST_CASE("Upload path parent escapes are rejected", "[network][upload]")
{
    REQUIRE_FALSE(Poseidon::PathHasParentEscape("tmp/players/Alice/face.jpg"));
    REQUIRE(Poseidon::PathHasParentEscape("tmp/players/Alice/../Bob/face.jpg"));
    REQUIRE_FALSE(Poseidon::PathHasParentEscape(nullptr));
}

TEST_CASE("Player custom asset paths reject unsafe path components", "[network][assets]")
{
    REQUIRE(Poseidon::BuildNetworkPlayerStorageKey(42) == RString("42"));
    REQUIRE(Poseidon::BuildNetworkPlayerStorageKey(-1).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpDir(42) == RString("tmp/players/42/"));
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("face.jpg")) == RString("tmp/players/42/face.jpg"));
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("face.paa")) == RString("tmp/players/42/face.paa"));

    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("face.gif")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(RString("Alice"), RString("face.jpg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(RString("Al/ice"), RString("face.jpg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("../face.jpg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("nested/face.jpg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("C:face.jpg")).GetLength() == 0);
}

TEST_CASE("Transferred custom asset paths are confined to expected temp namespaces", "[network][assets]")
{
    REQUIRE(Poseidon::IsNetworkTransferredAssetSizeAllowed(128 * 1024, 128 * 1024));
    REQUIRE_FALSE(Poseidon::IsNetworkTransferredAssetSizeAllowed(128 * 1024 + 1, 128 * 1024));
    REQUIRE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/face.jpg"), 128 * 1024,
                                                          128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/face.jpg"),
                                                                128 * 1024 + 1, 128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("../outside.bin"), 1, 128 * 1024));

    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/face.jpg")));
    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/face.paa")));
    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/sound/radio.ogg")));

    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/face.gif")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/../face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Al/ice/face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/squads/SQTAG/logo.paa")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/mpmissions/__CUR_MP.pbo")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("../outside.bin")));
}

TEST_CASE("Transferred custom asset probe maps semantic asset names to temp paths", "[network][assets]")
{
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("player"), RString("42"),
                                                              RString("face.jpg")) ==
            RString("tmp/players/42/face.jpg"));
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("playerFace"), RString("42"),
                                                              RString("face.paa")) ==
            RString("tmp/players/42/face.paa"));

    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("unknown"), RString("Alice"),
                                                              RString("face.jpg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("player"), RString("../Alice"),
                                                              RString("face.jpg")).GetLength() == 0);
}

TEST_CASE("Server player upload paths require safe player names and exact temp prefix", "[network][upload]")
{
    const RString tmp = RString("server-tmp");
    const RString playerDir = RString("server-tmp/players/42/");

    REQUIRE(Poseidon::BuildNetworkServerPlayerUploadDir(tmp, 42) == playerDir);
    REQUIRE(Poseidon::BuildNetworkServerPlayerUploadDir(RString("server-tmp\\session"), 42) ==
            RString("server-tmp/session/players/42/"));
    REQUIRE(Poseidon::BuildNetworkServerPlayerAssetUploadPath(tmp, 42, RString("face.jpg")) ==
            playerDir + RString("face.jpg"));
    REQUIRE(Poseidon::IsSafeNetworkServerPlayerUploadPath(playerDir + RString("face.jpg"), tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/Alice/face.jpg"),
                                                                tmp, 42));

    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/43/face.jpg"),
                                                                tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(playerDir + RString("../face.jpg"),
                                                                tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/4/2/face.jpg"),
                                                                tmp, 42));
}

// Transcription of the original relay guard pair:
//   if (info.dpid == from) continue;
//   if (info.state < minState) continue;
namespace
{
bool RelayEligible_original(int dpid, int state, int from, int minState)
{
    if (dpid == from)
        return false;
    if (state < minState)
        return false;
    return true;
}
} // namespace

TEST_CASE("Refactor: RelayEligible equals the original relay guard pair", "[refactor][network][relay]")
{
    for (int dpid = 0; dpid <= 3; dpid++)
    {
        for (int from = 0; from <= 3; from++)
        {
            for (int state = 0; state <= 5; state++)
            {
                for (int minState = 0; minState <= 5; minState++)
                {
                    INFO("dpid=" << dpid << " from=" << from << " state=" << state << " minState=" << minState);
                    REQUIRE(Poseidon::RelayEligible(dpid, state, from, minState) ==
                            RelayEligible_original(dpid, state, from, minState));
                }
            }
        }
    }
}
