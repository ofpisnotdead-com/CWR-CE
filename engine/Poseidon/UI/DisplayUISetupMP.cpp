#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <Poseidon/World/WorldChatInput.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <stdio.h>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
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

void DisplayMultiplayerSetup::Preinit()
{
    // hide params
    if (GetCtrl(IDC_MPSETUP_PARAM1_TITLE))
    {
        GetCtrl(IDC_MPSETUP_PARAM1_TITLE)->ShowCtrl(false);
    }
    if (GetCtrl(IDC_MPSETUP_PARAM1))
    {
        GetCtrl(IDC_MPSETUP_PARAM1)->ShowCtrl(false);
    }
    if (GetCtrl(IDC_MPSETUP_PARAM2_TITLE))
    {
        GetCtrl(IDC_MPSETUP_PARAM2_TITLE)->ShowCtrl(false);
    }
    if (GetCtrl(IDC_MPSETUP_PARAM2))
    {
        GetCtrl(IDC_MPSETUP_PARAM2)->ShowCtrl(false);
    }

    // hide roles
    if (GetCtrl(IDC_MPSETUP_ROLES_TITLE))
    {
        GetCtrl(IDC_MPSETUP_ROLES_TITLE)->ShowCtrl(false);
    }
    if (GetCtrl(IDC_MPSETUP_ROLES))
    {
        GetCtrl(IDC_MPSETUP_ROLES)->ShowCtrl(false);
    }

    // empty mission info
    C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_ISLAND));
    if (text)
    {
        text->SetText("");
    }
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_NAME));
    if (text)
    {
        text->SetText("");
    }
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_DESC));
    if (text)
    {
        text->SetText("");
    }

    // side buttons
    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_WEST));
    if (button)
    {
        button->EnableCtrl(false);
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_EAST));
    if (button)
    {
        button->EnableCtrl(false);
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_GUERRILA));
    if (button)
    {
        button->EnableCtrl(false);
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_CIVILIAN));
    if (button)
    {
        button->EnableCtrl(false);
    }

    // message
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_MESSAGE));
    if (text)
    {
        text->SetText(LocalizeString(IDS_MSG_WAIT_CONNECTING));
    }

    // hide kick button
    if (GetCtrl(IDC_MPSETUP_KICK))
    {
        GetCtrl(IDC_MPSETUP_KICK)->ShowCtrl(false);
    }

    // hide enable / disable all
    if (GetCtrl(IDC_MPSETUP_ENABLE_ALL))
    {
        GetCtrl(IDC_MPSETUP_ENABLE_ALL)->ShowCtrl(false);
    }

    // hide lock session
    if (GetCtrl(IDC_MPSETUP_LOCK))
    {
        GetCtrl(IDC_MPSETUP_LOCK)->ShowCtrl(false);
    }

    // cancel button
    if (!GetNetworkManager().IsServer())
    {
        CActiveText* button = dynamic_cast<CActiveText*>(GetCtrl(IDC_CANCEL));
        if (button)
        {
            button->SetText(LocalizeString(IDS_DISP_DISCONNECT));
        }
    }
}

void DisplayMultiplayerSetup::Init()
{
    const MissionHeader* header = GetNetworkManager().GetMissionHeader();

    // param1
    int n;
    if (header && (n = header->valuesParam1.Size()) > 0)
    {
        C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_PARAM1_TITLE));
        if (text)
        {
            text->ShowCtrl(true);
            text->SetText(header->titleParam1);
        }
        C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM1));
        if (lbox)
        {
            lbox->ShowCtrl(true);

            PoseidonAssert(header->textsParam1.Size() == n);
            int sel = 0;
            for (int i = 0; i < n; i++)
            {
                lbox->AddString(header->textsParam1[i]);
                if (header->valuesParam1[i] == header->defValueParam1)
                {
                    sel = i;
                }
            }
            lbox->SetCurSel(sel, false);
        }
    }
    else
    {
        GetCtrl(IDC_MPSETUP_PARAM1_TITLE)->ShowCtrl(false);
        GetCtrl(IDC_MPSETUP_PARAM1)->ShowCtrl(false);
    }

    // param2
    if (header && (n = header->valuesParam2.Size()) > 0)
    {
        C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_PARAM2_TITLE));
        if (text)
        {
            text->ShowCtrl(true);
            text->SetText(header->titleParam2);
        }
        C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM2));
        if (lbox)
        {
            lbox->ShowCtrl(true);

            PoseidonAssert(header->textsParam2.Size() == n);
            int sel = 0;
            for (int i = 0; i < n; i++)
            {
                lbox->AddString(header->textsParam2[i]);
                if (header->valuesParam2[i] == header->defValueParam2)
                {
                    sel = i;
                }
            }
            lbox->SetCurSel(sel, false);
        }
    }
    else
    {
        GetCtrl(IDC_MPSETUP_PARAM2_TITLE)->ShowCtrl(false);
        GetCtrl(IDC_MPSETUP_PARAM2)->ShowCtrl(false);
    }

    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_POOL));
    if (lbox)
    {
        lbox->SetColorPicture(true);
    }
}

void DisplayMultiplayerSetup::Update()
{
    bool server = GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster();

    // mission info
    const MissionHeader* header = GetNetworkManager().GetMissionHeader();
    C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_ISLAND));
    if (text)
    {
        RString island = "";
        if (header && header->island.GetLength() > 0)
        {
            island = Pars >> "CfgWorlds" >> header->island >> "description";
        }
        text->SetText(island);
    }
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_NAME));
    if (text)
    {
        text->SetText(header ? header->name : "");
    }
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_DESC));
    if (text)
    {
        text->SetText(header ? header->description : "");
    }

    // players
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_POOL));
    if (lbox)
    {
        lbox->ClearStrings();

        const AutoArray<PlayerIdentity>& identities = *GetNetworkManager().GetIdentities();
        if (&identities)
        {
            for (int i = 0; i < identities.Size(); i++)
            {
                const PlayerIdentity& identity = identities[i];
                int index = lbox->AddString(GetIdentityText(identity));
                lbox->SetValue(index, identity.dpnid);
                TargetSide side;
                bool locked;
                NetworkGameState state = GetPlayerState(identity.dpnid, &side, &locked);
                if (state >= NGSPrepareOK)
                {
                    lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 1)));
                }
                else if (state >= NGSPrepareRole)
                {
                    lbox->SetFtColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 1, 0, 1)));
                }
                else
                {
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                }

                Texture* texture = _none;
                if (state >= NGSPrepareRole)
                {
                    switch (side)
                    {
                        case TWest:
                            if (locked)
                            {
                                texture = _westLocked;
                            }
                            else
                            {
                                texture = _westUnlocked;
                            }
                            break;
                        case TEast:
                            if (locked)
                            {
                                texture = _eastLocked;
                            }
                            else
                            {
                                texture = _eastUnlocked;
                            }
                            break;
                        case TGuerrila:
                            if (locked)
                            {
                                texture = _guerLocked;
                            }
                            else
                            {
                                texture = _guerUnlocked;
                            }
                            break;
                        case TCivilian:
                            if (locked)
                            {
                                texture = _civlLocked;
                            }
                            else
                            {
                                texture = _civlUnlocked;
                            }
                            break;
                    }
                }
                lbox->SetTexture(index, texture);
            }
        }
        lbox->SortItemsByValue();

        if (server)
        {
            lbox->SetReadOnly(false);
            if (_player == NO_PLAYER)
            {
                _player = GetNetworkManager().GetPlayer();
            }
            int sel = 0;
            for (int i = 0; i < lbox->GetSize(); i++)
            {
                if (lbox->GetValue(i) == _player)
                {
                    sel = i;
                    break;
                }
            }
            lbox->SetCurSel(sel);
        }
        else
        {
            lbox->SetReadOnly(true);
            _player = GetNetworkManager().GetPlayer();
            for (int i = 0; i < lbox->GetSize(); i++)
            {
                if (lbox->GetValue(i) == _player)
                {
                    lbox->SetCurSel(i);
                    break;
                }
            }
        }
    }

    // sides
    if (_side == TSideUnknown && GetNetworkManager().NPlayerRoles() > 0)
    {
        _side = GetNetworkManager().GetPlayerRole(0)->side;
    }

    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_WEST));
    if (button)
    {
        button->EnableCtrl(SideExist(TWest));
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_EAST));
    if (button)
    {
        button->EnableCtrl(SideExist(TEast));
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_GUERRILA));
    if (button)
    {
        button->EnableCtrl(SideExist(TGuerrila));
    }
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_CIVILIAN));
    if (button)
    {
        button->EnableCtrl(SideExist(TCivilian));
    }

    CMPSideButton* btnWest = dynamic_cast<CMPSideButton*>(GetCtrl(IDC_MPSETUP_WEST));
    CMPSideButton* btnEast = dynamic_cast<CMPSideButton*>(GetCtrl(IDC_MPSETUP_EAST));
    CMPSideButton* btnGuer = dynamic_cast<CMPSideButton*>(GetCtrl(IDC_MPSETUP_GUERRILA));
    CMPSideButton* btnCivl = dynamic_cast<CMPSideButton*>(GetCtrl(IDC_MPSETUP_CIVILIAN));
    if (btnWest)
    {
        btnWest->Toggle(false);
    }
    if (btnEast)
    {
        btnEast->Toggle(false);
    }
    if (btnGuer)
    {
        btnGuer->Toggle(false);
    }
    if (btnCivl)
    {
        btnCivl->Toggle(false);
    }
    switch (_side)
    {
        case TWest:
            if (btnWest)
            {
                btnWest->Toggle(true);
            }
            break;
        case TEast:
            if (btnEast)
            {
                btnEast->Toggle(true);
            }
            break;
        case TGuerrila:
            if (btnGuer)
            {
                btnGuer->Toggle(true);
            }
            break;
        case TCivilian:
            if (btnCivl)
            {
                btnCivl->Toggle(true);
            }
            break;
    }

    NetworkGameState state = GetPlayerState(_player);

    // roles
    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_ROLES));
    if (lbox)
    {
        lbox->ClearStrings();
        if (_side != TSideUnknown)
        {
            lbox->ShowCtrl(true);

            int sel = -1;
            int group = -1;
            for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
            {
                const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                if (role->side != _side)
                {
                    continue;
                }

                if (role->group != group)
                {
                    group = role->group;
                    RString desc = GetGroupDescription(group);
                    int index = lbox->AddString(desc);
                    lbox->SetValue(index, -1);
                }

                RString desc = GetRoleDescription(role);
                int index = lbox->AddString(desc);
                lbox->SetValue(index, i);
                if (role->player == _player)
                {
                    sel = index;
                }
            }
            if (sel >= 0)
            {
                lbox->SetCurSel(sel, false);
            }
            else
            {
                lbox->Check();
            }
            lbox->ShowSelected(sel >= 0);

            text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_ROLES_TITLE));
            if (text)
            {
                text->ShowCtrl(true);
                RString message;
                switch (_side)
                {
                    case TWest:
                        message = LocalizeString(IDS_DISP_MPSETUP_ROLES_WEST);
                        break;
                    case TEast:
                        message = LocalizeString(IDS_DISP_MPSETUP_ROLES_EAST);
                        break;
                    case TGuerrila:
                        message = LocalizeString(IDS_DISP_MPSETUP_ROLES_GUERRILA);
                        break;
                    case TCivilian:
                        message = LocalizeString(IDS_DISP_MPSETUP_ROLES_CIVILIAN);
                        break;
                    default:
                        message = LocalizeString(IDS_DISP_MPROLE_ROLES);
                        break;
                }
                text->SetText(message);
            }
        }
        else
        {
            GetCtrl(IDC_MPSETUP_ROLES_TITLE)->ShowCtrl(false);
            lbox->ShowCtrl(false);
        }
    }

    // parameters
    float param1 = 0, param2 = 0;
    GetNetworkManager().GetParams(param1, param2);

    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM1));
    if (lbox && header && header->valuesParam1.Size() == lbox->GetSize())
    {
        lbox->SetReadOnly(!server);
        int sel = 0;
        for (int i = 0; i < lbox->GetSize(); i++)
        {
            if (param1 == header->valuesParam1[i])
            {
                sel = i;
                break;
            }
        }
        lbox->SetCurSel(sel, false);
    }

    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MPSETUP_PARAM2));
    if (lbox && header && header->valuesParam2.Size() == lbox->GetSize())
    {
        lbox->SetReadOnly(!server);
        int sel = 0;
        for (int i = 0; i < lbox->GetSize(); i++)
        {
            if (param2 == header->valuesParam2[i])
            {
                sel = i;
                break;
            }
        }
        lbox->SetCurSel(sel, false);
    }

    // message
    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MPSETUP_MESSAGE));
    if (text)
    {
        RString message;
        if (_player == NO_PLAYER)
        {
            message = LocalizeString(IDS_MSG_WAIT_CONNECTING);
        }
        else
        {
            switch (state)
            {
                case NGSPrepareSide:
                    message = LocalizeString(IDS_MSG_CHOOSE_ROLE);
                    break;
                case NGSPrepareRole:
                    if (server)
                    {
                        message = LocalizeString(IDS_MSG_OK_LAUNCH_GAME);
                    }
                    else
                    {
                        message = LocalizeString(IDS_MSG_OK_READY);
                    }
                    break;
                case NGSPrepareOK:
                    if (server)
                    {
                        message = LocalizeString(IDS_MSG_OK_LAUNCH_GAME);
                    }
                    else
                    {
                        message = LocalizeString(IDS_MSG_WAIT_FOR_OTHERS);
                    }
                    break;
            }
        }
        text->SetText(message);
    }

    // kick button
    if (GetCtrl(IDC_MPSETUP_KICK))
    {
        GetCtrl(IDC_MPSETUP_KICK)->ShowCtrl(server && _player != GetNetworkManager().GetPlayer());
    }

    // enable / disable all
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_ENABLE_ALL));
    if (button)
    {
        if (server && !header->disabledAI)
        {
            bool allDisabled = true;
            bool allPlayers = true;
            for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
            {
                const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                if (role->player == AI_PLAYER)
                {
                    allDisabled = false;
                    allPlayers = false;
                    break;
                }
                else if (role->player == NO_PLAYER)
                {
                    allPlayers = false;
                }
            }
            if (allPlayers)
            {
                button->ShowCtrl(false);
            }
            else
            {
                button->ShowCtrl(true);
                if (allDisabled)
                {
                    button->SetText("\\misc\\comp_allx.paa");
                    button->SetTooltip(LocalizeString(IDS_TOOLTIP_ENABLE_ALL_AI));
                }
                else
                {
                    button->SetText("\\misc\\comp_allai.paa");
                    button->SetTooltip(LocalizeString(IDS_TOOLTIP_DISABLE_ALL_AI));
                }
            }
            _allDisabled = allDisabled && !allPlayers;
        }
        else
        {
            button->ShowCtrl(false);
        }
    }

    // lock session
    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MPSETUP_LOCK));
    if (button)
    {
        if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster() && !GetNetworkManager().IsAdmin())
        {
            button->ShowCtrl(true);
            if (_sessionLocked)
            {
                button->SetText("\\misc\\lock_ed.paa");
                button->SetTooltip(LocalizeString(IDS_UNLOCK_HOST));
            }
            else
            {
                button->SetText("\\misc\\lock_open.paa");
                button->SetTooltip(LocalizeString(IDS_LOCK_HOST));
            }
        }
        else
        {
            button->ShowCtrl(false);
        }
    }

    CActiveText* okButton = dynamic_cast<CActiveText*>(GetCtrl(IDC_OK));
    if (okButton)
    {
        bool enabled = true;
        bool foundPlayer = false;
        int player = GetNetworkManager().GetPlayer();
        int n = 0;
        for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
        {
            int dpid = GetNetworkManager().GetPlayerRole(i)->player;
            if (dpid == player)
            {
                foundPlayer = true;
            }
            if (GetNetworkManager().FindIdentity(dpid))
            {
                n++;
            }
        }
        if (!GetNetworkManager().IsGameMaster())
        {
            // starting mission has no sense if my role is not assigned yet
            if (!foundPlayer)
            {
                enabled = false;
            }
        }
        else
        {
            // starting mission has no sense if no role is not assigned yet
            // my role need not be assigned - I am admin
            if (n == 0)
            {
                enabled = false;
            }
        }
        okButton->EnableCtrl(enabled);
    }
}

void DisplayMultiplayerSetup::OnDraw(EntityAI* vehicle, float alpha)
{
    if (_message.GetLength() > 0)
    {
        // const int w = GLOB_ENGINE->Width2D();
        // const int h = GLOB_ENGINE->Height2D();

        const int w3d = GLOB_ENGINE->Width();
        const int h3d = GLOB_ENGINE->Height();

        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        GLOB_ENGINE->Draw2D(mip, PackedBlack, Rect2DAbs(0, 0, w3d, h3d));

        float textW = GEngine->GetTextWidth(_messageSize, _messageFont, _message);
        GEngine->DrawText(Point2DFloat(0.5 - 0.5 * textW, 0.5), _messageSize, _messageFont, PackedWhite, _message);
    }
    else
    {
        Display::OnDraw(vehicle, alpha);
        GChatList.OnDraw();

        if (_dragging)
        {
            float x = InputSubsystem::Instance().GetCursorX() * 0.5 + 0.5;
            float y = InputSubsystem::Instance().GetCursorY() * 0.5 + 0.5;
            y -= 0.5 * _dragSize;
            GEngine->DrawText(Point2DFloat(x, y), _dragSize, _dragFont, _dragColor, _dragName);
        }
    }
}

bool DisplayMultiplayerSetup::CanDrag(int player)
{
    if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
    {
        return true;
    }
    return player == GetNetworkManager().GetPlayer();
}

void DisplayMultiplayerSetup::OnLBDrag(int idc, int curSel)
{
    switch (idc)
    {
        case IDC_MPSETUP_POOL:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(idc));
            PoseidonAssert(lbox);
            int player = lbox->GetValue(curSel);
            if (!CanDrag(player))
            {
                break;
            }
            _player = player;
            _dragging = true;
            SetCursor(nullptr);
            _dragPlayer = player;
            const PlayerIdentity* identity = GetNetworkManager().FindIdentity(player);
            if (identity)
            {
                _dragName = identity->GetName();
            }
            else
            {
                _dragName = lbox->GetText(curSel);
            }
        }
        break;
        default:
            Display::OnLBDrag(idc, curSel);
            break;
    }
}

void DisplayMultiplayerSetup::OnLBDragging(float x, float y)
{
    if (_dragging)
    {
        IControl* ctrl = GetCtrl(x, y);
        if (ctrl && ctrl->IDC() == IDC_MPSETUP_ROLES)
        {
            /*
                        C3DListBox *lbox = dynamic_cast<C3DListBox *>(ctrl);
                        PoseidonAssert(lbox);
                        lbox->SetCurSel(x, y);
            */
        }
    }
    else
    {
        Display::OnLBDragging(x, y);
    }
}

void DisplayMultiplayerSetup::OnLBDrop(float x, float y)
{
    if (_dragging)
    {
        IControl* ctrl = GetCtrl(x, y);
        if (ctrl && ctrl->IDC() == IDC_MPSETUP_ROLES)
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(ctrl);
            PoseidonAssert(lbox);
            lbox->SetCurSel(x, y);
            int sel = lbox->GetCurSel();
            int role = lbox->GetValue(sel);
            GetNetworkManager().AssignPlayer(role, _dragPlayer);
        }

        _dragging = false;
        SetCursor("Arrow");
    }
    else
    {
        Display::OnLBDrop(x, y);
    }
}

// Client and server briefing, debriefing

DisplayClientDebriefing::DisplayClientDebriefing(ControlsContainer* parent, bool animation) : base(parent, animation) {}

void DisplayClientDebriefing::OnSimulate(EntityAI* vehicle)
{
    NetworkGameState gameState = GetNetworkManager().GetGameState();
    switch (gameState)
    {
        case NGSNone:
        case NGSCreating:
        case NGSCreate:
        case NGSLogin:
        case NGSEdit:
        case NGSPrepareSide:
        case NGSPrepareRole:
        case NGSPrepareOK:
        case NGSTransferMission:
        case NGSLoadIsland:
        case NGSBriefing:
            OnButtonClicked(IDC_AUTOCANCEL);
            break;
    }
    /*
        CActiveText *button = dynamic_cast<CActiveText *>(GetCtrl(IDC_CANCEL));
        if (button)
        {
            if (GetNetworkManager().IsGameMaster())
                button->SetText(LocalizeString(IDS_DISP_CANCEL));
            else
                button->SetText(LocalizeString(IDS_DISP_DISCONNECT));
        }
    */
    base::OnSimulate(vehicle);
}

DisplayServerGetReady::DisplayServerGetReady(ControlsContainer* parent) : base(parent, "RscDisplayServerGetReady") {}

void DisplayServerGetReady::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        {
            CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_SERVER_READY_PLAYERS));
            PoseidonAssert(lbox);
            for (int i = 0; i < lbox->GetSize(); i++)
            {
                if (lbox->GetValue(i) == 0)
                {
                    CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_LAUNCH_GAME),
                                 IDD_MSG_LAUNCHGAME);
                    return;
                }
            }
            Exit(IDC_OK);
        }
        break;
        default:
            base::OnButtonClicked(idc);
            break;
    }
}

void DisplayServerGetReady::Destroy()
{
    base::Destroy();
}

void DisplayServerGetReady::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_MSG_LAUNCHGAME:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_OK)
            {
                Exit(IDC_OK);
            }
            break;
        default:
            DisplayMap::OnChildDestroyed(idd, exit);
            break;
    }
}

void DisplayServerGetReady::OnSimulate(EntityAI* vehicle)
{
    // update player list
    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_SERVER_READY_PLAYERS));
    if (lbox)
    {
        /*
                AUTO_STATIC_ARRAY(NetPlayerInfo, players, 32)
                GetNetworkManager().GetPlayers(players);
        */

        lbox->SetReadOnly();
        lbox->ShowSelected(false);
        lbox->ClearStrings();
        // FIXED
        /*
                for (int i=0; i<players.Size(); i++)
                {
                    NetPlayerInfo &info = players[i];
                    int index = lbox->AddString(info.name);
                    switch (GetNetworkManager().GetPlayerState(info.dpid))
        */
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
            int index = lbox->AddString(GetIdentityText(*identity));
            //			switch (GetNetworkManager().GetPlayerState(dpnid))
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
                case NGSDebriefing:
                case NGSDebriefingOK:
                case NGSTransferMission:
                case NGSLoadIsland:
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetValue(index, 0);
                    break;
                case NGSBriefing:
                    lbox->SetFtColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetValue(index, 1);
                    break;
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

    base::OnSimulate(vehicle);
}

DisplayClientGetReady::DisplayClientGetReady(ControlsContainer* parent) : base(parent, "RscDisplayClientGetReady")
{
    if (!AppConfig::Instance().GetMPAssign().empty())
        _autoReadyFrames = 120; // ~2 seconds at 60fps
}

void DisplayClientGetReady::Destroy()
{
    base::Destroy();
}

void DisplayClientGetReady::Launch()
{
    GetNetworkManager().ClientReady(NGSPlay);
    if (IControl* ok = GetCtrl(IDC_OK))
        ok->ShowCtrl(false);
    _launched = true;
}

void DisplayClientGetReady::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_AUTOCANCEL:
            Exit(idc);
            break;
        case IDC_OK:
            if (!_launched)
                Launch();
            break;
        default:
            base::OnButtonClicked(idc);
            break;
    }
}

void DisplayClientGetReady::OnDraw(EntityAI* vehicle, float alpha)
{
    base::OnDraw(vehicle, alpha);
    GChatList.OnDraw();
}

void DisplayClientGetReady::OnSimulate(EntityAI* vehicle)
{
    // update player list
    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_CLIENT_READY_PLAYERS));
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
            int index = lbox->AddString(GetIdentityText(*identity));
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
                case NGSDebriefing:
                case NGSDebriefingOK:
                case NGSTransferMission:
                case NGSLoadIsland:
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetValue(index, 0);
                    break;
                case NGSBriefing:
                    lbox->SetFtColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 1, 0, 1)));
                    lbox->SetValue(index, 1);
                    break;
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

    const NetworkGameState serverState = GetNetworkManager().GetServerState();
    const NetworkGameState clientState = GetNetworkManager().GetGameState();
    const NetworkGameState effectiveState =
        serverState < NGSBriefing && clientState >= NGSBriefing ? clientState : serverState;
    switch (effectiveState)
    {
        case NGSNone:
        case NGSCreating:
        case NGSCreate:
        case NGSLogin:
        case NGSEdit:
        case NGSPrepareSide:
        case NGSPrepareRole:
        case NGSPrepareOK:
        case NGSDebriefing:
        case NGSTransferMission:
            OnButtonClicked(IDC_AUTOCANCEL);
            break;
        case NGSLoadIsland:
            break;
        case NGSBriefing:
            // Auto-ready after delay when --mp-assign is active
            if (_autoReadyFrames > 0)
                _autoReadyFrames--;
            else if (_autoReadyFrames == 0 && !_autoReadySent)
            {
                LOG_DEBUG(Network, "[mp-assign] Auto-ready in briefing");
                OnButtonClicked(IDC_OK);
                _autoReadySent = true;
            }
            break;
        case NGSPlay:
            Exit(IDC_OK);
            break;
    }
    base::OnSimulate(vehicle);
}

// Multiplayer players display

void DisplayMPPlayers::OnSimulate(EntityAI* vehicle)
{
    if (Poseidon::ShouldHandleMultiplayerChatShortcut(GWorld->GetChat() != nullptr) &&
        InputSubsystem::Instance().GetActionToDo(UANetworkPlayers, true, false))
    {
        OnButtonClicked(IDC_CANCEL);
    }

    if (GetNetworkManager().GetGameState() < NGSPlay)
    {
        OnButtonClicked(IDC_CANCEL);
    }

    UpdatePlayers();
    Display::OnSimulate(vehicle);
}

void DisplayMPPlayers::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_MP_PLAYERS)
    {
        UpdatePlayerInfo();
    }
    else
    {
        Display::OnLBSelChanged(idc, curSel);
    }
}

void DisplayMPPlayers::UpdatePlayers()
{
    const MissionHeader* header = GetNetworkManager().GetMissionHeader();

    RString mission, island;
    int time = 0;
    if (header)
    {
        mission = header->name;
        island = header->island;
        if (GetNetworkManager().GetServerState() == NGSPlay)
        {
            time = Poseidon::Foundation::GlobalTickCount() - header->start;
        }
    }

    C3DStatic* ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MISSION));
    if (ctrl)
    {
        ctrl->SetText(mission);
    }

    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_ISLAND));
    if (ctrl)
    {
        ctrl->SetText(island);
    }

    int t = time / 1000;
    int m = t / 60;
    int s = t - m * 60;

    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_TIME));
    if (ctrl)
    {
        char buffer[256];
        ::sprintf(buffer, "%d:%02d", m, s);
        ctrl->SetText(buffer);
    }

    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_REST));
    if (ctrl)
    {
        float et = GetNetworkManager().GetEstimatedEndTime().toFloat();
        if (et > 0)
        {
            et -= t;
            int em = toInt(et / 60.0);
            if (time != 0)
            {
                saturateMax(em, 1);
            }

            char buffer[256];
            ::sprintf(buffer, LocalizeString(IDS_TIME_LEFT), em);
            ctrl->SetText(buffer);
            ctrl->ShowCtrl(true);
        }
        else
        {
            ctrl->ShowCtrl(false);
        }
    }

    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
    if (!lbox)
    {
        return;
    }

    /*
        PackedColor inactive = ModAlpha(lbox->GetFtColor(), 0.5);
        PackedColor inactiveSel = ModAlpha(lbox->GetSelColor(), 0.5);
    */

    int sel = lbox->GetCurSel();
    int cur = sel >= 0 ? lbox->GetValue(sel) : 0;

    sel = 0;
    lbox->ClearStrings();
    const AutoArray<PlayerIdentity>* pid = GetNetworkManager().GetIdentities();
    if (pid)
    {
        for (int i = 0; i < pid->Size(); i++)
        {
            const PlayerIdentity& identity = pid->Get(i);
            char buffer[256];
            // [M]/[I] markers show the local mute (VoN) / ignore (chat) state
            // for each player; language-neutral so no localization needed.
            ::sprintf(buffer, "%d: %s%s%s", identity.playerid, (const char*)identity.GetName(),
                      IsVoiceMuted(identity.dpnid) ? " [M]" : "", IsChatIgnored(identity.dpnid) ? " [I]" : "");
            int index = lbox->AddString(buffer);
            lbox->SetValue(index, identity.dpnid);
            PackedColor colorSel = _color;
            if (identity.state == NGSPlay)
            {
                for (int j = 0; j < GetNetworkManager().NPlayerRoles(); j++)
                {
                    const PlayerRole* item = GetNetworkManager().GetPlayerRole(j);
                    if (item->player == identity.dpnid)
                    {
                        switch (item->side)
                        {
                            case TEast:
                                colorSel = _colorEast;
                                break;
                            case TWest:
                                colorSel = _colorWest;
                                break;
                            case TGuerrila:
                                colorSel = _colorRes;
                                break;
                            case TCivilian:
                                colorSel = _colorCiv;
                                break;
                        }
                        break;
                    }
                }
            }
            PackedColor color = ModAlpha(colorSel, 0.5);
            lbox->SetFtColor(index, color);
            lbox->SetSelColor(index, colorSel);
            /*
            if (identity.state != NGSPlay)
            {
                lbox->SetFtColor(index, inactive);
                lbox->SetSelColor(index, inactiveSel);
            }
            */
            if (identity.dpnid == cur)
            {
                sel = index;
            }
        }
    }
    lbox->SetCurSel(sel);
}

static RString FormatNumberMaxDigits(int number, int maxDigits)
{
    int maxNumber = 1;
    while (--maxDigits >= 0)
    {
        maxNumber *= 10;
    }
    maxNumber--;
    if (number > maxNumber)
    {
        return FormatNumber(maxNumber);
    }
    return FormatNumber(number);
}

inline RString FormatNumberBandwidth(int number)
{
    return FormatNumberMaxDigits(number, 6);
}

inline RString FormatNumberPing(int number)
{
    return FormatNumberMaxDigits(number, 4);
}

void DisplayMPPlayers::UpdatePlayerInfo()
{
    const PlayerIdentity* player = nullptr;
    const SquadIdentity* squad = nullptr;

    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
    if (lbox)
    {
        int sel = lbox->GetCurSel();
        if (sel >= 0)
        {
            player = GetNetworkManager().FindIdentity(lbox->GetValue(sel));
            if (player)
            {
                squad = player->squad;
            }
        }
    }

    C3DStatic* ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL));
    if (ctrl)
    {
        ctrl->SetText(player ? player->name : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_NAME));
    if (ctrl)
    {
        ctrl->SetText(player ? player->fullname : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MAIL));
    if (ctrl)
    {
        ctrl->SetText(player ? player->email : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_ICQ));
    if (ctrl)
    {
        ctrl->SetText(player ? player->icq : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_REMARK));
    if (ctrl)
    {
        ctrl->SetText(player ? player->remark : "");
    }

    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MINPING));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberPing(player->_minPing) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_AVGPING));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberPing(player->_avgPing) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MAXPING));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberPing(player->_maxPing) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MINBAND));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberBandwidth(player->_minBandwidth) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_AVGBAND));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberBandwidth(player->_avgBandwidth) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_MAXBAND));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberBandwidth(player->_maxBandwidth) : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_PL_DESYNC));
    if (ctrl)
    {
        ctrl->SetText(player ? FormatNumberBandwidth(player->_desync) : "");
    }

    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->nick : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_NAME));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->name : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_ID));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->id : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_MAIL));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->email : "");
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_WEB));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->web : "");
    }
    RString picture;
    if (squad && squad->picture.GetLength() > 0)
    {
        picture = Poseidon::FindNetworkSquadPictureTmpPath(squad->nick, squad->picture, [](const RString& path)
                                                           { return QIFStream::FileExists(path); });
        if (picture.GetLength() > 0)
        {
            picture = RString("\\") + picture;
        }
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_PICTURE));
    if (ctrl)
    {
        ctrl->SetText(picture);
    }
    ctrl = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MP_SQ_TITLE));
    if (ctrl)
    {
        ctrl->SetText(squad ? squad->title : "");
    }

    // ADDED
    if (player)
    {
        bool show = GetNetworkManager().IsServer() && player->dpnid != GetNetworkManager().GetPlayer();
        IControl* ctrl = GetCtrl(IDC_MP_KICKOFF);
        if (ctrl)
        {
            ctrl->ShowCtrl(show ||
                           GetNetworkManager().IsGameMaster() && player->dpnid != GetNetworkManager().GetPlayer());
        }
        ctrl = GetCtrl(IDC_MP_BAN);
        if (ctrl)
        {
            ctrl->ShowCtrl(show);
        }

        // Mute (VoN) / Ignore (chat) are client-local and available to
        // everyone — show them for any other player (not just admins).
        bool other = player->dpnid != GetNetworkManager().GetPlayer();
        if (IControl* mute = GetCtrl(IDC_MP_MUTE))
        {
            mute->ShowCtrl(other);
        }
        if (IControl* ignore = GetCtrl(IDC_MP_IGNORE))
        {
            ignore->ShowCtrl(other);
        }
    }
}

// ADDED
void DisplayMPPlayers::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_MP_KICKOFF:
            if (GetNetworkManager().IsServer())
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
                if (!lbox)
                {
                    break;
                }
                int sel = lbox->GetCurSel();
                if (sel < 0)
                {
                    break;
                }
                int dpnid = lbox->GetValue(sel);
                if (dpnid != GetNetworkManager().GetPlayer())
                {
                    GetNetworkManager().KickOff(dpnid, KORKick);
                }
            }
            else if (GetNetworkManager().IsGameMaster())
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
                if (!lbox)
                {
                    break;
                }
                int sel = lbox->GetCurSel();
                if (sel < 0)
                {
                    break;
                }
                int dpnid = lbox->GetValue(sel);
                if (dpnid != GetNetworkManager().GetPlayer())
                {
                    GetNetworkManager().SendKick(dpnid);
                }
            }
            break;
        case IDC_MP_BAN:
            if (GetNetworkManager().IsServer())
            {
                C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
                if (!lbox)
                {
                    break;
                }
                int sel = lbox->GetCurSel();
                if (sel < 0)
                {
                    break;
                }
                int dpnid = lbox->GetValue(sel);
                if (dpnid != GetNetworkManager().GetPlayer())
                {
                    GetNetworkManager().Ban(dpnid);
                }
            }
            break;
        case IDC_MP_MUTE:
        case IDC_MP_IGNORE:
        {
            // Client-local toggle: silence a player's VoN voice / hide their
            // chat. Keyed by dpnid (same id VoN packets and chat senders use).
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MP_PLAYERS));
            if (!lbox)
            {
                break;
            }
            int sel = lbox->GetCurSel();
            if (sel < 0)
            {
                break;
            }
            int dpnid = lbox->GetValue(sel);
            if (dpnid == GetNetworkManager().GetPlayer())
            {
                break;
            }
            if (idc == IDC_MP_MUTE)
            {
                ToggleVoiceMute(dpnid);
            }
            else
            {
                ToggleChatIgnore(dpnid);
            }
            UpdatePlayers();
            UpdatePlayerInfo();
            break;
        }
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

// Client wait display

void DisplayClientWait::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_AUTOCANCEL:
            Exit(idc);
            break;
        default:
            DisplayMPPlayers::OnButtonClicked(idc);
            break;
    }
}

void DisplayClientWait::OnSimulate(EntityAI* vehicle)
{
    NetworkGameState gameState = GetNetworkManager().GetServerState();
    RString state;
    switch (gameState)
    {
        case NGSPrepareRole:
        case NGSPrepareOK:
            state = LocalizeString(IDS_SESSION_SETUP);
            {
                TargetSide side = TSideUnknown;
                int player = GetNetworkManager().GetPlayer();
                for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
                    if (role->player == player)
                    {
                        side = role->side;
                        break;
                    }
                }
                if (side == TSideUnknown)
                {
                    break; // continue with waiting
                }
            }
        case NGSNone:
        case NGSCreating:
        case NGSCreate:
        case NGSLogin:
        case NGSEdit:
        case NGSPrepareSide:
            OnButtonClicked(IDC_AUTOCANCEL);
            return;
        case NGSDebriefing:
        case NGSDebriefingOK:
            state = LocalizeString(IDS_SESSION_DEBRIEFING);
            // continue with waiting
            break;
        case NGSTransferMission:
        case NGSLoadIsland:
            state = LocalizeString(IDS_SESSION_SETUP);
            // continue with waiting
            break;
        case NGSBriefing:
            state = LocalizeString(IDS_SESSION_BRIEFING);
            // continue with waiting
            break;
        case NGSPlay:
            state = LocalizeString(IDS_SESSION_PLAY);
            // continue with waiting
            break;
    }
    if (state.GetLength() > 0)
    {
        char buffer[256];
        ::sprintf(buffer, LocalizeString(IDS_CLIENT_WAIT_TITLE), (const char*)state);
        CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_CLIENT_WAIT_TITLE));
        if (text)
        {
            text->SetText(buffer);
        }
    }

    UpdatePlayers();
    Display::OnSimulate(vehicle);
}

} // namespace Poseidon
