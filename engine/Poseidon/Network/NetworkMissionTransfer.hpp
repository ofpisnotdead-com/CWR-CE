#pragma once

#include <Poseidon/Network/NetworkFileTransfer.hpp>
#include <Poseidon/Network/NetworkIface.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Network/WireBounds.hpp>

#include <Poseidon/Foundation/Containers/RStringArray.hpp>

#include <Poseidon/Foundation/Strings/RString.hpp>

#include <limits>

namespace Poseidon
{

constexpr int NetworkMissionTransferSegmentSize = NetworkFileTransferSegmentSize;
constexpr NetMsgFlags NetworkMissionTransferSendFlags = NMFGuaranteed;
constexpr int NetworkMissionBulkTransferSegmentSize = 900;
constexpr int NetworkMissionBulkRetransmitMaxSegments = 64;
constexpr int NetworkMissionBulkActiveRetransmitMaxSegments = 8;
constexpr int NetworkMissionBulkInFlightSegmentGuard = 8;
constexpr DWORD NetworkMissionBulkRetransmitDelayMs = 150;
constexpr DWORD NetworkMissionBulkRepeatedRetransmitDelayMs = 1000;
constexpr int NetworkMissionBulkRawMagic = 0x4d50424f; // MPBO

enum class NetworkMissionBulkRawKind : int
{
    Data = 1,
    Request = 2,
};

struct NetworkMissionBulkRawPacket
{
    NetworkMissionBulkRawKind kind = NetworkMissionBulkRawKind::Data;
    int totalSize = 0;
    int totalSegments = 0;
    int curSegment = 0;
    int offset = 0;
    int segmentCount = 0;
    RString transferPath;
    const char* data = nullptr;
    int dataSize = 0;
};

struct NetworkMissionBulkRawHeader
{
    int kind;
    int totalSize;
    int totalSegments;
    int curSegment;
    int offset;
    int segmentCount;
    int transferPathSize;
};

struct NetworkMissionFileValidationResult
{
    bool fileValid = false;
    bool missionPathValid = false;
    bool checkedCache = false;
};

struct NetworkMissionFileValidationPlan
{
    RString missionPath;
    RString cachePath;
    NetworkMissionFileValidationResult validation;
};

struct NetworkMissionFileValidityReport
{
    bool missionFileValid = false;
};

struct NetworkMissionFileSendSource
{
    RString sourcePath;
    RString destinationPath;
    const void* data = nullptr;
    int totalSize = 0;
};

struct NetworkMissionTransferFailureReport
{
    bool shouldDisconnect = false;
    RString message;
};

struct NetworkMissionTransferSegmentReceivePlan
{
    RString transferPath;
    bool shouldResetBank = false;
};

struct NetworkMissionFileSendResult
{
    RString destinationPath;
    int segmentCount = 0;
    int sentCount = 0;
    int markedCount = 0;
};

template <typename State>
struct NetworkMissionLoadIslandTransition
{
    State nextState;
    bool shouldWarn = false;
};

template <typename State>
struct NetworkMissionLoadIslandEntry
{
    State nextState;
    bool shouldAssignState = false;
    bool shouldWarn = false;
};

template <typename State>
struct NetworkMissionJoinStatePlan
{
    State playerState;
    State stateToSend;
    bool shouldSendMissionInfo = false;
    bool shouldResetMissionValidity = false;
};

template <typename State>
struct NetworkMissionPlayerStateUpdate
{
    State nextState;
    bool shouldAssignState = false;
    bool shouldResetMissionValidity = false;
};

template <typename State>
struct NetworkMissionClientStateUpdate
{
    State nextState;
    bool shouldAssignState = false;
};

template <typename State>
struct NetworkMissionReceivedFilePlan
{
    RString bankPath;
    State nextState;
    bool shouldMarkMissionFileValid = false;
    bool shouldResetExistingBank = true;
};

template <typename State>
struct NetworkMissionTransferReceiveOutcome
{
    NetworkMissionReceivedFilePlan<State> receivePlan;
    NetworkMissionTransferFailureReport failureReport;
    bool shouldApplyReceivePlan = false;
};

template <typename State>
struct NetworkMissionPlayEntry
{
    State nextState;
    bool shouldAssignState = false;
    bool shouldRunJipInitScripts = false;
    bool shouldClearBodies = false;
    int hideBodies = 0;
};

template <typename State>
struct NetworkMissionBriefingEntry
{
    State nextState;
    bool shouldAssignState = false;
    bool shouldReportLoadError = false;
};

struct NetworkMissionBriefingReport
{
    bool shouldReport = false;
    RString message;
};

template <typename State>
struct NetworkMissionServerLoadResult
{
    State nextState;
    bool shouldAssignState = false;
    bool shouldReportMessage = false;
    RString message;
    bool shouldLogLoaded = false;
};

template <typename State>
struct NetworkMissionDebriefingTransition
{
    State nextState;
    bool shouldAdvance = false;
    bool shouldReassignPlayers = false;
    bool shouldClearRestart = false;
    bool shouldClearReassign = false;
    bool shouldLoadIsland = false;
    bool shouldLogWaiting = false;
};

template <typename State>
struct NetworkMissionAdminTransition
{
    State nextState;
    bool shouldAdvance = false;
    bool shouldReassignPlayers = false;
    bool shouldClearRestart = false;
    bool shouldClearReassign = false;
};

struct NetworkMissionPlayerRoleNormalization
{
    int requestedPlayer = 0;
    int noPlayer = 0;
};

struct NetworkJipMessageStoreAction
{
    bool shouldReplace = false;
    int replaceIndex = -1;
    bool shouldAppend = false;
};

template <typename State>
bool ShouldResetNetworkMissionValidityOnPrepareRole(State nextState, State currentState, State prepareRoleState,
                                                    State transferMissionState);

inline bool DoesNetworkMissionFileMatchHeader(uint64_t fileSize, int fileCrc, int expectedFileSizeL,
                                              int expectedFileSizeH, int expectedFileCrc)
{
    return expectedFileSizeL == static_cast<int>(fileSize & 0xffffffffULL) &&
           expectedFileSizeH == static_cast<int>(fileSize >> 32) && expectedFileCrc == fileCrc;
}

inline int NetworkMissionHeaderTransferSize(int fileSizeL, int fileSizeH)
{
    const uint64_t size = (static_cast<uint64_t>(static_cast<uint32_t>(fileSizeH)) << 32) |
                          static_cast<uint32_t>(fileSizeL);
    if (size > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(size);
}

bool ValidateNetworkMissionFileOnDisk(const RString& missionPath, int expectedFileSizeL, int expectedFileSizeH,
                                      int expectedFileCrc);

template <class ValidateFn>
NetworkMissionFileValidationResult ValidateNetworkMissionFileWithCache(const RString& missionPath,
                                                                       const RString& cachePath,
                                                                       ValidateFn&& validateMissionFile);

RString BuildNetworkMissionTransferCacheBasePath(const char* missionFileName);
RString BuildNetworkMissionTransferCachePboPath(const char* missionFileName);
RString BuildNetworkMissionTransferCachePboPathFromTransferPath(const char* transferPath);
RString BuildNetworkMissionTransferBankPath(const char* transferPath);
RString BuildNetworkMissionTransferFailureMessage(const char* format, const char* playerName, const char* transferPath);
RString BuildNetworkMissionBriefingErrorMessage(const char* loadErrorMessage, const char* missingAddonMessage);
RString BuildNetworkMissionMissingAddonMessage(const char* baseMessage, const FindArrayRStringCI& missingAddons);

inline NetworkMissionTransferFailureReport BuildNetworkMissionTransferFailureReport(int receiveResult,
                                                                                    const char* format,
                                                                                    const char* playerName,
                                                                                    const char* transferPath)
{
    return {receiveResult < 0, receiveResult < 0
                                   ? BuildNetworkMissionTransferFailureMessage(format, playerName, transferPath)
                                   : RString()};
}

inline NetworkMissionBriefingReport BuildNetworkMissionBriefingReport(bool shouldReportLoadError,
                                                                      const char* loadErrorMessage,
                                                                      const char* missingAddonMessage)
{
    return {shouldReportLoadError, shouldReportLoadError
                                       ? BuildNetworkMissionBriefingErrorMessage(loadErrorMessage, missingAddonMessage)
                                       : RString()};
}

inline bool ShouldResetNetworkMissionTransferBank(int segmentOffset)
{
    return segmentOffset == 0;
}

inline NetworkMissionTransferSegmentReceivePlan
BuildNetworkMissionTransferSegmentReceivePlan(const char* missionFileName, int segmentOffset)
{
    return {BuildNetworkMissionTransferCachePboPath(missionFileName),
            ShouldResetNetworkMissionTransferBank(segmentOffset)};
}

template <class Path, class ResetFn>
void ApplyNetworkMissionTransferSegmentReceivePlan(const NetworkMissionTransferSegmentReceivePlan& segmentPlan,
                                                   Path& transferPath, ResetFn&& resetBank)
{
    transferPath = segmentPlan.transferPath;
    if (segmentPlan.shouldResetBank)
    {
        std::forward<ResetFn>(resetBank)();
    }
}

inline int GetNetworkMissionTransferSegmentCount(int totalSize)
{
    return GetNetworkFileTransferSegmentCount(totalSize, NetworkMissionTransferSegmentSize);
}

inline int GetNetworkMissionBulkTransferSegmentCount(int totalSize)
{
    return GetNetworkFileTransferSegmentCount(totalSize, NetworkMissionBulkTransferSegmentSize);
}

template <typename SegmentMessage>
bool EncodeNetworkMissionBulkRawSegment(const SegmentMessage& segment, AutoArray<char>& payload)
{
    if (!WireBounds::SegmentInBounds(segment.totSize, segment.totSegments, segment.curSegment, segment.offset,
                                     segment.data.Size()) ||
        segment.path.GetLength() <= 0)
    {
        return false;
    }
    NetworkMissionBulkRawHeader header;
    header.kind = static_cast<int>(NetworkMissionBulkRawKind::Data);
    header.totalSize = segment.totSize;
    header.totalSegments = segment.totSegments;
    header.curSegment = segment.curSegment;
    header.offset = segment.offset;
    header.segmentCount = 0;
    header.transferPathSize = segment.path.GetLength();
    payload.Resize(static_cast<int>(sizeof(header)) + header.transferPathSize + segment.data.Size());
    memcpy(payload.Data(), &header, sizeof(header));
    memcpy(payload.Data() + sizeof(header), (const char*)segment.path, header.transferPathSize);
    if (segment.data.Size() > 0)
    {
        memcpy(payload.Data() + sizeof(header) + header.transferPathSize, segment.data.Data(), segment.data.Size());
    }
    return true;
}

inline bool EncodeNetworkMissionBulkRawRequest(int firstSegment, int segmentCount, int expectedSize,
                                               AutoArray<char>& payload)
{
    if (firstSegment < 0 || segmentCount <= 0 || expectedSize < 0)
    {
        return false;
    }
    NetworkMissionBulkRawHeader header;
    header.kind = static_cast<int>(NetworkMissionBulkRawKind::Request);
    header.totalSize = expectedSize;
    header.totalSegments = GetNetworkMissionBulkTransferSegmentCount(expectedSize);
    header.curSegment = firstSegment;
    header.offset = 0;
    header.segmentCount = std::min(segmentCount, NetworkMissionBulkRetransmitMaxSegments);
    header.transferPathSize = 0;
    payload.Resize(static_cast<int>(sizeof(header)));
    memcpy(payload.Data(), &header, sizeof(header));
    return true;
}

inline DWORD GetNetworkMissionBulkRetransmitDelayMs(int firstSegment, int segmentCount, int previousFirstSegment,
                                                    int previousSegmentCount)
{
    const int endSegment = firstSegment + segmentCount;
    const int previousEndSegment = previousFirstSegment + previousSegmentCount;
    const bool overlapsPrevious = firstSegment < previousEndSegment && previousFirstSegment < endSegment;
    return overlapsPrevious ? NetworkMissionBulkRepeatedRetransmitDelayMs : NetworkMissionBulkRetransmitDelayMs;
}

inline bool ShouldWaitForNetworkMissionBulkRetransmit(DWORD now, DWORD firstSegmentTime, DWORD lastRequestTime,
                                                      DWORD lastSegmentTime, DWORD delay)
{
    DWORD previousActivityTime = firstSegmentTime;
    if (lastRequestTime != 0 && (previousActivityTime == 0 || lastRequestTime > previousActivityTime))
    {
        previousActivityTime = lastRequestTime;
    }
    if (lastSegmentTime != 0 && (previousActivityTime == 0 || lastSegmentTime > previousActivityTime))
    {
        previousActivityTime = lastSegmentTime;
    }
    return previousActivityTime != 0 && now - previousActivityTime < delay;
}

inline int GetNetworkMissionBulkMissingScanLimit(int totalSegments, int highestReceivedSegment, DWORD now,
                                                 DWORD lastSegmentTime, DWORD activeDelay, DWORD quietDelay)
{
    if (totalSegments <= 0 || highestReceivedSegment < 0)
    {
        return -1;
    }
    const int lastSegment = totalSegments - 1;
    if (highestReceivedSegment >= lastSegment)
    {
        return lastSegment;
    }
    if (lastSegmentTime != 0 && now - lastSegmentTime < activeDelay)
    {
        return std::min(highestReceivedSegment, lastSegment);
    }
    if (lastSegmentTime != 0 && now - lastSegmentTime < quietDelay)
    {
        return std::min(highestReceivedSegment - NetworkMissionBulkInFlightSegmentGuard, lastSegment);
    }
    return lastSegment;
}

inline int GetNetworkMissionBulkRetransmitSegmentLimit(DWORD now, DWORD lastSegmentTime, DWORD delay)
{
    if (lastSegmentTime != 0 && now - lastSegmentTime < delay)
    {
        return NetworkMissionBulkActiveRetransmitMaxSegments;
    }
    return NetworkMissionBulkRetransmitMaxSegments;
}

inline int MarkNetworkMissionBulkRequestedRange(AutoArray<bool>& requestedSegments, int totalSegments, int firstSegment,
                                                int segmentCount)
{
    if (totalSegments <= 0 || firstSegment < 0 || segmentCount <= 0 || firstSegment >= totalSegments)
    {
        return 0;
    }
    if (requestedSegments.Size() != totalSegments)
    {
        requestedSegments.Resize(totalSegments);
        for (int i = 0; i < requestedSegments.Size(); ++i)
        {
            requestedSegments[i] = false;
        }
    }

    const int endSegment =
        std::min(totalSegments, firstSegment + std::min(segmentCount, NetworkMissionBulkRetransmitMaxSegments));
    int newlyMarked = 0;
    for (int segment = firstSegment; segment < endSegment; ++segment)
    {
        if (!requestedSegments[segment])
        {
            requestedSegments[segment] = true;
            ++newlyMarked;
        }
    }
    return newlyMarked;
}

inline bool WasNetworkMissionBulkSegmentRequested(const AutoArray<bool>& requestedSegments, int segment)
{
    return segment >= 0 && segment < requestedSegments.Size() && requestedSegments[segment];
}

inline bool MarkNetworkMissionBulkSegmentSeen(AutoArray<bool>& segments, int totalSegments, int segment)
{
    if (totalSegments <= 0 || segment < 0 || segment >= totalSegments)
    {
        return false;
    }
    if (segments.Size() != totalSegments)
    {
        segments.Resize(totalSegments);
        for (int i = 0; i < segments.Size(); ++i)
        {
            segments[i] = false;
        }
    }
    if (segments[segment])
    {
        return false;
    }
    segments[segment] = true;
    return true;
}

inline bool DecodeNetworkMissionBulkRawPayload(const char* payload, int payloadSize, NetworkMissionBulkRawPacket& packet)
{
    if (!payload || payloadSize < static_cast<int>(sizeof(NetworkMissionBulkRawHeader)))
    {
        return false;
    }
    NetworkMissionBulkRawHeader header;
    memcpy(&header, payload, sizeof(header));
    packet.kind = static_cast<NetworkMissionBulkRawKind>(header.kind);
    packet.totalSize = header.totalSize;
    packet.totalSegments = header.totalSegments;
    packet.curSegment = header.curSegment;
    packet.offset = header.offset;
    packet.segmentCount = header.segmentCount;
    packet.transferPath = RString();
    packet.data = nullptr;
    packet.dataSize = 0;

    switch (packet.kind)
    {
        case NetworkMissionBulkRawKind::Data:
        {
            if (header.transferPathSize <= 0 ||
                payloadSize < static_cast<int>(sizeof(NetworkMissionBulkRawHeader)) + header.transferPathSize)
            {
                return false;
            }
            packet.transferPath = RString(payload + sizeof(header), header.transferPathSize);
            packet.data = payload + sizeof(header) + header.transferPathSize;
            packet.dataSize =
                payloadSize - static_cast<int>(sizeof(NetworkMissionBulkRawHeader)) - header.transferPathSize;
            return WireBounds::SegmentInBounds(packet.totalSize, packet.totalSegments, packet.curSegment, packet.offset,
                                               packet.dataSize);
        }
        case NetworkMissionBulkRawKind::Request:
            packet.data = payload + sizeof(header);
            packet.dataSize = payloadSize - static_cast<int>(sizeof(header));
            return packet.curSegment >= 0 && packet.segmentCount > 0 && packet.totalSize >= 0 &&
                   header.transferPathSize == 0 &&
                   packet.totalSegments == GetNetworkMissionBulkTransferSegmentCount(packet.totalSize) &&
                   packet.dataSize == 0;
    }
    return false;
}

template <typename MessageType, typename SendFn>
int SendNetworkMissionFileSegments(const RString& destinationPath, const void* data, int totalSize,
                                   SendFn&& sendSegment)
{
    return SendNetworkFileTransferSegments<MessageType>(destinationPath, data, totalSize, sendSegment,
                                                        NetworkMissionTransferSegmentSize);
}

template <typename MessageType, typename SendFn>
int SendNetworkMissionRawFileSegments(const RString& destinationPath, const void* data, int totalSize,
                                      SendFn&& sendSegment)
{
    return SendNetworkFileTransferSegments<MessageType>(
        destinationPath, data, totalSize,
        [&](MessageType& msg)
        {
            AutoArray<char> payload;
            if (EncodeNetworkMissionBulkRawSegment(msg, payload))
            {
                std::forward<SendFn>(sendSegment)(payload);
            }
        },
        NetworkMissionBulkTransferSegmentSize);
}

template <typename MessageType, typename SendFn>
int SendNetworkMissionRawFileSegmentRange(const RString& destinationPath, const void* data, int totalSize,
                                          int firstSegment, int segmentCount, SendFn&& sendSegment)
{
    const int totalSegments = GetNetworkMissionBulkTransferSegmentCount(totalSize);
    if (!data || totalSize < 0 || firstSegment < 0 || segmentCount <= 0 || firstSegment >= totalSegments)
    {
        return 0;
    }
    const int endSegment =
        std::min(totalSegments, firstSegment + std::min(segmentCount, NetworkMissionBulkRetransmitMaxSegments));
    int sent = 0;
    for (int segment = firstSegment; segment < endSegment; ++segment)
    {
        const int offset = segment * NetworkMissionBulkTransferSegmentSize;
        const int size = std::min(NetworkMissionBulkTransferSegmentSize, totalSize - offset);
        MessageType msg;
        msg.path = destinationPath;
        msg.totSize = totalSize;
        msg.offset = offset;
        msg.totSegments = totalSegments;
        msg.curSegment = segment;
        msg.data.Resize(size);
        memcpy(msg.data.Data(), static_cast<const char*>(data) + offset, size);

        AutoArray<char> payload;
        if (EncodeNetworkMissionBulkRawSegment(msg, payload))
        {
            std::forward<SendFn>(sendSegment)(payload);
            ++sent;
        }
    }
    return sent;
}

template <class Buffer>
NetworkMissionFileSendSource BuildNetworkMissionFileSendSource(const RString& missionBank, const char* missionFileName,
                                                               const Buffer* buffer)
{
    return {missionBank, BuildNetworkMissionTransferCachePboPath(missionFileName),
            buffer != nullptr ? buffer->GetData() : nullptr, buffer != nullptr ? buffer->GetSize() : -1};
}

template <typename State>
State ComputeNextNetworkMissionTransferState(State currentState, State transferMissionState, State loadIslandState,
                                             bool prepareGameSucceeded)
{
    if (currentState == transferMissionState && prepareGameSucceeded)
    {
        return loadIslandState;
    }

    return currentState;
}

template <typename State>
NetworkMissionReceivedFilePlan<State>
BuildReceivedNetworkMissionFilePlan(const RString& transferPath, State currentState, State transferMissionState,
                                    State loadIslandState, bool prepareGameSucceeded)
{
    return {BuildNetworkMissionTransferBankPath(transferPath),
            ComputeNextNetworkMissionTransferState(currentState, transferMissionState, loadIslandState,
                                                   prepareGameSucceeded),
            true, false};
}

template <typename State, class PrepareFn, class BuildFailureFn>
NetworkMissionTransferReceiveOutcome<State>
BuildNetworkMissionTransferReceiveOutcome(int receiveResult, const RString& transferPath, State currentState,
                                          State transferMissionState, State loadIslandState, PrepareFn&& prepareGame,
                                          BuildFailureFn&& buildFailureReport)
{
    if (receiveResult > 0)
    {
        const bool prepareGameSucceeded =
            currentState == transferMissionState && std::forward<PrepareFn>(prepareGame)();
        return {BuildReceivedNetworkMissionFilePlan(transferPath, currentState, transferMissionState, loadIslandState,
                                                    prepareGameSucceeded),
                {},
                true};
    }

    if (receiveResult < 0)
    {
        return {{}, std::forward<BuildFailureFn>(buildFailureReport)(receiveResult, transferPath), false};
    }

    return {};
}

template <typename State, class CreateBankFn>
void ApplyReceivedNetworkMissionFilePlan(const NetworkMissionReceivedFilePlan<State>& receivePlan, const char* island,
                                         CreateBankFn&& createBank, bool& missionFileValid, State& currentState)
{
    std::forward<CreateBankFn>(createBank)(receivePlan.bankPath, island, receivePlan.shouldResetExistingBank);
    missionFileValid = receivePlan.shouldMarkMissionFileValid;
    currentState = receivePlan.nextState;
}

template <typename State, class ValidateFn, class CreateBankFn, class DisconnectFn>
void ApplyNetworkMissionTransferReceiveOutcome(const NetworkMissionTransferReceiveOutcome<State>& receiveOutcome,
                                               const char* island, ValidateFn&& validateReceivePlan,
                                               CreateBankFn&& createBank, DisconnectFn&& disconnectPeer,
                                               bool& missionFileValid, State& currentState)
{
    if (receiveOutcome.shouldApplyReceivePlan)
    {
        std::forward<ValidateFn>(validateReceivePlan)(receiveOutcome.receivePlan);
        ApplyReceivedNetworkMissionFilePlan(receiveOutcome.receivePlan, island, std::forward<CreateBankFn>(createBank),
                                            missionFileValid, currentState);
    }
    else if (receiveOutcome.failureReport.shouldDisconnect)
    {
        std::forward<DisconnectFn>(disconnectPeer)(receiveOutcome.failureReport.message);
    }
}

template <typename State>
bool ShouldFastTrackNetworkMissionJip(State clientState, State prepareOkState, bool isJip, State serverState,
                                      State playState)
{
    return clientState == prepareOkState && isJip && serverState >= playState;
}

template <typename State>
NetworkMissionServerLoadResult<State>
BuildNetworkMissionServerLoadResult(const RString& errorMessage, bool initVehiclesOk, bool simulateMode,
                                    State createState, State briefingState, const char* genericErrorMessage)
{
    if (errorMessage.GetLength() > 0)
    {
        return {createState, true, true, errorMessage, false};
    }

    if (!initVehiclesOk)
    {
        return {createState, true, true, RString(genericErrorMessage != nullptr ? genericErrorMessage : ""), false};
    }

    return {simulateMode ? briefingState : createState, simulateMode, false, RString(), true};
}

template <class GetPlayerFn, class GetPlayerStateFn, typename State>
bool AreNetworkMissionBriefingPlayersReady(int roleCount, GetPlayerFn&& getPlayer, GetPlayerStateFn&& getPlayerState,
                                           bool simulateMode, int mpAutoStart, int aiPlayer, int noPlayer,
                                           State playState)
{
    if (!simulateMode || mpAutoStart > 0)
    {
        for (int i = 0; i < roleCount; ++i)
        {
            const int player = std::forward<GetPlayerFn>(getPlayer)(i);
            if (player == aiPlayer || player == noPlayer)
            {
                continue;
            }

            if (std::forward<GetPlayerStateFn>(getPlayerState)(player) != playState)
            {
                return false;
            }
        }
    }

    return true;
}

template <class GetPlayerFn, class GetPlayerStateFn, typename State>
bool AreNetworkMissionPlayPlayersFinished(int roleCount, GetPlayerFn&& getPlayer, GetPlayerStateFn&& getPlayerState,
                                          int aiPlayer, int noPlayer, State playState)
{
    for (int i = 0; i < roleCount; ++i)
    {
        const int player = std::forward<GetPlayerFn>(getPlayer)(i);
        if (player == aiPlayer || player == noPlayer)
        {
            continue;
        }

        if (std::forward<GetPlayerStateFn>(getPlayerState)(player) == playState)
        {
            return false;
        }
    }

    return true;
}

template <class GetPlayerFn, class GetPlayerStateFn, typename State>
bool AreNetworkMissionDebriefingPlayersReady(int gameMaster, int aiPlayer, int noPlayer, int roleCount,
                                             GetPlayerFn&& getPlayer, GetPlayerStateFn&& getPlayerState,
                                             State debriefingState, State debriefingOkState)
{
    if (gameMaster != aiPlayer)
    {
        const State state = std::forward<GetPlayerStateFn>(getPlayerState)(gameMaster);
        return state != debriefingState && state <= debriefingOkState;
    }

    for (int i = 0; i < roleCount; ++i)
    {
        const int player = std::forward<GetPlayerFn>(getPlayer)(i);
        if (player == aiPlayer || player == noPlayer)
        {
            continue;
        }

        const State state = std::forward<GetPlayerStateFn>(getPlayerState)(player);
        if (state == debriefingState || state > debriefingOkState)
        {
            return false;
        }
    }

    return true;
}

template <typename State>
NetworkMissionDebriefingTransition<State>
BuildNetworkMissionDebriefingTransition(bool allPlayersReady, bool restart, bool reassign, bool missionEmpty,
                                        State prepareSideState, State prepareRoleState, State createState)
{
    if (!allPlayersReady)
    {
        return {createState, false, false, false, false, false, false};
    }

    if (restart && reassign)
    {
        return {prepareSideState, true, true, true, true, false, false};
    }

    if (restart && missionEmpty)
    {
        return {prepareRoleState, true, false, false, false, true, false};
    }

    return {createState, true, false, false, false, false, true};
}

template <typename State>
NetworkMissionAdminTransition<State> BuildNetworkMissionAdminTransition(State currentState, State playState,
                                                                        bool shouldReassignPlayers, State createState,
                                                                        State prepareSideState)
{
    if (currentState == playState)
    {
        return {currentState, false, false, false, false};
    }

    if (shouldReassignPlayers)
    {
        return {prepareSideState, true, true, true, true};
    }

    return {createState, true, false, false, false};
}

inline NetworkMissionPlayerRoleNormalization NormalizeNetworkMissionRequestedRolePlayer(int requestedPlayer,
                                                                                        bool disabledAI,
                                                                                        bool allDisabled, int aiPlayer,
                                                                                        int noPlayer)
{
    NetworkMissionPlayerRoleNormalization result{requestedPlayer, disabledAI ? noPlayer : aiPlayer};
    if (result.requestedPlayer == aiPlayer)
    {
        result.requestedPlayer = result.noPlayer;
    }

    if (result.noPlayer == aiPlayer && allDisabled)
    {
        result.noPlayer = noPlayer;
    }

    return result;
}

template <class GetRolePlayerFn>
int FindNetworkMissionRolePlayerIndex(int roleCount, GetRolePlayerFn&& getRolePlayer, int requestedPlayer, int noPlayer,
                                      int aiPlayer)
{
    if (requestedPlayer == noPlayer || requestedPlayer == aiPlayer)
    {
        return -1;
    }

    for (int i = 0; i < roleCount; ++i)
    {
        if (std::forward<GetRolePlayerFn>(getRolePlayer)(i) == requestedPlayer)
        {
            return i;
        }
    }

    return -1;
}

inline bool CanAssignUnlockedNetworkMissionRole(int currentPlayer, int requestedPlayer, int from, int aiPlayer,
                                                int noPlayer)
{
    return ((currentPlayer == aiPlayer || currentPlayer == noPlayer) && requestedPlayer == from) ||
           (currentPlayer == from && (requestedPlayer == aiPlayer || requestedPlayer == noPlayer)) ||
           (currentPlayer == aiPlayer && requestedPlayer == noPlayer) ||
           (currentPlayer == noPlayer && requestedPlayer == aiPlayer);
}

template <typename State, class GetPlayerStateFn, class GetPlayerIdFn, class SendFn>
void BroadcastNetworkMissionPlayerRoleUpdate(int playerCount, GetPlayerStateFn&& getPlayerState,
                                             GetPlayerIdFn&& getPlayerId, State createState, SendFn&& sendToPlayer)
{
    for (int playerIndex = 0; playerIndex < playerCount; ++playerIndex)
    {
        if (std::forward<GetPlayerStateFn>(getPlayerState)(playerIndex) < createState)
        {
            continue;
        }

        std::forward<SendFn>(sendToPlayer)(std::forward<GetPlayerIdFn>(getPlayerId)(playerIndex));
    }
}

template <class GetTypeFn, class GetKeyFn>
NetworkJipMessageStoreAction BuildNetworkJipMessageStoreAction(int messageCount, GetTypeFn&& getType, GetKeyFn&& getKey,
                                                               int messageType, const RString& key, int maxMessages)
{
    NetworkJipMessageStoreAction action{};
    if (key.GetLength() > 0)
    {
        for (int messageIndex = 0; messageIndex < messageCount; ++messageIndex)
        {
            if (std::forward<GetTypeFn>(getType)(messageIndex) != messageType)
            {
                continue;
            }
            if (std::forward<GetKeyFn>(getKey)(messageIndex) == key)
            {
                action.shouldReplace = true;
                action.replaceIndex = messageIndex;
                return action;
            }
        }
    }

    action.shouldAppend = messageCount < maxMessages;
    return action;
}

struct NetworkRemoteExecDispatchResult
{
    bool executedOnServer = false;
    bool acceptedClientTarget = false;
    bool sentToClient = false;
};

template <typename State, class GetPlayerIdFn, class GetPlayerStateFn, class SendFn, class ExecuteServerFn>
NetworkRemoteExecDispatchResult
DispatchNetworkRemoteExecTarget(int target, int playerCount, GetPlayerIdFn&& getPlayerId,
                                GetPlayerStateFn&& getPlayerState, State loadIslandState, SendFn&& sendToPlayer,
                                ExecuteServerFn&& executeServer)
{
    NetworkRemoteExecDispatchResult result;
    if (target == 0)
    {
        std::forward<ExecuteServerFn>(executeServer)();
        result.executedOnServer = true;
    }

    if (target == 2)
    {
        std::forward<ExecuteServerFn>(executeServer)();
        result.executedOnServer = true;
        return result;
    }

    if (target == 0 || target == -2 || target < 0)
    {
        result.acceptedClientTarget = true;
        const int excluded = target < 0 && target != -2 ? -target : 0;
        for (int playerIndex = 0; playerIndex < playerCount; ++playerIndex)
        {
            const int dpid = std::forward<GetPlayerIdFn>(getPlayerId)(playerIndex);
            if (excluded != 0 && dpid == excluded)
            {
                continue;
            }
            if (std::forward<GetPlayerStateFn>(getPlayerState)(playerIndex) < loadIslandState)
            {
                continue;
            }

            std::forward<SendFn>(sendToPlayer)(dpid);
            result.sentToClient = true;
        }
        return result;
    }

    for (int playerIndex = 0; playerIndex < playerCount; ++playerIndex)
    {
        const int dpid = std::forward<GetPlayerIdFn>(getPlayerId)(playerIndex);
        if (dpid != target)
        {
            continue;
        }
        result.acceptedClientTarget = true;
        if (std::forward<GetPlayerStateFn>(getPlayerState)(playerIndex) < loadIslandState)
        {
            continue;
        }

        std::forward<SendFn>(sendToPlayer)(dpid);
        result.sentToClient = true;
        return result;
    }
    return result;
}

template <typename State, class GetPlayerIdFn, class GetPlayerStateFn, class SendFn, class ExecuteServerFn,
          class GetObjectOwnerFn>
NetworkRemoteExecDispatchResult
DispatchNetworkRemoteExecTargetSelector(const RemoteExecTargetSelector& selector, int playerCount,
                                        GetPlayerIdFn&& getPlayerId, GetPlayerStateFn&& getPlayerState,
                                        State loadIslandState, SendFn&& sendToPlayer, ExecuteServerFn&& executeServer,
                                        GetObjectOwnerFn&& getObjectOwner)
{
    NetworkRemoteExecDispatchResult result;
    AutoArray<int> sentPlayers;
    bool serverExecuted = false;

    auto sendOnce = [&](int dpid)
    {
        for (int i = 0; i < sentPlayers.Size(); ++i)
        {
            if (sentPlayers[i] == dpid)
            {
                return;
            }
        }
        sentPlayers.Add(dpid);
        sendToPlayer(dpid);
        result.sentToClient = true;
    };
    auto executeServerOnce = [&]()
    {
        if (serverExecuted)
        {
            return;
        }
        serverExecuted = true;
        executeServer();
        result.executedOnServer = true;
    };

    auto dispatchScalar = [&](int target)
    {
        NetworkRemoteExecDispatchResult scalarResult = DispatchNetworkRemoteExecTarget(
            target, playerCount, getPlayerId, getPlayerState, loadIslandState, sendOnce, executeServerOnce);
        result.acceptedClientTarget = result.acceptedClientTarget || scalarResult.acceptedClientTarget;
    };

    auto dispatchOwner = [&](NetworkId id)
    {
        if (id.IsNull())
        {
            return;
        }
        const int owner = getObjectOwner(id);
        if (owner == AI_PLAYER)
        {
            executeServerOnce();
            return;
        }
        if (owner != 0)
        {
            dispatchScalar(owner);
        }
    };

    auto visit = [&](auto&& self, const RemoteExecTargetSelector& current) -> void
    {
        if (current.kind == RemoteExecTargetKind::Scalar)
        {
            dispatchScalar(current.scalar);
            return;
        }
        if (current.kind == RemoteExecTargetKind::Object || current.kind == RemoteExecTargetKind::Group)
        {
            dispatchOwner(current.id);
            return;
        }
        if (current.kind == RemoteExecTargetKind::Array)
        {
            for (int i = 0; i < current.items.Size(); ++i)
            {
                self(self, current.items[i]);
            }
        }
    };
    visit(visit, selector);
    return result;
}

template <class GetObjectOwnerFn>
bool RemoteExecTargetSelectorReplaysTo(const RemoteExecTargetSelector& selector, int dpnid,
                                       GetObjectOwnerFn&& getObjectOwner)
{
    if (selector.kind == RemoteExecTargetKind::Scalar)
    {
        const int target = selector.scalar;
        if (target == 2)
        {
            return false;
        }
        if (target > 0)
        {
            return target == dpnid;
        }
        if (target < 0 && target != -2)
        {
            return -target != dpnid;
        }
        return true;
    }
    if (selector.kind == RemoteExecTargetKind::Object || selector.kind == RemoteExecTargetKind::Group)
    {
        const int owner = getObjectOwner(selector.id);
        return owner != 0 && owner != AI_PLAYER && owner == dpnid;
    }
    if (selector.kind == RemoteExecTargetKind::Array)
    {
        for (int i = 0; i < selector.items.Size(); ++i)
        {
            if (RemoteExecTargetSelectorReplaysTo(selector.items[i], dpnid, getObjectOwner))
            {
                return true;
            }
        }
    }
    return false;
}

template <typename State, class SetRolePlayerFn, class SetRoleLockedFn, class GetPlayerIdFn, class GetInfoStateFn,
          class SetInfoStateFn, class SendMissionInfoFn, class SetPlayerStateFn>
void ApplyNetworkMissionDebriefingReassign(bool disabledAI, int roleCount, int playerCount, int aiPlayer, int noPlayer,
                                           State createState, State prepareSideState, SetRolePlayerFn&& setRolePlayer,
                                           SetRoleLockedFn&& setRoleLocked, GetPlayerIdFn&& getPlayerId,
                                           GetInfoStateFn&& getInfoState, SetInfoStateFn&& setInfoState,
                                           SendMissionInfoFn&& sendMissionInfo, SetPlayerStateFn&& setPlayerState)
{
    const int defaultPlayer = disabledAI ? noPlayer : aiPlayer;
    for (int i = 0; i < roleCount; ++i)
    {
        std::forward<SetRolePlayerFn>(setRolePlayer)(i, defaultPlayer);
        std::forward<SetRoleLockedFn>(setRoleLocked)(i, false);
    }

    for (int i = 0; i < playerCount; ++i)
    {
        const int dpid = std::forward<GetPlayerIdFn>(getPlayerId)(i);
        if (std::forward<GetInfoStateFn>(getInfoState)(i) < createState)
        {
            continue;
        }

        std::forward<SendMissionInfoFn>(sendMissionInfo)(dpid, true);
        std::forward<SetInfoStateFn>(setInfoState)(i, prepareSideState);
        std::forward<SetPlayerStateFn>(setPlayerState)(dpid, prepareSideState);
    }
}

template <typename State, class SetBotStateFn, class MissionStartFn>
bool ApplyNetworkMissionBriefingStart(bool allPlayersReady, State playState, int botClient, SetBotStateFn&& setBotState,
                                      MissionStartFn&& onMissionStart, State& currentState)
{
    if (!allPlayersReady)
    {
        return false;
    }

    currentState = playState;
    std::forward<SetBotStateFn>(setBotState)(botClient, playState);
    std::forward<MissionStartFn>(onMissionStart)();
    return true;
}

template <typename State, class SendMissionFn, class SendStateFn>
bool ApplyNetworkMissionJipFastTrack(State clientState, State prepareOkState, bool isJip, State serverState,
                                     State playState, bool missionFileValid, State transferMissionState,
                                     State loadIslandState, State briefingState, SendMissionFn&& sendMissionFile,
                                     SendStateFn&& sendState)
{
    if (!ShouldFastTrackNetworkMissionJip(clientState, prepareOkState, isJip, serverState, playState))
    {
        return false;
    }

    if (!missionFileValid)
    {
        std::forward<SendMissionFn>(sendMissionFile)();
    }

    SendNetworkMissionFastTrackStates(transferMissionState, loadIslandState, briefingState,
                                      std::forward<SendStateFn>(sendState));
    return true;
}

template <typename State>
NetworkMissionJoinStatePlan<State> BuildNetworkMissionJoinStatePlan(bool isJip, State serverState, State loginState,
                                                                    State prepareSideState, State prepareRoleState,
                                                                    State playState)
{
    if (isJip && serverState >= playState)
    {
        return {prepareRoleState, prepareRoleState, false, true};
    }

    NetworkMissionJoinStatePlan<State> plan{loginState, serverState, false, false};
    if (serverState >= prepareSideState)
    {
        plan.playerState = prepareSideState;
        plan.shouldSendMissionInfo = true;
        plan.shouldResetMissionValidity = true;
    }
    if (plan.stateToSend > prepareRoleState)
    {
        plan.stateToSend = prepareRoleState;
    }
    return plan;
}

inline bool ShouldSendNetworkMissionFileForFastTrack(bool missionFileValid)
{
    return !missionFileValid;
}

inline int ComputeNetworkMissionElapsedMs(int currentTick, int missionStartTick)
{
    return currentTick - missionStartTick;
}

inline int ComputeNetworkMissionStartTickFromElapsed(int currentTick, int elapsedMs)
{
    return currentTick - elapsedMs;
}

inline bool CanEnterNetworkMissionTransferState(bool hasMissionRole)
{
    return hasMissionRole;
}

inline bool CanEnterNetworkMissionLoadIsland(bool hasMissionRole, bool bypassMissionValidation, bool missionFileValid)
{
    return hasMissionRole && (bypassMissionValidation || missionFileValid);
}

template <typename State>
bool CanEnterNetworkMissionBriefing(State currentState, State loadIslandState)
{
    return currentState >= loadIslandState;
}

inline bool ShouldReportNetworkMissionBriefingLoadError(int maxError, int errorThreshold)
{
    return maxError >= errorThreshold;
}

template <typename State>
bool CanEnterNetworkMissionPlay(State currentState, State briefingState)
{
    return currentState == briefingState;
}

template <typename State>
NetworkMissionPlayerStateUpdate<State> ComputeNetworkMissionPlayerStateUpdate(
    State targetState, State currentState, bool hasMissionRole, bool isDedicatedBotClient, bool missionFileValid,
    State prepareRoleState, State prepareOkState, State debriefingState, State debriefingOkState,
    State transferMissionState, State loadIslandState, State briefingState, State playState)
{
    if (targetState == prepareRoleState)
    {
        if (currentState > prepareRoleState)
        {
            return {prepareRoleState, true,
                    ShouldResetNetworkMissionValidityOnPrepareRole(targetState, currentState, prepareRoleState,
                                                                   transferMissionState)};
        }
        if (isDedicatedBotClient)
        {
            return {prepareRoleState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == prepareOkState)
    {
        if (hasMissionRole || isDedicatedBotClient)
        {
            return {prepareOkState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == debriefingState || targetState == debriefingOkState)
    {
        if (currentState >= debriefingState)
        {
            return {targetState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == transferMissionState)
    {
        if (CanEnterNetworkMissionTransferState(hasMissionRole || isDedicatedBotClient))
        {
            return {transferMissionState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == loadIslandState)
    {
        if (CanEnterNetworkMissionLoadIsland(hasMissionRole || isDedicatedBotClient, isDedicatedBotClient,
                                             missionFileValid))
        {
            return {loadIslandState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == briefingState)
    {
        if (CanEnterNetworkMissionBriefing(currentState, loadIslandState))
        {
            return {briefingState, true, false};
        }
        return {currentState, false, false};
    }

    if (targetState == playState)
    {
        if (CanEnterNetworkMissionPlay(currentState, briefingState))
        {
            return {playState, true, false};
        }
        return {currentState, false, false};
    }

    return {targetState, true, false};
}

template <typename State>
NetworkMissionClientStateUpdate<State>
ComputeNetworkMissionClientStateUpdate(State targetState, State currentState, bool hasMissionRole,
                                       bool isDedicatedServer, State prepareRoleState, State prepareOkState,
                                       State debriefingState, State debriefingOkState, State transferMissionState)
{
    if (targetState == prepareRoleState)
    {
        if (isDedicatedServer)
        {
            return {prepareRoleState, true};
        }
        return {currentState, false};
    }

    if (targetState == prepareOkState)
    {
        if (hasMissionRole || isDedicatedServer)
        {
            return {prepareOkState, true};
        }
        return {currentState, false};
    }

    if (targetState == debriefingState || targetState == debriefingOkState)
    {
        if (currentState >= debriefingState)
        {
            return {targetState, true};
        }
        return {currentState, false};
    }

    if (targetState == transferMissionState)
    {
        if (CanEnterNetworkMissionTransferState(hasMissionRole || isDedicatedServer))
        {
            return {transferMissionState, true};
        }
        return {currentState, false};
    }

    return {targetState, true};
}

template <typename State>
void ApplyNetworkMissionClientStateUpdate(const NetworkMissionClientStateUpdate<State>& clientStateUpdate,
                                          State& currentState)
{
    if (clientStateUpdate.shouldAssignState)
    {
        currentState = clientStateUpdate.nextState;
    }
}

template <class GetRoleFn>
int CountNetworkMissionConnectedHumanClients(int roleCount, GetRoleFn&& getRole, int noPlayer, int aiPlayer)
{
    int connectedHumanClients = 0;
    for (int i = 0; i < roleCount; ++i)
    {
        const auto* role = std::forward<GetRoleFn>(getRole)(i);
        if (role != nullptr && role->player != noPlayer && role->player != aiPlayer)
        {
            ++connectedHumanClients;
        }
    }
    return connectedHumanClients;
}

inline int ComputeNetworkMissionHideBodiesBudget(bool isBotClient, int connectedHumanClients, int botClientBodies,
                                                 int clientBodiesTotal)
{
    if (isBotClient)
    {
        return -botClientBodies;
    }

    int hideBodies = -(clientBodiesTotal / connectedHumanClients);
    return std::min(hideBodies, -1);
}

template <class GetRoleFn>
int BuildNetworkMissionPlayHideBodies(bool isBotClient, int roleCount, GetRoleFn&& getRole, int noPlayer, int aiPlayer,
                                      int botClientBodies, int clientBodiesTotal)
{
    if (isBotClient)
    {
        return ComputeNetworkMissionHideBodiesBudget(true, 1, botClientBodies, clientBodiesTotal);
    }

    const int connectedHumanClients =
        CountNetworkMissionConnectedHumanClients(roleCount, std::forward<GetRoleFn>(getRole), noPlayer, aiPlayer);
    return ComputeNetworkMissionHideBodiesBudget(false, connectedHumanClients, botClientBodies, clientBodiesTotal);
}

template <typename State>
NetworkMissionPlayEntry<State> ComputeNetworkMissionPlayEntry(State currentState, State briefingState, State playState,
                                                              bool isJip, int hideBodies)
{
    if (!CanEnterNetworkMissionPlay(currentState, briefingState))
    {
        return {currentState, false, false, false, 0};
    }

    return {playState, true, isJip, true, hideBodies};
}

template <typename State, class ClientInfo, class CreateFn, class RunScriptFn, class ClearFn>
void ApplyNetworkMissionPlayEntry(const NetworkMissionPlayEntry<State>& playEntry, ClientInfo& clientInfo,
                                  CreateFn&& createLocalObject, RunScriptFn&& runMissionScript, ClearFn&& clearBodies,
                                  State& currentState, int& hideBodies)
{
    if (!playEntry.shouldAssignState)
    {
        return;
    }

    std::forward<CreateFn>(createLocalObject)(clientInfo);
    currentState = playEntry.nextState;

    if (playEntry.shouldRunJipInitScripts)
    {
        std::forward<RunScriptFn>(runMissionScript)("initPlayerLocal.sqs");
        std::forward<RunScriptFn>(runMissionScript)("initJIP.sqs");
    }

    hideBodies = playEntry.hideBodies;
    if (playEntry.shouldClearBodies)
    {
        std::forward<ClearFn>(clearBodies)();
    }
}

template <typename State>
NetworkMissionBriefingEntry<State> ComputeNetworkMissionBriefingEntry(State currentState, State loadIslandState,
                                                                      State briefingState, bool shouldReportLoadError)
{
    if (!CanEnterNetworkMissionBriefing(currentState, loadIslandState))
    {
        return {currentState, false, shouldReportLoadError};
    }

    return {briefingState, true, shouldReportLoadError};
}

template <typename State, class ReportFn, class ClearFn>
void ApplyNetworkMissionBriefingEntry(const NetworkMissionBriefingEntry<State>& briefingEntry,
                                      const NetworkMissionBriefingReport& briefingReport,
                                      ReportFn&& reportBriefingMessage, ClearFn&& clearRespawnQueue,
                                      State& currentState)
{
    if (briefingReport.shouldReport)
    {
        std::forward<ReportFn>(reportBriefingMessage)(briefingReport.message);
    }

    std::forward<ClearFn>(clearRespawnQueue)();
    if (briefingEntry.shouldAssignState)
    {
        currentState = briefingEntry.nextState;
    }
}

template <typename State>
bool ShouldResetNetworkMissionValidityOnPrepareRole(State nextState, State currentState, State prepareRoleState,
                                                    State transferMissionState)
{
    return nextState == prepareRoleState && currentState == transferMissionState;
}

template <typename ValidateFn>
NetworkMissionFileValidationResult ValidateNetworkMissionFileWithCache(const RString& missionPath,
                                                                       const RString& cachePath, ValidateFn&& validate)
{
    NetworkMissionFileValidationResult result;
    result.missionPathValid = validate(missionPath);
    result.fileValid = result.missionPathValid;
    if (!result.fileValid)
    {
        result.checkedCache = true;
        result.fileValid = validate(cachePath);
    }
    return result;
}

template <typename PlayerInfo>
bool ApplyNetworkMissionFileValidityReport(PlayerInfo* info, bool valid)
{
    if (info == nullptr || !valid)
    {
        return false;
    }

    info->missionFileValid = true;
    return true;
}

template <typename State>
State ComputeFallbackNetworkMissionLoadIslandState(State currentState, State prepareSideState)
{
    if (currentState < prepareSideState)
    {
        return prepareSideState;
    }

    return currentState;
}

template <typename State>
NetworkMissionLoadIslandTransition<State>
ComputeNetworkMissionLoadIslandTransition(State currentState, State prepareSideState, State loadIslandState,
                                          bool missionFileValid, bool prepareGameSucceeded)
{
    if (missionFileValid && prepareGameSucceeded)
    {
        return {loadIslandState, false};
    }

    const State fallbackState = ComputeFallbackNetworkMissionLoadIslandState(currentState, prepareSideState);
    if (fallbackState == prepareSideState)
    {
        return {prepareSideState, false};
    }

    return {currentState, true};
}

template <typename State, class PrepareFn>
NetworkMissionLoadIslandEntry<State> BuildNetworkMissionLoadIslandEntry(State currentState, State prepareSideState,
                                                                        State loadIslandState, bool canEnterLoadIsland,
                                                                        bool isServer, bool missionFileValid,
                                                                        PrepareFn&& prepareGame)
{
    if (!canEnterLoadIsland)
    {
        return {currentState, false, false};
    }

    if (isServer)
    {
        return {loadIslandState, true, false};
    }

    const bool prepareGameSucceeded = missionFileValid && std::forward<PrepareFn>(prepareGame)();
    const auto transition = ComputeNetworkMissionLoadIslandTransition(currentState, prepareSideState, loadIslandState,
                                                                      missionFileValid, prepareGameSucceeded);
    return {transition.nextState, true, transition.shouldWarn};
}

template <typename State, class WarnFn, class SkipFn>
void ApplyNetworkMissionLoadIslandEntry(const NetworkMissionLoadIslandEntry<State>& loadIslandEntry,
                                        WarnFn&& warnPrepareGame, SkipFn&& warnSkipped, State& currentState)
{
    if (!loadIslandEntry.shouldAssignState)
    {
        std::forward<SkipFn>(warnSkipped)();
        return;
    }

    currentState = loadIslandEntry.nextState;
    if (loadIslandEntry.shouldWarn)
    {
        std::forward<WarnFn>(warnPrepareGame)(currentState);
    }
}

template <typename State, typename SendFn>
void SendNetworkMissionFastTrackStates(State transferMissionState, State loadIslandState, State briefingState,
                                       SendFn&& sendState)
{
    sendState(transferMissionState);
    sendState(loadIslandState);
    sendState(briefingState);
}

template <class ValidateFn>
NetworkMissionFileValidationPlan BuildNetworkMissionFileValidationPlan(const RString& fileDir,
                                                                       const char* missionFileName,
                                                                       ValidateFn&& validateMissionFile)
{
    const RString missionPath = fileDir + RString(missionFileName != nullptr ? missionFileName : "");
    const RString cachePath = BuildNetworkMissionTransferCacheBasePath(missionFileName);
    return {missionPath, cachePath,
            ValidateNetworkMissionFileWithCache(missionPath, cachePath, std::forward<ValidateFn>(validateMissionFile))};
}

inline NetworkMissionFileValidityReport BuildNetworkMissionFileValidityReport(bool missionFileValid)
{
    return {missionFileValid};
}

template <class DisconnectFn, class ActivateFn>
bool ApplyNetworkMissionAddonAvailability(const FindArrayRStringCI& addOns, const FindArrayRStringCI& missingAddons,
                                          const char* missingAddonMessage, DisconnectFn&& disconnectPeer,
                                          ActivateFn&& activateAddons)
{
    if (missingAddons.Size() > 0)
    {
        std::forward<DisconnectFn>(disconnectPeer)(
            BuildNetworkMissionMissingAddonMessage(missingAddonMessage, missingAddons));
        return false;
    }

    std::forward<ActivateFn>(activateAddons)(addOns);
    return true;
}

template <class LogMissionFn, class LogCacheFn, class SendFn>
NetworkMissionFileValidityReport
ApplyNetworkMissionFileValidationPlan(const NetworkMissionFileValidationPlan& validationPlan,
                                      LogMissionFn&& logMissionPath, LogCacheFn&& logCachePath,
                                      SendFn&& sendAskMissionFile, bool& missionFileValid)
{
    missionFileValid = validationPlan.validation.fileValid;
    std::forward<LogMissionFn>(logMissionPath)(validationPlan.missionPath, validationPlan.validation.missionPathValid);
    if (validationPlan.validation.checkedCache)
    {
        std::forward<LogCacheFn>(logCachePath)(validationPlan.cachePath, missionFileValid);
    }

    const auto validityReport = BuildNetworkMissionFileValidityReport(missionFileValid);
    std::forward<SendFn>(sendAskMissionFile)(validityReport.missionFileValid);
    return validityReport;
}

template <typename PlayerInfo>
bool CanSendNetworkMissionFileToPlayer(const PlayerInfo& info, int botClient, int minimumTransferState)
{
    return info.dpid != botClient && info.state >= minimumTransferState && !info.missionFileValid;
}

template <typename PlayerInfo>
bool CanTrackNetworkMissionFilePlayer(const PlayerInfo& info, int botClient, int minimumTransferState)
{
    return info.dpid != botClient && info.state >= minimumTransferState;
}

template <typename GetPlayerFn, typename AddRecipientFn>
int CollectNetworkMissionFileRecipients(int playerCount, GetPlayerFn&& getPlayer, int botClient,
                                        int minimumTransferState, AddRecipientFn&& addRecipient)
{
    int recipientCount = 0;
    for (int i = 0; i < playerCount; ++i)
    {
        const auto& info = std::forward<GetPlayerFn>(getPlayer)(i);
        if (CanSendNetworkMissionFileToPlayer(info, botClient, minimumTransferState))
        {
            std::forward<AddRecipientFn>(addRecipient)(info.dpid);
            ++recipientCount;
        }
    }

    return recipientCount;
}

template <typename GetRecipientFn, typename SendFn>
int SendNetworkMissionFileSegmentToRecipients(int recipientCount, GetRecipientFn&& getRecipient, int botClient,
                                              SendFn&& sendSegment)
{
    int sentCount = 0;
    for (int i = 0; i < recipientCount; ++i)
    {
        const int dpid = std::forward<GetRecipientFn>(getRecipient)(i);
        if (dpid != botClient)
        {
            std::forward<SendFn>(sendSegment)(dpid);
            ++sentCount;
        }
    }

    return sentCount;
}

template <typename GetPlayerFn>
int MarkTrackedNetworkMissionFilePlayersValid(int playerCount, GetPlayerFn&& getPlayer, int botClient,
                                              int minimumTransferState);

template <typename GetPlayerFn, typename HasRoleFn>
bool AreTrackedNetworkMissionFilesValid(int playerCount, GetPlayerFn&& getPlayer, int botClient,
                                        int minimumTransferState, HasRoleFn&& hasRole, int& trackedCount)
{
    trackedCount = 0;
    for (int i = 0; i < playerCount; ++i)
    {
        const auto& info = getPlayer(i);
        if (info.dpid == botClient || !hasRole(info.dpid) || info.state < minimumTransferState)
        {
            continue;
        }

        ++trackedCount;
        if (!info.missionFileValid)
        {
            return false;
        }
    }

    return true;
}

template <typename State>
State GetNetworkMissionFileSendMinimumState(int player, int noPlayer, State createState, State transferMissionState)
{
    return player == noPlayer ? transferMissionState : createState;
}

template <typename MessageType, typename GetRecipientFn, typename SendFn, typename GetPlayerFn>
NetworkMissionFileSendResult SendCurrentNetworkMissionFile(const char* missionFileName, const void* data, int totalSize,
                                                           int recipientCount, GetRecipientFn&& getRecipient,
                                                           int botClient, SendFn&& sendMessage, int playerCount,
                                                           GetPlayerFn&& getPlayer, int minimumTransferState)
{
    NetworkMissionFileSendResult result;
    result.destinationPath = BuildNetworkMissionTransferCachePboPath(missionFileName);
    result.segmentCount = SendNetworkMissionFileSegments<MessageType>(
        result.destinationPath, data, totalSize,
        [&](MessageType& msg)
        {
            result.sentCount += SendNetworkMissionFileSegmentToRecipients(recipientCount, getRecipient, botClient,
                                                                          [&](int dpid) { sendMessage(dpid, msg); });
        });
    return result;
}

template <typename MessageType, typename GetRecipientFn, typename SendFn>
NetworkMissionFileSendResult SendCurrentNetworkMissionRawFile(const char* missionFileName, const void* data,
                                                              int totalSize, int recipientCount,
                                                              GetRecipientFn&& getRecipient, int botClient,
                                                              SendFn&& sendRaw)
{
    NetworkMissionFileSendResult result;
    result.destinationPath = BuildNetworkMissionTransferCachePboPath(missionFileName);
    result.segmentCount = SendNetworkMissionRawFileSegments<MessageType>(
        result.destinationPath, data, totalSize,
        [&](AutoArray<char>& payload)
        {
            result.sentCount += SendNetworkMissionFileSegmentToRecipients(
                recipientCount, getRecipient, botClient,
                [&](int dpid) { sendRaw(dpid, payload.Data(), payload.Size()); });
        });
    return result;
}

template <typename GetPlayerFn>
int MarkTrackedNetworkMissionFilePlayersValid(int playerCount, GetPlayerFn&& getPlayer, int botClient,
                                              int minimumTransferState)
{
    int markedCount = 0;
    for (int i = 0; i < playerCount; ++i)
    {
        auto& info = std::forward<GetPlayerFn>(getPlayer)(i);
        if (CanTrackNetworkMissionFilePlayer(info, botClient, minimumTransferState))
        {
            info.missionFileValid = true;
            ++markedCount;
        }
    }

    return markedCount;
}

} // namespace Poseidon
