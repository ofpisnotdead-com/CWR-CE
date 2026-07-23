#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkPlayerRoleAssignment.hpp>
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

#include <Poseidon/Network/NetworkServerConfig.hpp>

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

TEST_CASE("Player-role self refresh accepts an already-owned slot", "[network][role]")
{
    const int player = 42;
    REQUIRE(Poseidon::RoleSelfRefreshAllowed(player, player, player));
    REQUIRE_FALSE(Poseidon::RoleSelfRefreshAllowed(player, 43, player));
    REQUIRE_FALSE(Poseidon::RoleSelfRefreshAllowed(43, player, player));

    REQUIRE_FALSE(Poseidon::RoleDuplicateClearRequired(-1, 0));
    REQUIRE_FALSE(Poseidon::RoleDuplicateClearRequired(1, 1));
    REQUIRE(Poseidon::RoleDuplicateClearRequired(1, 2));
}

TEST_CASE("Delayed role assignment starts per-player mission catch-up", "[network][mission]")
{
    const int prepareRole = 8;
    const int transferMission = 11;
    const int briefing = 13;
    const int play = 14;

    REQUIRE(Poseidon::DelayedRoleShouldStartMissionCatchUp(true, transferMission, prepareRole, true, transferMission,
                                                           briefing));
    REQUIRE(Poseidon::DelayedRoleShouldStartMissionCatchUp(true, briefing, transferMission, true, transferMission,
                                                           briefing));

    REQUIRE_FALSE(Poseidon::DelayedRoleShouldStartMissionCatchUp(false, briefing, transferMission, true,
                                                                 transferMission, briefing));
    REQUIRE_FALSE(Poseidon::DelayedRoleShouldStartMissionCatchUp(true, prepareRole, prepareRole, true, transferMission,
                                                                 briefing));
    REQUIRE_FALSE(
        Poseidon::DelayedRoleShouldStartMissionCatchUp(true, briefing, briefing, true, transferMission, briefing));
    REQUIRE_FALSE(Poseidon::DelayedRoleShouldStartMissionCatchUp(true, briefing, transferMission, false,
                                                                 transferMission, briefing));
    REQUIRE_FALSE(
        Poseidon::DelayedRoleShouldStartMissionCatchUp(true, play, transferMission, true, transferMission, briefing));
}

TEST_CASE("Delayed mission participants ignore stale ready states", "[network][mission]")
{
    const int prepareSide = 6;
    const int prepareRole = 7;
    const int prepareOk = 8;
    const int transferMission = 11;
    const int loadIsland = 12;
    const int briefing = 13;
    const int play = 14;

    REQUIRE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareOk, transferMission, transferMission, prepareOk,
                                                         transferMission, briefing));
    REQUIRE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareRole, transferMission, loadIsland, prepareOk,
                                                         transferMission, briefing));
    REQUIRE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareOk, briefing, briefing, prepareOk, transferMission,
                                                         briefing));

    REQUIRE_FALSE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareOk, prepareOk, transferMission, prepareOk,
                                                               transferMission, briefing));
    REQUIRE_FALSE(Poseidon::ShouldIgnoreStaleMissionReadyState(play, transferMission, transferMission, prepareOk,
                                                               transferMission, briefing));
    REQUIRE_FALSE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareOk, transferMission, prepareSide, prepareOk,
                                                               transferMission, briefing));
    REQUIRE_FALSE(Poseidon::ShouldIgnoreStaleMissionReadyState(prepareOk, transferMission, play, prepareOk,
                                                               transferMission, briefing));
}

TEST_CASE("Joining players receive global phases only after the prior boundary", "[network][mission]")
{
    const int create = 2;
    const int transferMission = 11;
    const int loadIsland = 12;
    const int briefing = 13;
    const int play = 14;

    REQUIRE_FALSE(Poseidon::ShouldBroadcastNetworkMissionState(briefing, true, transferMission, true, transferMission,
                                                               loadIsland, briefing, play));
    REQUIRE_FALSE(Poseidon::ShouldBroadcastNetworkMissionState(play, true, create, true, transferMission, loadIsland,
                                                               briefing, play));

    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(loadIsland, true, create, true, transferMission, loadIsland,
                                                         briefing, play));
    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(briefing, true, loadIsland, true, transferMission, loadIsland,
                                                         briefing, play));
    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(play, true, briefing, true, transferMission, loadIsland,
                                                         briefing, play));
    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(play, false, create, true, transferMission, loadIsland,
                                                         briefing, play));
    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(transferMission, true, create, true, transferMission,
                                                         loadIsland, briefing, play));

    const int prepareRole = 7;
    REQUIRE(Poseidon::ShouldBroadcastNetworkMissionState(prepareRole, false, create, false, transferMission, loadIsland,
                                                         briefing, play));
    REQUIRE_FALSE(Poseidon::ShouldBroadcastNetworkMissionState(transferMission, false, create, false, transferMission,
                                                               loadIsland, briefing, play));
    REQUIRE_FALSE(Poseidon::ShouldBroadcastNetworkMissionState(play, false, create, false, transferMission, loadIsland,
                                                               briefing, play));
}

TEST_CASE("Client mission state replay does not regress an already loaded client", "[network][mission]")
{
    const int transferMission = 11;
    const int loadIsland = 12;
    const int briefing = 13;

    REQUIRE(Poseidon::ClientShouldAcceptTransferState(transferMission, loadIsland));
    REQUIRE_FALSE(Poseidon::ClientShouldAcceptTransferState(loadIsland, loadIsland));
    REQUIRE_FALSE(Poseidon::ClientShouldAcceptTransferState(briefing, loadIsland));

    REQUIRE(Poseidon::ClientShouldPrepareLoadIsland(transferMission, loadIsland));
    REQUIRE_FALSE(Poseidon::ClientShouldPrepareLoadIsland(loadIsland, loadIsland));
    REQUIRE_FALSE(Poseidon::ClientShouldPrepareLoadIsland(briefing, loadIsland));
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
    REQUIRE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/face.jpg"), 128 * 1024, 128 * 1024));
    REQUIRE_FALSE(
        Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/face.jpg"), 128 * 1024 + 1, 128 * 1024));
    REQUIRE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/squads/SQTAG/logo.paa"), 128 * 1024 + 1,
                                                          128 * 1024));
    REQUIRE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/squads/SQTAG/logo.paa"),
                                                          Poseidon::NetworkSquadFileMaxSize, 128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/squads/SQTAG/logo.paa"),
                                                                Poseidon::NetworkSquadFileMaxSize + 1, 128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString(), 1, 128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("../outside.bin"), 1, 128 * 1024));

    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/face.jpg")));
    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/face.paa")));
    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/sound/radio.ogg")));
    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/squads/SQTAG/logo.paa")));
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString("face.jpg")) ==
            Poseidon::NetworkPlayerUploadKind::Face);
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString("face.paa")) ==
            Poseidon::NetworkPlayerUploadKind::Face);
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString("sound/radio.ogg")) ==
            Poseidon::NetworkPlayerUploadKind::Sound);
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString("sound\\radio.ogg")) ==
            Poseidon::NetworkPlayerUploadKind::Sound);
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString()) == Poseidon::NetworkPlayerUploadKind::Other);
    REQUIRE(Poseidon::ClassifyNetworkPlayerUploadRelativePath(RString("radio.ogg")) ==
            Poseidon::NetworkPlayerUploadKind::Other);

    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/sound/radio.ogg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/face.gif")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Alice/../face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Al/ice/face.jpg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/mpmissions/__CUR_MP.pbo")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("../outside.bin")));
}

TEST_CASE("Mission transfer cache paths reject traversal in server-provided mission names",
          "[network][mission][transfer][security]")
{
    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("co06_clean_sweep.Intro").GetLength() > 0);

    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("../escape").GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("nested/escape").GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("/tmp/escape").GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("C:/Windows/System32/escape").GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkMissionTransferCachePboPath("C:\\Windows\\System32\\escape").GetLength() == 0);
}

TEST_CASE("Transferred custom asset probe maps semantic asset names to temp paths", "[network][assets]")
{
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("player"), RString("42"), RString("face.jpg")) ==
            RString("tmp/players/42/face.jpg"));
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(
                RString("playerFace"), RString("42"), RString("face.paa")) == RString("tmp/players/42/face.paa"));

    REQUIRE(
        Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("unknown"), RString("Alice"), RString("face.jpg"))
            .GetLength() == 0);
    REQUIRE(
        Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("player"), RString("../Alice"), RString("face.jpg"))
            .GetLength() == 0);
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
    REQUIRE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/server-tmp/players/42/face.jpg"), tmp, 42));
    REQUIRE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/server-tmp/players/42/sound/ack.ogg"), tmp,
                                                          42));
    REQUIRE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/face.jpg"), tmp, 42));
    REQUIRE(
        Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/sound/ack.ogg"), tmp, 42));
    REQUIRE(Poseidon::NormalizeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/face.jpg"), tmp,
                                                             42) == playerDir + RString("face.jpg"));
    REQUIRE(Poseidon::NormalizeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/sound/ack.ogg"), tmp,
                                                             42) == playerDir + RString("sound/ack.ogg"));
    REQUIRE(
        Poseidon::ClassifyNetworkServerPlayerUploadPath(
            Poseidon::NormalizeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/face.jpg"), tmp, 42),
            tmp, 42) == Poseidon::NetworkPlayerUploadKind::Face);
    REQUIRE(Poseidon::ClassifyNetworkServerPlayerUploadPath(
                Poseidon::NormalizeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/sound/ack.ogg"),
                                                                 tmp, 42),
                tmp, 42) == Poseidon::NetworkPlayerUploadKind::Sound);
    REQUIRE(Poseidon::NormalizeNetworkServerPlayerUploadPath(playerDir + RString("face.jpg"), tmp, 42) ==
            playerDir + RString("face.jpg"));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/Alice/face.jpg"), tmp, 42));

    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/43/face.jpg"), tmp, 42));
    REQUIRE_FALSE(
        Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/server-tmp/players/43/face.jpg"), tmp, 42));
    REQUIRE_FALSE(
        Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/43/sound/ack.ogg"), tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(playerDir + RString("../face.jpg"), tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/4/2/face.jpg"), tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/players/42/face.jpg"), tmp, 42));
    REQUIRE_FALSE(
        Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/42/nested/face.jpg"), tmp, 42));
    REQUIRE(Poseidon::NormalizeNetworkServerPlayerUploadPath(RString("/tmp/cwr/Tmp1985/players/43/face.jpg"), tmp, 42)
                .GetLength() == 0);
}

TEST_CASE("Player custom sound paths reject unsafe path components", "[network][assets][sound]")
{
    const RString localSoundDir = RString("users") + RString(PATH_SEP_STR) + RString("Alice") + RString(PATH_SEP_STR) +
                                  RString("sound") + RString(PATH_SEP_STR);

    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpDir(42) == RString("tmp/players/42/sound/"));
    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpPath(42, RString("radio.ogg")) ==
            RString("tmp/players/42/sound/radio.ogg"));
    REQUIRE(Poseidon::BuildNetworkCustomRadioSoundPath(RString("42"), RString("radio.ogg"), localSoundDir) ==
            RString("tmp/players/42/sound/radio.ogg"));
    REQUIRE(
        Poseidon::BuildNetworkCustomRadioSoundPath(RString("Alice"), RString("radio.ogg"), localSoundDir).GetLength() ==
        0);
    REQUIRE(Poseidon::BuildNetworkCustomRadioSoundPath(RString(), RString("radio.ogg"), localSoundDir) ==
            localSoundDir + RString("radio.ogg"));

    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpPath(RString("Al/ice"), RString("radio.ogg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpPath(42, RString("../radio.ogg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpPath(42, RString("nested/radio.ogg")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkPlayerSoundTmpPath(42, RString("C:radio.ogg")).GetLength() == 0);
    REQUIRE(
        Poseidon::BuildNetworkCustomRadioSoundPath(RString("42"), RString("../radio.ogg"), localSoundDir).GetLength() ==
        0);
    REQUIRE(Poseidon::BuildNetworkCustomRadioSoundPath(RString(), RString("../radio.ogg"), localSoundDir).GetLength() ==
            0);
}

TEST_CASE("Custom radio menu text decodes legacy filenames for supported languages",
          "[network][assets][sound][encoding]")
{
    struct Case
    {
        RString filename;
        Poseidon::Codepage preferredCodepage;
        RString expected;
    };

    const Case cases[] = {
        {RString("BO\x8E"
                 "E, TO JE ALE SPOU\x8A\x8D.ogg"),
         Poseidon::Codepage::CP1252,
         RString("BO\xC5\xBD"
                 "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4")},
        {RString("Gr\xFC\xDF"
                 "e caf\xE9 se\xF1"
                 "or.ogg"),
         Poseidon::Codepage::CP1252,
         RString("Gr\xC3\xBC\xC3\x9F"
                 "e caf\xC3\xA9 se\xC3\xB1"
                 "or")},
        {RString("\xCF\xF0\xE8\xE2\xE5\xF2.ogg"), Poseidon::Codepage::CP1252,
         RString("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82")},
        {RString("\xC4\x8C"
                 "au Gr\xC3\xBC\xC3\x9F"
                 "e.ogg"),
         Poseidon::Codepage::CP1250,
         RString("\xC4\x8C"
                 "au Gr\xC3\xBC\xC3\x9F"
                 "e")},
        {RString("BO\xC5\xBD"
                 "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4.ogg"),
         Poseidon::Codepage::CP1252,
         RString("BO\xC5\xBD"
                 "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4")},
    };

    for (const Case& c : cases)
    {
        REQUIRE(Poseidon::BuildNetworkCustomRadioMenuText(c.filename, c.preferredCodepage) == c.expected);
    }

    const char* languages[] = {"English",    "French",  "German",    "Italian",   "Spanish",   "Dutch",
                               "Portuguese", "Danish",  "Swedish",   "Norwegian", "Finnish",   "Icelandic",
                               "Czech",      "Polish",  "Slovak",    "Hungarian", "Slovenian", "Croatian",
                               "Romanian",   "Russian", "Ukrainian", "Bulgarian"};
    const RString reported = RString("BO\x8E"
                                     "E, TO JE ALE SPOU\x8A\x8D.ogg");
    const RString expected = RString("BO\xC5\xBD"
                                     "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4");
    for (const char* language : languages)
    {
        INFO(language);
        REQUIRE(Poseidon::BuildNetworkCustomRadioMenuText(reported, Poseidon::CodepageForLanguage(language)) ==
                expected);
    }

    const RString reportedUtf8 = RString("BO\xC5\xBD"
                                         "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4.ogg");
    for (const char* language : languages)
    {
        INFO(language);
        REQUIRE(Poseidon::BuildNetworkCustomRadioMenuText(reportedUtf8, Poseidon::CodepageForLanguage(language)) ==
                expected);
    }
}

TEST_CASE("Transferred custom sound paths are confined to player temp sound directories", "[network][assets][sound]")
{
    REQUIRE(Poseidon::IsNetworkTransferredAssetSizeAllowed(128 * 1024, 128 * 1024));
    REQUIRE_FALSE(Poseidon::IsNetworkTransferredAssetSizeAllowed(128 * 1024 + 1, 128 * 1024));
    REQUIRE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/sound/radio.ogg"), 128 * 1024,
                                                          128 * 1024));
    REQUIRE_FALSE(Poseidon::ShouldAcceptNetworkTransferredAsset(RString("tmp/players/42/sound/radio.ogg"),
                                                                128 * 1024 + 1, 128 * 1024));

    REQUIRE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/sound/radio.ogg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/sound/nested/radio.ogg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/42/sound/../radio.ogg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("tmp/players/Al/ice/sound/radio.ogg")));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkTransferredAssetPath(RString("../outside.bin")));
}

TEST_CASE("Transferred custom sound probe maps semantic asset names to temp paths", "[network][assets][sound]")
{
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("playerSound"), RString("42"),
                                                               RString("radio.ogg")) ==
            RString("tmp/players/42/sound/radio.ogg"));
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("sound"), RString("42"), RString("radio.ogg")) ==
            RString("tmp/players/42/sound/radio.ogg"));

    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("playerSound"), RString("42"),
                                                               RString("nested/radio.ogg"))
                .GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("playerSound"), RString("../Alice"),
                                                               RString("radio.ogg"))
                .GetLength() == 0);
}

TEST_CASE("Server player upload paths accept custom sounds only under the player's safe prefix",
          "[network][upload][sound]")
{
    const RString tmp = RString("server-tmp");
    const RString playerDir = RString("server-tmp/players/42/");
    const RString soundDir = playerDir + RString("sound/");

    REQUIRE(Poseidon::BuildNetworkServerPlayerUploadDir(tmp, 42) == playerDir);
    REQUIRE(Poseidon::BuildNetworkServerPlayerSoundUploadDir(tmp, 42) == soundDir);
    REQUIRE(Poseidon::BuildNetworkServerPlayerSoundUploadPath(tmp, 42, RString("radio.ogg")) ==
            soundDir + RString("radio.ogg"));
    REQUIRE(Poseidon::IsSafeNetworkServerPlayerUploadPath(soundDir + RString("radio.ogg"), tmp, 42));

    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(soundDir + RString("nested/radio.ogg"), tmp, 42));
    REQUIRE_FALSE(
        Poseidon::IsSafeNetworkServerPlayerUploadPath(RString("server-tmp/players/43/sound/radio.ogg"), tmp, 42));
    REQUIRE_FALSE(Poseidon::IsSafeNetworkServerPlayerUploadPath(soundDir + RString("../radio.ogg"), tmp, 42));
}

TEST_CASE("Squad logo paths reject unsafe XML path components", "[network][squad][assets]")
{
    REQUIRE(Poseidon::NetworkSquadFileMaxSize == 1024 * 1024);
    REQUIRE(Poseidon::IsNetworkSquadFileSizeAllowed(Poseidon::NetworkSquadFileMaxSize));
    REQUIRE_FALSE(Poseidon::IsNetworkSquadFileSizeAllowed(Poseidon::NetworkSquadFileMaxSize + 1));

    REQUIRE(Poseidon::BuildNetworkSquadPictureRelativePath(RString("CWR"), RString("synthetic_grid.paa")) ==
            RString("squads/CWR/synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkSquadPictureTmpPath(RString("CWR"), RString("synthetic_grid.paa")) ==
            RString("tmp/squads/CWR/synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkServerSquadPictureUploadPath(RString("server-tmp"), RString("CWR"),
                                                               RString("synthetic_grid.paa")) ==
            RString("server-tmp/squads/CWR/synthetic_grid.paa"));
    REQUIRE(Poseidon::NetworkSquadPictureStorageName(
                RString("https://gist.githubusercontent.com/simi/b4dbb7fea11cb4c7e7b1c090e5e065bc/raw/"
                        "synthetic_grid.paa")) == RString("synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkSquadPictureTmpPath(
                RString("CWR"), RString("https://gist.githubusercontent.com/simi/b4dbb7fea11cb4c7e7b1c090e5e065bc/raw/"
                                        "synthetic_grid.paa")) == RString("tmp/squads/CWR/synthetic_grid.paa"));

    REQUIRE(Poseidon::BuildNetworkSquadPictureTmpPath(RString("CW/R"), RString("synthetic_grid.paa")).GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkSquadPictureTmpPath(RString("CWR"), RString("../synthetic_grid.paa")).GetLength() ==
            0);
    REQUIRE(Poseidon::BuildNetworkSquadPictureDownloadUrl(
                RString("https://gist.githubusercontent.com/simi/b4dbb7fea11cb4c7e7b1c090e5e065bc/raw/squad.xml"),
                RString("synthetic_grid.paa")) ==
            RString("https://gist.githubusercontent.com/simi/b4dbb7fea11cb4c7e7b1c090e5e065bc/raw/"
                    "synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkSquadPictureDownloadUrl(
                RString("https://gist.githubusercontent.com/simi/b4dbb7fea11cb4c7e7b1c090e5e065bc/raw/squad.xml"),
                RString("../synthetic_grid.paa"))
                .GetLength() == 0);
    REQUIRE(Poseidon::BuildNetworkSquadPictureDownloadUrl(
                RString("https://example.invalid/squad.xml"),
                RString("https://cdn.example.invalid/assets/synthetic_grid.paa")) ==
            RString("https://cdn.example.invalid/assets/synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkSquadPictureDownloadUrl(
                RString("https://example.invalid/squad.xml"),
                RString("https://cdn.example.invalid/assets/../synthetic_grid.paa"))
                .GetLength() == 0);
}

TEST_CASE("Transferred squad asset probe maps semantic names to temp paths", "[network][squad][assets]")
{
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("squad"), RString("CWR"),
                                                               RString("synthetic_grid.paa")) ==
            RString("tmp/squads/CWR/synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("squadPicture"), RString("CWR"),
                                                               RString("synthetic_grid.paa")) ==
            RString("tmp/squads/CWR/synthetic_grid.paa"));
    REQUIRE(Poseidon::BuildNetworkTransferredAssetProbeTmpPath(RString("squad"), RString("CW/R"),
                                                               RString("synthetic_grid.paa"))
                .GetLength() == 0);
}

TEST_CASE("Player-role assignment request does not mutate the local role table", "[network][role]")
{
    struct Role
    {
        int player;
        bool roleLocked;
    };

    AutoArray<Role> roles;
    roles.Resize(3);
    roles[0] = {101, true};
    roles[1] = {-1, false};
    roles[2] = {202, true};

    const Role request = Poseidon::BuildNetworkPlayerRoleAssignmentRequest(roles[1], 101);

    REQUIRE(request.player == 101);
    REQUIRE_FALSE(request.roleLocked);

    REQUIRE(roles[0].player == 101);
    REQUIRE(roles[0].roleLocked);
    REQUIRE(roles[1].player == -1);
    REQUIRE_FALSE(roles[1].roleLocked);
    REQUIRE(roles[2].player == 202);
    REQUIRE(roles[2].roleLocked);
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

TEST_CASE("Server MaxCustomFileSize is read from config with a fallback", "[network][config][sound]")
{
    // Absent key -> keep the built-in default (no server-side change).
    ParamFile absent;
    REQUIRE(Poseidon::NetworkMaxCustomFileSizeFromCfg(absent, 131072) == 131072);

    // Present key -> server-configured cap (e.g. shrink the allowance).
    ParamFile capped;
    capped.Add("MaxCustomFileSize", 4096);
    REQUIRE(Poseidon::NetworkMaxCustomFileSizeFromCfg(capped, 131072) == 4096);

    // 0 -> block all custom files (any non-empty transfer is rejected/kicked).
    ParamFile blocked;
    blocked.Add("MaxCustomFileSize", 0);
    REQUIRE(Poseidon::NetworkMaxCustomFileSizeFromCfg(blocked, 131072) == 0);
}
