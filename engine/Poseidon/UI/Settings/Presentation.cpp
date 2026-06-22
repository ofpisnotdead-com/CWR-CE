#include <Poseidon/UI/Settings/Presentation.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{
namespace Presentation
{
namespace
{
AspectRatio::DisplayStyle s_style = AspectRatio::Modern;
AspectRatio::UltrawideClamp s_clamp = AspectRatio::Clamp21x9;
} // namespace

void SetPolicy(AspectRatio::DisplayStyle style, AspectRatio::UltrawideClamp clamp)
{
    s_style = style;
    s_clamp = clamp;
    AspectRatio::SetPillarboxBarsEnabled(style == AspectRatio::Modern);
}

AspectRatio::PolicyInput CurrentPolicy(int viewportWidth, int viewportHeight)
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = viewportWidth;
    input.viewportHeight = viewportHeight;
    input.style = s_style;
    input.ultrawideClamp = s_clamp;
    return input;
}

AspectRatio::Settings Resolve(int viewportWidth, int viewportHeight)
{
    if (AspectRatio::Live().overrideEnabled)
        return AspectRatio::ResolveLive(AspectRatio::Live(), viewportWidth, viewportHeight);
    return AspectRatio::ResolvePolicy(CurrentPolicy(viewportWidth, viewportHeight)).settings;
}

AspectRatio::Settings Apply(int viewportWidth, int viewportHeight)
{
    const AspectRatio::Settings s = Resolve(viewportWidth, viewportHeight);
    if (GEngine)
    {
        AspectSettings e{};
        e.leftFOV = s.leftFOV;
        e.topFOV = s.topFOV;
        e.uiTopLeftX = s.uiTopLeftX;
        e.uiTopLeftY = s.uiTopLeftY;
        e.uiBottomRightX = s.uiBottomRightX;
        e.uiBottomRightY = s.uiBottomRightY;
        e.worldLeft = s.worldLeft;
        e.worldTop = s.worldTop;
        e.worldRight = s.worldRight;
        e.worldBottom = s.worldBottom;
        GEngine->SetAspectSettings(e);
    }
    return s;
}

} // namespace Presentation

} // namespace Poseidon
