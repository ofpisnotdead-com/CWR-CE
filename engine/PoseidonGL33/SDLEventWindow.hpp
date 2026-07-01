#pragma once
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_touch.h>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Input/TouchInput.hpp>

// SDL input buffer functions (InputProcessing_sdl.cpp)
extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);
extern void SDLInput_BufferMouseWheel(float dy);
extern void SDLInput_GamepadAdded(SDL_JoystickID which);
extern void SDLInput_GamepadRemoved(SDL_JoystickID which);
extern void SDLInput_BufferUIKeyEvent(SDL_Keycode key, bool down);
extern void SDLInput_BufferUICharEvent(const char* text);
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
extern void SetSkipKeys(bool skip);

// SDL event-pump helper used by EngineGL33.  Does NOT own the SDL_Window —
// the renderer manages its lifecycle.  Handles SDL event polling, focus
// tracking, and input forwarding.
class SDLEventWindow
{
    SDL_Window* _sdlWindow = nullptr;
    int _width = 0, _height = 0;
    bool _open = false, _resized = false;
    bool _focused = true, _focusGained = false, _focusLost = false;
    bool _mouseGrab = true;
    bool _altEnterConsumed = false;
    bool _fullscreenTransitioning = false; // blocks phantom Alt+Enter during transition

  public:
    // Attach to an existing SDL window (does not take ownership).
    // Sets GApp->m_appActive and acquires mouse.
    void Attach(SDL_Window* window, int w, int h)
    {
        _sdlWindow = window;
        _width = w;
        _height = h;
        _open = (window != nullptr);
        _focused = true;

        if (_sdlWindow)
        {
            Poseidon::TouchInput_UpdateSafeAreaFromWindow(_sdlWindow);
            if (_mouseGrab)
                SDL_SetWindowRelativeMouseMode(_sdlWindow, true);
#ifndef POSEIDON_TARGET_IOS
            // Desktop only: keeps the OS IME ready for chat/console text
            // entry. On iOS this is what makes the on-screen keyboard pop
            // up immediately on launch with no way to dismiss it -- this
            // engine has no touch UI for it yet (see EngineMTL.hpp, the
            // only iOS user of this header-only class).
            SDL_StartTextInput(_sdlWindow);
#endif
        }

        extern void SetMouseAcquired(bool acquired);
        SetMouseAcquired(true);
        GApp->m_appActive = true;
    }

    void Detach()
    {
        Poseidon::TouchInput_UpdateSafeAreaFromWindow(nullptr);
        _sdlWindow = nullptr;
        _open = false;
    }

    // Init unused — the renderer creates the window.
    bool Init(int, int, bool) { return false; }

    int GetWidth() const { return _width; }
    int GetHeight() const { return _height; }
    void SwapBuffers() {} // renderer handles Present

    void HandleEvents()
    {
        _resized = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // ImGui needs every event so it can update its input state.
            // Forwarding is harmless when the overlay is hidden.
            Poseidon::Dev::DebugOverlay::ProcessEvent(event);

            // When the ImGui panel is focused over a slider / text input,
            // swallow the matching SDL events so they don't ALSO move the
            // player / fire the menu cursor / etc.  Always allow window
            // lifecycle events through.  Always allow F8 so the user can
            // dismiss the panel without it eating its own un-toggle.
            const bool isKeyPress = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
            const bool isKey = isKeyPress || event.type == SDL_EVENT_TEXT_INPUT;
            const bool isMouse = event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                 event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
            // Read event.key only for real key events; on a TEXT_INPUT event the active
            // union member is event.text, so event.key.scancode is garbage (UB).
            const bool isF8 = isKeyPress && event.key.scancode == SDL_SCANCODE_F8;
            if (!isF8 && ((isKey && Poseidon::Dev::DebugOverlay::WantsKeyboard()) ||
                          (isMouse && Poseidon::Dev::DebugOverlay::WantsMouse())))
            {
                continue;
            }

            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                // Alt+F4 is a legitimate in-game combo (Alt = freelook, F4 = "select
                // unit 4").  On Windows the OS turns Alt+F4 into a window-close request;
                // ignore it only during active gameplay so the F4 keypress reaches the
                // game.  Everywhere else (menus, briefing, the Esc dialog) Alt+F4 is the
                // standard desktop quit and must close — as do the title-bar X / taskbar
                // / menu Quit, which carry no Alt.
                const bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                if (!::Poseidon::ShouldHonorWindowClose(altDown, GApp->IsInGameplay()))
                {
                    LOG_INFO(Input, "SDLEventWindow: ignoring Alt+F4 close (valid in-game shortcut)");
                    continue;
                }
                _open = false;
                GApp->m_closeRequest = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                if (_sdlWindow)
                    SDL_GetWindowSizeInPixels(_sdlWindow, &_width, &_height);
                Poseidon::TouchInput_UpdateSafeAreaFromWindow(_sdlWindow);
                _resized = true;
                // Notify the engine so it can resize the swap chain with the
                // correct final dimensions (critical for D3D11 FLIP_DISCARD).
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnWindowResized(_width, _height);
            }
            else if (event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED)
            {
                Poseidon::TouchInput_UpdateSafeAreaFromWindow(_sdlWindow);
            }
            else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)
            {
                _fullscreenTransitioning = false;
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnFullscreenChanged(false);
            }
            else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            {
                _fullscreenTransitioning = false;
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->OnFullscreenChanged(true);
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            {
                _focused = true;
                _focusGained = true;
                GApp->m_appActive = true;
                if (_mouseGrab && _sdlWindow)
                    SDL_SetWindowRelativeMouseMode(_sdlWindow, true);
                if (::Poseidon::GEngine)
                    ::Poseidon::GEngine->Activate();
                SetSkipKeys(true);
                if (Poseidon::GSoundsys)
                    Poseidon::GSoundsys->Activate(true);
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                _focused = false;
                _focusLost = true;
                GApp->m_appActive = false;
                if (_sdlWindow)
                    SDL_SetWindowRelativeMouseMode(_sdlWindow, false);
                if (!GApp->m_keepFocus)
                {
                    if (::Poseidon::GEngine)
                        ::Poseidon::GEngine->Deactivate();
                    SetSkipKeys(true);
                    if (Poseidon::GSoundsys)
                        Poseidon::GSoundsys->Activate(false);
                }
            }
            else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                GApp->m_appPaused = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
            {
                GApp->m_appPaused = false;
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (!event.key.repeat && event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT))
                {
                    if (_fullscreenTransitioning)
                    {
                        LOG_INFO(Graphics, "SDLEventWindow: Alt+Enter ignored (transition in progress)");
                        _altEnterConsumed = true;
                        continue;
                    }
                    if (::Poseidon::GEngine && _sdlWindow)
                    {
                        bool windowed = ::Poseidon::GEngine->IsWindowed();
                        LOG_INFO(Graphics, "SDLEventWindow: Alt+Enter requesting {}",
                                 windowed ? "fullscreen" : "windowed");
                        _fullscreenTransitioning = true;
                        ::Poseidon::GEngine->SetWindowMode(windowed ? ::Poseidon::WindowMode::Borderless
                                                                    : ::Poseidon::WindowMode::Windowed);
                        // The Borderless and Windowed paths in SetWindowMode are
                        // synchronous (they don't go through SDL's fullscreen state
                        // machine — see the SDL #12791 comment in
                        // EngineGL33_Lifecycle.cpp) so the
                        // ENTER_FULLSCREEN / LEAVE_FULLSCREEN handlers further
                        // down won't fire to clear this flag.  Drop it here
                        // immediately for those two modes so the next Alt+Enter
                        // isn't swallowed as "transition in progress".  The
                        // exclusive-fullscreen path still goes through SDL's
                        // state machine and will clear the flag from its
                        // ENTER_FULLSCREEN handler when SDL completes the
                        // mode switch.
                        _fullscreenTransitioning = false;
                    }
                    _altEnterConsumed = true;
                    continue;
                }
                if (!event.key.repeat)
                    SDLInput_BufferKeyEvent(event.key.scancode, true, Poseidon::Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, true);
            }
            else if (event.type == SDL_EVENT_KEY_UP)
            {
                if (event.key.scancode == SDL_SCANCODE_RETURN && _altEnterConsumed)
                {
                    _altEnterConsumed = false;
                    continue;
                }
                SDLInput_BufferKeyEvent(event.key.scancode, false, Poseidon::Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, false);
            }
            else if (event.type == SDL_EVENT_TEXT_INPUT)
                SDLInput_BufferUICharEvent(event.text.text);
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                if (event.button.which == SDL_TOUCH_MOUSEID)
                    continue;
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, true);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                if (event.button.which == SDL_TOUCH_MOUSEID)
                    continue;
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, false);
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (event.motion.which == SDL_TOUCH_MOUSEID)
                    continue;
                SDLInput_BufferMouseMotion(event.motion.xrel, event.motion.yrel);
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
                SDLInput_BufferMouseWheel(event.wheel.y);
            else if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
                     event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
            {
                if (SDL_GetTouchDeviceType(event.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT)
                    Poseidon::TouchInput_HandleFingerEvent(event.tfinger);
            }
            else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                SDLInput_GamepadAdded(event.gdevice.which);
            else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
                SDLInput_GamepadRemoved(event.gdevice.which);
        }
    }

    bool IsOpen() const { return _open; }
    void* GetNativeHandle() const { return _sdlWindow; }
    bool WasResized()
    {
        bool r = _resized;
        _resized = false;
        return r;
    }
    bool HasFocus() const { return _focused; }
    bool ConsumeGainedFocus()
    {
        bool r = _focusGained;
        _focusGained = false;
        return r;
    }
    bool ConsumeLostFocus()
    {
        bool r = _focusLost;
        _focusLost = false;
        return r;
    }
    void SetMouseGrab(bool grab)
    {
        _mouseGrab = grab;
        if (_sdlWindow)
            SDL_SetWindowRelativeMouseMode(_sdlWindow, grab && _focused);
    }
    bool IsMouseGrabbed() const { return _mouseGrab; }
    void SetTitle(const char* title)
    {
        if (_sdlWindow)
            SDL_SetWindowTitle(_sdlWindow, title);
    }
};
