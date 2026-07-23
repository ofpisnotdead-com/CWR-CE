#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Input/KeyboardState.hpp>
#include <SDL3/SDL_scancode.h>

// Construction

using namespace Poseidon;

namespace
{
SDL_Scancode CheatCharToScancode(char c)
{
    if (c >= 'a' && c <= 'z')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'a'));
    if (c >= 'A' && c <= 'Z')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'A'));
    if (c >= '1' && c <= '9')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (c - '1'));
    if (c == '0')
        return SDL_SCANCODE_0;
    return SDL_SCANCODE_UNKNOWN;
}

void PressAndRelease(KeyboardState& kb, SDL_Scancode sc, DWORD& time)
{
    kb.BufferKeyEvent(sc, true, time);
    kb.BufferKeyEvent(sc, false, time + 10);
    time += 20;
    kb.Update(time, 20, true);
}

void TypeCheat(KeyboardState& kb, const char* text)
{
    DWORD time = 300;
    for (const char* p = text; *p; ++p)
    {
        SDL_Scancode sc = CheatCharToScancode(*p);
        REQUIRE(sc != SDL_SCANCODE_UNKNOWN);
        PressAndRelease(kb, sc, time);
    }
}

void ArmClassicCheatEntry(KeyboardState& kb)
{
    kb.cheatEntryTrigger = true;
    kb.Update(200, 100, true);
    kb.cheatEntryTrigger = false;
}
} // namespace

TEST_CASE("KeyboardState: default construction zeroes all state", "[input][keyboard]")
{
    KeyboardState kb;

    for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
    {
        REQUIRE(kb.keys[i] == 0.0f);
        REQUIRE(kb.keysToDo[i] == false);
        REQUIRE(kb.keysDoubleTapToDo[i] == false);
        REQUIRE(kb.keysDoubleTapActive[i] == false);
        REQUIRE(kb.keyPressed[i] == 0);
        REQUIRE(kb.keyLastPressed[i] == 0);
    }
    REQUIRE(kb.awaitCheat == false);
    REQUIRE(kb.cheatActive == CheatNone);
}

TEST_CASE("KeyboardState: second press within window sets double-tap edge and active hold", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_G, true, 1000);
    kb.BufferKeyEvent(SDL_SCANCODE_G, false, 1050);
    kb.Update(1100, 100, true);

    REQUIRE_FALSE(kb.keysDoubleTapToDo[SDL_SCANCODE_G]);
    REQUIRE_FALSE(kb.keysDoubleTapActive[SDL_SCANCODE_G]);

    kb.BufferKeyEvent(SDL_SCANCODE_G, true, 1300);
    kb.Update(1400, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_G]);
    REQUIRE(kb.keysDoubleTapToDo[SDL_SCANCODE_G]);
    REQUIRE(kb.keysDoubleTapActive[SDL_SCANCODE_G]);

    kb.BufferKeyEvent(SDL_SCANCODE_G, false, 1450);
    kb.Update(1500, 100, true);

    REQUIRE_FALSE(kb.keysDoubleTapActive[SDL_SCANCODE_G]);
}

TEST_CASE("KeyboardState: second press after window is not a double-tap", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_G, true, 1000);
    kb.BufferKeyEvent(SDL_SCANCODE_G, false, 1050);
    kb.Update(1100, 100, true);

    kb.BufferKeyEvent(SDL_SCANCODE_G, true, 1501);
    kb.Update(1600, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_G]);
    REQUIRE_FALSE(kb.keysDoubleTapToDo[SDL_SCANCODE_G]);
    REQUIRE_FALSE(kb.keysDoubleTapActive[SDL_SCANCODE_G]);
}

// Buffering

TEST_CASE("KeyboardState: BufferKeyEvent stores events for Update", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == true);
}

TEST_CASE("KeyboardState: out-of-range scancode is ignored", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(-1, true, 100);
    kb.BufferKeyEvent(SDL_SCANCODE_COUNT, true, 100);
    kb.BufferKeyEvent(9999, true, 100);
    kb.Update(200, 100, true);

    // Nothing should have changed
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
        REQUIRE(kb.keysToDo[i] == false);
}

TEST_CASE("KeyboardState: buffer overflow drops excess events", "[input][keyboard]")
{
    KeyboardState kb;

    // Fill buffer to capacity (64 events)
    for (int i = 0; i < 64; i++)
        kb.BufferKeyEvent(SDL_SCANCODE_A, true, 100);

    // 65th event should be dropped
    kb.BufferKeyEvent(SDL_SCANCODE_B, true, 100);
    kb.Update(200, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_A] == true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_B] == false);
}

TEST_CASE("KeyboardState: DiscardBuffered clears pending events", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.DiscardBuffered();
    kb.Update(200, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == 0.0f);
}

// Key press edge detection

TEST_CASE("KeyboardState: key press sets keysToDo on first frame", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_SPACE, true, 100);
    kb.Update(200, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_SPACE] == true);
    REQUIRE(kb.keyPressed[SDL_SCANCODE_SPACE] == 100);
}

TEST_CASE("KeyboardState: keysToDo resets on next frame", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_SPACE, true, 100);
    kb.Update(200, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_SPACE] == true);

    // Next frame — no new events
    kb.Update(300, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_SPACE] == false);
}

TEST_CASE("KeyboardState: duplicate press events ignored (still held)", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);

    // Another press without release — should not re-trigger edge
    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 250);
    kb.Update(300, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
}

// Time integration (held key duration)

TEST_CASE("KeyboardState: held key for full frame gives keys=1", "[input][keyboard]")
{
    KeyboardState kb;

    // Press at t=50 (before frame start at 100)
    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 50);
    kb.Update(200, 100, true);

    // Held for entire frame — keys[W] should be 1.0
    // (sysTime - keyPressed >= timeDelta → clamp to 1)
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(1.0f));
}

TEST_CASE("KeyboardState: held key for longer than frame gives keys=1", "[input][keyboard]")
{
    KeyboardState kb;

    // Press at t=50
    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 50);
    kb.Update(100, 100, true);

    // Next frame — key held since before frame start (50 < 200-100)
    kb.Update(200, 100, true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(1.0f));
}

TEST_CASE("KeyboardState: key released mid-frame gives partial duration", "[input][keyboard]")
{
    KeyboardState kb;

    // Press at start of frame
    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);

    // Release at t=250 (50ms into a 100ms frame)
    kb.BufferKeyEvent(SDL_SCANCODE_W, false, 250);
    kb.Update(300, 100, true);

    // keys[W] = 50/100 = 0.5
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(0.5f));
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] == 0);
}

TEST_CASE("KeyboardState: key pressed and released within same frame", "[input][keyboard]")
{
    KeyboardState kb;

    // Press at 110, release at 160 — both within frame [100,200]
    kb.BufferKeyEvent(SDL_SCANCODE_A, true, 110);
    kb.BufferKeyEvent(SDL_SCANCODE_A, false, 160);
    kb.Update(200, 100, true);

    // Duration = 50ms / 100ms = 0.5
    REQUIRE(kb.keys[SDL_SCANCODE_A] == Catch::Approx(0.5f));
    REQUIRE(kb.keysToDo[SDL_SCANCODE_A] == true);
    REQUIRE(kb.keyPressed[SDL_SCANCODE_A] == 0);
}

TEST_CASE("KeyboardState: multiple keys tracked independently", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.BufferKeyEvent(SDL_SCANCODE_A, true, 150);
    kb.Update(200, 100, true);

    // W held for full frame, A held for 50ms
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(1.0f));
    // A pressed mid-frame: held from 150 to 200 = 50ms
    REQUIRE(kb.keys[SDL_SCANCODE_A] >= 0.4f);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_A] == true);
}

TEST_CASE("KeyboardState: keys reset to 0 each frame", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.BufferKeyEvent(SDL_SCANCODE_W, false, 150);
    kb.Update(200, 100, true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] > 0.0f);

    // Next frame — no events, key not held
    kb.Update(300, 100, true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == 0.0f);
}

TEST_CASE("KeyboardState: zero timeDelta uses safe fallback", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.BufferKeyEvent(SDL_SCANCODE_W, false, 150);
    // timeDelta=0 should not crash (invTimeDelta = 1000)
    kb.Update(200, 0, true);

    // Should produce some value, not crash or produce NaN
    REQUIRE(kb.keys[SDL_SCANCODE_W] >= 0.0f);
}

// Input disabled

TEST_CASE("KeyboardState: userInputEnabled=false suppresses all input", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, false);

    REQUIRE(kb.keys[SDL_SCANCODE_W] == 0.0f);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] == 0);
}

TEST_CASE("KeyboardState: disabled clears buffer (events not replayed later)", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, false);

    // Re-enable — previous events should be gone
    kb.Update(300, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
}

// ForgetKeys

TEST_CASE("KeyboardState: ForgetKeys clears all state", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == true);
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] != 0);

    kb.ForgetKeys();

    for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
    {
        REQUIRE(kb.keys[i] == 0.0f);
        REQUIRE(kb.keysToDo[i] == false);
        REQUIRE(kb.keysDoubleTapToDo[i] == false);
        REQUIRE(kb.keysDoubleTapActive[i] == false);
        REQUIRE(kb.keyPressed[i] == 0);
        REQUIRE(kb.keyLastPressed[i] == 0);
    }
    REQUIRE(kb.awaitCheat == false);
    REQUIRE(kb.cheatActive == CheatNone);
}

TEST_CASE("KeyboardState: ForgetKeys discards buffered events", "[input][keyboard]")
{
    KeyboardState kb;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.ForgetKeys();
    kb.Update(200, 100, true);

    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
}

// Cheat state

TEST_CASE("KeyboardState: CheatActivated/CheatServed", "[input][keyboard]")
{
    KeyboardState kb;

    REQUIRE(kb.CheatActivated() == CheatNone);
    kb.cheatActive = CheatUnlockCampaign;
    REQUIRE(kb.CheatActivated() == CheatUnlockCampaign);
    kb.CheatServed();
    REQUIRE(kb.CheatActivated() == CheatNone);
}

TEST_CASE("KeyboardState: awaitCheat suppresses keysToDo", "[input][keyboard]")
{
    KeyboardState kb;
    kb.awaitCheat = true;

    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);

    // keysToDo should NOT be set when awaiting cheat
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);
    // But keyPressed should still track the press
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] == 100);
}

TEST_CASE("KeyboardState: Left Shift plus keypad minus arms classic cheat entry", "[input][keyboard][cheat]")
{
    KeyboardState kb;

    ArmClassicCheatEntry(kb);

    REQUIRE(kb.awaitCheat == true);
    REQUIRE(kb.cheatInProgress.GetLength() == 0);
    REQUIRE(kb.cheatActive == CheatNone);
}

TEST_CASE("KeyboardState: cheat entry arms only when the trigger flag is set", "[input][keyboard][cheat]")
{
    KeyboardState kb;

    kb.cheatEntryTrigger = false;
    kb.Update(200, 100, true);
    REQUIRE(kb.awaitCheat == false);

    kb.cheatEntryTrigger = true;
    kb.Update(300, 100, true);
    REQUIRE(kb.awaitCheat == true);
}

TEST_CASE("KeyboardState: classic SAVEGAME cheat activates from typed sequence", "[input][keyboard][cheat]")
{
    KeyboardState kb;

    ArmClassicCheatEntry(kb);
    TypeCheat(kb, "savegame");

    REQUIRE(kb.awaitCheat == false);
    REQUIRE(kb.cheatActive == CheatSaveGame);
}

TEST_CASE("KeyboardState: classic IWILLBETHEONE cheat activates god mode code", "[input][keyboard][cheat]")
{
    KeyboardState kb;

    ArmClassicCheatEntry(kb);
    TypeCheat(kb, "iwillbetheone");

    REQUIRE(kb.awaitCheat == false);
    REQUIRE(kb.cheatActive == CheatGodMode);
}

TEST_CASE("KeyboardState: wrong classic cheat prefix cancels entry", "[input][keyboard][cheat]")
{
    KeyboardState kb;

    ArmClassicCheatEntry(kb);
    TypeCheat(kb, "sx");

    REQUIRE(kb.awaitCheat == false);
    REQUIRE(kb.cheatActive == CheatNone);
}

// Multi-frame press-hold-release

TEST_CASE("KeyboardState: full press-hold-release lifecycle", "[input][keyboard]")
{
    KeyboardState kb;

    // Frame 1 (100-200): press at t=100
    kb.BufferKeyEvent(SDL_SCANCODE_W, true, 100);
    kb.Update(200, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(1.0f));
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] == 100);

    // Frame 2 (200-300): held, no events
    kb.Update(300, 100, true);
    REQUIRE(kb.keysToDo[SDL_SCANCODE_W] == false);           // edge cleared
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(1.0f)); // full frame
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] != 0);             // still held

    // Frame 3 (300-400): release at t=350
    kb.BufferKeyEvent(SDL_SCANCODE_W, false, 350);
    kb.Update(400, 100, true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == Catch::Approx(0.5f)); // half frame
    REQUIRE(kb.keyPressed[SDL_SCANCODE_W] == 0);             // released

    // Frame 4 (400-500): nothing
    kb.Update(500, 100, true);
    REQUIRE(kb.keys[SDL_SCANCODE_W] == 0.0f);
}

TEST_CASE("KeyboardState: rapid press-release-press within frame", "[input][keyboard]")
{
    KeyboardState kb;

    // Press, release, press again within one frame
    kb.BufferKeyEvent(SDL_SCANCODE_A, true, 100);
    kb.BufferKeyEvent(SDL_SCANCODE_A, false, 130);
    kb.BufferKeyEvent(SDL_SCANCODE_A, true, 150);
    kb.Update(200, 100, true);

    // First press: keysToDo set, keys gets 30ms duration from release
    // Second press: keyPressed[A] = 150, still held at frame end
    REQUIRE(kb.keysToDo[SDL_SCANCODE_A] == true);
    REQUIRE(kb.keyPressed[SDL_SCANCODE_A] == 150);
    // keys[A] = 30/100 (from first press-release) + 50/100 (held 150→200) = 0.8
    REQUIRE(kb.keys[SDL_SCANCODE_A] == Catch::Approx(0.8f));
}
