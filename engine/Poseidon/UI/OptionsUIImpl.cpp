#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/CheatCode.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Core/Progress.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/Foundation/Common/Win.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <memory>
#include <optional>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/UI/OptionsUICommon.hpp>
#include <Poseidon/UI/Options/ControlsPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

// Defined at global scope in World/World.cpp.
void ShowCinemaBorder(bool show);

namespace Poseidon
{

namespace
{
const char* kSingleOpenKey = "STR_SINGLE_OPEN";
const char* kSinglePlayKey = "STR_SINGLE_PLAY";
const char* kSingleResumeKey = "STR_SINGLE_RESUME";
const char* kSingleRestartKey = "STR_SINGLE_RESTART";
const char* kCampaignBeginKey = "STR_CAMPAIGN_BEGIN";
const char* kCampaignResumeKey = "STR_CAMPAIGN_RESUME";
const char* kCampaignRestartKey = "STR_CAMPAIGN_RESTART";
const char* kCampaignContinueListKey = "STR_CAMPAIGN_LB_CONTINUE";
const char* kDisplayCadetKey = "STR_DISP_DIFF_CADET";
const char* kDisplayVeteranKey = "STR_DISP_DIFF_VETERAN";

RString DecodeUserFacingLegacyText(RString text)
{
    return DecodeLegacyTextToRString(text, GLanguage);
}
} // namespace

// Single mission display
DisplaySingleMission::DisplaySingleMission(ControlsContainer* parent) : Display(parent)
{
    Load("RscDisplaySingleMission");
    LoadDirectory();
    OnChangeMission();
    LoadParams();
    OnChangeDifficulty();
    _exitWhenClose = -1;
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplaySingleMission::~DisplaySingleMission()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

void DisplaySingleMission::LoadParams()
{
    ParamArchiveLoad ar(GetUserParams());
    if (ar.Serialize("cadetMode", _cadetMode, 1, true) != LSOK)
    {
        Poseidon::Foundation::WarningMessage("Cannot load user paremeters.");
    }
}

void DisplaySingleMission::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(GetUserParams());
    if (ar.Serialize("cadetMode", _cadetMode, 1) != LSOK)
    {
    }
    if (ar.Save(GetUserParams()) != LSOK)
    {
    }
}

#define X_findfirst _findfirst

Control* DisplaySingleMission::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplaySingleMission::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_SINGLE_MISSION)
    {
        OnChangeMission();
    }

    Display::OnLBSelChanged(idc, curSel);
}

void DisplaySingleMission::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_SINGLE_MISSION)
    {
        OnButtonClicked(IDC_OK);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplaySingleMission::LoadDirectory()
{
    ProgressStart(LocalizeString(IDS_LOAD_WORLD));
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SINGLE_MISSION));
    if (!lbox)
    {
        return;
    }

    lbox->ClearStrings();

    if (_directory.GetLength() > 0)
    {
        // Parent directory
        int index = lbox->AddString("..");
        lbox->SetValue(index, -2); // subdirectory
    }

    RString dir = RString("Missions\\") + _directory;

    const char* searchStr = "briefingName";
    int searchLen = strlen(searchStr);

    _finddata_t info;

    // first search for .pbo files
    intptr_t h = _findfirst(dir + RString("*.pbo"), &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) == 0)
            {
                // remove extension (.pbo)
                RString name = info.name;
                int n = name.GetLength() - 4;
                PoseidonAssert(stricmp(name + n, ".pbo") == 0);
                RString nameNoExt = name.Substring(0, n);

                // create bank (temporary)
                QFBank bank;
                bank.open(dir + nameNoExt);

                ProgressRefresh();

                ParamFile f;
                if (f.ParseBin(bank, "mission.sqm"))
                {
                    const ParamEntry& entry = f >> "Mission" >> "Intel";
                    if (entry.FindEntry("briefingName"))
                    {
                        name = entry >> "briefingName";
                    }
                }
                else
                {
                    // fast search for mission name without parsing
                    // suppose "briefingName" is found in first 4KB of mission file
                    QIFStreamB file;
                    file.open(bank, "mission.sqm");

                    const char* searchIn = file.act();
                    const char* maxEnd = searchIn + file.rest();
                    int searchInLen = file.rest();
                    saturateMin(searchInLen, 4 * 1024); // seach max. one page
                    searchInLen -= searchLen;

                    for (int s = 0; s < searchInLen; s++)
                    {
                        if (!strnicmp(searchIn + s, searchStr, searchLen))
                        {
                            // candidate for a match
                            // scan for '=' sign
                            const char* end = searchIn + s + searchLen;
                            while (*end == ' ' && end < maxEnd)
                            {
                                end++;
                            }
                            if (*end == '=')
                            {
                                end++;
                                while (*end == ' ' && end < maxEnd)
                                {
                                    end++;
                                }
                                // should be followed with '"'
                                if (*end == '"')
                                {
                                    end++;
                                    // scan name
                                    char buf[1024];
                                    int maxC = 0;
                                    ;
                                    while (*end != '"' && end < maxEnd && maxC < sizeof(buf) - 1)
                                    {
                                        buf[maxC++] = *end++;
                                    }
                                    buf[maxC] = 0;
                                    if (*buf)
                                    {
                                        name = buf;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }

                int index = lbox->AddString(Localize(name));
                lbox->SetData(index, nameNoExt);
                lbox->SetValue(index, 1); // bank
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    // search for missions is subdirectories and for folders of missions
    h = X_findfirst(dir + RString("*.*"), &info);
    if (h != -1)
    {
        const char* searchStr = "briefingName";
        int searchLen = strlen(searchStr);
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                RString name = info.name;

                if (strchr(name, '.'))
                {
                    // Mission directory
                    RString filename = dir + name + RString("\\mission.sqm");

                    ProgressRefresh();

                    ParamFile f;
                    if (f.ParseBin(filename))
                    {
                        const ParamEntry& entry = f >> "Mission" >> "Intel";
                        if (entry.FindEntry("briefingName"))
                        {
                            name = entry >> "briefingName";
                        }
                    }
                    else
                    {
                        // fast search for mission name without parsing
                        // suppose "briefingName" is found in first 4KB of mission file
                        QIFStream file;
                        file.open(filename);

                        const char* searchIn = file.act();
                        const char* maxEnd = searchIn + file.rest();
                        int searchInLen = file.rest();
                        saturateMin(searchInLen, 4 * 1024); // seach max. one page
                        searchInLen -= searchLen;

                        for (int s = 0; s < searchInLen; s++)
                        {
                            if (!strnicmp(searchIn + s, searchStr, searchLen))
                            {
                                // candidate for a match
                                // scan for '=' sign
                                const char* end = searchIn + s + searchLen;
                                while (*end == ' ' && end < maxEnd)
                                {
                                    end++;
                                }
                                if (*end == '=')
                                {
                                    end++;
                                    while (*end == ' ' && end < maxEnd)
                                    {
                                        end++;
                                    }
                                    // should be followed with '"'
                                    if (*end == '"')
                                    {
                                        end++;
                                        // scan name
                                        char buf[1024];
                                        int maxC = 0;
                                        ;
                                        while (*end != '"' && end < maxEnd && maxC < sizeof(buf) - 1)
                                        {
                                            buf[maxC++] = *end++;
                                        }
                                        buf[maxC] = 0;
                                        if (*buf)
                                        {
                                            name = buf;
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }

                    // Localize briefingName.  For "@KEY" / "$STR_KEY" tokens we
                    // try the mission's local stringtable.csv first — at scan
                    // time no mission has been opened yet, so the global
                    // _tableMission is empty and Localize() would return ""
                    // (or pass through the literal) for keys that live only in
                    // the mission's own CSV.  Falls back to the global tables
                    // when the local CSV doesn't have the key.
                    RString displayName;
                    if (name[0] == '$' || name[0] == '@')
                    {
                        const char* keyOffset = (const char*)name + 1;
                        displayName =
                            LookupStringtableCsv(dir + RString(info.name) + RString("\\stringtable.csv"), keyOffset);
                    }
                    if (displayName.GetLength() == 0)
                    {
                        displayName = Localize(name);
                    }
                    if (displayName.GetLength() == 0)
                    {
                        // No translation found anywhere (not in mission CSV,
                        // not in global tables, not even the English column).
                        // Show the directory name (e.g. "Demo.Demo") rather
                        // than the raw "$STR_…" token — never expose template
                        // syntax to the player.
                        displayName = info.name;
                    }
                    int index = lbox->AddString(displayName);
                    lbox->SetData(index, info.name);
                    lbox->SetValue(index, 0); // directory
                }
                else
                {
                    // Subdirectory
                    int index = lbox->AddString(name + RString("..."));
                    lbox->SetData(index, name);
                    lbox->SetValue(index, -1); // subdirectory
                }
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    lbox->SortItemsByValue();
    lbox->SetCurSel(0);
    ProgressFinish();
}

RString GetOverviewFile(RString dir)
{
    return FindLocalizedMissionHtmlFile(dir, "overview");
}

void LoadMissionPreviewHtml(C3DHTML* html, RString filename, const MissionLanguageDetector::MissionPreviewInfo& info)
{
    RString overviewUtf8 = LoadLocalizedMissionHtmlUtf8(filename);
    RString combined = MissionLanguageDetector::BuildPreviewHtml(info, overviewUtf8);
    html->LoadBuffer(filename, combined, false);
}

static void CheckContinueSave(RString dir)
{
    RString resume = dir + RString("continue.fps");
    if (QIFStream::FileExists(resume))
    {
        return;
    }

    RString save = dir + RString("save.fps");
    RString autosave = dir + RString("autosave.fps");
    if (QIFStream::FileExists(save))
    {
        if (QIFStream::FileExists(autosave))
        {
            // select later — copy whichever save is more recent
            time_t timeSave = getFileModTime(save);
            time_t timeAutosave = getFileModTime(autosave);
            if (timeSave >= timeAutosave)
            {
                ::CopyFile(save, resume, FALSE);
            }
            else
            {
                ::CopyFile(autosave, resume, FALSE);
            }
        }
        else
        {
            ::CopyFile(save, resume, FALSE);
        }
    }
    else
    {
        if (QIFStream::FileExists(autosave))
        {
            ::CopyFile(autosave, resume, FALSE);
        }
    }
}

void DisplaySingleMission::OnChangeMission()
{
    C3DHTML* html = dynamic_cast<C3DHTML*>(GetCtrl(IDC_SINGLE_OVERVIEW));
    if (!html)
    {
        return;
    }
    html->Init();
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SINGLE_MISSION));
    if (!lbox)
    {
        return;
    }
    int sel = lbox->GetCurSel();
    if (sel < 0)
    {
        return;
    }

    RString mission = lbox->GetData(sel);
    RString directory = RString("missions\\") + _directory + mission;

    if (lbox->GetValue(sel) < 0)
    {
        if (lbox->GetValue(sel) == -1)
        {
            RString filename = GetOverviewFile(directory + RString("\\"));
            if (filename.GetLength() > 0)
            {
                html->LoadBuffer(filename, LoadLocalizedMissionHtmlUtf8(filename), false);
            }
        }

        CActiveText* ctrl = static_cast<CActiveText*>(GetCtrl(IDC_OK));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(kSingleOpenKey));
        }
        ctrl = static_cast<CActiveText*>(GetCtrl(IDC_SINGLE_LOAD));
        if (ctrl)
        {
            ctrl->ShowCtrl(false);
        }
        return;
    }

    RString filename;
    MissionLanguageDetector::MissionPreviewInfo preview;
    if (lbox->GetValue(sel) == 1)
    {
        RString bank = CreateSingleMissionBank(directory);
        if (bank.GetLength() == 0)
        {
            return;
        }
        preview = MissionLanguageDetector::DetectPreview(bank);
        filename = GetOverviewFile(bank);
    }
    else
    {
        preview = MissionLanguageDetector::DetectPreview(directory + RString("\\"));
        filename = GetOverviewFile(directory + RString("\\"));
    }
    if (filename.GetLength() > 0 || preview.missionViewDistance.has_value() ||
        MissionLanguageDetector::FormatTextLanguages(preview.languages) != RString("-") ||
        MissionLanguageDetector::FormatVoiceLanguages(preview.languages) != RString("-"))
    {
        LoadMissionPreviewHtml(html, filename, preview);
    }

    // update buttons
    RString dir = GetMissionSaveDirectory(_directory + mission);
    CheckContinueSave(dir);
    RString save = dir + RString("continue.fps");
    if (QIFStream::FileExists(save))
    {
        CActiveText* ctrl = static_cast<CActiveText*>(GetCtrl(IDC_OK));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(kSingleRestartKey));
        }
        ctrl = static_cast<CActiveText*>(GetCtrl(IDC_SINGLE_LOAD));
        if (ctrl)
        {
            int day, month, year, hour, minute;
            if (getFileLocalTime(save, day, month, year, hour, minute))
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(kSingleResumeKey), month, day, year, hour, minute);
                ctrl->SetText(buffer);
                ctrl->ShowCtrl(true);
            }
        }
    }
    else
    {
        CActiveText* ctrl = static_cast<CActiveText*>(GetCtrl(IDC_OK));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(kSinglePlayKey));
        }
        ctrl = static_cast<CActiveText*>(GetCtrl(IDC_SINGLE_LOAD));
        if (ctrl)
        {
            ctrl->ShowCtrl(false);
        }
    }
}

void DisplaySingleMission::OnChangeDifficulty()
{
    CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_SINGLE_DIFF));
    if (!ctrl)
    {
        return;
    }
    RString text;
    if (_cadetMode)
    {
        text = LocalizeString(kDisplayCadetKey);
    }
    else
    {
        text = LocalizeString(kDisplayVeteranKey);
    }
    ctrl->SetText(text);
}

void DisplaySingleMission::RefreshLanguage()
{
    // Mission names are localized once at LoadDirectory() time and cached in the
    // C3DListBox; on a runtime language switch we have to repopulate so
    // briefingName="@STRN_…" / "$STR_…" entries resolve in the new language.
    // Preserve the current selection so the preview pane updates in lock-step.
    int prevSel = -1;
    if (auto* lb = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SINGLE_MISSION)))
        prevSel = lb->GetCurSel();

    LoadDirectory();

    if (prevSel >= 0)
    {
        if (auto* lb = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SINGLE_MISSION)))
            lb->SetCurSel(prevSel, false);
    }
    OnChangeMission();
    OnChangeDifficulty();
}

void DisplaySingleMission::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SINGLE_MISSION));
            if (!lbox)
            {
                return;
            }
            int sel = lbox->GetCurSel();
            if (sel < 0)
            {
                return;
            }
            int value = lbox->GetValue(sel);

            if (value == -1)
            {
                _directory = _directory + lbox->GetData(sel) + RString("\\");
                LoadDirectory();
                return;
            }
            else if (value == -2)
            {
                int last = _directory.GetLength() - 1;
                if (last < 0)
                {
                    return;
                }
                PoseidonAssert(_directory[last] == '\\');
                RString name = _directory.Substring(0, last);
                const char* ext = strrchr(name, '\\');
                if (ext)
                {
                    _directory = name.Substring(0, ext - (const char*)name + 1);
                }
                else
                {
                    _directory = "";
                }
                LoadDirectory();
                return;
            }
            else
            {
                Exit(idc);
            }
        }
        break;
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl =
                dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_SINGLE_MISSION_PAD));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_SINGLE_LOAD:
            Exit(idc);
            break;
        case IDC_SINGLE_DIFF:
            _cadetMode = !_cadetMode;
            SaveParams();
            OnChangeDifficulty();
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplaySingleMission::OnCtrlClosed(int idc)
{
    if (idc == IDC_SINGLE_MISSION_PAD)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

// Load campaign display
DisplayCampaignLoad::DisplayCampaignLoad(ControlsContainer* parent) : Display(parent)
{
    LoadCampaigns();
    Load("RscDisplayCampaignLoad");

    _currentCampaign = 0;
    LoadParams();

    _showStatistics = false;

    OnChangeCampaign();
    OnChangeDifficulty();

    IControl* ctrl = GetCtrl(IDC_CAMPAIGN_DESCRIPTION);
    if (ctrl)
    {
        ctrl->ShowCtrl(false);
    }
    ctrl = GetCtrl(IDC_CAMPAIGN_MISSION);
    if (ctrl)
    {
        ctrl->ShowCtrl(false);
    }
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplayCampaignLoad::~DisplayCampaignLoad()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

void DisplayCampaignLoad::LoadParams()
{
    ParamArchiveLoad ar(GetUserParams());
    if (ar.Serialize("cadetMode", _cadetMode, 1, true) != LSOK)
    {
        Poseidon::Foundation::WarningMessage("Cannot load user paremeters.");
    }
    RString campaign;
    if (ar.Serialize("currentCampaign", campaign, 1, "") != LSOK)
    {
        Poseidon::Foundation::WarningMessage("Cannot load user paremeters.");
    }
    if (campaign.GetLength() > 0)
    {
        for (int i = 0; i < _campaigns.Size(); i++)
        {
            if (stricmp(_campaigns[i].campaignName, campaign) == 0)
            {
                _currentCampaign = i;
                break;
            }
        }
    }
}

void DisplayCampaignLoad::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(GetUserParams());
    if (ar.Serialize("cadetMode", _cadetMode, 1) != LSOK)
    {
    }
    RString campaign;
    if (_currentCampaign >= 0 && _currentCampaign < _campaigns.Size())
    {
        campaign = _campaigns[_currentCampaign].campaignName;
    }
    if (ar.Serialize("currentCampaign", campaign, 1) != LSOK)
    {
    }
    if (ar.Save(GetUserParams()) != LSOK)
    {
    }
}

void DisplayCampaignLoad::OnChangeDifficulty()
{
    CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CAMPAIGN_DIFF));
    if (!ctrl)
    {
        return;
    }
    RString text;
    if (_cadetMode)
    {
        text = LocalizeString(kDisplayCadetKey);
    }
    else
    {
        text = LocalizeString(kDisplayVeteranKey);
    }
    ctrl->SetText(text);
}

void DisplayCampaignLoad::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_CAMPAIGN_HISTORY)
    {
        OnChangeMission();
    }
    else
    {
        Display::OnLBSelChanged(idc, curSel);
    }
}

void DisplayCampaignLoad::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_CAMPAIGN_HISTORY)
    {
        C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_CAMPAIGN_HISTORY));
        if (!lbox)
        {
            return;
        }
        int sel = lbox->GetCurSel();
        DoAssert(sel >= 0);
        int value = lbox->GetValue(sel);
        if (value < 0)
        {
            OnButtonClicked(IDC_OK);
        }
        else
        {
            OnButtonClicked(IDC_CAMPAIGN_REPLAY);
        }
    }
}

void DisplayCampaignLoad::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_CAMPAIGN_BOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_OK:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_CAMPAIGN_HISTORY));
            if (!lbox)
            {
                break;
            }
            int sel = lbox->GetCurSel();
            DoAssert(sel >= 0);
            int value = lbox->GetValue(sel);
            if (value == -1 || sel == lbox->GetSize() - 1)
            {
                Exit(IDC_OK);
            }
            else
            {
                CreateChild(new DisplayRevert(this));
            }
        }
        break;
        case IDC_CAMPAIGN_REPLAY:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_CAMPAIGN_HISTORY));
            if (!lbox)
            {
                break;
            }
            int sel = lbox->GetCurSel();
            DoAssert(sel >= 0);
            int value = lbox->GetValue(sel);
            if (value >= 0)
            {
                CampaignHistory& campaign = _campaigns[_currentCampaign];
                BattleHistory& battle = campaign.battles[value >> 16];
                MissionHistory& mission = battle.missions[value & 0xffff];
                if (mission.completed)
                {
                    Exit(idc);
                }
            }
        }
        break;
        case IDC_CAMPAIGN_PREV:
            if (_currentCampaign > 0)
            {
                _currentCampaign--;
                SaveParams();
                OnChangeCampaign();
            }
            break;
        case IDC_CAMPAIGN_NEXT:
            if (_currentCampaign < _campaigns.Size() - 1)
            {
                _currentCampaign++;
                SaveParams();
                OnChangeCampaign();
            }
            break;
        case IDC_CAMPAIGN_DIFF:
            _cadetMode = !_cadetMode;
            SaveParams();
            OnChangeDifficulty();
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayCampaignLoad::OnChildDestroyed(int idd, int exit)
{
    Display::OnChildDestroyed(idd, exit);

    if (idd == IDD_REVERT && exit == IDC_OK)
    {
        Exit(IDC_OK);
    }
}

void DisplayCampaignLoad::OnCtrlClosed(int idc)
{
    if (idc == IDC_CAMPAIGN_BOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

static RString ResolveCampaignHistoryTitle(RString campaign, ParamFile& cfg, RString battleName, RString missionName,
                                           RString storedDisplayName);

void DisplayCampaignLoad::OnChangeCampaign()
{
    if (_currentCampaign < 0 || _currentCampaign >= _campaigns.Size())
    {
        if (GetCtrl(IDC_CAMPAIGN_PREV))
        {
            GetCtrl(IDC_CAMPAIGN_PREV)->ShowCtrl(false);
        }
        if (GetCtrl(IDC_CAMPAIGN_NEXT))
        {
            GetCtrl(IDC_CAMPAIGN_NEXT)->ShowCtrl(false);
        }
        return;
    }

    CampaignHistory& campaign = _campaigns[_currentCampaign];
    _cfg.Clear();
    _cfg.Parse(GetCampaignDirectory(campaign.campaignName) + RString("description.ext"));

    C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CAMPAIGN));
    if (text)
    {
        text->SetText(DecodeUserFacingLegacyText(_cfg >> "Campaign" >> "name"));
    }

    C3DActiveText* active = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_CAMPAIGN_PREV));
    if (active)
    {
        active->ShowCtrl(_currentCampaign > 0);
    }

    active = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_CAMPAIGN_NEXT));
    if (active)
    {
        active->ShowCtrl(_currentCampaign < _campaigns.Size() - 1);
    }

    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_CAMPAIGN_HISTORY));
    if (lbox)
    {
        int selectedValue = 0;
        bool hasSelection = false;
        if (int sel = lbox->GetCurSel(); sel >= 0)
        {
            selectedValue = lbox->GetValue(sel);
            hasSelection = true;
        }
        lbox->ClearStrings();
        int index = lbox->AddString(LocalizeString(kCampaignBeginKey));
        lbox->SetValue(index, -2);
        for (int i = 0; i < campaign.battles.Size(); i++)
        {
            BattleHistory& battle = campaign.battles[i];
            for (int j = 0; j < battle.missions.Size(); j++)
            {
                MissionHistory& mission = battle.missions[j];
                if (mission.displayName.GetLength() == 0)
                {
                    continue;
                }
                int index = lbox->AddString(ResolveCampaignHistoryTitle(campaign.campaignName, _cfg, battle.battleName,
                                                                        mission.missionName, mission.displayName));
                //				lbox->SetData(index, mission.missionName);
                lbox->SetValue(index, (i << 16) + j);
            }
        }
        RString dir = GetCampaignSaveDirectory(campaign.campaignName);
        CheckContinueSave(dir);
        RString save = dir + RString("continue.fps");
        if (QIFStream::FileExists(save))
        {
            int day, month, year, hour, minute;
            if (getFileLocalTime(save, day, month, year, hour, minute))
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(kCampaignContinueListKey), month, day, year, hour,
                         minute);
                int index = lbox->AddString(buffer);
                lbox->SetValue(index, -1);
            }
        }
        int selectedIndex = lbox->GetSize() - 1;
        if (hasSelection)
        {
            for (int i = 0; i < lbox->GetSize(); ++i)
            {
                if (lbox->GetValue(i) == selectedValue)
                {
                    selectedIndex = i;
                    break;
                }
            }
        }
        lbox->SetCurSel(selectedIndex);
    }
}

void DisplayCampaignLoad::OnChangeMission()
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_CAMPAIGN_HISTORY));
    if (!lbox)
    {
        return;
    }

    CampaignHistory& campaign = _campaigns[_currentCampaign];

    int sel = lbox->GetCurSel();
    DoAssert(sel >= 0);
    int value = lbox->GetValue(sel);
    if (value < 0)
    {
        C3DHTML* html = dynamic_cast<C3DHTML*>(GetCtrl(IDC_CAMPAIGN_DESCRIPTION));
        if (html)
        {
            RString filename = GetOverviewFile(GetCampaignDirectory(campaign.campaignName));
            if (filename.GetLength() > 0)
            {
                html->LoadBuffer(filename, LoadLocalizedMissionHtmlUtf8(filename), false);
            }
        }

        C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_MISSION));
        if (text)
        {
            text->SetText("");
        }

        CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CAMPAIGN_REPLAY));
        if (ctrl)
        {
            ctrl->ShowCtrl(false);
        }

        ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_OK));
        if (ctrl)
        {
            if (value == -2)
            {
                ctrl->SetText(LocalizeString(kCampaignBeginKey));
            }
            else
            {
                ctrl->SetText(LocalizeString(kCampaignResumeKey));
            }
        }

        UpdateStatistics(nullptr, nullptr);
    }
    else
    {
        int battleIndex = value >> 16;
        int missionIndex = value & 0xffff;
        BattleHistory& battle = campaign.battles[battleIndex];
        MissionHistory& mission = battle.missions[missionIndex];
        RString templ = _cfg >> "Campaign" >> battle.battleName >> mission.missionName >> "template";

        C3DHTML* html = dynamic_cast<C3DHTML*>(GetCtrl(IDC_CAMPAIGN_DESCRIPTION));
        if (html)
        {
            RString filename = GetOverviewFile(GetCampaignMissionDirectory(campaign.campaignName, templ));
            if (filename.GetLength() > 0)
            {
                html->LoadBuffer(filename, LoadLocalizedMissionHtmlUtf8(filename), false);
            }
        }

        C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_MISSION));
        if (text)
        {
            text->SetText(ResolveCampaignHistoryTitle(campaign.campaignName, _cfg, battle.battleName,
                                                      mission.missionName, mission.displayName));
        }

        CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CAMPAIGN_REPLAY));
        if (ctrl)
        {
            ctrl->ShowCtrl(mission.completed);
        }

        ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_OK));
        if (ctrl)
        {
            if (sel == lbox->GetSize() - 1)
            {
                ctrl->SetText(LocalizeString(kCampaignResumeKey));
            }
            else
            {
                ctrl->SetText(LocalizeString(kCampaignRestartKey));
            }
        }

        AIStatsCampaign* newStats = nullptr;
        missionIndex++;
        if (missionIndex < battle.missions.Size())
        {
            MissionHistory& newMission = battle.missions[missionIndex];
            newStats = &newMission.stats;
        }
        else
        {
            do
            {
                battleIndex++;
            } while (battleIndex < campaign.battles.Size() && campaign.battles[battleIndex].missions.Size() == 0);
            if (battleIndex < campaign.battles.Size())
            {
                BattleHistory& newBattle = campaign.battles[battleIndex];
                MissionHistory& newMission = newBattle.missions[0];
                newStats = &newMission.stats;
            }
        }
        UpdateStatistics(&mission.stats, newStats);
    }
}

void DisplayCampaignLoad::RefreshLanguage()
{
    OnChangeCampaign();
    OnChangeMission();
    OnChangeDifficulty();
}

void DisplayCampaignLoad::ShowStatistics(bool show)
{
    GetCtrl(IDC_CAMPAIGN_DATE)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_SCORE)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_DURATION)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CASUALTIES)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_KILLS_TITLE)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_ENEMY_ROW)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FRIENDLY_ROW)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CIVILIAN_ROW)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_INFANTRY_COLUMN)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_SOFT_COLUMN)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_ARMORED_COLUMN)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_AIRCRAFT_COLUMN)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_TOTAL_COLUMN)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_EINF)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_ESOFT)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_EARM)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_EAIR)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_ETOT)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FINF)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FSOFT)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FARM)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FAIR)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_FTOT)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CINF)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CSOFT)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CARM)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CAIR)->ShowCtrl(show);
    GetCtrl(IDC_CAMPAIGN_CTOT)->ShowCtrl(show);
}

void DisplayCampaignLoad::UpdateStatistics(AIStatsCampaign* begin, AIStatsCampaign* end)
{
    if (_showStatistics && begin != nullptr && end != nullptr)
    {
        ShowStatistics(true);

        C3DStatic* ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_DATE));
        if (ctrl)
        {
            if (end->_date == 0)
            {
                ctrl->ShowCtrl(false);
            }
            else
            {
                tm* t = localtime(&end->_date);
                char buffer[256];
                // Not strftime: its %a/%b use the C locale (English). FormatLocalizedDate pulls
                // the day/month names from the stringtable so they follow the UI language.
                FormatLocalizedDate(LocalizeString(IDS_CAMPAIGN_DATE_FORMAT), *t, buffer);
                RString value = Format(LocalizeString(IDS_CAMPAIGN_DATE), buffer);
                ctrl->SetText(value);
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_SCORE));
        if (ctrl)
        {
            RString value = Format(LocalizeString(IDS_CAMPAIGN_SCORE), end->_score - begin->_score, end->_score);
            ctrl->SetText(value);
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_DURATION));
        if (ctrl)
        {
            float t = end->_inCombat * (1.0f / 3600.0f);
            int hours = toIntFloor(t);
            t -= hours;
            t *= 60;
            int minutes = toInt(t);
            RString total;
            if (hours > 0)
            {
                total = Format(LocalizeString(IDS_DURATION_LONG), hours, minutes);
            }
            else
            {
                total = Format(LocalizeString(IDS_DURATION_SHORT), minutes);
            }

            t = (end->_inCombat - begin->_inCombat) * (1.0f / 3600.0f);
            hours = toIntFloor(t);
            t -= hours;
            t *= 60;
            minutes = toInt(t);
            RString mission;
            if (hours > 0)
            {
                mission = Format(LocalizeString(IDS_DURATION_LONG), hours, minutes);
            }
            else
            {
                mission = Format(LocalizeString(IDS_DURATION_SHORT), minutes);
            }

            RString value = Format(LocalizeString(IDS_CAMPAIGN_DURATION), (const char*)mission, (const char*)total);
            ctrl->SetText(value);
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CASUALTIES));
        if (ctrl)
        {
            RString value = Format(LocalizeString(IDS_CAMPAIGN_CASUALTIES), end->_casualties - begin->_casualties,
                                   end->_casualties);
            ctrl->SetText(value);
        }

        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_EINF));
        if (ctrl)
        {
            int total = end->_kills[SKEnemyInfantry];
            int mission = total - begin->_kills[SKEnemyInfantry];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_ESOFT));
        if (ctrl)
        {
            int total = end->_kills[SKEnemySoft];
            int mission = total - begin->_kills[SKEnemySoft];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_EARM));
        if (ctrl)
        {
            int total = end->_kills[SKEnemyArmor];
            int mission = total - begin->_kills[SKEnemyArmor];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_EAIR));
        if (ctrl)
        {
            int total = end->_kills[SKEnemyAir];
            int mission = total - begin->_kills[SKEnemyAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_ETOT));
        if (ctrl)
        {
            int total = end->_kills[SKEnemyInfantry] + end->_kills[SKEnemySoft] + end->_kills[SKEnemyArmor] +
                        end->_kills[SKEnemyAir];
            int mission = total - begin->_kills[SKEnemyInfantry] - begin->_kills[SKEnemySoft] -
                          begin->_kills[SKEnemyArmor] - begin->_kills[SKEnemyAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_FINF));
        if (ctrl)
        {
            int total = end->_kills[SKFriendlyInfantry];
            int mission = total - begin->_kills[SKFriendlyInfantry];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_FSOFT));
        if (ctrl)
        {
            int total = end->_kills[SKFriendlySoft];
            int mission = total - begin->_kills[SKFriendlySoft];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_FARM));
        if (ctrl)
        {
            int total = end->_kills[SKFriendlyArmor];
            int mission = total - begin->_kills[SKFriendlyArmor];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_FAIR));
        if (ctrl)
        {
            int total = end->_kills[SKFriendlyAir];
            int mission = total - begin->_kills[SKFriendlyAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_FTOT));
        if (ctrl)
        {
            int total = end->_kills[SKFriendlyInfantry] + end->_kills[SKFriendlySoft] + end->_kills[SKFriendlyArmor] +
                        end->_kills[SKFriendlyAir];
            int mission = total - begin->_kills[SKFriendlyInfantry] - begin->_kills[SKFriendlySoft] -
                          begin->_kills[SKFriendlyArmor] - begin->_kills[SKFriendlyAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CINF));
        if (ctrl)
        {
            int total = end->_kills[SKCivilianInfantry];
            int mission = total - begin->_kills[SKCivilianInfantry];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CSOFT));
        if (ctrl)
        {
            int total = end->_kills[SKCivilianSoft];
            int mission = total - begin->_kills[SKCivilianSoft];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CARM));
        if (ctrl)
        {
            int total = end->_kills[SKCivilianArmor];
            int mission = total - begin->_kills[SKCivilianArmor];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CAIR));
        if (ctrl)
        {
            int total = end->_kills[SKCivilianAir];
            int mission = total - begin->_kills[SKCivilianAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
        ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_CAMPAIGN_CTOT));
        if (ctrl)
        {
            int total = end->_kills[SKCivilianInfantry] + end->_kills[SKCivilianSoft] + end->_kills[SKCivilianArmor] +
                        end->_kills[SKCivilianAir];
            int mission = total - begin->_kills[SKCivilianInfantry] - begin->_kills[SKCivilianSoft] -
                          begin->_kills[SKCivilianArmor] - begin->_kills[SKCivilianAir];
            if (mission != 0 || total != 0)
            {
                RString value = Format("%d(%d)", mission, total);
                ctrl->SetText(value);
            }
            else
            {
                ctrl->SetText(RString());
            }
        }
    }
    else
    {
        ShowStatistics(false);
    }
}

void DisplayCampaignLoad::LoadCampaigns()
{
    RString dir = GetTmpSaveDirectory();

    _campaigns.Clear();

    _finddata_t info;
    intptr_t h = _findfirst("Campaigns\\*.*", &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                RString name = info.name;
                int i = _campaigns.Add();
                CampaignHistory& campaign = _campaigns[i];
                campaign.Clear(name);

                char buffer[256];
                snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, (const char*)name);
                ParamArchiveLoad ar;
                if (ar.LoadBin(buffer) || ar.Load(buffer) == LSOK)
                {
                    if (ar.Serialize("Campaign", campaign, 1) != LSOK)
                    {
                        campaign.Clear(name);
                        Poseidon::Foundation::WarningMessage("Cannot load campaign description %s", buffer);
                    }
                }
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    static const char campaigns[] = "campaigns\\";
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        const QFBank& bank = GFileBanks[i];
        const char* prefix = bank.GetPrefix();
        if (CmpStartStr(prefix, campaigns) == 0)
        {
            char name[256];
            snprintf(name, sizeof(name), "%s", (const char*)prefix + strlen(campaigns));
            int n = strlen(name) - 1;
            if (name[n] == '\\' || name[n] == '/')
            {
                name[n] = 0;
            }

            int i = _campaigns.Add();
            CampaignHistory& campaign = _campaigns[i];
            campaign.Clear(name);

            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, name);
            ParamArchiveLoad ar;
            if (ar.LoadBin(buffer) || ar.Load(buffer) == LSOK)
            {
                if (ar.Serialize("Campaign", campaign, 1) != LSOK)
                {
                    campaign.Clear(name);
                    Poseidon::Foundation::WarningMessage("Cannot load campaign description %s from bank", buffer);
                }
            }
        }
    }
}

void DisplayCampaignLoad::OnSimulate(EntityAI* vehicle)
{
    if (InputSubsystem::Instance().CheatActivated() == CheatUnlockCampaign)
    {
        OnCampaignCheat();
        InputSubsystem::Instance().CheatServed();
    }
    bool show = true;
    ControlObjectContainerAnim* book = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_CAMPAIGN_BOOK));
    if (book)
    {
        show = book->GetPhase() > 0.75;
    }
    IControl* ctrl = GetCtrl(IDC_CAMPAIGN_DESCRIPTION);
    if (ctrl)
    {
        ctrl->ShowCtrl(show);
    }
    ctrl = GetCtrl(IDC_CAMPAIGN_MISSION);
    if (ctrl)
    {
        ctrl->ShowCtrl(show);
    }

    if (show && !_showStatistics)
    {
        _showStatistics = true;
        OnChangeMission();
    }
    else if (!show && _showStatistics)
    {
        _showStatistics = false;
        ShowStatistics(false);
    }

    Display::OnSimulate(vehicle);
}

// Resolve a friendly title for a mission inside a campaign — opens the
// mission's mission.sqm via LoadLocalizedMissionBriefingName and falls
// back to "<template>.<island>" if no briefingName is set.  This is
// the same fallback path optionsUI.cpp uses when AddMission is called
// at mission-complete time; reused here so unlock-cheat output looks
// identical to "earned through play" entries.
static RString ResolveMissionDisplayName(RString campaign, RString templ)
{
    RString missionDir = GetCampaignMissionDirectory(campaign, templ);
    RString resolved = LoadLocalizedMissionBriefingName(missionDir);
    if (resolved.GetLength() > 0)
        return resolved;
    return templ;
}

// Title for one campaign-progress row, resolved at display time.  The
// mission's briefingName stringtable key (in its mission.sqm) is the
// source of truth: resolve it via the campaign template so the list
// follows the active language and old/broken .sqc files render
// correctly.  The persisted displayName is only a fallback — it may hold
// a save-time-localized string or a raw template name.
static RString ResolveCampaignHistoryTitle(RString campaign, ParamFile& cfg, RString battleName, RString missionName,
                                           RString storedDisplayName)
{
    if (const ParamEntry* campaignCfg = cfg.FindEntry("Campaign"))
        if (const ParamEntry* battleCfg = campaignCfg->FindEntry(battleName))
            if (const ParamEntry* missionCfg = battleCfg->FindEntry(missionName))
                if (const ParamEntry* tmpl = missionCfg->FindEntry("template"))
                {
                    RString templ = *tmpl;
                    RString resolved = ResolveMissionDisplayName(campaign, templ);
                    // ResolveMissionDisplayName returns the template name when
                    // the mission has no briefingName — only prefer a real title.
                    if (resolved.GetLength() > 0 && resolved != templ)
                        return resolved;
                }
    // Fallback to the persisted value: Localize() resolves a $STR/@ token
    // and returns a literal verbatim, so old .sqc rows still render.
    if (storedDisplayName.GetLength() > 0)
    {
        RString loc = Localize(storedDisplayName);
        return loc.GetLength() > 0 ? loc : storedDisplayName;
    }
    return missionName;
}

// Populate a single CampaignHistory from a campaign's description.ext.
// Marks every mission completed.  Shared between the live-display
// refresh (OnCampaignCheat) and the off-screen disk-write path.
static void PopulateUnlockedHistory(CampaignHistory& campaign)
{
    RString name = campaign.campaignName;
    campaign.Clear(name);

    RString filename = GetCampaignDirectory(name) + RString("description.ext");
    if (!QIFStreamB::FileExist(filename))
        return;
    ParamFile description;
    description.Parse(filename);

    const ParamEntry& cfgCampaign = description >> "Campaign";
    for (int i = 0; i < cfgCampaign.GetEntryCount(); i++)
    {
        const ParamEntry& cfgBattle = cfgCampaign.GetEntry(i);
        if (!cfgBattle.IsClass())
            continue;
        int bi = campaign.battles.Add();
        BattleHistory& battle = campaign.battles[bi];
        battle.battleName = cfgBattle.GetName();
        for (int j = 0; j < cfgBattle.GetEntryCount(); j++)
        {
            const ParamEntry& cfgMission = cfgBattle.GetEntry(j);
            if (!cfgMission.IsClass())
                continue;
            int mi = battle.missions.Add();
            MissionHistory& mission = battle.missions[mi];
            mission.missionName = cfgMission.GetName();
            RString templ = cfgMission >> "template";
            mission.displayName = ResolveMissionDisplayName(name, templ);
            mission.completed = true;
        }
    }
}

// Persist one campaign's unlocked history to <TmpSaveDir>/<name>.sqc
// so a future DisplayCampaignLoad sees it on next open.  Mirrors the
// file format LoadCampaigns reads back: ParamArchive with a single
// "Campaign" entry holding the serialized CampaignHistory.
static bool SaveCampaignHistoryToDisk(const CampaignHistory& campaign)
{
    RString dir = GetTmpSaveDirectory();
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, (const char*)campaign.campaignName);

    ParamArchiveSave ar(UserInfoVersion);
    // const_cast because SerializeClass::Serialize is non-const but this
    // path is logically read-only — it's the standard write idiom used
    // elsewhere by SaveParams.
    CampaignHistory& nonConst = const_cast<CampaignHistory&>(campaign);
    if (ar.Serialize("Campaign", nonConst, 1) != LSOK)
        return false;
    return ar.Save(buffer) == LSOK;
}

// Bridge for DebugCheats::Cmd_UnlockCampaign — unlocks every campaign
// found on disk (subdirs of Campaigns/ + .pb? banks), works regardless
// of which display is currently active.  When DisplayCampaignLoad is
// open we also refresh its in-memory _campaigns so the visible list
// updates immediately; otherwise the .sqc files we just wrote will be
// picked up on the next Campaign Load open.
bool DebugUnlockAllCampaigns()
{
    AutoArray<RString> names;

    // Subdirectories under Campaigns/ (same scan as LoadCampaigns).
    _finddata_t info;
    intptr_t h = _findfirst("Campaigns\\*.*", &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
                names.Add(RString(info.name));
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }

    // Mounted .pb? banks under campaigns\<name>\ (covers shipped campaigns).
    static const char campaignsPrefix[] = "campaigns\\";
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        const QFBank& bank = GFileBanks[i];
        const char* prefix = bank.GetPrefix();
        if (CmpStartStr(prefix, campaignsPrefix) != 0)
            continue;
        char name[256];
        snprintf(name, sizeof(name), "%s", prefix + strlen(campaignsPrefix));
        int n = (int)strlen(name) - 1;
        if (n >= 0 && (name[n] == '\\' || name[n] == '/'))
            name[n] = 0;
        // De-duplicate (a campaign present as both a folder AND a bank
        // would otherwise be unlocked + written twice).
        bool already = false;
        for (int j = 0; j < names.Size(); j++)
            if (stricmp(names[j], name) == 0)
            {
                already = true;
                break;
            }
        if (!already)
            names.Add(RString(name));
    }

    if (names.Size() == 0)
        return false;

    for (int i = 0; i < names.Size(); i++)
    {
        CampaignHistory hist;
        hist.Clear(names[i]);
        PopulateUnlockedHistory(hist);
        SaveCampaignHistoryToDisk(hist);
    }

    // Refresh the live display if it happens to be open.
    UITestEngine tmp;
    if (auto* disp = dynamic_cast<DisplayCampaignLoad*>(tmp.GetActiveDisplay()))
        disp->OnCampaignCheat();

    return true;
}

void DisplayCampaignLoad::OnCampaignCheat()
{
    // Reuse the shared populator so the live-display refresh produces
    // the same nicely-named entries as the off-screen UnlockAllCampaigns
    // disk write.  Previously this stored the raw `template` config field
    // ("01Flashpoint.Eden") as displayName, which showed up verbatim in
    // the mission list.  Now we read each mission's briefingName via
    // LoadLocalizedMissionBriefingName so the entries match what the
    // briefing screen shows once the player actually opens a mission.
    PopulateUnlockedHistory(_campaigns[_currentCampaign]);
    OnChangeCampaign();
}

DisplayRevert::DisplayRevert(ControlsContainer* parent) : Display(parent)
{
    Load("RscDisplayRevert");
    _exitWhenClose = -1;

    ControlObjectContainerAnim* book = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_REVERT_BOOK));
    if (book)
    {
        book->Open();
    }

    IControl* ctrl = GetCtrl(IDC_REVERT_TITLE);
    if (ctrl)
    {
        ctrl->ShowCtrl(false);
    }
    ctrl = GetCtrl(IDC_REVERT_QUESTION);
    if (ctrl)
    {
        ctrl->ShowCtrl(false);
        C3DStatic* text = dynamic_cast<C3DStatic*>(ctrl);
        if (text)
        {
            text->FormatText();
        }
    }
}

void DisplayRevert::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* book = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_REVERT_BOOK));
            if (book)
            {
                book->Close();
            }
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayRevert::OnSimulate(EntityAI* vehicle)
{
    ControlObjectContainerAnim* book = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_REVERT_BOOK));
    bool show = true;
    if (book)
    {
        if (_exitWhenClose >= 0 && book->GetPhase() <= 0.6)
        {
            Exit(_exitWhenClose);
            return;
        }
        show = book->GetPhase() > 0.75;
    }
    IControl* ctrl = GetCtrl(IDC_REVERT_TITLE);
    if (ctrl)
    {
        ctrl->ShowCtrl(show);
    }
    ctrl = GetCtrl(IDC_REVERT_QUESTION);
    if (ctrl)
    {
        ctrl->ShowCtrl(show);
    }

    Display::OnSimulate(vehicle);
}

// Game display
Control* DisplayGame::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_GAME_SELECT)
    {
        PoseidonAssert(type == CT_LISTBOX);
        CListBox* ctrl = new CListBox(this, idc, cls);
        {
            RString dir = GetTmpSaveDirectory();
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s*.fps", (const char*)dir);
            _finddata_t info;
            intptr_t h = _findfirst(buffer, &info);
            if (h != -1)
            {
                do
                {
                    char name[256];
                    snprintf(name, sizeof(name), "%s", (const char*)info.name);
                    char* ext = strrchr(name, '.');
                    if (ext)
                    {
                        *ext = 0;
                    }
                    ctrl->AddString(name);
                } while (0 == _findnext(h, &info));
                _findclose(h);
            }
        }
        ctrl->SortItems();
        ctrl->SetCurSel(0);
        return ctrl;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

bool DisplayGame::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }
    if (_exit == IDC_OK)
    {
        CListBox* ctrl = dynamic_cast<CListBox*>(GetCtrl(IDC_GAME_SELECT));
        PoseidonAssert(ctrl);
        if (ctrl->GetCurSel() < 0)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SELECT_GAME));
            return false;
        }
    }
    return true;
}

// Save display
bool DisplaySave::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }
    if (_exit != IDC_CANCEL)
    {
        CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_SIDE_NAME));
        PoseidonAssert(edit);
        if (!edit->GetText() || !edit->GetText()[0])
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_NAME_EMPTY));
            return false;
        }
    }
    return true;
}

bool IsCreditsConfigured()
{
    // Credits are offered only when CfgCredits defines a usable "<world>.<mission>"
    // cutscene. A package that ships no credits (e.g. the demo, matching the original
    // 2001 demo which had no credits entry) simply leaves the cutscene empty, which
    // hides the button — the options resource and logic stay shared.
    const ParamEntry* cfg = Pars.FindEntry("CfgCredits");
    const ParamEntry* cs = cfg ? cfg->FindEntry("cutscene") : nullptr;
    if (!cs)
        return false;
    RStringB cutscene = *cs;
    return cutscene.GetLength() > 0 && strchr(static_cast<const char*>(cutscene), '.') != nullptr;
}

void PlayCreditsCutscene(Display* parent)
{
    if (!IsCreditsConfigured())
        return;
    RString cutscene = Pars >> "CfgCredits" >> "cutscene";
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", (const char*)cutscene);

    // parse mission name out of "<world>.<mission>" cutscene id
    char* world = strchr(buffer, '.');
    PoseidonAssert(world);
    *world = 0;
    world++;

    SetMission(world, buffer, GetAnimsDir());
    SetBaseDirectory("");

    ParseIntro();

    if (CurrentTemplate.groups.Size() > 0)
    {
        GLOB_WORLD->SwitchLandscape(GetWorldName(world));
        GWorld->ActivateAddons(CurrentTemplate.addOns);
        GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
        GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate);
        parent->CreateChild(new DisplayIntro(parent));
    }
}

// Options display
DisplayOptions::DisplayOptions(ControlsContainer* parent, bool enableSimulation, bool credits) : Display(parent)
{
    _enableSimulation = enableSimulation;
    Load("RscDisplayOptions");
    if (!credits)
    {
        GetCtrl(IDC_OPTIONS_CREDITS)->ShowCtrl(false);
    }
}

void DisplayOptions::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_OPTIONS_NOTEBOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_OPTIONS_VIDEO:
            CreateChild(new DisplayOptionsVideo(this, _enableSimulation));
            break;
        case IDC_OPTIONS_AUDIO:
            CreateChild(new DisplayOptionsAudio(this, _enableSimulation));
            break;
        case IDC_OPTIONS_CONFIGURE:
        {
            if (Res.FindEntry("RscOptionsShell"))
            {
                auto* shell = new OptionsShell(this, _enableSimulation, false);
                shell->PushPage(std::make_unique<ControlsPage>());
                CreateChild(shell);
            }
            else
            {
                CreateChild(new DisplayConfigure(this, _enableSimulation));
            }
        }
            break;
        case IDC_OPTIONS_DIFFICULTY:
            CreateChild(new DisplayDifficulty(this, _enableSimulation));
            break;
        case IDC_OPTIONS_CREDITS:
            PlayCreditsCutscene(this);
            break;
        default:
            Display::OnButtonClicked(idc);
    }
}

void DisplayOptions::OnChildDestroyed(int idd, int exit)
{
    Display::OnChildDestroyed(idd, exit);

    switch (idd)
    {
        case IDD_INTRO:
            GWorld->DestroyMap(IDC_OK);
            ::ShowCinemaBorder(true);
            StartRandomCutscene(Glob.header.worldname);
            break;
    }
}

void DisplayOptions::OnCtrlClosed(int idc)
{
    if (idc == IDC_OPTIONS_NOTEBOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

} // namespace Poseidon
