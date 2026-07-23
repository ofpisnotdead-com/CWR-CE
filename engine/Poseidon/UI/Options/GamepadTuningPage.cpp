#include <Poseidon/UI/Options/GamepadTuningPage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <algorithm>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;
namespace Poseidon
{
extern Input GInput;
}

const char* GamepadTuningPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_TUNING");
}

const char* GamepadTuningPage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* GamepadTuningPage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

void GamepadTuningPage::OnReshown(OptionsShell& shell)
{
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    ScrollListPage::OnReshown(shell);
}

void GamepadTuningPage::Unmount(OptionsShell& shell)
{
    InputSubsystem::Instance().SaveKeys();
    ScrollListPage::Unmount(shell);
}

const char* GamepadTuningPage::GamepadProvider::RowLabel(int row) const
{
    switch (row)
    {
        case kRowEnabled:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_ENABLED");
        case kRowReverseYStick:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_GAMEPAD_REVERSE_Y", "Y-axis inversion");
        case kRowDeadzoneStick:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_STICK_DEADZONE");
        case kRowDeadzoneTrigger:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_TRIGGER_DEADZONE");
        case kRowLookSensitivity:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_LOOK_SENSITIVITY");
        default:
            return "";
    }
}

const char* GamepadTuningPage::GamepadProvider::RowDescription(int row) const
{
    switch (row)
    {
        case kRowEnabled:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_ENABLED_DESC");
        case kRowReverseYStick:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_GAMEPAD_REVERSE_Y_DESC",
                                              "Invert vertical stick motion - pushing the stick forward looks down.");
        case kRowDeadzoneStick:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_STICK_DEADZONE_DESC");
        case kRowDeadzoneTrigger:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_TRIGGER_DEADZONE_DESC");
        case kRowLookSensitivity:
            return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_LOOK_SENSITIVITY_DESC");
        default:
            return "";
    }
}

OptionsScrollList::RowDef GamepadTuningPage::GamepadProvider::RowFor(int row) const
{
    RefreshToggleTexts();
    switch (row)
    {
        case kRowEnabled:
            return {602, m_toggleOptions.data(), 2};
        case kRowReverseYStick:
            return {642, m_toggleOptions.data(), 2};
        case kRowDeadzoneStick:
            return {612, nullptr, -1};
        case kRowDeadzoneTrigger:
            return {622, nullptr, -1};
        case kRowLookSensitivity:
            return {632, nullptr, -1};
        default:
            return {-1, nullptr, 0};
    }
}

void GamepadTuningPage::GamepadProvider::RefreshToggleTexts() const
{
    m_toggleText[0] = LocalizeString("STR_DISABLED");
    m_toggleText[1] = LocalizeString("STR_ENABLED");
    m_toggleOptions[0] = m_toggleText[0].c_str();
    m_toggleOptions[1] = m_toggleText[1].c_str();
}

int GamepadTuningPage::DeadzoneToPercent(float v)
{
    // Engine deadzone range 0.0..0.5 → percent 0..100 (linear).
    int p = (int)(v / 0.5f * 100.0f + 0.5f);
    return std::clamp(p, 0, 100);
}

float GamepadTuningPage::PercentToDeadzone(int pct)
{
    int p = std::clamp(pct, 0, 100);
    return (p / 100.0f) * 0.5f;
}

int GamepadTuningPage::LookSensitivityToPercent(float v)
{
    // Engine sensitivity 0.1..5.0 → percent 0..100 (linear over a
    // 4.9-wide range; 1.0x default lands at ~18%).
    int p = (int)((v - 0.1f) / 4.9f * 100.0f + 0.5f);
    return std::clamp(p, 0, 100);
}

float GamepadTuningPage::PercentToLookSensitivity(int pct)
{
    int p = std::clamp(pct, 0, 100);
    return 0.1f + (p / 100.0f) * 4.9f;
}

int GamepadTuningPage::GamepadProvider::RowValue(int row) const
{
    switch (row)
    {
        case kRowEnabled:
            return GInput.gamepad.enabled ? 1 : 0;
        case kRowReverseYStick:
            return GInput.gamepad.reverseYStick ? 1 : 0;
        case kRowDeadzoneStick:
            return GamepadTuningPage::DeadzoneToPercent(GInput.gamepad.deadzoneStick);
        case kRowDeadzoneTrigger:
            return GamepadTuningPage::DeadzoneToPercent(GInput.gamepad.deadzoneTrigger);
        case kRowLookSensitivity:
            return GamepadTuningPage::LookSensitivityToPercent(GInput.gamepad.lookSensitivity);
        default:
            return 0;
    }
}

void GamepadTuningPage::GamepadProvider::SetRowValue(int row, int value)
{
    switch (row)
    {
        case kRowEnabled:
            if (value < 0 || value >= 2)
                return;
            GInput.gamepad.enabled = (value != 0);
            return;
        case kRowReverseYStick:
            if (value < 0 || value >= 2)
                return;
            GInput.gamepad.reverseYStick = (value != 0);
            return;
        case kRowDeadzoneStick:
            GInput.gamepad.deadzoneStick = GamepadTuningPage::PercentToDeadzone(value);
            return;
        case kRowDeadzoneTrigger:
            GInput.gamepad.deadzoneTrigger = GamepadTuningPage::PercentToDeadzone(value);
            return;
        case kRowLookSensitivity:
            GInput.gamepad.lookSensitivity = GamepadTuningPage::PercentToLookSensitivity(value);
            return;
    }
}
