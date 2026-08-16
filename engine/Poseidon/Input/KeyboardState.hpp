#pragma once

#include <SDL3/SDL_scancode.h>

#include <Poseidon/Input/CheatCode.hpp>

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Time/Time.hpp>

namespace Poseidon
{

// Per-device keyboard state — owns raw key arrays, event buffer, and processing.
// Movement aggregates (keyMoveForward, fire, lookAround, etc.) are NOT
// owned here — they're computed by InputSubsystem from multi-device actions.
struct KeyboardState
{
    // Per-scancode state
    DWORD keyPressed[SDL_SCANCODE_COUNT] = {};  // timestamp of last key-down (0 = not pressed)
    DWORD keyLastPressed[SDL_SCANCODE_COUNT] = {}; // timestamp of prior key-down for double-tap detection
    float keys[SDL_SCANCODE_COUNT] = {};         // integrated duration (0..1 per frame)
    bool keysToDo[SDL_SCANCODE_COUNT] = {};      // edge detection (true on key-down frame)
    bool keysDoubleTapToDo[SDL_SCANCODE_COUNT] = {}; // true on the second key-down frame
    bool keysDoubleTapActive[SDL_SCANCODE_COUNT] = {}; // true while the second press is held
    // Cheat state
#if _ENABLE_CHEATS
    bool cheat1 = false;
    bool cheat2 = false;
#endif
    bool cheatEntryTrigger = false; // set from the bound UACheatEntry combo; arms awaitCheat
    bool awaitCheat = false;
    CheatCode cheatActive = CheatNone;
    RString cheatInProgress;

    CheatCode CheatActivated() const { return cheatActive; }
    void CheatServed() { cheatActive = CheatNone; }

    // Activity timestamps (keyboard vs. mouse/gamepad arbitration)
    Foundation::UITime moveLastActive = UITIME_MIN;
    Foundation::UITime cursorLastActive = UITIME_MIN;
    Foundation::UITime turnLastActive = UITIME_MIN;
    Foundation::UITime thrustLastActive = UITIME_MIN;

    // Buffering (called from the SDL event thread)

    void BufferKeyEvent(int scancode, bool down, DWORD timestamp);
    void DiscardBuffered();

    // Process buffered events into keys[]/keysToDo[]/keyPressed[].
    // sysTime: current frame time (same epoch as event timestamps)
    // timeDelta: frame duration in ms
    // userInputEnabled: false suppresses all input (discards buffer)
    void Update(DWORD sysTime, DWORD timeDelta, bool userInputEnabled);

    void ForgetKeys();

  private:
    static constexpr int kKeyBufferSize = 64;
    static constexpr DWORD kDoubleTapWindowMs = 400;
    struct KeyEvent
    {
        int dik;
        bool down;
        DWORD timestamp;
    };
    KeyEvent keyBuffer_[kKeyBufferSize] = {};
    int keyCount_ = 0;

    // Cheat sequence processing — called on each key-down.
    // Sets keysToDo[dik] if NOT in cheat-await mode.
    void ProcessKeyPressed(int dik);
};
} // namespace Poseidon

