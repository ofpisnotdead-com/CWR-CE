#pragma once

#include <vector>

namespace Poseidon
{

struct MainMenuControlRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool visible = true;
};

struct MainMenuModsPlacement
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool rightAlign = false;
};

MainMenuModsPlacement CalculateMainMenuModsPlacement(const MainMenuControlRect& quit,
                                                     const std::vector<MainMenuControlRect>& foreground);

} // namespace Poseidon
