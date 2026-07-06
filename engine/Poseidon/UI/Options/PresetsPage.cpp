#include <Poseidon/UI/Options/ConfirmPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/UI/Options/PresetsPage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

using namespace Poseidon;
namespace Poseidon
{
extern Input GInput;
}

const char* PresetsPage::TitleText() const
{
    return LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS", "Reset All");
}

const char* PresetsPage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* PresetsPage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

void PresetsPage::OnReshown(OptionsShell& shell)
{
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    ScrollListPage::OnReshown(shell);
}

const char* PresetsPage::PresetsProvider::RowLabel(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS_PRESET", "Preset");
        case kRowApplyPreset:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS_APPLY", "Reset all to preset");
        default:
            return "";
    }
}

const char* PresetsPage::PresetsProvider::RowDescription(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS_PRESET_DESC",
                                              "Choose which preset to reset controls to.");
        case kRowApplyPreset:
            return LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS_APPLY_DESC",
                                              "Apply the selected preset to all controls.");
        default:
            return "";
    }
}

OptionsScrollList::RowDef PresetsPage::PresetsProvider::RowFor(int row) const
{
    RefreshStepperTexts();
    switch (row)
    {
        case kRowPreset:
            return {702, m_stepperOptions.data(), (int)m_stepperOptions.size()};
        default:
            return {-1, nullptr, 0};
    }
}

OptionsScrollList::Kind PresetsPage::PresetsProvider::RowKind(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return OptionsScrollList::KindStepper;
        case kRowApplyPreset:
            return OptionsScrollList::KindAction;
        default:
            return OptionsScrollList::Provider::RowKind(row);
    }
}

void PresetsPage::PresetsProvider::RefreshStepperTexts() const
{
    m_stepperText[0] = LocalizeString("STR_DISP_DEFAULT");
    m_stepperText[1] = LocalizeStringWithFallback("STR_DISP_OPT_CTL_PRESETS_A3_LEGACY", "A3 Legacy");
    m_stepperOptions[0] = m_stepperText[0].c_str();
    m_stepperOptions[1] = m_stepperText[1].c_str();
}

int PresetsPage::PresetsProvider::RowValue(int row) const
{
    switch (row)
    {
        case kRowPreset:
            return m_preset;
        default:
            return 0;
    }
}

void PresetsPage::PresetsProvider::SetRowValue(int row, int value)
{
    switch (row)
    {
        case kRowPreset:
            if (value < 0 || value >= 2)
                return;
            m_preset = value;
            return;
        default:
            return;
    }
}

void PresetsPage::PresetsProvider::OnRowAction(int row, Display& host)
{
    if (row != kRowApplyPreset)
        return;
    if (m_preset < 0 || m_preset > 1)
        return;

    auto* shell = dynamic_cast<OptionsShell*>(&host);
    if (!shell)
        return;

    // New variable is needed to capture in onYes
    int preset = m_preset;

    char title[160];
    snprintf(
        title, sizeof(title),
        (const char*)LocalizeStringWithFallback("STR_DISP_OPT_CTL_RESET_PRESET", "Reset all to controls to \"%s\"?"),
        m_stepperOptions[preset]);
    char confirm[160];
    snprintf(confirm, sizeof(confirm),
             (const char*)LocalizeStringWithFallback("STR_DISP_OPT_CTL_CONFIRM_PRESET", "Reset all to \"%s\"?"),
             m_stepperOptions[preset]);

    auto onYes = [shell, preset]()
    {
        auto& sub = InputSubsystem::Instance();
        for (int c = 0; c < ControlsCategoryCount; ++c)
            switch (preset)
            {
                case 0:
                    sub.ResetCategoryDefaults((ControlsCategory)c);
                case 1:
                    sub.ResetCategoryA3Legacy((ControlsCategory)c);
            }
        sub.SaveKeys();

        shell->PopPage();
    };
    shell->PushPage(std::make_unique<ConfirmPage>(
        title, std::string((const char*)LocalizeString("STR_DISP_OPT_CONFIRM_RESET_BODY")), std::move(onYes), confirm,
        std::string((const char*)LocalizeString("STR_DISP_OPT_CAP_CANCEL"))));
}
