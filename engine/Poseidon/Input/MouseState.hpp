#pragma once

#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/MouseTuning.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>

namespace Poseidon
{

// Per-device mouse state — owns all mouse fields, buffer, and processing.
// Cursor position and aim delta are NOT owned here — they're shared across
// mouse and gamepad (both contribute). Passed to Update() as CursorAccum&.
struct MouseState
{
    // Button state (analog pressure 0..1 for each button)
    float buttons[N_MOUSE_BUTTONS] = {};
    bool buttonsToDo[N_MOUSE_BUTTONS] = {};
    bool buttonsDoubleToDo[N_MOUSE_BUTTONS] = {};
    bool buttonsDoubleActive[N_MOUSE_BUTTONS] = {};

    // Per-frame motion delta (sensitivity pre-applied)
    float deltaX = 0, deltaY = 0, deltaZ = 0;

    // Raw delta before sensitivity scaling (useful for mode-specific sensitivity)
    float rawDeltaX = 0, rawDeltaY = 0;

    // Convenience button booleans
    bool left = false, right = false, middle = false;
    bool leftToDo = false, rightToDo = false, middleToDo = false;
    bool doubleClickToDo = false;

    // Sensitivity
    float sensitivityX = 1.0f;
    float sensitivityY = 1.0f;

    // Config
    bool reverseY = false;
    bool buttonsReversed = false;

    // Live feel tuning (DPI normalize, smoothing, accel, menu cursor scale,
    // master baseScale).  Defaults reproduce classic behavior exactly.
    MouseTuning tuning;

    // Activity timestamps
    Foundation::UITime cursorLastActive = UITIME_MIN;
    Foundation::UITime turnLastActive = UITIME_MIN;

    // Buffering (called from the SDL event thread)

    void BufferButton(int btn, bool down);
    void BufferMotion(float dx, float dy);
    void BufferWheel(float dy);
    void DiscardBuffered();

    // Cursor clamping rectangle (aspect-corrected screen bounds)
    struct CursorClamp
    {
        float minX, maxX, minY, maxY;
    };

    // Shared cursor + aim accumulator — both mouse and gamepad write here.
    struct CursorAccum
    {
        float cursorX = 0, cursorY = 0;
        float aimDeltaX = 0, aimDeltaY = 0, aimDeltaZ = 0;
    };

    // Process buffered input into state. Writes cursor/aim to shared accumulator.
    // Returns true if mouse turn was active AND lookAround was enabled
    // (caller should mark keyboard turn active in that case — cross-device concern).
    bool Update(CursorAccum& cursor, int gameFocusLost, bool lookAroundEnabled,
                Foundation::UITime currentTime, const CursorClamp* clamp);

    // Flush buffered input and reset per-frame deltas.
    void FlushAndReset();

    // Test-only: inject relative motion into the per-frame buffer.
    // Update() then reads it like any other SDL motion event would.
    // Production code does not call this; the tri integration tests
    // use it to drive viewer controls without an SDL window.
    void TestInjectMotion(float dx, float dy)
    {
        bufDeltaX_ += dx;
        bufDeltaY_ += dy;
    }

    // Test-only: hold a mouse button down (or release) without going
    // through the SDL event buffer.  Writes both `buttons[btn]` (the
    // backing state Update() reads) and the `left/right/middle`
    // convenience flags so triMouseLeft / triMouseRight / triMouseMid
    // survive the next frame's Update() copy-back.
    void TestSetButton(int btn, bool down)
    {
        if (btn >= 0 && btn < N_MOUSE_BUTTONS)
            buttons[btn] = down ? 1.0f : 0.0f;
        if (btn == 0) left = down;
        else if (btn == 1) right = down;
        else if (btn == 2) middle = down;
    }

  private:
    static constexpr int kButtonBufferSize = 16;
    static constexpr int kDoubleClickWindowMs = 400;
    static constexpr float kCursorScaleX = 1.0f / 200.0f;
    static constexpr float kCursorScaleY = 1.0f / 150.0f;
    static constexpr float kCursorLimitX = 0.6f;
    static constexpr float kCursorLimitY = 0.4f;
    static constexpr float kWheelToCursorScale = 0.01f;
    // Downstream game logic was written expecting mouse-wheel deltas in 120-units-per-notch
    // steps (the Windows WHEEL_DELTA convention). SDL3 reports 1.0 per notch; scale to match.
    static constexpr float kWheelUnitsPerNotch = 120.0f;
    static constexpr float kActivityThreshold = 0.001f;
    struct ButtonEvent
    {
        int btn;
        bool down;
    };
    ButtonEvent btnBuffer_[kButtonBufferSize] = {};
    int btnCount_ = 0;
    int buttonLastPressedMs_[N_MOUSE_BUTTONS] = {};
    float bufDeltaX_ = 0, bufDeltaY_ = 0;
    float bufWheel_ = 0;
    // Smoothing low-pass state (only used when tuning.smoothing > 0).
    float smoothX_ = 0, smoothY_ = 0;
};
} // namespace Poseidon

