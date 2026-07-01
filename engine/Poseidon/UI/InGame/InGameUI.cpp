#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
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
#include <limits.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/EnumDecl.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#ifdef _WIN32
#include <io.h>
#endif
#include <SDL3/SDL_scancode.h>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <Poseidon/UI/InGame/InGameUIDrawShared.hpp>

namespace Poseidon
{
class AIUnit;
}
class UIActions;
void AddDropActions(AIUnit* unit, UIActions& actions);

namespace Poseidon
{
using Poseidon::Foundation::MStorage;
using Poseidon::Foundation::UITime;

// #include "win.h"

#define FontS "tahomaB24"
#define FontM "tahomaB36"

namespace
{
int TranslateLegacyMenuKey(int key)
{
    if (key >= 2 && key <= 10)
        return SDL_SCANCODE_1 + (key - 2);
    if (key == 11)
        return SDL_SCANCODE_0;
    if (key == 14)
        return SDL_SCANCODE_BACKSPACE;
    return key;
}
} // namespace

AIUnit* GetSelectedUnit(int i);
void SetSelectedUnit(int i, AIUnit* unit);
void ClearSelectedUnits();
bool IsEmptySelectedUnits();
PackedBoolArray ListSelectedUnits();
Team GetTeam(int i);
void SetTeam(int i, Team team);
void ClearTeams();
PackedBoolArray ListTeam(Team team);

RString InGameUI::GetActionMenuTexts() const
{
    if (!ShouldShowGameplayHUD())
    {
        return RString();
    }

    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return RString();
    }
    RString result;
    for (int i = 0; i < _actions.Size(); i++)
    {
        if (i > 0)
        {
            result = result + RString("\n");
        }
        result = result + _actions[i].GetDisplayName(unit);
    }
    return result;
}

RString InGameUI::GetCommandMenuTexts() const
{
    if (_menuType == MTNone || !_menuCurrent)
    {
        return RString();
    }

    RString result = _menuCurrent->_text;
    for (int i = 0; i < _menuCurrent->_items.Size(); i++)
    {
        const MenuItem* item = _menuCurrent->_items[i];
        if (!item || !item->_visible || item->_cmd == CMD_SEPARATOR)
        {
            continue;
        }

        if (result.GetLength() > 0)
        {
            result = result + RString("\n");
        }
        result = result + item->_text;
    }
    return result;
}

bool InGameUI::ShouldShowGameplayHUD() const
{
    if (!GWorld)
    {
        return false;
    }

    return GWorld->GetControllerUiScene().kind == ControllerSceneKind::Gameplay;
}

void InGameUI::ProcessActions(AIUnit* unit)
{
    if (!ShouldShowGameplayHUD())
    {
        _actions.Clear();
        return;
    }

    if (unit->GetLifeState() != AIUnit::LSAlive || !unit->GetGroup())
    {
        _actions.Clear();
        return;
    }

    auto& input = InputSubsystem::Instance();

    // first process user input, than collect new actions (actions may changed)
    // user input
    float age = _actions.GetAge();
    bool visible = age >= protectionTime && age <= dimTime;

    float scrollDelta = input.ConsumeCursorScroll();
    if (input.GetActionToDo(UAPrevAction))
    {
        _actions.SelectPrev(true);
        _actions.Refresh(true);
    }
    if (scrollDelta > 0 && visible)
    {
        _actions.SelectPrev(false);
        _actions.Refresh(true);
    }
    if (input.GetActionToDo(UANextAction))
    {
        _actions.SelectNext(true);
        _actions.Refresh(true);
    }
    if (scrollDelta < 0 && visible)
    {
        _actions.SelectNext(false);
        _actions.Refresh(true);
    }
    if (input.GetActionToDo(UAAction))
    {
        if (visible)
        {
            if (_actions.Size() > 0)
            {
                if (!GetNetworkManager().IsControlsPaused())
                {
                    _actions.ProcessAction(unit);
                }
            }
            else
            {
                _actions.Hide();
            }
        }
        else
        {
            _actions.Refresh(true);
        }
    }

    UIActions actions;
    static StaticStorage<UIAction> actionStorage;
    actions.SetStorage(actionStorage.Init(64));

    // collect all actions
    EntityAI* veh = unit->GetVehicle();
    veh->GetActions(actions, unit, true);

    const TargetList& visibleList = *VisibleList();
    for (int i = 0; i < visibleList.Size(); i++)
    {
        EntityAI* target = visibleList[i]->idExact;
        if (!target)
        {
            continue;
        }
        if (target == veh)
        {
            continue;
        }
        target->GetActions(actions, unit, true);
    }

    ::AddDropActions(unit, actions);

    Person* person = unit->GetPerson();
    if (person->QIsManual())
    {
        // night vision
        if (person->IsNVEnabled())
        {
            actions.Add(ATNVGoggles, person, 0.511, 0);
        }
    }

    int n = actions.Size();

    // check for new action
    bool newAction = false;
    if (n == 0)
    {
        if (_actions.Size() > 0)
        {
            _actions.Hide();
        }
    }
    else
    {
        for (int i = 0; i < n; i++)
        {
            UIAction& action = actions[i];
            if (!action.showWindow)
            {
                continue;
            }
            bool found = false;
            for (int j = 0; j < _actions.Size(); j++)
            {
                UIAction& compare = _actions[j];
                if (compare.target == action.target && compare.type == action.type && compare.param == action.param)
                {
                    found = compare.showWindow;
                    break;
                }
            }
            if (!found)
            {
                newAction = true;
                break;
            }
        }
    }
    _actions.Resize(n);
    for (int i = 0; i < n; i++)
    {
        _actions[i] = actions[i];
    }
    if (newAction)
    {
        _actions.Refresh(false);
    }

    // sort actions
    _actions.Sort();
}

// Global selected units - valid for IngameUI and map

DisplayUnitInfo::DisplayUnitInfo(ControlsContainer* parent) : Display(parent)
{
    SetCursor(nullptr);
    InitControls();
}

void DisplayUnitInfo::InitControls()
{
    time = nullptr;
    date = nullptr;
    name = nullptr;
    unit = nullptr;
    valueExp = nullptr;
    formation = nullptr;
    combatMode = nullptr;
    valueHealth = nullptr;
    weapon = nullptr;
    ammo = nullptr;
    vehicle = nullptr;
    speed = nullptr;
    alt = nullptr;
    valueArmor = nullptr;
    valueFuel = nullptr;
    cargoMan = nullptr;
    cargoFuel = nullptr;
    cargoRepair = nullptr;
    cargoAmmo = nullptr;
}

void DisplayUnitInfo::Reload(const ParamEntry& clsEntry)
{
    Init();
    InitControls();
    Load(clsEntry);
}

Control* DisplayUnitInfo::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_IGUI_BG:
            background = new CStatic(this, idc, cls);
            return background;
        case IDC_IGUI_TIME:
            time = new CStaticTime(this, idc, cls, true);
            return time;
        case IDC_IGUI_DATE:
            date = new CStatic(this, idc, cls);
            return date;
        case IDC_IGUI_NAME:
            name = new CStatic(this, idc, cls);
            return name;
        case IDC_IGUI_UNIT:
            unit = new CStatic(this, idc, cls);
            return unit;
        case IDC_IGUI_VALUE_EXP:
            valueExp = new CProgressBar(this, idc, cls);
            return valueExp;
        case IDC_IGUI_FORMATION:
            formation = new CStatic(this, idc, cls);
            return formation;
        case IDC_IGUI_COMBAT_MODE:
            combatMode = new CStatic(this, idc, cls);
            return combatMode;
        case IDC_IGUI_VALUE_HEALTH:
            valueHealth = new CProgressBar(this, idc, cls);
            return valueHealth;
        case IDC_IGUI_WEAPON:
            weapon = new CStatic(this, idc, cls);
            return weapon;
        case IDC_IGUI_AMMO:
            ammo = new CStatic(this, idc, cls);
            return ammo;
        case IDC_IGUI_VEHICLE:
            vehicle = new CStatic(this, idc, cls);
            return vehicle;
        case IDC_IGUI_SPEED:
            speed = new CStatic(this, idc, cls);
            return speed;
        case IDC_IGUI_ALT:
            alt = new CStatic(this, idc, cls);
            return alt;
        case IDC_IGUI_VALUE_ARMOR:
            valueArmor = new CProgressBar(this, idc, cls);
            return valueArmor;
        case IDC_IGUI_VALUE_FUEL:
            valueFuel = new CProgressBar(this, idc, cls);
            return valueFuel;
        case IDC_IGUI_CARGO_MAN:
            cargoMan = new CStatic(this, idc, cls);
            return cargoMan;
        case IDC_IGUI_CARGO_FUEL:
            cargoFuel = new CStatic(this, idc, cls);
            return cargoFuel;
        case IDC_IGUI_CARGO_REPAIR:
            cargoRepair = new CStatic(this, idc, cls);
            return cargoRepair;
        case IDC_IGUI_CARGO_AMMO:
            cargoAmmo = new CStatic(this, idc, cls);
            return cargoAmmo;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

// Hints

DisplayHint::DisplayHint(ControlsContainer* parent) : Display(parent)
{
    SetCursor(nullptr);
    Load(Res >> "RscInGameUI" >> "RscHint");
    if (!_background || !_hint)
    {
        LOG_WARN(UI, "DisplayHint: missing RscHint controls in RscInGameUI");
    }
}

Control* DisplayHint::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_IGHINT_BG:
            _background = new CStatic(this, idc, cls);
            return _background;
        case IDC_IGHINT_HINT:
            _hint = new CStatic(this, idc, cls);
            _hint->EnableCtrl(false);
            return _hint;
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayHint::SetHint(RString hint)
{
    if (!_hint || !_background)
        return;

    _hint->SetText(hint);
    float h = _hint->GetTextHeight();
    float dh = _background->H() - _hint->H();
    _hint->SetPos(_hint->X(), _hint->Y(), _hint->W(), h);
    _background->SetPos(_background->X(), _background->Y(), _background->W(), h + dh);
}

void DisplayHint::SetPosition(float top)
{
    if (!_hint || !_background)
        return;

    float dy = _hint->Y() - _background->Y();
    _background->SetPos(_background->X(), top, _background->W(), _background->H());
    _hint->SetPos(_hint->X(), top + dy, _hint->W(), _hint->H());
}

MenuItem::MenuItem(RString text, int key, RString ch, int cmd, UIActionType action, EntityAI* target, int param,
                   int param2, RString param3)
    : _text(text), _baseText(text), _key(key), _char(ch), _cmd(cmd), _action(action), _target(target), _param(param),
      _param2(param2), _param3(param3), _enable(true), _visible(true), _check(false)
{
}

MenuItem::MenuItem(RString text, int key, RString ch, Menu* submenu, int cmd)
    : _text(text), _baseText(text), _key(key), _char(ch), _cmd(cmd), _action(ATNone), _target(nullptr), _param(0),
      _param2(0), _param3(""), _submenu(submenu), _enable(true), _visible(true), _check(false)
{
}

Menu::Menu()
{
    _parent = nullptr;
    _enable = true;
    _visible = true;
    _minCmd = INT_MAX, _maxCmd = 0; // for faster rejection of EnableCommand/ShowCommand
    _atomic = false;                // submenu may be enabled/disabled only as whole
}
Menu::Menu(const char* text, Menu* parent)
{
    _text = text;
    _parent = parent;
    _enable = true;
    _visible = true;
    _minCmd = INT_MAX, _maxCmd = 0; // for faster rejection of EnableCommand/ShowCommand
    _atomic = false;                // submenu may be enabled/disabled only as whole
}

void Menu::Load(const ParamEntry* cls)
{
    _text = RString((*cls) >> "title");
    _atomic = (*cls) >> "atomic";
    for (int i = 0; i < ((*cls) >> "items").GetSize(); i++)
    {
        const ParamEntry* clsItem = &((*cls) >> RString(((*cls) >> "items")[i]));
        RString itemTitle = (*clsItem) >> "title";
        int itemKey = TranslateLegacyMenuKey(((*clsItem) >> "key").GetInt());
        RString itemChar = (*clsItem) >> "character";
        int itemCmd = ((*clsItem) >> "command").GetInt();
        if (clsItem->FindEntry("menu"))
        {
            Menu* submenu = new Menu();
            submenu->_parent = this;
            submenu->Load(&(Res >> RString((*clsItem) >> "menu")));

            if (!submenu->_atomic)
            {
                saturateMax(_maxCmd, submenu->_maxCmd);
                saturateMin(_minCmd, submenu->_minCmd);
            }

            _items.Add(new MenuItem(itemTitle, itemKey, itemChar, submenu, itemCmd));
        }
        else
        {
            _items.Add(new MenuItem(itemTitle, itemKey, itemChar, itemCmd));
        }
        if (itemCmd >= 0)
        {
            saturateMax(_maxCmd, itemCmd);
            saturateMin(_minCmd, itemCmd);
        }
    }
}

bool Menu::CanBeInMenu(int cmd) const
{
    return cmd >= _minCmd && cmd <= _maxCmd;
}

void Menu::NotifySubmenuCommandAdded(int cmd)
{
    saturateMax(_maxCmd, cmd);
    saturateMin(_minCmd, cmd);
    // note: change needs to be propagated to all upper level menus
    if (!_atomic && _parent)
    {
        _parent->NotifySubmenuCommandAdded(cmd);
    }
}

void Menu::NotifySubmenuCommandRemoved(int cmd)
{
    if (!_atomic && _parent)
    {
        _parent->NotifySubmenuCommandRemoved(cmd);
    }
    if (cmd == _maxCmd || cmd == _minCmd)
    {
        // range may be descreased - something happening on the edge
        RescanMinMax();
    }
}

void Menu::RescanParents()
{
    Menu* parent = _parent;
    while (parent)
    {
        parent->RescanMinMax();
        if (parent->_atomic)
        {
            break;
        }
        parent = parent->_parent;
    }
}

void Menu::RescanChildren()
{
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_submenu)
        {
            item->_submenu->RescanChildren();
            item->_submenu->RescanMinMax();
        }
    }
}

void Menu::RescanMinMax()
{
    // scan menu (and optionally all submenus)
    int oldMin = _minCmd, oldMax = _maxCmd;
    _minCmd = INT_MAX, _maxCmd = INT_MIN;
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_cmd >= 0)
        {
            saturateMax(_maxCmd, item->_cmd);
            saturateMin(_minCmd, item->_cmd);
        }
        if (!_atomic && item->_submenu)
        {
            saturateMax(_maxCmd, item->_submenu->_maxCmd);
            saturateMin(_minCmd, item->_submenu->_minCmd);
        }
    }
    if (_minCmd != oldMin || _maxCmd != oldMax)
    {
        // change - force all parents to update
        RescanParents();
    }
}

void Menu::AddItem(MenuItem* item)
{
    if (item->_cmd >= 0)
    {
        NotifySubmenuCommandAdded(item->_cmd);
    }
    _items.Add(item);
}

bool Menu::EnableCommand(int cmd, bool enable)
{
    if (!CanBeInMenu(cmd))
    {
        return false;
    }
    if (_atomic)
    {
        return false;
    }
    bool retValue = false;
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_cmd == cmd)
        {
            item->_enable = enable;
            retValue = true;
        }
        else if (item->_submenu)
        {
            if (item->_submenu->EnableCommand(cmd, enable))
            {
                bool ok = false;
                for (int j = 0; j < item->_submenu->_items.Size(); j++)
                {
                    MenuItem* subItem = item->_submenu->_items[j];
                    int cmd = subItem->_cmd;
                    if (cmd == CMD_BACK || cmd == CMD_SEPARATOR || cmd == CMD_HIDE_MENU)
                    {
                        continue;
                    }
                    if (subItem->_enable)
                    {
                        ok = true;
                        break;
                    }
                }
                item->_enable = ok;
                item->_submenu->_enable = ok;
                retValue = true;
            }
        }
    }
    return retValue;
}

bool Menu::ShowCommand(int cmd, bool show)
{
    if (!CanBeInMenu(cmd))
    {
        return false;
    }
    if (_atomic)
    {
        return false;
    }
    bool ret = false;
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_cmd == cmd)
        {
            item->_visible = show;
            ret = true;
            break;
        }
        else if (item->_submenu)
        {
            if (item->_submenu->ShowCommand(cmd, show))
            {
                bool ok = false;
                for (int j = 0; j < item->_submenu->_items.Size(); j++)
                {
                    MenuItem* subItem = item->_submenu->_items[j];
                    if (subItem->_visible)
                    {
                        int cmd = subItem->_cmd;
                        if (cmd != CMD_SEPARATOR && cmd != CMD_BACK && cmd != CMD_HIDE_MENU)
                        {
                            ok = true;
                            break;
                        }
                    }
                }
                item->_visible = ok;
                item->_submenu->_visible = ok;
                ret = true;
                break;
            }
        }
    }

    return ret;
}

bool Menu::ShowAndEnableCommand(int cmd, bool show, bool enable)
{
    if (!CanBeInMenu(cmd))
    {
        return false;
    }
    if (_atomic)
    {
        return false;
    }
    bool ret = false;
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_cmd == cmd)
        {
            item->_visible = show;
            item->_enable = enable;
            ret = true;
            break;
        }
        else if (item->_submenu)
        {
            if (item->_submenu->ShowAndEnableCommand(cmd, show, enable))
            {
                bool okShow = false;
                bool okEnable = false;
                for (int j = 0; j < item->_submenu->_items.Size(); j++)
                {
                    MenuItem* subItem = item->_submenu->_items[j];
                    int cmd = subItem->_cmd;
                    if (cmd != CMD_SEPARATOR && cmd != CMD_BACK && cmd != CMD_HIDE_MENU)
                    {
                        if (subItem->_visible)
                        {
                            okShow = true;
                        }
                        if (subItem->_enable)
                        {
                            okEnable = true;
                        }
                    }
                }
                item->_visible = okShow;
                item->_enable = okEnable;
                item->_submenu->_visible = okShow;
                item->_submenu->_enable = okEnable;
                ret = true;
                break;
            }
        }
    }
    return ret;
}

Menu* Menu::FindMenu(int cmd, bool alsoInAtomic)
{
    if (!alsoInAtomic)
    {
        if (!CanBeInMenu(cmd))
        {
            return nullptr;
        }
        if (_atomic)
        {
            return nullptr;
        }
    }
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_submenu)
        {
            Menu* found = item->_submenu->FindMenu(cmd, alsoInAtomic);
            if (found != nullptr)
            {
                return found;
            }
        }
        else
        {
            if (item->_cmd == cmd)
            {
                return this;
            }
        }
    }
    return nullptr;
}

MenuItem* Menu::Find(int cmd, bool alsoInAtomic)
{
    if (!alsoInAtomic)
    {
        if (!CanBeInMenu(cmd))
        {
            return nullptr;
        }
        if (_atomic)
        {
            return nullptr;
        }
    }
    for (int i = 0; i < _items.Size(); i++)
    {
        MenuItem* item = _items[i];
        if (item->_cmd == cmd)
        {
            return item;
        }
        if (item->_submenu)
        {
            MenuItem* found = item->_submenu->Find(cmd, alsoInAtomic);
            if (found != nullptr)
            {
                return found;
            }
        }
    }
    return nullptr;
}

bool Menu::SetText(int cmd, RString text)
{
    MenuItem* item = Find(cmd);
    if (item == nullptr)
    {
        return false;
    }
    item->_text = text;
    return true;
}

bool Menu::ResetText(int cmd)
{
    MenuItem* item = Find(cmd);
    if (item == nullptr)
    {
        return false;
    }
    item->_text = item->_baseText;
    return true;
}

bool Menu::CheckCommand(int cmd, bool check)
{
    MenuItem* item = Find(cmd);
    if (item == nullptr)
    {
        return false;
    }
    item->_check = check;
    return true;
}

InGameUI::InGameUI() : _mode(UIFire), _modeAuto(UIFire), _groundPointValid(false)
{
#if _ENABLE_CHEATS
    _showAll = false;
#endif
    _cursorWorld = false;
    _worldCursor = VForward; // world cursor direction
    _modelCursor = VForward;
    _worldCursorTime = Glob.uiTime - 120;

    _lastMeTime = Glob.uiTime;
    _lastCmdId = -1;
    _lastCmdTime = Glob.uiTime - 120;
    _lastTargetTime = Glob.uiTime - 120;
    _lastGroupDirTime = Glob.uiTime - 120;
    _lastFormTime = Glob.uiTime - 120;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        _lastSelTime[i] = Glob.uiTime - 120;
    }
    _lastUnitInfoTime = Glob.uiTime;
    _lastMenuTime = Glob.uiTime;
    _timeToPlay = Glob.uiTime + GRandGen.PlusMinus(60, 30);

    _target = nullptr;
    _lockTarget = nullptr;
    _wantLock = false; // users wants to lock enemy target

    _lockAimValidUntil = Glob.uiTime - 60;
    //	_curWeapon = 0;

    _blinkState = false;
    _blinkStateChange = UITime(0);

    _dragging = false;
    _mouseDown = false;

    //	_timeSendLoad = UITIME_MAX;

    Init();

    _lastUnitInfoType = NUnitInfoType;

    _unitInfo = new DisplayUnitInfo(nullptr);

    _hint = new DisplayHint(nullptr);
    _hint->SetHint("");
    _hintTime = Glob.uiTime;

    _tmPos = 1;
    _tmTime = UITime(0);
    _tmIn = false;
    _tmOut = false;

    _tankPos = 1;
    _tankTime = UITime(0);
    _tankIn = false;
    _tankOut = false;

    _font24 = GLOB_ENGINE->LoadFont(GetFontID(FontS));
    _font36 = GLOB_ENGINE->LoadFont(GetFontID(FontM));

    _leftPressed = false;
    _rightPressed = false;

    InitMenu();

    tdName = "";
    giName = "";
    piName = "";
    uiName = "";

    const ParamEntry* mainCfg = &(Pars >> "CfgInGameUI");
    bgColor = GetPackedColor((*mainCfg) >> "colorBackground");
    bgColorCmd = GetPackedColor((*mainCfg) >> "colorBackgroundCommand");
    bgColorHelp = GetPackedColor((*mainCfg) >> "colorBackgroundHelp");
    ftColor = GetPackedColor((*mainCfg) >> "colorText");
    _imageBar = GLOB_ENGINE->TextBank()->Load(GetPictureName((*mainCfg) >> "Bar" >> "imageBar"));

    const ParamEntry* cfg = &((*mainCfg) >> "Picture");
    pictureColor = GetPackedColor((*cfg) >> "color");
    pictureProblemsColor = GetPackedColor((*cfg) >> "colorProblems");

    cfg = &((*mainCfg) >> "Capture");
    capBgColor = GetPackedColor((*cfg) >> "colorBackground");
    capFtColor = GetPackedColor((*cfg) >> "colorText");
    capLnColor = GetPackedColor((*cfg) >> "colorLine");

    cfg = &((*mainCfg) >> "Menu");
    tmX = (*cfg) >> "left";
    tmY = (*cfg) >> "top";
    tmW = (*cfg) >> "width";
    tmH = (*cfg) >> "height";
    menuCheckedColor = GetPackedColor((*cfg) >> "colorChecked");
    menuEnabledColor = GetPackedColor((*cfg) >> "colorEnabled");
    menuDisabledColor = GetPackedColor((*cfg) >> "colorDisabled");
    menuHideTime = (*cfg) >> "hideTime";

    cfg = &((*mainCfg) >> "Messages");
    msg1Color = GetPackedColor((*cfg) >> "color1");
    msg2Color = GetPackedColor((*cfg) >> "color2");
    msg3Color = GetPackedColor((*cfg) >> "color3");

    cfg = &((*mainCfg) >> "TacticalDisplay");
    tdX = (*cfg) >> "left";
    tdY = (*cfg) >> "top";
    tdW = (*cfg) >> "width";
    tdH = (*cfg) >> "height";
    friendlyColor = GetPackedColor((*cfg) >> "colorFriendly");
    enemyColor = GetPackedColor((*cfg) >> "colorEnemy");
    neutralColor = GetPackedColor((*cfg) >> "colorNeutral");
    civilianColor = GetPackedColor((*cfg) >> "colorCivilian");
    unknownColor = GetPackedColor((*cfg) >> "colorUnknown");
    cameraColor = GetPackedColor((*cfg) >> "colorCamera");
    cfg = &((*cfg) >> "Cursor");
    tdCurW = (*cfg) >> "width";
    tdCurH = (*cfg) >> "height";
    tdCursorColor = GetPackedColor((*cfg) >> "color");

    cfg = &((*mainCfg) >> "TankDirection");

    tankX = (*cfg) >> "left";
    tankY = (*cfg) >> "top";
    tankW = (*cfg) >> "width";
    tankH = (*cfg) >> "height";

    tankColor = GetPackedColor((*cfg) >> "color");
    tankColorFullDammage = GetPackedColor((*cfg) >> "colorFullDammage");
    tankColorHalfDammage = GetPackedColor((*cfg) >> "colorHalfDammage");

    _imageTurret = GlobLoadTexture(GetPictureName((*cfg) >> "imageTurret"));
    _imageGun = GlobLoadTexture(GetPictureName((*cfg) >> "imageGun"));
    _imageObsTurret = GlobLoadTexture(GetPictureName((*cfg) >> "imageObsTurret"));

    _imageHull = GlobLoadTexture(GetPictureName((*cfg) >> "imageHull"));
    _imageEngine = GlobLoadTexture(GetPictureName((*cfg) >> "imageEngine"));

    _imageLTrack = GlobLoadTexture(GetPictureName((*cfg) >> "imageLTrack"));
    _imageRTrack = GlobLoadTexture(GetPictureName((*cfg) >> "imageRTrack"));

    cfg = &((*mainCfg) >> "GroupDir");
    gdX = (*cfg) >> "left";
    gdY = (*cfg) >> "top";
    gdW = (*cfg) >> "width";
    gdH = (*cfg) >> "height";
    groupDirDimStartTime = (*cfg) >> "dimmStartTime";
    groupDirDimEndTime = (*cfg) >> "dimmEndTime";
    _imageGroupDir = GlobLoadTexture(GetPictureName((*cfg) >> "image"));

    cfg = &((*mainCfg) >> "Compass");
    coX = (*cfg) >> "left";
    coY = (*cfg) >> "top";
    coW = (*cfg) >> "width";
    coH = (*cfg) >> "height";
    compassColor = GetPackedColor((*cfg) >> "color");

    compassDirColor = GetPackedColor((*cfg) >> "dirColor");
    compassTurretDirColor = GetPackedColor((*cfg) >> "turretDirColor");

    cfg = &((*mainCfg) >> "GameInfo");
    giX = (*cfg) >> "left";
    giY = (*cfg) >> "top";
    giW = (*cfg) >> "width";
    giH = (*cfg) >> "height";

    cfg = &((*mainCfg) >> "PlayerInfo");
    timeColor = GetPackedColor((*cfg) >> "colorTime");
    piX = (*cfg) >> "left";
    piY = (*cfg) >> "top";
    piW = (*cfg) >> "width";
    piH = (*cfg) >> "height";
    piDimStartTime = (*cfg) >> "dimmStartTime";
    piDimEndTime = (*cfg) >> "dimmEndTime";
    abarW = (*cfg) >> "ArmorBar" >> "width";
    fbarW = (*cfg) >> "FuelBar" >> "width";
    hbarW = (*cfg) >> "HealthBar" >> "width";
    ebarW = (*cfg) >> "ExperienceBar" >> "width";
    ebarColor = GetPackedColor((*cfg) >> "ExperienceBar" >> "color");
    ppicW = (*cfg) >> "UnitPicture" >> "width";
    ppicH = (*cfg) >> "UnitPicture" >> "height";
    piSideH = (*cfg) >> "Side" >> "height";
    piSideW = (*cfg) >> "Side" >> "width";
    cfg = &((*cfg) >> "Sign");
    piSignH = (*cfg) >> "height";
    piSignSW = (*cfg) >> "widthSector";
    piSignGW = (*cfg) >> "widthGroup";
    piSignUW = (*cfg) >> "widthUnit";

    cfg = &((*mainCfg) >> "GroupInfo");
    uiX = (*cfg) >> "left";
    uiY = (*cfg) >> "top";
    uiW = (*cfg) >> "width";
    uiH = (*cfg) >> "height";
    groupInfoDim = (*cfg) >> "dimm";
    uiColorNone = GetPackedColor((*cfg) >> "colorIDNone");
    uiColorNormal = GetPackedColor((*cfg) >> "colorIDNormal");
    uiColorSelected = GetPackedColor((*cfg) >> "colorIDSelected");
    uiColorPlayer = GetPackedColor((*cfg) >> "colorIDPlayer");
    _imageDefaultWeapons = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "imageDefaultWeapons"));
    _imageNoWeapons = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "imageNoWeapons"));
    cfg = &((*cfg) >> "Semaphore");
    semW = (*cfg) >> "width";
    semH = (*cfg) >> "height";
    holdFireColor = GetPackedColor((*cfg) >> "colorHoldFire");
    _imageSemaphore = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "imageSemaphore"));

    cfg = &((*mainCfg) >> "Cursor");
    actW = (*cfg) >> "activeWidth";
    actH = (*cfg) >> "activeHeight";
    actMin = (*cfg) >> "activeMinimum";
    actMax = (*cfg) >> "activeMaximum";
    cursorColor = GetPackedColor((*cfg) >> "color");
    cursorBgColor = GetPackedColor((*cfg) >> "colorBackground");
    cursorDim = (*cfg) >> "dimm";
    cursorLockColor = GetPackedColor((*cfg) >> "colorLocked");
    enemyActColor = GetPackedColor((*cfg) >> "enemyActiveColor");
    _iconMe = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "me"));
    _iconSelect = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "select"));
    _iconLeader = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "leader"));
    _iconMission = GLOB_ENGINE->TextBank()->Load(GetPictureName((*cfg) >> "mission"));

    meColor = GetPackedColor((*cfg) >> "meColor");
    selectColor = GetPackedColor((*cfg) >> "selectColor");
    leaderColor = GetPackedColor((*cfg) >> "leaderColor");
    missionColor = GetPackedColor((*cfg) >> "missionColor");

    meDim = (*cfg) >> "dimmMe";
    meDimStartTime = (*cfg) >> "dimmMeStartTime";
    meDimEndTime = (*cfg) >> "dimmMeEndTime";
    cmdDimStartTime = (*cfg) >> "dimmCmdStartTime";
    cmdDimEndTime = (*cfg) >> "dimmCmdEndTime";

    formDimStartTime = (*cfg) >> "dimmCmdStartTime";
    formDimEndTime = (*cfg) >> "dimmCmdEndTime";
    targetDimStartTime = (*cfg) >> "dimmCmdStartTime";
    targetDimEndTime = (*cfg) >> "dimmCmdEndTime";

    cfg = &((*cfg) >> "Sign");
    curSignH = (*cfg) >> "height";
    curSignSW = (*cfg) >> "widthSector";
    curSignGW = (*cfg) >> "widthGroup";
    curSignUW = (*cfg) >> "widthUnit";

    cfg = &((*mainCfg) >> "Bar");
    barBgColor = GetPackedColor((*cfg) >> "colorBackground");
    barGreenColor = GetPackedColor((*cfg) >> "colorGreen");
    barYellowColor = GetPackedColor((*cfg) >> "colorYellow");
    barRedColor = GetPackedColor((*cfg) >> "colorRed");
    barBlinkOnColor = GetPackedColor((*cfg) >> "colorBlinkOn");
    barBlinkOffColor = GetPackedColor((*cfg) >> "colorBlinkOff");
    barH = (*cfg) >> "height";

    teamColors[TeamMain] = PackedColor(Color(1, 1, 1, 1));
    teamColors[TeamRed] = PackedColor(Color(1, 0, 0, 1));
    teamColors[TeamGreen] = PackedColor(Color(0, 1, 0, 1));
    teamColors[TeamBlue] = PackedColor(Color(0, 0, 1, 1));
    teamColors[TeamYellow] = PackedColor(Color(0.8, 0.8, 0, 1));

    dragColor = PackedColor(Color(0, 1, 0, 1));

    cfg = &((*mainCfg) >> "Hint");
    hintDimStartTime = (*cfg) >> "dimmStartTime";
    hintDimEndTime = (*cfg) >> "dimmEndTime";
    GetValue(_hintSound, (*cfg) >> "sound");

    cfg = &((*mainCfg) >> "ConnectionLost");

    _clX = (*cfg) >> "left";
    _clY = (*cfg) >> "top";
    _clW = (*cfg) >> "width";
    _clH = (*cfg) >> "height";
    _clFont = GEngine->LoadFont(GetFontID((*cfg) >> "font"));
    _clSize = (*cfg) >> "size";
    _clColor = GetPackedColor((*cfg) >> "color");
}

InGameUI::~InGameUI()
{
    _visibleListTemp.Clear();
    //	_visibleListS.Clear();
}

AbstractUI* CreateInGameUI()
{
    return new InGameUI;
}

void AbstractUI::ShowAll(bool show)
{
    _showUnitInfo = show;
    _showTacticalDisplay = show;
    _showCompass = show;
    _showMenu = show;
    _showTankDirection = show;
    _showGroupInfo = show;
}

void InGameUI::Init()
{
    ShowAll();
    ShowCursors();
}

int ValidateWeapon(EntityAI* vehicle, int weapon)
{
    int n = vehicle->NMagazineSlots();
    if (weapon < 0 || weapon >= n)
    {
        return -1;
    }

    return weapon;
}

void InGameUI::ResetVehicle(EntityAI* vehicle)
{
    int weapon = vehicle->FirstWeapon();
    weapon = ValidateWeapon(vehicle, weapon);
    // if the weapon is not weapon, but rather special item, do not select it
    if (weapon >= 0)
    {
        const MagazineSlot& slot = vehicle->GetMagazineSlot(weapon);
        const WeaponType* type = slot._weapon;
        if (!(type->_weaponType & MaskSlotPrimary) && (type->_weaponType & (MaskSlotBinocular | MaskSlotSecondary)))
        {
            // do not autoselect binocular or secondary weapon
            weapon = -1;
        }
    }
    if (vehicle->QIsManual())
    {
        vehicle->SelectWeapon(weapon, true);
    }

    if (vehicle == _target.IdExact())
    {
        _target = nullptr;
    }
    if (vehicle == _lockTarget.IdExact())
    {
        _lockTarget = nullptr, _lockAimValidUntil = Glob.uiTime - 60;
    }

    _cursorWorld = false;
    _modelCursor = VForward;
    // commander should always use eye direction

    AIUnit* unit = GWorld->FocusOn();
    bool isObserver = unit && unit == vehicle->ObserverUnit();
    bool isGunner = unit && unit == vehicle->GunnerUnit();

    if (isObserver)
    {
        _worldCursor = vehicle->GetEyeDirection();
    }
    else if (weapon >= 0 && isGunner)
    {
        _worldCursor = vehicle->GetWeaponDirection(0);
    }
    else
    {
        _worldCursor = vehicle->Direction();
    }
    SetCursorDirection(_worldCursor);

    //	_timeSendLoad = UITIME_MAX;

    _lastCmdId = -1;
}

void InGameUI::OnWeaponRemoved(int slot)
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    SelectWeapon(unit, unit->GetVehicle()->FirstWeapon());
}

void InGameUI::ResetHUD()
{
    _target = nullptr;
    _lockTarget = nullptr;

    _lockAimValidUntil = Glob.uiTime - 60;

    //	_curWeapon = -1;

    _modeAuto = _mode = UIFire;
    _groundPointValid = false;

    ClearSelectedUnits();
    ClearTeams();

    _menuType = MTNone;
    _menuCurrent = _menuMain;

    _dragging = false;
    _mouseDown = false;

    _visibleListTemp.Clear();

    //	_timeSendLoad = UITIME_MAX;

    _hint->SetHint("");

    _lastCmdId = -1;
}

DEFINE_ENUM_BEG(VCommand)
VCFire, VCMove, VCCancelFire,
    VCCancelMove DEFINE_ENUM_END(VCommand)

        void InGameUI::IssueVCommand(EntityAI* vehicle, VCommand cmd)
{
    AIUnit* unit = GWorld->FocusOn();
    Transport* transport = dyn_cast<Transport>(vehicle);
    if (!transport)
    {
        return;
    }
    if (transport->CommanderUnit() != unit)
    {
        return;
    }
    // unit is vehicle commander
    // issue command to vehicle driver or gunner

    switch (cmd)
    {
        case VCMove:
            if (_groundPointValid)
            {
                if (transport->IsLocal())
                {
                    transport->SendMove(_groundPoint);
                }
                else
                {
                    RadioMessageVMove msg(transport, _groundPoint);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
            break;
        case VCFire:
            if (transport->IsLocal())
            {
                transport->SendFire(_target);
            }
            else
            {
                RadioMessageVFire msg(transport, _target);
                GetNetworkManager().SendRadioMessage(&msg);
            }
            break;
        case VCCancelMove:
            if (transport->IsLocal())
            {
                transport->SendJoin();
            }
            else
            {
                RadioMessageVFormation msg(transport);
                GetNetworkManager().SendRadioMessage(&msg);
            }
            break;
        case VCCancelFire:
            if (transport->IsLocal())
            {
                transport->SendSimpleCommand(SCCeaseFire);
            }
            else
            {
                RadioMessageVSimpleCommand msg(transport, SCCeaseFire);
                GetNetworkManager().SendRadioMessage(&msg);
            }
            break;
    }
}

void InGameUI::IssueCommand(EntityAI* vehicle, Command::Message cmd, bool follow)
{
    AIUnit* unit = GWorld->FocusOn();

    // AIUnit *unit = vehicle->Brain();
    if (!unit)
    {
        return;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }

    if (cmd == Command::NoCommand)
    { // auto command - command type is selected by context
        switch (_modeAuto)
        {
            case UIStrategySelect:
            {
                if (!InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) &&
                    !InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
                {
                    ClearSelectedUnits();
                }

                EntityAI* vTarget = _target.IdExact();
                PoseidonAssert(vTarget);
                if (!vTarget)
                {
                    return;
                }
                AIUnit* u = vTarget->CommanderUnit();
                if (u)
                {
                    if (u->GetVehicle() != vehicle)
                    {
                        int index = u->ID() - 1;
                        SetSelectedUnit(index, u);
                        _lastSelTime[index] = Glob.uiTime;
                    }
                }
                return;
            }
            case UIStrategyMove:
                cmd = Command::Move;
                break;
            case UIStrategyAttack:
                cmd = Command::AttackAndFire;
                break;
            case UIStrategyGetIn:
                cmd = Command::GetIn;
                break;
            case UIStrategyWatch:
            {
                if (_target.IdExact())
                {
                    grp->SendState(new RadioMessageWatchTgt(grp, ListSelectedUnits(), _target));
                    ClearSelectedUnits();
                }
                else
                {
                    grp->SendState(new RadioMessageWatchPos(grp, ListSelectedUnits(), _groundPoint));
                    ClearSelectedUnits();
                }
                return;
            }

            default:
                return;
        }
    }

    if (cmd <= 0)
    {
        return;
    }

    vehicle = unit->GetVehicle();
    // count selected units
    if (IsEmptySelectedUnits())
    {
        // try to issue command to my vehicle
        Transport* transport = dyn_cast<Transport>(vehicle);
        if (!transport)
        {
            return;
        }
        if (transport->CommanderUnit() != unit)
        {
            return;
        }
        // unit is vehicle commander
        // issue command to vehicle driver or gunner

        switch (cmd)
        {
            case Command::Move:
                IssueVCommand(transport, VCMove);
                // if ( _groundPointValid) transport->SendMove(_groundPoint);
                break;
            case Command::Attack:
#if ENABLE_HOLDFIRE_FIX
            case Command::AttackAndFire:
#endif
                // transport->SendFire(_target.idExact);
                break;
            case Command::Join:
                IssueVCommand(transport, VCCancelMove);
                break;
        }
        return;
    }

    // create command
    Command command;
    command._message = cmd;
    command._context = Command::CtxUI;
    switch (cmd)
    { // add command type specific parameters
        case Command::Stop:
            // no destination
            break;
        case Command::Move:
            if (_target.IdExact())
            {
                // Move into house
                command._target = _target.IdExact();
                command._param = _housePos;
                const IPaths* house = command._target->GetIPaths();
                if (house && _housePos >= 0 && _housePos < house->NPos())
                {
                    command._destination = house->GetPosition(house->GetPos(_housePos));
                }
                else
                {
                    command._destination = command._target->Position();
                }
            }
            else
            {
                if (!_groundPointValid)
                {
                    return;
                }
                command._destination = _groundPoint;
            }
            break;
        case Command::Heal:
            if (!grp->FindHealPosition(command))
            {
                return;
            }
            goto JoinAfterCommand;
        case Command::Repair:
            if (!grp->FindRepairPosition(command))
            {
                return;
            }
            goto JoinAfterCommand;
        case Command::Refuel:
            if (!grp->FindRefuelPosition(command))
            {
                return;
            }
            goto JoinAfterCommand;
        case Command::Rearm:
        {
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* unit = GetSelectedUnit(i);
                if (!unit)
                {
                    continue;
                }
                if (unit->IsSoldier())
                {
                    const AITargetInfo* target = unit->CheckAmmo(AIUnit::RSCritical);
                    if (target)
                    {
                        Command cmd;
                        cmd._message = Command::Rearm;
                        cmd._destination = target->_realPos;
                        cmd._target = target->_idExact;
                        if (unit->GetSubgroup() == grp->MainSubgroup())
                        {
                            cmd._context = Command::CtxUIWithJoin;
                            cmd._joinToSubgroup = grp->MainSubgroup();
                        }
                        else
                        {
                            cmd._context = Command::CtxUI;
                        }
                        PackedBoolArray list;
                        list.Set(unit->ID() - 1, true);
                        grp->SendCommand(cmd, list);
                    }
                    SetSelectedUnit(i, nullptr);
                }
            }
            if (IsEmptySelectedUnits())
            {
                return;
            }
            if (!grp->FindRearmPosition(command))
            {
                return;
            }
            goto JoinAfterCommand;
            goto JoinAfterCommand;
        }
        case Command::Join:
            command._joinToSubgroup = grp->MainSubgroup();
            break;
        case Command::Attack:
#if ENABLE_HOLDFIRE_FIX
        case Command::AttackAndFire:
#endif
        {
            // command destination is not used
            // we give commander's destination so that is contains something
            command._destination = vehicle->Position();
            command._targetE = _target;
            if (!command._targetE)
            {
                command._targetE = _lockTarget;
            }
            if (!command._targetE)
            {
                return;
            }
            goto JoinAfterCommand;
        }
        case Command::GetOut:
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* unit = GetSelectedUnit(i);
                if (!unit)
                {
                    continue;
                }
                unit->AllowGetIn(false);
            }
        JoinAfterCommand:
            if (CheckJoin(grp))
            {
                command._context = Command::CtxUIWithJoin;
                command._joinToSubgroup = grp->MainSubgroup();
            }
            break;
        case Command::GetIn:
            if (!_target.IdExact())
            {
                Transport* transport = dyn_cast<Transport>(unit->GetVehicle());
                if (transport && transport->QCanIGetInCargo())
                {
                    command._target = transport;
                }
                else
                {
                    return;
                }
            }
            else
            {
                Transport* transport = dyn_cast<Transport, Object>(_target.IdExact());
                if (transport)
                {
                    command._target = transport;
                }
                else
                {
                    return;
                }
            }

            { // assign vehicle
                Transport* veh = dyn_cast<Transport, EntityAI>(command._target);
                PoseidonAssert(veh);
                bool canAsDriver = veh->GetType()->HasDriver();
                bool canAsCommander = veh->GetType()->HasCommander();
                bool canAsGunner = veh->GetType()->HasGunner();
                AIUnit* driver = veh->GetDriverAssigned();
                if (driver)
                {
                    if (veh->QIsDriverIn())
                    {
                        canAsDriver = false;
                    }
                    else if (driver->GetGroup() && driver->GetGroup()->CommandSent(driver, Command::GetIn))
                    {
                        canAsDriver = false;
                    }
                }
                if (canAsCommander)
                {
                    AIUnit* commander = veh->GetCommanderAssigned();
                    if (commander)
                    {
                        if (veh->QIsCommanderIn())
                        {
                            canAsCommander = false;
                        }
                        else if (commander->GetGroup() && commander->GetGroup()->CommandSent(commander, Command::GetIn))
                        {
                            canAsCommander = false;
                        }
                    }
                }
                if (canAsGunner)
                {
                    AIUnit* gunner = veh->GetGunnerAssigned();
                    if (gunner)
                    {
                        if (veh->QIsGunnerIn())
                        {
                            canAsGunner = false;
                        }
                        else if (gunner->GetGroup() && gunner->GetGroup()->CommandSent(gunner, Command::GetIn))
                        {
                            canAsGunner = false;
                        }
                    }
                }
                for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                {
                    AIUnit* unit = GetSelectedUnit(i);
                    if (!unit)
                    {
                        continue;
                    }
                    if (canAsDriver)
                    {
                        if (veh->GetGroupAssigned() != grp)
                        {
                            grp->AddVehicle(veh);
                        }
                        unit->AssignAsDriver(veh);
                        canAsDriver = false;
                    }
                    else if (canAsGunner)
                    {
                        unit->AssignAsGunner(veh);
                        canAsGunner = false;
                    }
                    else if (canAsCommander)
                    {
                        unit->AssignAsCommander(veh);
                        canAsCommander = false;
                    }
                    else if (unit->VehicleAssigned() != veh)
                    {
                        unit->AssignAsCargo(veh);
                    }
                    unit->AllowGetIn(true);
                }
            }

            break;
    }

    grp->SendCommand(command, ListSelectedUnits());
    ClearSelectedUnits();
}

TargetSide InGameUI::RadarTargetSide(AIUnit* unit, Target& tar)
{
    TargetSide side = tar.idExact->GetType()->GetTypicalSide();
    if (!tar.idExact->EngineIsOn())
    {
        AIGroup* grp = tar.idExact->GetGroup();
        if (grp != unit->GetGroup())
        {
            side = TCivilian; // empty marked as civilian
        }
    }

    if (USER_CONFIG.IsEnabled(DTEnemyTag)
#if _ENABLE_CHEATS
        || _showAll
#endif
    )
    {
        side = tar.side;
        if (tar.destroyed)
        {
            side = TCivilian;
        }
    }
    return side;
}

void InGameUI::FindTarget(EntityAI* me, bool prev)
{
    int weapon = me->SelectedWeapon();
    weapon = ValidateWeapon(me, weapon);
    if (weapon < 0)
    {
        return;
    }
    Vector3 curDir = me->GetWeaponDirection(weapon);
    AIUnit* unit = me->CommanderUnit();
    if (!unit)
    {
        return; // no commander - no TAB switching
    }
    if (unit->IsFreeSoldier())
    {
        return; // soldier - no TAB switching
    }
    if (unit->IsGunner())
    {
        return; // soldier - no TAB switching
    }
    if (_lockTarget)
    {
        curDir = _lockTarget->AimingPosition() - me->Position();
    }
    else if (_lockAimValidUntil >= Glob.uiTime)
    {
        curDir = _lockAim;
    }
    AICenter* myCenter = unit->GetGroup()->GetCenter();
    float curAzimut = atan2(curDir.X(), curDir.Z());
    int i;
    // check radar visible targets
    // lock only enemy or unknown
    const TargetList& visibleList = *VisibleList();

    float minDiffE = 1e10;
    float minDiffU = H_PI / 4;
    int minIE = -1;
    int minIU = -1;

    for (i = 0; i < visibleList.Size(); i++)
    {
        Target& tar = *visibleList[i];
        if (tar.vanished)
        {
            continue;
        }
        if (tar.destroyed)
        {
            continue;
        }
        EntityAI* ai = tar.idExact;
        if (!ai)
        {
            continue;
        }
        if (ai == _lockTarget.IdExact())
        {
            continue; // skip current target
        }
        if (ai == me)
        {
            continue; // skip current target
        }
        if (!ai->GetType()->GetIRTarget() && !ai->GetType()->GetLaserTarget())
        {
            continue; // skip non-IR targets
        }
        if (!me->CanLock(ai))
        {
            continue;
        }
        Vector3Val pos = ai->AimingPosition();
        float dist2 = me->Position().Distance2(pos);
        float visible = me->CalcVisibility(ai, dist2);
        if (visible < 0.01)
        {
            continue;
        }
        TargetSide side = RadarTargetSide(unit, tar);

        Vector3 relPos = pos - me->Position();
        float azimut = atan2(relPos.X(), relPos.Z());
        float diff = AngleDifference(azimut, curAzimut);
        if (prev)
        {
            diff = -diff;
        }
        if (diff <= 0)
        {
            diff += (H_PI * 2);
        }
        if (myCenter->IsEnemy(side))
        {
            if (minDiffE > diff)
            {
                minDiffE = diff, minIE = i;
            }
        }
        else if (side == TSideUnknown)
        {
            if (minDiffU > diff)
            {
                minDiffU = diff, minIU = i;
            }
        }
    }
    if (minIE < 0)
    {
        minIE = minIU;
    }
    if (minIE < 0)
    {
        Matrix3 rotY(MRotationY, prev ? +H_PI / 4 : -H_PI / 4);
        _lockTarget = nullptr;
        _lockAim = rotY * curDir;
        _lockAimValidUntil = Glob.uiTime + 3.0;
        return; // no target
    }
    Target* tgt = visibleList[minIE];
    _lockTarget = tgt;
    // we have to disclose and report target
    RevealTarget(tgt, 0.3);

    _timeSendTarget = Glob.uiTime + 1.0;
    _lockAimValidUntil = Glob.uiTime - 60;
}

#define CameraFrame() GScene->GetCamera()
// #define CameraFrame() GWorld->CameraOn()

void InGameUI::SetCursorMode(bool world)
{
    if (_cursorWorld == world)
    {
        return;
    }
    _cursorWorld = world;
    const FrameBase* cam = CameraFrame();
    PoseidonAssert(cam);
    if (cam)
    {
        if (!_cursorWorld)
        { // convert from world cursor
            Matrix4Val invTransform = cam->GetInvTransform();
            _modelCursor = invTransform.Rotate(_worldCursor);
        }
        else
        { // convert to world cursor
            _worldCursor = cam->DirectionModelToWorld(_modelCursor);
        }
    }
}

bool InGameUI::GetCursorMode() const
{
    return _cursorWorld;
}

Vector3 InGameUI::GetWorldCursor() const
{
    return _worldCursor;
}
void InGameUI::SetWorldCursor(Vector3Par dir)
{
    _worldCursor = dir;
}
Vector3 InGameUI::GetModelCursor() const
{
    return _modelCursor;
}
void InGameUI::SetModelCursor(Vector3Par dir)
{
    _modelCursor = dir;
}

Vector3 InGameUI::GetCursorDirection() const
{
    if (!_cursorWorld)
    {
        const FrameBase* cam = CameraFrame();
        if (cam)
        {
            return cam->DirectionModelToWorld(_modelCursor);
        }
    }
    return _worldCursor;
}

void InGameUI::SetCursorDirection(Vector3Par dir)
{
    if (!_cursorWorld)
    {
        const FrameBase* cam = CameraFrame();
        if (cam)
        {
            Matrix4Val invTransform = cam->GetInvTransform();
            //_modelCursor=invTransform.Rotate(_worldCursor);
            _modelCursor = invTransform.Rotate(dir);
            return;
        }
        return;
    }
    _worldCursor = dir;
}

bool InGameUI::CheckJoin(AIGroup* grp)
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GetSelectedUnit(i);
        if (!u)
        {
            continue;
        }
        if (u->GetSubgroup() == grp->MainSubgroup())
        {
            return true;
        }
        //		if (grp->CommandSent(u, Command::Join)) return true;
    }
    return false;
}

void InGameUI::BackupTargets()
{
    _visibleListTemp.Clear();
    const TargetList& visibleList = *VisibleList();
    for (int i = 0; i < visibleList.Size(); i++)
    {
        Target* target = visibleList[i];
        if (target)
        {
            _visibleListTemp.Add(target);
        }
    }
}

#define COMMAND_TIMEOUT 480.0 // 8 min

void InGameUI::IssueAction(AIGroup* grp, MenuItem& item)
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = GetSelectedUnit(i);
        if (unit)
        {
            if (item._target && item._target != unit->GetVehicle())
            {
                // send autocommand
                Command cmd;
                cmd._message = Command::Action;
                cmd._action = item._action;
                cmd._target = item._target;
                cmd._destination = item._target->Position();
                cmd._param = item._param;
                cmd._param2 = item._param2;
                cmd._param3 = item._param3;
                cmd._time = Glob.time + COMMAND_TIMEOUT;
                unit->GetGroup()->SendAutoCommandToUnit(cmd, unit, true);
            }
            else
            {
                // process immediatelly
                UIAction action;
                action.type = item._action;
                action.target = unit->GetVehicle();
                action.param = item._param;
                action.param2 = item._param2;
                action.param3 = item._param3;
                action.priority = 0;
                action.showWindow = false;
                action.hideOnUse = false;
                unit->GetVehicle()->StartActionProcessing(action, unit);
            }
        }
    }

    ClearSelectedUnits();
}

void InGameUI::IssueMove(AIGroup* grp, int where)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }

    AIUnit* unit = nullptr;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GetSelectedUnit(i);
        if (u)
        {
            unit = u;
            break;
        }
    }
    if (!unit)
    {
        return;
    }

    int dir = where / N_MOVE_DIST;
    int dist = where - dir * N_MOVE_DIST;
    float angle = (2 * H_PI / N_MOVE_DIR) * dir;
    float distance = 0;
    switch (dist)
    {
        case 0:
            distance = 50;
            break;
        case 1:
            distance = 100;
            break;
        case 2:
            distance = 200;
            break;
        case 3:
            distance = 500;
            break;
        case 4:
            distance = 1000;
            break;
        case 5:
            distance = 2000;
            break;
    }
    Matrix3 rotY(MRotationY, angle);
    Vector3 move = Vector3(0, 0, distance) * rotY;

    Command cmd;
    cmd._message = Command::Move;
    cmd._context = Command::CtxUI;
    cmd._destination = unit->Position() + move;
    grp->SendCommand(cmd, ListSelectedUnits());
    ClearSelectedUnits();
}

void InGameUI::IssueVMove(Transport* vehicle, int where)
{
    if (!vehicle)
    {
        return;
    }

    int dir = where / N_MOVE_DIST;
    int dist = where - dir * N_MOVE_DIST;
    float angle = (2 * H_PI / N_MOVE_DIR) * dir;
    float distance = 0;
    switch (dist)
    {
        case 0:
            distance = 50;
            break;
        case 1:
            distance = 100;
            break;
        case 2:
            distance = 200;
            break;
        case 3:
            distance = 500;
            break;
        case 4:
            distance = 1000;
            break;
        case 5:
            distance = 2000;
            break;
    }
    Matrix3 rotY(MRotationY, angle);
    Vector3 move = Vector3(0, 0, distance) * rotY;
    Vector3 pos = vehicle->Position() + move;

    if (vehicle->IsLocal())
    {
        vehicle->SendMove(pos);
    }
    else
    {
        RadioMessageVMove msg(vehicle, pos);
        GetNetworkManager().SendRadioMessage(&msg);
    }
}

void InGameUI::IssueWatchTarget(AIGroup* grp, int tgt)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }

    Target* target = _visibleListTemp[tgt];
    if (!target)
    {
        return;
    }

    grp->SendTarget(target, false, false, ListSelectedUnits());

    // grp->SendState(new RadioMessageWatchTgt(grp,ListSelectedUnits(),target));
    ClearSelectedUnits();
}

void InGameUI::IssueWatchAround(AIGroup* grp)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }
    grp->SendState(new RadioMessageWatchAround(grp, ListSelectedUnits()));
    ClearSelectedUnits();
}

void InGameUI::IssueEngage(AIGroup* grp)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }
    grp->SendState(new RadioMessageTarget(grp, ListSelectedUnits(), nullptr, true, false));
    ClearSelectedUnits();
}

void InGameUI::IssueFire(AIGroup* grp)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }
    grp->SendState(new RadioMessageTarget(grp, ListSelectedUnits(), nullptr, false, true));
    ClearSelectedUnits();
}

void InGameUI::IssueWatchAuto(AIGroup* grp)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }
    grp->SendState(new RadioMessageWatchAuto(grp, ListSelectedUnits()));
    ClearSelectedUnits();
}

void InGameUI::SendFireReady(AIUnit* unit, bool ready)
{
    if (!unit)
    {
        return;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }
    grp->ReportFire(unit, ready);
}

void InGameUI::SendAnswer(AIUnit* unit, AI::Answer answer)
{
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }
    if (unit->IsSubgroupLeader())
    {
        AISubgroup* subgrp = unit->GetSubgroup();
        subgrp->SendAnswer(answer);
        subgrp->FailCommand();
        if (unit == grp->Leader())
        {
            Command temp;
            temp._message = Command::Wait;
            grp->GetRadio().Transmit(
                new RadioMessageSubgroupAnswer(unit->GetSubgroup(), nullptr, answer, &temp, true, unit),
                grp->GetCenter()->GetLanguage());
        }
    }
    else
    {
        Command temp;
        temp._message = Command::Wait;
        grp->GetRadio().Transmit(
            new RadioMessageSubgroupAnswer(unit->GetSubgroup(), nullptr, answer, &temp, true, unit),
            grp->GetCenter()->GetLanguage());
    }
}

void InGameUI::SendConfirm(AIUnit* unit)
{
    // get command
    AISubgroup* subgrp = unit->GetSubgroup();
    Command* cmd = subgrp->GetCommand();
    Command temp;
    if (!cmd)
    {
        temp._message = Command::Wait;
        cmd = &temp;
    }
    AIGroup* group = subgrp->GetGroup();
    group->GetRadio().Transmit(new RadioMessageCommandConfirm(unit, group, *cmd), group->GetCenter()->GetLanguage());
}

void InGameUI::SendResourceState(AIUnit* unit, AI::Answer answer)
{
    unit->SendAnswer(answer);
    // report some state
}

void InGameUI::SendObjectDestroyed(AIUnit* unit, AIGroup* grp)
{
    // find if there is some taget to report
    if (!grp)
    {
        return;
    }
    const TargetList& list = grp->GetTargetList();
    Target* tgt = nullptr;
    for (int i = 0; i < list.Size(); i++)
    {
        Target* tar = list[i];
        EntityAI* killer = tar->idKiller;
        if (!killer)
        {
            continue;
        }
        if ((killer == unit->GetVehicle() || killer == unit->GetPerson()) && tar->destroyed &&
            (tar->timeReported <= TIME_MIN || tar->timeReported < Glob.time - 30))
        {
            tgt = tar;
            // mark as reported
            tar->timeReported = Glob.time;
            tar->posReported = tar->position;
            break;
        }
    }

    AICenter* center = grp->GetCenter();
    grp->GetRadio().Transmit(new RadioMessageObjectDestroyed(unit, grp, tgt ? tgt->type : nullptr),
                             center->GetLanguage());
}

void InGameUI::SendKilled(AIUnit* unit, PackedBoolArray list)
{
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (list.Get(i))
        {
            grp->SendUnitDown(unit, grp->UnitWithID(i + 1));
        }
    }
}

void InGameUI::IssueWatch(AIGroup* grp, int what)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }

    int dir = what;
    float angle = (-2 * H_PI / N_WATCH_DIR) * dir;
    Matrix3 rotY(MRotationY, angle);
    Vector3 tgtDir = rotY.Direction();

    grp->SendState(new RadioMessageWatchDir(grp, ListSelectedUnits(), tgtDir));

    ClearSelectedUnits();
}

void InGameUI::IssueAttack(AIGroup* grp, int tgt)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }

    Target* target = _visibleListTemp[tgt];
    if (!target)
    {
        return;
    }

    AIUnit* unit = nullptr;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GetSelectedUnit(i);
        if (u)
        {
            unit = u;
            break;
        }
    }
    if (!unit)
    {
        return;
    }

    Command cmd;
    cmd._message = Command::AttackAndFire;
    cmd._targetE = target;
    cmd._destination = unit->Position();

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GetSelectedUnit(i);
        if (u)
        {
            u->AssignTarget(target);
        }
    }

    if (CheckJoin(grp))
    {
        cmd._context = Command::CtxUIWithJoin;
        cmd._joinToSubgroup = grp->MainSubgroup();
    }
    else
    {
        cmd._context = Command::CtxUI;
    }

    grp->SendCommand(cmd, ListSelectedUnits());
    ClearSelectedUnits();
}

void InGameUI::IssueGetIn(AIGroup* grp, int index)
{
    if (IsEmptySelectedUnits())
    {
        return;
    }

    int tgt = index / N_GETIN_POS;
    int pos = index - tgt * N_GETIN_POS;

    Target* target = _visibleListTemp[tgt];
    if (!target)
    {
        return;
    }
    TargetType* obj = target->idExact;
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
    {
        return;
    }

    // assign vehicle
    bool canAsDriver = veh->GetType()->HasDriver();
    bool canAsCommander = veh->GetType()->HasCommander();
    bool canAsGunner = veh->GetType()->HasGunner();
    AIUnit* driver = veh->GetDriverAssigned();
    if (driver)
    {
        if (veh->QIsDriverIn())
        {
            canAsDriver = false;
        }
        else if (driver->GetGroup() && driver->GetGroup()->CommandSent(driver, Command::GetIn))
        {
            canAsDriver = false;
        }
    }
    if (canAsCommander)
    {
        AIUnit* commander = veh->GetCommanderAssigned();
        if (commander)
        {
            if (veh->QIsCommanderIn())
            {
                canAsCommander = false;
            }
            else if (commander->GetGroup() && commander->GetGroup()->CommandSent(commander, Command::GetIn))
            {
                canAsCommander = false;
            }
        }
    }
    if (canAsGunner)
    {
        AIUnit* gunner = veh->GetGunnerAssigned();
        if (gunner)
        {
            if (veh->QIsGunnerIn())
            {
                canAsGunner = false;
            }
            else if (gunner->GetGroup() && gunner->GetGroup()->CommandSent(gunner, Command::GetIn))
            {
                canAsGunner = false;
            }
        }
    }

    switch (pos)
    {
        case 1:
            if (!canAsDriver)
            {
                return;
            }
            break;
        case 2:
            if (!canAsCommander)
            {
                return;
            }
            break;
        case 3:
            if (!canAsGunner)
            {
                return;
            }
            break;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = GetSelectedUnit(i);
        if (!unit)
        {
            continue;
        }
        unit->AllowGetIn(true);

        switch (pos)
        {
            case 0: // anywhere
                if (canAsDriver)
                {
                    if (veh->GetGroupAssigned() != grp)
                    {
                        grp->AddVehicle(veh);
                    }
                    unit->AssignAsDriver(veh);
                    canAsDriver = false;
                }
                else if (canAsCommander)
                {
                    unit->AssignAsCommander(veh);
                    canAsCommander = false;
                }
                else if (canAsGunner)
                {
                    unit->AssignAsGunner(veh);
                    canAsGunner = false;
                }
                else if (unit->VehicleAssigned() != veh)
                {
                    unit->AssignAsCargo(veh);
                }
                break;
            case 4: // cargo
                    //			if (unit->VehicleAssigned() != veh)
                unit->AssignAsCargo(veh);
                break;
            case 1: // driver
                // canAsDriver == true
                if (veh->GetGroupAssigned() != grp)
                {
                    grp->AddVehicle(veh);
                }
                unit->AssignAsDriver(veh);
                ClearSelectedUnits();
                SetSelectedUnit(i, unit);
                goto SendGetIn;
            case 2: // commander
                // canAsCommander == true
                unit->AssignAsCommander(veh);
                ClearSelectedUnits();
                SetSelectedUnit(i, unit);
                goto SendGetIn;
            case 3: // gunner
                // canAsGunner == true
                unit->AssignAsGunner(veh);
                ClearSelectedUnits();
                SetSelectedUnit(i, unit);
                goto SendGetIn;
        }
    }

SendGetIn:

    Command cmd;
    cmd._message = Command::GetIn;
    cmd._target = obj;
    {
        cmd._context = Command::CtxUI;
    }

    grp->SendCommand(cmd, ListSelectedUnits());
    ClearSelectedUnits();
}

} // namespace Poseidon
