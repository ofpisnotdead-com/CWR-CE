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

int MousePage::SensitivityToPercent(float value)
{
    float pct = (value - 0.5f) / 1.5f * 100.0f;
    return std::clamp((int)(pct + 0.5f), 0, 100);
}

float MousePage::PercentToSensitivity(int percent)
{
    return 0.5f + (std::clamp(percent, 0, 100) / 100.0f) * 1.5f;
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
            return MousePage::SensitivityToPercent(sub.GetMouseSensitivityX());
        case kRowSensitivityY:
            return MousePage::SensitivityToPercent(sub.GetMouseSensitivityY());
        case kRowMouseDpi:
        {
            const auto& t = sub.GetMouseTuning();
            return MousePage::DpiToIndex(t.dpiNormalize, t.mouseDpi);
        }
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
            sub.SetMouseSensitivityX(MousePage::PercentToSensitivity(value));
            return;
        case kRowSensitivityY:
            sub.SetMouseSensitivityY(MousePage::PercentToSensitivity(value));
            return;
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
    }
}

} // namespace Poseidon
