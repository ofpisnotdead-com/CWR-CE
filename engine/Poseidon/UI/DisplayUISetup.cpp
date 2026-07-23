#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkManagerState.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

// Parse --mp-assign "SIDE:SLOT" into TargetSide and 1-based slot number
// Returns true on success, false on invalid format
static bool ParseMPAssign(const std::string& assign, TargetSide& outSide, int& outSlot)
{
    auto colon = assign.find(':');
    if (colon == std::string::npos || colon == 0 || colon == assign.size() - 1)
        return false;
    std::string sideStr = assign.substr(0, colon);
    std::string slotStr = assign.substr(colon + 1);
    // Map side string to enum
    if (sideStr == "WEST")
        outSide = TWest;
    else if (sideStr == "EAST")
        outSide = TEast;
    else if (sideStr == "RES")
        outSide = TGuerrila;
    else if (sideStr == "CIV")
        outSide = TCivilian;
    else
        return false;
    try
    {
        outSlot = std::stoi(slotStr);
    }
    catch (...)
    {
        return false;
    }
    return outSlot >= 1;
}

// Find flat role index for given side and 1-based slot number
// Returns -1 if not found, -2 if slot is occupied by another human
static int FindRoleForAssign(TargetSide side, int slot, int myPlayer)
{
    int sideCount = 0;
    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
    {
        const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
        if (!role || role->side != side)
            continue;
        sideCount++;
        if (sideCount == slot)
        {
            if (role->player != NO_PLAYER && role->player != AI_PLAYER && role->player != myPlayer)
                return -2; // occupied
            return i;
        }
    }
    return -1; // slot doesn't exist
}

// Client display
void DisplayClient::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        {
            // init game info
            SetBaseDirectory("");
            CurrentCampaign = "";
            CurrentBattle = "";
            CurrentMission = "";
            USER_CONFIG.easyMode = false;

            CurrentTemplate.Clear();
            if (GWorld->UI())
            {
                GWorld->UI()->Init();
            }

            // In auto-assign mode, default exit code is 2 (server offline before play).
            // It gets overridden to 0 when a mission completes.
            if (!AppConfig::Instance().GetMPAssign().empty())
                GApp->m_exitCode = 2;

            CreateChild(new DisplayMultiplayerSetup(this));
        }
        break;
        case IDC_AUTOCANCEL:
            Exit(idc);
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayClient::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_MP_SETUP:
            Display::OnChildDestroyed(idd, exit);
            _wasConnected = true;
            if (exit == IDC_CANCEL)
            {
                Exit(IDC_CANCEL);
            }
            break;
            //		case IDD_CLIENT_SETUP:
        case IDD_CLIENT_SIDE:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_CANCEL)
            {
                Exit(IDC_CANCEL);
            }
            break;
        case IDD_SERVER:
            // remote missions display
            if (exit == IDC_OK)
            {
                RString mission;
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(_child->GetCtrl(IDC_SERVER_MISSION));
                if (lbox)
                {
                    int sel = lbox->GetCurSel();
                    if (sel >= 0)
                    {
                        mission = lbox->GetData(sel);
                    }
                }

                bool cadetMode = false;
                DisplayRemoteMissions* disp = dynamic_cast<DisplayRemoteMissions*>((ControlsContainer*)_child);
                if (disp)
                {
                    cadetMode = disp->_cadetMode;
                }

                Display::OnChildDestroyed(idd, exit);

                if (GetNetworkManager().CanSelectMission())
                {
                    GetNetworkManager().SelectMission(mission, cadetMode);
                    GetNetworkManager().ClientReady(NGSMissionVoted);
                }
                else if (GetNetworkManager().CanVoteMission())
                {
                    GetNetworkManager().VoteMission(mission, cadetMode);
                    GetNetworkManager().ClientReady(NGSMissionVoted);
                }
            }
            else
            {
                Display::OnChildDestroyed(idd, exit);
                if (exit == IDC_CANCEL)
                {
                    Exit(IDC_CANCEL); // do not end session when autocancel
                }
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

void DisplayClient::OnSimulate(EntityAI* vehicle)
{
    NetworkGameState gameState = GetNetworkManager().GetServerState();
    const std::string& mpAssign = AppConfig::Instance().GetMPAssign();

    // Track whether we've ever connected to the server
    if (gameState > NGSNone)
        _wasConnected = true;

    // Auto-assign mode: handle disconnect exit codes here,
    // but let the normal UI chain handle slot assignment, briefing, and gameplay
    // (DisplayMultiplayerSetup, DisplayClientGetReady, DisplayMission have their own auto-assign code)
    if (!mpAssign.empty() && gameState == NGSNone && _wasConnected)
    {
        // m_exitCode is set to 0 by DisplayMultiplayerSetup when a mission completes.
        // Also check WasServerPlaying(): if the server ever reached NGSPlay, the mission
        // ran (possibly ending very quickly before DisplayMission was created).
        LOG_DEBUG(Network, "[mp-assign] Disconnect: gameState=NGSNone wasConnected={} exitCode={} serverPlaying={}",
                  _wasConnected, GApp->m_exitCode, GetNetworkManager().WasServerPlaying());
        const bool missionCompleted = GApp->m_exitCode == 0 || GetNetworkManager().WasServerPlaying();
        const bool shutdownAlreadyRequested = GApp->m_closeRequest;
        GApp->m_exitCode =
            ResolveMultiplayerAutomationExitCode(GApp->m_exitCode, shutdownAlreadyRequested, missionCompleted);
        if (shutdownAlreadyRequested)
        {
            LOG_DEBUG(Network, "[mp-assign] Disconnect after shutdown request, preserving exitCode={}",
                      GApp->m_exitCode);
        }
        else if (missionCompleted)
        {
            LOG_DEBUG(Network, "[mp-assign] Server disconnected after play (mission complete)");
        }
        else
        {
            LOG_DEBUG(Network, "[mp-assign] Server offline before play started");
        }
        GApp->m_closeRequest = true;
        return;
    }

    switch (gameState)
    {
        case NGSNone:
            OnButtonClicked(IDC_AUTOCANCEL);
            return;
        case NGSCreating:
        case NGSCreate:
        case NGSLogin:
            // continue with waiting
            if (GetNetworkManager().CanSelectMission())
            {
                CreateChild(new DisplayRemoteMissions(this));
                GetNetworkManager().ClientReady(NGSLogin);
            }
            else if (GetNetworkManager().CanVoteMission())
            {
                CreateChild(new DisplayRemoteMissions(this));
                GetNetworkManager().ClientReady(NGSLogin);
            }
            {
                CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_CLIENT_TEXT));
                if (text)
                {
                    text->SetText(LocalizeString(IDS_DISP_CLIENT_TEXT));
                }
            }
            break;
        case NGSEdit:
            // continue with waiting
            {
                CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_CLIENT_TEXT));
                if (text)
                {
                    text->SetText(LocalizeString(IDS_DISP_CLIENT_TEXT_EDIT));
                }
            }
            break;
        case NGSPrepareSide:
        case NGSPrepareRole:
        case NGSPrepareOK:
        case NGSDebriefing:
        case NGSTransferMission:
        case NGSLoadIsland:
        case NGSBriefing:
        case NGSPlay:
            OnButtonClicked(IDC_OK);
            break;
    }

    // update player list
    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_CLIENT_PLAYERS));
    if (lbox)
    {
        lbox->SetReadOnly();
        lbox->ShowSelected(false);
        lbox->ClearStrings();

        const AutoArray<PlayerIdentity>& identities = *GetNetworkManager().GetIdentities();
        for (int i = 0; i < identities.Size(); i++)
        {
            const PlayerIdentity& identity = identities[i];
            int index = lbox->AddString(GetIdentityText(identity));
            switch (identity.state)
            {
                case NGSNone:
                case NGSCreating:
                case NGSCreate:
                case NGSLogin:
                case NGSEdit:
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetValue(index, 0);
                    break;
                case NGSMissionVoted:
                case NGSPrepareSide:
                case NGSPrepareRole:
                case NGSPrepareOK:
                case NGSDebriefing:
                case NGSDebriefingOK:
                case NGSTransferMission:
                case NGSLoadIsland:
                case NGSBriefing:
                case NGSPlay:
                    lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetValue(index, 2);
                    break;
                default:
                    break;
            }
        }
        lbox->SortItemsByValue();
    }

    Display::OnSimulate(vehicle);
}

static bool SideExist(TargetSide side)
{
    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
    {
        if (GetNetworkManager().GetPlayerRole(i)->side == side)
        {
            return true;
        }
    }
    return false;
}

static NetworkGameState GetPlayerState(int player, TargetSide* side = nullptr, bool* locked = nullptr)
{
    if (side)
    {
        *side = TSideUnknown;
    }
    if (locked)
    {
        *locked = false;
    }

    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
    {
        const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
        if (role->player == player)
        {
            if (side)
            {
                *side = role->side;
            }
            if (locked)
            {
                *locked = role->roleLocked;
            }

            const PlayerIdentity* identity = GetNetworkManager().FindIdentity(player);
            if (identity && identity->state >= NGSPrepareOK)
            {
                return NGSPrepareOK;
            }
            else
            {
                return NGSPrepareRole;
            }
        }
    }
    return NGSPrepareSide;
}

static RString GetGroupDescription(int group)
{
    // group
    int colors = (Pars >> "CfgWorlds" >> "GroupColors").GetEntryCount();
    int name = group / colors;
    int color = group % colors;
    const ParamEntry& clsName = (Pars >> "CfgWorlds" >> "GroupNames").GetEntry(name);
    const ParamEntry& clsColor = (Pars >> "CfgWorlds" >> "GroupColors").GetEntry(color);
    RString descName = clsName >> "name";
    RString descColor = clsColor >> "name";

    BString<512> buffer;
    sprintf(buffer, LocalizeString(IDS_MPSETUP_GRP_DESC), (const char*)descName, (const char*)descColor);
    return (const char*)buffer;
}

static RString GetRoleDescription(const PlayerRole* role)
{
    // vehicle
    const ParamEntry& clsVehicle = Pars >> "CfgVehicles" >> role->vehicle;
    RString vehicle = clsVehicle >> "displayName";
    RString position;
    switch (role->position)
    {
        case PRPCommander:
            position = RString(" ") + LocalizeString(IDS_POSITION_COMMANDER);
            break;
        case PRPDriver:
        {
            const ParamEntry& clsAir = Pars >> "CfgVehicles" >> "Air";
            PoseidonAssert(clsVehicle.IsClass());
            PoseidonAssert(clsAir.IsClass());
            if (static_cast<const ParamClass&>(clsVehicle).IsDerivedFrom(static_cast<const ParamClass&>(clsAir)))
            {
                position = RString(" ") + LocalizeString(IDS_POSITION_PILOT);
            }
            else
            {
                position = RString(" ") + LocalizeString(IDS_POSITION_DRIVER);
            }
            break;
        }
        case PRPGunner:
            position = RString(" ") + LocalizeString(IDS_POSITION_GUNNER);
            break;
    }

    // group leader
    RString leader;
    if (role->leader)
    {
        leader = LocalizeString(IDS_POSITION_LEADER);
    }

    BString<512> buffer;
    sprintf(buffer, "%d: %s%s%s", role->unit + 1, (const char*)vehicle, (const char*)position, (const char*)leader);
    return (const char*)buffer;
}

class CMPSideButton : public C3DActiveText
{
  protected:
    bool _toggled;
    PackedColor _colorShade;
    PackedColor _colorDisabled;
    float _textureWidth;
    float _textureHeight;
    float _textHeight;
    TargetSide _side;
    Ref<Texture> _sideDisabled;

  public:
    CMPSideButton(ControlsContainer* parent, int idc, const ParamEntry& cls, TargetSide side);

    bool IsToggled() const { return _toggled; }
    void Toggle(bool set) { _toggled = set; }

    void OnDraw(float alpha) override;
};

CMPSideButton::CMPSideButton(ControlsContainer* parent, int idc, const ParamEntry& cls, TargetSide side)
    : C3DActiveText(parent, idc, cls)
{
    _side = side;
    _toggled = false;

    _colorShade = GetPackedColor(cls >> "colorShade");
    _colorDisabled = GetPackedColor(cls >> "colorDisabled");

    RString path = FindPicture(cls >> "picture");
    path.Lower();
    _texture = GlobLoadTexture(path);
    if (_texture)
    {
        _texture->SetMaxSize(1024); // no limits
    }

    _textureWidth = cls >> "pictureWidth";
    _textureHeight = cls >> "pictureHeight";
    _textHeight = cls >> "textHeight";

    _sideDisabled = GlobLoadTexture("misc\\side_krizek.paa");
}

void CMPSideButton::OnDraw(float alpha)
{
    PackedColor baseColor;
    if (!_enabled)
    {
        baseColor = _colorDisabled;
    }
    else if (_active)
    {
        baseColor = _colorActive;
    }
    else
    {
        baseColor = _color;
    }
    /*
        else if (_toggled) baseColor = _color;
        else baseColor = _colorDisabled;
    */

    PackedColor color = ModAlpha(baseColor, alpha);
    PackedColor colorShade = ModAlpha(_colorShade, alpha);

    // exception - right line
    PackedColor baseColorRight(0, _colorShade.A8(), 0, 255);
    PackedColor colorRight = ModAlpha(baseColorRight, alpha);

    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 position = _position - 0.002 * normal;

    // shade
    if (_toggled)
    {
        GEngine->Draw3D(_position, _down, _right, ClipAll, colorShade, DisableSun, nullptr);
    }

    // frame
    if (_toggled)
    {
        GEngine->DrawLine3D(position, position + _right, color, DisableSun);
        GEngine->DrawLine3D(position + _right, position + _right + _down, colorRight, DisableSun);
        GEngine->DrawLine3D(position + _right + _down, position + _down, color, DisableSun);
        GEngine->DrawLine3D(position + _down, position, color, DisableSun);
    }

    // picture
    Vector3 posPicture = position + 0.5 * (1.0 - _textureWidth) * _right;
    Vector3 downPicture = _textureHeight * _down;
    Vector3 rightPicture = _textureWidth * _right;
    if (_texture)
    {
        GEngine->Draw3D(posPicture, downPicture, rightPicture, ClipAll, color, DisableSun, _texture);
    }

    Vector3 up = -_textHeight * _down;
    position = _position - 0.003 * normal + (1.0 - _textHeight) * _down;

    Vector3 right = 0.75 * up.Size() * _right.Normalized();

    Vector3 offset = VZero;
    Vector3 width = GEngine->GetText3DWidth(right, _font, _text);
    switch (_style & ST_HPOS)
    {
        case ST_RIGHT:
            offset = _right - width;
            break;
        case ST_CENTER:
            offset = 0.5 * (_right - width);
            break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) break;
    }
    position += offset;

    float invRSize = 1.0 / right.Size();
    float x1c = 0, x2c = _right.Size() * invRSize;
    bool clip = width.SquareSize() > _right.SquareSize();
    if (clip)
    {
        float offsetSize = offset.Size() * invRSize;
        x1c += offsetSize;
        x2c += offsetSize;
    }

    /*
    bool focused = IsFocused();
    bool selected = IsDefault() && !_parent->GetFocused()->CanBeDefault();
    if (focused || selected)
    {
        PackedColor col;
        if (focused) col = color;
        else col = ModAlpha(color, 0.5);

        if (clip)
            GEngine->DrawLine3D
            (
                position - offset + _down, position - offset + _down + _right,
                col, DisableSun
            );
        else
            GEngine->DrawLine3D
            (
                position + _down, position + _down + width,
                col, DisableSun
            );
    }
    */

    PackedColor colorText(Color(1, 1, 0, 1));
    colorText.SetA8(color.A8());

    // text
    GEngine->DrawText3D(position, up, right, ClipAll, _font, colorText, DisableSun, _text, x1c, 0, x2c, 1);

    // number of assigned / all roles
    int all = 0, assigned = 0;
    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
    {
        const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
        if (role && role->side == _side)
        {
            if (role->player != NO_PLAYER)
            {
                all++;
                if (role->player != AI_PLAYER)
                {
                    assigned++;
                }
            }
        }
    }
    if (all > 0)
    {
        RString text = Format("%d/%d", assigned, all);
        width = GEngine->GetText3DWidth(right, _font, text);
        position = _position - 0.003 * normal + 0.5 * (_right - width);
        GEngine->DrawText3D(position, up, right, ClipAll, _font, colorText, DisableSun, text);
    }

    if (_texture && !_enabled)
    {
        // PackedColor white = ModAlpha(PackedWhite, alpha);
        GEngine->Draw3D(posPicture, downPicture, rightPicture, ClipAll, color, DisableSun, _sideDisabled);
    }
}

class CMPRoles : public C3DListBox
{
    typedef C3DListBox base;

  protected:
    PackedColor _bgColor;

    Ref<Texture> _enableAI;
    Ref<Texture> _disableAI;

  public:
    CMPRoles(ControlsContainer* parent, int idc, const ParamEntry& cls);

    void OnLButtonUp(float x, float y) override;

    void DrawItem(Vector3Par position, Vector3Par down, int i, float alpha) override;
    void DrawTooltip(float x, float y) override;
};

CMPRoles::CMPRoles(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DListBox(parent, idc, cls)
{
    _sb3DWidth = 0.05;
    _bgColor = GetPackedColor(cls >> "colorBackground");

    _enableAI = GlobLoadTexture("misc\\compx.paa");
    _disableAI = GlobLoadTexture("misc\\compai.paa");
}

static const float col1coef = 0.87;

void CMPRoles::OnLButtonUp(float x, float y)
{
    if (_scrollbar.IsLocked())
    {
        _scrollbar.OnLButtonUp();
    }
    else if (!IsReadOnly() && IsInside(x, y))
    {
        if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
        {
        }
        else
        {
            float index = _v * _rows;
            if (index >= 0 && index < _rows)
            {
                int sel = toIntFloor(_topString + index);

                float column1 = (1.0 - _sb3DWidth) * col1coef;
                if (_u <= column1)
                {
                    // column1
                    SetCurSel(sel);
                }
                else
                {
                    // column2
                    // enable / disable AI
                    if (!GetNetworkManager().GetMissionHeader()->disabledAI)
                    {
                        int value = GetValue(sel);
                        const PlayerRole* role = GetNetworkManager().GetPlayerRole(value);
                        if (role)
                        {
                            if (role->player == AI_PLAYER)
                            {
                                GetNetworkManager().AssignPlayer(value, NO_PLAYER);
                            }
                            else if (role->player == NO_PLAYER)
                            {
                                GetNetworkManager().AssignPlayer(value, AI_PLAYER);
                            }
                        }
                    }
                }
            }
        }
    }

    if (_dragging)
    {
        if (_parent)
        {
            _parent->OnLBDrop(x, y);
        }
        _dragging = false;
    }
}

void CMPRoles::DrawTooltip(float x, float y)
{
    if (_scrollbar.IsLocked())
    {
        return;
    }
    if (!IsReadOnly() && IsInside(x, y))
    {
        if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
        {
            return;
        }

        float index = _v * _rows;
        if (index >= 0 && index < _rows)
        {
            if (GetNetworkManager().GetMissionHeader()->disabledAI)
            {
                return;
            }
            int sel = toIntFloor(_topString + index);
            if (sel < 0 || sel >= GetSize())
            {
                return;
            }

            float column1 = (1.0 - _sb3DWidth) * col1coef;
            if (_u <= column1)
            {
                return;
            }

            // enable / disable AI
            int value = GetValue(sel);
            const PlayerRole* role = GetNetworkManager().GetPlayerRole(value);
            if (role)
            {
                if (role->player == AI_PLAYER)
                {
                    _tooltip = LocalizeString(IDS_TOOLTIP_DISABLE_AI);
                    base::DrawTooltip(x, y);
                }
                else if (role->player == NO_PLAYER)
                {
                    _tooltip = LocalizeString(IDS_TOOLTIP_ENABLE_AI);
                    base::DrawTooltip(x, y);
                }
            }
        }
    }
}

void CMPRoles::DrawItem(Vector3Par position, Vector3Par down, int i, float alpha)
{
    float y1c = 0;
    float y2c = 1;
    if (i < _topString)
    {
        y1c = _topString - i;
    }
    if (i > _topString + _rows - 1)
    {
        y2c = _topString + _rows - i;
    }

    Vector3 normal = _down.CrossProduct(_right).Normalized();

    /*
        Vector3 rightSB = _right;
        if (GetSize() > _rows)
            rightSB = (1.0 - _sb3DWidth) * _right;
        float rightSBSize = rightSB.Size();
        Vector3 border = 0.02 * _right;
    */
    Vector3 column1 = (1.0 - _sb3DWidth) * _right;
    Vector3 column2 = VZero;
    Vector3 border = 0.02 * column1;

    int value = GetValue(i);
    const PlayerRole* role = value >= 0 ? GetNetworkManager().GetPlayerRole(value) : nullptr;

    Vector3 pos = position;

    // draw selection
    PackedColor baseColor = GetFtColor(i);
    if (value >= 0)
    {
        column2 = (1.0 - col1coef) * column1;
        column1 *= col1coef;

        const float indent = 0.05;
        pos += indent * column1;
        column1 *= (1.0 - indent);

        Vector3 curPos = pos - 0.002 * normal;
        PackedColor baseBgColor = _bgColor;
        bool selected = i == GetCurSel() && IsEnabled();
        if (selected && _showSelected)
        {
            baseBgColor = _selBgColor;
            baseColor = GetSelColor(i);
        }
        PackedColor bgColor = ModAlpha(baseBgColor, alpha);
        const float coef = 0.9;
        const float invCoef = 1.0 / coef;
        float topClip = y1c * invCoef;
        saturate(topClip, 0, 1);
        float bottomClip = y2c * invCoef;
        saturate(bottomClip, 0, 1);
        GEngine->Draw3D(curPos, coef * down, column1, ClipAll, bgColor, DisableSun, nullptr, 0, topClip, 1, bottomClip);
    }
    PackedColor color = ModAlpha(baseColor, alpha);
    float column1Size = column1.Size();
    // float column2Size = column2.Size();

    // draw picture
    Vector3 curPos = pos - 0.003 * normal;
    Texture* texture = GetTexture(i);
    if (texture)
    {
        Vector3 right = (float)texture->AWidth() / (float)texture->AHeight() * down.Size() * _right.Normalized();
        float rightSize = right.Size();
        float x2c = 1;
        if (rightSize > column1Size)
        {
            x2c = column1Size / rightSize;
        }
        GEngine->Draw3D(curPos, down, right, ClipAll, color, DisableSun, texture, 0, y1c, x2c, y2c);
        curPos += right;
        column1Size -= rightSize;
        if (column1Size <= 0)
        {
            return;
        }
    }

    // draw text
    RString text = GetText(i);

    if (value < 0)
    {
        const float size = 0.5;
        float top = 0.4;
        float y1ct = 0;
        float y2ct = 1;
        if (y1c > top)
        {
            y1ct = (y1c - top) / size;
        }
        if (y2c < top + size)
        {
            y2ct = (y2c - top) / size;
        }

        Vector3 posText = curPos + top * down + border;
        Vector3 up = -size * down;
        Vector3 right = 0.75 * up.Size() * _right.Normalized();
        float x2c = (column1Size - 2.0 * border.Size()) / right.Size();
        GEngine->DrawText3D(posText, up, right, ClipAll, _font, color, DisableSun, text, 0, y1ct, x2c, y2ct);
    }
    else
    {
        // first row
        const float size = 0.4;
        float top = 0;
        float y1ct = 0;
        float y2ct = 1;
        if (y1c > top)
        {
            y1ct = (y1c - top) / size;
        }
        if (y2c < top + size)
        {
            y2ct = (y2c - top) / size;
        }

        Vector3 posText = curPos + top * down + border;
        Vector3 up = -size * down;
        Vector3 right = 0.75 * up.Size() * _right.Normalized();
        float x2c = (column1Size - 2.0 * border.Size()) / right.Size();
        GEngine->DrawText3D(posText, up, right, ClipAll, _font, color, DisableSun, text, 0, y1ct, x2c, y2ct);

        // second row
        if (role)
        {
            // player name
            float textAlpha = (color.A8() / 255.0) * alpha;
            PackedColor color = PackedColor(Color(0, 1, 0, textAlpha));
            RString player = LocalizeString(IDS_PLAYER_AI);
            int dpid = role->player;
            if (dpid == AI_PLAYER)
            {
            }
            else if (dpid == NO_PLAYER)
            {
                player = LocalizeString(IDS_PLAYER_NONE);
                color = PackedColor(Color(1, 1, 1, textAlpha));
            }
            else
            {
                const PlayerIdentity* identity = GetNetworkManager().FindIdentity(dpid);
                if (identity)
                {
                    player = identity->GetName();
                    color = PackedColor(Color(1, 1, 0, textAlpha));
                }
            }

            const float size = 0.5;
            float top = 0.4;
            float y1ct = 0;
            float y2ct = 1;
            if (y1c > top)
            {
                y1ct = (y1c - top) / size;
            }
            if (y2c < top + size)
            {
                y2ct = (y2c - top) / size;
            }

            Vector3 posText = curPos + top * down + border;
            Vector3 up = -size * down;
            Vector3 right = 0.75 * up.Size() * _right.Normalized();
            float x2c = (column1Size - 2.0 * border.Size()) / right.Size();
            GEngine->DrawText3D(posText, up, right, ClipAll, _font, color, DisableSun, player, 0, y1ct, x2c, y2ct);
        }

        // second column
        if (!GetNetworkManager().GetMissionHeader()->disabledAI && role &&
            (role->player == AI_PLAYER || role->player == NO_PLAYER))
        {
            Texture* texture;
            if (role->player == AI_PLAYER)
            {
                texture = _disableAI;
            }
            else
            {
                texture = _enableAI;
            }

            if (texture)
            {
                const float coef = 0.9;
                const float invCoef = 1.0 / coef;
                float topClip = y1c * invCoef;
                saturate(topClip, 0, 1);
                float bottomClip = y2c * invCoef;
                saturate(bottomClip, 0, 1);
                GEngine->Draw3D(curPos + column1, coef * down, column2, ClipAll, color, DisableSun, texture, 0, topClip,
                                1, bottomClip);
            }
        }
    }
}

DisplayMultiplayerSetup::DisplayMultiplayerSetup(ControlsContainer* parent) : Display(parent)
{
    _enableSimulation = false;
    _player = NO_PLAYER;

    _transferMission = true;
    _transferOverlayVisible = false;
    _transferOverlayShows = 0;
    _loadIsland = true;
    _play = false;

    _sessionLocked = false;

    _allDisabled = false;
    _autoAssigned = false;

    _side = TSideUnknown;

    _dragging = false;
    _dragFont = GEngine->LoadFont(GetFontID("courierNewB64"));
    _dragSize = 0.024;
    _dragColor = PackedColor(Color(1, 1, 1, 0.75));

    const ParamEntry& fontPars = Pars >> "CfgInGameUI" >> "ProgressFont";
    _messageFont = GEngine->LoadFont(GetFontID(fontPars >> "font"));
    _messageSize = 0.75 * _messageFont->Height();

    _none = GlobLoadTexture("data\\clear_empty.paa");
    _westUnlocked = GlobLoadTexture("misc\\usflag_normal.paa");
    _westLocked = GlobLoadTexture("misc\\usflag_locked.paa");
    _eastUnlocked = GlobLoadTexture("misc\\rusflag_normal.paa");
    _eastLocked = GlobLoadTexture("misc\\rusflag_locked.paa");
    _guerUnlocked = GlobLoadTexture("misc\\fiaflag_normal.paa");
    _guerLocked = GlobLoadTexture("misc\\fiaflag_locked.paa");
    _civlUnlocked = GlobLoadTexture("misc\\civflag_normal.paa");
    _civlLocked = GlobLoadTexture("misc\\civflag_locked.paa");

    Load("RscDisplayMultiplayerSetup");

    Preinit();
    _init = false;
}

Control* DisplayMultiplayerSetup::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MPSETUP_WEST:
            return new CMPSideButton(this, idc, cls, TWest);
        case IDC_MPSETUP_EAST:
            return new CMPSideButton(this, idc, cls, TEast);
        case IDC_MPSETUP_GUERRILA:
            return new CMPSideButton(this, idc, cls, TGuerrila);
        case IDC_MPSETUP_CIVILIAN:
            return new CMPSideButton(this, idc, cls, TCivilian);
        case IDC_MPSETUP_ROLES:
            return new CMPRoles(this, idc, cls);
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayMultiplayerSetup::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_MPSETUP_WEST:
            _side = TWest;
            break;
        case IDC_MPSETUP_EAST:
            _side = TEast;
            break;
        case IDC_MPSETUP_GUERRILA:
            _side = TGuerrila;
            break;
        case IDC_MPSETUP_CIVILIAN:
            _side = TCivilian;
            break;
        case IDC_OK:
            if (GetNetworkManager().IsServer())
            {
                // test if all users are assigned
                const AutoArray<PlayerIdentity>* identities = GetNetworkManager().GetIdentities();
                if (!identities)
                {
                    break;
                }

                int n = 0;
                bool foundPlayer = false;
                int player = GetNetworkManager().GetPlayer();
                for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
                {
                    int dpid = GetNetworkManager().GetPlayerRole(i)->player;
                    if (GetNetworkManager().FindIdentity(dpid))
                    {
                        n++;
                    }
                    if (dpid == player)
                    {
                        foundPlayer = true;
                    }
                }

                if (!foundPlayer)
                {
                    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_ASSIGN_PLAYERS));
                    break;
                }

                if (n < GetNetworkManager().NPlayerRoles() && n < identities->Size())
                {
                    CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_LAUNCH_GAME),
                                 IDD_MSG_LAUNCHGAME);
                    break;
                }

                // create game
                GetNetworkManager().SetGameState(NGSTransferMission);
            }
            else
            {
                if (GetNetworkManager().IsGameMaster())
                {
                    const AutoArray<PlayerIdentity>* identities = GetNetworkManager().GetIdentities();
                    if (!identities)
                    {
                        break;
                    }
                    int n = 0;
                    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
                    {
                        int dpid = GetNetworkManager().GetPlayerRole(i)->player;
                        if (GetNetworkManager().FindIdentity(dpid))
                        {
                            n++;
                        }
                    }
                    if (n < GetNetworkManager().NPlayerRoles() && n < identities->Size())
                    {
                        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_LAUNCH_GAME),
                                     IDD_MSG_LAUNCHGAME);
                        break;
                    }
                }
                GetNetworkManager().ClientReady(NGSPrepareOK);
            }
            break;
        case IDC_CANCEL:
            if (GetNetworkManager().IsServer())
            {
                // unassign all roles
                for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                    if (role->player != AI_PLAYER)
                    {
                        GetNetworkManager().AssignPlayer(i, AI_PLAYER);
                    }
                }
                GetNetworkManager().SetGameState(NGSCreate);
            }
            Exit(IDC_CANCEL);
            break;
        case IDC_AUTOCANCEL:
            Exit(idc);
            break;
        case IDC_MPSETUP_KICK:
            if (GetNetworkManager().IsServer())
            {
                if (_player != GetNetworkManager().GetPlayer())
                {
                    GetNetworkManager().KickOff(_player, KORKick);
                }
            }
            else if (GetNetworkManager().IsGameMaster())
            {
                if (_player != GetNetworkManager().GetPlayer())
                {
                    GetNetworkManager().SendKick(_player);
                }
            }
            break;
        case IDC_MPSETUP_ENABLE_ALL:
        {
            if (!GetNetworkManager().IsServer() && !GetNetworkManager().IsGameMaster())
            {
                break;
            }
            if (GetNetworkManager().GetMissionHeader()->disabledAI)
            {
                break;
            }

            bool allDisabled = true;
            for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
            {
                const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                if (role->player == AI_PLAYER)
                {
                    allDisabled = false;
                    break;
                }
            }
            for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
            {
                const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                if (allDisabled)
                {
                    if (role->player == NO_PLAYER)
                    {
                        GetNetworkManager().AssignPlayer(i, AI_PLAYER);
                    }
                }
                else
                {
                    if (role->player == AI_PLAYER)
                    {
                        GetNetworkManager().AssignPlayer(i, NO_PLAYER);
                    }
                }
            }
        }
        break;
        case IDC_MPSETUP_LOCK:
            if (GetNetworkManager().IsServer())
            {
                _sessionLocked = !_sessionLocked;
                GetNetworkManager().LockSession(_sessionLocked);
            }
            else if (GetNetworkManager().IsGameMaster() && !GetNetworkManager().IsAdmin())
            {
                _sessionLocked = !_sessionLocked;
                GetNetworkManager().SendLockSession(_sessionLocked);
            }
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayMultiplayerSetup::OnLBSelChanged(int idc, int curSel)
{
    switch (idc)
    {
        case IDC_MPSETUP_ROLES:
            if (curSel >= 0 && _player != NO_PLAYER)
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_ROLES));
                if (lbox)
                {
                    int val = lbox->GetValue(curSel);
                    if (val >= 0)
                    {
                        const PlayerRole* role = GetNetworkManager().GetPlayerRole(val);
                        if (role)
                        {
                            if (role->player == _player)
                            {
                                if (_player == GetNetworkManager().GetPlayer())
                                {
                                    GetNetworkManager().ClientReady(NGSPrepareSide);
                                }

                                if (_allDisabled)
                                {
                                    GetNetworkManager().AssignPlayer(val, NO_PLAYER);
                                }
                                else
                                {
                                    GetNetworkManager().AssignPlayer(val, AI_PLAYER);
                                }
                            }
                            else
                            {
                                GetNetworkManager().AssignPlayer(val, _player);
                            }
                        }
                    }
                }
            }
            break;
        case IDC_MPSETUP_POOL:
            if (curSel >= 0)
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_POOL));
                if (lbox)
                {
                    _player = lbox->GetValue(curSel);
                }
            }
            break;
        case IDC_MPSETUP_PARAM1:
        case IDC_MPSETUP_PARAM2:
        {
            float param1 = 0, param2 = 0;
            const MissionHeader* header = GetNetworkManager().GetMissionHeader();

            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM1));
            if (lbox)
            {
                int sel = lbox->GetCurSel();
                if (sel >= 0 && header && header->valuesParam1.Size() == lbox->GetSize())
                {
                    param1 = header->valuesParam1[sel];
                }
            }

            lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM2));
            if (lbox)
            {
                int sel = lbox->GetCurSel();
                if (sel >= 0 && header && header->valuesParam2.Size() == lbox->GetSize())
                {
                    param2 = header->valuesParam2[sel];
                }
            }

            GetNetworkManager().SetParams(param1, param2);
        }
        break;
        default:
            Display::OnLBSelChanged(idc, curSel);
            break;
    }
}

void DisplayMultiplayerSetup::OnChildDestroyed(int idd, int exit)
{
    if (GetNetworkManager().IsServer())
    {
        switch (idd)
        {
            case IDD_MSG_LAUNCHGAME:
                Display::OnChildDestroyed(idd, exit);
                if (exit == IDC_OK)
                {
                    // check if player assigned
                    int player = GetNetworkManager().GetPlayer();
                    bool found = false;
                    if (player != 0)
                    {
                        for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
                        {
                            const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                            if (role && role->player == player)
                            {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found)
                    {
                        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_ASSIGN_PLAYERS));
                        break;
                    }

                    // create game
                    GetNetworkManager().SetGameState(NGSTransferMission);
                    // wait until NGSTransferMission on client
                }
                break;
            case IDD_SERVER_GET_READY:
            {
                Display::OnChildDestroyed(idd, exit);
                if (exit == IDC_OK)
                {
                    GetNetworkManager().SetGameState(NGSPlay);
                    _play = true;
                }
                else
                {
                    GetNetworkManager().SetGameState(NGSPrepareSide);
                    GetNetworkManager().ClientReady(NGSPrepareSide);
                    _transferMission = true;
                    _loadIsland = true;
                    _play = false;
                    _message = "";
                }
            }
            break;
            case IDD_MISSION:
            {
                Display::OnChildDestroyed(idd, exit);
                GetNetworkManager().DestroyAllObjects();
                GetNetworkManager().SetGameState(NGSDebriefing);
                GStats.OnMPMissionEnd();
                CreateChild(new DisplayDebriefing(this, false));
            }
            break;
            case IDD_DEBRIEFING:
            {
                GetNetworkManager().SetGameState(NGSPrepareSide);
                GetNetworkManager().ClientReady(NGSPrepareSide);
                _transferMission = true;
                _loadIsland = true;
                _play = false;
                Display::OnChildDestroyed(idd, exit);
            }
            break;
            default:
                Display::OnChildDestroyed(idd, exit);
                break;
        }
    }
    else
    {
        switch (idd)
        {
            case IDD_MSG_LAUNCHGAME:
                Display::OnChildDestroyed(idd, exit);
                if (exit == IDC_OK && GetNetworkManager().IsGameMaster())
                {
                    GetNetworkManager().ClientReady(NGSPrepareOK);
                    GetCtrl(IDC_OK)->ShowCtrl(false);
                }
                break;
            case IDD_CLIENT_GET_READY:
                Display::OnChildDestroyed(idd, exit);
                LOG_INFO(Network, "[MP] ClientGetReady exited with {} — gameState={} serverState={} worldMode={}", exit,
                         (int)GetNetworkManager().GetGameState(), (int)GetNetworkManager().GetServerState(),
                         (int)GWorld->GetMode());
                if (exit == IDC_OK)
                {
                    GetNetworkManager().ClientReady(NGSPlay);
                    CreateChild(new DisplayMission(this));
                }
                else if (exit == IDC_CANCEL)
                {
                    Exit(IDC_CANCEL);
                }
                else
                {
                    GetNetworkManager().DestroyAllObjects();
                    _message = "";
                    CreateChild(new DisplayClientWait(this));
                }
                break;
            case IDD_MISSION:
            {
                Display::OnChildDestroyed(idd, exit);
                // In auto-assign mode, reaching this point means a mission was played
                if (!AppConfig::Instance().GetMPAssign().empty())
                    GApp->m_exitCode =
                        ResolveMultiplayerAutomationExitCode(GApp->m_exitCode, GApp->m_closeRequest, true);
                GetNetworkManager().DestroyAllObjects();
                GetNetworkManager().ClientReady(NGSDebriefing);
                CreateChild(new DisplayClientDebriefing(this, false));
            }
            break;
            case IDD_CLIENT_WAIT:
                Display::OnChildDestroyed(idd, exit);
                if (exit == IDC_CANCEL)
                {
                    Exit(IDC_CANCEL);
                }
                break;
            case IDD_DEBRIEFING:
                Display::OnChildDestroyed(idd, exit);
                if (GetNetworkManager().IsGameMaster())
                {
                    _transferMission = true;
                    _loadIsland = true;
                    _play = false;
                }
                else if (exit == IDC_CANCEL)
                {
                    Exit(IDC_CANCEL);
                }
                break;
            default:
                Display::OnChildDestroyed(idd, exit);
                break;
        }
    }
}

void DisplayMultiplayerSetup::OnSimulate(EntityAI* vehicle)
{
    NetworkGameState state = Poseidon::ResolveMultiplayerSetupDisplayState(
        GetNetworkManager().IsServer(), GetNetworkManager().GetServerState(), GetNetworkManager().GetGameState());
    LOG_DEBUG(Network, "[mp-setup] OnSimulate: state={} isServer={}", (int)state, GetNetworkManager().IsServer());

    if (!GetNetworkManager().IsServer())
    {
        const bool transferOverlayVisible = state == NGSTransferMission;
        if (transferOverlayVisible && !_transferOverlayVisible)
        {
            ++_transferOverlayShows;
        }
        _transferOverlayVisible = transferOverlayVisible;
    }

    if (GetNetworkManager().IsServer())
    {
        switch (state)
        {
            case NGSTransferMission:
                if (_transferMission)
                {
                    _transferMission = false;
                    GetNetworkManager().SendMissionFile();
                    GetNetworkManager().SetGameState(NGSLoadIsland);
                    // wait until NGSTransferMission on client
                }
                break;
            case NGSLoadIsland:
                if (_loadIsland)
                {
                    _loadIsland = false;
                    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));

                    // set parameters - after SwitchLandscape
                    float param1 = 0, param2 = 0;
                    GetNetworkManager().GetParams(param1, param2);
                    GameState* gstate = GWorld->GetGameState();
                    gstate->VarSet("param1", GameValue(param1), false);
                    gstate->VarSet("param2", GameValue(param2), false);
                    GetNetworkManager().PublicVariable("param1");
                    GetNetworkManager().PublicVariable("param2");

                    GStats.ClearMission();
                    GWorld->ActivateAddons(CurrentTemplate.addOns);
                    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
                    if (!GLOB_WORLD->InitVehicles(GModeNetware, CurrentTemplate))
                    {
                    }

                    _message = LocalizeString(IDS_NETWORK_SEND);

                    GetNetworkManager().CreateAllObjects();
                    // CreateAllObjects ends with a guaranteed Briefing report. The server
                    // advances only after consuming it, behind every world update above.
                }
                break;
            case NGSBriefing:
                if (!_play)
                {
                    PoseidonAssert(GWorld->CameraOn());
                    GetNetworkManager().ClientReady(NGSPlay);
                    // always show briefing - used for synchronization
                    RString weapons = GetSaveDirectory() + RString("weapons.cfg");
                    unlink(weapons);
                    RunInitScript();
                    {
                        void RunMissionScript(const char* filename, GameValue argument);
                        if (GetNetworkManager().IsServer())
                        {
                            RunMissionScript("initServer.sqs", GameValue());
                        }
                        RunMissionScript("initPlayerLocal.sqs", GameValue());
                    }
                    CreateChild(new DisplayServerGetReady(this));
                    return;
                }
            case NGSPlay:
                if (_play)
                {
                    _play = false;
                    _message = "";
                    CreateChild(new DisplayMission(this));
                    return;
                }
        }
    }
    else
    {
        switch (state)
        {
            case NGSNone:
            case NGSCreating:
            case NGSCreate:
            case NGSLogin:
            case NGSEdit:
                OnButtonClicked(IDC_AUTOCANCEL);
                break;
            case NGSPrepareSide:
            case NGSPrepareRole:
            case NGSPrepareOK:
            {
                // Auto-assign slot when --mp-assign is set
                const std::string& mpAssign = AppConfig::Instance().GetMPAssign();
                if (!mpAssign.empty() && !_autoAssigned)
                {
                    int nRoles = GetNetworkManager().NPlayerRoles();
                    LOG_DEBUG(Network, "[mp-assign] State={}, roles={}, assign='{}'", (int)state, nRoles, mpAssign);
                    // Wait for roles to sync from server
                    if (nRoles == 0)
                        break;
                    TargetSide targetSide;
                    int targetSlot;
                    if (!ParseMPAssign(mpAssign, targetSide, targetSlot))
                    {
                        LOG_ERROR(Network, "[mp-assign] Invalid format: '{}', expected WEST:1", mpAssign);
                        GApp->m_exitCode = 1;
                        GApp->m_closeRequest = true;
                        break;
                    }
                    int roleIdx = FindRoleForAssign(targetSide, targetSlot, GetNetworkManager().GetPlayer());
                    if (roleIdx == -1)
                    {
                        LOG_ERROR(Network, "[mp-assign] Slot {}:{} does not exist (roles={})", mpAssign, targetSlot,
                                  nRoles);
                        GApp->m_exitCode = 3;
                        GApp->m_closeRequest = true;
                        break;
                    }
                    if (roleIdx == -2)
                    {
                        LOG_ERROR(Network, "[mp-assign] Slot {}:{} is occupied", mpAssign, targetSlot);
                        GApp->m_exitCode = 4;
                        GApp->m_closeRequest = true;
                        break;
                    }
                    LOG_DEBUG(Network, "[mp-assign] Assigning to {} slot {} (roleIdx={})", mpAssign, targetSlot,
                              roleIdx);
                    GetNetworkManager().AssignPlayer(roleIdx, GetNetworkManager().GetPlayer());
                    GetNetworkManager().ClientReady(NGSPrepareOK);
                    _autoAssigned = true;
                }
            }
            break;
            case NGSTransferMission:
            {
                int curBytes, totBytes;
                GetNetworkManager().GetTransferStats(curBytes, totBytes);
                int curKB = toInt(curBytes / 1024);
                int totKB = toInt(totBytes / 1024);
                BString<256> buffer;
                sprintf(buffer, LocalizeString(IDS_MP_TRANSFER_FILE), curKB, totKB);
                _message = (const char*)buffer;
            }
            // continue with waiting
            break;
            case NGSLoadIsland:
                _message = LocalizeString(IDS_NETWORK_RECEIVE);
                // continue with waiting
                break;
            case NGSBriefing:
                // always show briefing - used for synchronization
                _message = "";
                if (GetNetworkManager().GetMyPlayerRole())
                {
                    // create markers
                    markersMap.Clear();
                    int n = CurrentTemplate.markers.Size();
                    for (int i = 0; i < n; i++)
                    {
                        markersMap.Add(CurrentTemplate.markers[i]);
                    }

                    RString weapons = GetSaveDirectory() + RString("weapons.cfg");
                    unlink(weapons);

                    RunInitScript();
                    CreateChild(new DisplayClientGetReady(this));
                    GetNetworkManager().ClientReady(NGSBriefing);
                    break;
                }
                // else continue
            case NGSPlay:
            {
                if (GetNetworkManager().GetGameState() >= NGSPlay && GetNetworkManager().GetMyPlayerRole())
                    CreateChild(new DisplayMission(this));
                else
                    CreateChild(new DisplayClientWait(this));
                break;
            }
            case NGSDebriefing:
                break;
        }
    }

    if (state >= NGSPrepareSide && GetNetworkManager().FindIdentity(GetNetworkManager().GetPlayer()))
    {
        if (!_init)
        {
            Init();
            _init = true;
        }
        Update();
    }

    if (_message.GetLength() == 0)
    {
        Display::OnSimulate(vehicle);
    }
}

} // namespace Poseidon
