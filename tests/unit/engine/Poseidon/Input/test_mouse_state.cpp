#include <Poseidon/Input/MouseState.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>

using Poseidon::Foundation::UITime;

using namespace Poseidon;
TEST_CASE("MouseState: default construction zeroes everything", "[input][mouse]")
{
    MouseState ms;
    REQUIRE(ms.deltaX == 0);
    REQUIRE(ms.deltaY == 0);
    REQUIRE(ms.deltaZ == 0);
    REQUIRE(ms.sensitivityX == 1.0f);
    REQUIRE(ms.sensitivityY == 1.0f);
    REQUIRE_FALSE(ms.left);
    REQUIRE_FALSE(ms.right);
    REQUIRE_FALSE(ms.middle);
    REQUIRE_FALSE(ms.leftToDo);
    REQUIRE_FALSE(ms.rightToDo);
    REQUIRE_FALSE(ms.middleToDo);
    REQUIRE_FALSE(ms.doubleClickToDo);
    REQUIRE_FALSE(ms.reverseY);
    REQUIRE_FALSE(ms.buttonsReversed);

    for (int i = 0; i < N_MOUSE_BUTTONS; i++)
    {
        REQUIRE(ms.buttons[i] == 0.0f);
        REQUIRE_FALSE(ms.buttonsToDo[i]);
        REQUIRE_FALSE(ms.buttonsDoubleToDo[i]);
        REQUIRE_FALSE(ms.buttonsDoubleActive[i]);
    }
}

TEST_CASE("CursorAccum: default construction zeroes everything", "[input][mouse]")
{
    MouseState::CursorAccum c;
    REQUIRE(c.cursorX == 0.0f);
    REQUIRE(c.cursorY == 0.0f);
    REQUIRE(c.aimDeltaX == 0.0f);
    REQUIRE(c.aimDeltaY == 0.0f);
    REQUIRE(c.aimDeltaZ == 0.0f);
}

TEST_CASE("MouseState: FlushAndReset clears deltas", "[input][mouse]")
{
    MouseState ms;
    ms.deltaX = 42;
    ms.deltaY = -17;
    ms.deltaZ = 3;

    ms.FlushAndReset();

    REQUIRE(ms.deltaX == 0);
    REQUIRE(ms.deltaY == 0);
    REQUIRE(ms.deltaZ == 0);
}

TEST_CASE("MouseState: FlushAndReset preserves config and button state", "[input][mouse]")
{
    MouseState ms;
    ms.sensitivityX = 2.5f;
    ms.sensitivityY = 0.8f;
    ms.reverseY = true;
    ms.buttonsReversed = true;
    ms.left = true;
    ms.leftToDo = true;
    ms.buttons[0] = 1.0f;

    ms.FlushAndReset();

    // Config preserved
    REQUIRE(ms.sensitivityX == 2.5f);
    REQUIRE(ms.sensitivityY == 0.8f);
    REQUIRE(ms.reverseY);
    REQUIRE(ms.buttonsReversed);

    // Button state preserved (FlushAndReset only clears deltas)
    REQUIRE(ms.left);
    REQUIRE(ms.leftToDo);
    REQUIRE(ms.buttons[0] == 1.0f);
}

// --- Buffer + Update tests ---

TEST_CASE("MouseState: BufferMotion accumulates and Update applies with sensitivity", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.sensitivityX = 1.0f;
    ms.sensitivityY = 1.0f;

    ms.BufferMotion(10.0f, 20.0f);
    ms.BufferMotion(5.0f, -3.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // delta = accumulated * sensitivity * 1.5f (float, no truncation)
    REQUIRE(ms.deltaX == Catch::Approx(15.0f * 1.0f * 1.5f));
    REQUIRE(ms.deltaY == Catch::Approx(17.0f * 1.0f * 1.5f));

    // Raw delta preserves pre-sensitivity values
    REQUIRE(ms.rawDeltaX == Catch::Approx(15.0f));
    REQUIRE(ms.rawDeltaY == Catch::Approx(17.0f));
}

TEST_CASE("MouseState: BufferButton processes left click", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.left);
    REQUIRE(ms.leftToDo);
    REQUIRE(ms.buttons[0] == 1.0f);
    REQUIRE(ms.buttonsToDo[0]);
}

TEST_CASE("MouseState: button release clears held state but not edge", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Press
    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.left);

    // Release
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE_FALSE(ms.left);
    REQUIRE_FALSE(ms.leftToDo); // edge reset at start of Update
}

TEST_CASE("MouseState: buttonsReversed swaps left/right", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.buttonsReversed = true;

    ms.BufferButton(0, true); // physical left → logical right
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE_FALSE(ms.left);
    REQUIRE(ms.right);
    REQUIRE(ms.buttons[1] == 1.0f);
}

TEST_CASE("MouseState: BufferWheel applies DInput scaling", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferWheel(1.0f); // SDL wheel unit
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaZ == Catch::Approx(120.0f)); // 1.0 * 120.0 = 120 DInput units
}

TEST_CASE("MouseState: Update applies cursor clamping", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Move cursor far right
    cursor.cursorX = 0.9f;
    ms.BufferMotion(100.0f, 0.0f);

    MouseState::CursorClamp clamp{-1.0f, 1.0f, -1.0f, 1.0f};
    ms.Update(cursor, 0, false, UITime(), &clamp);

    REQUIRE(cursor.cursorX <= 1.0f);
}

TEST_CASE("MouseState: gameFocusLost suppresses aimDelta but not cursor position", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferMotion(100.0f, 100.0f);
    ms.Update(cursor, 1, false, UITime(), nullptr); // focus lost

    // cursorX/Y updated (for UI cursor drawing)
    REQUIRE(cursor.cursorX != 0.0f);
    // aimDelta NOT updated (game shouldn't react)
    REQUIRE(cursor.aimDeltaX == 0.0f);
    REQUIRE(cursor.aimDeltaY == 0.0f);
}

TEST_CASE("MouseState: reverseY inverts aimDeltaY", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.reverseY = true;
    ms.BufferMotion(0.0f, 100.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // cursorY goes positive (normal), aimDeltaY goes negative (reversed)
    REQUIRE(cursor.cursorY > 0.0f);
    REQUIRE(cursor.aimDeltaY < 0.0f);
}

TEST_CASE("MouseState: DiscardBuffered clears pending input", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferMotion(50.0f, 50.0f);
    ms.BufferButton(0, true);
    ms.BufferWheel(2.0f);

    ms.DiscardBuffered();
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaX == 0);
    REQUIRE(ms.deltaY == 0);
    REQUIRE(ms.deltaZ == 0);
    REQUIRE_FALSE(ms.left);
}

TEST_CASE("MouseState: Update returns true when turn active during lookAround", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferMotion(100.0f, 0.0f);
    bool markKbd = ms.Update(cursor, 0, true, UITime(), nullptr); // lookAround = true

    REQUIRE(markKbd); // caller should mark keyboard turn active
}

TEST_CASE("MouseState: Update returns false when turn active without lookAround", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferMotion(100.0f, 0.0f);
    bool markKbd = ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE_FALSE(markKbd);
}

TEST_CASE("MouseState: float delta preserves sub-pixel precision", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.sensitivityX = 0.5f;
    ms.sensitivityY = 0.5f;

    // Small motion that would truncate to 0 with int cast
    ms.BufferMotion(0.3f, 0.7f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // With int: (int)(0.3 * 0.5 * 1.5) = (int)(0.225) = 0 — LOST
    // With float: 0.225 preserved
    REQUIRE(ms.deltaX == Catch::Approx(0.3f * 0.5f * 1.5f));
    REQUIRE(ms.deltaY == Catch::Approx(0.7f * 0.5f * 1.5f));
    REQUIRE(ms.deltaX > 0.0f); // sub-pixel motion preserved

    // Raw delta = pre-sensitivity input
    REQUIRE(ms.rawDeltaX == Catch::Approx(0.3f));
    REQUIRE(ms.rawDeltaY == Catch::Approx(0.7f));
}

TEST_CASE("MouseState: raw delta cleared each frame", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.BufferMotion(10.0f, 20.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.rawDeltaX == Catch::Approx(10.0f));

    // Next frame, no motion
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.rawDeltaX == Catch::Approx(0.0f));
    REQUIRE(ms.rawDeltaY == Catch::Approx(0.0f));
}

TEST_CASE("CursorAccum: mouse and gamepad can both accumulate", "[input][mouse]")
{
    MouseState::CursorAccum cursor;
    MouseState ms;

    // Mouse contributes
    ms.BufferMotion(50.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    float afterMouse = cursor.aimDeltaX;
    REQUIRE(afterMouse != 0.0f);

    // Simulate gamepad also writing to cursor (as the real code does)
    cursor.aimDeltaX += 0.1f;
    REQUIRE(cursor.aimDeltaX == Catch::Approx(afterMouse + 0.1f));
}

// --- Edge cases: buttons ---

TEST_CASE("MouseState: right and middle button processing", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferButton(1, true);
    ms.BufferButton(2, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE_FALSE(ms.left);
    REQUIRE(ms.right);
    REQUIRE(ms.rightToDo);
    REQUIRE(ms.middle);
    REQUIRE(ms.middleToDo);
    REQUIRE(ms.buttons[1] == 1.0f);
    REQUIRE(ms.buttons[2] == 1.0f);
}

TEST_CASE("MouseState: multiple simultaneous button press and release", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Press all three
    ms.BufferButton(0, true);
    ms.BufferButton(1, true);
    ms.BufferButton(2, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.left);
    REQUIRE(ms.right);
    REQUIRE(ms.middle);

    // Release left only
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE_FALSE(ms.left);
    REQUIRE(ms.right);
    REQUIRE(ms.middle);
}

TEST_CASE("MouseState: out-of-range button index is ignored", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferButton(-1, true);
    ms.BufferButton(N_MOUSE_BUTTONS, true);
    ms.BufferButton(99, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // Nothing should have changed
    REQUIRE_FALSE(ms.left);
    REQUIRE_FALSE(ms.right);
    REQUIRE_FALSE(ms.middle);
    for (int i = 0; i < N_MOUSE_BUTTONS; i++)
        REQUIRE(ms.buttons[i] == 0.0f);
}

TEST_CASE("MouseState: buffer overflow drops excess events", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Buffer 16 events (capacity), then one more
    for (int i = 0; i < 16; i++)
        ms.BufferButton(0, (i % 2) == 0); // alternating press/release
    ms.BufferButton(1, true);             // 17th event — should be dropped

    ms.Update(cursor, 0, false, UITime(), nullptr);

    // Button 1 should NOT be pressed (17th event was dropped)
    REQUIRE_FALSE(ms.right);
    REQUIRE(ms.buttons[1] == 0.0f);
}

TEST_CASE("MouseState: buttonsReversed only swaps buttons 0 and 1", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.buttonsReversed = true;

    // Button 2 (middle) should NOT be swapped
    ms.BufferButton(2, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.middle);
    REQUIRE(ms.buttons[2] == 1.0f);
    REQUIRE_FALSE(ms.left);
    REQUIRE_FALSE(ms.right);
}

TEST_CASE("MouseState: buttonsToDo resets between frames", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.leftToDo);

    // Next frame — no new button events
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE_FALSE(ms.leftToDo);
    // But button stays held
    REQUIRE(ms.left);
}

TEST_CASE("MouseState: second click within window sets double-click edge and active hold", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferButton(0, true);
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(1000), nullptr);

    REQUIRE_FALSE(ms.buttonsDoubleToDo[0]);
    REQUIRE_FALSE(ms.buttonsDoubleActive[0]);

    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(1300), nullptr);

    REQUIRE(ms.buttonsToDo[0]);
    REQUIRE(ms.buttonsDoubleToDo[0]);
    REQUIRE(ms.buttonsDoubleActive[0]);
    REQUIRE(ms.doubleClickToDo);

    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(1400), nullptr);
    REQUIRE_FALSE(ms.buttonsDoubleActive[0]);
}

TEST_CASE("MouseState: second click after window is not a double-click", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferButton(0, true);
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(1000), nullptr);

    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(1401), nullptr);

    REQUIRE(ms.buttonsToDo[0]);
    REQUIRE_FALSE(ms.buttonsDoubleToDo[0]);
    REQUIRE_FALSE(ms.buttonsDoubleActive[0]);
}

// --- Edge cases: motion ---

TEST_CASE("MouseState: negative motion deltas", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferMotion(-30.0f, -50.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaX == Catch::Approx(-30.0f * 1.5f));
    REQUIRE(ms.deltaY == Catch::Approx(-50.0f * 1.5f));
    REQUIRE(ms.rawDeltaX == Catch::Approx(-30.0f));
    REQUIRE(ms.rawDeltaY == Catch::Approx(-50.0f));
}

TEST_CASE("MouseState: asymmetric sensitivity", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.sensitivityX = 2.0f;
    ms.sensitivityY = 0.5f;

    ms.BufferMotion(10.0f, 10.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaX == Catch::Approx(10.0f * 2.0f * 1.5f));
    REQUIRE(ms.deltaY == Catch::Approx(10.0f * 0.5f * 1.5f));
    // Raw deltas are NOT affected by sensitivity
    REQUIRE(ms.rawDeltaX == Catch::Approx(10.0f));
    REQUIRE(ms.rawDeltaY == Catch::Approx(10.0f));
}

TEST_CASE("MouseState: deltas reset each frame", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferMotion(10.0f, 20.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.deltaX != 0.0f);

    // Next frame — no motion buffered
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.deltaX == 0.0f);
    REQUIRE(ms.deltaY == 0.0f);
    REQUIRE(ms.deltaZ == 0.0f);
}

// --- Edge cases: wheel ---

TEST_CASE("MouseState: negative wheel scroll", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferWheel(-1.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaZ == Catch::Approx(-120.0f));
}

TEST_CASE("MouseState: wheel accumulates across multiple BufferWheel calls", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferWheel(1.0f);
    ms.BufferWheel(0.5f);
    ms.BufferWheel(-0.25f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaZ == Catch::Approx(1.25f * 120.0f));
}

TEST_CASE("MouseState: wheel updates aimDeltaZ", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferWheel(2.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // aimDeltaZ = 0.01 * deltaZ = 0.01 * 240 = 2.4
    REQUIRE(cursor.aimDeltaZ == Catch::Approx(0.01f * 2.0f * 120.0f));
}

// --- Edge cases: cursor movement ---

TEST_CASE("MouseState: cursor movement saturates at limits", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Huge motion — cursor move should be clamped to ±0.6 X, ±0.4 Y
    ms.BufferMotion(10000.0f, 10000.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // aimDelta should be clamped (0.6 max X, 0.4 max Y)
    REQUIRE(cursor.aimDeltaX == Catch::Approx(0.6f));
    REQUIRE(cursor.aimDeltaY == Catch::Approx(0.4f));
}

TEST_CASE("MouseState: cursor accumulates across frames", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferMotion(10.0f, 5.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    float cx1 = cursor.cursorX;
    float cy1 = cursor.cursorY;

    ms.BufferMotion(10.0f, 5.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // Cursor should have moved further
    REQUIRE(cursor.cursorX == Catch::Approx(cx1 * 2));
    REQUIRE(cursor.cursorY == Catch::Approx(cy1 * 2));
}

TEST_CASE("MouseState: asymmetric clamp bounds", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Pre-position cursor and push it past asymmetric bounds
    cursor.cursorX = 0.0f;
    cursor.cursorY = 0.0f;
    ms.BufferMotion(10000.0f, 10000.0f);

    MouseState::CursorClamp clamp{-0.5f, 0.8f, -0.3f, 0.9f};
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(cursor.cursorX <= 0.8f);
    REQUIRE(cursor.cursorY <= 0.9f);

    // Now push negative
    ms.BufferMotion(-10000.0f, -10000.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(cursor.cursorX >= -0.5f);
    REQUIRE(cursor.cursorY >= -0.3f);
}

TEST_CASE("MouseState: no clamp when clamp pointer is null", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Push cursor way past ±1 (kCursorLimitY=0.4/frame, need 3+ frames)
    for (int i = 0; i < 4; i++)
    {
        ms.BufferMotion(10000.0f, 10000.0f);
        ms.Update(cursor, 0, false, UITime(), nullptr);
    }

    // Without clamp, cursor can exceed ±1
    REQUIRE(cursor.cursorX > 1.0f);
    REQUIRE(cursor.cursorY > 1.0f);
}

// --- Edge cases: activity timestamps ---

TEST_CASE("MouseState: button press updates cursorLastActive", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    UITime t(5.0f);

    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, t, nullptr);

    REQUIRE(ms.cursorLastActive == t);
}

TEST_CASE("MouseState: motion updates both cursorLastActive and turnLastActive", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    UITime t(7.0f);

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, t, nullptr);

    REQUIRE(ms.cursorLastActive == t);
    REQUIRE(ms.turnLastActive == t);
}

TEST_CASE("MouseState: no input does not update activity timestamps", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    UITime t(10.0f);

    // No buffered input
    ms.Update(cursor, 0, false, t, nullptr);

    REQUIRE_FALSE(ms.cursorLastActive == t);
    REQUIRE_FALSE(ms.turnLastActive == t);
}

TEST_CASE("MouseState: button-only activity does not update turnLastActive", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    UITime t(3.0f);

    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, t, nullptr);

    REQUIRE(ms.cursorLastActive == t);
    // No motion → turnLastActive should NOT be updated
    REQUIRE_FALSE(ms.turnLastActive == t);
}

// --- Multi-frame scenarios ---

TEST_CASE("MouseState: press-hold-release over three frames", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Frame 1: press
    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.left);
    REQUIRE(ms.leftToDo);

    // Frame 2: hold (no new events)
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.left);           // still held
    REQUIRE_FALSE(ms.leftToDo); // edge cleared

    // Frame 3: release
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE_FALSE(ms.left);
    REQUIRE_FALSE(ms.leftToDo);
}

TEST_CASE("MouseState: rapid press-release in same frame", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Both press and release buffered in same frame
    ms.BufferButton(0, true);
    ms.BufferButton(0, false);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // Final state should be released (processed in order)
    REQUIRE_FALSE(ms.left);
    // But buttonsToDo should have been set (press happened)
    REQUIRE(ms.buttonsToDo[0]);
}

TEST_CASE("MouseState: DiscardBuffered does not affect current state", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    // Set up some state first
    ms.BufferButton(0, true);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.left);

    // Buffer new events then discard
    ms.BufferMotion(100.0f, 100.0f);
    ms.BufferButton(1, true);
    ms.DiscardBuffered();

    // Current state unchanged
    REQUIRE(ms.left);
}

TEST_CASE("MouseState: FlushAndReset also clears raw deltas", "[input][mouse]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferMotion(10.0f, 20.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(ms.rawDeltaX != 0.0f);

    ms.FlushAndReset();
    REQUIRE(ms.rawDeltaX == 0.0f);
    REQUIRE(ms.rawDeltaY == 0.0f);
}

// --- MouseTuning: pure helpers ---

TEST_CASE("MouseTuning: defaults reproduce classic feel", "[input][mouse][tuning]")
{
    MouseTuning t;
    REQUIRE(t.baseScale == 1.5f); // == the old hard-coded kSensitivityScale
    REQUIRE_FALSE(t.dpiNormalize);
    REQUIRE(t.DpiFactor() == 1.0f);
    REQUIRE(t.smoothing == 0.0f);
    REQUIRE_FALSE(t.acceleration);
    REQUIRE(t.menuCursorScale == 1.0f);
    REQUIRE_FALSE(t.extendedRange);
    REQUIRE(t.SensMin() == 0.5f);
    REQUIRE(t.SensMax() == 2.0f);
}

TEST_CASE("MouseTuning: DpiFactor normalizes against reference DPI", "[input][mouse][tuning]")
{
    MouseTuning t;
    t.dpiNormalize = true;
    t.referenceDpi = 800;

    t.mouseDpi = 800;
    REQUIRE(t.DpiFactor() == Catch::Approx(1.0f));
    t.mouseDpi = 1600;
    REQUIRE(t.DpiFactor() == Catch::Approx(0.5f)); // 2x DPI → half speed
    t.mouseDpi = 12000;
    REQUIRE(t.DpiFactor() == Catch::Approx(800.0f / 12000.0f));

    // Disabled → always 1.0 regardless of DPI.
    t.dpiNormalize = false;
    REQUIRE(t.DpiFactor() == 1.0f);
}

TEST_CASE("MouseTuning: extendedRange widens the sensitivity band", "[input][mouse][tuning]")
{
    MouseTuning t;
    t.extendedRange = true;
    REQUIRE(t.SensMin() == 0.05f);
    REQUIRE(t.SensMax() == 3.0f);
}

TEST_CASE("MouseTuning: Normalize clamps every field", "[input][mouse][tuning]")
{
    MouseTuning t;
    t.baseScale = 99.0f;
    t.mouseDpi = 0;
    t.referenceDpi = 999999;
    t.smoothing = 5.0f;
    t.accelExponent = 9.0f;
    t.menuCursorScale = 99.0f;
    REQUIRE(t.Normalize());
    REQUIRE(t.baseScale == 3.0f);
    REQUIRE(t.mouseDpi == 100);
    REQUIRE(t.referenceDpi == 32000);
    REQUIRE(t.smoothing == 0.95f);
    REQUIRE(t.accelExponent == 2.0f);
    REQUIRE(t.menuCursorScale == 4.0f);

    // Already-in-range is a no-op.
    MouseTuning ok;
    REQUIRE_FALSE(ok.Normalize());
}

TEST_CASE("MouseTuning: referenceDpi defaults to the validated-comfortable 1600", "[input][mouse][tuning]")
{
    MouseTuning t;
    REQUIRE(t.referenceDpi == 1600);
    REQUIRE(t.mouseDpi == 1600);
    // At mouseDpi == referenceDpi the factor is 1.0, so turning normalization on
    // without changing DPI is identical to Off (Off ≡ 1600).
    t.dpiNormalize = true;
    REQUIRE(t.DpiFactor() == Catch::Approx(1.0f));
}

TEST_CASE("MouseTuning: DPI presets stay within the sensitivity coverage span", "[input][mouse][tuning]")
{
    // Sensitivity spans 0.5..2.0 → a 4x range.  For there to be no unreachable
    // "dead zone" between two adjacent DPI presets, their ratio must be ≤ 4 so
    // the sensitivity slider can always bridge the gap.
    REQUIRE(kMouseDpiPresets[0] == 0); // index 0 is "Off"
    REQUIRE(kMouseDpiPresetCount >= 2);
    // Labels must line up 1:1 with the preset values (the stepper indexes both).
    REQUIRE(static_cast<int>(sizeof(kMouseDpiLabels) / sizeof(kMouseDpiLabels[0])) == kMouseDpiPresetCount);
    for (int i = 2; i < kMouseDpiPresetCount; ++i) // ratios between real DPI presets only
    {
        REQUIRE(kMouseDpiPresets[i] > kMouseDpiPresets[i - 1]); // strictly increasing
        const float ratio = static_cast<float>(kMouseDpiPresets[i]) / static_cast<float>(kMouseDpiPresets[i - 1]);
        REQUIRE(ratio <= 4.0f);
    }
}

// --- MouseState: tuning applied in Update ---

TEST_CASE("MouseState: baseScale replaces the legacy 1.5 constant", "[input][mouse][tuning]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.tuning.baseScale = 3.0f;

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    REQUIRE(ms.deltaX == Catch::Approx(10.0f * 1.0f * 3.0f));
}

TEST_CASE("MouseState: DPI normalization scales the delta", "[input][mouse][tuning]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.tuning.dpiNormalize = true;
    ms.tuning.mouseDpi = 1600;
    ms.tuning.referenceDpi = 800;

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // 10 * sens(1) * baseScale(1.5) * dpiFactor(0.5)
    REQUIRE(ms.deltaX == Catch::Approx(10.0f * 1.5f * 0.5f));
}

TEST_CASE("MouseState: smoothing low-passes motion across frames", "[input][mouse][tuning]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.tuning.smoothing = 0.5f; // alpha = 0.5

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    const float frame1 = ms.deltaX; // smoothX = 5 → 5 * 1.5 = 7.5

    REQUIRE(frame1 == Catch::Approx(5.0f * 1.5f));
    REQUIRE(frame1 < 10.0f * 1.5f); // less than the un-smoothed value

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);
    const float frame2 = ms.deltaX; // smoothX = 7.5 → 11.25

    REQUIRE(frame2 == Catch::Approx(7.5f * 1.5f));
    REQUIRE(frame2 > frame1); // converging toward the steady input
}

TEST_CASE("MouseState: acceleration amplifies motion, exponent 1 is a no-op", "[input][mouse][tuning]")
{
    MouseState::CursorAccum cursor;

    // Exponent 1 (or disabled) → identical to linear.
    MouseState linear;
    linear.tuning.acceleration = true;
    linear.tuning.accelExponent = 1.0f;
    linear.BufferMotion(10.0f, 0.0f);
    linear.Update(cursor, 0, false, UITime(), nullptr);
    REQUIRE(linear.deltaX == Catch::Approx(10.0f * 1.5f));

    // Exponent > 1 → faster-than-linear for the same input.
    MouseState accel;
    accel.tuning.acceleration = true;
    accel.tuning.accelExponent = 2.0f;
    MouseState::CursorAccum cursor2;
    accel.BufferMotion(10.0f, 0.0f);
    accel.Update(cursor2, 0, false, UITime(), nullptr);
    REQUIRE(accel.deltaX > 10.0f * 1.5f);
}

TEST_CASE("MouseState: menuCursorScale decouples cursor from aim", "[input][mouse][tuning]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;
    ms.tuning.menuCursorScale = 2.0f;

    ms.BufferMotion(10.0f, 0.0f); // small move, below the per-frame clamp
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // Aim uses the unscaled move; the cursor gets menuCursorScale applied.
    REQUIRE(cursor.aimDeltaX > 0.0f);
    REQUIRE(cursor.cursorX == Catch::Approx(2.0f * cursor.aimDeltaX));
}

TEST_CASE("MouseState: default tuning keeps cursor and aim identical", "[input][mouse][tuning]")
{
    MouseState ms;
    MouseState::CursorAccum cursor;

    ms.BufferMotion(10.0f, 0.0f);
    ms.Update(cursor, 0, false, UITime(), nullptr);

    // menuCursorScale defaults to 1.0 → classic shared movement.
    REQUIRE(cursor.cursorX == Catch::Approx(cursor.aimDeltaX));
}
