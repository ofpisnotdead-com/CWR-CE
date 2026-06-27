
#include <Poseidon/Foundation/Platform/InitBridge.hpp> // IsMenuOverriddenByMod (custom-menu hijack)
#include <Poseidon/UI/Options/OptionsShell.hpp>
using namespace Poseidon;
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>

#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Core/SaveVersion.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Random/randomGen.hpp>

#include <SDL3/SDL_keycode.h>

#include <Poseidon/Game/Scripting/Scripts.hpp>

#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/ModInstall.hpp>
#include <Poseidon/Core/ModArchive.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Core/DownloadProgress.hpp>
#include <Poseidon/Core/DownloadDialogView.hpp>
#include <Poseidon/UI/Controls/ProgressBarWidget.hpp>
#include <Poseidon/UI/GameModule.hpp>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Audio/VoiceLangPath.hpp>
#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/UI/DisplayUI.hpp>

#include <Poseidon/UI/OptionsUICommon.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

extern bool AutoTest; // Synced from AppConfig in appConfig.cpp (global)
#include <Poseidon/Foundation/Platform/VersionNo.h>
#include <Poseidon/Core/Version.hpp>

// Static-linkage variables defined in OptionsUI.cpp — duplicated here (each TU gets its own copy).
static RString BaseDirectory;
static RString BaseSubdirectory;
static std::optional<float> CurrentMissionViewDistance;

namespace Poseidon
{
// Forward declarations for non-static functions defined in OptionsUI.cpp.
CampaignHistory& GetGCampaignHistory();
void StartMission(Display* disp, bool newBattle);
bool CheckAward(Display* disp);
bool NextMission(Display* disp, EndMode mode);
} // namespace Poseidon

namespace Poseidon
{

RString GetAppVersion()
{
#if defined(_M_X64) || defined(__x86_64__)
    const char* platform = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const char* platform = "arm64";
#else
    const char* platform = "x86";
#endif

    RString renderer = GEngine ? GEngine->GetRendererName() : "No renderer";

    char buf[160];
    snprintf(buf, sizeof(buf), "%s (%s, %s)", (const char*)GetVersionString(), platform, (const char*)renderer);
    return buf;
}

// Main-menu IDC → STRINGTABLE key. Verified present in packages/Combined/BIN/STRINGTABLE.CSV.
// IDC_MAIN_PLAYER is excluded (DisplayMain::OnDraw re-sets it every frame).
// IDC_MAIN_VERSION and IDC_MAIN_DATE are excluded (non-translated strings).
// IDC_MAIN_EDITOR maps to STR_DISP_MAIN_CUSTOM because the rendered button text is "MISSION EDITOR".
static const struct
{
    int idc;
    const char* key;
} kMainMenuLabels[] = {
    {IDC_MAIN_GAME, "STR_DISP_MAIN_GAME"},         {IDC_MAIN_OPTIONS, "STR_DISP_MAIN_OPTIONS"},
    {IDC_MAIN_MULTIPLAYER, "STR_DISP_MAIN_MULTI"}, {IDC_MAIN_QUIT, "STR_DISP_MAIN_QUIT"},
    {IDC_MAIN_EDITOR, "STR_DISP_MAIN_CUSTOM"},     {IDC_MAIN_SINGLE, "STR_DISP_MAIN_SINGLE"},
    {IDC_MAIN_CREDITS, "STR_CREDITS23"},
};

static void SetMainPlayerControlText(IControl* ctrl)
{
    if (!ctrl)
        return;

    char buffer[256];
    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_MAIN_PLAYER), (const char*)Glob.header.playerName);

    if (CActiveText* text = dynamic_cast<CActiveText*>(ctrl))
        text->SetText(buffer);
    else if (CStatic* text = dynamic_cast<CStatic*>(ctrl))
        text->SetText(buffer);
}

static const char* kLangRotation[] = {
    "English", "Czech", "French", "German", "Italian", "Spanish", "Russian",
};
static constexpr int kLangRotationCount = sizeof(kLangRotation) / sizeof(kLangRotation[0]);

DisplayMain::DisplayMain(ControlsContainer* parent) : Display(parent)
{
    _version = GetAppVersion();

    // Viewer mode: skip menu control load entirely. The Display still
    // exists as the topmost UI container (so the world's per-frame
    // render path runs through Display::DrawHUD as usual), but no
    // RscDisplayMain controls get created — no buttons, no logo, no
    // version text overlay on the model.
    if (AppConfig::Instance().IsViewerMode())
    {
        return;
    }

    // Remaster main menu: inherits the locked vanilla RscDisplayMain
    // layout but overrides the CWA logo control to use cwr_logo.paa.
    // Defined in resources/menu/splashLogo.hpp, merged at runtime by
    // ParseResource via each package's BIN/resource-extra.cpp.
    //
    // Fall back to the vanilla RscDisplayMain when the override class
    // hasn't been merged (e.g. PoseidonUITest's package has its own
    // RESOURCE.BIN with no resource-extra.cpp).  Without this guard,
    // Load() returns an empty class and the next GetCtrl(IDC_MAIN_*)
    // call derefs nullptr in EnableCtrl.
    // Keep the community addon's custom menu when a mod overrode the menu resource
    // (we hijack it below to add Mods + wire Options); otherwise use the remaster
    // menu. Without this the base RscDisplayMainRemaster — restored by
    // MergeBaseResourceExtra so the Options screen works — would replace the mod's
    // menu and lose its branding.
    const char* mainRsc = (!IsMenuOverriddenByMod() && Res.FindEntry("RscDisplayMainRemaster"))
                              ? "RscDisplayMainRemaster"
                              : "RscDisplayMain";
    Load(mainRsc);
    //	LoadHeader();

    // Hijack a community addon's custom menu that has no Mods entry: clone the Quit
    // button (so the new entry matches the mod's own menu styling), give it the
    // Mods idc + label, and place it immediately to the left of Quit on the same
    // row. idc=119 dispatches through GameModuleRegistry (DisplayMain::OnButtonClicked)
    // to the Mods screen, so wiring it is automatic. Heuristic finds Quit by its
    // standard class name; extend as more community menus are tested.
    if (GetCtrl(IDC_MAIN_MODS) == nullptr && GameModuleRegistry::FindByIDC(IDC_MAIN_MODS) != nullptr)
    {
        Control* quit = dynamic_cast<Control*>(GetCtrl(IDC_MAIN_QUIT));
        ParamEntry* quitCls = Res.FindEntry(mainRsc) ? (Res >> mainRsc).FindEntry("Quit") : nullptr;
        if (quit != nullptr && quitCls != nullptr && quitCls->IsClass())
        {
            static ParamFile s_modsInjectCfg; // outlives the control (it back-references the class)
            s_modsInjectCfg.Clear();
            ParamClass* modsCls = s_modsInjectCfg.AddClass("ModsInject");
            // Add the local overrides BEFORE wiring the base: ParamClass::Add resolves
            // FindEntry through the base chain and writes through to an inherited
            // entry, so setting idc/text with the base already attached would mutate
            // the real Quit class (turning the live Quit button into a second "MODS").
            // With no base yet, these create local entries; SetBase then supplies only
            // the inherited geometry/styling.
            modsCls->Add("idc", IDC_MAIN_MODS);
            modsCls->Add("text", RString("$STR_DISP_MAIN_MODS"));
            // Right-align the label so it hugs the box's right edge (next to Quit)
            // instead of floating at the left of a Quit-width box, which is what made
            // the gap look large no matter how the box was positioned.
            modsCls->Add("style", ST_RIGHT);
            modsCls->SetBase(quitCls->GetClassInterface());
            LoadControl(*modsCls);
            if (Control* mods = dynamic_cast<Control*>(GetCtrl(IDC_MAIN_MODS)))
            {
                // Sit Mods just to the left of Quit, same row, with a small gap. With
                // the label right-aligned, this gap is the actual space the player
                // sees between "MODS" and "QUIT GAME".
                const float gap = quit->W() * 0.05f;
                float x = quit->X() - quit->W() - gap;
                if (x < 0.0f)
                    x = 0.0f;
                mods->SetPos(x, quit->Y(), quit->W(), quit->H());
            }
        }
    }

    // Guard every control lookup: when the RscDisplayMain class is absent or
    // minimal (PoseidonUITest's own RESOURCE.BIN, or a headless --test-mission
    // run that doesn't merge the menu resources), Load() yields an empty class
    // and GetCtrl returns null — calling EnableCtrl/ShowCtrl on it then derefs
    // nullptr.  The main menu simply has no buttons in those cases, which is fine.
    if (IControl* cont = GetCtrl(IDC_MAIN_CONTINUE))
        cont->EnableCtrl(false);

    // Enable/disable module buttons based on registered game modules
    for (int idc : {IDC_MAIN_SINGLE, IDC_MAIN_GAME, IDC_MAIN_MULTIPLAYER, IDC_MAIN_EDITOR, IDC_MAIN_MODS})
        if (IControl* btn = GetCtrl(idc))
            btn->EnableCtrl(GameModuleRegistry::FindByIDC(idc) != nullptr);

    // "All Missions (for designers)" — hidden in all shipping builds
    if (IControl* custom = GetCtrl(IDC_MAIN_CUSTOM))
        custom->ShowCtrl(false);
    // Language refresh is handled by the Display base: on F12 / triSetLanguage each
    // control re-resolves its own `text="$STR_..."` via ReloadLocalizedText.
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplayMain::~DisplayMain()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

void DisplayMain::RefreshLanguage()
{
    SetMainPlayerControlText(GetCtrl(IDC_MAIN_PLAYER));
}

bool DisplayMain::DoKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    // Language cycling lives in the Ctrl+` dev panel's Game tab (kLangRotation
    // list + setters), not on F11/F12 here.
    return Display::DoKeyDown(nChar, nRepCnt, nFlags);
}

Control* DisplayMain::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MAIN_VERSION:
        {
            CStatic* text = new CStatic(this, idc, cls);
            text->SetText(_version);
            return text;
        }
        case IDC_MAIN_DATE:
        {
            CStatic* text = new CStatic(this, idc, cls);
#if defined _WIN32
            HANDLE hf =
                ::CreateFile("bin\\generic.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            FILETIME info, infoLocal;
            ::GetFileTime(hf, nullptr, nullptr, &info);
            ::CloseHandle(hf);
            ::FileTimeToLocalFileTime(&info, &infoLocal);
            WORD date, time;
            ::FileTimeToDosDateTime(&infoLocal, &date, &time);
            int day = date & 0x1f;
            int month = (date >> 5) & 0x0f;
            int year = (date >> 9) + 1980;

            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%d/%d/%d", month, day, year);
            text->SetText(buffer);
#else
            struct stat st;
            if (stat("bin/generic.bin", &st) == 0)
            {
                struct tm* tm = localtime(&st.st_mtime);
                if (tm)
                {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "%d/%d/%d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
                    text->SetText(buffer);
                }
            }
#endif

            return text;
        }
        default:
        {
            Control* ctrl = Display::OnCreateCtrl(type, idc, cls);
            if (idc == IDC_MAIN_PLAYER)
                SetMainPlayerControlText(ctrl);
            return ctrl;
        }
    }
}

void StartCampaign(RString campaign, Display* disp)
{
    if (!disp)
    {
        disp = dynamic_cast<Display*>(GWorld->Options());
    }
    if (!disp)
    {
        return;
    }
    SetCampaign(campaign);
    CurrentBattle = ExtParsCampaign >> "Campaign" >> "firstBattle";
    if (CurrentBattle.GetLength() == 0)
    {
        return;
    }
    CurrentMission = ExtParsCampaign >> "Campaign" >> CurrentBattle >> "firstMission";
    if (CurrentMission.GetLength() == 0)
    {
        return;
    }

    Glob.header.playerSide = TWest;

    GStats.ClearAll();
    GetGCampaignHistory().Clear(CurrentCampaign);
    GetGCampaignHistory().campaignName = CurrentCampaign;

    RString dir = GetTmpSaveDirectory();
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, (const char*)CurrentCampaign);
    SaveMission(buffer);

    StartMission(disp, true);
}

static void UpdateUserProfile()
{
    InputSubsystem::Instance().LoadKeys();
    UserConfig_LoadDifficulties(USER_CONFIG);

    GEngine->LoadConfig();
    GScene->LoadConfig();
    if (GSoundsys)
    {
        GSoundsys->LoadConfig();
    }
    LoadGameSettings();
}

void __cdecl CreateEditor(ControlsContainer* parent, bool multiplayer = false);

void __cdecl CreateOutro(Display* disp, EndMode end);

void __cdecl CreateOutro(Display* disp, EndMode end)
{
    disp->CreateChild(new DisplayOutro(disp, end));
}

void DisplayMain::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_CAMPAIGN_LOAD: // Campaign
            if (exit == IDC_OK)
            {
                DisplayCampaignLoad* disp = dynamic_cast<DisplayCampaignLoad*>((ControlsContainer*)_child);
                PoseidonAssert(dynamic_cast<C3DListBox*>(disp->GetCtrl(IDC_CAMPAIGN_HISTORY)));

                USER_CONFIG.easyMode = disp->_cadetMode;

                C3DListBox* lbox = static_cast<C3DListBox*>(disp->GetCtrl(IDC_CAMPAIGN_HISTORY));
                CampaignHistory& campaign = disp->_campaigns[disp->_currentCampaign];
                int sel = lbox->GetCurSel();
                DoAssert(sel >= 0);
                int value = lbox->GetValue(sel);
                if (value == -2)
                {
                    // remove continue.fps
                    RString dir = GetCampaignSaveDirectory(campaign.campaignName);
                    RString save = dir + RString("continue.fps");
                    ::DeleteFile(save);
                    save = dir + RString("autosave.fps");
                    ::DeleteFile(save);
                    save = dir + RString("save.fps");
                    ::DeleteFile(save);

                    RString name = campaign.campaignName;
                    Display::OnChildDestroyed(idd, exit);
                    StartCampaign(name, this);
                }
                else if (value == -1)
                {
                    GetGCampaignHistory() = campaign;
                    RString save = GetCampaignSaveDirectory(campaign.campaignName) + RString("continue.fps");
                    GWorld->LoadBin(save, IDS_LOAD_GAME);
                    // remove continue.fps
                    ::DeleteFile(save);
                    Display::OnChildDestroyed(idd, exit);
                    CreateChild(new DisplayMission(this, false, false, true));
                }
                else
                {
                    // remove continue.fps
                    RString dir = GetCampaignSaveDirectory(campaign.campaignName);
                    RString save = dir + RString("continue.fps");
                    ::DeleteFile(save);
                    save = dir + RString("autosave.fps");
                    ::DeleteFile(save);
                    save = dir + RString("save.fps");
                    ::DeleteFile(save);

                    int iBattle = value >> 16;
                    int iMission = value & 0xffff;
                    BattleHistory& battle = campaign.battles[iBattle];
                    MissionHistory& mission = battle.missions[iMission];

                    SetCampaign(campaign.campaignName);
                    CurrentBattle = battle.battleName;
                    CurrentMission = mission.missionName;
                    GStats._campaign = mission.stats;

                    if (iMission == 0)
                    {
                        campaign.battles.Resize(iBattle);
                        DoAssert(iBattle <= 0 || campaign.battles[iBattle - 1].missions.Size() > 0);
                    }
                    else
                    {
                        campaign.battles.Resize(iBattle + 1);
                        battle.missions.Resize(iMission);
                    }
                    GetGCampaignHistory() = campaign;

                    Display::OnChildDestroyed(idd, exit);

                    {
                        RString dir = GetTmpSaveDirectory();
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, (const char*)CurrentCampaign);
                        GetGCampaignHistory().campaignName = CurrentCampaign;
                        SaveMission(buffer);
                        StartMission(this, iMission == 0);
                    }
                }
            }
            else if (exit == IDC_CAMPAIGN_REPLAY)
            {
                DisplayCampaignLoad* disp = dynamic_cast<DisplayCampaignLoad*>((ControlsContainer*)_child);
                PoseidonAssert(dynamic_cast<C3DListBox*>(disp->GetCtrl(IDC_CAMPAIGN_HISTORY)));
                C3DListBox* lbox = static_cast<C3DListBox*>(disp->GetCtrl(IDC_CAMPAIGN_HISTORY));
                CampaignHistory& campaign = disp->_campaigns[disp->_currentCampaign];
                int sel = lbox->GetCurSel();
                PoseidonAssert(sel >= 0);
                int value = lbox->GetValue(sel);
                PoseidonAssert(value != -1);
                int iBattle = value >> 16;
                int iMission = value & 0xffff;
                BattleHistory& battle = campaign.battles[iBattle];
                MissionHistory& mission = battle.missions[iMission];

                USER_CONFIG.easyMode = disp->_cadetMode;

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                SetBaseDirectory(GetCampaignDirectory(campaign.campaignName));

                RString src = GetCampaignSaveDirectory(campaign.campaignName) + RString("objects.sav");
                RString dst = GetCampaignSaveDirectory("") + RString("objects.sav");
                ::CopyFile(src, dst, FALSE);

                RString epizode =
                    ExtParsCampaign >> "Campaign" >> battle.battleName >> mission.missionName >> "template";

                GStats.ClearAll();
                GStats._campaign = mission.stats;

                // campaign.battles.Resize(iBattle + 1);
                // battle.missions.Resize(iMission);

                if (iMission == 0)
                {
                    campaign.battles.Resize(iBattle);
                    DoAssert(iBattle <= 0 || campaign.battles[iBattle - 1].missions.Size() > 0);
                }
                else
                {
                    campaign.battles.Resize(iBattle + 1);
                    battle.missions.Resize(iMission);
                }

                GetGCampaignHistory() = campaign;

                Display::OnChildDestroyed(idd, exit);
                CreateChild(new DisplayIntro(this, epizode));
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_CAMPAIGN:
        {
            Display::OnChildDestroyed(idd, exit);
            CurrentMission = ExtParsCampaign >> "Campaign" >> CurrentBattle >> "firstMission";
            SetMission(Glob.header.worldname, CurrentMission);
            StartMission(this, false);
        }
        break;
        case IDD_GAME: // Load
            if (exit == IDC_OK)
            {
                CListBox* selector = dynamic_cast<CListBox*>(_child->GetCtrl(IDC_GAME_SELECT));
                int selGame = selector->GetCurSel();
                // Load game
                {
                    // Return to previous game is not availiable now
                    GLOB_WORLD->Clear();

                    // load
                    RString dir = GetTmpSaveDirectory();
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "%s%s.fps", (const char*)dir,
                             (const char*)selector->GetText(selGame));
                    GLOB_WORLD->LoadBin(buffer, IDS_LOAD_GAME);
                }
                Display::OnChildDestroyed(idd, exit);
                Exit(IDC_MAIN_GAME);
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_CUSTOM_ARCADE: // Custom game
            if (exit == IDC_CUST_PLAY)
            {
                // Return to previous game is not availiable now
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }

                CTree* tree = dynamic_cast<CTree*>(_child->GetCtrl(IDC_CUST_GAME));
                CTreeItem* item = tree->GetSelected();
                PoseidonAssert(item);
                PoseidonAssert(item->level == 3);

                RString mission = item->data;
                item = item->parent;
                RString world = item->data;
                item = item->parent;

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                USER_CONFIG.easyMode = false;
                if (item->data.GetLength() == 0)
                {
                    SetBaseDirectory("");
                }
                else
                {
                    SetBaseDirectory(GetCampaignDirectory(item->data));

                    RString src = GetCampaignSaveDirectory(item->data) + RString("objects.sav");
                    RString dst = GetCampaignSaveDirectory("") + RString("objects.sav");
                    ::CopyFile(src, dst, FALSE);
                }
                SetMission(world, mission);

                GStats.ClearAll();
                Display::OnChildDestroyed(idd, exit);
                CreateChild(new DisplayIntro(this));
            }
            else if (exit == IDC_CUST_EDIT)
            {
                // Return to previous game is not availiable now
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }

                CTree* tree = dynamic_cast<CTree*>(_child->GetCtrl(IDC_CUST_GAME));
                CTreeItem* item = tree->GetSelected();
                PoseidonAssert(item);
                PoseidonAssert(item->level >= 2);

                RString mission;
                if (item->level == 3)
                {
                    mission = item->data;
                    item = item->parent;
                }
                else
                {
                    mission = "";
                }
                RString world = item->data;
                item = item->parent;

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                USER_CONFIG.easyMode = false;
                if (item->data.GetLength() == 0)
                {
                    SetBaseDirectory("");
                }
                else
                {
                    SetBaseDirectory(GetCampaignDirectory(item->data));

                    RString src = GetCampaignSaveDirectory(item->data) + RString("objects.sav");
                    RString dst = GetCampaignSaveDirectory("") + RString("objects.sav");
                    ::CopyFile(src, dst, FALSE);
                }
                SetMission(world, mission);

                Display::OnChildDestroyed(idd, exit);
                GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
                // Macrovision CD Protection
                CreateEditor(this);
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_SINGLE_MISSION: // Single mission
            if (exit == IDC_OK)
            {
                // Return to previous game is not availiable now
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }

                C3DListBox* lbox = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_SINGLE_MISSION));
                int sel = lbox->GetCurSel();
                if (sel < 0)
                {
                    Display::OnChildDestroyed(idd, exit);
                    break;
                }

                DisplaySingleMission* disp = dynamic_cast<DisplaySingleMission*>((ControlsContainer*)_child);
                USER_CONFIG.easyMode = disp->_cadetMode;

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                SetCampaign("");

                RString dir = RString("missions\\") + disp->GetDirectory();
                if (lbox->GetValue(sel) == 1)
                {
                    RString filename = lbox->GetData(sel);
                    RString directory = dir + filename;
                    char buffer[1024];
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)CreateSingleMissionBank(directory));
                    if (*buffer == 0)
                    {
                        Display::OnChildDestroyed(idd, exit);
                        break;
                    }
                    char* str = strrchr(buffer, '\\');
                    PoseidonAssert(str);
                    *str = 0;
                    char* world = strrchr(buffer, '.');
                    PoseidonAssert(world);
                    *world = 0;
                    world++;
                    const char* mission = strrchr(buffer, '\\');
                    PoseidonAssert(mission);
                    mission++;
                    SetMission(world, mission);
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)filename);
                    world = strrchr(buffer, '.');
                    PoseidonAssert(world);
                    *world = 0;
                    Glob.header.filenameReal = disp->GetDirectory() + RString(buffer);
                }
                else
                {
                    RString mission = lbox->GetData(sel);
                    if (!ProcessTemplateName(mission, dir))
                    {
                        Display::OnChildDestroyed(idd, exit);
                        break;
                    }
                }

                GStats.ClearAll();
                Display::OnChildDestroyed(idd, exit);
                ApplyCurrentMissionViewDistance();
                CreateChild(new DisplayIntro(this));
            }
            else if (exit == IDC_SINGLE_LOAD)
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_SINGLE_MISSION));
                int sel = lbox->GetCurSel();
                PoseidonAssert(sel >= 0);

                DisplaySingleMission* disp = dynamic_cast<DisplaySingleMission*>((ControlsContainer*)_child);

                RString mission = lbox->GetData(sel);
                if (lbox->GetValue(sel) >= 0)
                {
                    RString dir = RString("missions\\") + disp->GetDirectory();
                    if (lbox->GetValue(sel) == 1)
                    {
                        RString bank = CreateSingleMissionBank(dir + mission);
                        if (bank.GetLength() > 0)
                            CurrentMissionViewDistance =
                                MissionLanguageDetector::DetectPreview(bank).missionViewDistance;
                    }
                    else
                    {
                        CurrentMissionViewDistance =
                            MissionLanguageDetector::DetectPreview(dir + mission + RString("\\")).missionViewDistance;
                    }
                    ApplyCurrentMissionViewDistance();
                }
                RString filename = GetMissionSaveDirectory(disp->GetDirectory() + mission) + RString("continue.fps");
                GWorld->LoadBin(filename, IDS_LOAD_GAME);

                Display::OnChildDestroyed(idd, exit);
                CreateChild(new DisplayMission(this, false, false));
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_SELECT_ISLAND:
            if (exit == IDC_OK)
            {
                // Return to previous game is not availiable now
                //				GNetworkManager->DoneNetworkManager();
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }

                //				CListBox *lbox = dynamic_cast<CListBox *>(_child->GetCtrl(IDC_SELECT_ISLAND));
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_SELECT_ISLAND));
                RString world = lbox->GetData(lbox->GetCurSel());

                RString userDir = GetUserMissionsBase();
                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                SetBaseDirectory(userDir);
                ::CreateDirectory(BaseDirectory + RString("missions"), nullptr);
                SetMission(world, "");

                Display::OnChildDestroyed(idd, exit);
                GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
                // Macrovision CD Protection
                CreateEditor(this);
            }
            else if (exit == IDC_CUST_PLAY)
            {
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";
                USER_CONFIG.easyMode = false;

                GStats.ClearAll();
                Display::OnChildDestroyed(idd, exit);
                CreateChild(new DisplayIntro(this));
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_INTRO:
        {
            Display::OnChildDestroyed(idd, exit);
            GWorld->DestroyMap(IDC_OK);

            CurrentTemplate.Clear();
            if (GWorld->UI())
            {
                GWorld->UI()->Init();
            }
            if (!ParseMission(false) || CurrentTemplate.groups.Size() == 0)
            {
                // add intro into campaign tree
                if (IsCampaign())
                {
                    RString displayName = Localize(CurrentTemplate.intel.briefingName);
                    if (displayName.GetLength() == 0)
                    {
                        displayName = RString(Glob.header.filename) + RString(".") + RString(Glob.header.worldname);
                    }
                    AddMission(CurrentCampaign, CurrentBattle, CurrentMission, displayName);
                }

                _end = EMLoser;
                goto StartOutro;
            }

            GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
            GStats.ClearMission();
            if (IsCampaign())
            {
                GStats._mission._lives = ExtParsCampaign >> "Campaign" >> CurrentBattle >> CurrentMission >> "lives";
            }
            {
                // load variables
                GameState* gstate = GWorld->GetGameState();
                AutoArray<GameVariable>& variables = GStats._campaign._variables;
                for (int i = 0; i < variables.Size(); i++)
                {
                    GameVariable& var = variables[i];
                    gstate->VarSet(var._name, var._value, var._readOnly);
                }
            }
            GWorld->ActivateAddons(CurrentTemplate.addOns);
            GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
            GLOB_WORLD->InitVehicles(GModeArcade, CurrentTemplate);

            RString weapons = GetSaveDirectory() + RString("weapons.cfg");
            ::DeleteFile(weapons);

            // remove continue.fps
            RString dir = GetSaveDirectory();
            RString save = dir + RString("continue.fps");
            ::DeleteFile(save);
            save = dir + RString("autosave.fps");
            ::DeleteFile(save);
            save = dir + RString("save.fps");
            ::DeleteFile(save);

            RString briefing = GetBriefingFile();
            if (briefing.GetLength() > 0)
            {
                CreateChild(new DisplayGetReady(this));
            }
            else
            {
                goto StartMission;
            }
        }
        break;
        case IDD_INTEL_GETREADY:
        {
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_CANCEL)
            {
                if (IsCampaign())
                {
                    RString displayName = Localize(CurrentTemplate.intel.briefingName);
                    if (displayName.GetLength() == 0)
                    {
                        displayName = RString(Glob.header.filename) + RString(".") + RString(Glob.header.worldname);
                    }
                    // void AddMission(RString campaign, RString battle, RString mission, RString displayName);
                    AddMission(CurrentCampaign, CurrentBattle, CurrentMission, displayName);
                }

                // exit to main menu
                StartRandomCutscene(Glob.header.worldname);
                break;
            }
        StartMission:
            CreateChild(new DisplayMission(this));
        }
        break;
        case IDD_MISSION:
        {
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_MAIN_QUIT)
            {
                /*
                                // save state for continue
                                if (!ContinueSaved && GWorld->GetRealPlayer() &&
                   !GWorld->GetRealPlayer()->IsDammageDestroyed())
                                {
                                    char buffer[256];
                                    snprintf(buffer, sizeof(buffer), "%scontinue.fps", (const char
                   *)GetSaveDirectory()); GWorld->SaveBin(buffer);
                                }
                                ContinueSaved = true;
                */
                // escape directly into menu
                if (AutoTest) // Synced from AppConfig in appConfig.cpp
                {
                    Exit(IDC_MAIN_QUIT);
                }
                else
                {
                    StartRandomCutscene(Glob.header.worldname);
                }
            }
            else
            {
                _end = GWorld->GetEndMode();
                ParamEntry* entry = ExtParsMission.FindEntry("debriefing");
                if (entry && !(bool)(*entry))
                {
                    GStats.Update();
                    goto StartOutro;
                }
                else
                {
                    CreateChild(new DisplayDebriefing(this, true));
                }
            }
        }
        break;
        case IDD_DEBRIEFING:
        {
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_OK)
            {
                // restart mission
                RString weapons = GetSaveDirectory() + RString("weapons.cfg");
                ::DeleteFile(weapons);

                // remove continue.fps
                RString dir = GetSaveDirectory();
                RString save = dir + RString("continue.fps");
                ::DeleteFile(save);
                save = dir + RString("autosave.fps");
                ::DeleteFile(save);
                save = dir + RString("save.fps");
                ::DeleteFile(save);

                RString briefing = GetBriefingFile();
                if (briefing.GetLength() > 0)
                {
                    CreateChild(new DisplayGetReady(this));
                    break;
                }
                else
                {
                    goto StartMission;
                }
            }
        StartOutro:
            CreateOutro(this, _end);
        }
        break;
        case IDD_OUTRO:
        {
            Display::OnChildDestroyed(idd, exit);
            if (IsCampaign())
            {
                if (!CheckAward(this))
                {
                    goto StartNextMission;
                }
            }
            else
            {
                StartRandomCutscene(Glob.header.worldname);
            }
        }
        break;
        case IDD_AWARD:
        {
            Display::OnChildDestroyed(idd, exit);
            PoseidonAssert(IsCampaign());
        StartNextMission:
            if (NextMission(this, _end))
            {
                // continue with campaign
            }
            else
            {
                StartRandomCutscene(Glob.header.worldname);
            }
        }
        break;
        case IDD_ARCADE_MAP:
            Display::OnChildDestroyed(idd, exit);
            StartRandomCutscene(Glob.header.worldname);
            break;
        case IDD_OPTIONS:
            Display::OnChildDestroyed(idd, exit);
            break;
        case IDD_SAVE:
        {
            CEdit* edit = dynamic_cast<CEdit*>(_child->GetCtrl(IDC_SIDE_NAME));
            const char* name = edit->GetText();
            PoseidonAssert(name && strlen(name) > 0);
            RString dir = GetTmpSaveDirectory();
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s%s.fps", (const char*)dir, name);
            GLOB_WORLD->SaveBin(buffer, IDS_SAVE_GAME);
            Display::OnChildDestroyed(idd, exit);
        }
        break;
        case IDD_MULTIPLAYER:
            PoseidonAssert(exit == IDC_CANCEL);
            GetNetworkManager().Done();
            Display::OnChildDestroyed(idd, exit);
            StartRandomCutscene(Glob.header.worldname);
            break;
        case IDD_CLIENT:
        case IDD_SERVER:
            // FIX
            GetNetworkManager().Done();
            Display::OnChildDestroyed(idd, exit);
            StartRandomCutscene(Glob.header.worldname);
            break;
        case IDD_LOGIN:
            if (exit == IDC_OK)
            {
                C3DListBox* ctrl = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_LOGIN_USER));
                int index = ctrl->GetCurSel();
                PoseidonAssert(index >= 0);
                Glob.header.playerName = ctrl->GetText(index);
                //				LoadHeader();
                const char* name = Glob.header.playerName;
                Foundation::prefsSetString(AppName, "PlayerName", name);
                Display::OnChildDestroyed(idd, exit);

                // load info from identity
                RString filename = GetUserParams();
                if (QIFStream::FileExists(filename))
                {
                    ParamFile cfg;
                    cfg.Parse(filename);
                    const ParamEntry* identity = cfg.FindEntry("Identity");
                    if (identity)
                    {
                        Glob.header.playerFace = (*identity) >> "face";
                        if (identity->FindEntry("glasses"))
                        {
                            Glob.header.playerGlasses = (*identity) >> "glasses";
                        }
                        Glob.header.playerSpeaker = (*identity) >> "speaker";
                        Glob.header.playerPitch = (*identity) >> "pitch";
                    }
                    UpdateUserProfile();
                }
            }
            else if (exit == IDC_LOGIN_NEW || exit == IDC_LOGIN_EDIT)
            {
                C3DListBox* ctrl = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_LOGIN_USER));
                int index = ctrl->GetCurSel();
                if (index >= 0)
                {
                    Glob.header.playerName = ctrl->GetText(index);
                }
                RString player = Glob.header.playerName;
                Display::OnChildDestroyed(idd, exit);
                CreateChild(new DisplayNewUser(this, player, exit == IDC_LOGIN_EDIT));
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
            }
            break;
        case IDD_NEW_USER:
            if (exit == IDC_OK)
            {
                C3DEdit* edit = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_NEW_USER_NAME));
                Glob.header.playerName = edit->GetText();
                const char* name = Glob.header.playerName;

                DisplayNewUser* disp = static_cast<DisplayNewUser*>((ControlsContainer*)_child);
                if (disp->_edit && stricmp(name, disp->_name) != 0)
                {
                    // rename directory
                    ::MoveFile(RString("Users\\") + disp->_name, RString("Users\\") + Glob.header.playerName);
                }

                //				LoadHeader();
                Foundation::prefsSetString(AppName, "PlayerName", name);
                GetTmpSaveDirectory(); // only create directory

                C3DListBox* ctrl = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_NEW_USER_FACE));
                if (ctrl)
                {
                    int sel = ctrl->GetCurSel();
                    if (sel >= 0)
                    {
                        Glob.header.playerFace = ctrl->GetData(sel);
                    }
                }
                ctrl = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_NEW_USER_GLASSES));
                if (ctrl)
                {
                    int sel = ctrl->GetCurSel();
                    if (sel >= 0)
                    {
                        Glob.header.playerGlasses = ctrl->GetData(sel);
                    }
                }
                ctrl = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_NEW_USER_SPEAKER));
                if (ctrl)
                {
                    int sel = ctrl->GetCurSel();
                    if (sel >= 0)
                    {
                        Glob.header.playerSpeaker = ctrl->GetData(sel);
                    }
                }
                C3DSlider* slider = dynamic_cast<C3DSlider*>(_child->GetCtrl(IDC_NEW_USER_PITCH));
                if (slider)
                {
                    Glob.header.playerPitch = slider->GetThumbPos();
                }

                RString squad;
                edit = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_NEW_USER_SQUAD));
                if (edit)
                {
                    squad = edit->GetText();
                }

                // save info to identity
                RString filename = GetUserParams();
                ParamFile cfg;
                cfg.Parse(filename);
                ParamEntry* identity = cfg.AddClass("Identity");
                identity->Add("face", Glob.header.playerFace);
                identity->Add("glasses", Glob.header.playerGlasses);
                identity->Add("speaker", Glob.header.playerSpeaker);
                identity->Add("pitch", Glob.header.playerPitch);
                identity->Add("squad", squad);
                cfg.Save(filename);
                UpdateUserProfile();
            }
            Display::OnChildDestroyed(idd, exit);
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

void __cdecl CreateDisplaySingleMission(ControlsContainer* parent);

void __cdecl CreateDisplaySingleMission(ControlsContainer* parent)
{
    parent->CreateChild(new DisplaySingleMission(parent));
}

void __cdecl CreateDisplayCampaign(ControlsContainer* parent);

void __cdecl CreateDisplayCampaign(ControlsContainer* parent)
{
    parent->CreateChild(new DisplayCampaignLoad(parent));
}

void __cdecl CreateDisplayMultiplayer(ControlsContainer* parent);

void __cdecl CreateDisplayMultiplayer(ControlsContainer* parent)
{
    parent->CreateChild(new DisplayMultiplayer(parent));
}

void __cdecl CreateDisplayEditor(ControlsContainer* parent);

void __cdecl CreateDisplayEditor(ControlsContainer* parent)
{
    parent->CreateChild(new DisplaySelectIsland(parent));
}

void __cdecl CreateDisplayMods(ControlsContainer* parent);

void __cdecl CreateDisplayMods(ControlsContainer* parent)
{
    parent->CreateChild(new DisplayMods(parent));
}

// The mods root the MODS screen scans for local @<mod> folders: the explicit
// --mods-dir (tests / power users) when given, else the managed mods root.
// The two managed mod roots. Local mods (the user's own) live under --mods-dir or
// GamePaths::ModsDir(); downloaded (workshop) mods under --workshop-dir or
// GamePaths::WorkshopDir(). Keeping them apart preserves each mod's source across
// launches — a downloaded mod stays "workshop" because it sits in the workshop root.
static std::string LocalModsRoot()
{
    const RString& cli = AppConfig::Instance().GetModsDir();
    if (cli.GetLength() > 0)
        return (const char*)cli;
    return Foundation::GamePaths::Instance().ModsDir();
}

static std::string WorkshopModsRoot()
{
    const RString& cli = AppConfig::Instance().GetWorkshopDir();
    if (cli.GetLength() > 0)
        return (const char*)cli;
    return Foundation::GamePaths::Instance().WorkshopDir();
}

static std::string LowerStr(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Build the catalog rows from the mods actually on disk: local mods (source=Local)
// from LocalModsRoot() + downloaded mods (source=Workshop) from WorkshopModsRoot().
// A mod whose @<folder> is in the current mod path (ModSystem::GetModList) is
// Active+ticked, else Downloaded+unticked. The remote catalog is merged on top
// later (MergeWorkshopMods) for not-yet-downloaded workshop mods.
static AutoArray<ModRow> ScanModRows()
{
    AutoArray<ModRow> rows;

    std::set<std::string> active; // lowercased "@folder" basenames currently mounted
    std::istringstream ss(std::string((const char*)Poseidon::ModSystem::GetModList()));
    std::string seg;
    while (std::getline(ss, seg, ';'))
    {
        if (seg.empty())
            continue;
        active.insert(LowerStr(std::filesystem::path(seg).filename().string()));
    }

    std::set<std::string> seen; // lowercased catalog modId/folder id, so both roots list once
    auto scan = [&](const std::string& root, ModRowSource source)
    {
        for (const ScannedMod& sm : ScanLocalMods(root))
        {
            if (!seen.insert(LowerStr(sm.modId)).second)
                continue;
            ModRow r;
            r.modId = sm.modId.c_str();
            r.folderName = sm.folderName.c_str();
            r.name = sm.name.c_str();
            r.version = sm.version.c_str();
            r.sizeBytes = sm.sizeBytes;
            r.source = source;
            const bool isActive = active.count(LowerStr(sm.folderName)) > 0;
            r.state = isActive ? ModRowState::Active : ModRowState::Downloaded;
            r.checked = isActive;
            rows.Add(r);
        }
    };
    scan(LocalModsRoot(), ModRowSource::Local);
    scan(WorkshopModsRoot(), ModRowSource::Workshop);
    return rows;
}

Control* DisplayMods::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_MODS_LIST)
    {
        CModsList* list = new CModsList(this, idc, cls);
        list->SetRows(ScanModRows());
        return list;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayMods::SeedTestMods(int count)
{
    CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
    if (!list)
        return;
    AutoArray<ModRow> rows;
    for (int i = 0; i < count; i++)
    {
        ModRow r;
        char buf[64];
        snprintf(buf, sizeof(buf), "@testmod%d", i + 1);
        r.modId = buf;
        r.folderName = buf;
        snprintf(buf, sizeof(buf), "Test Mod %d", i + 1);
        r.name = buf;
        r.version = "1.0";
        r.sizeBytes = static_cast<int64_t>(i + 1) * 10 * 1024 * 1024;
        r.state = static_cast<ModRowState>(i % 3);                              // cycles Missing/Downloaded/Active
        r.source = (i % 2 == 0) ? ModRowSource::Workshop : ModRowSource::Local; // odd rows Local
        r.checked = (i % 2 == 0);
        rows.Add(r);
    }
    list->SetRows(rows);
    // Keep the active sort + its caret consistent with the freshly-seeded rows.
    list->Sort(static_cast<ModsSortColumn>(_sortColumn), _sortAscending);
    UpdateSortCarets();
}

// libcurl download bridged to the agnostic DownloadFileFn. The C progress
// callback can't carry captures, so onBytes is reached through a void* context.
static DownloadFileFn MakeMasterServerTransport(std::string proxy)
{
    return [proxy](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                   const std::function<bool()>& cancelled, std::string& error) -> bool
    {
        struct Ctx
        {
            const std::function<void(int64_t, int64_t)>* cb;
            const std::function<bool()>* cancelled;
        } ctx{&onBytes, &cancelled};
        const bool ok = DownloadMasterServerServiceFile(
            task.url.c_str(), proxy.empty() ? nullptr : proxy.c_str(), task.destPath.c_str(), &ctx,
            [](void* instance, int64_t received, int64_t total)
            {
                auto* c = static_cast<Ctx*>(instance);
                if (c != nullptr && c->cb != nullptr && *c->cb)
                    (*c->cb)(received, total);
            },
            [](void* instance) -> bool
            {
                // Abort the libcurl transfer promptly when the worker is cancelled.
                auto* c = static_cast<Ctx*>(instance);
                return c != nullptr && c->cancelled != nullptr && *c->cancelled && (*c->cancelled)();
            });
        if (!ok)
            error = "download failed";
        return ok;
    };
}

// Bare mod id (no leading '@') — ModInstallDir/GetModInstallStatus prepend it.
static std::string BareModId(const RString& modId)
{
    std::string id = (const char*)modId;
    if (!id.empty() && id[0] == '@')
        id.erase(0, 1);
    return id;
}

// Download tasks for every ticked-but-not-downloaded row that carries a URL.
// Each task downloads the zstd-wrapped mod artifact next to its install dir then unpacks it
// into <root>/<folderName> (so GetModInstallStatus sees it installed). Returns true if any.
static bool BuildCheckedDownloadTasks(CModsList* list, const std::string& root, std::vector<DownloadTask>& tasks)
{
    const AutoArray<ModRow>& rows = list->GetRows();
    for (int i = 0; i < rows.Size(); i++)
    {
        const ModRow& r = rows[i];
        if (!r.checked || r.state != ModRowState::Missing || r.downloadUrl.GetLength() == 0)
            continue;
        const std::string modId = BareModId(r.modId);
        const std::string folderName = (const char*)r.folderName;
        const std::string destDir = ModInstallDir(root, modId, folderName);
        DownloadTask t;
        t.label = (const char*)r.name;
        t.url = (const char*)r.downloadUrl;
        t.destPath = destDir + ".pbo.zst";
        t.expectedBytes = r.sizeBytes;
        t.postStep = [destDir, modId, name = std::string((const char*)r.name),
                      version = std::string((const char*)r.version), folderName,
                      downloadUrl = std::string((const char*)r.downloadUrl),
                      sizeBytes = r.sizeBytes](const DownloadTask& task, std::string& error) -> bool
        {
            if (!ModArchive::Unpack(task.destPath.c_str(), destDir.c_str(), &error))
                return false;
            return WriteInstalledModManifest(destDir, modId, name, version, folderName, downloadUrl, sizeBytes, &error);
        };
        tasks.push_back(std::move(t));
    }
    return !tasks.empty();
}

static RString LocalizedDownloadUnitNoun(const char* unitNoun, int count)
{
    const bool plural = count != 1;
    if (unitNoun != nullptr && stricmp(unitNoun, "mod") == 0)
        return LocalizeString(plural ? "STR_DISP_MODS_NOUN_MODS" : "STR_DISP_MODS_NOUN_MOD");
    return LocalizeString(plural ? "STR_DISP_MODS_NOUN_ADDONS" : "STR_DISP_MODS_NOUN_ADDON");
}

// After a successful download, flip the now-installed rows off Missing so
// BuildModPath mounts them. Preserves ticks (SetRows copies the rows back).
static void RefreshInstalledStates(CModsList* list, const std::string& root)
{
    AutoArray<ModRow> rows = list->GetRows();
    bool changed = false;
    for (int i = 0; i < rows.Size(); i++)
    {
        if (rows[i].state != ModRowState::Missing)
            continue;
        const ModInstallStatus st = GetModInstallStatus(root, BareModId(rows[i].modId), (const char*)rows[i].version);
        if (st != ModInstallStatus::NotInstalled)
        {
            rows[i].state = ModRowState::Downloaded;
            changed = true;
        }
    }
    if (changed)
        list->SetRows(rows);
}

void DisplayMods::OnButtonClicked(int idc)
{
    if (idc == IDC_CANCEL)
    {
        _exitWhenClose = idc;
        ControlObjectContainerAnim* notebook = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_MODS_NOTEBOOK));
        if (notebook)
            notebook->Close();
        else
            Exit(idc); // no notebook control (minimal/absent rsc) — close at once
        return;
    }

    if (idc == IDC_MODS_APPLY)
    {
        // Apply the ticked set: re-mount the game in place with exactly those mods
        // (RequestRemountWithMods, serviced at the next AppIdle so the teardown runs
        // between frames). Menu-only (GModeIntro) — a re-mount mid-mission would evict
        // assets the simulation still references. An empty set re-mounts the base game.
        CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
        if (list == nullptr || GApp == nullptr || GWorld == nullptr || GWorld->GetMode() != GModeIntro)
            return;

        // If any ticked mod isn't downloaded yet, gate the re-mount behind the
        // download dialog; OnChildDestroyed(IDD_MODS_DOWNLOAD, IDC_OK) re-mounts
        // once it reports success.
        std::vector<DownloadTask> tasks;
        if (BuildCheckedDownloadTasks(list, WorkshopModsRoot(), tasks))
        {
            std::string proxy = (const char*)GetNetworkProxy();
            CreateChild(new DisplayModDownload(this, std::move(tasks), MakeMasterServerTransport(proxy)));
            return;
        }

        RString modPath = list->BuildModPath(LocalModsRoot().c_str(), WorkshopModsRoot().c_str());
        GApp->RequestRemountWithMods((const char*)modPath);
        return;
    }

    if (idc == IDC_MODS_SOURCE)
    {
        // Cycle the Source filter: All -> Workshop -> Local -> All.
        if (CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST)))
        {
            int mode = (list->GetSourceMode() + 1) % 3;
            list->SetSourceMode(mode);
            UpdateSourceButton(mode);
        }
        return;
    }

    if (idc == IDC_MODS_FILTER)
    {
        // Open the name-filter dialog seeded with the current filter; OnChildDestroyed
        // applies the result on OK.
        CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
        CreateChild(new DisplayModsFilter(this, list != nullptr ? list->GetNameFilter() : RString()));
        return;
    }

    // A column-header click sorts by that column; clicking the active column
    // again flips the direction (mirrors the MP server browser).
    ModsSortColumn col;
    switch (idc)
    {
        case IDC_MODS_COL_NAME:
            col = MSCName;
            break;
        case IDC_MODS_COL_VERSION:
            col = MSCVersion;
            break;
        case IDC_MODS_COL_SIZE:
            col = MSCSize;
            break;
        case IDC_MODS_COL_STATE:
            col = MSCState;
            break;
        case IDC_MODS_COL_SOURCE:
            col = MSCSource;
            break;
        default:
            Display::OnButtonClicked(idc);
            return;
    }
    _sortAscending = (_sortColumn == static_cast<int>(col)) ? !_sortAscending : true;
    _sortColumn = static_cast<int>(col);
    if (CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST)))
        list->Sort(col, _sortAscending);
    UpdateSortCarets();
}

bool DisplayMods::DoControllerUiAction(ControllerUiAction action)
{
    switch (action)
    {
        case ControllerUiAction::Preview:
            OnButtonClicked(IDC_MODS_APPLY);
            return true;
        case ControllerUiAction::Delete:
            OnButtonClicked(IDC_MODS_FILTER);
            return true;
        case ControllerUiAction::PreviousTab:
        case ControllerUiAction::NextTab:
            OnButtonClicked(IDC_MODS_SOURCE);
            return true;
        default:
            break;
    }
    return Display::DoControllerUiAction(action);
}

void DisplayMods::ApplyInitialSort()
{
    _sortColumn = MSCName;
    _sortAscending = true;
    CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
    if (list != nullptr)
        list->Sort(MSCName, _sortAscending);
    UpdateSortCarets();
    UpdateSourceButton(list != nullptr ? list->GetSourceMode() : 0);
    UpdateFilterButton();
}

void DisplayMods::UpdateSourceButton(int mode)
{
    C3DActiveText* btn = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MODS_SOURCE));
    if (btn == nullptr)
        return;
    RString label = (mode == 1)   ? LocalizeString("STR_DISP_MODS_SOURCE_WORKSHOP")
                    : (mode == 2) ? LocalizeString("STR_DISP_MODS_SOURCE_LOCAL")
                                  : LocalizeString("STR_DISP_MODS_SOURCE_ALL");
    btn->SetText(label);
}

void DisplayMods::UpdateFilterButton()
{
    C3DActiveText* btn = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MODS_FILTER));
    if (btn == nullptr)
        return;
    CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
    RString f = list != nullptr ? list->GetNameFilter() : RString();
    if (f.GetLength() > 0)
        btn->SetText(Format(LocalizeString("STR_DISP_MODS_FILTER_ACTIVE"), (const char*)f));
    else
        btn->SetText(LocalizeString("STR_DISP_MODS_FILTER"));
}

void DisplayMods::OnChildDestroyed(int idd, int exit)
{
    if (idd == IDD_MODS_FILTER && exit == IDC_OK)
    {
        C3DEdit* edit = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_MODS_FILTER_NAME));
        CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
        if (edit != nullptr && list != nullptr)
        {
            list->SetNameFilter(edit->GetText());
            UpdateFilterButton();
        }
    }
    else if (idd == IDD_MODS_DOWNLOAD && exit == IDC_OK)
    {
        // The download dialog confirmed every ticked mod is now installed — flip
        // their rows off Missing and run the re-mount Apply deferred to it.
        CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
        if (list != nullptr && GApp != nullptr && GWorld != nullptr && GWorld->GetMode() == GModeIntro)
        {
            RefreshInstalledStates(list, WorkshopModsRoot());
            RString modPath = list->BuildModPath(LocalModsRoot().c_str(), WorkshopModsRoot().c_str());
            GApp->RequestRemountWithMods((const char*)modPath);
        }
    }
    Display::OnChildDestroyed(idd, exit);
}

Control* DisplayModsFilter::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_MODS_FILTER_NAME)
    {
        C3DEdit* edit = new C3DEdit(this, idc, cls);
        edit->SetText(_filterName);
        return edit;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

// Background workshop-catalog fetch: a detached worker fills this; OnSimulate polls
// `done` and merges `mods` once. Held by shared_ptr so the worker outlives a
// quickly-closed DisplayMods (it writes here, never into the display).
struct ModsWorkshopFetch
{
    std::mutex mtx;
    std::vector<MasterServerServiceModCatalogEntry> mods;
    bool done = false;
    bool ok = false;
};

void DisplayMods::OnSimulate(EntityAI* vehicle)
{
    // Kick the remote catalog fetch once. Skipped under autotest so UI tests stay off
    // the network (workshop rows are exercised deterministically via triSeedWorkshopMods).
    if (!_workshopStarted && !AutoTest)
    {
        _workshopStarted = true;
        StartWorkshopFetch();
    }
    if (_workshopFetch != nullptr && !_workshopMerged)
    {
        std::vector<MasterServerServiceModCatalogEntry> ready;
        bool take = false;
        {
            std::lock_guard<std::mutex> lk(_workshopFetch->mtx);
            if (_workshopFetch->done)
            {
                _workshopMerged = true;
                if (_workshopFetch->ok)
                {
                    ready = std::move(_workshopFetch->mods);
                    take = true;
                }
            }
        }
        if (take)
            MergeWorkshopMods(ready);
    }
    Display::OnSimulate(vehicle);
}

void DisplayMods::StartWorkshopFetch()
{
    auto fetch = std::make_shared<ModsWorkshopFetch>();
    _workshopFetch = fetch;
    std::string host = (const char*)GetNetworkMasterServer();
    std::string proxy = (const char*)GetNetworkProxy();
    std::thread(
        [fetch, host, proxy]()
        {
            std::vector<MasterServerServiceModCatalogEntry> mods;
            const bool ok =
                FetchMasterServerServiceModList(host.c_str(), "", proxy.empty() ? nullptr : proxy.c_str(), mods);
            std::lock_guard<std::mutex> lk(fetch->mtx);
            fetch->mods = std::move(mods);
            fetch->ok = ok;
            fetch->done = true;
        })
        .detach();
}

void DisplayMods::MergeWorkshopMods(const std::vector<MasterServerServiceModCatalogEntry>& catalog)
{
    CModsList* list = dynamic_cast<CModsList*>(GetCtrl(IDC_MODS_LIST));
    if (list == nullptr)
        return;

    AutoArray<ModRow> rows;
    const AutoArray<ModRow>& current = list->GetRows();
    std::set<std::string> existing; // lowercased catalog modId/folder id already in the list
    for (int i = 0; i < current.Size(); i++)
    {
        rows.Add(current[i]);
        existing.insert(LowerStr((const char*)current[i].modId));
    }

    const std::string root = WorkshopModsRoot();
    for (const MasterServerServiceModCatalogEntry& e : catalog)
    {
        if (e.modId.empty())
            continue;
        if (existing.count(LowerStr(e.modId)) > 0)
            continue; // a local mod already shows this id — prefer the installed one
        existing.insert(LowerStr(e.modId));

        ModRow r;
        r.modId = e.modId.c_str();
        r.folderName = (e.folderName.empty() ? ("@" + e.modId) : e.folderName).c_str();
        r.name = e.name.empty() ? e.modId.c_str() : e.name.c_str();
        r.version = e.version.c_str();
        r.sizeBytes = e.sizeBytes;
        r.source = ModRowSource::Workshop;
        r.downloadUrl = e.downloadUrl.c_str();
        const ModInstallStatus st = GetModInstallStatus(root, e.modId, e.version);
        r.state = (st == ModInstallStatus::NotInstalled) ? ModRowState::Missing : ModRowState::Downloaded;
        r.checked = false; // shown only — not activated/downloaded for now
        rows.Add(r);
    }

    list->SetRows(rows);
    list->Sort(static_cast<ModsSortColumn>(_sortColumn), _sortAscending);
    UpdateSortCarets();
    UpdateSourceButton(list->GetSourceMode());
    UpdateFilterButton();
}

void DisplayMods::UpdateSortCarets()
{
    // Shared with the MP server browser: show the up/down arrow on the active sort
    // column, hide the others. The arrow textures are the same the MP browser uses.
    const RString up = "\\misc\\sipkau.paa";
    const RString down = "\\misc\\sipkad.paa";
    UpdateSortCaret(GetCtrl(IDC_MODS_ICON_NAME), _sortColumn == MSCName, _sortAscending, up, down);
    UpdateSortCaret(GetCtrl(IDC_MODS_ICON_SOURCE), _sortColumn == MSCSource, _sortAscending, up, down);
    UpdateSortCaret(GetCtrl(IDC_MODS_ICON_VERSION), _sortColumn == MSCVersion, _sortAscending, up, down);
    UpdateSortCaret(GetCtrl(IDC_MODS_ICON_SIZE), _sortColumn == MSCSize, _sortAscending, up, down);
    UpdateSortCaret(GetCtrl(IDC_MODS_ICON_STATE), _sortColumn == MSCState, _sortAscending, up, down);
}

// DisplayModDownload — the download-progress dialog (agnostic)

DisplayModDownload::DisplayModDownload(ControlsContainer* parent, std::vector<DownloadTask> tasks,
                                       DownloadFileFn transport, const char* unitNoun, std::function<double()> now)
    : Display(parent), _tasks(std::move(tasks)), _worker(std::move(transport), std::move(now)),
      _unitNoun(unitNoun != nullptr ? unitNoun : "addon"), _phase(PhasePrompt), _started(false)
{
    _enableSimulation = false;
    Load("RscDisplayModDownload");

    int64_t total = 0;
    for (const DownloadTask& t : _tasks)
        total += (t.expectedBytes > 0 ? t.expectedBytes : 0);
    const int count = static_cast<int>(_tasks.size());
    RString noun = LocalizedDownloadUnitNoun((const char*)_unitNoun, count);
    _prompt = Format(LocalizeString("STR_DISP_MODS_DOWNLOAD_PROMPT"), count, (const char*)noun,
                     DownloadProgress::FormatBytes(total).c_str());
    SetNotebookText(IDC_MODS_DL_PROMPT, _prompt);
}

void DisplayModDownload::SetNotebookText(int idc, RString text)
{
    if (C3DStatic* s = dynamic_cast<C3DStatic*>(GetCtrl(idc)))
        s->SetText(text);
    else if (C3DActiveText* a = dynamic_cast<C3DActiveText*>(GetCtrl(idc)))
        a->SetText(text);
}

void DisplayModDownload::StartDownload()
{
    _worker.Start(_tasks); // cancels + restarts if a prior run failed (Retry)
    _started = true;
    _phase = PhaseRunning;
    SetNotebookText(IDC_MODS_DL_PROMPT, "");
    if (CActiveText* btn = dynamic_cast<CActiveText*>(GetCtrl(IDC_MODS_DOWNLOAD_GO)))
    {
        btn->SetText(LocalizeString("STR_DISP_MODS_DOWNLOAD"));
        btn->EnableCtrl(false); // dimmed while the worker runs; Cancel still active
    }
}

void DisplayModDownload::ApplyView()
{
    DownloadSnapshot s = _worker.Poll();
    if (s.failed)
        _phase = PhaseFailed;
    else if (s.done)
        _phase = PhaseDone;

    DownloadDialogView v = BuildDownloadDialogView(s, (const char*)_unitNoun);
    SetNotebookText(IDC_MODS_DL_CURRENT_LABEL, v.currentLine.c_str());
    SetNotebookText(IDC_MODS_DL_OVERALL_LABEL, v.overallLine.c_str());
    SetNotebookText(IDC_MODS_DL_STATUS, v.statusLine.c_str());

    if (ControlObjectContainer* nb = dynamic_cast<ControlObjectContainer*>(GetCtrl(IDC_MODS_DL_NOTEBOOK)))
    {
        // Layouts match RscDisplayModDownload's CurrentTrack / OverallTrack.
        const ProgressBarLayout cur{0.120f, 0.380f, 0.760f, 0.035f};
        const ProgressBarLayout all{0.120f, 0.540f, 0.760f, 0.035f};
        RenderProgressBar(*nb, IDC_MODS_DL_CURRENT_FILL, cur, v.currentFraction);
        RenderProgressBar(*nb, IDC_MODS_DL_OVERALL_FILL, all, v.overallFraction);
    }

    if (CActiveText* btn = dynamic_cast<CActiveText*>(GetCtrl(IDC_MODS_DOWNLOAD_GO)))
    {
        if (_phase == PhaseDone)
        {
            btn->SetText(LocalizeString("STR_DISP_MODS_CONTINUE"));
            btn->EnableCtrl(true);
        }
        else if (_phase == PhaseFailed)
        {
            btn->SetText(LocalizeString("STR_DISP_MODS_RETRY"));
            btn->EnableCtrl(true);
        }
    }
}

void DisplayModDownload::OnButtonClicked(int idc)
{
    if (idc == IDC_MODS_DOWNLOAD_GO)
    {
        if (_phase == PhaseDone)
        {
            Exit(IDC_OK); // downloads landed — let the opener proceed (re-mount)
            return;
        }
        StartDownload(); // Prompt -> run; Failed -> retry
        return;
    }
    if (idc == IDC_CANCEL)
    {
        _worker.Cancel();
        Display::OnButtonClicked(idc); // base exits with IDC_CANCEL
        return;
    }
    Display::OnButtonClicked(idc);
}

void DisplayModDownload::OnSimulate(EntityAI* vehicle)
{
    if (_started)
        ApplyView();
    Display::OnSimulate(vehicle);
}

void DisplayMain::OnButtonClicked(int idc)
{
    // Dispatch registered game module buttons via registry
    if (auto* mod = GameModuleRegistry::FindByIDC(idc))
    {
        mod->menuAction(this);
        return;
    }

    // Non-module buttons
    switch (idc)
    {
        case IDC_MAIN_OPTIONS:
            // Offer the Credits button only when CfgCredits is configured (the demo
            // ships none, matching the original 2001 demo).
            if (Res.FindEntry("RscOptionsShell"))
                CreateChild(new OptionsShell(this, true, IsCreditsConfigured()));
            else
                CreateChild(new DisplayOptions(this, true, IsCreditsConfigured()));
            break;
        case IDC_MAIN_CUSTOM:
            CreateChild(new DisplayCustomArcade(this));
            break;
        case IDC_MAIN_QUIT:
            Exit(IDC_MAIN_QUIT);
            break;
        case IDC_MAIN_PLAYER:
            CreateChild(new DisplayLogin(this));
            break;
        case IDC_MAIN_CREDITS:
            PlayCreditsCutscene(this);
            break;
        case IDC_MAIN_LOAD:
            CreateChild(new DisplayGame(this));
            break;
        case IDC_MAIN_SAVE:
            CreateChild(new DisplaySave(this));
            break;
        default:
            // no escape - only QUIT
            break;
    }
}

bool DisplayMain::DoControllerUiAction(ControllerUiAction action)
{
    if (action == ControllerUiAction::Pause)
    {
        OnButtonClicked(IDC_MAIN_OPTIONS);
        return true;
    }
    return Display::DoControllerUiAction(action);
}

void DisplayMain::OnDraw(EntityAI* vehicle, float alpha)
{
    // IDC_MAIN_PLAYER's resource class (Player in RscDisplayMain) has no explicit
    // `type`, so some configs create it as CStatic instead of CActiveText.
    // Tolerate either rather than null-deref on dynamic_cast failure.
    SetMainPlayerControlText(GetCtrl(IDC_MAIN_PLAYER));

    Display::OnDraw(vehicle, alpha);
}

} // namespace Poseidon
