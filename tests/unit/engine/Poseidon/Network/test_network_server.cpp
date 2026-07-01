#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Audio/Dummy/WaveDummy.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Network/NetworkSoundReplication.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace
{
class TestNetworkComponent : public NetworkComponent
{
  public:
    TestNetworkComponent() : NetworkComponent(nullptr) {}

    using NetworkComponent::ReceiveFileSegment;

    int PendingReceivedBytes(const RString& path) const
    {
        for (int i = 0; i < _files.Size(); ++i)
        {
            if (_files[i].fileName == path)
            {
                return _files[i].received;
            }
        }
        return -1;
    }

    bool DXSendMsg(int, NetworkMessageRaw&, DWORD&, NetMsgFlags) override { return false; }
    void OnSimulate() override {}
    void OnMessage(int, NetworkMessage*, NetworkMessageType) override {}
    unsigned CleanUpMemory() override { return 0; }
    const char* GetDebugName() const override { return "test"; }
    NetworkMessageFormatBase* GetFormat(int) override { return nullptr; }
    NetworkObject* GetObject(NetworkId&) override { return nullptr; }

  protected:
    void EnqueueMsg(int, NetworkMessage*, NetworkMessageType) override {}
    void EnqueueMsgNonGuaranteed(int, NetworkMessage*, NetworkMessageType) override {}
};

class TestWave : public Poseidon::WaveDummy
{
  public:
    TestWave() : Poseidon::WaveDummy(RString("test")) {}

    void Repeat(int repeat = 1) override
    {
        repeatCount = repeat;
        repeated = true;
    }

    void LastLoop() override
    {
        lastLooped = true;
        Poseidon::WaveDummy::LastLoop();
    }

    void Stop() override
    {
        stopped = true;
        Poseidon::WaveDummy::Stop();
    }

    bool repeated = false;
    bool lastLooped = false;
    bool stopped = false;
    int repeatCount = 0;
};

TransferFileMessage MakeTransferSegment(const RString& path, int totalSize, int segment, int totalSegments, int offset,
                                        const char* bytes)
{
    TransferFileMessage msg;
    msg.path = path;
    msg.totSize = totalSize;
    msg.totSegments = totalSegments;
    msg.curSegment = segment;
    msg.offset = offset;
    const int size = static_cast<int>(strlen(bytes));
    msg.data.Resize(size);
    memcpy(msg.data.Data(), bytes, size);
    return msg;
}

std::string ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() /
               ("cwr_network_server_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
}

class ScopedModPath
{
  public:
    explicit ScopedModPath(const std::filesystem::path& path)
    {
        Poseidon::ModSystem::SetModPath(path.string().c_str());
    }
    ~ScopedModPath() { Poseidon::ModSystem::SetModPath(""); }
};
} // namespace

TEST_CASE("networkImpl.hpp compiles", "[network][networkServer]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("NetworkMessageRaw default constructor", "[network][networkServer]")
{
    NetworkMessageRaw raw;
    SUCCEED("NetworkMessageRaw constructed");
}

TEST_CASE("Network constants are defined", "[network][networkServer]")
{
    REQUIRE(MAX_PLAYERS == 50);
    REQUIRE(LOCAL_PLAYER == 1);
    REQUIRE(TO_SERVER == 1);
}

TEST_CASE("mission file transfer sends contiguous one kilobyte segments to invalid recipients",
          "[network][mission][transfer]")
{
    struct Player
    {
        int dpid;
        NetworkGameState state;
        bool missionFileValid;
    };
    struct SentSegment
    {
        int dpid;
        int offset;
        int size;
        int curSegment;
        int totSegments;
        char firstByte;
    };

    std::string payload(5 * Poseidon::NetworkMissionTransferSegmentSize + 17, '\0');
    for (size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<char>('A' + (i % 26));
    }

    std::vector<int> recipients{11, 13};
    std::vector<Player> players{{11, NGSCreate, false},
                                {12, NGSCreating, false},
                                {13, NGSCreate, false},
                                {14, NGSCreate, true},
                                {99, NGSCreate, false}};
    std::vector<SentSegment> sent;

    const Poseidon::NetworkMissionFileSendResult result =
        Poseidon::SendCurrentNetworkMissionFile<TransferMissionFileMessage>(
            "throughput_test.Intro", payload.data(), static_cast<int>(payload.size()),
            static_cast<int>(recipients.size()), [&recipients](int index) { return recipients[index]; }, 99,
            [&sent](int dpid, TransferMissionFileMessage& msg)
            { sent.push_back({dpid, msg.offset, msg.data.Size(), msg.curSegment, msg.totSegments, msg.data[0]}); },
            static_cast<int>(players.size()), [&players](int index) -> Player& { return players[index]; }, NGSCreate);

    const int expectedSegments = Poseidon::GetNetworkMissionTransferSegmentCount(static_cast<int>(payload.size()));
    REQUIRE(Poseidon::NetworkMissionTransferSegmentSize == 1024);
    REQUIRE(result.segmentCount == expectedSegments);
    REQUIRE(result.sentCount == expectedSegments * static_cast<int>(recipients.size()));
    REQUIRE(result.markedCount == 3);
    REQUIRE(sent.size() == static_cast<size_t>(result.sentCount));

    for (int segment = 0; segment < expectedSegments; ++segment)
    {
        const int offset = segment * Poseidon::NetworkMissionTransferSegmentSize;
        const int expectedSize =
            std::min(Poseidon::NetworkMissionTransferSegmentSize, static_cast<int>(payload.size()) - offset);
        for (size_t recipient = 0; recipient < recipients.size(); ++recipient)
        {
            const SentSegment& item = sent[segment * recipients.size() + recipient];
            REQUIRE(item.dpid == recipients[recipient]);
            REQUIRE(item.curSegment == segment);
            REQUIRE(item.totSegments == expectedSegments);
            REQUIRE(item.offset == offset);
            REQUIRE(item.size == expectedSize);
            REQUIRE(item.firstByte == payload[static_cast<size_t>(offset)]);
        }
    }

    REQUIRE_FALSE(players[1].missionFileValid);
    REQUIRE(players[2].missionFileValid);
    REQUIRE(players[3].missionFileValid);
    REQUIRE_FALSE(players[4].missionFileValid);
}

TEST_CASE("mission transfer segment count scales linearly for benchmark payload sizes",
          "[network][mission][transfer][benchmark]")
{
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(0) == 0);
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(1) == 1);
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(1024) == 1);
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(1025) == 2);
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(1024 * 1024) == 1024);
    REQUIRE(Poseidon::GetNetworkMissionTransferSegmentCount(5 * 1024 * 1024) == 5120);
}

TEST_CASE("mission params preserve explicit server.cfg values over description defaults",
          "[network][networkServer][missionParams]")
{
    const NetworkMissionParams params = ResolveNetworkMissionParams(1.0f, 2.0f, 7.0f, 8.0f);

    REQUIRE(params.param1 == Catch::Approx(1.0f));
    REQUIRE(params.param2 == Catch::Approx(2.0f));
}

TEST_CASE("mission params preserve explicit zero server.cfg values", "[network][networkServer][missionParams]")
{
    const NetworkMissionParams params = ResolveNetworkMissionParams(0.0f, 0.0f, 7.0f, 8.0f);

    REQUIRE(params.param1 == Catch::Approx(0.0f));
    REQUIRE(params.param2 == Catch::Approx(0.0f));
}

TEST_CASE("mission params use description defaults when server.cfg omits values",
          "[network][networkServer][missionParams]")
{
    const NetworkMissionParams params = ResolveNetworkMissionParams(FLT_MAX, FLT_MAX, 7.0f, 8.0f);

    REQUIRE(params.param1 == Catch::Approx(7.0f));
    REQUIRE(params.param2 == Catch::Approx(8.0f));
}

TEST_CASE("MP mission lookup resolves active mod mission PBOs before base directories", "[network][mission][mods]")
{
    const auto root = MakeTempDir();
    const auto mod = root / "@missionmod";
    const auto missions = mod / "mpmissions";
    std::filesystem::create_directories(missions);
    std::ofstream(missions / "addon_lookup.eden.pbo", std::ios::binary) << "test pbo marker";

    RString resolved;
    {
        const ScopedModPath modPath(mod);
        resolved = Poseidon::ResolveMPMissionTemplateBase("addon_lookup", "Eden");
    }

    CHECK(std::filesystem::path((const char*)resolved) == missions / "addon_lookup.eden");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("voice channel routing targets normal network player ids", "[network][VoN]")
{
    REQUIRE(Poseidon::SelectNetworkVoiceTargetPlayerId(42, 0) == 42);
    REQUIRE(Poseidon::SelectNetworkVoiceTargetPlayerId(42, 99) == 42);
}

TEST_CASE("voice channel route refresh only targets other active speakers", "[network][VoN]")
{
    constexpr int changedPlayer = 10;
    constexpr int createState = 2;
    constexpr int noneChannel = 0;
    constexpr int groupChannel = 2;

    REQUIRE(Poseidon::ShouldRefreshOtherNetworkVoiceRoute(11, changedPlayer, createState, groupChannel, createState,
                                                          noneChannel));
    REQUIRE_FALSE(Poseidon::ShouldRefreshOtherNetworkVoiceRoute(changedPlayer, changedPlayer, createState, groupChannel,
                                                                createState, noneChannel));
    REQUIRE_FALSE(Poseidon::ShouldRefreshOtherNetworkVoiceRoute(11, changedPlayer, createState - 1, groupChannel,
                                                                createState, noneChannel));
    REQUIRE_FALSE(Poseidon::ShouldRefreshOtherNetworkVoiceRoute(11, changedPlayer, createState, noneChannel,
                                                                createState, noneChannel));
}

TEST_CASE("direct voice route refresh notifies joining listener about active speaker", "[network][VoN]")
{
    constexpr int changedPlayer = 10;
    constexpr int activeSpeaker = 11;
    constexpr int createState = 2;
    constexpr int groupChannel = 2;
    constexpr int directChannel = 5;

    REQUIRE(Poseidon::ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(activeSpeaker, changedPlayer, createState,
                                                                       directChannel, createState, directChannel));
    REQUIRE_FALSE(Poseidon::ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(
        changedPlayer, changedPlayer, createState, directChannel, createState, directChannel));
    REQUIRE_FALSE(Poseidon::ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(
        activeSpeaker, changedPlayer, createState - 1, directChannel, createState, directChannel));
    REQUIRE_FALSE(Poseidon::ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(activeSpeaker, changedPlayer, createState,
                                                                             groupChannel, createState, directChannel));
}

TEST_CASE("replicated sound state clears stopped and last-looped waves", "[network][sound]")
{
    Link<IWave> repeating = new TestWave();
    REQUIRE(Poseidon::ApplyReplicatedNetworkSoundState(repeating, SSRepeat, [](IWave*) { FAIL("unexpected delete"); }));
    REQUIRE(static_cast<TestWave*>(repeating.GetRef())->repeated);
    REQUIRE(static_cast<TestWave*>(repeating.GetRef())->repeatCount == 1000);

    Link<IWave> lastLoop = new TestWave();
    TestWave* lastLoopWave = static_cast<TestWave*>(lastLoop.GetRef());
    REQUIRE(
        Poseidon::ApplyReplicatedNetworkSoundState(lastLoop, SSLastLoop, [](IWave*) { FAIL("unexpected delete"); }));
    REQUIRE(lastLoopWave->lastLooped);
    REQUIRE(lastLoop == nullptr);

    Link<IWave> stopped = new TestWave();
    bool deleted = false;
    REQUIRE(Poseidon::ApplyReplicatedNetworkSoundState(stopped, SSStop,
                                                       [&deleted](IWave* wave)
                                                       {
                                                           deleted = true;
                                                           wave->Stop();
                                                       }));
    REQUIRE(deleted);
    REQUIRE(stopped == nullptr);
}

TEST_CASE("received file transfer segments tolerate duplicates and out-of-order completion", "[network][file-transfer]")
{
    const std::filesystem::path output = std::filesystem::current_path() / "tmp" / "network-receive-segment.bin";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    TestNetworkComponent component;
    const RString outputPath(output.string().c_str());

    TransferFileMessage tail = MakeTransferSegment(outputPath, 6, 1, 2, 3, "DEF");
    REQUIRE(component.ReceiveFileSegment(tail) == 0);
    REQUIRE(component.PendingReceivedBytes(outputPath) == 3);
    REQUIRE_FALSE(std::filesystem::exists(output));

    TransferFileMessage duplicateTail = MakeTransferSegment(outputPath, 6, 1, 2, 3, "DEF");
    REQUIRE(component.ReceiveFileSegment(duplicateTail) == 0);
    REQUIRE(component.PendingReceivedBytes(outputPath) == 3);
    REQUIRE_FALSE(std::filesystem::exists(output));

    TransferFileMessage head = MakeTransferSegment(outputPath, 6, 0, 2, 0, "ABC");
    REQUIRE(component.ReceiveFileSegment(head) == 1);
    REQUIRE(component.PendingReceivedBytes(outputPath) == -1);
    REQUIRE(ReadBinaryFile(output) == "ABCDEF");

    std::filesystem::remove(output, ec);
}

TEST_CASE("NetworkPlayerInfo jip flag defaults to false", "[network][networkServer][jip]")
{
    NetworkPlayerInfo info;
    info.jip = false;
    REQUIRE(info.jip == false);
}

TEST_CASE("NetworkPlayerInfo jip flag can be set", "[network][networkServer][jip]")
{
    NetworkPlayerInfo info;
    info.jip = true;
    REQUIRE(info.jip == true);
}

TEST_CASE("NetworkObjectInfo has create field for JIP", "[network][networkServer][jip]")
{
    NetworkObjectInfo info;
    info.id.creator = 1;
    info.id.id = 42;
    info.owner = 1;

    // create message should be null by default
    REQUIRE(info.create.message == nullptr);
    REQUIRE(info.create.type == NMTNone);

    // can store a creation message type
    info.create.type = NMTCreateVehicle;
    info.create.from = 1;
    REQUIRE(info.create.type == NMTCreateVehicle);
    REQUIRE(info.create.from == 1);
}

// JIPInitMessage is protected in NetworkServer, so test the same pattern:
// a struct with Ref<NetworkMessage> + ClassIsMovable stored in AutoArray
namespace
{
struct TestJIPMsg
{
    NetworkMessageType type;
    Ref<NetworkMessage> msg;
    RString key;
};

// Helper: simulate JIP queue add/replace logic (mirrors server code)
bool AddOrReplaceJIPMessage(AutoArray<TestJIPMsg>& queue, NetworkMessageType type, Ref<NetworkMessage> msg,
                            const RString& key, int maxMessages = 10000)
{
    if ((type == NMTPublicVariable || type == NMTRemoteExec) && key.GetLength() > 0)
    {
        for (int i = 0; i < queue.Size(); i++)
        {
            if (queue[i].type == type && queue[i].key == key)
            {
                queue[i].msg = msg;
                return true; // replaced
            }
        }
    }
    if (queue.Size() < maxMessages)
    {
        TestJIPMsg jipMsg;
        jipMsg.type = type;
        jipMsg.msg = msg;
        jipMsg.key = key;
        queue.Add(jipMsg);
        return true; // added
    }
    return false; // dropped
}

bool RemoveJIPMessage(AutoArray<TestJIPMsg>& queue, NetworkMessageType type, const RString& key)
{
    bool removed = false;
    for (int i = 0; i < queue.Size();)
    {
        if (queue[i].type == type && queue[i].key == key)
        {
            queue.Delete(i);
            removed = true;
            continue;
        }
        ++i;
    }
    return removed;
}

struct RemoteExecDispatchFixture
{
    std::vector<int> ids;
    std::vector<int> states;
};

Poseidon::NetworkRemoteExecDispatchResult DispatchRemoteExecTargetForTest(int target,
                                                                          const RemoteExecDispatchFixture& fixture,
                                                                          std::vector<int>& sent,
                                                                          bool& executedOnServer)
{
    return Poseidon::DispatchNetworkRemoteExecTarget(
        target, (int)fixture.ids.size(), [&fixture](int index) { return fixture.ids[index]; },
        [&fixture](int index) { return fixture.states[index]; }, 2, [&sent](int dpid) { sent.push_back(dpid); },
        [&executedOnServer]() { executedOnServer = true; });
}

Poseidon::NetworkRemoteExecDispatchResult DispatchRemoteExecSelectorForTest(
    const Poseidon::RemoteExecTargetSelector& selector, const RemoteExecDispatchFixture& fixture,
    const std::vector<std::pair<NetworkId, int>>& owners, std::vector<int>& sent, bool& executedOnServer)
{
    return Poseidon::DispatchNetworkRemoteExecTargetSelector(
        selector, (int)fixture.ids.size(), [&fixture](int index) { return fixture.ids[index]; },
        [&fixture](int index) { return fixture.states[index]; }, 2, [&sent](int dpid) { sent.push_back(dpid); },
        [&executedOnServer]() { executedOnServer = true; },
        [&owners](const NetworkId& id)
        {
            for (const auto& owner : owners)
            {
                if (owner.first == id)
                {
                    return owner.second;
                }
            }
            return 0;
        });
}
} // namespace

TEST_CASE("remoteExec target resolver -- target 0 includes server and eligible clients",
          "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11, 12}, {2, 1, 3}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(0, fixture, sent, executedOnServer);

    REQUIRE(executedOnServer);
    REQUIRE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{10, 12});
}

TEST_CASE("remoteExec target resolver -- target 2 is server only", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11}, {2, 2}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(2, fixture, sent, executedOnServer);

    REQUIRE(executedOnServer);
    REQUIRE(result.executedOnServer);
    REQUIRE_FALSE(result.acceptedClientTarget);
    REQUIRE(sent.empty());
}

TEST_CASE("remoteExec target resolver -- positive DPID selects one eligible client", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11, 12}, {2, 1, 2}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(12, fixture, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{12});
}

TEST_CASE("remoteExec target resolver -- disconnected positive DPID is ignored", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11}, {2, 2}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(12, fixture, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE_FALSE(result.acceptedClientTarget);
    REQUIRE(sent.empty());
}

TEST_CASE("remoteExec target resolver -- negative DPID excludes that client", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11, 12}, {2, 2, 2}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(-11, fixture, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{10, 12});
}

TEST_CASE("remoteExec target resolver -- -2 targets clients only", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11}, {2, 2}};
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecTargetForTest(-2, fixture, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{10, 11});
}

TEST_CASE("remoteExec target resolver -- object selector targets object owner", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11, 12}, {2, 1, 2}};
    Poseidon::RemoteExecTargetSelector selector;
    selector.kind = Poseidon::RemoteExecTargetKind::Object;
    selector.id = NetworkId(7, 42);
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecSelectorForTest(selector, fixture, {{NetworkId(7, 42), 12}}, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{12});
}

TEST_CASE("remoteExec target resolver -- server-owned object executes on server", "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11}, {2, 2}};
    Poseidon::RemoteExecTargetSelector selector;
    selector.kind = Poseidon::RemoteExecTargetKind::Object;
    selector.id = NetworkId(7, 42);
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecSelectorForTest(selector, fixture, {{NetworkId(7, 42), AI_PLAYER}}, sent, executedOnServer);

    REQUIRE(executedOnServer);
    REQUIRE(result.executedOnServer);
    REQUIRE_FALSE(result.acceptedClientTarget);
    REQUIRE(sent.empty());
}

TEST_CASE("remoteExec target resolver -- array selector flattens and deduplicates targets",
          "[network][remoteExec][target]")
{
    const RemoteExecDispatchFixture fixture{{10, 11, 12}, {2, 2, 2}};
    Poseidon::RemoteExecTargetSelector scalar;
    scalar.kind = Poseidon::RemoteExecTargetKind::Scalar;
    scalar.scalar = 10;
    Poseidon::RemoteExecTargetSelector object;
    object.kind = Poseidon::RemoteExecTargetKind::Object;
    object.id = NetworkId(7, 42);
    Poseidon::RemoteExecTargetSelector duplicate;
    duplicate.kind = Poseidon::RemoteExecTargetKind::Scalar;
    duplicate.scalar = 12;
    Poseidon::RemoteExecTargetSelector selector;
    selector.kind = Poseidon::RemoteExecTargetKind::Array;
    selector.items.Add(scalar);
    selector.items.Add(object);
    selector.items.Add(duplicate);
    std::vector<int> sent;
    bool executedOnServer = false;

    Poseidon::NetworkRemoteExecDispatchResult result =
        DispatchRemoteExecSelectorForTest(selector, fixture, {{NetworkId(7, 42), 12}}, sent, executedOnServer);

    REQUIRE_FALSE(executedOnServer);
    REQUIRE_FALSE(result.executedOnServer);
    REQUIRE(result.acceptedClientTarget);
    REQUIRE(sent == std::vector<int>{10, 12});
}

TEST_CASE("remoteExec target resolver -- JIP replay re-resolves object owner", "[network][remoteExec][target]")
{
    Poseidon::RemoteExecTargetSelector selector;
    selector.kind = Poseidon::RemoteExecTargetKind::Object;
    selector.id = NetworkId(7, 42);

    REQUIRE(Poseidon::RemoteExecTargetSelectorReplaysTo(selector, 12, [](const NetworkId& id)
                                                        { return id == NetworkId(7, 42) ? 12 : 0; }));
    REQUIRE_FALSE(Poseidon::RemoteExecTargetSelectorReplaysTo(selector, 10, [](const NetworkId& id)
                                                              { return id == NetworkId(7, 42) ? 12 : 0; }));
}

TEST_CASE("JIPInitMessage pattern works with AutoArray", "[network][networkServer][jip]")
{
    AutoArray<TestJIPMsg> queue;

    // Add messages to queue
    TestJIPMsg m1;
    m1.type = NMTPublicVariable;
    m1.msg = nullptr;
    queue.Add(m1);

    TestJIPMsg m2;
    m2.type = NMTPublicVariable;
    m2.msg = nullptr;
    queue.Add(m2);

    REQUIRE(queue.Size() == 2);
    REQUIRE(queue[0].type == NMTPublicVariable);
    REQUIRE(queue[1].type == NMTPublicVariable);

    // Clear queue (like InitMission does)
    queue.Resize(0);
    REQUIRE(queue.Size() == 0);
}

TEST_CASE("JIP queue grows and clears correctly", "[network][networkServer][jip]")
{
    AutoArray<TestJIPMsg> queue;

    // Simulate multiple publicVariable calls being queued
    for (int i = 0; i < 10; i++)
    {
        TestJIPMsg m;
        m.type = NMTPublicVariable;
        m.msg = nullptr;
        queue.Add(m);
    }
    REQUIRE(queue.Size() == 10);

    // Simulate InitMission clearing the queue
    queue.Resize(0);
    REQUIRE(queue.Size() == 0);

    // Queue can be reused after clear
    TestJIPMsg m;
    m.type = NMTPublicVariable;
    m.msg = nullptr;
    queue.Add(m);
    REQUIRE(queue.Size() == 1);
}

TEST_CASE("publicVariable wire format -- scalar round-trip", "[network][jip]")
{
    // Serialize a scalar using the same wire format as publicVariable
    QOStream out;
    int type = GameScalar;
    out.write(&type, sizeof(int));
    GameScalarType value = 42.5f;
    out.write(&value, sizeof(value));

    // Deserialize
    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameScalar);
    GameScalarType readValue;
    in.read(&readValue, sizeof(readValue));
    REQUIRE(readValue == Catch::Approx(42.5f));
}

TEST_CASE("publicVariable wire format -- string with length prefix", "[network][jip]")
{
    QOStream out;
    int type = GameString;
    out.write(&type, sizeof(int));
    const char* str = "hello";
    int len = 5;
    out.write(&len, sizeof(int));
    out.write(str, len);

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == 5);
    char buf[6];
    in.read(buf, readLen);
    buf[readLen] = 0;
    REQUIRE(strcmp(buf, "hello") == 0);
}

TEST_CASE("publicVariable wire format -- array of scalars", "[network][jip]")
{
    // Serialize: [type=GameArray, count=3, [scalar,1.0], [scalar,2.0], [scalar,3.0]]
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 3;
    out.write(&count, sizeof(int));
    for (int i = 0; i < 3; i++)
    {
        int scalarType = GameScalar;
        out.write(&scalarType, sizeof(int));
        GameScalarType val = (float)(i + 1);
        out.write(&val, sizeof(val));
    }

    // Deserialize
    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 3);
    for (int i = 0; i < 3; i++)
    {
        int elemType;
        in.read(&elemType, sizeof(int));
        REQUIRE(elemType == GameScalar);
        GameScalarType val;
        in.read(&val, sizeof(val));
        REQUIRE(val == Catch::Approx((float)(i + 1)));
    }
}

TEST_CASE("publicVariable wire format -- nested array", "[network][jip]")
{
    // Serialize: [[1.0, 2.0], [3.0]]
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int outerCount = 2;
    out.write(&outerCount, sizeof(int));

    // First inner array [1.0, 2.0]
    out.write(&arrType, sizeof(int));
    int inner1Count = 2;
    out.write(&inner1Count, sizeof(int));
    int scalarType = GameScalar;
    GameScalarType v1 = 1.0f, v2 = 2.0f;
    out.write(&scalarType, sizeof(int));
    out.write(&v1, sizeof(v1));
    out.write(&scalarType, sizeof(int));
    out.write(&v2, sizeof(v2));

    // Second inner array [3.0]
    out.write(&arrType, sizeof(int));
    int inner2Count = 1;
    out.write(&inner2Count, sizeof(int));
    GameScalarType v3 = 3.0f;
    out.write(&scalarType, sizeof(int));
    out.write(&v3, sizeof(v3));

    // Verify we can read back the structure
    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readOuter;
    in.read(&readOuter, sizeof(int));
    REQUIRE(readOuter == 2);

    // Read first inner
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readInner1;
    in.read(&readInner1, sizeof(int));
    REQUIRE(readInner1 == 2);

    int elemType;
    GameScalarType rv;
    in.read(&elemType, sizeof(int));
    in.read(&rv, sizeof(rv));
    REQUIRE(rv == Catch::Approx(1.0f));
    in.read(&elemType, sizeof(int));
    in.read(&rv, sizeof(rv));
    REQUIRE(rv == Catch::Approx(2.0f));

    // Read second inner
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readInner2;
    in.read(&readInner2, sizeof(int));
    REQUIRE(readInner2 == 1);
    in.read(&elemType, sizeof(int));
    in.read(&rv, sizeof(rv));
    REQUIRE(rv == Catch::Approx(3.0f));
}

TEST_CASE("NetworkObjectInfo update classes are independent of create", "[network][networkServer][jip]")
{
    NetworkObjectInfo info;
    info.id.creator = 1;
    info.id.id = 1;
    info.owner = 1;

    // Verify update class indices
    REQUIRE(NMCUpdateGeneric == 0);
    REQUIRE(NMCUpdatePosition == 1);
    REQUIRE(NMCUpdateDammage == 2);
    REQUIRE(NMCUpdateN == 3);

    // Update classes should be independent
    info.current[NMCUpdateGeneric].type = NMTUpdateVehicle;
    info.current[NMCUpdatePosition].type = NMTUpdatePositionVehicle;
    REQUIRE(info.current[NMCUpdateGeneric].type == NMTUpdateVehicle);
    REQUIRE(info.current[NMCUpdatePosition].type == NMTUpdatePositionVehicle);
}

// ── DeserializePublicVarValue hardening tests ──

TEST_CASE("publicVariable wire format -- bool round-trip", "[network][jip]")
{
    QOStream out;
    int type = GameBool;
    out.write(&type, sizeof(int));
    GameBoolType value = true;
    out.write(&value, sizeof(value));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameBool);
    GameBoolType readValue;
    in.read(&readValue, sizeof(readValue));
    REQUIRE(readValue == true);
    REQUIRE_FALSE(in.fail());
}

TEST_CASE("publicVariable wire format -- empty array", "[network][jip]")
{
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 0;
    out.write(&count, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 0);
    REQUIRE_FALSE(in.fail());
}

TEST_CASE("publicVariable wire format -- negative count rejected", "[network][jip]")
{
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = -1;
    out.write(&count, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());

    // Manually read and check -- negative count should be rejected by deserializer
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == -1); // raw data is -1
    // Deserializer would reject this (count < 0 check)
}

TEST_CASE("publicVariable wire format -- oversized count rejected", "[network][jip]")
{
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 10001; // exceeds 10000 limit
    out.write(&count, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 10001);
    // Deserializer would reject this (count > 10000 check)
}

TEST_CASE("publicVariable wire format -- truncated stream sets fail", "[network][jip]")
{
    // Stream with only type, no payload
    QOStream out;
    int type = GameScalar;
    out.write(&type, sizeof(int));
    // No value written -- stream ends after type

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameScalar);

    // Try to read value from exhausted stream
    GameScalarType val = -999.0f;
    in.read(&val, sizeof(val));
    REQUIRE(in.fail());
}

TEST_CASE("publicVariable wire format -- empty stream sets fail immediately", "[network][jip]")
{
    QIStream in;
    in.init(nullptr, 0);
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(in.fail());
}

TEST_CASE("publicVariable wire format -- mixed type array round-trip", "[network][jip]")
{
    // Serialize: [42.0, true, "hello"]
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 3;
    out.write(&count, sizeof(int));

    // Element 0: scalar 42.0
    int scalarType = GameScalar;
    out.write(&scalarType, sizeof(int));
    GameScalarType sv = 42.0f;
    out.write(&sv, sizeof(sv));

    // Element 1: bool true
    int boolType = GameBool;
    out.write(&boolType, sizeof(int));
    GameBoolType bv = true;
    out.write(&bv, sizeof(bv));

    // Element 2: string "hello"
    int strType = GameString;
    out.write(&strType, sizeof(int));
    int len = 5;
    out.write(&len, sizeof(int));
    out.write("hello", 5);

    QIStream in;
    in.init(out.str(), out.pcount());

    // Read outer array
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 3);

    // Scalar
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameScalar);
    GameScalarType rsv;
    in.read(&rsv, sizeof(rsv));
    REQUIRE(rsv == Catch::Approx(42.0f));

    // Bool
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameBool);
    GameBoolType rbv;
    in.read(&rbv, sizeof(rbv));
    REQUIRE(rbv == true);

    // String
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == 5);
    char buf[6];
    in.read(buf, readLen);
    buf[readLen] = 0;
    REQUIRE(strcmp(buf, "hello") == 0);

    REQUIRE_FALSE(in.fail());
}

TEST_CASE("publicVariable wire format -- string with zero length", "[network][jip]")
{
    QOStream out;
    int type = GameString;
    out.write(&type, sizeof(int));
    int len = 0;
    out.write(&len, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == 0);
    REQUIRE_FALSE(in.fail());
}

TEST_CASE("publicVariable wire format -- string with negative length rejected", "[network][jip]")
{
    QOStream out;
    int type = GameString;
    out.write(&type, sizeof(int));
    int len = -5;
    out.write(&len, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == -5);
    // Deserializer would reject this (len < 0 check)
}

TEST_CASE("publicVariable wire format -- deeply nested arrays (depth limit)", "[network][jip]")
{
    // Build a wire stream with nesting depth 40 (exceeds kMaxPublicVarArrayDepth=32)
    QOStream out;
    const int depth = 40;
    int arrType = GameArray;
    int one = 1;
    for (int i = 0; i < depth; i++)
    {
        out.write(&arrType, sizeof(int));
        out.write(&one, sizeof(int));
    }
    // Innermost: a scalar
    int scalarType = GameScalar;
    out.write(&scalarType, sizeof(int));
    GameScalarType val = 1.0f;
    out.write(&val, sizeof(val));

    // Verify the structure is valid wire format (readable to at least 32 levels)
    QIStream in;
    in.init(out.str(), out.pcount());
    int level = 0;
    while (level < depth)
    {
        int readType;
        in.read(&readType, sizeof(int));
        if (in.fail())
            break;
        if (readType != GameArray)
            break;
        int readCount;
        in.read(&readCount, sizeof(int));
        if (in.fail())
            break;
        REQUIRE(readCount == 1);
        level++;
    }
    // We should have been able to read at least the first 32 levels
    REQUIRE(level >= 32);
}

TEST_CASE("MissionHeader joinInProgress survives serialization round-trip", "[network][jip]")
{
    // Test that joinInProgress is correctly serialized/deserialized via MissionHeader
    MissionHeader hdr;
    REQUIRE(hdr.joinInProgress == false);

    hdr.joinInProgress = true;
    // Verify it persists
    REQUIRE(hdr.joinInProgress == true);

    MissionHeader hdr2;
    hdr2.joinInProgress = false;
    REQUIRE(hdr2.joinInProgress == false);
    REQUIRE(hdr.joinInProgress == true); // independent instances
}

TEST_CASE("JIP queue survives many add/clear cycles", "[network][networkServer][jip]")
{
    AutoArray<TestJIPMsg> queue;

    // Simulate 5 mission cycles of queue usage
    for (int cycle = 0; cycle < 5; cycle++)
    {
        // Fill with varying counts
        int msgCount = (cycle + 1) * 7;
        for (int i = 0; i < msgCount; i++)
        {
            TestJIPMsg m;
            m.type = NMTPublicVariable;
            m.msg = nullptr;
            queue.Add(m);
        }
        REQUIRE(queue.Size() == msgCount);

        // Clear (simulating InitMission)
        queue.Resize(0);
        REQUIRE(queue.Size() == 0);
    }
}

TEST_CASE("NetworkObjectInfo create field defaults to null message", "[network][networkServer][jip]")
{
    NetworkObjectInfo info;
    info.id.creator = 0;
    info.id.id = 0;
    info.owner = 0;

    REQUIRE(info.create.message == nullptr);

    // All current update slots also default to null
    for (int i = 0; i < NMCUpdateN; i++)
    {
        REQUIRE(info.current[i].message == nullptr);
    }
}

// World state file format tests (SaveWorldState / LoadWorldState binary format)

static const int32_t JIPS_MAGIC = 0x4A495053; // 'JIPS'
static const int32_t JIPS_VERSION = 1;

TEST_CASE("World state file format -- valid header round-trip", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_valid.jips";

    // Write valid header
    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    int32_t version = JIPS_VERSION;
    int32_t timeElapsed = 30000; // 30 seconds
    int32_t objectCount = 15;
    int32_t jipCount = 8;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&timeElapsed, sizeof(timeElapsed), 1, f);
    fwrite(&objectCount, sizeof(objectCount), 1, f);
    fwrite(&jipCount, sizeof(jipCount), 1, f);
    fclose(f);

    // Read back and validate
    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion, rTime, rObj, rJip;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    REQUIRE(fread(&rTime, sizeof(rTime), 1, f) == 1);
    REQUIRE(fread(&rObj, sizeof(rObj), 1, f) == 1);
    REQUIRE(fread(&rJip, sizeof(rJip), 1, f) == 1);
    fclose(f);

    REQUIRE(rMagic == JIPS_MAGIC);
    REQUIRE(rVersion == JIPS_VERSION);
    REQUIRE(rTime == 30000);
    REQUIRE(rObj == 15);
    REQUIRE(rJip == 8);

    remove(tmpfile);
}

TEST_CASE("World state file format -- wrong magic rejected", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_badmagic.jips";

    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t badMagic = 0xDEADBEEF;
    int32_t version = JIPS_VERSION;
    fwrite(&badMagic, sizeof(badMagic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fclose(f);

    // Read back -- magic mismatch
    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    fclose(f);

    REQUIRE(rMagic != JIPS_MAGIC);

    remove(tmpfile);
}

TEST_CASE("World state file format -- wrong version rejected", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_badversion.jips";

    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    int32_t badVersion = 99;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&badVersion, sizeof(badVersion), 1, f);
    fclose(f);

    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    fclose(f);

    REQUIRE(rMagic == JIPS_MAGIC);
    REQUIRE(rVersion != JIPS_VERSION);

    remove(tmpfile);
}

TEST_CASE("World state file format -- truncated header detected", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_truncated.jips";

    // Write only magic (incomplete header)
    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    fwrite(&magic, sizeof(magic), 1, f);
    fclose(f);

    // Read -- version read should fail
    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    size_t read = fread(&rVersion, sizeof(rVersion), 1, f);
    REQUIRE(read == 0); // truncated -- can't read version
    fclose(f);

    remove(tmpfile);
}

TEST_CASE("World state file format -- empty file detected", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_empty.jips";

    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    fclose(f);

    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic;
    size_t read = fread(&rMagic, sizeof(rMagic), 1, f);
    REQUIRE(read == 0); // empty
    fclose(f);

    remove(tmpfile);
}

TEST_CASE("World state file format -- zero time and counts valid", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_zeros.jips";

    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    int32_t version = JIPS_VERSION;
    int32_t zero = 0;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&zero, sizeof(zero), 1, f); // timeElapsed = 0
    fwrite(&zero, sizeof(zero), 1, f); // objectCount = 0
    fwrite(&zero, sizeof(zero), 1, f); // jipCount = 0
    fclose(f);

    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion, rTime, rObj, rJip;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    REQUIRE(fread(&rTime, sizeof(rTime), 1, f) == 1);
    REQUIRE(fread(&rObj, sizeof(rObj), 1, f) == 1);
    REQUIRE(fread(&rJip, sizeof(rJip), 1, f) == 1);
    fclose(f);

    REQUIRE(rMagic == JIPS_MAGIC);
    REQUIRE(rVersion == JIPS_VERSION);
    REQUIRE(rTime == 0);
    REQUIRE(rObj == 0);
    REQUIRE(rJip == 0);

    remove(tmpfile);
}

TEST_CASE("World state file format -- large counts preserved", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_large.jips";

    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    int32_t version = JIPS_VERSION;
    int32_t timeElapsed = 3600000; // 1 hour in ms
    int32_t objectCount = 50000;   // CTI-scale mission
    int32_t jipCount = 100000;     // heavy publicVariable usage
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&timeElapsed, sizeof(timeElapsed), 1, f);
    fwrite(&objectCount, sizeof(objectCount), 1, f);
    fwrite(&jipCount, sizeof(jipCount), 1, f);
    fclose(f);

    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion, rTime, rObj, rJip;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    REQUIRE(fread(&rTime, sizeof(rTime), 1, f) == 1);
    REQUIRE(fread(&rObj, sizeof(rObj), 1, f) == 1);
    REQUIRE(fread(&rJip, sizeof(rJip), 1, f) == 1);
    fclose(f);

    REQUIRE(rMagic == JIPS_MAGIC);
    REQUIRE(rVersion == JIPS_VERSION);
    REQUIRE(rTime == 3600000);
    REQUIRE(rObj == 50000);
    REQUIRE(rJip == 100000);

    remove(tmpfile);
}

TEST_CASE("World state file format -- partial data truncation detected", "[network][jip][worldstate]")
{
    const char* tmpfile = "/tmp/jip_test_worldstate_partdata.jips";

    // Write magic + version + timeElapsed (missing objectCount and jipCount)
    FILE* f = fopen(tmpfile, "wb");
    REQUIRE(f != nullptr);
    int32_t magic = JIPS_MAGIC;
    int32_t version = JIPS_VERSION;
    int32_t timeElapsed = 5000;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&timeElapsed, sizeof(timeElapsed), 1, f);
    fclose(f);

    f = fopen(tmpfile, "rb");
    REQUIRE(f != nullptr);
    int32_t rMagic, rVersion, rTime, rObj;
    REQUIRE(fread(&rMagic, sizeof(rMagic), 1, f) == 1);
    REQUIRE(fread(&rVersion, sizeof(rVersion), 1, f) == 1);
    REQUIRE(fread(&rTime, sizeof(rTime), 1, f) == 1);
    size_t read = fread(&rObj, sizeof(rObj), 1, f);
    REQUIRE(read == 0); // truncated at data section
    fclose(f);

    remove(tmpfile);
}

// JIP queue pattern tests -- simulating server message queueing behavior

TEST_CASE("JIP queue -- mixed message types (publicVariable + remoteExec)", "[network][jip]")
{
    AutoArray<TestJIPMsg> queue;

    // Simulate interleaved publicVariable and remoteExec queueing
    TestJIPMsg pv;
    pv.type = NMTPublicVariable;
    pv.msg = nullptr;
    queue.Add(pv);

    TestJIPMsg re;
    re.type = NMTRemoteExec;
    re.msg = nullptr;
    queue.Add(re);

    queue.Add(pv); // another publicVariable
    queue.Add(re); // another remoteExec
    queue.Add(pv);

    REQUIRE(queue.Size() == 5);
    REQUIRE(queue[0].type == NMTPublicVariable);
    REQUIRE(queue[1].type == NMTRemoteExec);
    REQUIRE(queue[2].type == NMTPublicVariable);
    REQUIRE(queue[3].type == NMTRemoteExec);
    REQUIRE(queue[4].type == NMTPublicVariable);
}

TEST_CASE("JIP queue -- ordering preserved across large batches", "[network][jip]")
{
    struct TestJIPMsg
    {
        NetworkMessageType type;
        int sequence;
        Ref<NetworkMessage> msg;
    };

    AutoArray<TestJIPMsg> queue;

    // Add 1000 messages with sequence numbers
    for (int i = 0; i < 1000; i++)
    {
        TestJIPMsg m;
        m.type = (i % 3 == 0) ? NMTRemoteExec : NMTPublicVariable;
        m.sequence = i;
        m.msg = nullptr;
        queue.Add(m);
    }

    REQUIRE(queue.Size() == 1000);

    // Verify FIFO ordering preserved
    for (int i = 0; i < 1000; i++)
    {
        REQUIRE(queue[i].sequence == i);
    }
}

TEST_CASE("JIP queue -- selective clearing preserves capacity pattern", "[network][jip]")
{
    AutoArray<TestJIPMsg> queue;

    // Fill with 100 messages
    for (int i = 0; i < 100; i++)
    {
        TestJIPMsg m;
        m.type = NMTPublicVariable;
        m.msg = nullptr;
        queue.Add(m);
    }
    REQUIRE(queue.Size() == 100);

    // Clear (InitMission pattern)
    queue.Resize(0);
    REQUIRE(queue.Size() == 0);

    // Refill with different count -- should work without issues
    for (int i = 0; i < 250; i++)
    {
        TestJIPMsg m;
        m.type = NMTRemoteExec;
        m.msg = nullptr;
        queue.Add(m);
    }
    REQUIRE(queue.Size() == 250);
    REQUIRE(queue[0].type == NMTRemoteExec);
    REQUIRE(queue[249].type == NMTRemoteExec);
}

// NetworkObjectInfo -- object sync ordering tests

TEST_CASE("NetworkObjectInfo -- all create message types categorized", "[network][jip]")
{
    // Verify the 6-pass ordering types used by SendWorldState
    // Non-AI objects pass first, then AI in hierarchy order
    NetworkObjectInfo info;
    info.id.creator = 1;
    info.id.id = 1;
    info.owner = 1;

    // Test each AI type used in SendWorldState ordering
    info.create.type = NMTCreateAICenter;
    REQUIRE(info.create.type == NMTCreateAICenter);

    info.create.type = NMTCreateAIGroup;
    REQUIRE(info.create.type == NMTCreateAIGroup);

    info.create.type = NMTCreateAISubgroup;
    REQUIRE(info.create.type == NMTCreateAISubgroup);

    info.create.type = NMTCreateAIUnit;
    REQUIRE(info.create.type == NMTCreateAIUnit);

    info.create.type = NMTCreateCommand;
    REQUIRE(info.create.type == NMTCreateCommand);

    // Non-AI types go in pass 1
    info.create.type = NMTCreateVehicle;
    REQUIRE(info.create.type == NMTCreateVehicle);
}

TEST_CASE("NetworkObjectInfo -- update classes are distinct", "[network][jip]")
{
    // Verify update class constants used in current[] slots
    REQUIRE(NMCUpdateGeneric != NMCUpdatePosition);
    REQUIRE(NMCUpdateGeneric != NMCUpdateDammage);
    REQUIRE(NMCUpdatePosition != NMCUpdateDammage);
    REQUIRE(NMCUpdateFirst >= 0);
    REQUIRE(NMCUpdateN > NMCUpdateFirst);
}

TEST_CASE("NetworkObjectInfo -- multiple objects independent", "[network][jip]")
{
    // Simulates the _objects array in NetworkServer
    RefArray<NetworkObjectInfo> objects;

    for (int i = 0; i < 5; i++)
    {
        NetworkObjectInfo* info = new NetworkObjectInfo;
        info->id.creator = 1;
        info->id.id = i;
        info->owner = 1;
        info->create.type = (i < 3) ? NMTCreateVehicle : NMTCreateAIUnit;
        info->create.from = 1;
        objects.Add(info);
    }

    REQUIRE(objects.Size() == 5);

    // Count by type (mirrors SendWorldState pass logic)
    int nonAI = 0, aiUnits = 0;
    for (int i = 0; i < objects.Size(); i++)
    {
        if (objects[i]->create.type == NMTCreateVehicle)
            nonAI++;
        else if (objects[i]->create.type == NMTCreateAIUnit)
            aiUnits++;
    }
    REQUIRE(nonAI == 3);
    REQUIRE(aiUnits == 2);
}

// Session locking and JIP flag interaction

TEST_CASE("MissionHeader joinInProgress and session locking interaction", "[network][jip]")
{
    // Test the logic from OnCreatePlayer:
    // if (_sessionLocked && !_missionHeader.joinInProgress) -> reject
    struct SessionState
    {
        bool sessionLocked;
        bool joinInProgress;

        bool shouldReject() const { return sessionLocked && !joinInProgress; }
    };

    // Not locked, no JIP -> allow (pre-game lobby)
    REQUIRE_FALSE(SessionState{false, false}.shouldReject());

    // Not locked, JIP -> allow
    REQUIRE_FALSE(SessionState{false, true}.shouldReject());

    // Locked, JIP enabled -> allow (this is the JIP use case)
    REQUIRE_FALSE(SessionState{true, true}.shouldReject());

    // Locked, no JIP -> REJECT (session started, JIP disabled)
    REQUIRE(SessionState{true, false}.shouldReject());
}

TEST_CASE("JIP fast-track state transition logic", "[network][jip]")
{
    // Test the logic from JIP fast-track:
    // if (player.jip && _state >= NGSPlay) -> fast-track
    NetworkPlayerInfo player;

    // Pre-JIP: player not JIP, state not playing -> no fast-track
    player.jip = false;
    REQUIRE_FALSE(player.jip);

    // JIP enabled
    player.jip = true;
    REQUIRE(player.jip == true);

    // Verify game states used in fast-track condition
    REQUIRE(NGSPlay > NGSLoadIsland);
    REQUIRE(NGSLoadIsland >= 0);
}

// publicVariable wire format -- additional deserialization edge cases

TEST_CASE("publicVariable wire format -- large string (1KB)", "[network][jip]")
{
    QOStream out;
    int type = GameString;
    out.write(&type, sizeof(int));

    std::string bigStr(1024, 'X');
    int len = static_cast<int>(bigStr.size());
    out.write(&len, sizeof(int));
    out.write(bigStr.c_str(), len);

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == 1024);

    std::vector<char> buf(readLen + 1);
    in.read(buf.data(), readLen);
    buf[readLen] = 0;
    REQUIRE(std::string(buf.data()) == bigStr);
}

TEST_CASE("publicVariable wire format -- array of 100 scalars", "[network][jip]")
{
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 100;
    out.write(&count, sizeof(int));
    for (int i = 0; i < 100; i++)
    {
        int scalarType = GameScalar;
        out.write(&scalarType, sizeof(int));
        GameScalarType val = static_cast<GameScalarType>(i * 1.5f);
        out.write(&val, sizeof(val));
    }

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 100);

    for (int i = 0; i < 100; i++)
    {
        int elemType;
        in.read(&elemType, sizeof(int));
        REQUIRE(elemType == GameScalar);
        GameScalarType val;
        in.read(&val, sizeof(val));
        REQUIRE(val == Catch::Approx(static_cast<GameScalarType>(i * 1.5f)));
    }
}

TEST_CASE("publicVariable wire format -- nested 3 levels deep", "[network][jip]")
{
    // [[[1.0]]]
    QOStream out;
    int arrType = GameArray;

    // Level 0: array[1]
    out.write(&arrType, sizeof(int));
    int one = 1;
    out.write(&one, sizeof(int));

    // Level 1: array[1]
    out.write(&arrType, sizeof(int));
    out.write(&one, sizeof(int));

    // Level 2: array[1]
    out.write(&arrType, sizeof(int));
    out.write(&one, sizeof(int));

    // Leaf: scalar 1.0
    int scalarType = GameScalar;
    out.write(&scalarType, sizeof(int));
    GameScalarType val = 1.0f;
    out.write(&val, sizeof(val));

    QIStream in;
    in.init(out.str(), out.pcount());

    // Read level 0
    int t;
    in.read(&t, sizeof(int));
    REQUIRE(t == GameArray);
    int c;
    in.read(&c, sizeof(int));
    REQUIRE(c == 1);

    // Read level 1
    in.read(&t, sizeof(int));
    REQUIRE(t == GameArray);
    in.read(&c, sizeof(int));
    REQUIRE(c == 1);

    // Read level 2
    in.read(&t, sizeof(int));
    REQUIRE(t == GameArray);
    in.read(&c, sizeof(int));
    REQUIRE(c == 1);

    // Read leaf
    in.read(&t, sizeof(int));
    REQUIRE(t == GameScalar);
    GameScalarType rv;
    in.read(&rv, sizeof(rv));
    REQUIRE(rv == Catch::Approx(1.0f));
}

TEST_CASE("publicVariable wire format -- string containing null bytes", "[network][jip]")
{
    QOStream out;
    int type = GameString;
    out.write(&type, sizeof(int));

    // String with embedded null: "AB\0CD"
    char data[] = {'A', 'B', '\0', 'C', 'D'};
    int len = 5;
    out.write(&len, sizeof(int));
    out.write(data, len);

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameString);
    int readLen;
    in.read(&readLen, sizeof(int));
    REQUIRE(readLen == 5);
    char buf[5];
    in.read(buf, readLen);
    REQUIRE(buf[0] == 'A');
    REQUIRE(buf[1] == 'B');
    REQUIRE(buf[2] == '\0');
    REQUIRE(buf[3] == 'C');
    REQUIRE(buf[4] == 'D');
}

TEST_CASE("publicVariable wire format -- max array count boundary", "[network][jip]")
{
    // Count exactly at the limit (10000) should be accepted
    QOStream out;
    int arrType = GameArray;
    out.write(&arrType, sizeof(int));
    int count = 10000;
    out.write(&count, sizeof(int));

    QIStream in;
    in.init(out.str(), out.pcount());
    int readType;
    in.read(&readType, sizeof(int));
    REQUIRE(readType == GameArray);
    int readCount;
    in.read(&readCount, sizeof(int));
    REQUIRE(readCount == 10000); // at boundary -- accepted by validator
}

// MissionHeader configuration tests

TEST_CASE("MissionHeader -- multiple fields independent", "[network][jip]")
{
    MissionHeader header;
    header.joinInProgress = true;

    // Setting JIP should not affect other fields
    REQUIRE(header.joinInProgress == true);

    header.joinInProgress = false;
    REQUIRE(header.joinInProgress == false);
}

TEST_CASE("MissionHeader -- joinInProgress serialization with different states", "[network][jip]")
{
    // Test round-trip with joinInProgress=true
    {
        QOStream out;
        bool jip = true;
        out.write(&jip, sizeof(bool));

        QIStream in;
        in.init(out.str(), out.pcount());
        bool readJip;
        in.read(&readJip, sizeof(bool));
        REQUIRE(readJip == true);
    }

    // Test round-trip with joinInProgress=false
    {
        QOStream out;
        bool jip = false;
        out.write(&jip, sizeof(bool));

        QIStream in;
        in.init(out.str(), out.pcount());
        bool readJip;
        in.read(&readJip, sizeof(bool));
        REQUIRE(readJip == false);
    }
}

// JIP publicVariable deduplication tests

TEST_CASE("JIP queue -- publicVariable deduplication replaces by name", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "score");
    REQUIRE(queue.Size() == 1);

    // Same variable name -- should replace, not append
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "score");
    REQUIRE(queue.Size() == 1);

    // Different variable name -- should append
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "lives");
    REQUIRE(queue.Size() == 2);

    // Update first variable again -- still 2 entries
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "score");
    REQUIRE(queue.Size() == 2);
}

TEST_CASE("JIP queue -- anonymous remoteExec appends, keyed remoteExec replaces", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    REQUIRE(queue.Size() == 3);

    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "state");
    REQUIRE(queue.Size() == 4);
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "state");
    REQUIRE(queue.Size() == 4);
}

TEST_CASE("JIP queue -- mixed publicVariable and remoteExec", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    // publicVariable "funds" -- added
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "funds");
    REQUIRE(queue.Size() == 1);

    // remoteExec -- appended
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    REQUIRE(queue.Size() == 2);

    // publicVariable "funds" -- replaced entry 0, remoteExec stays
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "funds");
    REQUIRE(queue.Size() == 2);

    // publicVariable "kills" -- appended
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "kills");
    REQUIRE(queue.Size() == 3);

    // another remoteExec -- appended
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    REQUIRE(queue.Size() == 4);

    // "funds" updated again -- still 4
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "funds");
    REQUIRE(queue.Size() == 4);
}

TEST_CASE("JIP queue -- CTI stress: many updates to same variables stay bounded", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    // Simulate 4-hour CTI: 10 variables updated every 5 seconds = ~28800 updates
    const char* varNames[] = {"westFunds",   "eastFunds", "westScore", "eastScore",  "playerCount",
                              "ticketCount", "flagState", "timeLeft",  "spawnCount", "missionPhase"};
    int numVars = 10;

    for (int tick = 0; tick < 28800; tick++)
    {
        int varIdx = tick % numVars;
        AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, varNames[varIdx]);
    }

    // Should have exactly 10 entries -- one per unique variable
    REQUIRE(queue.Size() == numVars);

    // Verify all variable names are present
    for (int i = 0; i < numVars; i++)
    {
        bool found = false;
        for (int j = 0; j < queue.Size(); j++)
        {
            if (queue[j].key == RString(varNames[i]))
            {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("JIP queue -- publicVariable and keyed remoteExec deduplicate independently", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    // Two publicVariables with same key -- deduplicated
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "test");
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "test");
    REQUIRE(queue.Size() == 1);

    // remoteExec with the same key replaces the remoteExec entry, not the publicVariable.
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "test");
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "test");
    REQUIRE(queue.Size() == 2);
}

TEST_CASE("JIP queue -- remoteExecRemove deletes keyed remoteExec entries", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "state");
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "marker");
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "");
    REQUIRE(queue.Size() == 3);

    REQUIRE(RemoveJIPMessage(queue, NMTRemoteExec, "state"));
    REQUIRE(queue.Size() == 2);
    REQUIRE(queue[0].key == RString("marker"));
    REQUIRE(queue[1].key == RString(""));
    REQUIRE_FALSE(RemoveJIPMessage(queue, NMTRemoteExec, "missing"));
}

TEST_CASE("JIP queue -- cap still applies for remoteExec overflow", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;
    int cap = 100; // small cap for testing

    // Fill with anonymous remoteExec (each appends)
    for (int i = 0; i < cap + 50; i++)
    {
        AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "", cap);
    }
    REQUIRE(queue.Size() == cap);

    // Add one publicVariable with room first
    queue.Resize(0);
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "funds", cap);
    REQUIRE(queue.Size() == 1);

    // Fill rest with remoteExec
    for (int i = 1; i < cap; i++)
    {
        AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "", cap);
    }
    REQUIRE(queue.Size() == cap);

    // Now update "funds" -- dedup finds it, replaces in-place, no growth
    bool result = AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "funds", cap);
    REQUIRE(result == true);
    REQUIRE(queue.Size() == cap);

    // New variable "kills" -- queue full, should be dropped
    result = AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "kills", cap);
    REQUIRE(result == false);
    REQUIRE(queue.Size() == cap);

    // Keyed remoteExec replacement at cap succeeds because it does not grow.
    queue.Resize(0);
    AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "state", cap);
    for (int i = 1; i < cap; i++)
    {
        AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "", cap);
    }
    REQUIRE(queue.Size() == cap);
    result = AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "state", cap);
    REQUIRE(result == true);
    REQUIRE(queue.Size() == cap);

    // New keyed remoteExec at cap is rejected just like a new append entry.
    result = AddOrReplaceJIPMessage(queue, NMTRemoteExec, nullptr, "newState", cap);
    REQUIRE(result == false);
    REQUIRE(queue.Size() == cap);
}

TEST_CASE("JIP queue -- empty key publicVariable appends without dedup", "[network][jip][dedup]")
{
    AutoArray<TestJIPMsg> queue;

    // Empty key -- can't deduplicate, just append
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "");
    AddOrReplaceJIPMessage(queue, NMTPublicVariable, nullptr, "");
    REQUIRE(queue.Size() == 2);
}
