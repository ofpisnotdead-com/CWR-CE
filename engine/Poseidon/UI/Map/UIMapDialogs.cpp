#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Map/UIMapCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

class CHTMLContainer;
extern void AddObjective(CHTMLContainer* html, int src, int dst, int value);
extern Camera OriginalCamera;

namespace Poseidon
{
using Poseidon::Foundation::QSort;

namespace
{
void UpdateDebriefingActionLabels(DisplayDebriefing* display)
{
    if (!display)
        return;

    if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
    {
        if (auto* ctrl = dynamic_cast<CActiveText*>(display->GetCtrl(IDC_DEBRIEFING_RESTART)))
        {
            ctrl->SetText(LocalizeString(IDS_DISP_CLIENT_READY));
            ctrl->ShowCtrl(false);
        }
        return;
    }

    if (GetNetworkManager().GetGameState() > NGSNone)
    {
        if (auto* ctrl = dynamic_cast<CActiveText*>(display->GetCtrl(IDC_DEBRIEFING_RESTART)))
            ctrl->SetText(LocalizeString(IDS_DISP_CLIENT_READY));
        if (auto* ctrl = dynamic_cast<CActiveText*>(display->GetCtrl(IDC_CANCEL)))
            ctrl->SetText(LocalizeString(IDS_DISP_DISCONNECT));
    }
}
} // namespace

DisplayMainMap::DisplayMainMap(ControlsContainer* parent) : DisplayMap(parent, "RscDisplayMainMap")
{
    _enableDisplay = false;

    ShowWarrant(false);
    ShowWalkieTalkie(false);
    ShowGPS(true);
    for (int i = sensorsMap.Size() - 1; i >= 0; i--)
    {
        Vehicle* veh = sensorsMap[i];
        if (!veh)
        {
            continue;
        }
        Detector* det = dyn_cast<Detector>(veh);
        PoseidonAssert(det);
        if (det->IsActive() && !det->IsRepeating())
        {
            continue;
        }
        if (det->GetActivationBy() >= ASAAlpha && det->GetActivationBy() <= ASAJuliet)
        {
            ShowWalkieTalkie(true);
            break;
        }
    }
    //	SwitchDebriefing(false);
}

DisplayInsertMarker::DisplayInsertMarker(ControlsContainer* parent, float x, float y, float cx, float cy, float cw,
                                         float ch, bool enableSimulation)
    : Display(parent)
{
    _enableSimulation = enableSimulation;

    InputSubsystem::Instance().ChangeGameFocus(+1);

    Load("RscDisplayInsertMarker");
    _x = x;
    _y = y;
    _cx = cx;
    _cy = cy;
    _cw = cw;
    _ch = ch;
    m_dir = 0;

    _exitKey = -1;
    _exitVK = -1;

    _picture = 0;
    _color = 0;
    UpdatePicture();

    Control* picture = dynamic_cast<Control*>(GetCtrl(IDC_INSERT_MARKER_PICTURE));
    Control* edit = dynamic_cast<Control*>(GetCtrl(IDC_INSERT_MARKER));
    if (picture && edit)
    {
        float wP = picture->W();
        float wE = edit->W();
        float w = wP + wE;
        float hP = picture->H();
        float hE = edit->H();
        float h = floatMax(hP, hE);
        y -= 0.5 * h;
        x -= 0.5 * wP;
        saturate(x, _cx, _cx + _cw - w);
        saturate(y, _cy, _cy + _ch - h);
        picture->SetPos(x, y + 0.5 * (h - hP), wP, hP);
        edit->SetPos(x + wP, y + 0.5 * (h - hE), wE, hE);
    }
}

DisplayInsertMarker::~DisplayInsertMarker()
{
    InputSubsystem::Instance().ChangeGameFocus(-1);
}

void DisplayInsertMarker::OnSimulate(EntityAI* vehicle)
{
    auto& input = InputSubsystem::Instance();
    // !!! Do not use GetKeysToDo - returns always false in this context

    if (input.IsKeyPressed(SDL_SCANCODE_ESCAPE))
    {
        input.ConsumeKeyPress(SDL_SCANCODE_ESCAPE);
        _exitKey = IDC_CANCEL;
        if (_exitVK == IDC_CANCEL)
        {
            Exit(IDC_CANCEL);
        }
        return;
    }
    if (input.IsKeyPressed(SDL_SCANCODE_RETURN))
    {
        input.ConsumeKeyPress(SDL_SCANCODE_RETURN);
        _exitKey = IDC_OK;
        if (_exitVK == IDC_OK)
        {
            Exit(IDC_OK);
        }
        return;
    }
    if (input.IsKeyPressed(SDL_SCANCODE_KP_ENTER))
    {
        input.ConsumeKeyPress(SDL_SCANCODE_KP_ENTER);
        _exitKey = IDC_OK;
        if (_exitVK == IDC_OK)
        {
            Exit(IDC_OK);
        }
        return;
    }
    if (input.IsKeyPressed(SDL_SCANCODE_DOWN))
    {
        const bool pressingAlt = input.IsKeyDown(SDL_SCANCODE_LALT) || input.GetKey(SDL_SCANCODE_RALT);
        input.ConsumeKeyPress(SDL_SCANCODE_DOWN);
        if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.GetKey(SDL_SCANCODE_RSHIFT))
        {
            NextColor(pressingAlt ? 3 : 1);
        }
        else if (input.IsKeyDown(SDL_SCANCODE_LCTRL) || input.GetKey(SDL_SCANCODE_RCTRL))
        {
            ChangeDir(pressingAlt ? -90 : -30);
        }
        else
        {
            NextPicture(pressingAlt ? 5 : 1);
        }
        UpdatePicture();
    }
    if (input.IsKeyPressed(SDL_SCANCODE_UP))
    {
        const bool pressingAlt = input.IsKeyDown(SDL_SCANCODE_LALT) || input.GetKey(SDL_SCANCODE_RALT);
        input.ConsumeKeyPress(SDL_SCANCODE_UP);
        if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.GetKey(SDL_SCANCODE_RSHIFT))
        {
            PrevColor(pressingAlt ? 3 : 1);
        }
        else if (input.IsKeyDown(SDL_SCANCODE_LCTRL) || input.GetKey(SDL_SCANCODE_RCTRL))
        {
            ChangeDir(pressingAlt ? +90 : +30);
        }
        else
        {
            PrevPicture(pressingAlt ? 5 : 1);
        }
        UpdatePicture();
    }
    Display::OnSimulate(vehicle);
}

void DisplayInsertMarker::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_CANCEL:
        case IDC_OK:
            _exitVK = idc;
            if (_exitKey == idc)
            {
                Exit(idc);
            }
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

// Control *DisplayInsertMarker::OnCreateCtrl(int type, int idc, const ParamEntry &cls);
void DisplayInsertMarker::Destroy()
{
    Display::Destroy();

    if (_exit != IDC_OK)
    {
        return;
    }

    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_INSERT_MARKER));
    PoseidonAssert(edit);
    _text = edit->GetText();
}

void DisplayInsertMarker::UpdatePicture()
{
    CStatic* ctrl = dynamic_cast<CStatic*>(GetCtrl(IDC_INSERT_MARKER_PICTURE));
    if (ctrl)
    {
        const ParamEntry& markers = Pars >> "CfgMarkers";
        if (_picture < 0 || _picture >= markers.GetEntryCount())
        {
            return;
        }
        const ParamEntry& colors = Pars >> "CfgMarkerColors";
        if (_color < 0 || _color >= colors.GetEntryCount())
        {
            return;
        }

        const ParamEntry& markerCfg = markers.GetEntry(_picture);
        RString picture = markerCfg >> "icon";
        const ParamEntry& colorCfg = colors.GetEntry(_color);
        PackedColor color;
        if (stricmp(colorCfg.GetName(), "default") == 0)
        {
            color = GetPackedColor(markerCfg >> "color");
        }
        else
        {
            color = GetPackedColor(colorCfg >> "color");
        }
        ctrl->SetText(picture);
        ctrl->SetFtColor(color);

        ctrl->SetAzimut((m_dir % 360) * (H_PI / 180.0));
    }
}

void DisplayInsertMarker::PrevPicture(int step)
{
    const ParamEntry& markers = Pars >> "CfgMarkers";
    int n = markers.GetEntryCount();
    _picture -= step;
    while (_picture < 0)
    {
        _picture += n;
    } 
}

void DisplayInsertMarker::NextPicture(int step)
{
    const ParamEntry& markers = Pars >> "CfgMarkers";
    int n = markers.GetEntryCount();
	_picture += step;
    while (_picture >= n)
    {
        _picture -= n;
    }
}

void DisplayInsertMarker::PrevColor(int step)
{
    const ParamEntry& colors = Pars >> "CfgMarkerColors";
    int n = colors.GetEntryCount();
    _color -= step;
    while (_color < 0)
    {
        _color += n;
    }
}

void DisplayInsertMarker::NextColor(int step)
{
    const ParamEntry& colors = Pars >> "CfgMarkerColors";
    int n = colors.GetEntryCount();
    _color += step;
    while (_color >= n)
    {
        _color -= n;
    }
}

void DisplayInsertMarker::ChangeDir(int delta)
{
    m_dir += delta;
}

DisplayGetReady::DisplayGetReady(ControlsContainer* parent) : DisplayMap(parent, "RscDisplayGetReady")
{
    _enableSimulation = false;
    _enableDisplay = false;
    ShowCompass(false);
    ShowWatch(false);
    ShowWalkieTalkie(false);
    ShowGPS(false);
    const ParamEntry* cls = ExtParsMission.FindEntry("showMap");
    ShowMap(!cls ? true : (*cls));
    cls = ExtParsMission.FindEntry("showNotepad");
    ShowNotepad(!cls ? true : (*cls));

    //	SwitchDebriefing(false);

    AIUnit* unit = GWorld->FocusOn();
    _selectWeapons = unit && unit->IsGroupLeader();

    RString weapons = GetSaveDirectory() + RString("weapons.cfg");
    if (QIFStream::FileExists(weapons))
    {
        _weaponsInfo.Load(weapons);
        // create html page
        CreateWeaponsPage();
    }
    else
    {
        UpdateWeaponsInBriefing();
    }

    GChatList.SetRect(0.05, 0.90, 0.9, 0.02);
    GChatList.SetRows(4);

    if (GetNetworkManager().GetGameState() >= NGSCreate && ActualChatChannel() != CCSide)
    {
        SetChatChannel(CCSide);
        if (GWorld->ChatChannel())
        {
            GWorld->ChatChannel()->ResetHUD();
        }
        if (GWorld->VoiceChat())
        {
            GWorld->VoiceChat()->ResetHUD();
        }
        GWorld->OnChannelChanged();
    }

    _soundPlanPlayed = false;
    _soundNotesPlayed = false;
    _soundGearPlayed = false;
    _soundGroupPlayed = false;
    int index = _briefing->CurrentSection();
    const HTMLSection& section = _briefing->GetSection(index);
    if (section.names.Size() > 0)
    {
        SwitchBriefingSection(section.names[0]);
    }
}

DisplayGetReady::DisplayGetReady(ControlsContainer* parent, RString resource) : DisplayMap(parent, resource)
{
    _enableSimulation = false;
    _enableDisplay = false;
    ShowCompass(false);
    ShowWatch(false);
    ShowWalkieTalkie(false);
    ShowGPS(false);
    const ParamEntry* cls = ExtParsMission.FindEntry("showMap");
    ShowMap(!cls ? true : (*cls));
    cls = ExtParsMission.FindEntry("showNotepad");
    ShowNotepad(!cls ? true : (*cls));

    //	SwitchDebriefing(false);

    AIUnit* unit = GWorld->FocusOn();
    _selectWeapons = unit && unit->IsGroupLeader();

    RString weapons = GetSaveDirectory() + RString("weapons.cfg");
    if (QIFStream::FileExists(weapons))
    {
        _weaponsInfo.Load(weapons);
        // create html page
        CreateWeaponsPage();
    }
    else
    {
        UpdateWeaponsInBriefing();
    }

    GChatList.SetRect(0.05, 0.90, 0.9, 0.02);
    GChatList.SetRows(4);

    if (GetNetworkManager().GetGameState() >= NGSCreate && ActualChatChannel() != CCSide)
    {
        SetChatChannel(CCSide);
        if (GWorld->ChatChannel())
        {
            GWorld->ChatChannel()->ResetHUD();
        }
        if (GWorld->VoiceChat())
        {
            GWorld->VoiceChat()->ResetHUD();
        }
        GWorld->OnChannelChanged();
    }
}

DisplayGetReady::~DisplayGetReady()
{
    GChatList.SetRect(0.05, 0.02, 0.9, 0.02);
    GChatList.SetRows(4);
}

} // namespace Poseidon
extern Poseidon::Foundation::RString GBriefingOnPlan;
extern Poseidon::Foundation::RString GBriefingOnNotes;
extern Poseidon::Foundation::RString GBriefingOnGear;
extern Poseidon::Foundation::RString GBriefingOnGroup;
namespace Poseidon
{

void DisplayGetReady::SwitchBriefingSection(RString section)
{
    DisplayMap::SwitchBriefingSection(section);
    _sound = nullptr;
    if (stricmp(section, "__plan") == 0)
    {
        if (!_soundPlanPlayed)
        {
            PlaySound(GBriefingOnPlan);
            _soundPlanPlayed = true;
        }
    }
    else if (stricmp(section, "__briefing") == 0)
    {
        if (!_soundNotesPlayed)
        {
            PlaySound(GBriefingOnNotes);
            _soundNotesPlayed = true;
        }
    }
    else if (stricmp(section, "equipment") == 0)
    {
        if (!_soundGearPlayed)
        {
            PlaySound(GBriefingOnGear);
            _soundGearPlayed = true;
        }
    }
    else if (stricmp(section, "group") == 0)
    {
        if (!_soundGroupPlayed)
        {
            PlaySound(GBriefingOnGroup);
            _soundGroupPlayed = true;
        }
    }
}

const ParamEntry* FindSound(RString name, SoundPars& pars);

void DisplayGetReady::PlaySound(RString name)
{
    if (name.GetLength() == 0)
    {
        return;
    }

    SoundPars pars;
    if (!FindSound(name, pars))
    {
        return;
    }
    _sound = GSoundScene->OpenAndPlayOnce2D(pars.name, pars.vol, pars.freq, false);
    _sound->SetKind(WaveMusic);
    _sound->SetSticky(true);
}

void DisplayGetReady::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        case IDC_CANCEL:
            Display::OnButtonClicked(idc);
            break;
        default:
            DisplayMap::OnButtonClicked(idc);
            break;
    }
}

void DisplayGetReady::Destroy()
{
    if (_selectWeapons)
    {
        RString weapons = GetSaveDirectory() + RString("weapons.cfg");
        _weaponsInfo.Save(weapons);

        _weaponsInfo.Apply();
    }
    DisplayMap::Destroy();
}
DisplayDebriefing::DisplayDebriefing(ControlsContainer* parent, bool animation) : Display(parent)
{
    //	GWorld->EnableDisplay(false);
    Load("RscDisplayDebriefing");
    // PoseidonAssert(_debriefing);
    CreateDebriefing();
    _animation = animation;
    _enableSimulation = false;

    GetCtrl(IDC_DEBRIEFING_PAD2)->ShowCtrl(false);

    if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
    {
        // server
        _server = true;
        _client = false;
        // GetNetworkManager().ClientReady(NGSDebriefingOK);
        CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_DEBRIEFING_RESTART));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(IDS_DISP_CLIENT_READY));
            ctrl->ShowCtrl(false);
        }
    }
    else if (GetNetworkManager().GetGameState() > NGSNone)
    {
        // client
        _server = false;
        _client = true;
        CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_DEBRIEFING_RESTART));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(IDS_DISP_CLIENT_READY));
        }
        ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CANCEL));
        if (ctrl)
        {
            ctrl->SetText(LocalizeString(IDS_DISP_DISCONNECT));
        }
    }
    else
    {
        // single player
        _server = false;
        _client = false;
        GetCtrl(IDC_DEBRIEFING_PLAYERS_TITLE_BG)->ShowCtrl(false);
        GetCtrl(IDC_DEBRIEFING_PLAYERS_TITLE)->ShowCtrl(false);
        GetCtrl(IDC_DEBRIEFING_PLAYERS_BG)->ShowCtrl(false);
        GetCtrl(IDC_DEBRIEFING_PLAYERS)->ShowCtrl(false);
    }

    UpdateDebriefingActionLabels(this);

    if (_langCbToken >= 0)
        UnregisterLanguageChangedCallback(_langCbToken);
    _langCbToken = RegisterLanguageChangedCallback(
        [this]()
        {
            RefreshLocalizedText();
            RefreshLanguage();
        });
}

void DisplayDebriefing::Destroy()
{
    Display::Destroy();
    //	GWorld->EnableDisplay(true);
}

void DisplayDebriefing::RefreshLanguage()
{
    const RString leftSection = GetCurrentHtmlSectionName(_left);
    const RString rightSection = GetCurrentHtmlSectionName(_right);
    const RString statsSection = GetCurrentHtmlSectionName(_stats);
    const bool statsVisible = GetCtrl(IDC_DEBRIEFING_PAD2)->IsVisible();

    CreateDebriefing();
    UpdateDebriefingActionLabels(this);
    GetCtrl(IDC_DEBRIEFING_PAD2)->ShowCtrl(statsVisible);

    if (leftSection.GetLength() > 0 && _left->FindSection(leftSection) >= 0)
        _left->SwitchSection(leftSection);
    if (rightSection.GetLength() > 0 && _right->FindSection(rightSection) >= 0)
        _right->SwitchSection(rightSection);
    if (statsSection.GetLength() > 0 && _stats->FindSection(statsSection) >= 0)
        _stats->SwitchSection(statsSection);
}

Control* DisplayDebriefing::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_DEBRIEFING_LEFT:
        {
            C3DHTML* html = new C3DHTML(this, idc, cls);
            _left = html;
            return html;
        }
        break;
        case IDC_DEBRIEFING_RIGHT:
        {
            C3DHTML* html = new C3DHTML(this, idc, cls);
            _right = html;
            return html;
        }
        break;
        case IDC_DEBRIEFING_STAT:
        {
            C3DHTML* html = new C3DHTML(this, idc, cls);
            _stats = html;
            return html;
        }
        break;
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayDebriefing::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_DEBRIEFING_RESTART:
            if (_client)
            {
                GetNetworkManager().ClientReady(NGSDebriefingOK);
                GetCtrl(IDC_CANCEL)->ShowCtrl(false);
            }
            else if (!_server)
            {
                if (_oldStats._mission._lives == 0)
                {
                    break;
                }
                GStats = _oldStats;

                // restart mission
                GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));
                GStats.ClearMission();
                GWorld->ActivateAddons(CurrentTemplate.addOns);
                GWorld->InitGeneral(CurrentTemplate.intel);
                if (!GWorld->InitVehicles(GModeArcade, CurrentTemplate))
                {
                    break;
                }

                int lives = GStats._mission._lives;
                if (GStats._mission._lives > 0)
                {
                    GStats._mission._lives = lives - 1;
                }
                Exit(IDC_OK);
            }
            break;
        case IDC_CANCEL:
            if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
            {
                GetNetworkManager().ClientReady(NGSDebriefingOK);
            }
            Exit(IDC_CANCEL);
            break;
        case IDC_AUTOCANCEL:
            Exit(IDC_AUTOCANCEL);
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayDebriefing::OnHTMLLink(int idc, RString link)
{
    if (idc == IDC_DEBRIEFING_RIGHT)
    {
        if (stricmp(link, "stat:open") == 0)
        {
            GetCtrl(IDC_DEBRIEFING_PAD2)->ShowCtrl(true);
            return;
        }
    }
    else if (idc == IDC_DEBRIEFING_STAT)
    {
        if (stricmp(link, "stat:close") == 0)
        {
            GetCtrl(IDC_DEBRIEFING_PAD2)->ShowCtrl(false);
            return;
        }
    }

    Display::OnHTMLLink(idc, link);
}

void DisplayDebriefing::OnSimulate(EntityAI* vehicle)
{
    if (_server)
    {
        if (!GetNetworkManager().IsServer() && !GetNetworkManager().IsGameMaster())
        {
            _server = false;
            GetCtrl(IDC_DEBRIEFING_RESTART)->ShowCtrl(true);
            CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CANCEL));
            if (ctrl)
            {
                ctrl->SetText(LocalizeString(IDS_DISP_DISCONNECT));
            }
        }
    }
    else
    {
        if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
        {
            _server = true;
            GetCtrl(IDC_DEBRIEFING_RESTART)->ShowCtrl(false);
            CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_CANCEL));
            if (ctrl)
            {
                ctrl->SetText(LocalizeString(IDS_DISP_CONTINUE));
            }
        }
    }

    // update player list
    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_DEBRIEFING_PLAYERS));
    if (lbox)
    {
        lbox->SetReadOnly();
        lbox->ShowSelected(false);
        lbox->ClearStrings();

        for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
        {
            int dpnid = GetNetworkManager().GetPlayerRole(i)->player;
            if (dpnid == NO_PLAYER || dpnid == AI_PLAYER)
            {
                continue;
            }
            const PlayerIdentity* identity = GetNetworkManager().FindIdentity(dpnid);
            if (!identity)
            {
                continue;
            }
            int index = lbox->AddString(identity->name);
            switch (identity->state)
            {
                case NGSNone:
                case NGSCreating:
                case NGSCreate:
                case NGSLogin:
                case NGSEdit:
                case NGSMissionVoted:
                case NGSPrepareSide:
                case NGSPrepareRole:
                case NGSPrepareOK:
                case NGSDebriefingOK:
                    lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetValue(index, 2);
                    break;
                case NGSDebriefing:
                    lbox->SetFtColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetValue(index, 1);
                    break;
                case NGSTransferMission:
                case NGSLoadIsland:
                case NGSBriefing:
                case NGSPlay:
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetValue(index, 0);
                    break;
                default:
                    break;
            }
        }
        lbox->SortItemsByValue();
    }

    Display::OnSimulate(vehicle);
}

struct KillsInfo
{
    const VehicleType* type;
    RString playerName;
    int n;
};

struct CasualtiesInfo
{
    bool player;
    RString killedName;
    RString killerName;
    int n;
};

int CmpKillsInfo(const KillsInfo* info1, const KillsInfo* info2)
{
    if (info2->playerName.GetLength() > 0)
    {
        if (info2->playerName.GetLength() > 0)
        {
            return info2->n - info1->n;
        }
        else
        {
            return 1;
        }
    }
    else if (info1->playerName.GetLength() > 0)
    {
        return -1;
    }
    else
    {
        return (int)(info2->type->_cost - info1->type->_cost);
    }
}

int CmpCasualtiesInfo(const CasualtiesInfo* info1, const CasualtiesInfo* info2)
{
    if (info2->player)
    {
        if (info1->player)
        {
            return info2->n - info1->n;
        }
        else
        {
            return 1;
        }
    }
    else if (info1->player)
    {
        return -1;
    }
    else
    {
        return strcmp(info1->killedName, info2->killedName);
    }
}

#if _ENABLE_CHEATS
#define LOG_DEBRIEFING 1
#else
#define LOG_DEBRIEFING 0
#endif

void DisplayDebriefing::CreateDebriefing()
{
    _left->Init();
    _right->Init();
    _stats->Init();

    // prepare statistics
    _oldStats = GStats;
    GStats.Update();

    AIUnitHeader& oldInfo = _oldStats._campaign._playerInfo;
    AIUnitHeader& newInfo = GStats._campaign._playerInfo;
    RString playerName = GetLocalPlayerName();
    if (newInfo.unit)
    {
        playerName = newInfo.unit->GetPerson()->GetInfo()._name;
    }

    // preload and create section
    const RString briefing = GetBriefingFile();
    LoadLocalizedMissionHtml(_left, briefing);
    LoadLocalizedMissionHtml(_right, briefing);
    int lSection = _left->AddSection();
    _left->AddName(lSection, "__OBJECTIVES");
    int rSection = _right->AddSection();
    _right->AddName(rSection, "__DEBRIEFING");
    int sSection = _stats->AddSection();
    _stats->AddName(sSection, "__STATISTICS");

#if LOG_DEBRIEFING
    bool doOutput = false;
    const char* end = "";
    switch (GWorld->GetEndMode())
    {
        case EMLoser:
            doOutput = true;
            end = "LOST";
            break;
        case EMEnd1:
            doOutput = true;
            end = "END 1";
            break;
        case EMEnd2:
            doOutput = true;
            end = "END 2";
            break;
        case EMEnd3:
            doOutput = true;
            end = "END 3";
            break;
        case EMEnd4:
            doOutput = true;
            end = "END 4";
            break;
        case EMEnd5:
            doOutput = true;
            end = "END 5";
            break;
        case EMEnd6:
            doOutput = true;
            end = "END 6";
            break;
    }

    FILE* file = nullptr;
    if (doOutput)
        file = fopen((GamePaths::Instance().UserDir() + "rating.log").c_str(), "a+");
#endif

    // mission name
    RString text = LoadLocalizedMissionBriefingName(GetMissionDirectory(), CurrentTemplate.intel.briefingName);
    if (text.GetLength() == 0)
    {
        text = RString(Glob.header.filename);
    }
    _right->AddText(rSection, text, HFH1, HACenter, false, false, "");
    _right->AddBreak(rSection, false);

#if LOG_DEBRIEFING
    if (file)
    {
        fprintf(file, "************************************************************\n");
        fprintf(file, "User: %s\n", (const char*)Glob.header.playerName);
        fprintf(file, "Mission: %s (%s)\n", (const char*)Glob.header.filename, end);
        time_t t;
        time(&t);
        fprintf(file, "Time: %s\n", ctime(&t));
    }
#endif

    // rank, name of soldier
    text = LocalizeString(IDS_PRIVATE + ClampRankIndex(newInfo.rank)) + RString(" ") + playerName;
    _right->AddText(rSection, text, HFP, HALeft, false, false, "");
    _right->AddBreak(rSection, false);

    // mission duration
    int day = CurrentTemplate.intel.day - 1;
    int year = CurrentTemplate.intel.year;
    for (int m = 0; m < CurrentTemplate.intel.month - 1; m++)
    {
        day += Poseidon::GetDaysInMonth(year, m);
    }
    float time = CurrentTemplate.intel.hour * OneHour + CurrentTemplate.intel.minute * OneMinute + day * OneDay +
                 0.5 * OneSecond;
    float dt = floatMax(Glob.clock.GetTimeInYear() - time, 0);
    dt *= 365 * 24;
    int hours = toIntFloor(dt);
    dt -= hours;
    dt *= 60;
    int minutes = toInt(dt);
    dt -= minutes;
    char buffer[256];
    if (hours > 0)
    {
        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_DURATION_LONG), hours, minutes);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_DURATION_SHORT), minutes);
    }
    _right->AddText(rSection, buffer, HFP, HALeft, false, false, "");
    _right->AddBreak(rSection, false);

#if LOG_DEBRIEFING
    if (file)
        fprintf(file, "%s\n", buffer);
#endif

    // score
    float score = newInfo.experience - oldInfo.experience;
    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_SCORE), score);
    _right->AddText(rSection, buffer, HFP, HALeft, false, false, "");
    int points = (int)(GStats._campaign._score - _oldStats._campaign._score);
    if (points >= 0)
    {
        RString image = USER_CONFIG.easyMode ? "debr_pecka.paa" : "debr_star.paa";
#if LOG_DEBRIEFING
        RString sign = USER_CONFIG.easyMode ? "o" : "*";
#endif
        for (int i = 0; i < points + 1; i++)
        {
            _right->AddImage(rSection, image, HALeft, false, 16, 16, "");
#if LOG_DEBRIEFING
            strncat(buffer, sign, sizeof(buffer) - strlen(buffer) - 1);
#endif
        }
    }
    else
    {
        RString image = "mission_uncomplete.paa";
#if LOG_DEBRIEFING
        RString sign = "X";
#endif
        for (int i = 0; i < -points; i++)
        {
            _right->AddImage(rSection, image, HALeft, false, 16, 16, "");
#if LOG_DEBRIEFING
            strncat(buffer, sign, sizeof(buffer) - strlen(buffer) - 1);
#endif
        }
    }
    _right->AddBreak(rSection, false);

#if LOG_DEBRIEFING
    if (file)
    {
        fprintf(file, "%s\n", buffer);
        fprintf(file, "Total score in campaign: %d\n", GStats._campaign._score);
    }
#endif

    // mission end
    int src = -1;
    switch (GWorld->GetEndMode())
    {
        case EMLoser:
            src = _right->FindSection("Debriefing:Loser");
            break;
        case EMEnd1:
            src = _right->FindSection("Debriefing:End1");
            break;
        case EMEnd2:
            src = _right->FindSection("Debriefing:End2");
            break;
        case EMEnd3:
            src = _right->FindSection("Debriefing:End3");
            break;
        case EMEnd4:
            src = _right->FindSection("Debriefing:End4");
            break;
        case EMEnd5:
            src = _right->FindSection("Debriefing:End5");
            break;
        case EMEnd6:
            src = _right->FindSection("Debriefing:End6");
            break;
    }
    if (src >= 0)
    {
        _right->CopySection(src, rSection);
    }

    // objectives
    _left->AddText(lSection, LocalizeString(IDS_BRIEF_OBJECTIVES), HFH1, HACenter, false, false, "");
    _left->AddBreak(lSection, false);
    TargetSide side = TargetSide(Glob.header.playerSide);
    for (int s = 0; s < _left->NSections(); s++)
    {
        const HTMLSection& src = _left->GetSection(s);
        for (int n = 0; n < src.names.Size(); n++)
        {
            RString name = src.names[n];
            static const char* prefix = "OBJ_";
            static const char* prefixWest = "OBJ_WEST_";
            static const char* prefixEast = "OBJ_EAST_";
            static const char* prefixGuerrila = "OBJ_GUER_";
            static const char* prefixCivilian = "OBJ_CIVIL_";
            if (strnicmp(name, prefix, strlen(prefix)) == 0)
            {
                if (strnicmp(name, prefixWest, strlen(prefixWest)) == 0)
                {
                    if (side != TWest)
                    {
                        continue;
                    }
                }
                else if (strnicmp(name, prefixEast, strlen(prefixEast)) == 0)
                {
                    if (side != TEast)
                    {
                        continue;
                    }
                }
                if (strnicmp(name, prefixGuerrila, strlen(prefixGuerrila)) == 0)
                {
                    if (side != TGuerrila)
                    {
                        continue;
                    }
                }
                if (strnicmp(name, prefixCivilian, strlen(prefixCivilian)) == 0)
                {
                    if (side != TCivilian)
                    {
                        continue;
                    }
                }
                GameState* state = GWorld->GetGameState();
                int value = toInt((float)state->Evaluate(name));
                ::AddObjective(_left, s, lSection, value);
                break; // next section
            }
        }
    }
    _left->AddBreak(lSection, false);

    // statistics
    _stats->AddText(sSection, LocalizeString(IDS_BRIEF_STATISTICS), HFH1, HACenter, false, false, "");
    _stats->AddBreak(sSection, false);

    AutoArray<AIStatsEvent>& events = _oldStats._mission._events;

    AUTO_STATIC_ARRAY(KillsInfo, kills, 32);
    // kills - enemies
    for (int i = 0; i < events.Size(); i++)
    {
        AIStatsEvent& event = events[i];
        switch (event.type)
        {
            case SETKillsEnemyInfantry:
            case SETKillsEnemySoft:
            case SETKillsEnemyArmor:
            case SETKillsEnemyAir:
            {
                RString playerName = event.killedPlayer ? event.killedName : "";
                for (int j = 0; j < kills.Size(); j++)
                {
                    if (kills[j].type == event.killedType && kills[j].playerName == playerName)
                    {
                        kills[j].n++;
                        goto ExitSwitchEnemies;
                    }
                }
                {
                    int index = kills.Add();
                    kills[index].type = event.killedType;
                    kills[index].playerName = playerName;
                    kills[index].n = 1;
                }
            }
            ExitSwitchEnemies:
                break;
        }
    }
    if (kills.Size() > 0)
    {
        _stats->AddText(sSection, LocalizeString(IDS_BRIEF_YOURKILLS), HFP, HALeft, false, false, "");
        _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
        if (file)
            fprintf(file, "\nYour kills:\n");
#endif

        QSort(kills.Data(), kills.Size(), CmpKillsInfo);
        for (int j = 0; j < kills.Size(); j++)
        {
            KillsInfo& info = kills[j];
            RString killed = info.playerName;
            if (killed.GetLength() == 0)
            {
                killed = info.type->GetDisplayName();
            }
            if (info.n > 1)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_GENERIC_TIMES), info.n,
                         (const char*)killed);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%s", (const char*)killed);
            }
            _stats->AddText(sSection, buffer, HFP, HALeft, false, false, "");
            _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
            if (file)
                fprintf(file, "%s\n", buffer);
#endif
        }
        _stats->AddBreak(sSection, false);
    }

    // kills - friends
    kills.Resize(0);
    for (int i = 0; i < events.Size(); i++)
    {
        AIStatsEvent& event = events[i];
        switch (event.type)
        {
            case SETKillsFriendlyInfantry:
            case SETKillsFriendlySoft:
            case SETKillsFriendlyArmor:
            case SETKillsFriendlyAir:
                //		case SETUnitLost:
                {
                    RString playerName = event.killedPlayer ? event.killedName : "";
                    for (int j = 0; j < kills.Size(); j++)
                    {
                        if (kills[j].type == event.killedType && kills[j].playerName == playerName)
                        {
                            kills[j].n++;
                            goto ExitSwitchFriends;
                        }
                    }
                    {
                        int index = kills.Add();
                        kills[index].type = event.killedType;
                        kills[index].playerName = playerName;
                        kills[index].n = 1;
                    }
                }
            ExitSwitchFriends:
                break;
        }
    }
    if (kills.Size() > 0)
    {
        _stats->AddText(sSection, LocalizeString(IDS_BRIEF_YOURKILLS_FRIENDLY), HFP, HALeft, false, false, "");
        _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
        if (file)
            fprintf(file, "\nYour kills - friendly units:\n");
#endif

        QSort(kills.Data(), kills.Size(), CmpKillsInfo);
        for (int j = 0; j < kills.Size(); j++)
        {
            KillsInfo& info = kills[j];
            RString killed = info.playerName;
            if (killed.GetLength() == 0)
            {
                killed = info.type->GetDisplayName();
            }
            if (info.n > 1)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_GENERIC_TIMES), info.n,
                         (const char*)killed);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%s", (const char*)killed);
            }
            _stats->AddText(sSection, buffer, HFP, HALeft, false, false, "");
            _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
            if (file)
                fprintf(file, "%s\n", buffer);
#endif
        }
        _stats->AddBreak(sSection, false);
    }

    // kills - civilians
    kills.Resize(0);
    for (int i = 0; i < events.Size(); i++)
    {
        AIStatsEvent& event = events[i];
        switch (event.type)
        {
            case SETKillsCivilInfantry:
            case SETKillsCivilSoft:
            case SETKillsCivilArmor:
            case SETKillsCivilAir:
            {
                RString playerName = event.killedPlayer ? event.killedName : "";
                for (int j = 0; j < kills.Size(); j++)
                {
                    if (kills[j].type == event.killedType && kills[j].playerName == playerName)
                    {
                        kills[j].n++;
                        goto ExitSwitchCivil;
                    }
                }
                {
                    int index = kills.Add();
                    kills[index].type = event.killedType;
                    kills[index].playerName = playerName;
                    kills[index].n = 1;
                }
            }
            ExitSwitchCivil:
                break;
        }
    }
    if (kills.Size() > 0)
    {
        _stats->AddText(sSection, LocalizeString(IDS_BRIEF_YOURKILLS_CIVIL), HFP, HALeft, false, false, "");
        _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
        if (file)
            fprintf(file, "\nYour kills - civilians:\n");
#endif

        QSort(kills.Data(), kills.Size(), CmpKillsInfo);
        for (int j = 0; j < kills.Size(); j++)
        {
            KillsInfo& info = kills[j];
            RString killed = info.playerName;
            if (killed.GetLength() == 0)
            {
                killed = info.type->GetDisplayName();
            }
            if (info.n > 1)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_GENERIC_TIMES), info.n,
                         (const char*)killed);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%s", (const char*)killed);
            }
            _stats->AddText(sSection, buffer, HFP, HALeft, false, false, "");
            _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
            if (file)
                fprintf(file, "%s\n", buffer);
#endif
        }
        _stats->AddBreak(sSection, false);
    }

    AUTO_STATIC_ARRAY(CasualtiesInfo, casual, 32);
    int playerTotal = 0;
    for (int i = 0; i < events.Size(); i++)
    {
        AIStatsEvent& event = events[i];
        if (event.type != SETUnitLost)
        {
            continue;
        }

        RString killerName;
        if (event.killedPlayer)
        {
            playerTotal++;
            if (!event.killerPlayer)
            {
                continue;
            }
            killerName = event.killerName;
        }
        for (int j = 0; j < casual.Size(); j++)
        {
            if (casual[j].killedName == event.killedName && casual[j].killerName == killerName)
            {
                casual[j].n++;
                goto ExitSwitch2;
            }
        }
        {
            int index = casual.Add();
            casual[index].player = event.killedPlayer;
            casual[index].killedName = event.killedName;
            casual[index].killerName = killerName;
            casual[index].n = 1;
        }
    ExitSwitch2:;
    }
    if (playerTotal > 0 || casual.Size() > 0)
    {
        _stats->AddText(sSection, LocalizeString(IDS_BRIEF_CASUALTIES), HFP, HALeft, false, false, "");
        _stats->AddBreak(sSection, false);

#if LOG_DEBRIEFING
        if (file)
            fprintf(file, "\nCasualties:\n");
#endif

        QSort(casual.Data(), casual.Size(), CmpCasualtiesInfo);
        if (playerTotal > 0)
        {
            if (playerTotal > 1)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_YOU_TIMES), playerTotal,
                         (const char*)GetLocalPlayerName());
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_YOU_ONCE),
                         (const char*)GetLocalPlayerName());
            }
            _stats->AddText(sSection, buffer, HFP, HALeft, false, false, "");
            _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
            if (file)
                fprintf(file, "%s\n", buffer);
#endif
        }
        for (int j = 0; j < casual.Size(); j++)
        {
            CasualtiesInfo& info = casual[j];
            if (info.player)
            {
                PoseidonAssert(info.killerName.GetLength() > 0);
                if (info.n > 1)
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_BY_TIMES), info.n,
                             (const char*)info.killerName);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_BY_ONCE),
                             (const char*)info.killerName);
                }
            }
            else
            {
                if (info.n > 1)
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_BRIEF_FORMAT_GENERIC_TIMES), info.n,
                             (const char*)info.killedName);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)info.killedName);
                }
            }
            _stats->AddText(sSection, buffer, HFP, HALeft, false, false, "");
            _stats->AddBreak(sSection, false);
#if LOG_DEBRIEFING
            if (file)
                fprintf(file, "%s\n", buffer);
#endif
        }
    }

    _right->AddText(rSection, LocalizeString(IDS_BRIEF_STAT_OPEN), HFP, HALeft, false, false, "Stat:open");
    _right->AddBreak(rSection, false);

    _stats->AddText(sSection, LocalizeString(IDS_BRIEF_STAT_CLOSE), HFP, HALeft, false, false, "Stat:close");
    _stats->AddBreak(sSection, false);

    // complete and switch to debriefing
    _left->FormatSection(lSection);
    _left->SwitchSection("__OBJECTIVES");
    _right->FormatSection(rSection);
    _right->SwitchSection("__DEBRIEFING");
    _stats->FormatSection(sSection);
    _stats->SwitchSection("__STATISTICS");

#if LOG_DEBRIEFING
    if (file)
        fclose(file);
#endif
}

Notepad::Notepad(ControlsContainer* parent, int idc, const ParamEntry& cls) : ControlObjectContainer(parent, idc, cls)
{
    _briefing = nullptr;

    _paper.Init(_shape, "papir", nullptr);
    _paper1 = GlobLoadTexture("data\\blok_stranka1.pac");
    _paper2 = GlobLoadTexture("data\\blok_stranka2.pac");
    _paper3 = GlobLoadTexture("data\\blok_stranka3.pac");
    _paper4 = GlobLoadTexture("data\\blok_stranka4.pac");
    _paper5 = GlobLoadTexture("data\\blok_stranka5.pac");
    _paper6 = GlobLoadTexture("data\\blok_stranka6_next.pac");
    _paper7 = GlobLoadTexture("data\\blok_stranka7_next.pac");
}

void Notepad::SetPosition(Vector3Par pos)
{
    ControlObjectContainer::SetPosition(pos);

    DisplayMap* parent = dynamic_cast<DisplayMap*>(_parent);
    if (parent)
    {
        parent->AdjustMapVisibleRect();
    }
}

void Notepad::Animate(int level)
{
    switch (_briefing->ActiveBookmark())
    {
        case 0:
            _paper.SetTexture(_shape, level, _paper1);
            break;
        case 1:
            _paper.SetTexture(_shape, level, _paper2);
            break;
        case 2:
            _paper.SetTexture(_shape, level, _paper3);
            break;
        case 3:
            _paper.SetTexture(_shape, level, _paper4);
            break;
        case 4:
            _paper.SetTexture(_shape, level, _paper6);
            break;
        case 5:
            _paper.SetTexture(_shape, level, _paper7);
            break;
        default:
            _paper.SetTexture(_shape, level, _paper5);
            break;
    }
}

void Notepad::Deanimate(int level)
{
    _paper.SetTexture(_shape, level, _paper5);
}

Compass::Compass(ControlsContainer* parent, int idc, const ParamEntry& cls) : ControlObjectWithZoom(parent, idc, cls)
{
    _shape->AllowAnimation();
    _pointer.Init(_shape, "kompas", nullptr, "osa kompasu");
    _cover.Init(_shape, "vicko", nullptr, "osa vicka");
}

Vector3 Compass::Center() const
{
    return _pointer.Center();
}

void Compass::Animate(int level)
{
    Vector3 dir = OriginalCamera.Direction();
    float angle = atan2(dir.X(), dir.Z());
    _pointer.Rotate(_shape, angle, level);
    _cover.Rotate(_shape, -0.45 * H_PI, level);
}

void Compass::Deanimate(int level)
{
    _pointer.Restore(_shape, level);
    _cover.Restore(_shape, level);
}

Watch::Watch(ControlsContainer* parent, int idc, const ParamEntry& cls) : ControlObjectWithZoom(parent, idc, cls)
{
    _shape->AllowAnimation();
    _hour.Init(_shape, "hodinova", nullptr, "osa");
    _minute.Init(_shape, "minutova", nullptr, "osa");
    _second.Init(_shape, "vterinova", nullptr, "osa");

    _date1.Init(_shape, "date1");
    _date2.Init(_shape, "date2");
    _day.Init(_shape, "day");
}

void Watch::Animate(int level)
{
    int year = Glob.clock.GetYear();
    int dayOfYear = toIntFloor(Glob.clock.GetTimeInYear() * 365);
    float timeOfDay = Glob.clock.GetTimeOfDay();
    if (timeOfDay >= 1.0)
    {
        timeOfDay--;
        dayOfYear++;
    }
    PoseidonAssert(timeOfDay >= 0 && timeOfDay < 1.0);
    PoseidonAssert(dayOfYear >= 0 && dayOfYear < 365);
    struct tm tmDate = {0, 0, 0, 0, 0, 0};
    tmDate.tm_year = year - 1900;
    tmDate.tm_yday = dayOfYear;
    int m = 0;
    while (tmDate.tm_yday >= Poseidon::GetDaysInMonth(year, m))
    {
        tmDate.tm_yday -= Poseidon::GetDaysInMonth(year, m);
        m++;
    }
    tmDate.tm_mday = tmDate.tm_yday + 1;
    tmDate.tm_mon = m;
    mktime(&tmDate);
    PoseidonAssert(tmDate.tm_yday == dayOfYear);

    int day = tmDate.tm_wday - 1;
    if (day < 0)
    {
        day = 6;
    }
    _day.UVOffset(_shape, 0, 0.1 * day, level);

    day = tmDate.tm_mday / 10 - 1;
    if (day < 0)
    {
        day = 9;
    }
    _date1.UVOffset(_shape, 0, 0.1 * day, level);

    day = tmDate.tm_mday % 10 - 1;
    if (day < 0)
    {
        day = 9;
    }
    _date2.UVOffset(_shape, 0, 0.1 * day, level);

    float angle = 4.0 * H_PI * timeOfDay;
    _hour.Rotate(_shape, angle, level);

    timeOfDay = fmod(24.0 * timeOfDay, 1.0);
    angle = 2.0 * H_PI * timeOfDay;
    _minute.Rotate(_shape, angle, level);

    timeOfDay = fmod(60.0 * timeOfDay, 1.0);
    int sec = toIntFloor(60.0 * timeOfDay);
    angle = 2.0 * H_PI * (1.0 / 60.0) * sec;
    _second.Rotate(_shape, angle, level);
}

void Watch::Deanimate(int level)
{
    _hour.Restore(_shape, level);
    _minute.Restore(_shape, level);
    _second.Restore(_shape, level);

    _date1.Restore(_shape, level);
    _date2.Restore(_shape, level);
    _day.Restore(_shape, level);
}

PackedBoolArray CUnitsSelector::GetArray() const
{
    return ListSelectedUnits();
}

// creation of main display

AbstractOptionsUI* CreateMainMapUI()
{
    return new DisplayMainMap(nullptr);
}

void DisplayMainMap::DestroyHUD(int exit)
{
    GLOB_WORLD->DestroyMap(exit);
}

} // namespace Poseidon
