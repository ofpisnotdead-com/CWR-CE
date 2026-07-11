#include <Poseidon/UI/Options/DifficultyPage.hpp>

#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Global.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

const char* DifficultyPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPTIONS_DIFFICULTY");
}

const char* DifficultyPage::CloseLabel()
{
    return LocalizeString("STR_DISP_CLOSE");
}

const char* DifficultyPage::CloseDescription()
{
    return LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC");
}

void DifficultyPage::Mount(OptionsShell& shell)
{
    m_difficulty.SetPage(this);
    UserConfig_LoadDifficulties(USER_CONFIG);
    for (int i = 0; i < DTN; ++i)
    {
        m_cadetDifficulty[i] = USER_CONFIG.cadetDifficulty[i];
        m_veteranDifficulty[i] = USER_CONFIG.veteranDifficulty[i];
    }
    m_selectedMode = USER_CONFIG.easyMode ? 0 : 1;
    m_modeLabels[0] = LocalizeString("STR_DISP_DIFF_CADET");
    m_modeLabels[1] = LocalizeString("STR_DISP_DIFF_VETERAN");
    m_modeOptions[0] = m_modeLabels[0].c_str();
    m_modeOptions[1] = m_modeLabels[1].c_str();
    m_toggleLabels[0] = LocalizeString("STR_DISABLED");
    m_toggleLabels[1] = LocalizeString("STR_ENABLED");
    m_toggleOptions[0] = m_toggleLabels[0].c_str();
    m_toggleOptions[1] = m_toggleLabels[1].c_str();
    ScrollListPage::Mount(shell);
}

void DifficultyPage::OnReshown(OptionsShell& shell)
{
    m_provider.SetCloseTexts(CloseLabel(), CloseDescription());
    m_modeLabels[0] = LocalizeString("STR_DISP_DIFF_CADET");
    m_modeLabels[1] = LocalizeString("STR_DISP_DIFF_VETERAN");
    m_modeOptions[0] = m_modeLabels[0].c_str();
    m_modeOptions[1] = m_modeLabels[1].c_str();
    m_toggleLabels[0] = LocalizeString("STR_DISABLED");
    m_toggleLabels[1] = LocalizeString("STR_ENABLED");
    m_toggleOptions[0] = m_toggleLabels[0].c_str();
    m_toggleOptions[1] = m_toggleLabels[1].c_str();
    ScrollListPage::OnReshown(shell);
}

void DifficultyPage::Unmount(OptionsShell& shell)
{
    for (int i = 0; i < DTN; ++i)
    {
        USER_CONFIG.cadetDifficulty[i] = m_cadetDifficulty[i];
        USER_CONFIG.veteranDifficulty[i] = m_veteranDifficulty[i];
    }
    USER_CONFIG.easyMode = m_selectedMode == 0;
    UserConfig_SaveDifficulties(USER_CONFIG);
    ScrollListPage::Unmount(shell);
}

const char* DifficultyPage::DifficultyProvider::RowLabel(int row) const
{
    if (!m_page)
        return "";
    if (row == kRowMode)
        return LocalizeString("STR_DISP_OPTIONS_DIFFICULTY");
    if (row >= kRowFirstSetting && row < kRowReset)
        return LocalizeString(Config::diffDesc[SettingIndex(row)].stringId);
    if (row == kRowReset)
        return LocalizeString("STR_DISP_DEFAULT");
    return "";
}

const char* DifficultyPage::DifficultyProvider::RowDescription(int row) const
{
    if (row == kRowMode)
        return LocalizeString("STR_DISP_OPT_DIFF_MODE_DESC");
    if (row >= kRowFirstSetting && row < kRowReset)
        return LocalizeString("STR_DISP_OPT_DIFF_SETTING_DESC");
    if (row == kRowReset)
        return LocalizeString("STR_DISP_OPT_DIFF_RESET_DESC");
    return "";
}

OptionsScrollList::RowDef DifficultyPage::DifficultyProvider::RowFor(int row) const
{
    if (!m_page)
        return {-1, nullptr, 0};
    if (row == kRowMode)
        return {502, m_page->m_modeOptions.data(), 2};
    if (row >= kRowFirstSetting && row < kRowReset)
        return {-1, m_page->m_toggleOptions.data(), 2};
    return {-1, nullptr, 0};
}

int DifficultyPage::DifficultyProvider::RowValue(int row) const
{
    if (!m_page)
        return 0;
    if (row == kRowMode)
        return m_page->m_selectedMode;
    if (row < kRowFirstSetting || row >= kRowReset)
        return 0;
    return ActiveDifficultyArray()[SettingIndex(row)] ? 1 : 0;
}

void DifficultyPage::DifficultyProvider::SetRowValue(int row, int value)
{
    if (!m_page || value < 0 || value > 1)
        return;

    if (row == kRowMode)
    {
        m_page->m_selectedMode = value;
        Persist();
        return;
    }
    if (row < kRowFirstSetting || row >= kRowReset)
        return;

    ActiveDifficultyArray()[SettingIndex(row)] = value != 0;
    Persist();
}

OptionsScrollList::Kind DifficultyPage::DifficultyProvider::RowKind(int row) const
{
    if (row == kRowMode)
        return OptionsScrollList::KindStepper;
    if (row >= kRowFirstSetting && row < kRowReset)
        return OptionsScrollList::KindBoolean;
    if (row == kRowReset)
        return OptionsScrollList::KindAction;
    return OptionsScrollList::Provider::RowKind(row);
}

void DifficultyPage::DifficultyProvider::OnRowAction(int row, Display& /*host*/)
{
    if (!m_page || row != kRowReset)
        return;

    bool* active = ActiveDifficultyArray();
    for (int i = 0; i < DTN; ++i)
        active[i] =
            (m_page->m_selectedMode == 0) ? Config::diffDesc[i].defaultCadet : Config::diffDesc[i].defaultVeteran;
    Persist();
}

bool* DifficultyPage::DifficultyProvider::ActiveDifficultyArray() const
{
    return (m_page && m_page->m_selectedMode == 0) ? m_page->m_cadetDifficulty.data()
                                                   : m_page->m_veteranDifficulty.data();
}

void DifficultyPage::DifficultyProvider::Persist() const
{
    if (!m_page)
        return;

    for (int i = 0; i < DTN; ++i)
    {
        USER_CONFIG.cadetDifficulty[i] = m_page->m_cadetDifficulty[i];
        USER_CONFIG.veteranDifficulty[i] = m_page->m_veteranDifficulty[i];
    }
    USER_CONFIG.easyMode = m_page->m_selectedMode == 0;
    UserConfig_SaveDifficulties(USER_CONFIG);
}

} // namespace Poseidon
