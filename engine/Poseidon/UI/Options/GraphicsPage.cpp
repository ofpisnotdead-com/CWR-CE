#include <Poseidon/UI/Options/GraphicsPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/UI/Settings/GraphicsApply.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
struct GraphicsRowText
{
    const char* label;
    const char* desc;
};
const GraphicsRowText kRows[] = {
    {"STR_DISP_MAIN_OPT_GRAPHICS_QUALITY_PRESET", "STR_DISP_MAIN_OPT_GRAPHICS_QUALITY_PRESET_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_TERRAIN_DETAIL", "STR_DISP_MAIN_OPT_GRAPHICS_TERRAIN_DETAIL_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_OBJECT_LOD", "STR_DISP_MAIN_OPT_GRAPHICS_OBJECT_LOD_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_SHADOW_QUALITY", "STR_DISP_MAIN_OPT_GRAPHICS_SHADOW_QUALITY_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_PARTICLES", "STR_DISP_MAIN_OPT_GRAPHICS_PARTICLES_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_VSYNC", "STR_DISP_MAIN_OPT_GRAPHICS_VSYNC_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_FPS_CAP", "STR_DISP_MAIN_OPT_GRAPHICS_FPS_CAP_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_BRIGHTNESS", "STR_DISP_MAIN_OPT_GRAPHICS_BRIGHTNESS_DESC"},
    {"STR_DISP_MAIN_OPT_GRAPHICS_GAMMA", "STR_DISP_MAIN_OPT_GRAPHICS_GAMMA_DESC"},
};
constexpr int kFpsCapValues[] = {0, 30, 60, 90, 120, 144, 240};

// Brightness 0.4 .. 1.8 → slider 0..100; Gamma 0.5 .. 2.3 → slider 0..100.
int FloatToSlider(float v, float lo, float hi)
{
    float c = std::clamp(v, lo, hi);
    return (int)std::lround((c - lo) / (hi - lo) * 100.0f);
}
float SliderToFloat(int s, float lo, float hi)
{
    int c = std::clamp(s, 0, 100);
    return lo + (c / 100.0f) * (hi - lo);
}

std::string GraphicsCfgPath()
{
    return GamePaths::Instance().UserDir() + "graphics.cfg";
}

int FpsValueToIndex(int fps)
{
    for (int i = 0; i < (int)(sizeof(kFpsCapValues) / sizeof(int)); ++i)
        if (kFpsCapValues[i] == fps)
            return i;
    return 0; // unknown → Unlimited
}

// Drop the displayed Quality Preset to PresetCustom whenever the
// four tier rows no longer match any bundle.  Caller invokes this
// after each tier-row write.
void RederivePreset(GraphicsConfig& cfg)
{
    cfg.qualityPreset = cfg.DerivePresetFromTiers();
}
} // namespace

// Bound at construction so row queries work on a page never shown.
GraphicsPage::GraphicsPage()
{
    m_graphics.SetPage(this);
}

const char* GraphicsPage::TitleText() const
{
    return LocalizeString("STR_DISP_MAIN_OPT_GRAPHICS");
}

int GraphicsPage::BrightnessToSlider(float value)
{
    return FloatToSlider(value, 0.4f, 1.8f);
}

float GraphicsPage::SliderToBrightness(int slider)
{
    return SliderToFloat(slider, 0.4f, 1.8f);
}

int GraphicsPage::GammaToSlider(float value)
{
    return FloatToSlider(value, 0.5f, 2.3f);
}

float GraphicsPage::SliderToGamma(int slider)
{
    return SliderToFloat(slider, 0.5f, 2.3f);
}

int GraphicsPage::FpsCapValueToIndex(int fps)
{
    return FpsValueToIndex(fps);
}

const char* GraphicsPage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* GraphicsPage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

const char* GraphicsPage::GraphicsProvider::RowLabel(int row) const
{
    static_assert(sizeof(kRows) / sizeof(kRows[0]) == kRowCount, "GraphicsPage row table out of sync with kRowCount");
    if (row >= 0 && row < kRowCount)
        return LocalizeString(kRows[row].label);
    return "";
}

const char* GraphicsPage::GraphicsProvider::RowDescription(int row) const
{
    if (row >= 0 && row < kRowCount)
        return LocalizeString(kRows[row].desc);
    return "";
}

OptionsScrollList::RowDef GraphicsPage::GraphicsProvider::RowFor(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return {502, m_page->m_presetCStrs.data(), 5};
        case kRowTerrain:
            return {512, m_page->m_tierFourCStrs.data(), 4};
        case kRowObjectLod:
            return {522, m_page->m_tierFourCStrs.data(), 4};
        case kRowShadow:
            return {532, m_page->m_shadowCStrs.data(), 4};
        case kRowParticles:
            return {542, m_page->m_particlesCStrs.data(), 3};
        case kRowVsync:
            return {552, m_page->m_vsyncCStrs.data(), 3};
        case kRowFpsCap:
            return {562, m_page->m_fpsCapCStrs.data(), 7};
        case kRowBrightness:
            return {572, nullptr, -1}; // slider
        case kRowGamma:
            return {582, nullptr, -1}; // slider
    }
    return {-1, nullptr, 0};
}

int GraphicsPage::GraphicsProvider::RowValue(int row) const
{
    if (!m_page)
        return 0;
    const GraphicsConfig& c = m_page->m_cfg;
    switch (row)
    {
        case kRowPreset:
            return (c.qualityPreset >= GraphicsConfig::PresetLow && c.qualityPreset <= GraphicsConfig::PresetCustom)
                       ? (int)c.qualityPreset
                       : (int)GraphicsConfig::PresetCustom;
        case kRowTerrain:
            // Map TierLow..TierUltra (1..4) to options-array index 0..3.
            return (int)c.terrainDetail - (int)GraphicsConfig::TierLow;
        case kRowObjectLod:
            return (int)c.objectLod - (int)GraphicsConfig::TierLow;
        case kRowShadow:
            // TierOff (0) → 0; TierLow..High (1..3) → 1..3.
            return (int)c.shadowQuality;
        case kRowParticles:
        {
            // Off (0) → 0; Low (1) → 1; High (3) → 2.
            switch (c.particlesQuality)
            {
                case GraphicsConfig::TierOff:
                    return 0;
                case GraphicsConfig::TierLow:
                    return 1;
                case GraphicsConfig::TierHigh:
                    return 2;
                default:
                    return 2;
            }
        }
        case kRowVsync:
            return (int)c.vsync;
        case kRowFpsCap:
            return GraphicsPage::FpsCapValueToIndex(c.fpsCap);
        case kRowBrightness:
            return GraphicsPage::BrightnessToSlider(c.brightness);
        case kRowGamma:
            return GraphicsPage::GammaToSlider(c.gamma);
    }
    return 0;
}

// Brightness is a gain and gamma an exponent, so both rows print their value;
// the bar carries the position in the range.
const char* GraphicsPage::GraphicsProvider::SliderValueText(int row) const
{
    if (!m_page || (row != kRowBrightness && row != kRowGamma))
        return nullptr;

    const GraphicsConfig& c = m_page->m_cfg;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f", row == kRowBrightness ? c.brightness : c.gamma);
    m_sliderValueText = buffer;
    return m_sliderValueText.c_str();
}

void GraphicsPage::GraphicsProvider::SetRowValue(int row, int v)
{
    if (!m_page)
        return;
    GraphicsConfig& c = m_page->m_cfg;
    bool tierTouched = false;
    switch (row)
    {
        case kRowPreset:
            // Selecting a preset stamps the four tier rows.  Custom
            // is a derived state — picking it from the dropdown is a
            // no-op (the UI never explicitly chooses Custom).
            if (v >= GraphicsConfig::PresetLow && v <= GraphicsConfig::PresetUltra)
            {
                c.qualityPreset = static_cast<GraphicsConfig::Preset>(v);
                c.ApplyPresetToTiers(c.qualityPreset);
            }
            break;
        case kRowTerrain:
            if (v >= 0 && v < 4)
            {
                c.terrainDetail = static_cast<GraphicsConfig::Tier>((int)GraphicsConfig::TierLow + v);
                tierTouched = true;
            }
            break;
        case kRowObjectLod:
            if (v >= 0 && v < 4)
            {
                c.objectLod = static_cast<GraphicsConfig::Tier>((int)GraphicsConfig::TierLow + v);
                tierTouched = true;
            }
            break;
        case kRowShadow:
            // Index 0..3 → TierOff..TierHigh.
            if (v >= 0 && v < 4)
            {
                c.shadowQuality = static_cast<GraphicsConfig::Tier>(v);
                tierTouched = true;
            }
            break;
        case kRowParticles:
            // 0 → Off, 1 → Low, 2 → High.
            switch (v)
            {
                case 0:
                    c.particlesQuality = GraphicsConfig::TierOff;
                    tierTouched = true;
                    break;
                case 1:
                    c.particlesQuality = GraphicsConfig::TierLow;
                    tierTouched = true;
                    break;
                case 2:
                    c.particlesQuality = GraphicsConfig::TierHigh;
                    tierTouched = true;
                    break;
            }
            break;
        case kRowVsync:
            if (v >= 0 && v <= 2)
                c.vsync = static_cast<GraphicsConfig::VsyncMode>(v);
            break;
        case kRowFpsCap:
            if (v >= 0 && v < (int)(sizeof(kFpsCapValues) / sizeof(int)))
                c.fpsCap = kFpsCapValues[v];
            break;
        case kRowBrightness:
            c.brightness = GraphicsPage::SliderToBrightness(v);
            break;
        case kRowGamma:
            c.gamma = GraphicsPage::SliderToGamma(v);
            break;
    }

    // Tier change → re-derive Preset (drops to Custom on divergence).
    if (tierTouched)
        RederivePreset(c);

    // Live-apply: push the whole cfg to the engine every time, idempotent.
    ApplyGraphicsConfigToEngine(c);
}

// GraphicsPage — Mount snapshots cfg, Unmount persists it.

void GraphicsPage::Mount(OptionsShell& shell)
{
    // Read graphics.cfg from disk so the rows reflect what's currently
    // live (which the boot path put there at autodetect time, and any
    // previous Unmount may have updated).  If the file is missing the
    // instance falls back to class defaults — same as PresetUltra.
    if (!m_cfg.Load(GraphicsCfgPath()))
        m_cfg.LoadDefaults();
    // Ensure the displayed Preset is consistent with the tiers loaded
    // from disk; a hand-edited file might set preset=Ultra with mismatched
    // tiers, in which case we derive Custom.
    m_cfg.qualityPreset = m_cfg.DerivePresetFromTiers();

    RefreshLocalizedChoices();
    ScrollListPage::Mount(shell);
}

void GraphicsPage::OnReshown(OptionsShell& shell)
{
    RefreshLocalizedChoices();
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    ScrollListPage::OnReshown(shell);
}

void GraphicsPage::RefreshLocalizedChoices()
{
    m_presetLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_LOW");
    m_presetLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_MEDIUM");
    m_presetLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_HIGH");
    m_presetLabels[3] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ULTRA");
    m_presetLabels[4] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_CUSTOM");
    for (size_t i = 0; i < m_presetLabels.size(); ++i)
        m_presetCStrs[i] = m_presetLabels[i].c_str();

    m_tierFourLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_LOW");
    m_tierFourLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_MEDIUM");
    m_tierFourLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_HIGH");
    m_tierFourLabels[3] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ULTRA");
    for (size_t i = 0; i < m_tierFourLabels.size(); ++i)
        m_tierFourCStrs[i] = m_tierFourLabels[i].c_str();

    m_shadowLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_shadowLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_LOW");
    m_shadowLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_MEDIUM");
    m_shadowLabels[3] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_HIGH");
    for (size_t i = 0; i < m_shadowLabels.size(); ++i)
        m_shadowCStrs[i] = m_shadowLabels[i].c_str();

    m_particlesLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_particlesLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_LOW");
    m_particlesLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_HIGH");
    for (size_t i = 0; i < m_particlesLabels.size(); ++i)
        m_particlesCStrs[i] = m_particlesLabels[i].c_str();

    m_vsyncLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_vsyncLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ON");
    m_vsyncLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ADAPTIVE");
    for (size_t i = 0; i < m_vsyncLabels.size(); ++i)
        m_vsyncCStrs[i] = m_vsyncLabels[i].c_str();

    m_fpsCapLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_UNLIMITED");
    m_fpsCapLabels[1] = "30";
    m_fpsCapLabels[2] = "60";
    m_fpsCapLabels[3] = "90";
    m_fpsCapLabels[4] = "120";
    m_fpsCapLabels[5] = "144";
    m_fpsCapLabels[6] = "240";
    for (size_t i = 0; i < m_fpsCapLabels.size(); ++i)
        m_fpsCapCStrs[i] = m_fpsCapLabels[i].c_str();
}

void GraphicsPage::Unmount(OptionsShell& shell)
{
    // Live-apply pattern (same as AudioPage): values already flowed to
    // the engine on each row change.  Unmount just persists the final
    // state.  Any boot-time normalize-but-don't-persist values are now
    // committed since the user explicitly visited the screen.
    if (!m_cfg.Save(GraphicsCfgPath()))
        LOG_WARN(Graphics, "GraphicsPage::Unmount: failed to write graphics.cfg");

    ScrollListPage::Unmount(shell);
}

} // namespace Poseidon
