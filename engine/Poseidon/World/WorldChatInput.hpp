#pragma once

namespace Poseidon
{
inline bool ShouldHandleMultiplayerChatShortcut(bool chatInputActive)
{
    return !chatInputActive;
}

inline bool ShouldSwitchMultiplayerChannelFromChatInput()
{
    return false;
}

inline bool ShouldUseExplicitMultiplayerVoiceTargets(int channel, int unitCount, int globalChannel, int directChannel)
{
    return channel != globalChannel && channel != directChannel && unitCount > 0;
}
} // namespace Poseidon
