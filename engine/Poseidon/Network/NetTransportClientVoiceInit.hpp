#pragma once

#include <Poseidon/Network/NetTransport.hpp>

#include <Poseidon/Audio/Voice/OpusCodec.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
using Poseidon::OpusCodec;

namespace Poseidon
{

struct NetTransportClientVoiceInitResult
{
    bool initialized = false;
    bool captureOpened = false;
};

template <class InitClientFn, class GetClientFn, class SendVoiceFn>
NetTransportClientVoiceInitResult InitializeNetTransportClientVoice(InitClientFn&& initClient, GetClientFn&& getClient,
                                                                    uint32_t playerId, SendVoiceFn&& sendVoice)
{
    initClient();
    auto* client = getClient();
    if (!client)
    {
        return {};
    }

    client->setCodecFactory([]() { return std::make_unique<OpusCodec>(); });
    const bool captureOpened = client->openCapture(nullptr, 16000, 16000);
    client->setPacketSink(
        [sendVoice = std::forward<SendVoiceFn>(sendVoice)](const std::vector<uint8_t>& packet)
        { sendVoice(const_cast<uint8_t*>(packet.data()), static_cast<int>(packet.size()), NMFHighPriority); });
    client->setSenderId(playerId);
    return {.initialized = true, .captureOpened = captureOpened};
}

template <class GetClientFn>
bool SetNetTransportClientVoiceTransmit(GetClientFn&& getClient, bool on)
{
    auto* client = getClient();
    if (!client)
    {
        return false;
    }

    client->setTransmit(on);
    return true;
}

template <class GetClientFn>
bool SetNetTransportClientVoiceSenderId(GetClientFn&& getClient, uint32_t playerId)
{
    auto* client = getClient();
    if (!client)
    {
        return false;
    }

    client->setSenderId(playerId);
    return true;
}

template <class GetClientFn>
bool SetNetTransportClientVoiceSenderIdIfAccepted(bool ackAccepted, GetClientFn&& getClient, uint32_t playerId)
{
    if (!ackAccepted)
    {
        return false;
    }
    return SetNetTransportClientVoiceSenderId(std::forward<GetClientFn>(getClient), playerId);
}

inline bool IsNetTransportClientVoiceAckAccepted(ConnectResult result)
{
    return result == CROK;
}

template <class InitClientFn, class GetClientFn, class SendVoiceFn, class WarnCaptureFn, class LogInitializedFn>
bool ProcessNetTransportClientVoiceInit(InitClientFn&& initClient, GetClientFn&& getClient, uint32_t playerId,
                                        SendVoiceFn&& sendVoice, WarnCaptureFn&& warnCaptureUnavailable,
                                        LogInitializedFn&& logInitialized)
{
    const NetTransportClientVoiceInitResult result =
        InitializeNetTransportClientVoice(std::forward<InitClientFn>(initClient), std::forward<GetClientFn>(getClient),
                                          playerId, std::forward<SendVoiceFn>(sendVoice));
    if (!result.initialized)
    {
        return false;
    }
    if (!result.captureOpened)
    {
        std::forward<WarnCaptureFn>(warnCaptureUnavailable)();
    }
    std::forward<LogInitializedFn>(logInitialized)();
    return true;
}

template <class InitClientFn, class GetClientFn, class SendMessageFn, class WarnCaptureFn, class LogInitializedFn>
bool ProcessNetTransportClientVoiceInitWithSend(InitClientFn&& initClient, GetClientFn&& getClient, uint32_t playerId,
                                                SendMessageFn&& sendMessage, WarnCaptureFn&& warnCaptureUnavailable,
                                                LogInitializedFn&& logInitialized)
{
    auto send = std::forward<SendMessageFn>(sendMessage);
    return ProcessNetTransportClientVoiceInit(
        std::forward<InitClientFn>(initClient), std::forward<GetClientFn>(getClient), playerId,
        [send = std::move(send)](uint8_t* data, int size, NetMsgFlags flags) { send(data, size, flags); },
        std::forward<WarnCaptureFn>(warnCaptureUnavailable), std::forward<LogInitializedFn>(logInitialized));
}

template <class InitClientFn, class GetClientFn, class QueueSendFn, class WarnCaptureFn, class LogInitializedFn>
bool ProcessNetTransportClientVoiceInitWithQueuedSend(InitClientFn&& initClient, GetClientFn&& getClient,
                                                      uint32_t playerId, QueueSendFn&& queueSend,
                                                      WarnCaptureFn&& warnCaptureUnavailable,
                                                      LogInitializedFn&& logInitialized)
{
    auto send = std::forward<QueueSendFn>(queueSend);
    return ProcessNetTransportClientVoiceInit(
        std::forward<InitClientFn>(initClient), std::forward<GetClientFn>(getClient), playerId,
        [send = std::move(send)](uint8_t* data, int size, NetMsgFlags flags)
        {
            DWORD msgID;
            send(reinterpret_cast<BYTE*>(data), size, msgID, flags);
        },
        std::forward<WarnCaptureFn>(warnCaptureUnavailable), std::forward<LogInitializedFn>(logInitialized));
}

} // namespace Poseidon
