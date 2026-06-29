#include <Poseidon/UI/Options/TouchPage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/TouchInput.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <algorithm>
#include <cmath>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
constexpr float kMinSensitivity = 0.25f;
constexpr float kMaxSensitivity = 3.0f;
constexpr float kCursorSliderGamma = 4.5075756f;

float SensitivityToUnit(float value)
{
    return std::clamp((value - kMinSensitivity) / (kMaxSensitivity - kMinSensitivity), 0.0f, 1.0f);
}

float PercentToUnit(int percent)
{
    return std::clamp(percent, 0, 100) / 100.0f;
}

float UnitToSensitivity(float unit)
{
    return kMinSensitivity + unit * (kMaxSensitivity - kMinSensitivity);
}
} // namespace

const char* TouchPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CTL_TOUCH");
}

const char* TouchPage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* TouchPage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

int TouchPage::SensitivityToPercent(float value)
{
    float pct = SensitivityToUnit(value) * 100.0f;
    return std::clamp((int)(pct + 0.5f), 0, 100);
}

float TouchPage::PercentToSensitivity(int percent)
{
    return UnitToSensitivity(PercentToUnit(percent));
}

int TouchPage::CursorSensitivityToPercent(float value)
{
    float pct = std::pow(SensitivityToUnit(value), 1.0f / kCursorSliderGamma) * 100.0f;
    return std::clamp((int)(pct + 0.5f), 0, 100);
}

float TouchPage::PercentToCursorSensitivity(int percent)
{
    return UnitToSensitivity(std::pow(PercentToUnit(percent), kCursorSliderGamma));
}

void TouchPage::OnReshown(OptionsShell& shell)
{
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    ScrollListPage::OnReshown(shell);
}

void TouchPage::Unmount(OptionsShell& shell)
{
    InputSubsystem::Instance().SaveKeys();
    ScrollListPage::Unmount(shell);
}

const char* TouchPage::TouchProvider::RowLabel(int row) const
{
    switch (row)
    {
        case kRowAimSensitivity:
            return LocalizeString("STR_DISP_OPT_TOUCH_AIM_SENSITIVITY");
        case kRowCursorSensitivity:
            return LocalizeString("STR_DISP_OPT_TOUCH_CURSOR_SENSITIVITY");
        default:
            return "";
    }
}

const char* TouchPage::TouchProvider::RowDescription(int row) const
{
    switch (row)
    {
        case kRowAimSensitivity:
            return LocalizeString("STR_DISP_OPT_TOUCH_AIM_SENSITIVITY_DESC");
        case kRowCursorSensitivity:
            return LocalizeString("STR_DISP_OPT_TOUCH_CURSOR_SENSITIVITY_DESC");
        default:
            return "";
    }
}

OptionsScrollList::RowDef TouchPage::TouchProvider::RowFor(int row) const
{
    switch (row)
    {
        case kRowAimSensitivity:
            return {642, nullptr, -1};
        case kRowCursorSensitivity:
            return {652, nullptr, -1};
        default:
            return {-1, nullptr, 0};
    }
}

int TouchPage::TouchProvider::RowValue(int row) const
{
    switch (row)
    {
        case kRowAimSensitivity:
            return TouchPage::SensitivityToPercent(TouchInput_GetAimSensitivity());
        case kRowCursorSensitivity:
            return TouchPage::CursorSensitivityToPercent(TouchInput_GetCursorSensitivity());
        default:
            return 0;
    }
}

void TouchPage::TouchProvider::SetRowValue(int row, int value)
{
    switch (row)
    {
        case kRowAimSensitivity:
            TouchInput_SetAimSensitivity(TouchPage::PercentToSensitivity(value));
            return;
        case kRowCursorSensitivity:
            TouchInput_SetCursorSensitivity(TouchPage::PercentToCursorSensitivity(value));
            return;
    }
}

} // namespace Poseidon
