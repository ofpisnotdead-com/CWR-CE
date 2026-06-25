#pragma once

#include <Poseidon/Input/ControlsCategory.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/MouseTuning.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <SDL3/SDL_scancode.h>
#include <array>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{

struct Input;
struct UserActionDesc;

// InputSubsystem — singleton coordinator for all input.
class InputSubsystem
{
  public:
    static InputSubsystem& Instance();

    // Per-frame update — computes movement state from actions
    void Update();

    // Context management
    void SetContext(InputContext ctx);
    InputContext GetContext() const { return context_; }

    // Action queries
    float GetAction(UserAction action, bool checkFocus = true) const;
    float GetAction(InputContext ctx, UserAction action, bool checkFocus = true) const;
    bool GetActionToDo(UserAction action, bool reset = true, bool checkFocus = true);

    // Computed movement state (set by Update())
    float GetMoveForward() const { return moveForward_; }
    float GetMoveFastForward() const { return moveFastForward_; }
    float GetMoveSlowForward() const { return moveSlowForward_; }
    float GetMoveBack() const { return moveBack_; }
    float GetMoveLeft() const { return moveLeft_; }
    float GetMoveRight() const { return moveRight_; }
    float GetMoveUp() const { return moveUp_; }
    float GetMoveDown() const { return moveDown_; }
    float GetTurnLeft() const { return turnLeft_; }
    float GetTurnRight() const { return turnRight_; }
    bool IsFiring() const { return fire_; }
    bool IsFirePressed() const { return fireToDo_; }
    bool IsLookAroundEnabled() const { return lookAroundEnabled_; }
    bool IsLookAroundToggled() const { return lookAroundToggled_; }
    bool FreelookChanged() const { return freelookChanged_; }
    void ResetLookAroundToggle();

    // Activity detection
    bool IsMouseTurnActive() const;
    bool IsMouseCursorActive() const;
    bool IsJoystickActive() const;
    bool IsJoystickThrustActive() const;
    bool IsJoystickEnabled() const;
    bool IsKeyboardCursorMoreRecent() const;  // keyboard cursor more recent than mouse
    bool IsMouseCursorRecentlyActive() const; // mouse cursor active within last tick

    // Activity timestamp marking
    void MarkKeyboardMoveActive();
    void MarkKeyboardTurnActive();
    void MarkKeyboardCursorActive();
    void MarkKeyboardThrustActive();
    void MarkJoystickMoveActive();
    void MarkJoystickThrustActive();
    bool IsActionBoundToRecentAxis(UserAction action) const;

    // Joystick analog axes
    float GetStickForward() const;
    float GetStickLeft() const;
    float GetStickThrust() const;
    float GetStickRudder() const;

    // Focus management
    void ChangeGameFocus(int add);

    // Fire/mouse with focus check
    bool GetFire(bool checkFocus = true) const;
    bool GetFireToDo(bool reset = true, bool checkFocus = true);
    bool GetMouseL(bool checkFocus = true);
    bool GetMouseR(bool checkFocus = true);
    bool GetMouseLToDo(bool reset = true, bool checkFocus = true);
    bool GetMouseRToDo(bool reset = true, bool checkFocus = true);

    // Mouse button edge detection (raw, no focus check)
    bool IsMouseLeftPressed() const;
    bool IsMouseRightPressed() const;

    // Cursor delta consumption (read + clear)
    float ConsumeCursorDeltaX();
    float ConsumeCursorDeltaY();
    float ConsumeCursorScroll();

    // Cursor position reset
    void ResetCursorPosition();
    void FlushAndResetMouse();

    // Packed key queries (INPUT_DEVICE_* | code format)
    float GetKey(int packedKey, bool checkFocus = true) const;
    bool GetKeyToDo(int packedKey, bool reset = true, bool checkFocus = true);

    // Cheat system state
    int CheatActivated() const;
    void CheatServed();
    void ForgetKeys();

#if _ENABLE_CHEATS
    float GetCheat1(SDL_Scancode sc) const;
    float GetCheat2(SDL_Scancode sc) const;
#endif

    // Raw key state
    bool IsKeyDown(SDL_Scancode sc) const;
    bool IsKeyPressed(SDL_Scancode sc) const; // edge (ToDo)
    float GetKeyValue(SDL_Scancode sc) const; // analog (time-weighted 0..1)
    bool ConsumeKeyPress(SDL_Scancode sc);    // read + clear edge
    // Like ConsumeKeyPress, but a no-op when AppConfig::DevMode() is off.
    // Use for hotkeys gated behind --dev (font tuner, view dump, slot picker)
    // so call sites stay one-line.  When dev mode is off, the edge is still
    // cleared so the press doesn't leak into a later frame's handler.
    bool ConsumeDevKeyPress(SDL_Scancode sc);

#if _ENABLE_CHEATS
    bool GetCheat1ToDo(SDL_Scancode sc, bool reset = true);
    bool GetCheat2ToDo(SDL_Scancode sc, bool reset = true);
#endif

    // Mouse buttons
    bool IsMouseButtonDown(int button) const;
    bool IsMouseLeftDown() const;
    bool IsMouseRightDown() const;
    bool IsMouseMiddleDown() const;

    // Mouse movement delta (per-frame, sensitivity pre-applied)
    float GetMouseDeltaX() const;
    float GetMouseDeltaY() const;
    float GetMouseWheel() const;

    // Raw mouse delta before sensitivity scaling
    float GetMouseRawDeltaX() const;
    float GetMouseRawDeltaY() const;

    // UI cursor position (-1..+1)
    float GetCursorX() const;
    float GetCursorY() const;

    // Gamepad
    float GetGamepadAxis(int axis) const;
    bool IsGamepadButtonDown(int button) const;
    bool IsGamepadButtonPressed(int button) const; // edge

    // Initialize all context profiles with default bindings
    void LoadDefaultProfiles();

    // Config loading/saving
    void LoadKeys();
    void SaveKeys();

    // Action descriptions
    static UserActionDesc* GetUserActionDesc();

    // Config settings
    bool IsReverseMouse() const;
    void SetReverseMouse(bool v);
    void ToggleReverseMouse();
    void SetJoystickEnabled(bool v);
    void ToggleJoystickEnabled();
    bool IsReverseJoystick() const;
    void SetReverseJoystick(bool v);
    void ToggleReverseJoystick();
    bool IsMouseButtonsReversed() const;
    void SetMouseButtonsReversed(bool v);
    void ToggleMouseButtonsReversed();
    float GetMouseSensitivityX() const;
    float GetMouseSensitivityY() const;
    void SetMouseSensitivityX(float v);
    void SetMouseSensitivityY(float v);

    // Live mouse-feel tuning read by MouseState each frame; the dev Mouse tab edits it.
    MouseTuning& GetMouseTuning();
    const MouseTuning& GetMouseTuning() const;

    // Key binding access
    const AutoArray<int>& GetUserKeys(UserAction action) const;
    void SetUserKeys(UserAction action, const AutoArray<int>& keys);
    void CompactUserKeys(UserAction action);

    // Modifier access — parallel array to userKeys; -1 = no modifier.
    // Same length as userKeys[action]; SetUserKeyModifiers caller is
    // responsible for keeping lengths in sync.
    const AutoArray<int>& GetUserKeyModifiers(UserAction action) const;
    void SetUserKeyModifiers(UserAction action, const AutoArray<int>& modifiers);

    // Conflict scan: returns the first UserAction in the active context
    // profile whose binding list already contains (packedCode, modifier),
    // ignoring (excludeAction, excludeSlot).  Two bindings collide only if
    // both code AND modifier match, so "Ctrl+W" doesn't conflict with bare
    // "W".  Returns UAN if no conflict.  Pass excludeSlot = -1 to scan
    // every slot of excludeAction too.  modifier = -1 means "no modifier".
    UserAction FindBindingConflict(int packedCode,
                                   UserAction excludeAction = UAN,
                                   int excludeSlot = -1,
                                   int modifier = -1) const;

    // Restore the engine defaults (UserActionDesc[i].keys) for every
    // action in the given Controls UI category.  Other categories'
    // bindings are left untouched.  Caller persists via SaveKeys().
    void ResetCategoryDefaults(ControlsCategory cat);

    // Binding detection — hardware polling for rebind UI
    bool GetMouseButtonToDo(int i) const;
    bool GetStickButtonToDo(int i) const;
    bool GetStickPovToDo(int i) const;
    bool ConsumeAxisBigActive(int i);

    // Synthetic gamepad input used by harness tests (triGpadButton / triGpadPov).
    // Separate from GInput.gamepad.stickButtonsToDo so per-frame ProcessJoystick
    // doesn't wipe the flag before the consumer reads it.  Consumers read via
    // ConsumeSyntheticStick*, which returns the flag and clears it atomically.
    void SetSyntheticStickButton(int i, bool value);
    void SetSyntheticStickPov(int i, bool value);
    void SetSyntheticLeftStick(float x, float y);
    bool ConsumeSyntheticStickButton(int i);
    bool ConsumeSyntheticStickPov(int i);
    bool GetSyntheticLeftStick(float& x, float& y) const;

    // Profile management
    InputProfile& GetProfile(InputContext ctx);
    const InputProfile& GetProfile(InputContext ctx) const;

  private:
    InputSubsystem();
    ~InputSubsystem() = default;
    InputSubsystem(const InputSubsystem&) = delete;
    InputSubsystem& operator=(const InputSubsystem&) = delete;

    void ComputeMovementState();
    void SyncToGInput();

    InputContext context_ = InputContext::Menu;

    // Computed movement state
    float moveForward_ = 0, moveFastForward_ = 0, moveSlowForward_ = 0, moveBack_ = 0;
    float moveLeft_ = 0, moveRight_ = 0, moveUp_ = 0, moveDown_ = 0;
    float turnLeft_ = 0, turnRight_ = 0;
    bool fire_ = false, fireToDo_ = false;
    bool lookAroundEnabled_ = false, lookAroundToggled_ = false;
    bool freelookChanged_ = false;

    static constexpr int kNumContexts = static_cast<int>(InputContext::Count);
    std::array<InputProfile, kNumContexts> profiles_;
    std::array<std::array<bool, UAN>, kNumContexts> actionDoneByContext_ = {};

    // Synthetic harness input (see above).  N_JOYSTICK_BUTTONS and
    // N_JOYSTICK_POV are in GamepadState.hpp / KeyInput.hpp which are
    // included by the .cpp, so keep raw sizes here.
    bool syntheticStickButtons_[12] = {};
    bool syntheticStickPov_[8] = {};
    float syntheticLeftStickX_ = 0.0f;
    float syntheticLeftStickY_ = 0.0f;
};
} // namespace Poseidon

