// Milestone-0 smoke test for the native Metal backend: opens a window and
// continuously presents frames cleared to a fixed, visually distinct color.
// No shaders/meshes/textures — just proves the SDL3 -> CAMetalLayer ->
// MTLDevice -> clear -> present pipeline works end to end on this machine.
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    bool checkMode = false;
    int frames = 3;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--check") == 0)
        {
            checkMode = true;
        }
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            frames = std::max(1, std::atoi(argv[++i]));
        }
    }

    Poseidon::EngineMTLBootstrap engine;
    if (!engine.Init("PoseidonMTL Smoke Test", 1280, 720))
    {
        std::fprintf(stderr, "Failed to initialize Metal bootstrap\n");
        return 1;
    }

    std::printf("Window open. Clearing to cornflower blue. Close the window or press Esc to quit.\n");

    bool running = true;
    int rendered = 0;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                running = false;
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                engine.OnWindowResized(event.window.data1, event.window.data2);
        }

        // Cornflower blue (100, 149, 237) in linear 0..1.
        engine.RenderClearAndPresent(100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f, 1.0f);
        if (checkMode && ++rendered >= frames)
            running = false;
    }

    engine.Shutdown();
    return 0;
}
