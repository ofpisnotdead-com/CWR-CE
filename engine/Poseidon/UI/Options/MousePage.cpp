#include <Poseidon/UI/Options/MousePage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <algorithm>
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
    // Nearest preset, for display only — a hand-edited non-preset mouseDpi maps to
    // the closest entry but isn't written back unless the user changes the stepper.
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
    // Mouse changes apply live — persist on unmount so the next boot
    // sees them.  The snapshot members exist for future Cancel-vs-OK
    // routing once the page grows an explicit Cancel affordance; for
    // now Close = commit (matching every other live-apply scalar page
    // in the new shell).
    InputSubsystem::Instance().SaveKeys();
    ScrollListPage::Unmount(shell);
}

const char* MousePage::MouseProvider::RowLabel(int row) const
{
    switch (row)
    {
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
        case kRowExtendedRange:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_EXTENDED_RANGE", "Advanced sensitivity range");
        case kRowSmoothing:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_SMOOTHING", "Mouse smoothing");
        case kRowAcceleration:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_ACCELERATION", "Mouse acceleration");
        case kRowAccelExponent:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_ACCEL_STRENGTH", "Acceleration strength");
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
        case kRowExtendedRange:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_EXTENDED_RANGE_DESC",
                                              "Allow sensitivity sliders to use 0.05x to 3.0x.");
        case kRowSmoothing:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_SMOOTHING_DESC",
                                              "Smooth uneven mouse input. Higher values add latency.");
        case kRowAcceleration:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_ACCELERATION_DESC",
                                              "Increase large mouse movements while keeping fine aim slower.");
        case kRowAccelExponent:
            return LocalizeStringWithFallback("STR_DISP_OPT_MOUSE_ACCEL_STRENGTH_DESC",
                                              "Acceleration curve strength. Range 1.0x to 2.0x.");
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
    switch (row)
    {
        case kRowReverseY:
            return {502, m_toggleOptions.data(), 2};
        case kRowButtonsReversed:
            return {512, m_toggleOptions.data(), 2};
        case kRowSensitivityX:
            return {522, nullptr, -1};
        case kRowSensitivityY:
            return {532, nullptr, -1};
        case kRowMouseDpi:
            return {542, kMouseDpiLabels, kMouseDpiPresetCount};
        case kRowExtendedRange:
            return {552, m_toggleOptions.data(), 2};
        case kRowSmoothing:
            return {562, nullptr, -1};
        case kRowAcceleration:
            return {572, m_toggleOptions.data(), 2};
        case kRowAccelExponent:
            return {582, nullptr, -1};
        case kRowMenuCursorScale:
            return {592, nullptr, -1};
        default:
            return {-1, nullptr, 0};
    }
}

void MousePage::MouseProvider::RefreshToggleTexts() const
{
    m_toggleText[0] = LocalizeString("STR_DISABLED");
    m_toggleText[1] = LocalizeString("STR_ENABLED");
    m_toggleOptions[0] = m_toggleText[0].c_str();
    m_toggleOptions[1] = m_toggleText[1].c_str();
}

int MousePage::MouseProvider::RowValue(int row) const
{
    auto& sub = InputSubsystem::Instance();
    switch (row)
    {
        case kRowReverseY:
            return sub.IsReverseMouse() ? 1 : 0;
        case kRowButtonsReversed:
            return sub.IsMouseButtonsReversed() ? 1 : 0;
        case kRowSensitivityX:
        {
            const auto& t = sub.GetMouseTuning();
            return MousePage::FloatRangeToPercent(sub.GetMouseSensitivityX(), t.SensMin(), t.SensMax());
        }
        case kRowSensitivityY:
        {
            const auto& t = sub.GetMouseTuning();
            return MousePage::FloatRangeToPercent(sub.GetMouseSensitivityY(), t.SensMin(), t.SensMax());
        }
        case kRowMouseDpi:
        {
            const auto& t = sub.GetMouseTuning();
            return MousePage::DpiToIndex(t.dpiNormalize, t.mouseDpi);
        }
        case kRowExtendedRange:
            return sub.GetMouseTuning().extendedRange ? 1 : 0;
        case kRowSmoothing:
            return MousePage::FloatRangeToPercent(sub.GetMouseTuning().smoothing, 0.0f, 0.95f);
        case kRowAcceleration:
            return sub.GetMouseTuning().acceleration ? 1 : 0;
        case kRowAccelExponent:
            return MousePage::FloatRangeToPercent(sub.GetMouseTuning().accelExponent, 1.0f, 2.0f);
        case kRowMenuCursorScale:
            return MousePage::FloatRangeToPercent(sub.GetMouseTuning().menuCursorScale, 0.1f, 4.0f);
        default:
            return 0;
    }
}

void MousePage::MouseProvider::SetRowValue(int row, int value)
{
    auto& sub = InputSubsystem::Instance();
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
        {
            const auto& t = sub.GetMouseTuning();
            sub.SetMouseSensitivityX(MousePage::PercentToFloatRange(value, t.SensMin(), t.SensMax()));
            return;
        }
        case kRowSensitivityY:
        {
            const auto& t = sub.GetMouseTuning();
            sub.SetMouseSensitivityY(MousePage::PercentToFloatRange(value, t.SensMin(), t.SensMax()));
            return;
        }
        case kRowMouseDpi:
        {
            auto& t = sub.GetMouseTuning();
            if (value <= 0 || value >= kMouseDpiPresetCount)
            {
                t.dpiNormalize = false; // "Off"
            }
            else
            {
                t.dpiNormalize = true;
                t.mouseDpi = kMouseDpiPresets[value];
            }
            return;
        }
        case kRowExtendedRange:
        {
            auto& t = sub.GetMouseTuning();
            if (value < 0 || value >= 2)
                return;
            t.extendedRange = value != 0;
            sub.SetMouseSensitivityX(std::clamp(sub.GetMouseSensitivityX(), t.SensMin(), t.SensMax()));
            sub.SetMouseSensitivityY(std::clamp(sub.GetMouseSensitivityY(), t.SensMin(), t.SensMax()));
            return;
        }
        case kRowSmoothing:
            sub.GetMouseTuning().smoothing = MousePage::PercentToFloatRange(value, 0.0f, 0.95f);
            return;
        case kRowAcceleration:
            if (value < 0 || value >= 2)
                return;
            sub.GetMouseTuning().acceleration = value != 0;
            return;
        case kRowAccelExponent:
            sub.GetMouseTuning().accelExponent = MousePage::PercentToFloatRange(value, 1.0f, 2.0f);
            return;
        case kRowMenuCursorScale:
            sub.GetMouseTuning().menuCursorScale = MousePage::PercentToFloatRange(value, 0.1f, 4.0f);
            return;
    }
}

} // namespace Poseidon
