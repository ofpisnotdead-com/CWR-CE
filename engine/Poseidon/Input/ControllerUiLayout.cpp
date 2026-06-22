#include <Poseidon/Input/ControllerUiLayout.hpp>

namespace Poseidon
{

namespace
{
constexpr float kFirstRepeatDelay = 0.333f;
constexpr float kRepeatDelay = 0.085f;
} // namespace

ControllerUiDispatch ControllerMenuUiLayout::Map(ControllerUiAction action) const
{
    switch (action)
    {
        case ControllerUiAction::NavigateUp:
            return {ControllerUiDispatchKind::KeyTap, SDLK_UP};
        case ControllerUiAction::NavigateDown:
            return {ControllerUiDispatchKind::KeyTap, SDLK_DOWN};
        case ControllerUiAction::NavigateLeft:
            return {ControllerUiDispatchKind::KeyTap, SDLK_LEFT};
        case ControllerUiAction::NavigateRight:
            return {ControllerUiDispatchKind::KeyTap, SDLK_RIGHT};
        case ControllerUiAction::Confirm:
            return {ControllerUiDispatchKind::KeyTap, SDLK_RETURN};
        case ControllerUiAction::Cancel:
            return {ControllerUiDispatchKind::KeyTap, SDLK_ESCAPE};
        case ControllerUiAction::PagePrevious:
            return {ControllerUiDispatchKind::KeyTap, SDLK_PAGEUP};
        case ControllerUiAction::PageNext:
            return {ControllerUiDispatchKind::KeyTap, SDLK_PAGEDOWN};
        default:
            return {};
    }
}

ControllerUiDispatch ControllerEditorUiLayout::Map(ControllerUiAction action) const
{
    switch (action)
    {
        case ControllerUiAction::NavigateUp:
            return {ControllerUiDispatchKind::KeyTap, SDLK_UP};
        case ControllerUiAction::NavigateDown:
            return {ControllerUiDispatchKind::KeyTap, SDLK_DOWN};
        case ControllerUiAction::NavigateLeft:
            return {ControllerUiDispatchKind::KeyTap, SDLK_LEFT};
        case ControllerUiAction::NavigateRight:
            return {ControllerUiDispatchKind::KeyTap, SDLK_RIGHT};
        case ControllerUiAction::Confirm:
        case ControllerUiAction::PrimaryClick:
            return {ControllerUiDispatchKind::PointerPrimaryClick, SDLK_UNKNOWN};
        case ControllerUiAction::PrimaryDown:
            return {ControllerUiDispatchKind::PointerPrimaryDown, SDLK_UNKNOWN};
        case ControllerUiAction::PrimaryUp:
            return {ControllerUiDispatchKind::PointerPrimaryUp, SDLK_UNKNOWN};
        case ControllerUiAction::Cancel:
            return {ControllerUiDispatchKind::KeyTap, SDLK_ESCAPE};
        case ControllerUiAction::PagePrevious:
            return {ControllerUiDispatchKind::KeyTap, SDLK_PAGEUP};
        case ControllerUiAction::PageNext:
            return {ControllerUiDispatchKind::KeyTap, SDLK_PAGEDOWN};
        case ControllerUiAction::PreviousTab:
            return {ControllerUiDispatchKind::PreviousTab, SDLK_UNKNOWN};
        case ControllerUiAction::NextTab:
            return {ControllerUiDispatchKind::NextTab, SDLK_UNKNOWN};
        case ControllerUiAction::Preview:
            return {ControllerUiDispatchKind::Preview, SDLK_UNKNOWN};
        case ControllerUiAction::Delete:
            return {ControllerUiDispatchKind::KeyTap, SDLK_DELETE};
        default:
            return {};
    }
}

ControllerUiDispatch ControllerInGameUiLayout::Map(ControllerUiAction action) const
{
    switch (action)
    {
        case ControllerUiAction::Pause:
            return {ControllerUiDispatchKind::Pause, SDLK_UNKNOWN};
        default:
            return {};
    }
}

bool ControllerUiRepeater::ShouldEmitDirection(ControllerUiAction action, bool pressed, Foundation::UITime now)
{
    if (!IsDirection(action))
        return false;

    RepeatState& state = StateFor(action);
    if (!pressed)
    {
        state = {};
        return false;
    }

    if (!state.held)
    {
        state.held = true;
        state.repeating = false;
        state.next = now + kFirstRepeatDelay;
        return true;
    }

    if (now < state.next)
        return false;

    state.repeating = true;
    state.next = now + kRepeatDelay;
    return true;
}

void ControllerUiRepeater::ResetDirection(ControllerUiAction action)
{
    if (IsDirection(action))
        StateFor(action) = {};
}

void ControllerUiRepeater::ResetDirections()
{
    up_ = {};
    down_ = {};
    left_ = {};
    right_ = {};
}

ControllerUiRepeater::RepeatState& ControllerUiRepeater::StateFor(ControllerUiAction action)
{
    switch (action)
    {
        case ControllerUiAction::NavigateUp:
            return up_;
        case ControllerUiAction::NavigateDown:
            return down_;
        case ControllerUiAction::NavigateLeft:
            return left_;
        case ControllerUiAction::NavigateRight:
            return right_;
        default:
            return up_;
    }
}

bool ControllerUiRepeater::IsDirection(ControllerUiAction action)
{
    return action == ControllerUiAction::NavigateUp || action == ControllerUiAction::NavigateDown ||
           action == ControllerUiAction::NavigateLeft || action == ControllerUiAction::NavigateRight;
}

} // namespace Poseidon
