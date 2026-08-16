#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <ctype.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#endif
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Game/Chat.hpp>

using namespace Poseidon;
namespace Poseidon
{

} // namespace Poseidon
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

namespace Poseidon
{
RString GetUserDirectory();
}

// Global-scope symbols (defined outside namespace Poseidon); declared here so the
// in-namespace calls below resolve to the true global definitions via fall-through lookup.
void AddDropAllActions(AIUnit* unit, UIActions& actions);
void ActivateSensor(ArcadeSensorActivation activ);
Poseidon::RadioChannel* FindChannel(AIUnit* unit, int channel);
extern int MaxCustomSoundSize;

namespace Poseidon
{

// Forward declarations from inGameUI.cpp
enum IDS : int;
RString LocalizeString(IDS ids);
RString LocalizeString(int ids);
RString LocalizeString(const char* str);
RString Localize(RString str);
AIUnit* GetSelectedUnit(int i);
void SetSelectedUnit(int i, AIUnit* unit);
void ClearSelectedUnits();
bool IsEmptySelectedUnits();
PackedBoolArray ListSelectedUnits();
int ValidateWeapon(EntityAI* vehicle, int weapon);
bool CheckJoin(AIGroup* grp);
OLink<AIUnit>* GetGSelectedUnits();
Team GetTeam(int i);
#define COMMAND_TIMEOUT 480.0 // 8 min

void InGameUI::CreateAttackList(AIGroup* group, Menu* submenu, int cmdBase)
{
    int nItem = 1;

    submenu->AddItem(new MenuItem(LocalizeString(IDS_WATCH_AUTO), SDL_SCANCODE_1, "1", CMD_WATCH_AUTO));

    bool friendly = false;
    RString menuName = submenu->_text;

    int n = _visibleListTemp.Size();
    for (int i = 0; i < n; i++)
    {
        Target* target = _visibleListTemp[i];
        if (!target)
        {
            continue;
        }
        if (!target->IsKnown())
        {
            continue;
        }
        if (target->vanished)
        {
            continue;
        }
        if (target->destroyed)
        {
            continue;
        }
        if (!target->idExact)
        {
            continue;
        }
        bool forceSplit = !friendly && target->side != TSideUnknown && !group->GetCenter()->IsEnemy(target->side);
        if (forceSplit)
        {
            friendly = true;
        }
        //		if (nItem == 0) forceSplit = false;
        if (nItem == 9 || forceSplit)
        {
            Menu* newmenu = new Menu();
            newmenu->_text = menuName;
            newmenu->_parent = submenu;
            submenu->AddItem(new MenuItem(LocalizeString(IDS_MORE_MENU), SDL_SCANCODE_0, "0", newmenu, CMD_NOTHING));
            submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU), SDL_SCANCODE_BACKSPACE,
                                          LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
            submenu = newmenu;
            nItem = 0;
        }
        char key[2] = "1";
        key[0] += nItem;
        RString name = target->type->GetDisplayName();

        char text[256];

        EntityAI* veh = target->idExact;
        AIUnit* u = veh ? veh->CommanderUnit() : nullptr;
        AIGroup* g = u ? u->GetGroup() : nullptr;
        if (g == group)
        {
            snprintf(text, sizeof(text), LocalizeString(IDS_TARGET_MENU_GROUP),
                     (const char*)u->GetPerson()->GetInfo()._name, (const char*)name, u->ID());
        }
        else
        {
            Vector3 pos = GWorld->CameraOn()->GetInvTransform() * target->position;
            int azimut = toInt(atan2(pos.X(), pos.Z()) * (6 / H_PI));
            if (azimut <= 0)
            {
                azimut += 12;
            }

            bool showSensor = false;
            EntityAI* sensorVeh = target->idSensor;
            AIUnit* sensor = sensorVeh ? sensorVeh->CommanderUnit() : nullptr;
            if (sensor)
            {
                AISubgroup* subSensor = sensor->GetSubgroup();
                if (subSensor)
                {
                    AIGroup* grpSensor = subSensor->GetGroup();
                    if (grpSensor == group)
                    {
                        showSensor = subSensor != group->MainSubgroup();
                    }
                }
            }

            if (showSensor)
            {
                snprintf(text, sizeof(text), LocalizeString(IDS_TARGET_MENU_SENSOR), (const char*)name, sensor->ID(),
                         azimut);
            }
            else
            {
                snprintf(text, sizeof(text), LocalizeString(IDS_TARGET_MENU), (const char*)name, azimut);
            }
        }
        submenu->AddItem(new MenuItem(text, SDL_SCANCODE_1 + nItem, key, cmdBase + i));
        nItem++;
    }
    submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU), SDL_SCANCODE_BACKSPACE,
                                  LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
}

void InGameUI::CollectActions(UIActions& actions)
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    AIUnit* unit = nullptr;
    int nUnits = 0;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (GSelectedUnits[i])
        {
            unit = GSelectedUnits[i];
            nUnits++;
        }
    }

    if (nUnits == 0)
    {
        return;
    }
    if (nUnits == 1)
    {
        // one unit selected
        int n = _visibleListTemp.Size();
        for (int i = 0; i < n; i++)
        {
            Target* target = _visibleListTemp[i];
            if (!target)
            {
                continue;
            }
            if (!target->IsKnown())
            {
                continue;
            }
            if (target->vanished)
            {
                continue;
            }
            EntityAI* veh = target->idExact;
            if (!veh)
            {
                continue;
            }
            veh->GetActions(actions, unit, false);
        }
        AddDropAllActions(unit, actions);
    }
    else
    {
        // more units selected
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = GSelectedUnits[i];
            if (unit)
            {
                unit->GetVehicle()->GetActions(actions, unit, true);
            }
        }
        for (int i = 0; i < actions.Size(); i++)
        {
            actions[i].target = nullptr;
        }
    }

    for (int i = 0; i < actions.Size();)
    {
        UIActionType type = actions[i].type;
        if (type == ATGetInCommander || type == ATGetInDriver || type == ATGetInGunner || type == ATGetInCargo ||
            type == ATGetOut)
        {
            actions.Delete(i);
        }
        else
        {
            i++;
        }
    }

    // agregation
    for (int i = 0; i < actions.Size(); i++)
    {
        UIAction& action1 = actions[i];
        for (int j = i + 1; j < actions.Size();)
        {
            UIAction& action2 = actions[j];
            if (action1.type == action2.type && action1.param == action2.param && action1.param2 == action2.param2 &&
                action1.param3 == action2.param3)
            {
                // select action with better target
                if (action1.target)
                {
                    if (action2.target)
                    {
                        // select better target
                        Vector3Val pos = unit->Position();
                        if (pos.Distance2(action1.target->Position()) > pos.Distance2(action2.target->Position()))
                        {
                            action1.target = action2.target;
                        }
                    }
                    // else action2 has no target - delete it
                }
                else
                {
                    // action1 has no target - delete it
                    action1.target = action2.target;
                }
                actions.Delete(j);
            }
            else
            {
                j++;
            }
        }
    }

    // sorting
    actions.Sort();
}

void InGameUI::RefreshActionsMenu()
{
    MenuItem* item = nullptr;
    for (int i = 0; i < _menuMain->_items.Size(); i++)
    {
        MenuItem* it = _menuMain->_items[i];
        if (it->_cmd == CMD_ACTION)
        {
            item = it;
            break;
        }
    }
    if (!item)
    {
        return;
    }

    UIActions actions;
    BackupTargets();
    CollectActions(actions);

    // level 1 submenu becomes current
    Menu* submenu = new Menu();
    submenu->_text = LocalizeString(IDS_ACTION);
    submenu->_parent = _menuMain;
    item->_submenu = submenu;
    _menuMain->RescanMinMax();

    _menuCurrent = submenu;
    if (_menuType == MTNone)
    {
        _menuType = MTMain;
    }
    ShowMenu();

    // add actions to menu
    int nItem = 0;
    for (int i = 0; i < actions.Size(); i++)
    {
        if (nItem == 9)
        {
            // more actions - new submenu
            Menu* newmenu = new Menu();
            newmenu->_text = LocalizeString(IDS_ACTION);
            newmenu->_parent = submenu;
            submenu->AddItem(new MenuItem(LocalizeString(IDS_MORE_MENU), SDL_SCANCODE_0, "0", newmenu, CMD_NOTHING));
            submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU), SDL_SCANCODE_BACKSPACE,
                                          LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
            submenu = newmenu;
            nItem = 0;
        }
        // add action
        UIAction& action = actions[i];
        RString displayName = action.GetDisplayName(nullptr);
        if (displayName.GetLength() == 0)
        {
            continue;
        }
        char key[2] = "1";
        key[0] += nItem;
        submenu->AddItem(new MenuItem(displayName, SDL_SCANCODE_1 + nItem, key, CMD_ACTION_TARGET, action.type,
                                      action.target, action.param, action.param2, action.param3));
        nItem++;
    }
    submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU), SDL_SCANCODE_BACKSPACE,
                                  LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
}

bool InGameUI::PlayCustomRadio(int index)
{
    if (index < 0 || index >= _customRadio.Size())
    {
        return false;
    }

    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return false;
    }

    RString name = RString("#") + _customRadio[index];
    ChatChannel channel = CCGroup;
    if (GWorld->GetMode() == GModeNetware)
    {
        channel = ActualChatChannel();
    }

    RadioChannel* radio = FindChannel(unit, channel);
    if (!radio)
    {
        return false;
    }

    radio->Say(name, unit, "", "", 2.0);
    SendRadioChatWave(channel, name, unit, "");
    return true;
}

void InGameUI::ProcessMenu(const Camera& camera, EntityAI* vehicle)
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    auto& input = InputSubsystem::Instance();
    AISubgroup* subgroup = unit->GetSubgroup();
    PoseidonAssert(subgroup);
    AIGroup* group = subgroup->GetGroup();
    PoseidonAssert(group);
    AICenter* center = group->GetCenter();
    PoseidonAssert(center);

    // blink state
    if (Glob.uiTime > _blinkStateChange)
    {
        _blinkStateChange = Glob.uiTime + 0.5;
        _blinkState = !_blinkState;
    }

    {
        static const struct UnitKey
        {
            int key, unit;
        } unitKeys[MAX_UNITS_PER_GROUP] = {{SDL_SCANCODE_F1, 0},  {SDL_SCANCODE_F2, 1},   {SDL_SCANCODE_F3, 2},
                                           {SDL_SCANCODE_F4, 3},  {SDL_SCANCODE_F5, 4},   {SDL_SCANCODE_F6, 5},
                                           {SDL_SCANCODE_F7, 6},  {SDL_SCANCODE_F8, 7},   {SDL_SCANCODE_F9, 8},
                                           {SDL_SCANCODE_F10, 9}, {SDL_SCANCODE_F11, 10}, {SDL_SCANCODE_F12, 11}};
        if (unit->IsGroupLeader() && group->NUnits() > 1)
        {
            // Units selection
            bool selectionChanged = false;
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                if (input.GetKeyToDo(unitKeys[i].key))
                {
                    ToggleSelection(group, unitKeys[i].unit + 1);
                    selectionChanged = true;
                }
            }
            if (InputSubsystem::Instance().GetActionToDo(UASelectAll))
            {
                bool allSelected = true;
                bool mainSubgrpSelected = true;
                for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                {
                    AIUnit* u = group->UnitWithID(i + 1);
                    if (!u || u == unit)
                    {
                        continue;
                    }
                    if (!GetSelectedUnit(i))
                    {
                        allSelected = false;
                        if (u->GetSubgroup() == group->MainSubgroup())
                        {
                            mainSubgrpSelected = false;
                        }
                    }
                }
                if (allSelected)
                {
                    ClearSelectedUnits();
                }
                else if (mainSubgrpSelected)
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* u = group->UnitWithID(i + 1);
                        if (!u || u == unit)
                        {
                            continue;
                        }
                        SetSelectedUnit(i, u);
                    }
                }
                else
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* u = group->UnitWithID(i + 1);
                        if (!u || u == unit)
                        {
                            continue;
                        }
                        if (u->GetSubgroup() == group->MainSubgroup())
                        {
                            SetSelectedUnit(i, u);
                        }
                        else
                        {
                            SetSelectedUnit(i, nullptr);
                        }
                    }
                }
                selectionChanged = true;
            }
            if (selectionChanged)
            {
                if (_menuType == MTNone && !IsEmptySelectedUnits())
                {
                    _menuType = MTMain;
                    _menuCurrent = _menuMain;
                }
                ShowMenu();

                Menu* actionMenu = nullptr;
                for (int i = 0; i < _menuMain->_items.Size(); i++)
                {
                    MenuItem* item = _menuMain->_items[i];
                    if (item->_cmd == CMD_ACTION)
                    {
                        actionMenu = item->_submenu;
                        break;
                    }
                }
                bool found = false;
                Menu* menu = _menuCurrent;
                while (menu)
                {
                    if (menu == actionMenu)
                    {
                        found = true;
                        break;
                    }
                    menu = menu->_parent;
                }
                if (found)
                {
                    RefreshActionsMenu();
                }
            }
        }
        else
        {
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                if (input.GetKeyToDo(unitKeys[i].key))
                {
                    _menuType = MTMain;
                    ShowMenu();
                }
            }
            ClearSelectedUnits();
        }
    }

    bool enableCommands = (unit->IsGroupLeader() && unit->GetLifeState() == AIUnit::LSAlive && group->NUnits() > 1);

    // check list of selected units
    bool notEmpty = false;
    bool notEmptyMySubgroup = false;
    bool notEmptySubgroups = false;
    bool notEmptyMainTeam = false;
    bool notEmptyRedTeam = false;
    bool notEmptyGreenTeam = false;
    bool notEmptyBlueTeam = false;
    bool notEmptyYellowTeam = false;

    if (enableCommands)
    {
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* u = GetSelectedUnit(i);
            if (u)
            {
                notEmpty = true;
                if (u->GetSubgroup() == subgroup)
                {
                    notEmptyMySubgroup = true;
                }
                if (u->GetSubgroup() != group->MainSubgroup())
                {
                    notEmptySubgroups = true;
                }
            }
        }

        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            if (!group->UnitWithID(i + 1))
            {
                continue;
            }
            switch (GetTeam(i))
            {
                case TeamMain:
                    notEmptyMainTeam = true;
                    break;
                case TeamRed:
                    notEmptyRedTeam = true;
                    break;
                case TeamGreen:
                    notEmptyGreenTeam = true;
                    break;
                case TeamBlue:
                    notEmptyBlueTeam = true;
                    break;
                case TeamYellow:
                    notEmptyYellowTeam = true;
                    break;
            }
        }
    }

    // show / hide radio menu items
    // bool canHaveRadioMenu = false;
    _menuMain->ShowCommand(CMD_RADIO_ALPHA, false);
    _menuMain->ShowCommand(CMD_RADIO_BRAVO, false);
    _menuMain->ShowCommand(CMD_RADIO_CHARLIE, false);
    _menuMain->ShowCommand(CMD_RADIO_DELTA, false);
    _menuMain->ShowCommand(CMD_RADIO_ECHO, false);
    _menuMain->ShowCommand(CMD_RADIO_FOXTROT, false);
    _menuMain->ShowCommand(CMD_RADIO_GOLF, false);
    _menuMain->ShowCommand(CMD_RADIO_HOTEL, false);
    _menuMain->ShowCommand(CMD_RADIO_INDIA, false);
    _menuMain->ShowCommand(CMD_RADIO_JULIET, false);
    if (unit->IsGroupLeader())
    {
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
            int cmd;
            switch (det->GetActivationBy())
            {
                case ASAAlpha:
                    cmd = CMD_RADIO_ALPHA;
                    goto showRadioCmd;
                case ASABravo:
                    cmd = CMD_RADIO_BRAVO;
                    goto showRadioCmd;
                case ASACharlie:
                    cmd = CMD_RADIO_CHARLIE;
                    goto showRadioCmd;
                case ASADelta:
                    cmd = CMD_RADIO_DELTA;
                    goto showRadioCmd;
                case ASAEcho:
                    cmd = CMD_RADIO_ECHO;
                    goto showRadioCmd;
                case ASAFoxtrot:
                    cmd = CMD_RADIO_FOXTROT;
                    goto showRadioCmd;
                case ASAGolf:
                    cmd = CMD_RADIO_GOLF;
                    goto showRadioCmd;
                case ASAHotel:
                    cmd = CMD_RADIO_HOTEL;
                    goto showRadioCmd;
                case ASAIndia:
                    cmd = CMD_RADIO_INDIA;
                    goto showRadioCmd;
                case ASAJuliet:
                    cmd = CMD_RADIO_JULIET;
                    goto showRadioCmd;
                showRadioCmd:
                {
                    RString text = det->GetText();
                    if (stricmp(text, "null") == 0)
                    {
                        continue;
                    }
                    _menuMain->ShowCommand(cmd, true);
                    text = Localize(text);
                    if (text.GetLength() > 0)
                    {
                        _menuMain->SetText(cmd, text);
                    }
                    else
                    {
                        _menuMain->ResetText(cmd);
                    }
                    // canHaveRadioMenu = true;
                }
            }
        }
    }

    Transport* transport = dyn_cast<Transport>(vehicle);
    bool vehicleCommander = transport && transport->CommanderUnit() == unit;
    bool commandsToGunner = (vehicleCommander && transport->GunnerUnit() && transport->GunnerUnit() != unit);
    bool commandsToPilot = (vehicleCommander && transport->PilotUnit() && transport->PilotUnit() != unit);

    // show / hide menu
    if (_menuType == MTMain && Glob.uiTime >= _lastMenuTime + menuHideTime)
    {
        _menuType = MTNone;
    }
    if (_menuType == MTMain)
    { // no menu if not group leader
        if (_tmPos > 0)
        {
            if (_tmIn)
            {
                _tmPos -= 3.0 * (Glob.uiTime - _tmTime);
                if (_tmPos < 0)
                {
                    _tmPos = 0;
                }
            }
            else
            {
                _tmIn = true;
                _tmOut = false;
            }
            _tmTime = Glob.uiTime;
        }
    }
    else
    {
        if (_tmPos < 1)
        {
            if (_tmOut)
            {
                _tmPos += 3.0 * (Glob.uiTime - _tmTime);
                if (_tmPos > 1)
                {
                    _tmPos = 1;
                }
            }
            else
            {
                _tmIn = false;
                _tmOut = true;
            }
            _tmTime = Glob.uiTime;
        }
    }

    // show / hide tank direction
    CameraType type = GWorld->GetCameraType();
    const VehicleType* vehType = vehicle->GetType();
    if (vehType->HasCommander() && vehicle->IsTurret(type))
    {
        _tankPos = 0;
    }
    else
    {
        _tankPos = 1;
    }

    bool isCommander = unit == vehicle->CommanderUnit();

    bool enableRepeat = subgroup->HasCommand() || GWorld->GetMode() == GModeNetware;
    _menuMain->EnableCommand(CMD_REPLY_DONE, enableRepeat);
    _menuMain->EnableCommand(CMD_REPLY_FAIL, enableRepeat);
    _menuMain->EnableCommand(CMD_REPLY_COPY, enableRepeat);
    _menuMain->EnableCommand(CMD_REPLY_REPEAT, enableRepeat);
    _menuMain->EnableCommand(CMD_REPLY_FIREREADY, isCommander);
    _menuMain->EnableCommand(CMD_REPLY_FIRENOTREADY, isCommander);

    bool isLeader = unit->IsGroupLeader() && group->NUnits() > 1;
    if (!isLeader)
    {
        ClearSelectedUnits();
    }
    while ((!_menuCurrent->_visible || !_menuCurrent->_enable) && _menuCurrent->_parent)
    {
        _menuCurrent = _menuCurrent->_parent;
    }

    // check formation
    for (int i = 0; i <= AI::NForms; i++)
    {
        _menuMain->CheckCommand(CMD_FORM_COLUMN + i, false);
    }
    _menuMain->CheckCommand(CMD_FORM_COLUMN + subgroup->GetFormation(), true);

    // some items are context dependent
    _menuMain->ShowCommand(CMD_REPORT, isLeader);
    _menuMain->ShowCommand(CMD_REPLY_WHERE_ARE_YOU, !isLeader);

    // enable / disable menu items, select mode, ground point
    static const int commandLeaderOnly[] = {
        CMD_FORM_COLUMN,   CMD_FORM_STAGCOL,   CMD_FORM_WEDGE,    CMD_FORM_ECHLEFT,   CMD_FORM_ECHRIGHT,
        CMD_FORM_VEE,      CMD_FORM_LINE,

        CMD_TEAM_RED,      CMD_TEAM_GREEN,     CMD_TEAM_BLUE,     CMD_TEAM_YELLOW,    CMD_TEAM_MAIN,
        CMD_ASSIGN_RED,    CMD_ASSIGN_GREEN,   CMD_ASSIGN_BLUE,   CMD_ASSIGN_YELLOW,  CMD_ASSIGN_MAIN,

        CMD_SUPPORT_MEDIC, CMD_SUPPORT_REPAIR, CMD_SUPPORT_REARM, CMD_SUPPORT_REFUEL, CMD_SUPPORT_DONE,
    };
    static const int commandNotEmpty[] = {// commands to unit
                                          CMD_MOVE_SUBMENU, CMD_WATCH_SUBMENU,

                                          CMD_GETIN, CMD_ADVANCE, CMD_STAY_BACK, CMD_FLANK_LEFT, CMD_FLANK_RIGHT,

                                          CMD_ACTION, CMD_HIDE, CMD_STOP, CMD_EXPECT, CMD_ENGAGE, CMD_FIRE,

                                          CMD_GETOUT, CMD_KEEP_FORM, CMD_LOOSE_FORM, CMD_STEALTH, CMD_COMBAT, CMD_AWARE,
                                          CMD_SAFE, CMD_POS_UP, CMD_POS_DOWN, CMD_POS_AUTO, CMD_REPORT,
                                          // CMD_WATCH_N,CMD_WATCH_NE,CMD_WATCH_E,CMD_WATCH_SE,
                                          // CMD_WATCH_S,CMD_WATCH_SW,CMD_WATCH_W,CMD_WATCH_NW,
                                          CMD_WATCH_AROUND, CMD_WATCH_AUTO, CMD_WATCH_TARGET, CMD_REPLY_KILLED};
    static const int commandGunner[] = {
        // commands to unit or vehicle
        CMD_HOLD_FIRE,
        CMD_OPEN_FIRE,
    };
    static const int commandDriver[] = {
        // commands to unit or vehicle
        CMD_NEXT_WAYPOINT,
        CMD_JOIN,
    };
    for (int i = 0; i < sizeof(commandLeaderOnly) / sizeof(*commandLeaderOnly); i++)
    {
        _menuMain->ShowAndEnableCommand(commandLeaderOnly[i], isLeader, isLeader);
    }
    for (int i = 0; i < sizeof(commandNotEmpty) / sizeof(*commandNotEmpty); i++)
    {
        _menuMain->ShowAndEnableCommand(commandNotEmpty[i], isLeader, notEmpty);
    }
    for (int i = 0; i < sizeof(commandGunner) / sizeof(*commandGunner); i++)
    {
        _menuMain->ShowAndEnableCommand(commandGunner[i], isLeader || vehicleCommander, notEmpty || commandsToGunner);
    }
    for (int i = 0; i < sizeof(commandDriver) / sizeof(*commandDriver); i++)
    {
        _menuMain->ShowAndEnableCommand(commandDriver[i], isLeader || vehicleCommander, notEmpty || commandsToPilot);
    }

    _menuMain->ShowAndEnableCommand(CMD_TEAM_MAIN, isLeader, notEmptyMainTeam);
    _menuMain->ShowAndEnableCommand(CMD_TEAM_RED, isLeader, notEmptyRedTeam);
    _menuMain->ShowAndEnableCommand(CMD_TEAM_GREEN, isLeader, notEmptyGreenTeam);
    _menuMain->ShowAndEnableCommand(CMD_TEAM_BLUE, isLeader, notEmptyBlueTeam);
    _menuMain->ShowAndEnableCommand(CMD_TEAM_YELLOW, isLeader, notEmptyYellowTeam);
    _menuMain->ShowCommand(CMD_ASSIGN_MAIN, isLeader);
    _menuMain->ShowCommand(CMD_ASSIGN_RED, isLeader);
    _menuMain->ShowCommand(CMD_ASSIGN_GREEN, isLeader);
    _menuMain->ShowCommand(CMD_ASSIGN_BLUE, isLeader);
    _menuMain->ShowCommand(CMD_ASSIGN_YELLOW, isLeader);

    // Menu
    while ((!_menuCurrent->_visible || !_menuCurrent->_enable) && _menuCurrent->_parent)
    {
        _menuCurrent = _menuCurrent->_parent;
    }
    if (_menuCurrent)
    {
        for (int i = 0; i < _menuCurrent->_items.Size(); i++)
        {
            MenuItem* item = _menuCurrent->_items[i];
            if (item->_cmd == CMD_SEPARATOR)
            {
                continue;
            }
            if (!item->_visible || !item->_enable)
            {
                continue;
            }
            if (!input.GetKeyToDo(item->_key))
            {
                continue;
            }

            if (item->_cmd == CMD_ACTION_TARGET)
            {
                // action
                IssueAction(group, *item);
                _visibleListTemp.Clear();
                MenuItem* actions = _menuMain->Find(CMD_ACTION);
                if (actions)
                {
                    actions->_submenu = nullptr;
                    _menuMain->RescanMinMax();
                }
            }
            else if (item->_cmd >= CMD_WATCH_TARGET)
            {
                if (notEmpty)
                {
                    IssueWatchTarget(group, item->_cmd - CMD_WATCH_TARGET);
                    _visibleListTemp.Clear();
                }
                else
                {
                    if (vehicleCommander)
                    {
                        Target* target = _visibleListTemp[item->_cmd - CMD_WATCH_TARGET];
                        if (target)
                        {
                            if (transport->IsLocal())
                            {
                                transport->SendTarget(target);
                            }
                            else
                            {
                                RadioMessageVTarget msg(transport, target);
                                GetNetworkManager().SendRadioMessage(&msg);
                            }
                            _lockTarget = target;
                            _timeSendTarget = UITIME_MAX;
                            _lockAimValidUntil = Glob.uiTime - 60;
                        }
                    }
                }
                _visibleListTemp.Clear();
            }
            else if (item->_cmd >= CMD_GETIN_TARGET)
            {
                // get in
                IssueGetIn(group, item->_cmd - CMD_GETIN_TARGET);
                _visibleListTemp.Clear();
            }
            else if (item->_cmd >= CMD_MOVE_FIRST)
            {
                // move
                if (!IsEmptySelectedUnits())
                {
                    IssueMove(group, item->_cmd - CMD_MOVE_FIRST);
                }
            }
            else
            {
                switch (item->_cmd)
                {
                    case CMD_HIDE_MENU:
                        if (_menuType == MTNone)
                        {
                            _menuType = MTMain;
                            ShowMenu();
                        }
                        else
                        {
                            _menuType = MTNone;
                        }
                        _menuCurrent = _menuMain;
                        goto MenuOK;
                    case CMD_BACK:
                        if (_menuType == MTNone)
                        {
                            _menuType = MTMain;
                            _menuCurrent = _menuMain;
                        }
                        else
                        {
                            if (_menuCurrent->_parent)
                            {
                                _menuCurrent = _menuCurrent->_parent;
                            }
                        }
                        ShowMenu();
                        goto MenuOK;
                    case CMD_GETIN:
                        BackupTargets();
                        if (transport)
                        {
                            for (int i = 0; i < _visibleListTemp.Size(); i++)
                            {
                                Target* target = _visibleListTemp[i];
                                PoseidonAssert(target);
                                if (target->idExact == transport)
                                {
                                    _visibleListTemp.Delete(i);
                                    _visibleListTemp.Insert(0, target);
                                    break;
                                }
                            }
                        }
                        {
                            int n = _visibleListTemp.Size();
                            if (n > 0)
                            {
                                Menu* submenu = new Menu();
                                submenu->_text = LocalizeString(IDS_GETIN);
                                submenu->_parent = _menuCurrent;
                                submenu->RescanMinMax();
                                item->_submenu = submenu;
                                _menuCurrent = submenu;
                                if (_menuType == MTNone)
                                {
                                    _menuType = MTMain;
                                }
                                ShowMenu();
                                int nItem = 1;

                                submenu->AddItem(
                                    new MenuItem(LocalizeString(IDS_GETOUT), SDL_SCANCODE_1, "1", CMD_GETOUT));

                                for (int i = 0; i < n; i++)
                                {
                                    Target* target = _visibleListTemp[i];
                                    if (!target)
                                    {
                                        continue;
                                    }
                                    if (!target->IsKnown())
                                    {
                                        continue;
                                    }
                                    if (target->vanished)
                                    {
                                        continue;
                                    }
                                    if (target->destroyed)
                                    {
                                        continue;
                                    }
                                    Object* obj = target->idExact;
                                    Transport* veh = dyn_cast<Transport>(obj);
                                    if (!veh)
                                    {
                                        continue;
                                    }
                                    if (!center->IsFriendly(target->side))
                                    {
                                        continue;
                                    }
                                    bool asDriver = veh->QCanIGetIn();
                                    bool asCommander = veh->QCanIGetInCommander();
                                    bool asGunner = veh->QCanIGetInGunner();
                                    bool asCargo = veh->QCanIGetInCargo();
                                    if (!asDriver && !asCommander && !asGunner && !asCargo)
                                    {
                                        continue;
                                    }
                                    if (nItem == 9)
                                    {
                                        Menu* newmenu = new Menu();
                                        newmenu->_text = LocalizeString(IDS_GETIN);
                                        newmenu->_parent = submenu;
                                        submenu->AddItem(new MenuItem(LocalizeString(IDS_MORE_MENU), SDL_SCANCODE_0,
                                                                      "0", newmenu, CMD_NOTHING));
                                        submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU),
                                                                      SDL_SCANCODE_BACKSPACE,
                                                                      LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
                                        submenu = newmenu;
                                        nItem = 0;
                                    }
                                    char key[2] = "1";
                                    key[0] += nItem;
                                    char text[256];
                                    RString name = target->type->GetDisplayName();

                                    AIUnit* u = veh ? veh->CommanderUnit() : nullptr;
                                    AIGroup* g = u ? u->GetGroup() : nullptr;
                                    if (g == group)
                                    {
                                        snprintf(text, sizeof(text), LocalizeString(IDS_TARGET_MENU_GROUP),
                                                 (const char*)u->GetPerson()->GetInfo()._name, (const char*)name,
                                                 u->ID());
                                    }
                                    else
                                    {
                                        Vector3 pos = GWorld->CameraOn()->GetInvTransform() * target->position;
                                        int azimut = toInt(atan2(pos.X(), pos.Z()) * (6 / H_PI));
                                        if (azimut <= 0)
                                        {
                                            azimut += 12;
                                        }
                                        snprintf(text, sizeof(text), LocalizeString(IDS_TARGET_MENU), (const char*)name,
                                                 azimut);
                                    }
                                    Menu* newmenu = new Menu();
                                    submenu->AddItem(
                                        new MenuItem(text, SDL_SCANCODE_1 + nItem, key, newmenu, CMD_NOTHING));
                                    newmenu->_text = LocalizeString(IDS_GETIN_POS);
                                    newmenu->_parent = submenu;
                                    int cmd = CMD_GETIN_TARGET + N_GETIN_POS * i;
                                    newmenu->AddItem(
                                        new MenuItem(LocalizeString(IDS_GETIN_POS_ANY), SDL_SCANCODE_1, "1", cmd));
                                    if (asDriver)
                                    {
                                        if (target->type->IsKindOf(GWorld->Preloaded(VTypeAir)))
                                        {
                                            newmenu->AddItem(new MenuItem(LocalizeString(IDS_GETIN_POS_PILOT),
                                                                          SDL_SCANCODE_2, "2", cmd + 1));
                                        }
                                        else
                                        {
                                            newmenu->AddItem(new MenuItem(LocalizeString(IDS_GETIN_POS_DRIVER),
                                                                          SDL_SCANCODE_2, "2", cmd + 1));
                                        }
                                    }
                                    if (asCommander)
                                    {
                                        newmenu->AddItem(new MenuItem(LocalizeString(IDS_GETIN_POS_COMM),
                                                                      SDL_SCANCODE_3, "3", cmd + 2));
                                    }
                                    if (asGunner)
                                    {
                                        newmenu->AddItem(new MenuItem(LocalizeString(IDS_GETIN_POS_GUNN),
                                                                      SDL_SCANCODE_4, "4", cmd + 3));
                                    }
                                    if (asCargo)
                                    {
                                        newmenu->AddItem(new MenuItem(LocalizeString(IDS_GETIN_POS_CARGO),
                                                                      SDL_SCANCODE_5, "5", cmd + 4));
                                    }
                                    newmenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU),
                                                                  SDL_SCANCODE_BACKSPACE,
                                                                  LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
                                    nItem++;
                                }
                                submenu->AddItem(new MenuItem(LocalizeString(IDS_CANCEL_MENU), SDL_SCANCODE_BACKSPACE,
                                                              LocalizeString(IDS_MENU_BACKSPACE), CMD_BACK));
                            }
                        }
                        goto MenuOK;
                        break;
                    case CMD_GETOUT:
                        IssueCommand(vehicle, Command::GetOut);
                        break;
                    case CMD_HIDE:
                        IssueCommand(vehicle, Command::Hide);
                        break;
                    case CMD_STOP:
                        IssueCommand(vehicle, Command::Stop);
                        break;
                    case CMD_EXPECT:
                        IssueCommand(vehicle, Command::Expect);
                        break;
                    case CMD_WATCH_N:
                    case CMD_WATCH_NE:
                    case CMD_WATCH_E:
                    case CMD_WATCH_SE:
                    case CMD_WATCH_S:
                    case CMD_WATCH_SW:
                    case CMD_WATCH_W:
                    case CMD_WATCH_NW:
                        IssueWatch(group, item->_cmd - CMD_WATCH_N);
                        break;
                    case CMD_WATCH_AROUND:
                        IssueWatchAround(group);
                        break;
                    case CMD_WATCH_AUTO:
                        IssueWatchAuto(group);
                        break;
                    case CMD_ENGAGE:
                        IssueEngage(group);
                        break;
                    case CMD_FIRE:
                        IssueFire(group);
                        break;
                    case CMD_JOIN:
                        IssueCommand(vehicle, Command::Join);
                        break;
                    case CMD_WATCH:
                        BackupTargets();
                        {
                            int n = _visibleListTemp.Size();
                            if (n > 0)
                            {
                                Menu* submenu = new Menu();
                                submenu->_text = LocalizeString(IDS_WATCH);
                                submenu->_parent = _menuCurrent;
                                item->_submenu = submenu;
                                submenu->RescanMinMax();
                                _menuCurrent = submenu;
                                if (_menuType == MTNone)
                                {
                                    _menuType = MTMain;
                                }
                                ShowMenu();
                                CreateAttackList(group, submenu, CMD_WATCH_TARGET);
                            }
                        }
                        goto MenuOK;
                    case CMD_ACTION:
                        RefreshActionsMenu();
                        goto MenuOK;
                    case CMD_FORM_COLUMN:
                    case CMD_FORM_STAGCOL:
                    case CMD_FORM_WEDGE:
                    case CMD_FORM_ECHLEFT:
                    case CMD_FORM_ECHRIGHT:
                    case CMD_FORM_VEE:
                    case CMD_FORM_LINE:
                        group->SendFormation((AI::Formation)(AI::FormColumn + item->_cmd - CMD_FORM_COLUMN), subgroup);
                        ClearSelectedUnits();
                        break;
                    case CMD_STEALTH:
                        group->SendBehaviour(CMStealth, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_COMBAT:
                        group->SendBehaviour(CMCombat, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_AWARE:
                        group->SendBehaviour(CMAware, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_SAFE:
                        group->SendBehaviour(CMSafe, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_KEEP_FORM:
                        group->SendLooseFormation(false, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_LOOSE_FORM:
                        group->SendLooseFormation(true, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_HOLD_FIRE:
                        group->SendOpenFire(OFSHoldFire, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_OPEN_FIRE:
                        group->SendOpenFire(OFSOpenFire, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_ADVANCE:
                        SetFormationPos(unit, AI::PosAdvance);
                        ClearSelectedUnits();
                        break;
                    case CMD_STAY_BACK:
                        SetFormationPos(unit, AI::PosStayBack);
                        ClearSelectedUnits();
                        break;
                    case CMD_FLANK_LEFT:
                        SetFormationPos(unit, AI::PosFlankLeft);
                        ClearSelectedUnits();
                        break;
                    case CMD_FLANK_RIGHT:
                        SetFormationPos(unit, AI::PosFlankRight);
                        ClearSelectedUnits();
                        break;
                    case CMD_POS_UP:
                        SetUnitPosition(unit, UPUp);
                        break;
                    case CMD_POS_DOWN:
                        SetUnitPosition(unit, UPDown);
                        break;
                    case CMD_POS_AUTO:
                        SetUnitPosition(unit, UPAuto);
                        break;
                    case CMD_REPORT:
                        group->SendReportStatus(ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_TEAM_MAIN:
                        ClearSelectedUnits();
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            if (GetTeam(i) != TeamMain)
                            {
                                continue;
                            }
                            AIUnit* u = group->UnitWithID(i + 1);
                            if (!u || u == unit)
                            {
                                continue;
                            }
                            SetSelectedUnit(i, u);
                        }
                        goto MainMenu;
                    case CMD_TEAM_RED:
                        ClearSelectedUnits();
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            if (GetTeam(i) != TeamRed)
                            {
                                continue;
                            }
                            AIUnit* u = group->UnitWithID(i + 1);
                            if (!u || u == unit)
                            {
                                continue;
                            }
                            SetSelectedUnit(i, u);
                        }
                        goto MainMenu;
                    case CMD_TEAM_GREEN:
                        ClearSelectedUnits();
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            if (GetTeam(i) != TeamGreen)
                            {
                                continue;
                            }
                            AIUnit* u = group->UnitWithID(i + 1);
                            if (!u || u == unit)
                            {
                                continue;
                            }
                            SetSelectedUnit(i, u);
                        }
                        goto MainMenu;
                    case CMD_TEAM_BLUE:
                        ClearSelectedUnits();
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            if (GetTeam(i) != TeamBlue)
                            {
                                continue;
                            }
                            AIUnit* u = group->UnitWithID(i + 1);
                            if (!u || u == unit)
                            {
                                continue;
                            }
                            SetSelectedUnit(i, u);
                        }
                        goto MainMenu;
                    case CMD_TEAM_YELLOW:
                        ClearSelectedUnits();
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            if (GetTeam(i) != TeamYellow)
                            {
                                continue;
                            }
                            AIUnit* u = group->UnitWithID(i + 1);
                            if (!u || u == unit)
                            {
                                continue;
                            }
                            SetSelectedUnit(i, u);
                        }
                        goto MainMenu;
                    case CMD_ASSIGN_MAIN:
                        group->GetRadio().Transmit(new RadioMessageTeam(group, ListSelectedUnits(), TeamMain),
                                                   center->GetLanguage());
                        ClearSelectedUnits();
                        break;
                    case CMD_ASSIGN_RED:
                        group->GetRadio().Transmit(new RadioMessageTeam(group, ListSelectedUnits(), TeamRed),
                                                   center->GetLanguage());
                        ClearSelectedUnits();
                        break;
                    case CMD_ASSIGN_GREEN:
                        group->GetRadio().Transmit(new RadioMessageTeam(group, ListSelectedUnits(), TeamGreen),
                                                   center->GetLanguage());
                        ClearSelectedUnits();
                        break;
                    case CMD_ASSIGN_BLUE:
                        group->GetRadio().Transmit(new RadioMessageTeam(group, ListSelectedUnits(), TeamBlue),
                                                   center->GetLanguage());
                        ClearSelectedUnits();
                        break;
                    case CMD_ASSIGN_YELLOW:
                        group->GetRadio().Transmit(new RadioMessageTeam(group, ListSelectedUnits(), TeamYellow),
                                                   center->GetLanguage());
                        ClearSelectedUnits();
                        break;
                    case CMD_RADIO_ALPHA:
                        ActivateSensor(ASAAlpha);
                        break;
                    case CMD_RADIO_BRAVO:
                        ActivateSensor(ASABravo);
                        break;
                    case CMD_RADIO_CHARLIE:
                        ActivateSensor(ASACharlie);
                        break;
                    case CMD_RADIO_DELTA:
                        ActivateSensor(ASADelta);
                        break;
                    case CMD_RADIO_ECHO:
                        ActivateSensor(ASAEcho);
                        break;
                    case CMD_RADIO_FOXTROT:
                        ActivateSensor(ASAFoxtrot);
                        break;
                    case CMD_RADIO_GOLF:
                        ActivateSensor(ASAGolf);
                        break;
                    case CMD_RADIO_HOTEL:
                        ActivateSensor(ASAHotel);
                        break;
                    case CMD_RADIO_INDIA:
                        ActivateSensor(ASAIndia);
                        break;
                    case CMD_RADIO_JULIET:
                        ActivateSensor(ASAJuliet);
                        break;
                    case CMD_NEXT_WAYPOINT:
                        if (notEmpty)
                        {
                            // FIX - implementation of command "NEXT WAYPOINT" for selected units
                            int& index = group->GetCurrent()->_fsm->Var(0);
                            if (index < group->NWaypoints())
                            {
                                const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
                                Command cmd;
                                cmd._message = Command::Move;
                                cmd._context = Command::CtxUI;
                                cmd._destination = wInfo.position;
                                group->SendCommand(cmd, ListSelectedUnits());
                                ClearSelectedUnits();
                            }
                        }
                        else if (vehicleCommander)
                        {
                            int& index = group->GetCurrent()->_fsm->Var(0);
                            if (index < group->NWaypoints())
                            {
                                const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
                                if (transport->IsLocal())
                                {
                                    transport->SendMove(wInfo.position);
                                }
                                else
                                {
                                    RadioMessageVMove msg(transport, wInfo.position);
                                    GetNetworkManager().SendRadioMessage(&msg);
                                }
                            }
                        }
                        break;
                    case CMD_REPLY_FIREREADY:
                        SendFireReady(unit, true);
                        break;
                    case CMD_REPLY_FIRENOTREADY:
                        SendFireReady(unit, false);
                        break;
                    case CMD_REPLY_DONE:
                        SendAnswer(unit, AI::CommandCompleted);
                        break;
                    case CMD_REPLY_FAIL:
                        SendAnswer(unit, AI::CommandFailed);
                        break;
                    case CMD_REPLY_COPY:
                        SendConfirm(unit);
                        break;
                    case CMD_REPLY_KILLED:
                        SendKilled(unit, ListSelectedUnits());
                        ClearSelectedUnits();
                        break;
                    case CMD_REPLY_HIT:
                        if (unit->GetVehicleIn())
                        {
                            SendResourceState(unit, AI::DammageCritical);
                        }
                        else
                        {
                            SendResourceState(unit, AI::HealthCritical);
                        }
                        break;
                    case CMD_REPLY_INJURED:
                        SendResourceState(unit, AI::HealthCritical);
                        if (unit->IsGroupLeader())
                        {
                            const AITargetInfo* info = group->FindHealPosition(AIUnit::RSCritical, unit);
                            if (info)
                            {
                                VehicleSupply* veh = dyn_cast<VehicleSupply, EntityAI>(info->_idExact);
                                if (veh)
                                {
                                    Command cmd;
                                    cmd._message = Command::Heal;
                                    cmd._destination = veh->Position();
                                    cmd._target = veh;
                                    cmd._time = Glob.time + COMMAND_TIMEOUT;
                                    group->SendAutoCommandToUnit(cmd, unit, true);
                                }
                            }
                        }
                        break;
                    case CMD_REPLY_AMMO_LOW:
                        SendResourceState(unit, AI::AmmoLow);
                        break;
                    case CMD_REPLY_FUEL_LOW:
                        SendResourceState(unit, AI::FuelLow);
                        break;
                    case CMD_REPLY_REPEAT:
                        group->GetRadio().Transmit(new RadioMessageRepeatCommand(unit, group), center->GetLanguage());
                        break;
                    case CMD_REPLY_WHERE_ARE_YOU:
                        group->GetRadio().Transmit(new RadioMessageWhereAreYou(unit, group), center->GetLanguage());
                        break;
                    case CMD_REPLY_ENGAGING:
                    {
                        Command cmd;
                        cmd._message = Command::Attack;
                        group->GetRadio().Transmit(new RadioMessageNotifyCommand(unit, group, cmd),
                                                   center->GetLanguage());
                    }
                    break;
                    case CMD_REPLY_UNDER_FIRE:
                        group->GetRadio().Transmit(new RadioMessageUnderFire(unit, group), center->GetLanguage());
                        break;
                    case CMD_REPLY_ONE_LESS:
                        SendObjectDestroyed(unit, group);
                        break;
                    case CMD_SUPPORT_MEDIC:
                        center->GetRadio().Transmit(new RadioMessageSupportAsk(group, ATHeal), center->GetLanguage());
                        break;
                    case CMD_SUPPORT_REPAIR:
                        center->GetRadio().Transmit(new RadioMessageSupportAsk(group, ATRepair), center->GetLanguage());
                        break;
                    case CMD_SUPPORT_REARM:
                        center->GetRadio().Transmit(new RadioMessageSupportAsk(group, ATRearm), center->GetLanguage());
                        break;
                    case CMD_SUPPORT_REFUEL:
                        center->GetRadio().Transmit(new RadioMessageSupportAsk(group, ATRefuel), center->GetLanguage());
                        break;
                    case CMD_SUPPORT_DONE:
                        center->GetRadio().Transmit(new RadioMessageSupportDone(group), center->GetLanguage());
                        break;
                    case CMD_RADIO_CUSTOM_1:
                    case CMD_RADIO_CUSTOM_2:
                    case CMD_RADIO_CUSTOM_3:
                    case CMD_RADIO_CUSTOM_4:
                    case CMD_RADIO_CUSTOM_5:
                    case CMD_RADIO_CUSTOM_6:
                    case CMD_RADIO_CUSTOM_7:
                    case CMD_RADIO_CUSTOM_8:
                    case CMD_RADIO_CUSTOM_9:
                    case CMD_RADIO_CUSTOM_0:
                        PlayCustomRadio(item->_cmd - CMD_RADIO_CUSTOM_1);
                        break;
                    default:
                        if (item->_submenu)
                        {
                            if (_menuType == MTNone)
                            {
                                Menu* top = _menuCurrent;
                                while (top->_parent)
                                {
                                    top = top->_parent;
                                }
                                if (top == _menuMain)
                                {
                                    _menuType = MTMain;
                                }
                                else
                                {
                                    Fail("Unknown root menu");
                                }
                            }
                            _menuCurrent = item->_submenu;
                            ShowMenu();
                            goto MenuOK;
                        }
                        else
                        {
                            LOG_DEBUG(UI, "Bad command id {}", item->_cmd);
                            ClearSelectedUnits();
                        }
                        break;
                } // switch( command )
            }
            _menuType = MTNone;
        MainMenu:
            _menuCurrent = _menuMain;

        MenuOK:
            break;
        } // for( menu items )
    } // if( _menuCurrent )
}

} // namespace Poseidon
