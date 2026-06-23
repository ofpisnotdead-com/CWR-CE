#include <Poseidon/Input/MouseState.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>

namespace Poseidon
{

using Poseidon::Foundation::UITime;

void MouseState::BufferButton(int btn, bool down)
{
    if (btnCount_ >= kButtonBufferSize)
        return;
    btnBuffer_[btnCount_++] = {btn, down};
}

void MouseState::BufferMotion(float dx, float dy)
{
    bufDeltaX_ += dx;
    bufDeltaY_ += dy;
}

void MouseState::BufferWheel(float dy)
{
    bufWheel_ += dy * kWheelUnitsPerNotch;
}

void MouseState::DiscardBuffered()
{
    btnCount_ = 0;
    bufDeltaX_ = bufDeltaY_ = bufWheel_ = 0;
    smoothX_ = smoothY_ = 0;
}

bool MouseState::Update(CursorAccum& cursor, int gameFocusLost, bool lookAroundEnabled, UITime currentTime,
                        const CursorClamp* clamp)
{
    // Reset per-frame edge state
    leftToDo = false;
    rightToDo = false;
    middleToDo = false;
    doubleClickToDo = false;

    for (int i = 0; i < N_MOUSE_BUTTONS; i++)
    {
        buttonsToDo[i] = false;
        buttonsDoubleToDo[i] = false;
    }

    deltaX = 0;
    deltaY = 0;
    deltaZ = 0;
    rawDeltaX = 0;
    rawDeltaY = 0;

    // Process buffered button events
    for (int i = 0; i < btnCount_; i++)
    {
        int btn = btnBuffer_[i].btn;
        if (buttonsReversed && btn < 2)
            btn = 1 - btn;

        if (btn >= 0 && btn < N_MOUSE_BUTTONS)
        {
            if (btnBuffer_[i].down)
            {
                buttons[btn] = true;
                buttonsToDo[btn] = true;
                const int nowMs = currentTime.toInt();
                if (buttonLastPressedMs_[btn] != 0 && nowMs - buttonLastPressedMs_[btn] <= kDoubleClickWindowMs)
                {
                    buttonsDoubleToDo[btn] = true;
                    buttonsDoubleActive[btn] = true;
                    doubleClickToDo = true;
                }
                buttonLastPressedMs_[btn] = nowMs;
            }
            else
            {
                buttons[btn] = false;
                buttonsDoubleActive[btn] = false;
            }
        }
    }
    btnCount_ = 0;

    rawDeltaX = bufDeltaX_;
    rawDeltaY = bufDeltaY_;

    // Tuning pipeline, each stage a no-op at default tuning: smoothing → DPI → accel → sens·baseScale.
    float inX = bufDeltaX_;
    float inY = bufDeltaY_;

    if (tuning.smoothing > 0.0f)
    {
        const float alpha = 1.0f - tuning.smoothing;
        smoothX_ += (inX - smoothX_) * alpha;
        smoothY_ += (inY - smoothY_) * alpha;
        inX = smoothX_;
        inY = smoothY_;
    }
    else
    {
        smoothX_ = inX; // keep state coherent if smoothing is enabled later
        smoothY_ = inY;
    }

    const float dpi = tuning.DpiFactor();

    if (tuning.acceleration && tuning.accelExponent > 1.0f)
    {
        const float speed = std::sqrt(inX * inX + inY * inY);
        const float mult = std::pow(1.0f + speed, tuning.accelExponent - 1.0f);
        inX *= mult;
        inY *= mult;
    }

    deltaX = inX * sensitivityX * tuning.baseScale * dpi;
    deltaY = inY * sensitivityY * tuning.baseScale * dpi;
    deltaZ = bufWheel_;
    bufDeltaX_ = bufDeltaY_ = bufWheel_ = 0;

    // Update convenience booleans
    left = buttons[0] > 0;
    right = buttons[1] > 0;
    middle = buttons[2] > 0;
    leftToDo = buttonsToDo[0];
    rightToDo = buttonsToDo[1];
    middleToDo = buttonsToDo[2];

    // Cursor movement (matches Windows logic)
    float moveX = deltaX * kCursorScaleX;
    float moveY = deltaY * kCursorScaleY;

    saturate(moveX, -kCursorLimitX, +kCursorLimitX);
    saturate(moveY, -kCursorLimitY, +kCursorLimitY);

    if (gameFocusLost <= 0)
    {
        cursor.aimDeltaX += moveX;
        if (reverseY)
            cursor.aimDeltaY -= moveY;
        else
            cursor.aimDeltaY += moveY;
    }
    // The menu cursor can be scaled independently of look (menuCursorScale 1.0
    // == classic: cursor and aim move together).
    cursor.cursorX += moveX * tuning.menuCursorScale;
    cursor.cursorY += moveY * tuning.menuCursorScale;

    cursor.aimDeltaZ += kWheelToCursorScale * deltaZ;

    if (clamp)
    {
        saturate(cursor.cursorX, clamp->minX, clamp->maxX);
        saturate(cursor.cursorY, clamp->minY, clamp->maxY);
    }

    // Activity detection
    bool turnActiveThisFrame = false;
    if ((left | right | middle) || deltaX != 0.0f || deltaY != 0.0f || deltaZ != 0.0f ||
        fabs(cursor.aimDeltaY) + fabs(cursor.aimDeltaX) > kActivityThreshold)
    {
        cursorLastActive = currentTime;
    }
    if (deltaX != 0.0f || deltaY != 0.0f || fabs(cursor.aimDeltaY) + fabs(cursor.aimDeltaX) > kActivityThreshold)
    {
        if (!lookAroundEnabled)
            turnLastActive = currentTime;
        else
            turnActiveThisFrame = true; // caller should mark keyboard turn active
    }

    return turnActiveThisFrame;
}

void MouseState::FlushAndReset()
{
    deltaX = 0;
    deltaY = 0;
    deltaZ = 0;
    rawDeltaX = 0;
    rawDeltaY = 0;
}
} // namespace Poseidon
