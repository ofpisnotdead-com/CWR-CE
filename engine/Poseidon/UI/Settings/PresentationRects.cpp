#include <Poseidon/UI/Settings/PresentationRects.hpp>

#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{
namespace Presentation
{
namespace
{
constexpr float kRatio4x3 = 4.0f / 3.0f;
constexpr float kRatio16x9 = 16.0f / 9.0f;
constexpr float kRatio21x9 = 21.0f / 9.0f;

float SafeRatio(float ratio)
{
    return ratio > 0.0f ? ratio : kRatio4x3;
}
} // namespace

Rect PhysicalViewportRect()
{
    return {};
}

Rect Logical2DRect(const AspectSettings& aspect)
{
    Rect rect;
    rect.x = aspect.uiTopLeftX;
    rect.y = aspect.uiTopLeftY;
    rect.w = aspect.uiBottomRightX - aspect.uiTopLeftX;
    rect.h = aspect.uiBottomRightY - aspect.uiTopLeftY;
    return rect;
}

Rect CenterRatioRect(float targetRatio, int viewportWidth, int viewportHeight)
{
    targetRatio = SafeRatio(targetRatio);
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return {};

    const float viewportRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    Rect rect;
    if (viewportRatio > targetRatio)
    {
        rect.w = targetRatio / viewportRatio;
        rect.x = (1.0f - rect.w) * 0.5f;
        return rect;
    }

    rect.h = viewportRatio / targetRatio;
    rect.y = (1.0f - rect.h) * 0.5f;
    return rect;
}

Rect ResolveRect(RectKind kind, int viewportWidth, int viewportHeight, const AspectSettings& aspect)
{
    switch (kind)
    {
        case RectKind::Logical2DViewport:
            return Logical2DRect(aspect);
        case RectKind::Safe4x3:
            return CenterRatioRect(kRatio4x3, viewportWidth, viewportHeight);
        case RectKind::Safe16x9:
        case RectKind::HudSafe16x9:
        case RectKind::TitleSafe16x9:
            return CenterRatioRect(kRatio16x9, viewportWidth, viewportHeight);
        case RectKind::Safe21x9:
        case RectKind::HudSafe21x9:
            return CenterRatioRect(kRatio21x9, viewportWidth, viewportHeight);
        case RectKind::PhysicalViewport:
        default:
            return PhysicalViewportRect();
    }
}

const char* RectKindName(RectKind kind)
{
    switch (kind)
    {
        case RectKind::PhysicalViewport:
            return "PhysicalViewport";
        case RectKind::Logical2DViewport:
            return "Logical2DViewport";
        case RectKind::Safe4x3:
            return "Safe4x3";
        case RectKind::Safe16x9:
            return "Safe16x9";
        case RectKind::Safe21x9:
            return "Safe21x9";
        case RectKind::HudSafe16x9:
            return "HudSafe16x9";
        case RectKind::HudSafe21x9:
            return "HudSafe21x9";
        case RectKind::TitleSafe16x9:
            return "TitleSafe16x9";
        default:
            return "PhysicalViewport";
    }
}

bool ParseRectKind(const char* name, RectKind& out)
{
    if (!name)
        return false;

    const RectKind all[] = {
        RectKind::PhysicalViewport, RectKind::Logical2DViewport, RectKind::Safe4x3,     RectKind::Safe16x9,
        RectKind::Safe21x9,         RectKind::HudSafe16x9,       RectKind::HudSafe21x9, RectKind::TitleSafe16x9,
    };
    for (RectKind kind : all)
    {
        if (!strcmpi(name, RectKindName(kind)))
        {
            out = kind;
            return true;
        }
    }
    return false;
}

} // namespace Presentation
} // namespace Poseidon
