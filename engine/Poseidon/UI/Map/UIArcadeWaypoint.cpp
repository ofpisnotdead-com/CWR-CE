#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Foundation/Common/Win.h>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Game/Mission/MissionPathLoader.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <optional>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

DisplayArcadeWaypoint::DisplayArcadeWaypoint(ControlsContainer* parent, ArcadeTemplate* templ, int indexGroup,
                                             int index, ArcadeWaypointInfo& waypoint, bool advanced)
    : Display(parent)
{
    _enableSimulation = false;
    _enableDisplay = false;

    _template = templ;
    _indexGroup = indexGroup;
    _index = index;
    _waypoint = waypoint;

    _advanced = advanced;

    if (advanced)
    {
        Load("RscDisplayArcadeWaypoint");
    }
    else
    {
        Load("RscDisplayArcadeWaypointSimple");
    }
    if (!GetCtrl(IDC_ARCWP_HOUSEPOS))
    {
        GetCtrl(IDC_ARCWP_HOUSEPOSTEXT)->ShowCtrl(false);
    }
}

Control* DisplayArcadeWaypoint::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_ARCWP_TITLE:
        {
            CStatic* text = new CStatic(this, idc, cls);
            if (_index < 0)
            {
                text->SetText(LocalizeString(IDS_ARCWP_TITLE1));
            }
            else
            {
                text->SetText(LocalizeString(IDS_ARCWP_TITLE2));
            }
            return text;
        }
        case IDC_ARCWP_TYPE:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            ArcadeGroupInfo& gInfo = _template->groups[_indexGroup];
            int sel = 0;
            if (gInfo.side == TLogic)
            {
                for (int i = ACLOGIC; i < ACLOGICN; i++)
                {
                    int index = combo->AddString(LocalizeString(IDS_AC_AND + i - ACAND));
                    combo->SetValue(index, i);
                    if (i == _waypoint.type)
                    {
                        sel = index;
                    }
                }
            }
            else
            {
                for (int i = ACMOVE; i < ACN; i++)
                {
                    int index = combo->AddString(LocalizeString(IDS_AC_MOVE + i - ACMOVE));
                    combo->SetValue(index, i);
                    if (i == _waypoint.type)
                    {
                        sel = index;
                    }
                }
            }
            combo->SetCurSel(sel);
            return combo;
        }
        case IDC_ARCWP_SEMAPHORE:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_NO_CHANGE));
            int i;
            for (i = 0; i < AI::NSemaphores; i++)
            {
                combo->AddString(LocalizeString(IDS_IGNORE + i));
            }
            combo->SetCurSel(_waypoint.combatMode + 1);
            return combo;
        }
        case IDC_ARCWP_FORM:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_NO_CHANGE));
            int i;
            for (i = 0; i < AI::NForms; i++)
            {
                combo->AddString(LocalizeString(IDS_COLUMN + i));
            }
            combo->SetCurSel(_waypoint.formation + 1);
            return combo;
        }
        case IDC_ARCWP_SPEED:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_SPEED_UNCHANGED));
            combo->AddString(LocalizeString(IDS_SPEED_LIMITED));
            combo->AddString(LocalizeString(IDS_SPEED_NORMAL));
            combo->AddString(LocalizeString(IDS_SPEED_FULL));
            combo->SetCurSel(_waypoint.speed);
            return combo;
        }
        case IDC_ARCWP_COMBAT:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_COMBAT_UNCHANGED));
            combo->AddString(LocalizeString(IDS_COMBAT_CARELESS));
            combo->AddString(LocalizeString(IDS_COMBAT_SAFE));
            combo->AddString(LocalizeString(IDS_COMBAT_AWARE));
            combo->AddString(LocalizeString(IDS_COMBAT_COMBAT));
            combo->AddString(LocalizeString(IDS_COMBAT_STEALTH));
            combo->SetCurSel(_waypoint.combat);
            return combo;
        }
        case IDC_ARCWP_SEQ:
        {
            CCombo* combo = new CCombo(this, idc, cls);

            char buffer[256];
            ArcadeGroupInfo& gInfo = _template->groups[_indexGroup];
            int i, n = gInfo.waypoints.Size();
            for (i = 0; i < n; i++)
            {
                ArcadeWaypointInfo& info = gInfo.waypoints[i];
                snprintf(buffer, sizeof(buffer), "%d: %s %s", i,
                         (const char*)LocalizeString(IDS_AC_MOVE + info.type - ACMOVE),
                         info.description ? (const char*)info.description : "");
                combo->AddString(buffer);
            }
            snprintf(buffer, sizeof(buffer), "%d:", n);
            combo->AddString(buffer);

            if (_index < 0)
            {
                combo->SetCurSel(n);
            }
            else
            {
                combo->SetCurSel(_index);
            }
            return combo;
        }
        case IDC_ARCWP_DESC:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_waypoint.description ? _waypoint.description : "");
            return edit;
        }
        case IDC_ARCWP_EXPCOND:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_waypoint.expCond);
            return edit;
        }
        case IDC_ARCWP_EXPACTIV:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_waypoint.expActiv);
            return edit;
        }
        case IDC_ARCWP_SCRIPT:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_waypoint.script);
            return edit;
        }
        case IDC_ARCWP_PLACE:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%g", _waypoint.placement);
            edit->SetText(buffer);
            return edit;
        }
        case IDC_ARCWP_TIMEOUT_MIN:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%g", _waypoint.timeoutMin);
            edit->SetText(buffer);
            return edit;
        }
        case IDC_ARCWP_TIMEOUT_MID:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%g", _waypoint.timeoutMid);
            edit->SetText(buffer);
            return edit;
        }
        case IDC_ARCWP_TIMEOUT_MAX:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%g", _waypoint.timeoutMax);
            edit->SetText(buffer);
            return edit;
        }
        case IDC_ARCWP_HOUSEPOS:
        {
            if (_waypoint.idStatic < 0)
            {
                return nullptr;
            }
            Vehicle* veh = nullptr;
            int n = GWorld->NBuildings();
            for (int i = 0; i < n; i++)
            {
                Vehicle* v = GWorld->GetBuilding(i);
                if (v && v->ID() == _waypoint.idStatic)
                {
                    veh = v;
                    break;
                }
            }
            if (!veh)
            {
                return nullptr;
            }
            if (!veh->GetShape())
            {
                return nullptr;
            }
            if (veh->GetShape()->FindPaths() < 0)
            {
                return nullptr;
            }
            const IPaths* build = veh->GetIPaths();
            if (!build)
            {
                return nullptr;
            }
            n = build->NPos();
            if (n == 0)
            {
                return nullptr;
            }
            CCombo* combo = new CCombo(this, idc, cls);
            for (int i = 0; i < n; i++)
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_HOUSE_POSITION), i + 1);
                combo->AddString(buffer);
            }
            combo->SetCurSel(_waypoint.housePos);
            return combo;
        }
        case IDC_ARCWP_SHOW:
        {
            CToolBox* toolbox = new CToolBox(this, idc, cls);
            toolbox->SetCurSel(_waypoint.showWP);
            return toolbox;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayArcadeWaypoint::OnButtonClicked(int idc)
{
    if (idc == IDC_ARCWP_EFFECTS)
    {
        CreateChild(new DisplayArcadeEffects(this, _waypoint.effects, _advanced));
    }
    else
    {
        Display::OnButtonClicked(idc);
    }
}

void DisplayArcadeWaypoint::OnChildDestroyed(int idd, int exit)
{
    if (idd == IDD_ARCADE_EFFECTS && exit == IDC_OK)
    {
        DisplayArcadeEffects* display = dynamic_cast<DisplayArcadeEffects*>((ControlsContainer*)_child);
        PoseidonAssert(display);
        _waypoint.effects = display->_effects;
    }
    Display::OnChildDestroyed(idd, exit);
}

bool DisplayArcadeWaypoint::CanDestroy()
{
    if (_exit != IDC_OK)
    {
        return true;
    }

    GameState* gstate = GWorld->GetGameState();

    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_EXPCOND));
    if (edit)
    {
        if (!gstate->CheckEvaluateBool(edit->GetText()))
        {
            FocusCtrl(IDC_ARCWP_EXPCOND);
            CreateMsgBox(MB_BUTTON_OK, gstate->GetLastErrorText());
            edit->SetCaretPos(gstate->GetLastErrorPos());
            return false;
        }
    }

    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_EXPACTIV));
    if (edit)
    {
        if (!gstate->CheckExecute(edit->GetText()))
        {
            FocusCtrl(IDC_ARCWP_EXPACTIV);
            CreateMsgBox(MB_BUTTON_OK, gstate->GetLastErrorText());
            edit->SetCaretPos(gstate->GetLastErrorPos());
            return false;
        }
    }

    return true;
}

void DisplayArcadeWaypoint::Destroy()
{
    Display::Destroy();

    if (_exit != IDC_OK)
    {
        return;
    }

    CCombo* combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_TYPE));
    if (combo)
    {
        _waypoint.type = (ArcadeWaypointType)combo->GetValue(combo->GetCurSel());
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_SEMAPHORE));
    if (combo)
    {
        _waypoint.combatMode = (AI::Semaphore)(combo->GetCurSel() - 1);
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_FORM));
    if (combo)
    {
        _waypoint.formation = (AI::Formation)(combo->GetCurSel() - 1);
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_SPEED));
    if (combo)
    {
        _waypoint.speed = (SpeedMode)combo->GetCurSel();
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_COMBAT));
    if (combo)
    {
        _waypoint.combat = (CombatMode)combo->GetCurSel();
    }
    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_DESC));
    if (edit)
    {
        _waypoint.description = edit->GetText();
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_EXPCOND));
    if (edit)
    {
        _waypoint.expCond = edit->GetText();
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_EXPACTIV));
    if (edit)
    {
        _waypoint.expActiv = edit->GetText();
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_SCRIPT));
    if (edit)
    {
        _waypoint.script = edit->GetText();
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_PLACE));
    if (edit)
    {
        _waypoint.placement = edit->GetText() ? atof(edit->GetText()) : 0;
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_TIMEOUT_MIN));
    if (edit)
    {
        _waypoint.timeoutMin = edit->GetText() ? atof(edit->GetText()) : 0;
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_TIMEOUT_MID));
    if (edit)
    {
        _waypoint.timeoutMid = edit->GetText() ? atof(edit->GetText()) : 0;
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_ARCWP_TIMEOUT_MAX));
    if (edit)
    {
        _waypoint.timeoutMax = edit->GetText() ? atof(edit->GetText()) : 0;
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCWP_HOUSEPOS));
    if (combo)
    {
        _waypoint.housePos = combo->GetCurSel();
    }
    else
    {
        _waypoint.housePos = -1;
    }

    CToolBox* toolbox = dynamic_cast<CToolBox*>(GetCtrl(IDC_ARCWP_SHOW));
    if (toolbox)
    {
        //		_waypoint.show = toolbox->GetCurSel() == 1;
        _waypoint.showWP = (AWPShow)toolbox->GetCurSel();
    }
}

Control* DisplayTemplateSave::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_TEMPL_NAME:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(Glob.header.filename);
            return edit;
        }
        case IDC_TEMPL_MODE:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_EXPORT_NONE));
            combo->AddString(LocalizeString(IDS_EXPORT_SINGLE));
            combo->AddString(LocalizeString(IDS_EXPORT_MULTI));
            combo->AddString(LocalizeString(IDS_EXPORT_MAIL));
            combo->SetCurSel(0);
            return combo;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

Control* DisplayTemplateLoad::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_TEMPL_TITLE)
    {
        CStatic* text = new CStatic(this, idc, cls);
        if (_merge)
        {
            text->SetText(LocalizeString(IDS_TEMPL_MERGE));
        }
        return text;
    }
    else if (idc == IDC_TEMPL_ISLAND)
    {
        CCombo* combo = new CCombo(this, idc, cls);
        int sel = 0;
        // int n = (Pars>>"CfgWorlds">>"worlds").GetSize();
        int n = (Pars >> "CfgWorldList").GetEntryCount();
        for (int i = 0; i < n; i++)
        {
            const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(i);
            if (!entry.IsClass())
            {
                continue;
            }
            RString name = entry.GetName();
            // RString name = (Pars>>"CfgWorlds">>"worlds")[i];

            // ADDED - check if wrp file exists
            RString fullname = GetWorldName(name);
            if (!QIFStreamB::FileExist(fullname))
            {
                continue;
            }

            int index = combo->AddString(Pars >> "CfgWorlds" >> name >> "description");
            combo->SetData(index, name);

            if (stricmp(name, Glob.header.worldname) == 0)
            {
                sel = index;
            }
        }
        combo->SetCurSel(sel);
        return combo;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayTemplateLoad::OnComboSelChanged(int idc, int curSel)
{
    if (idc == IDC_TEMPL_ISLAND)
    {
        OnIslandChanged();
    }

    Display::OnComboSelChanged(idc, curSel);
}

void DisplayTemplateLoad::OnIslandChanged()
{
    //	PoseidonAssert(dynamic_cast<CCombo *>(GetCtrl(IDC_TEMPL_ISLAND)));
    CCombo* combo = static_cast<CCombo*>(GetCtrl(IDC_TEMPL_ISLAND));
    if (!combo)
    {
        return;
    }
    int index = combo->GetCurSel();
    if (index < 0)
    {
        return;
    }
    RString island = combo->GetData(index);

    PoseidonAssert(dynamic_cast<CCombo*>(GetCtrl(IDC_TEMPL_NAME)));
    combo = static_cast<CCombo*>(GetCtrl(IDC_TEMPL_NAME));
    if (!combo)
    {
        return;
    }

    combo->ClearStrings();
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s*.%s", (const char*)GetMissionsDirectory(), (const char*)island);
    _finddata_t info;
    intptr_t h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                char name[256];
                snprintf(name, sizeof(name), "%s", info.name);
                char* ext = strrchr(name, '.');
                if (ext)
                {
                    *ext = 0;
                }
                combo->AddString(name);
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    combo->SortItems();
    combo->SetCurSel(0);
}

// static const int daysInMonth[]={31,28,31,30,31,30,31,31,30,31,30,31};
} // namespace Poseidon
namespace Poseidon
{
int GetDaysInMonth(int year, int month);
}
namespace Poseidon
{

void DisplayIntel::UpdateDays(CCombo* combo, int day)
{
    CCombo* monthCombo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_MONTH));
    PoseidonAssert(monthCombo);
    int month = monthCombo->GetCurSel();
    PoseidonAssert(month >= 0);

    int curSel = day > 0 ? day - 1 : combo->GetCurSel();
    combo->ClearStrings();
    char buffer[4];
    for (int i = 0; i < Poseidon::GetDaysInMonth(_intel.year, month); i++)
    {
        snprintf(buffer, sizeof(buffer), "%d", i + 1);
        combo->AddString(buffer);
    }
    if (curSel < 0)
    {
        curSel = 0;
    }
    if (curSel >= combo->GetSize())
    {
        curSel = combo->GetSize() - 1;
    }
    combo->SetCurSel(curSel);
}

void DisplayIntel::OnComboSelChanged(int idc, int curSel)
{
    switch (idc)
    {
        case IDC_INTEL_MONTH:
        {
            CCombo* combo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_DAY));
            if (combo)
            {
                UpdateDays(combo);
            }
        }
        break;
    }
}

Control* DisplayIntel::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_INTEL_MONTH:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            int i;
            for (i = 0; i < 12; i++)
            {
                combo->AddString(LocalizeString(IDS_JANUARY + i));
            }
            combo->SetCurSel(_intel.month - 1);
            return combo;
        }
        case IDC_INTEL_DAY:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            UpdateDays(combo, _intel.day);
            return combo;
        }
        case IDC_INTEL_HOUR:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            char buffer[4];
            for (int i = 0; i < 24; i++)
            {
                snprintf(buffer, sizeof(buffer), "%d", i);
                combo->AddString(buffer);
            }
            combo->SetCurSel(_intel.hour);
            return combo;
        }
        case IDC_INTEL_MINUTE:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            char buffer[4];
            for (int i = 0; i < 60; i += 5)
            {
                snprintf(buffer, sizeof(buffer), "%02d", i);
                combo->AddString(buffer);
            }
            combo->SetCurSel(toInt(_intel.minute / 5.0));
            return combo;
        }
        case IDC_INTEL_RESISTANCE:
        {
            CToolBox* ctrl = new CToolBox(this, idc, cls);
            if (_intel.friends[TGuerrila][TEast] < 0.5)
            {
                if (_intel.friends[TGuerrila][TWest] < 0.5)
                {
                    ctrl->SetCurSel(0);
                }
                else
                {
                    ctrl->SetCurSel(1);
                }
            }
            else if (_intel.friends[TGuerrila][TWest] < 0.5)
            {
                ctrl->SetCurSel(2);
            }
            else
            {
                ctrl->SetCurSel(3);
            }
            return ctrl;
        }
        case IDC_INTEL_WEATHER:
        {
            CSlider* slider = new CSlider(this, idc, cls);
            slider->SetRange(0, 1);
            slider->SetSpeed(0.01, 0.1);
            slider->SetThumbPos(1.0 - _intel.weather);
            return slider;
        }
        case IDC_INTEL_FOG:
        {
            CSlider* slider = new CSlider(this, idc, cls);
            slider->SetRange(0, 1);
            slider->SetSpeed(0.01, 0.1);
            slider->SetThumbPos(_intel.fog);
            return slider;
        }
        case IDC_INTEL_WEATHER_FORECAST:
        {
            CSlider* slider = new CSlider(this, idc, cls);
            slider->SetRange(0, 1);
            slider->SetSpeed(0.01, 0.1);
            slider->SetThumbPos(1.0 - _intel.weatherForecast);
            return slider;
        }
        case IDC_INTEL_FOG_FORECAST:
        {
            CSlider* slider = new CSlider(this, idc, cls);
            slider->SetRange(0, 1);
            slider->SetSpeed(0.01, 0.1);
            slider->SetThumbPos(_intel.fogForecast);
            return slider;
        }
        case IDC_INTEL_BRIEFING_NAME:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_intel.briefingName);
            return edit;
        }
        case IDC_INTEL_BRIEFING_DESC:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(_intel.briefingDescription);
            return edit;
        }
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayIntel::Destroy()
{
    Display::Destroy();

    if (_exit != IDC_OK)
    {
        return;
    }

    CCombo* combo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_MONTH));
    if (combo)
    {
        _intel.month = combo->GetCurSel() + 1;
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_DAY));
    if (combo)
    {
        _intel.day = combo->GetCurSel() + 1;
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_HOUR));
    if (combo)
    {
        _intel.hour = combo->GetCurSel();
    }
    combo = dynamic_cast<CCombo*>(GetCtrl(IDC_INTEL_MINUTE));
    if (combo)
    {
        _intel.minute = 5 * combo->GetCurSel();
    }
    CToolBox* ctrl = dynamic_cast<CToolBox*>(GetCtrl(IDC_INTEL_RESISTANCE));
    if (ctrl)
    {
        switch (ctrl->GetCurSel())
        {
            case 0:
                _intel.friends[TWest][TGuerrila] = _intel.friends[TGuerrila][TWest] = 0.0;
                _intel.friends[TEast][TGuerrila] = _intel.friends[TGuerrila][TEast] = 0.0;
                break;
            case 1:
                _intel.friends[TWest][TGuerrila] = _intel.friends[TGuerrila][TWest] = 1.0;
                _intel.friends[TEast][TGuerrila] = _intel.friends[TGuerrila][TEast] = 0.0;
                break;
            case 2:
                _intel.friends[TWest][TGuerrila] = _intel.friends[TGuerrila][TWest] = 0.0;
                _intel.friends[TEast][TGuerrila] = _intel.friends[TGuerrila][TEast] = 1.0;
                break;
            case 3:
                _intel.friends[TWest][TGuerrila] = _intel.friends[TGuerrila][TWest] = 1.0;
                _intel.friends[TEast][TGuerrila] = _intel.friends[TGuerrila][TEast] = 1.0;
                break;
        }
    }
    CSlider* slider = dynamic_cast<CSlider*>(GetCtrl(IDC_INTEL_WEATHER));
    if (slider)
    {
        _intel.weather = 1.0 - slider->GetThumbPos();
    }
    slider = dynamic_cast<CSlider*>(GetCtrl(IDC_INTEL_FOG));
    if (slider)
    {
        _intel.fog = slider->GetThumbPos();
    }
    slider = dynamic_cast<CSlider*>(GetCtrl(IDC_INTEL_WEATHER_FORECAST));
    if (slider)
    {
        _intel.weatherForecast = 1.0 - slider->GetThumbPos();
    }
    slider = dynamic_cast<CSlider*>(GetCtrl(IDC_INTEL_FOG_FORECAST));
    if (slider)
    {
        _intel.fogForecast = slider->GetThumbPos();
    }
    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_INTEL_BRIEFING_NAME));
    if (edit)
    {
        _intel.briefingName = edit->GetText();
    }
    edit = dynamic_cast<CEdit*>(GetCtrl(IDC_INTEL_BRIEFING_DESC));
    if (edit)
    {
        _intel.briefingDescription = edit->GetText();
    }
}

Display* CurrentDisplay()
{
    if (!GWorld)
    {
        return nullptr;
    }

    Display* disp = dynamic_cast<Display*>(GWorld->Options());
    if (!disp)
    {
        return nullptr;
    }

    ControlsContainer* ptr = disp;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }
    return dynamic_cast<Display*>(ptr);
}

bool MsgBox(RString text, int flags, int idd)
{
    Display* disp = CurrentDisplay();
    if (!disp)
    {
        return false;
    }

    disp->CreateMsgBox(flags, text, idd);
    return true;
}

bool MsgBox(int ids, int flags, int idd)
{
    return MsgBox(LocalizeString(ids), flags, idd);
}

bool ProcessTemplateName(RString name, RString dir)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", (const char*)name);

    // parse mission name

    // mission
    char* world = strchr(buffer, '.');
    if (!world)
    {
        return false;
    }
    *world = 0;
    world++;

    // world
    char* ext = strchr(world, '.');
    if (ext)
    {
        *ext = 0;
        ext++;
        PoseidonAssert(stricmp(ext, "sqm") == 0);
    }

    SetMission(world, buffer, dir);
    return true;
}

bool ProcessTemplateName(RString name)
{
    return ProcessTemplateName(name, GetMissionsDir());
}

bool ProcessFullName(RString name)
{
    const auto selection = MissionPathLoader::Loader::DescribeMissionFile((const char*)name);
    if (!selection)
    {
        return false;
    }

    CurrentCampaign = "";
    CurrentBattle = "";
    CurrentMission = "";
    SetBaseDirectory("");
    SetMission(selection->worldName.c_str(), selection->missionName.c_str(), selection->missionParentDirectory.c_str());
    return true;
}

bool ParseCutscene(RString cutscene, bool multiplayer)
{
    RString name = GetMissionDirectory() + RString("mission.sqm");

    ParamArchiveLoad ar;
    if (!ar.LoadBin(name) && ar.Load(name) != LSOK)
    {
        CurrentTemplate.Clear();
        return false;
    }
    ATSParams params;
    ar.SetParams(&params);
    LSError result = ar.Serialize(cutscene, CurrentTemplate, 1);
    if (result == LSNoAddOn)
    {
        RString message = LocalizeString(IDS_MSG_ADDON_MISSING);
        bool first = true;
        for (int i = 0; i < CurrentTemplate.missingAddOns.Size(); i++)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                message = message + RString(", ");
            }
            message = message + CurrentTemplate.missingAddOns[i];
        }
        LOG_WARN(Mission, "Cannot load {} from {}: {}", (const char*)cutscene, (const char*)name, (const char*)message);
        Poseidon::Foundation::WarningMessage(message);
    }
    else if (result != LSOK)
    {
        LOG_WARN(Mission, "Cannot load {} from {}: {}", (const char*)cutscene, (const char*)name,
                 ar.GetErrorName(result));
        Poseidon::Foundation::WarningMessage("Cannot load mission");
    }
    if (ar.GetArVersion() < 7)
    {
        if (ar.Serialize("Intel", CurrentTemplate.intel, 1) != LSOK)
        {
            Poseidon::Foundation::WarningMessage("Cannot load intel (old mission format)");
        }
    }

    if (CurrentTemplate.groups.Size() == 0 || !CurrentTemplate.IsConsistent(nullptr, multiplayer))
    {
        CurrentTemplate.Clear();
        return false;
    }

    ArcadeUnitInfo* uInfo = CurrentTemplate.FindPlayer();
    if (uInfo)
    {
        Glob.header.playerSide = (TargetSide)uInfo->side;
    }
    return true;
}

bool ParseIntro()
{
    return ParseCutscene("Intro", false);
}

bool ParseMission(bool multiplayer)
{
    return ParseCutscene("Mission", multiplayer);
}

} // namespace Poseidon
