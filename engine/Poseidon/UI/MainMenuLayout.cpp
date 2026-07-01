#include <Poseidon/UI/MainMenuLayout.hpp>

namespace Poseidon
{

namespace
{

bool Overlaps(const MainMenuControlRect& a, const MainMenuControlRect& b)
{
    if (!b.visible || b.w <= 0.0f || b.h <= 0.0f)
        return false;
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

bool OverlapsForeground(const MainMenuControlRect& rect, const std::vector<MainMenuControlRect>& foreground)
{
    for (const MainMenuControlRect& ctrl : foreground)
        if (Overlaps(rect, ctrl))
            return true;
    return false;
}

} // namespace

MainMenuModsPlacement CalculateMainMenuModsPlacement(const MainMenuControlRect& quit,
                                                     const std::vector<MainMenuControlRect>& foreground)
{
    MainMenuModsPlacement placement;
    placement.w = quit.w;
    placement.h = quit.h;
    placement.rightAlign = quit.x > 0.10f;

    const float gap = quit.w * 0.05f;
    if (quit.x > 0.10f)
    {
        placement.x = quit.x - quit.w - gap;
        if (placement.x < 0.0f)
            placement.x = 0.0f;
        placement.y = quit.y;
        return placement;
    }

    const float yAbove = quit.y - quit.h - gap;
    const MainMenuControlRect above{quit.x, yAbove, quit.w, quit.h, true};
    if (yAbove >= 0.0f && !OverlapsForeground(above, foreground))
    {
        placement.x = above.x;
        placement.y = above.y;
        return placement;
    }

    if (quit.y + quit.h * 2.0f + gap <= 0.95f)
    {
        placement.x = quit.x;
        placement.y = quit.y + quit.h + gap;
        return placement;
    }

    placement.x = 0.0f;
    placement.y = yAbove >= 0.0f ? yAbove : quit.y;
    return placement;
}

} // namespace Poseidon
