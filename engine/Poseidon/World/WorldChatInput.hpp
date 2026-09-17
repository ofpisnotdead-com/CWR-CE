#pragma once

#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_keycode.h>

namespace Poseidon
{
// Chat submits on both the main and numpad Enter. The original engine got this for
// free — Windows delivered VK_RETURN (0x0D) for either key — and the DirectInput
// builds spelled it out as DIK_RETURN || DIK_NUMPADENTER. SDL keeps the two keys as
// distinct keycodes, so both must be listed explicitly.
inline bool IsChatSubmitKey(unsigned keyCode)
{
    return keyCode == SDLK_RETURN || keyCode == SDLK_KP_ENTER;
}

// Keys the chat line consumes so they never leak into game controls while typing:
// submit and cancel. The set must match what DisplayChatLine::OnKeyUp acts on.
inline bool IsChatConsumedKey(unsigned keyCode)
{
    return IsChatSubmitKey(keyCode) || keyCode == SDLK_ESCAPE;
}

inline bool ShouldHandleMultiplayerChatShortcut(bool chatInputActive)
{
    return !chatInputActive;
}

struct ChatInputActions
{
    bool previousChannel;
    bool nextChannel;
    bool historyUp;
    bool historyDown;
};

inline ChatInputActions PollChatInputActions(InputSubsystem& input)
{
    return {
        input.GetActionToDo(InputContext::Chat, UAChatPrevChannel, true, false),
        input.GetActionToDo(InputContext::Chat, UAChatNextChannel, true, false),
        input.GetActionToDo(InputContext::Chat, UAChatHistoryUp, true, false),
        input.GetActionToDo(InputContext::Chat, UAChatHistoryDown, true, false),
    };
}

inline bool ShouldUseExplicitMultiplayerVoiceTargets(int channel, int unitCount, int globalChannel, int directChannel)
{
    return channel != globalChannel && channel != directChannel && unitCount > 0;
}
} // namespace Poseidon
