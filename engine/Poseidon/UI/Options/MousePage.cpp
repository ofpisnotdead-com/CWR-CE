#include <Poseidon/UI/Options/MousePage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <algorithm>
#include <cstdio>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

const char* MousePage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CTL_MOUSE");
}

const char* MousePage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* MousePage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

int MousePage::FloatRangeToPercent(float value, float lo, float hi)
{
    if (hi <= lo)
        return 0;
    const float pct = (value - lo) / (hi - lo) * 100.0f;
    return std::clamp(static_cast<int>(pct + 0.5f), 0, 100);
}

float MousePage::PercentToFloatRange(int percent, float lo, float hi)
{
    if (hi <= lo)
        return lo;
    return lo + (std::clamp(percent, 0, 100) / 100.0f) * (hi - lo);
}

int MousePage::SensitivityToPercent(float value)
{
    return FloatRangeToPercent(value, 0.5f, 2.0f);
}

float MousePage::PercentToSensitivity(int percent)
{
    return PercentToFloatRange(percent, 0.5f, 2.0f);
}

int MousePage::DpiToIndex(bool normalize, int mouseDpi)
{
    if (!normalize)
        return 0; // Off
    int best = 1;
    int bestDiff = 1 << 30;
    for (int i = 1; i < kMouseDpiPresetCount; ++i)
    {
        int d = mouseDpi - kMouseDpiPresets[i];
        if (d < 0)
            d = -d;
        if (d < bestDiff)
        {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

int MousePage::IndexToDpi(int index)
{
    if (index <= 0 || index >= kMouseDpiPresetCount)
        return 0; // Off sentinel
    return kMouseDpiPresets[index];
}

void MousePage::Mount(OptionsShell& shell)
{
    auto& sub = InputSubsystem::Instance();
    m_savedReverseY = sub.IsReverseMouse();
    m_savedButtonsReversed = sub.IsMouseButtonsReversed();
    m_savedSensitivityX = sub.GetMouseSensitivityX();
    m_savedSensitivityY = sub.GetMouseSensitivityY();
    m_savedDpiNormalize = sub.GetMouseTuning().dpiNormalize;
    m_savedMouseDpi = sub.GetMouseTuning().mouseDpi;
    ScrollListPage::Mount(shell);
}

void MousePage::OnReshown(OptionsShell& shell)
{
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    ScrollListPage::OnReshown(shell);
}

void MousePage::Unmount(OptionsShell& shell)
{
    InputSubsystem::Instance().SaveKeys();
    ScrollListPage::Unmount(shell);
}

void MousePage::MouseProvider::RefreshToggleTexts() const
{
    m_toggleText[0] = LocalizeString("STR_DISABLED");
    m_toggleText[1] = LocalizeString("STR_ENABLED");
    m_toggleOptions[0] = m_toggleText[0].c_str();
    m_toggleOptions[1] = m_toggleText[1].c_str();
}

void MousePage::MouseProvider::RefreshAimModeTexts() const
{
    m_aimModeText[0] = LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE_CLASSIC", "Classic");
    m_aimModeText[1] = LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE_REDUCED", "Reduced");
    m_aimModeText[2] = LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE_DIRECT", "Direct Aim");
    m_aimModeText[3] = LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE_CUSTOM", "Custom");
    for (int i = 0; i < 4; ++i)
        m_aimModeOptions[i] = m_aimModeText[i].c_str();
}

const char* MousePage::MouseProvider::RowLabel(int row) const
{
    switch (row)
    {
        case kRowBasicHeader:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_SECTION_MOUSE", "Mouse");
        case kRowReverseY:
            return LocalizeString("STR_DISP_OPT_MOUSE_REVERSE_Y");
        case kRowButtonsReversed:
            return LocalizeString("STR_DISP_OPT_MOUSE_SWAP_BUTTONS");
        case kRowSensitivityX:
            return LocalizeString("STR_DISP_OPT_MOUSE_SENSITIVITY_X");
        case kRowSensitivityY:
            return LocalizeString("STR_DISP_OPT_MOUSE_SENSITIVITY_Y");
        case kRowMouseDpi:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_DPI", "Mouse DPI");
        case kRowAdvancedHeader:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_SECTION_ADVANCED", "Advanced Mouse");
        case kRowInputDeadZone:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_INPUT_DEAD_ZONE", "Input dead zone");
        case kRowSmoothing:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_SMOOTHING", "Mouse smoothing");
        case kRowAcceleration:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_ACCELERATION", "Mouse acceleration");
        case kRowAimMode:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE", "Aim mode");
        case kRowFreeAimZoneX:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_FREE_AIM_ZONE_X", "Free-aim zone X");
        case kRowFreeAimZoneY:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_FREE_AIM_ZONE_Y", "Free-aim zone Y");
        case kRowMenuCursorScale:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_MENU_CURSOR_SPEED", "Menu cursor speed");
        default:
            return "";
    }
}

const char* MousePage::MouseProvider::RowDescription(int row) const
{
    switch (row)
    {
        case kRowReverseY:
            return LocalizeString("STR_DISP_OPT_MOUSE_REVERSE_Y_DESC");
        case kRowButtonsReversed:
            return LocalizeString("STR_DISP_OPT_MOUSE_SWAP_BUTTONS_DESC");
        case kRowSensitivityX:
            return LocalizeString("STR_DISP_OPT_MOUSE_SENSITIVITY_X_DESC");
        case kRowSensitivityY:
            return LocalizeString("STR_DISP_OPT_MOUSE_SENSITIVITY_Y_DESC");
        case kRowMouseDpi:
            return LocalizeStringWithFallback(
                "STR_DISP_OPT_MOUSE_DPI_DESC",
                "Set this to your mouse's DPI (or close). Fine-tune with sensitivity for a smoother feel.");
        case kRowInputDeadZone:
            return LocalizeStringWithFallback(
                "STR_DISP_OPT_MOUSE_INPUT_DEAD_ZONE_DESC",
                "Ignore very small mouse movement before it affects aiming. Range 0.00 to 2.00 normalized counts.");
        case kRowSmoothing:
            return LocalizeStringWithFallback(
                "STR_DISP_OPT_MOUSE_SMOOTHING_DESC",
                "Smooth mouse movement over time. Higher values feel steadier but add delay.");
        case kRowAcceleration:
            return LocalizeStringWithFallback(
                "STR_DISP_OPT_MOUSE_ACCELERATION_DESC",
                "Increase turn speed for faster mouse movement. 0% disables acceleration.");
        case kRowAimMode:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_AIM_MODE_DESC",
                                              "Choose how much free-aim movement is allowed before the view turns. "
                                              "Custom unlocks the free-aim zone sliders.");
        case kRowFreeAimZoneX:
            return LocalizeStringWithFallback(
                "STR_DISP_OPT_MOUSE_FREE_AIM_ZONE_X_DESC",
                "Horizontal free-aim zone in normalized cursor units. Range 0.00 to 0.80.");
        case kRowFreeAimZoneY:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_FREE_AIM_ZONE_Y_DESC",
                                              "Vertical free-aim zone in normalized cursor units. Range 0.00 to 0.50.");
        case kRowMenuCursorScale:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_MENU_CURSOR_SPEED_DESC",
                                              "Menu cursor speed separate from aiming. Range 0.1x to 4.0x.");
        default:
            return "";
    }
}

OptionsScrollList::RowDef MousePage::MouseProvider::RowFor(int row) const
{
    RefreshToggleTexts();
    RefreshAimModeTexts();
    switch (row)
    {
        case kRowReverseY:
            return {512, m_toggleOptions.data(), 2};
        case kRowButtonsReversed:
            return {522, m_toggleOptions.data(), 2};
        case kRowSensitivityX:
            return {532, nullptr, -1};
        case kRowSensitivityY:
            return {542, nullptr, -1};
        case kRowMouseDpi:
            return {552, kMouseDpiLabels, kMouseDpiPresetCount};
        case kRowInputDeadZone:
            return {572, nullptr, -1};
        case kRowSmoothing:
            return {582, nullptr, -1};
        case kRowAcceleration:
            return {592, nullptr, -1};
        case kRowAimMode:
            return {602, m_aimModeOptions.data(), 4};
        case kRowFreeAimZoneX:
            return {612, nullptr, -1};
        case kRowFreeAimZoneY:
            return {622, nullptr, -1};
        case kRowMenuCursorScale:
            return {632, nullptr, -1};
        default:
            return {-1, nullptr, 0};
    }
}

OptionsScrollList::Kind MousePage::MouseProvider::RowKind(int row) const
{
    if (row == kRowBasicHeader || row == kRowAdvancedHeader)
        return OptionsScrollList::KindHeader;
    return OptionsScrollList::Provider::RowKind(row);
}

bool MousePage::MouseProvider::IsDisabled(int row) const
{
    if (row == kRowFreeAimZoneX || row == kRowFreeAimZoneY)
        return InputSubsystem::Instance().GetMouseTuning().aimMode != MouseAimMode::Custom;
    return false;
}

int MousePage::MouseProvider::RowValue(int row) const
{
    auto& sub = InputSubsystem::Instance();
    const auto& t = sub.GetMouseTuning();
    switch (row)
    {
        case kRowReverseY:
            return sub.IsReverseMouse() ? 1 : 0;
        case kRowButtonsReversed:
            return sub.IsMouseButtonsReversed() ? 1 : 0;
        case kRowSensitivityX:
            return MousePage::SensitivityToPercent(sub.GetMouseSensitivityX());
        case kRowSensitivityY:
            return MousePage::SensitivityToPercent(sub.GetMouseSensitivityY());
        case kRowMouseDpi:
            return MousePage::DpiToIndex(t.dpiNormalize, t.mouseDpi);
        case kRowInputDeadZone:
            return MousePage::FloatRangeToPercent(t.inputDeadZone, MouseTuning::kInputDeadZoneMin,
                                                  MouseTuning::kInputDeadZoneMax);
        case kRowSmoothing:
            return MousePage::FloatRangeToPercent(t.smoothing, 0.0f, 0.95f);
        case kRowAcceleration:
            return t.acceleration ? MousePage::FloatRangeToPercent(t.accelExponent, 1.0f, 2.0f) : 0;
        case kRowAimMode:
            return std::clamp(static_cast<int>(t.aimMode), 0, 3);
        case kRowFreeAimZoneX:
            return MousePage::FloatRangeToPercent(t.freeAimZoneX, 0.0f, MouseTuning::kClassicFreeAimZoneX);
        case kRowFreeAimZoneY:
            return MousePage::FloatRangeToPercent(t.freeAimZoneY, 0.0f, MouseTuning::kClassicFreeAimZoneY);
        case kRowMenuCursorScale:
            return MousePage::FloatRangeToPercent(t.menuCursorScale, 0.1f, 4.0f);
        default:
            return 0;
    }
}

void MousePage::MouseProvider::SetRowValue(int row, int value)
{
    auto& sub = InputSubsystem::Instance();
    auto& t = sub.GetMouseTuning();
    switch (row)
    {
        case kRowReverseY:
            if (value < 0 || value >= 2)
                return;
            sub.SetReverseMouse(value != 0);
            return;
        case kRowButtonsReversed:
            if (value < 0 || value >= 2)
                return;
            sub.SetMouseButtonsReversed(value != 0);
            return;
        case kRowSensitivityX:
            sub.SetMouseSensitivityX(MousePage::PercentToSensitivity(value));
            return;
        case kRowSensitivityY:
            sub.SetMouseSensitivityY(MousePage::PercentToSensitivity(value));
            return;
        case kRowMouseDpi:
            if (value <= 0 || value >= kMouseDpiPresetCount)
            {
                t.dpiNormalize = false;
            }
            else
            {
                t.dpiNormalize = true;
                t.mouseDpi = kMouseDpiPresets[value];
            }
            return;
        case kRowInputDeadZone:
            t.inputDeadZone =
                MousePage::PercentToFloatRange(value, MouseTuning::kInputDeadZoneMin, MouseTuning::kInputDeadZoneMax);
            return;
        case kRowSmoothing:
            t.smoothing = MousePage::PercentToFloatRange(value, 0.0f, 0.95f);
            return;
        case kRowAcceleration:
            value = std::clamp(value, 0, 100);
            t.acceleration = value > 0;
            t.accelExponent = value > 0 ? MousePage::PercentToFloatRange(value, 1.0f, 2.0f) : 1.0f;
            return;
        case kRowAimMode:
        {
            if (value < 0 || value > 3)
                return;
            t.aimMode = static_cast<MouseAimMode>(value);
            if (t.aimMode != MouseAimMode::Custom)
                MouseTuning::AimZonesForMode(t.aimMode, t.freeAimZoneX, t.freeAimZoneY);
            return;
        }
        case kRowFreeAimZoneX:
            if (t.aimMode == MouseAimMode::Custom)
                t.freeAimZoneX = MousePage::PercentToFloatRange(value, 0.0f, MouseTuning::kClassicFreeAimZoneX);
            return;
        case kRowFreeAimZoneY:
            if (t.aimMode == MouseAimMode::Custom)
                t.freeAimZoneY = MousePage::PercentToFloatRange(value, 0.0f, MouseTuning::kClassicFreeAimZoneY);
            return;
        case kRowMenuCursorScale:
            t.menuCursorScale = MousePage::PercentToFloatRange(value, 0.1f, 4.0f);
            return;
    }
}

const char* MousePage::MouseProvider::SliderValueText(int row) const
{
    auto& sub = InputSubsystem::Instance();
    const auto& t = sub.GetMouseTuning();
    char buf[32] = {};
    switch (row)
    {
        case kRowSensitivityX:
            std::snprintf(buf, sizeof(buf), "%.2fx", sub.GetMouseSensitivityX());
            break;
        case kRowSensitivityY:
            std::snprintf(buf, sizeof(buf), "%.2fx", sub.GetMouseSensitivityY());
            break;
        case kRowInputDeadZone:
            std::snprintf(buf, sizeof(buf), "%.2f", t.inputDeadZone);
            break;
        case kRowSmoothing:
            std::snprintf(buf, sizeof(buf), "%d%%", RowValue(row));
            break;
        case kRowAcceleration:
            std::snprintf(buf, sizeof(buf), "%d%%", RowValue(row));
            break;
        case kRowFreeAimZoneX:
            std::snprintf(buf, sizeof(buf), "%.2f", t.freeAimZoneX);
            break;
        case kRowFreeAimZoneY:
            std::snprintf(buf, sizeof(buf), "%.2f", t.freeAimZoneY);
            break;
        case kRowMenuCursorScale:
            std::snprintf(buf, sizeof(buf), "%.2fx", t.menuCursorScale);
            break;
        default:
            return nullptr;
    }
    m_sliderValueText = buf;
    return m_sliderValueText.c_str();
}

} // namespace Poseidon
