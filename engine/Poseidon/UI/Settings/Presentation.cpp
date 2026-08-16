#include <Poseidon/UI/Settings/Presentation.hpp>

#include <optional>

#include <Poseidon/Core/Profile/UserConfig.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{
namespace Presentation
{
namespace
{
AspectRatio::DisplayStyle s_style = AspectRatio::Modern;
AspectRatio::UltrawideClamp s_clamp = AspectRatio::Clamp21x9;
struct UserFov
{
    float left;
    float top;
};
std::optional<UserFov> s_userFov;
} // namespace

void SetPolicy(AspectRatio::DisplayStyle style, AspectRatio::UltrawideClamp clamp)
{
    s_style = style;
    s_clamp = clamp;
    AspectRatio::SetPillarboxBarsEnabled(style == AspectRatio::Modern);
}

namespace
{
AspectRatio::PolicyInput CurrentPolicy(int viewportWidth, int viewportHeight)
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = viewportWidth;
    input.viewportHeight = viewportHeight;
    input.style = s_style;
    input.ultrawideClamp = s_clamp;
    return input;
}

AspectRatio::Settings ResolveAutomatic(int viewportWidth, int viewportHeight)
{
    return AspectRatio::ResolvePolicy(CurrentPolicy(viewportWidth, viewportHeight)).settings;
}
} // namespace

bool ConfigureUserFov(UserConfig& config, int viewportWidth, int viewportHeight)
{
    const AspectRatio::Settings automatic = ResolveAutomatic(viewportWidth, viewportHeight);
    const bool migrated = config.MigrateFov(automatic.leftFOV, automatic.topFOV);
    if (!config.HasCustomFov())
    {
        s_userFov.reset();
        return migrated;
    }
    s_userFov = UserFov{config.fovLeft, config.fovTop};
    return migrated;
}

AspectRatio::Settings Resolve(int viewportWidth, int viewportHeight)
{
    if (AspectRatio::Live().overrideEnabled)
        return AspectRatio::ResolveLive(AspectRatio::Live(), viewportWidth, viewportHeight);
    AspectRatio::Settings settings = ResolveAutomatic(viewportWidth, viewportHeight);
    if (s_userFov)
    {
        settings.leftFOV = s_userFov->left;
        settings.topFOV = s_userFov->top;
    }
    return settings;
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
