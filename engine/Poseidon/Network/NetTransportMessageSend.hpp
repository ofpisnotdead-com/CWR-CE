#pragma once

#include <Poseidon/Network/Legacy/netpch.hpp>

#include <Poseidon/Network/NetTransportUserIteration.hpp>

#include <type_traits>
#include <utility>
#include <Poseidon/Network/NetTransport.hpp>
using Poseidon::Foundation::IteratorState;

namespace Poseidon
{

inline unsigned16 BuildNetTransportGuaranteedMessageFlags(bool urgent)
{
    return MSG_VIM_FLAG | (urgent ? MSG_URGENT_FLAG : 0);
}

inline unsigned16 BuildNetTransportBroadcastMessageFlags(bool guaranteed, bool urgent)
{
    unsigned16 flags = guaranteed ? MSG_VIM_FLAG : 0;
    if (urgent)
    {
        flags |= MSG_URGENT_FLAG;
    }
    return flags;
}

template <class SendChunk>
bool SendNetTransportMessageFragments(int bufferSize, int maxMessage, unsigned16 baseFlags, SendChunk&& sendChunk)
{
    int remaining = bufferSize;
    const unsigned16 fragmentFlags = baseFlags | MSG_PART_FLAG;
    do
    {
        const int packetSize = (remaining > maxMessage) ? maxMessage : remaining;
        remaining -= packetSize;
        if (!sendChunk(packetSize, fragmentFlags | (remaining ? 0 : MSG_CLOSING_FLAG)))
        {
            return false;
        }
    } while (remaining);

    return true;
}

template <class MessageRef, class CreateMessage, class ConfigureCommon, class DispatchMessage>
bool TrySendNetTransportMessage(MessageRef& outMessage, int payloadSize, unsigned16 flags, unsigned8* payload,
                                CreateMessage&& createMessage, ConfigureCommon&& configureCommon,
                                DispatchMessage&& dispatchMessage)
{
    outMessage = createMessage(payloadSize);
    if (!outMessage)
    {
        return false;
    }

    outMessage->setFlags(MSG_ALL_FLAGS, flags);
    if (flags)
    {
        outMessage->setOrderedPrevious();
    }
    else
    {
        configureCommon(*outMessage);
    }
    outMessage->setData(payload, payloadSize);
    dispatchMessage(*outMessage);
    return true;
}

template <class MessageRef, class CreateMessage, class DispatchMessage>
bool TrySendNetTransportGuaranteedBuffer(MessageRef& outMessage, int bufferSize, int maxMessage, unsigned16 flags,
                                         unsigned8*& payload, CreateMessage&& createMessage,
                                         DispatchMessage&& dispatchMessage)
{
    const int maxGuaranteedPayload =
        maxMessage > NetTransportReliableFragmentPayload ? NetTransportReliableFragmentPayload : maxMessage;
    if (bufferSize > maxGuaranteedPayload)
    {
        return SendNetTransportMessageFragments(
            bufferSize, maxGuaranteedPayload, flags,
            [&outMessage, &payload, &createMessage, &dispatchMessage](int packetSize, unsigned16 packetFlags)
            {
                if (!TrySendNetTransportMessage(
                        outMessage, packetSize, packetFlags, payload, createMessage, [](auto&) {}, dispatchMessage))
                {
                    return false;
                }

                payload += packetSize;
                return true;
            });
    }

    return TrySendNetTransportMessage(
        outMessage, bufferSize, flags, payload, std::forward<CreateMessage>(createMessage), [](auto&) {},
        std::forward<DispatchMessage>(dispatchMessage));
}

template <class UserMapT, class MessageRef, class CreateMessage, class ConfigureCommon, class DispatchMessage>
bool TryBroadcastNetTransportMessage(UserMapT& users, MessageRef& outMessage, int payloadSize, unsigned16 flags,
                                     unsigned8* payload, CreateMessage&& createMessage,
                                     ConfigureCommon&& configureCommon, DispatchMessage&& dispatchMessage)
{
    bool sentOk = true;
    ForEachNetTransportUserChannel(users,
                                   [&](NetChannel* targetChannel)
                                   {
                                       if (!sentOk)
                                       {
                                           return;
                                       }

                                       sentOk = TrySendNetTransportMessage(
                                           outMessage, payloadSize, flags, payload,
                                           [&](int messageSize) { return createMessage(messageSize, targetChannel); },
                                           configureCommon, dispatchMessage);
                                   });
    return sentOk;
}

struct NetTransportSingleSendResult
{
    bool success = false;
    bool foundChannel = false;
};

enum class NetTransportClientSendStatus
{
    InvalidInput,
    OversizedNonGuaranteed,
    AllocationFailed,
    Sent,
};

struct NetTransportClientSendResult
{
    NetTransportClientSendStatus status = NetTransportClientSendStatus::InvalidInput;
    DWORD msgID = 0;
};

enum class NetTransportServerSendStatus
{
    InvalidInput,
    OversizedNonGuaranteed,
    MissingChannel,
    AllocationFailed,
    Sent,
};

struct NetTransportServerSendResult
{
    NetTransportServerSendStatus status = NetTransportServerSendStatus::InvalidInput;
    DWORD msgID = 0;
};

template <class ChannelT, class CreateMessage, class ConfigureCommon, class DispatchGuaranteed, class DispatchCommon>
NetTransportClientSendResult
TrySendNetTransportClientMessage(ChannelT* channel, int bufferSize, unsigned8* payload, bool guaranteed, bool urgent,
                                 CreateMessage&& createMessage, ConfigureCommon&& configureCommon,
                                 DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon)
{
    NetTransportClientSendResult result{};
    if (!channel || !payload || bufferSize <= 0)
    {
        return result;
    }

    const int maxMessage = channel->maxMessageData();
    if (!guaranteed && bufferSize > maxMessage)
    {
        result.status = NetTransportClientSendStatus::OversizedNonGuaranteed;
        return result;
    }

    using MessageRef = std::invoke_result_t<CreateMessage, int>;
    MessageRef outMessage;
    const bool sent =
        guaranteed
            ? TrySendNetTransportGuaranteedBuffer(
                  outMessage, bufferSize, maxMessage, BuildNetTransportGuaranteedMessageFlags(urgent), payload,
                  std::forward<CreateMessage>(createMessage), std::forward<DispatchGuaranteed>(dispatchGuaranteed))
            : TrySendNetTransportMessage(outMessage, bufferSize, 0, payload, std::forward<CreateMessage>(createMessage),
                                         std::forward<ConfigureCommon>(configureCommon),
                                         std::forward<DispatchCommon>(dispatchCommon));
    if (!sent)
    {
        result.status = NetTransportClientSendStatus::AllocationFailed;
        return result;
    }

    result.status = NetTransportClientSendStatus::Sent;
    result.msgID = (DWORD)outMessage->id;
    return result;
}

template <class ChannelT, class EnterFn, class LeaveFn, class CreateMessage, class ConfigureCommon,
          class DispatchGuaranteed, class DispatchCommon, class OnOversizedFn>
bool ProcessNetTransportClientSendLocked(ChannelT* channel, int bufferSize, unsigned8* payload, bool guaranteed,
                                         bool urgent, DWORD& msgID, EnterFn&& enterSend, LeaveFn&& leaveSend,
                                         CreateMessage&& createMessage, ConfigureCommon&& configureCommon,
                                         DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon,
                                         OnOversizedFn&& onOversized)
{
    std::forward<EnterFn>(enterSend)();
    const NetTransportClientSendResult result = TrySendNetTransportClientMessage(
        channel, bufferSize, payload, guaranteed, urgent, std::forward<CreateMessage>(createMessage),
        std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchGuaranteed>(dispatchGuaranteed),
        std::forward<DispatchCommon>(dispatchCommon));
    if (result.status == NetTransportClientSendStatus::OversizedNonGuaranteed)
    {
        std::forward<OnOversizedFn>(onOversized)(bufferSize);
    }
    if (result.status != NetTransportClientSendStatus::Sent)
    {
        std::forward<LeaveFn>(leaveSend)();
        return false;
    }

    msgID = result.msgID;
    std::forward<LeaveFn>(leaveSend)();
    return true;
}

template <class ChannelT, class EnterFn, class LeaveFn, class CreateMessage, class ConfigureCommon,
          class DispatchGuaranteed, class DispatchCommon, class OnOversizedFn>
bool ProcessNetTransportClientSendMessageLocked(ChannelT* channel, int bufferSize, unsigned8* payload,
                                                NetMsgFlags flags, DWORD& msgID, EnterFn&& enterSend,
                                                LeaveFn&& leaveSend, CreateMessage&& createMessage,
                                                ConfigureCommon&& configureCommon,
                                                DispatchGuaranteed&& dispatchGuaranteed,
                                                DispatchCommon&& dispatchCommon, OnOversizedFn&& onOversized)
{
    const bool guaranteed = (flags & NMFGuaranteed) != 0;
    const bool urgent = (flags & NMFHighPriority) != 0;
    return ProcessNetTransportClientSendLocked(
        channel, bufferSize, payload, guaranteed, urgent, msgID, std::forward<EnterFn>(enterSend),
        std::forward<LeaveFn>(leaveSend), std::forward<CreateMessage>(createMessage),
        std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchGuaranteed>(dispatchGuaranteed),
        std::forward<DispatchCommon>(dispatchCommon), std::forward<OnOversizedFn>(onOversized));
}

template <class ChannelT, class EnterFn, class LeaveFn, class CreateMessage, class ConfigureCommon,
          class DispatchGuaranteed, class DispatchCommon>
bool ProcessNetTransportClientQueuedSendMessage(ChannelT* channel, int bufferSize, unsigned8* payload,
                                                NetMsgFlags flags, DWORD& msgID, EnterFn&& enterSend,
                                                LeaveFn&& leaveSend, CreateMessage&& createMessage,
                                                ConfigureCommon&& configureCommon,
                                                DispatchGuaranteed&& dispatchGuaranteed,
                                                DispatchCommon&& dispatchCommon)
{
    return ProcessNetTransportClientSendMessageLocked(
        channel, bufferSize, payload, flags, msgID, std::forward<EnterFn>(enterSend), std::forward<LeaveFn>(leaveSend),
        std::forward<CreateMessage>(createMessage), std::forward<ConfigureCommon>(configureCommon),
        std::forward<DispatchGuaranteed>(dispatchGuaranteed), std::forward<DispatchCommon>(dispatchCommon),
        [bufferSize](int)
        { RptF("NetClient: trying to send too large non-guaranteed message (%d bytes long)", bufferSize); });
}

template <class UserMapT, class BroadcastCreate, class SingleCreate, class ConfigureCommon, class DispatchGuaranteed,
          class DispatchCommon>
NetTransportServerSendResult
TrySendNetTransportServerMessage(UserMapT& users, bool broadcast, int player, int bufferSize, int maxMessage,
                                 unsigned8* payload, bool guaranteed, bool urgent, BroadcastCreate&& createBroadcast,
                                 SingleCreate&& createSingle, ConfigureCommon&& configureCommon,
                                 DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon)
{
    NetTransportServerSendResult result{};
    if (!payload || bufferSize <= 0)
    {
        return result;
    }

    if ((!guaranteed || broadcast) && bufferSize > maxMessage)
    {
        result.status = NetTransportServerSendStatus::OversizedNonGuaranteed;
        return result;
    }

    using MessageRef = std::invoke_result_t<BroadcastCreate, int, NetChannel*>;
    MessageRef outMessage;
    const unsigned16 flags = BuildNetTransportBroadcastMessageFlags(guaranteed, urgent);
    if (broadcast)
    {
        if (!TryBroadcastNetTransportMessage(
                users, outMessage, bufferSize, flags, payload, std::forward<BroadcastCreate>(createBroadcast),
                std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchCommon>(dispatchCommon)))
        {
            result.status = NetTransportServerSendStatus::AllocationFailed;
            return result;
        }
    }
    else
    {
        RefD<NetChannel> channel;
        const NetTransportSingleSendResult sent = TrySendNetTransportToSingleUser(
            users, player, channel, outMessage, bufferSize, maxMessage, flags, payload,
            [&channel, &createSingle](int messageSize) { return createSingle(messageSize, channel); },
            std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchGuaranteed>(dispatchGuaranteed),
            std::forward<DispatchCommon>(dispatchCommon));
        if (!sent.foundChannel)
        {
            result.status = NetTransportServerSendStatus::MissingChannel;
            return result;
        }
        if (!sent.success)
        {
            result.status = NetTransportServerSendStatus::AllocationFailed;
            return result;
        }
    }

    result.status = NetTransportServerSendStatus::Sent;
    result.msgID = (DWORD)outMessage->id;
    return result;
}

template <class UserMapT, class GetMaxMessageFn, class IsBroadcastFn, class EnterUsrFn, class LeaveUsrFn,
          class BroadcastCreate, class SingleCreate, class ConfigureCommon, class DispatchGuaranteed,
          class DispatchCommon, class OnEmptyFn, class OnOversizedFn, class OnMissingChannelFn,
          class OnAllocationFailedFn>
bool ProcessNetTransportServerSendFromPeer(
    UserMapT& users, int player, int bufferSize, unsigned8* payload, bool guaranteed, bool urgent, DWORD& msgID,
    GetMaxMessageFn&& getMaxMessage, IsBroadcastFn&& isBroadcast, EnterUsrFn&& enterUsers, LeaveUsrFn&& leaveUsers,
    BroadcastCreate&& createBroadcast, SingleCreate&& createSingle, ConfigureCommon&& configureCommon,
    DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon, OnEmptyFn&& onEmpty,
    OnOversizedFn&& onOversized, OnMissingChannelFn&& onMissingChannel, OnAllocationFailedFn&& onAllocationFailed)
{
    if (!payload || bufferSize <= 0)
    {
        std::forward<OnEmptyFn>(onEmpty)(player);
        return false;
    }

    const bool broadcast = std::forward<IsBroadcastFn>(isBroadcast)(player);
    const int maxMessage = std::forward<GetMaxMessageFn>(getMaxMessage)();
    return ProcessNetTransportServerSend(
        users, broadcast, player, bufferSize, maxMessage, payload, guaranteed, urgent, msgID,
        std::forward<EnterUsrFn>(enterUsers), std::forward<LeaveUsrFn>(leaveUsers),
        std::forward<BroadcastCreate>(createBroadcast), std::forward<SingleCreate>(createSingle),
        std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchGuaranteed>(dispatchGuaranteed),
        std::forward<DispatchCommon>(dispatchCommon), std::forward<OnOversizedFn>(onOversized),
        std::forward<OnMissingChannelFn>(onMissingChannel), std::forward<OnAllocationFailedFn>(onAllocationFailed));
}

template <class UserMapT, class GetMaxMessageFn, class IsBroadcastFn, class EnterUsrFn, class LeaveUsrFn,
          class BroadcastCreate, class SingleCreate, class ConfigureCommon, class DispatchGuaranteed,
          class DispatchCommon, class OnEmptyFn, class OnOversizedFn, class OnMissingChannelFn,
          class OnAllocationFailedFn>
bool ProcessNetTransportServerSendMessageFromPeer(
    UserMapT& users, int player, int bufferSize, unsigned8* payload, NetMsgFlags flags, DWORD& msgID,
    GetMaxMessageFn&& getMaxMessage, IsBroadcastFn&& isBroadcast, EnterUsrFn&& enterUsers, LeaveUsrFn&& leaveUsers,
    BroadcastCreate&& createBroadcast, SingleCreate&& createSingle, ConfigureCommon&& configureCommon,
    DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon, OnEmptyFn&& onEmpty,
    OnOversizedFn&& onOversized, OnMissingChannelFn&& onMissingChannel, OnAllocationFailedFn&& onAllocationFailed)
{
    const bool guaranteed = (flags & NMFGuaranteed) != 0;
    const bool urgent = (flags & NMFHighPriority) != 0;
    return ProcessNetTransportServerSendFromPeer(
        users, player, bufferSize, payload, guaranteed, urgent, msgID, std::forward<GetMaxMessageFn>(getMaxMessage),
        std::forward<IsBroadcastFn>(isBroadcast), std::forward<EnterUsrFn>(enterUsers),
        std::forward<LeaveUsrFn>(leaveUsers), std::forward<BroadcastCreate>(createBroadcast),
        std::forward<SingleCreate>(createSingle), std::forward<ConfigureCommon>(configureCommon),
        std::forward<DispatchGuaranteed>(dispatchGuaranteed), std::forward<DispatchCommon>(dispatchCommon),
        std::forward<OnEmptyFn>(onEmpty), std::forward<OnOversizedFn>(onOversized),
        std::forward<OnMissingChannelFn>(onMissingChannel), std::forward<OnAllocationFailedFn>(onAllocationFailed));
}

template <class UserMapT, class GetMaxMessageFn, class IsBroadcastFn, class EnterUsrFn, class LeaveUsrFn,
          class BroadcastCreate, class SingleCreate, class ConfigureCommon, class DispatchGuaranteed,
          class DispatchCommon, class UserCountFn, class AssertUsersFn>
bool ProcessNetTransportServerQueuedSendMessage(UserMapT& users, int player, int bufferSize, unsigned8* payload,
                                                NetMsgFlags flags, DWORD& msgID, GetMaxMessageFn&& getMaxMessage,
                                                IsBroadcastFn&& isBroadcast, EnterUsrFn&& enterUsers,
                                                LeaveUsrFn&& leaveUsers, BroadcastCreate&& createBroadcast,
                                                SingleCreate&& createSingle, ConfigureCommon&& configureCommon,
                                                DispatchGuaranteed&& dispatchGuaranteed,
                                                DispatchCommon&& dispatchCommon, UserCountFn&& getUserCount,
                                                AssertUsersFn&& assertUsers)
{
    return ProcessNetTransportServerSendMessageFromPeer(
        users, player, bufferSize, payload, flags, msgID, std::forward<GetMaxMessageFn>(getMaxMessage),
        std::forward<IsBroadcastFn>(isBroadcast), std::forward<EnterUsrFn>(enterUsers),
        std::forward<LeaveUsrFn>(leaveUsers), std::forward<BroadcastCreate>(createBroadcast),
        std::forward<SingleCreate>(createSingle), std::forward<ConfigureCommon>(configureCommon),
        std::forward<DispatchGuaranteed>(dispatchGuaranteed), std::forward<DispatchCommon>(dispatchCommon),
        [](int emptyPlayer) { RptF("NetServer: trying to send empty message to %d", emptyPlayer); },
        [](int oversizedBytes)
        { RptF("NetServer: trying to send too large non-guaranteed message (%d bytes long)", oversizedBytes); },
        [&getUserCount, &assertUsers](int missingPlayer)
        {
            RptF("NetServer::SendMsg: cannot find channel #%d, users.card=%u", missingPlayer, getUserCount());
            RptF("NetServer: users.get failed when sending to %d", missingPlayer);
            assertUsers();
        },
        [](int failedPlayer) { RptF("NetServer: pool()->newMessage failed when sending to %d", failedPlayer); });
}

template <class UserMapT, class EnterUsrFn, class LeaveUsrFn, class BroadcastCreate, class SingleCreate,
          class ConfigureCommon, class DispatchGuaranteed, class DispatchCommon, class OnOversizedFn,
          class OnMissingChannelFn, class OnAllocationFailedFn>
bool ProcessNetTransportServerSend(UserMapT& users, bool broadcast, int player, int bufferSize, int maxMessage,
                                   unsigned8* payload, bool guaranteed, bool urgent, DWORD& msgID,
                                   EnterUsrFn&& enterUsers, LeaveUsrFn&& leaveUsers, BroadcastCreate&& createBroadcast,
                                   SingleCreate&& createSingle, ConfigureCommon&& configureCommon,
                                   DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon,
                                   OnOversizedFn&& onOversized, OnMissingChannelFn&& onMissingChannel,
                                   OnAllocationFailedFn&& onAllocationFailed)
{
    std::forward<EnterUsrFn>(enterUsers)();
    const NetTransportServerSendResult result = TrySendNetTransportServerMessage(
        users, broadcast, player, bufferSize, maxMessage, payload, guaranteed, urgent,
        std::forward<BroadcastCreate>(createBroadcast), std::forward<SingleCreate>(createSingle),
        std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchGuaranteed>(dispatchGuaranteed),
        std::forward<DispatchCommon>(dispatchCommon));
    if (result.status == NetTransportServerSendStatus::OversizedNonGuaranteed)
    {
        std::forward<OnOversizedFn>(onOversized)(bufferSize);
        std::forward<LeaveUsrFn>(leaveUsers)();
        return false;
    }
    if (result.status == NetTransportServerSendStatus::MissingChannel)
    {
        std::forward<OnMissingChannelFn>(onMissingChannel)(player);
        std::forward<LeaveUsrFn>(leaveUsers)();
        return false;
    }
    if (result.status != NetTransportServerSendStatus::Sent)
    {
        std::forward<LeaveUsrFn>(leaveUsers)();
        std::forward<OnAllocationFailedFn>(onAllocationFailed)(player);
        return false;
    }

    msgID = result.msgID;
    std::forward<LeaveUsrFn>(leaveUsers)();
    return true;
}

template <class UserMapT, class ChannelRef, class MessageRef, class CreateMessage, class ConfigureCommon,
          class DispatchGuaranteed, class DispatchCommon>
NetTransportSingleSendResult
TrySendNetTransportToSingleUser(UserMapT& users, int player, ChannelRef& channel, MessageRef& outMessage,
                                int bufferSize, int maxMessage, unsigned16 flags, unsigned8* payload,
                                CreateMessage&& createMessage, ConfigureCommon&& configureCommon,
                                DispatchGuaranteed&& dispatchGuaranteed, DispatchCommon&& dispatchCommon)
{
    NetTransportSingleSendResult result{};
    result.foundChannel = users.get(player, channel);
    if (!result.foundChannel)
    {
        return result;
    }

    const int maxGuaranteedPayload =
        maxMessage > NetTransportReliableFragmentPayload ? NetTransportReliableFragmentPayload : maxMessage;
    if ((flags & MSG_VIM_FLAG) && bufferSize > maxGuaranteedPayload)
    {
        result.success =
            TrySendNetTransportGuaranteedBuffer(outMessage, bufferSize, maxMessage, flags, payload,
                                                std::forward<CreateMessage>(createMessage),
                                                std::forward<DispatchGuaranteed>(dispatchGuaranteed));
        return result;
    }

    result.success = TrySendNetTransportMessage(
        outMessage, bufferSize, flags, payload, std::forward<CreateMessage>(createMessage),
        std::forward<ConfigureCommon>(configureCommon), std::forward<DispatchCommon>(dispatchCommon));
    return result;
}

} // namespace Poseidon
