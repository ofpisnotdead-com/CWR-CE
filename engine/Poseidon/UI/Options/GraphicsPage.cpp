#include <Poseidon/UI/Options/GraphicsPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
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
// labelEn / descEn render when the key is missing from the loaded data package.
struct GraphicsRowText
{
    const char* label;
    const char* desc;
    const char* labelEn;
    const char* descEn;
};
const GraphicsRowText kRows[] = {
    {"STR_DISP_MAIN_OPT_GRAPHICS_QUALITY_PRESET", "STR_DISP_MAIN_OPT_GRAPHICS_QUALITY_PRESET_DESC", "Quality Preset",
     "Sets the four quality tiers below to a known bundle. Touching any tier row drops the preset to Custom."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_TERRAIN_DETAIL", "STR_DISP_MAIN_OPT_GRAPHICS_TERRAIN_DETAIL_DESC", "Terrain Detail",
     "Terrain mesh density. Lower means a coarser ground silhouette but cheaper rendering. Extreme is not recommended "
     "and is not fully compatible with the original game."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_OBJECT_LOD", "STR_DISP_MAIN_OPT_GRAPHICS_OBJECT_LOD_DESC", "Object LOD",
     "Bias for entity LOD selection. Higher means finer geometry at the same distance."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_SHADOW_QUALITY", "STR_DISP_MAIN_OPT_GRAPHICS_SHADOW_QUALITY_DESC", "Shadow Quality",
     "Whether dynamic objects and vehicles cast shadows."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_PARTICLES", "STR_DISP_MAIN_OPT_GRAPHICS_PARTICLES_DESC", "Particles & Volumetrics",
     "Cloudlets smoke dust and muzzle flashes."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_VSYNC", "STR_DISP_MAIN_OPT_GRAPHICS_VSYNC_DESC", "VSync",
     "Synchronise frame presentation to the monitor refresh. Adaptive falls back to On when the GPU cannot keep up."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_FPS_CAP", "STR_DISP_MAIN_OPT_GRAPHICS_FPS_CAP_DESC", "FPS Cap",
     "Limits how fast frames are drawn. Native follows your monitor's refresh rate. Unlimited keeps a 300 FPS "
     "ceiling."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_BRIGHTNESS", "STR_DISP_MAIN_OPT_GRAPHICS_BRIGHTNESS_DESC", "Brightness",
     "Uniform multiplier in the post pass."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_GAMMA", "STR_DISP_MAIN_OPT_GRAPHICS_GAMMA_DESC", "Gamma", "Display LUT gamma curve."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_ADVANCED", "", "Advanced", ""},
    {"STR_DISP_MAIN_OPT_GRAPHICS_ANTIALIASING", "STR_DISP_MAIN_OPT_GRAPHICS_ANTIALIASING_DESC", "Anti-aliasing",
     "Multisample anti-aliasing on the frame target. Higher sample counts smooth polygon edges at a GPU cost."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_SUPERSAMPLING", "STR_DISP_MAIN_OPT_GRAPHICS_SUPERSAMPLING_DESC", "Supersampling",
     "Renders the whole frame above window resolution and downsamples it. The strongest cure for sub-pixel shimmer "
     "and the most expensive."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_MULTITEXTURING", "STR_DISP_MAIN_OPT_GRAPHICS_MULTITEXTURING_DESC", "Multitexturing",
     "Detail and specular texture stages on terrain and objects. Off falls back to the base texture like the "
     "original compatibility switch."},
    {"STR_DISP_MAIN_OPT_GRAPHICS_NIGHT_EYE", "STR_DISP_MAIN_OPT_GRAPHICS_NIGHT_EYE_DESC", "Night Color Loss",
     "Simulates night vision by draining color from unlit surfaces after dark. Off keeps night scenes in full "
     "color but renders them darker."},
};
constexpr int kFpsCapValues[] = {0, 30, 60, 90, 120, 144, 240};
constexpr int kMsaaValues[] = {0, 2, 4, 8};
constexpr float kRenderScaleValues[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

// Brightness 0.4 .. 1.8 → slider 0..100; Gamma 0.5 .. 2.3 → slider 0..100.
int FloatToSlider(float v, float lo, float hi)
{
    float c = std::clamp(v, lo, hi);
    return (int)std::lround((c - lo) / (hi - lo) * 100.0f);
}
// Snapped to 0.1 so the printed value is exactly the value that gets stored.
float SliderToFloat(int s, float lo, float hi)
{
    int c = std::clamp(s, 0, 100);
    const float raw = lo + (c / 100.0f) * (hi - lo);
    return std::round(raw * 10.0f) / 10.0f;
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

int GraphicsPage::MsaaSamplesToIndex(int samples)
{
    for (int i = 0; i < (int)(sizeof(kMsaaValues) / sizeof(int)); ++i)
        if (kMsaaValues[i] == samples)
            return i;
    return 0;
}

int GraphicsPage::MsaaIndexToSamples(int index)
{
    if (index < 0 || index >= (int)(sizeof(kMsaaValues) / sizeof(int)))
        return 0;
    return kMsaaValues[index];
}

int GraphicsPage::RenderScaleToIndex(float scale)
{
    int best = 0;
    float bestDiff = std::fabs(scale - kRenderScaleValues[0]);
    for (int i = 1; i < (int)(sizeof(kRenderScaleValues) / sizeof(float)); ++i)
    {
        const float diff = std::fabs(scale - kRenderScaleValues[i]);
        if (diff < bestDiff)
        {
            best = i;
            bestDiff = diff;
        }
    }
    return best;
}

float GraphicsPage::RenderScaleIndexToValue(int index)
{
    if (index < 0 || index >= (int)(sizeof(kRenderScaleValues) / sizeof(float)))
        return 1.0f;
    return kRenderScaleValues[index];
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
    if (row < 0 || row >= kRowCount)
        return "";
    return LocalizeStringWithFallback(kRows[row].label, kRows[row].labelEn);
}

const char* GraphicsPage::GraphicsProvider::RowDescription(int row) const
{
    if (row < 0 || row >= kRowCount)
        return "";
    return LocalizeStringWithFallback(kRows[row].desc, kRows[row].descEn);
}

OptionsScrollList::RowDef GraphicsPage::GraphicsProvider::RowFor(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return {502, m_page->m_presetCStrs.data(), 5};
        case kRowTerrain:
            return {512, m_page->m_terrainCStrs.data(), 5};
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
        case kRowAdvanced:
            return {592, nullptr, 0};
        case kRowAntiAliasing:
            return {602, m_page->m_msaaCStrs.data(), 4};
        case kRowSupersampling:
            return {612, m_page->m_renderScaleCStrs.data(), 5};
        case kRowMultitexturing:
            return {622, m_page->m_offOnCStrs.data(), 2};
        case kRowNightEye:
            return {632, m_page->m_offOnCStrs.data(), 2};
    }
    return {-1, nullptr, 0};
}

OptionsScrollList::Kind GraphicsPage::GraphicsProvider::RowKind(int row) const
{
    if (row == kRowAdvanced)
        return OptionsScrollList::KindHeader;
    if (row == kRowMultitexturing || row == kRowNightEye)
        return OptionsScrollList::KindBoolean;
    return OptionsScrollList::Provider::RowKind(row);
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
        case kRowAntiAliasing:
            return GraphicsPage::MsaaSamplesToIndex(c.msaaSamples);
        case kRowSupersampling:
            return GraphicsPage::RenderScaleToIndex(c.renderScale);
        case kRowMultitexturing:
            return c.multitexturing ? 1 : 0;
        case kRowNightEye:
            return c.nightEye ? 1 : 0;
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
            if (v >= 0 && v < 5)
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
        case kRowAntiAliasing:
            c.msaaSamples = GraphicsPage::MsaaIndexToSamples(v);
            break;
        case kRowSupersampling:
            c.renderScale = GraphicsPage::RenderScaleIndexToValue(v);
            break;
        case kRowMultitexturing:
            c.multitexturing = v != 0;
            break;
        case kRowNightEye:
            c.nightEye = v != 0;
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

    m_terrainLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_LOW");
    m_terrainLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_MEDIUM");
    m_terrainLabels[2] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_HIGH");
    m_terrainLabels[3] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ULTRA");
    m_terrainLabels[4] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_EXTREME");
    for (size_t i = 0; i < m_terrainLabels.size(); ++i)
        m_terrainCStrs[i] = m_terrainLabels[i].c_str();

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

    m_msaaLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_msaaLabels[1] = "2x";
    m_msaaLabels[2] = "4x";
    m_msaaLabels[3] = "8x";
    for (size_t i = 0; i < m_msaaLabels.size(); ++i)
        m_msaaCStrs[i] = m_msaaLabels[i].c_str();

    m_renderScaleLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_renderScaleLabels[1] = "125%";
    m_renderScaleLabels[2] = "150%";
    m_renderScaleLabels[3] = "175%";
    m_renderScaleLabels[4] = "200%";
    for (size_t i = 0; i < m_renderScaleLabels.size(); ++i)
        m_renderScaleCStrs[i] = m_renderScaleLabels[i].c_str();

    m_offOnLabels[0] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_OFF");
    m_offOnLabels[1] = LocalizeString("STR_DISP_MAIN_OPT_VALUE_ON");
    for (size_t i = 0; i < m_offOnLabels.size(); ++i)
        m_offOnCStrs[i] = m_offOnLabels[i].c_str();

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

    // Multitexturing also lives in the per-profile user params that
    // Engine::LoadConfig replays on a profile switch; both stores must agree.
    if (GEngine)
        GEngine->SaveConfig();

    ScrollListPage::Unmount(shell);
}

} // namespace Poseidon
