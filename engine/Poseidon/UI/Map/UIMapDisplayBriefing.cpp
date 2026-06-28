#include <Poseidon/Core/Application.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Map/UIMapCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Evaluator/express.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::EnumName;

using namespace Poseidon;
namespace
{
RString GetCurrentMissionDisplayName()
{
    RString name = LoadLocalizedMissionBriefingName(GetMissionDirectory(), CurrentTemplate.intel.briefingName);
    if (name.GetLength() == 0)
        name = Glob.header.filename;
    return name;
}
} // namespace

RString GetEquipmentFile();
static int FindSectionForUnit(CHTML* html, RString sectionName, AIUnit* unit);

static void UpdateGetReadyTitleText(CStatic* text)
{
    if (!text)
        return;

    AIUnit* unit = GWorld->FocusOn();
    AIGroup* grp = unit ? unit->GetGroup() : nullptr;
    if (!grp)
    {
        text->SetText("");
        return;
    }

    text->SetText(FormatLocalizedBriefingTitle(unit->GetPerson()->GetInfo()._name, grp->GetName(), unit->ID()));
}

static int FindSectionForUnit(CHTML* html, RString sectionName, AIUnit* unit)
{
    RString prefix = sectionName + RString(".");
    int section = -1;
    if (unit)
    {
        RString name = unit->GetVehicle()->GetVarName();
        if (name.GetLength() > 0)
        {
            section = html->FindSection(prefix + name);
        }
        if (section < 0)
        {
            AIGroup* grp = unit->GetGroup();
            if (grp)
            {
                AIUnit* leader = grp->Leader();
                if (leader && leader != unit)
                {
                    name = leader->GetVehicle()->GetVarName();
                    if (name.GetLength() > 0)
                    {
                        section = html->FindSection(prefix + name);
                    }
                }
                if (section < 0)
                {
                    AICenter* center = grp->GetCenter();
                    if (center)
                    {
                        switch (center->GetSide())
                        {
                            case TWest:
                                name = prefix + RString("West");
                                goto SideFound;
                            case TEast:
                                name = prefix + RString("East");
                                goto SideFound;
                            case TGuerrila:
                                name = prefix + RString("Guerrila");
                                goto SideFound;
                            case TCivilian:
                                name = prefix + RString("Civilian");
                                goto SideFound;
                            SideFound:
                                section = html->FindSection(name);
                                break;
                        }
                    }
                }
            }
        }
    }
    if (section < 0)
    {
        section = html->FindSection(sectionName);
    }
    return section;
}

static RString GetSkill(float skill)
{
    RString text(" (");
    if (skill <= 0.25)
    {
        text = text + LocalizeString(IDS_SKILL_NOVICE);
    }
    else if (skill <= 0.45)
    {
        text = text + LocalizeString(IDS_SKILL_ROOKIE);
    }
    else if (skill <= 0.65)
    {
        text = text + LocalizeString(IDS_SKILL_RECRUIT);
    }
    else if (skill <= 0.85)
    {
        text = text + LocalizeString(IDS_SKILL_VETERAN);
    }
    else
    {
        text = text + LocalizeString(IDS_SKILL_EXPERT);
    }
    return text + RString(")");
}

void DisplayMap::UpdateUnitsInBriefing()
{
    int section = _briefing->FindSection("Group");
    if (section < 0)
    {
        section = _briefing->AddSection();
    }
    else
    {
        _briefing->InitSection(section);
    }

    // list of weapons
    _briefing->AddName(section, "Group");
    _briefing->AddText(section, LocalizeString(IDS_BRIEF_GROUP), HFH1, HALeft, false, false, "");
    _briefing->AddBreak(section, false);

    Person* veh = GWorld->PlayerOn();
    if (!veh)
    {
        return;
    }
    AIUnit* unit = veh->Brain();
    if (!unit)
    {
        return;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        if (unit->IsFreeSoldier())
        {
            char id[8];
            snprintf(id, sizeof(id), "%d: ", unit->ID());
            RString text = RString(id) +
                           LocalizeString(IDS_SHORT_PRIVATE + ClampRankIndex(unit->GetPerson()->GetRank())) +
                           RString(". ") + unit->GetPerson()->GetInfo()._name;
            if (unit->IsGroupLeader())
            {
                text = text + LocalizeString(IDS_BRIEF_GROUP_LEADER);
            }
            text = text + GetSkill(unit->GetAbility());
            char href[32];
            snprintf(href, sizeof(href), "gear:%d", unit->ID());
            _briefing->AddText(section, text, HFP, HALeft, false, false, href);
            _briefing->AddBreak(section, false);
        }
        else
        {
            Transport* veh = unit->GetVehicleIn();
            PoseidonAssert(veh);
            RString text = veh->GetType()->GetDisplayName();
            RString href = "";
            _briefing->AddText(section, text, HFP, HALeft, false, false, href);
            _briefing->AddBreak(section, false);
            AIUnit* u = veh->CommanderBrain();
            if (u)
            {
                RString text = LocalizeString(IDS_BRIEF_COMMANDER);
                text = text + LocalizeString(IDS_PRIVATE + ClampRankIndex(u->GetPerson()->GetRank()));
                text = text + RString(" ");
                text = text + u->GetPerson()->GetInfo()._name;
                if (u->IsGroupLeader())
                {
                    text = text + LocalizeString(IDS_BRIEF_GROUP_LEADER);
                }
                text = text + GetSkill(u->GetAbility());
                char href[32];
                snprintf(href, sizeof(href), "gear:%d", u->ID());
                _briefing->AddText(section, text, HFP, HALeft, false, false, href);
                _briefing->AddBreak(section, false);
            }
            u = veh->DriverBrain();
            if (u)
            {
                RString text = LocalizeString(IDS_BRIEF_DRIVER);
                text = text + LocalizeString(IDS_PRIVATE + ClampRankIndex(u->GetPerson()->GetRank()));
                text = text + RString(" ");
                text = text + u->GetPerson()->GetInfo()._name;
                if (u->IsGroupLeader())
                {
                    text = text + LocalizeString(IDS_BRIEF_GROUP_LEADER);
                }
                text = text + GetSkill(u->GetAbility());
                char href[32];
                snprintf(href, sizeof(href), "gear:%d", u->ID());
                _briefing->AddText(section, text, HFP, HALeft, false, false, href);
                _briefing->AddBreak(section, false);
            }
            u = veh->GunnerBrain();
            if (u)
            {
                RString text = LocalizeString(IDS_BRIEF_GUNNER);
                text = text + LocalizeString(IDS_PRIVATE + ClampRankIndex(u->GetPerson()->GetRank()));
                text = text + RString(" ");
                text = text + u->GetPerson()->GetInfo()._name;
                if (u->IsGroupLeader())
                {
                    text = text + LocalizeString(IDS_BRIEF_GROUP_LEADER);
                }
                text = text + GetSkill(u->GetAbility());
                char href[32];
                snprintf(href, sizeof(href), "gear:%d", u->ID());
                _briefing->AddText(section, text, HFP, HALeft, false, false, href);
                _briefing->AddBreak(section, false);
            }
            for (int j = 0; j < veh->GetManCargo().Size(); j++)
            {
                Person* soldier = veh->GetManCargo()[j];
                if (!soldier)
                {
                    continue;
                }
                u = soldier->Brain();
                if (!u)
                {
                    continue;
                }
                RString text = LocalizeString(IDS_BRIEF_CARGO);
                text = text + LocalizeString(IDS_PRIVATE + ClampRankIndex(u->GetPerson()->GetRank()));
                text = text + RString(" ");
                text = text + u->GetPerson()->GetInfo()._name;
                if (u->IsGroupLeader())
                {
                    text = text + LocalizeString(IDS_BRIEF_GROUP_LEADER);
                }
                text = text + GetSkill(u->GetAbility());
                char href[32];
                snprintf(href, sizeof(href), "gear:%d", u->ID());
                _briefing->AddText(section, text, HFP, HALeft, false, false, href);
                _briefing->AddBreak(section, false);
            }
        }
    }
    _briefing->FormatSection(section);
}

void AddObjective(CHTMLContainer* html, int src, int dst, int value)
{
    if (value == OSHidden)
    {
        return;
    }

    RString picture;
    switch (value)
    {
        case OSDone:
            picture = "mission_done.paa";
            break;
        case OSFailed:
            picture = "mission_uncomplete.paa";
            break;
        default:
            Fail("Unknown objective type");
        case OSActive:
            picture = "mission_dot.paa";
            break;
    }
    html->AddBreak(dst, false);
    float imgHeight = 640.0f * 1.5 * html->GetPHeight();
    HTMLField* fld = html->AddImage(dst, picture, HALeft, false, -1, imgHeight, "");
    fld->exclude = true;
    float indent = 1.2 * fld->width;
    html->SetIndent(indent);
    html->CopySection(src, dst);
    html->SetIndent(0);
}

void DisplayMap::UpdatePlan()
{
    // init section
    bool actual = false;
    int section = _briefing->FindSection("__PLAN");
    if (section < 0)
    {
        section = _briefing->AddSection();
    }
    else
    {
        _briefing->InitSection(section);
        if (_briefing->CurrentSection() == section)
        {
            actual = true;
        }
        int s;
        while ((s = _briefing->FindSection("__PLAN")) >= 0)
        {
            if (_briefing->CurrentSection() == s)
            {
                actual = true;
            }
            _briefing->RemoveSection(s);
        }
    }
    _briefing->AddName(section, "__PLAN");

    // plan header
    AIUnit* unit = GWorld->FocusOn();
    int source = FindSectionForUnit(_briefing, "Plan", unit);
    // JIP: player not assigned yet — try by side from role
    if (source < 0 && !unit)
    {
        const PlayerRole* role = GetNetworkManager().GetMyPlayerRole();
        if (role)
        {
            RString sideName;
            switch (role->side)
            {
                case TWest:
                    sideName = "Plan.West";
                    break;
                case TEast:
                    sideName = "Plan.East";
                    break;
                case TGuerrila:
                    sideName = "Plan.Guerrila";
                    break;
                case TCivilian:
                    sideName = "Plan.Civilian";
                    break;
                default:
                    break;
            }
            if (sideName.GetLength() > 0)
                source = _briefing->FindSection(sideName);
        }
    }
    if (source >= 0)
    {
        _briefing->CopySection(source, section);
    }

    // objectives
    TargetSide side = TSideUnknown;
    if (unit)
    {
        AIGroup* grp = unit->GetGroup();
        if (grp)
        {
            AICenter* center = grp->GetCenter();
            if (center)
            {
                side = center->GetSide();
            }
        }
    }
    if (side == TSideUnknown && !unit)
    {
        const PlayerRole* role = GetNetworkManager().GetMyPlayerRole();
        if (role)
            side = role->side;
    }
    for (int s = 0; s < _briefing->NSections(); s++)
    {
        const HTMLSection& src = _briefing->GetSection(s);
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
                AddObjective(_briefing, s, section, value);
                break; // next section
            }
        }
    }

    _briefing->FormatSection(section);
    if (actual)
    {
        SwitchBriefingSection("__PLAN");
    }
}

DrawCoord SceneToScreen(Vector3Par pos)
{
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    float invz = CameraZoom / pos.Z();
    float x3d = 1 / as.leftFOV * pos.X() * invz + 0.5;
    float y3d = 0.5 - 1 / as.topFOV * pos.Y() * invz;
    DrawCoord pt;
    pt.x = (x3d - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX);
    pt.y = (y3d - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY);
    return pt;
}

void DisplayMap::SetRadioText()
{
    _radioAlpha->SetText(RString());
    _radioBravo->SetText(RString());
    _radioCharlie->SetText(RString());
    _radioDelta->SetText(RString());
    _radioEcho->SetText(RString());
    _radioFoxtrot->SetText(RString());
    _radioGolf->SetText(RString());
    _radioHotel->SetText(RString());
    _radioIndia->SetText(RString());
    _radioJuliet->SetText(RString());

    Person* player = GWorld->GetRealPlayer();
    if (!player || player->IsDammageDestroyed())
    {
        return;
    }

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
        CActiveText* ctrl;
        const char* radioKey = nullptr;
        switch (det->GetActivationBy())
        {
            case ASAAlpha:
                ctrl = _radioAlpha;
                radioKey = "STR_RADIO_ALPHA";
                goto showRadio;
            case ASABravo:
                ctrl = _radioBravo;
                radioKey = "STR_RADIO_BRAVO";
                goto showRadio;
            case ASACharlie:
                ctrl = _radioCharlie;
                radioKey = "STR_RADIO_CHARLIE";
                goto showRadio;
            case ASADelta:
                ctrl = _radioDelta;
                radioKey = "STR_RADIO_DELTA";
                goto showRadio;
            case ASAEcho:
                ctrl = _radioEcho;
                radioKey = "STR_RADIO_ECHO";
                goto showRadio;
            case ASAFoxtrot:
                ctrl = _radioFoxtrot;
                radioKey = "STR_RADIO_FOXTROT";
                goto showRadio;
            case ASAGolf:
                ctrl = _radioGolf;
                radioKey = "STR_RADIO_GOLF";
                goto showRadio;
            case ASAHotel:
                ctrl = _radioHotel;
                radioKey = "STR_RADIO_HOTEL";
                goto showRadio;
            case ASAIndia:
                ctrl = _radioIndia;
                radioKey = "STR_RADIO_INDIA";
                goto showRadio;
            case ASAJuliet:
                ctrl = _radioJuliet;
                radioKey = "STR_RADIO_JULIET";
                goto showRadio;
            showRadio:
            {
                RString text = det->GetText();
                if (stricmp(text, "null") == 0)
                {
                    continue;
                }
                text = Localize(text);
                if (text.GetLength() > 0)
                {
                    ctrl->SetText(text);
                }
                else
                {
                    ctrl->SetText(radioKey ? LocalizeString(radioKey) : RString());
                }
            }
        }
    }
}

RString DisplayMap::GetRadioTexts()
{
    CActiveText* radios[] = {_radioAlpha,   _radioBravo, _radioCharlie, _radioDelta, _radioEcho,
                             _radioFoxtrot, _radioGolf,  _radioHotel,   _radioIndia, _radioJuliet};
    RString result;
    for (CActiveText* radio : radios)
    {
        if (!radio)
        {
            continue;
        }
        if (result.GetLength() > 0)
        {
            result = result + RString("\n");
        }
        result = result + radio->GetText();
    }
    return result;
}

LSError DisplayMap::SerializeParams(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Compass", *(SerializeClass*)_compass, 1))
    PARAM_CHECK(ar.Serialize("Watch", *(SerializeClass*)_watch, 1))
    PARAM_CHECK(ar.Serialize("WalkieTalkie", *(SerializeClass*)_walkieTalkie, 1))
    PARAM_CHECK(ar.Serialize("Notepad", *(SerializeClass*)_notepad, 1))
    PARAM_CHECK(ar.Serialize("Warrant", *(SerializeClass*)_warrant, 1))
    PARAM_CHECK(ar.Serialize("GPS", *(SerializeClass*)_gps, 1))
    return LSOK;
}

void DisplayMap::LoadParams()
{
    ParamArchiveLoad ar(GetUserParams());
    ParamArchive arSubcls;
    if (!ar.OpenSubclass("MainMap", arSubcls))
    {
        return;
    }
    if (SerializeParams(arSubcls) != LSOK)
    {
        return;
    }
}

void DisplayMap::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(GetUserParams());

    ParamArchive arSubcls;
    if (!ar.OpenSubclass("MainMap", arSubcls))
    {
        return;
    }
    if (SerializeParams(arSubcls) != LSOK)
    {
        return;
    }
    if (ar.Save(GetUserParams()) != LSOK)
    {
        return;
    }
}

LSError DisplayMap::Serialize(ParamArchive& ar)
{
    if (_map)
        PARAM_CHECK(_map->Serialize(ar))
    return LSOK;
}

Control* DisplayMap::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_RADIO_ALPHA:
            _radioAlpha = new CActiveText(this, idc, cls);
            return _radioAlpha;
        case IDC_RADIO_BRAVO:
            _radioBravo = new CActiveText(this, idc, cls);
            return _radioBravo;
        case IDC_RADIO_CHARLIE:
            _radioCharlie = new CActiveText(this, idc, cls);
            return _radioCharlie;
        case IDC_RADIO_DELTA:
            _radioDelta = new CActiveText(this, idc, cls);
            return _radioDelta;
        case IDC_RADIO_ECHO:
            _radioEcho = new CActiveText(this, idc, cls);
            return _radioEcho;
        case IDC_RADIO_FOXTROT:
            _radioFoxtrot = new CActiveText(this, idc, cls);
            return _radioFoxtrot;
        case IDC_RADIO_GOLF:
            _radioGolf = new CActiveText(this, idc, cls);
            return _radioGolf;
        case IDC_RADIO_HOTEL:
            _radioHotel = new CActiveText(this, idc, cls);
            return _radioHotel;
        case IDC_RADIO_INDIA:
            _radioIndia = new CActiveText(this, idc, cls);
            return _radioIndia;
        case IDC_RADIO_JULIET:
            _radioJuliet = new CActiveText(this, idc, cls);
            return _radioJuliet;
        case IDC_MAP_NAME:
            _name = new CStatic(this, idc, cls);
            UpdateMissionName();
            return _name;
        case IDC_BRIEFING:
            _briefing = new CHTML(this, idc, cls);
            return _briefing;
        case IDC_MAP_NOTES:
            _bookmark1 = new CActiveText(this, idc, cls);
            return _bookmark1;
        case IDC_MAP_PLAN:
            _bookmark2 = new CActiveText(this, idc, cls);
            return _bookmark2;
        case IDC_MAP_GEAR:
            _bookmark3 = new CActiveText(this, idc, cls);
            return _bookmark3;
        case IDC_MAP_GROUP:
            _bookmark4 = new CActiveText(this, idc, cls);
            return _bookmark4;
        case IDC_MAP:
            _map = new CStaticMapMain(this, idc, cls);
            return _map;
        case IDC_WARRANT:
            return new CWarrant(this, idc, cls);
        case IDC_GPS:
            _gpsCtrl = new CStatic(this, idc, cls);
            return _gpsCtrl;
        case IDC_GETREADY_TITLE:
        {
            CStatic* text = new CStatic(this, idc, cls);
            UpdateGetReadyTitleText(text);
            return text;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void ActivateSensor(ArcadeSensorActivation activ)
{
    Person* player = GWorld->GetRealPlayer();
    if (!player || player->IsDammageDestroyed())
    {
        return;
    }

    for (int i = 0; i < sensorsMap.Size(); i++)
    {
        Vehicle* veh = sensorsMap[i];
        if (!veh)
        {
            continue;
        }
        Detector* det = dyn_cast<Detector>(veh);
        PoseidonAssert(det);
        if (det->GetActivationBy() == activ)
        {
            if (stricmp(det->GetText(), "null") == 0)
            {
                continue;
            }
            if (!det->IsActive() || det->IsRepeating())
            {
                det->DoActivate();
                GetNetworkManager().DetectorActivation(det, true);
            }
        }
    }
}

void DisplayMap::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_RADIO_ALPHA:
            ActivateSensor(ASAAlpha);
            break;
        case IDC_RADIO_BRAVO:
            ActivateSensor(ASABravo);
            break;
        case IDC_RADIO_CHARLIE:
            ActivateSensor(ASACharlie);
            break;
        case IDC_RADIO_DELTA:
            ActivateSensor(ASADelta);
            break;
        case IDC_RADIO_ECHO:
            ActivateSensor(ASAEcho);
            break;
        case IDC_RADIO_FOXTROT:
            ActivateSensor(ASAFoxtrot);
            break;
        case IDC_RADIO_GOLF:
            ActivateSensor(ASAGolf);
            break;
        case IDC_RADIO_HOTEL:
            ActivateSensor(ASAHotel);
            break;
        case IDC_RADIO_INDIA:
            ActivateSensor(ASAIndia);
            break;
        case IDC_RADIO_JULIET:
            ActivateSensor(ASAJuliet);
            break;
        case IDC_MAP_NOTES:
            SwitchBriefingSection("__BRIEFING");
            break;
        case IDC_MAP_PLAN:
            SwitchBriefingSection("__PLAN");
            break;
        case IDC_MAP_GEAR:
            //		if (!_debriefing)
            SwitchBriefingSection("Equipment");
            break;
        case IDC_MAP_GROUP:
            //		if (!_debriefing)
            SwitchBriefingSection("Group");
            break;
        case IDC_CANCEL:
            break;
    }
}

void DisplayMap::RemoveUnusableMagazines(UnitWeaponsInfo& info)
{
    for (int i = 0; i < MAGAZINE_SLOTS;)
    {
        Magazine* magazine = info.magazines[i];
        if (!magazine)
        {
            i++;
            continue;
        }
        MagazineType* type = magazine->_type;
        if ((type->_magazineType & MaskSlotItem) != 0)
        {
            int nItems = GetItemSlotsCount(type->_magazineType);
            if (!info.IsMagazineUsable(type))
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }
            i += nItems;
        }
        else if ((type->_magazineType & MaskSlotHandGunItem) != 0)
        {
            int nItems = GetHandGunItemSlotsCount(type->_magazineType);
            if (!info.IsMagazineUsable(type))
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }
            i += nItems;
        }
    }
}

void DisplayMap::AddUsableMagazines(UnitWeaponsInfo& info, const WeaponType* weapon, int from, int to)
{
    saturateMin(to, GetItemSlotsCount(info.weaponSlots));
    if (from >= to)
    {
        return;
    }

    if (weapon->_muzzles.Size() == 0)
    {
        return;
    }
    const MuzzleType* muzzle = weapon->_muzzles[0];

    if (muzzle->_magazines.Size() == 0)
    {
        return;
    }
    const MagazineType* type = muzzle->_magazines[0];

    int nSlots = GetItemSlotsCount(type->_magazineType);
    for (int i = from; i <= to - nSlots;)
    {
        bool free = true;
        for (int j = 0; j < nSlots; j++)
        {
            if (info.magazines[i + j])
            {
                free = false;
                break;
            }
        }
        if (free)
        {
            // find magazine
            Ref<Magazine> magazine;
            for (int k = 0; k < _weaponsInfo._magazinesPool.Size(); k++)
            {
                if (_weaponsInfo._magazinesPool[k]->_type == type)
                {
                    magazine = _weaponsInfo._magazinesPool[k];
                    _weaponsInfo._magazinesPool.Delete(k);
                    break;
                }
            }
            if (!magazine)
            {
                return;
            }
            for (int j = 0; j < nSlots; j++)
            {
                info.magazines[i++] = magazine;
            }
        }
        else
        {
            i++;
        }
    }
}

void DisplayMap::AddUsableHandGunMagazines(UnitWeaponsInfo& info, const WeaponType* weapon, int from, int to)
{
    saturateMin(to, 10 + GetHandGunItemSlotsCount(info.weaponSlots));
    if (from >= to)
    {
        return;
    }

    if (weapon->_muzzles.Size() == 0)
    {
        return;
    }
    const MuzzleType* muzzle = weapon->_muzzles[0];

    if (muzzle->_magazines.Size() == 0)
    {
        return;
    }
    const MagazineType* type = muzzle->_magazines[0];

    int nSlots = GetHandGunItemSlotsCount(type->_magazineType);
    for (int i = from; i <= to - nSlots;)
    {
        bool free = true;
        for (int j = 0; j < nSlots; j++)
        {
            if (info.magazines[i + j])
            {
                free = false;
                break;
            }
        }
        if (free)
        {
            // find magazine
            Ref<Magazine> magazine;
            for (int k = 0; k < _weaponsInfo._magazinesPool.Size(); k++)
            {
                if (_weaponsInfo._magazinesPool[k]->_type == type)
                {
                    magazine = _weaponsInfo._magazinesPool[k];
                    _weaponsInfo._magazinesPool.Delete(k);
                    break;
                }
            }
            if (!magazine)
            {
                return;
            }
            for (int j = 0; j < nSlots; j++)
            {
                info.magazines[i++] = magazine;
            }
        }
        else
        {
            i++;
        }
    }
}

void DisplayMap::OnHTMLLink(int idc, RString link)
{
    const char* ptr = link;
    const char* name = "marker:";
    int n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        for (int i = 0; i < markersMap.Size(); i++)
        {
            ArcadeMarkerInfo& mInfo = markersMap[i];
            if (stricmp(ptr, mInfo.name) == 0)
            {
                float invSizeLand = 1.0 / (LandGrid * LandRange);
                Vector3 curPos = _map->GetCenter();
                float curScale = _map->GetScale();
                float endScale = curScale;
                float diff = curPos.Distance(mInfo.position);
                float scale = 2.0 * diff * invSizeLand;
                int sizeAnim = _map->_animation.Size();
                if (sizeAnim > 0)
                {
                    // animation interrupted, save end scale
                    MapAnimationPhase& phase = _map->_animation[sizeAnim - 1];
                    endScale = phase.scale;
                }
                _map->ClearAnimation();
                if (scale > curScale)
                {
                    float time = log(scale / curScale);
                    _map->AddAnimationPhase(time, scale, curPos);
                    _map->AddAnimationPhase(1.0, scale, mInfo.position);
                }
                else
                {
                    float time = diff * invSizeLand / curScale;
                    _map->AddAnimationPhase(time, curScale, mInfo.position);
                    scale = curScale;
                }
                if (scale > endScale)
                {
                    float time = log(scale / endScale);
                    _map->AddAnimationPhase(time, endScale, mInfo.position);
                }
                _map->CreateInterpolator();

                _map->SetActiveMarker(i);
                return;
            }
        }
        return;
    }

    if (_selectWeapons)
    {
        name = "slot:";
        n = strlen(name);
        if (strnicmp(ptr, name, n) == 0)
        {
            ptr += n;
            _currentSlot = *ptr - 'A';
            CreateWeaponsPoolPage(_currentSlot);
            SwitchBriefingSection("Pool");
        }
    }

    name = "gear:prev";
    if (stricmp(ptr, name) == 0)
    {
        _currentUnit--;
        CreateWeaponsPage();
        SwitchBriefingSection("Equipment");
        return;
    }

    name = "gear:next";
    if (stricmp(ptr, name) == 0)
    {
        _currentUnit++;
        CreateWeaponsPage();
        SwitchBriefingSection("Equipment");
        return;
    }

    name = "gear:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        int newUnitID = atoi(ptr);
        int index = -1;
        for (int i = 0; i < _weaponsInfo._weapons.Size(); i++)
        {
            const UnitWeaponsInfo& info = _weaponsInfo._weapons[i];
            if (info.unit && info.unit->ID() == newUnitID)
            {
                index = i;
            }
        }
        if (index >= 0)
        {
            _currentUnit = index;
            CreateWeaponsPage();
            SwitchBriefingSection("Equipment");
        }
        return;
    }

    name = "weapon:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        if (stricmp(ptr, "cancel") != 0)
        {
            UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
            WeaponType* weapon = info.weapons[_currentSlot];
            if (weapon)
            {
                _weaponsInfo._weaponsPool.Add(weapon);
                info.RemoveWeapon(weapon);
                RemoveUnusableMagazines(info);
            }

            if (*ptr)
            {
                // add new weapon
                Ref<WeaponType> weapon;
                for (int i = 0; i < _weaponsInfo._weaponsPool.Size(); i++)
                {
                    if (stricmp(ptr, _weaponsInfo._weaponsPool[i]->GetName()) == 0)
                    {
                        weapon = _weaponsInfo._weaponsPool[i];
                        _weaponsInfo._weaponsPool.Delete(i);
                        break;
                    }
                }
                PoseidonAssert(weapon);
                if (weapon->_weaponType & MaskSlotBinocular)
                {
                    if (info.weaponSlots & MaskSlotBinocular)
                    {
                        PoseidonAssert(!info.weapons[_currentSlot]);
                        info.weapons[_currentSlot] = weapon;
                    }
                    else
                    {
                        // FIX: Return to pool
                        _weaponsInfo._weaponsPool.Add(weapon);
                    }
                }
                else if ((weapon->_weaponType & info.weaponSlots) == weapon->_weaponType)
                {
                    if (weapon->_weaponType & MaskSlotPrimary)
                    {
                        if (info.weapons[0])
                        {
                            _weaponsInfo._weaponsPool.Add(info.weapons[0]);
                            info.RemoveWeapon(info.weapons[0]);
                            RemoveUnusableMagazines(info);
                        }
                        info.weapons[0] = weapon;
                        AddUsableMagazines(info, weapon, 0, 4);
                    }
                    if (weapon->_weaponType & MaskSlotSecondary)
                    {
                        if (info.weapons[1])
                        {
                            _weaponsInfo._weaponsPool.Add(info.weapons[1]);
                            info.RemoveWeapon(info.weapons[1]);
                            RemoveUnusableMagazines(info);
                        }
                        info.weapons[1] = weapon;
                        AddUsableMagazines(info, weapon, 4, 10);
                    }
                    if (weapon->_weaponType & MaskSlotHandGun)
                    {
                        if (info.weapons[4])
                        {
                            _weaponsInfo._weaponsPool.Add(info.weapons[4]);
                            info.RemoveWeapon(info.weapons[4]);
                            RemoveUnusableMagazines(info);
                        }
                        info.weapons[4] = weapon;
                        AddUsableHandGunMagazines(info, weapon, 10, MAGAZINE_SLOTS);
                    }
                }
                else
                {
                    // does not fit into slots
                    // FIX: Return to pool
                    _weaponsInfo._weaponsPool.Add(weapon);
                }
            }
            CreateWeaponsPage();
        }
        SwitchBriefingSection("Equipment");
        return;
    }

    name = "magazine:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        if (stricmp(ptr, "cancel") != 0)
        {
            UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
            int slot = _currentSlot - WEAPON_SLOTS;
            Magazine* magazine = info.magazines[slot];
            if (magazine)
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }

            if (*ptr)
            {
                MagazineType* type = MagazineTypes.New(ptr);
                bool handGun = (type->_magazineType & MaskSlotHandGunItem) != 0;

                int maxItems = handGun
                                   ? GetItemSlotsCount(info.weaponSlots) + GetHandGunItemSlotsCount(info.weaponSlots)
                                   : GetItemSlotsCount(info.weaponSlots);

                Ref<Magazine> magazine;

                int slots =
                    handGun ? GetHandGunItemSlotsCount(type->_magazineType) : GetItemSlotsCount(type->_magazineType);
                if (type && slots <= maxItems)
                {
                    magazine = _weaponsInfo.RemovePoolMagazine(type);
                }

                if (magazine)
                {
                    saturateMin(slot, maxItems - slots);
                    for (int i = slot; i < slot + slots; i++)
                    {
                        if (info.magazines[i])
                        {
                            _weaponsInfo._magazinesPool.Add(info.magazines[i]);
                            info.RemoveMagazine(info.magazines[i]);
                        }
                        info.magazines[i] = magazine;
                    }
                }
            }
            CreateWeaponsPage();
        }
        SwitchBriefingSection("Equipment");
        return;
    }

    name = "fill1:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        MagazineType* type = MagazineTypes.New(ptr);
        if (!type)
        {
            return;
        }

        UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
        int maxItems = GetItemSlotsCount(info.weaponSlots);
        int iCnt = maxItems;
        saturateMin(iCnt, 4);

        int slots = GetItemSlotsCount(type->_magazineType);
        if (iCnt <= slots)
        {
            return;
        }

        // remove all magazines from items 1
        int iMin = 0, iMax = iMin + iCnt;
        for (int i = iMin; i < iMax; i++)
        {
            Magazine* magazine = info.magazines[i];
            if (magazine)
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }
        }

        // add magazines
        for (int i = iMin; i < iMax - slots + 1; i += slots)
        {
            Ref<Magazine> magazine = _weaponsInfo.RemovePoolMagazine(type);
            if (!magazine)
            {
                break;
            }
            for (int j = i; j < i + slots; j++)
            {
                info.magazines[j] = magazine;
            }
        }
        CreateWeaponsPage();
        return;
    }

    name = "fill2:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        MagazineType* type = MagazineTypes.New(ptr);
        if (!type)
        {
            return;
        }

        UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
        int maxItems = GetItemSlotsCount(info.weaponSlots);
        int iCnt = maxItems - 4;

        int slots = GetItemSlotsCount(type->_magazineType);
        if (iCnt <= slots)
        {
            return;
        }

        // remove all magazines from items 1
        int iMin = 4, iMax = iMin + iCnt;
        for (int i = iMin; i < iMax; i++)
        {
            Magazine* magazine = info.magazines[i];
            if (magazine)
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }
        }

        // add magazines
        for (int i = iMin; i < iMax - slots + 1; i += slots)
        {
            Ref<Magazine> magazine = _weaponsInfo.RemovePoolMagazine(type);
            if (!magazine)
            {
                break;
            }
            for (int j = i; j < i + slots; j++)
            {
                info.magazines[j] = magazine;
            }
        }
        CreateWeaponsPage();
        return;
    }

    name = "fill3:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        MagazineType* type = MagazineTypes.New(ptr);
        if (!type)
        {
            return;
        }

        UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
        int maxItems = GetHandGunItemSlotsCount(info.weaponSlots);
        int iCnt = maxItems;

        int slots = GetHandGunItemSlotsCount(type->_magazineType);
        if (iCnt <= slots)
        {
            return;
        }

        // remove all magazines from items 1
        int iMin = 10, iMax = iMin + iCnt;
        for (int i = iMin; i < iMax; i++)
        {
            Magazine* magazine = info.magazines[i];
            if (magazine)
            {
                _weaponsInfo._magazinesPool.Add(magazine);
                info.RemoveMagazine(magazine);
            }
        }

        // add magazines
        for (int i = iMin; i < iMax - slots + 1; i += slots)
        {
            Ref<Magazine> magazine = _weaponsInfo.RemovePoolMagazine(type);
            if (!magazine)
            {
                break;
            }
            for (int j = i; j < i + slots; j++)
            {
                info.magazines[j] = magazine;
            }
        }
        CreateWeaponsPage();
        return;
    }

    name = "dropweapon:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
        AIUnit* unit = info.unit;
        if (unit && unit->GetPerson() == GWorld->GetRealPlayer())
        {
            // create action and perform on unit
            UIAction action;
            action.type = ATDropWeapon;
            action.target = nullptr;
            action.param = 0;
            action.param2 = 0;
            action.param3 = ptr;
            action.priority = 0;
            action.showWindow = false;
            action.hideOnUse = false;
            action.Process(unit);
            //			UpdateWeaponsInBriefing();
        }
        else if (unit && unit->GetGroup())
        {
            // send command to unit
            Command cmd;
            cmd._message = Command::Action;
            cmd._action = ATDropWeapon;
            cmd._target = nullptr;
            cmd._destination = unit->Position();
            cmd._param = 0;
            cmd._param2 = 0;
            cmd._param3 = ptr;
            cmd._time = Glob.time + 480.0;
            unit->GetGroup()->SendAutoCommandToUnit(cmd, unit, true);
        }
        return;
    }

    name = "dropmagazine:";
    n = strlen(name);
    if (strnicmp(ptr, name, n) == 0)
    {
        ptr += n;
        UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];
        AIUnit* unit = info.unit;
        if (unit && unit->GetPerson() == GWorld->GetRealPlayer())
        {
            // create action and perform on unit
            UIAction action;
            action.type = ATDropMagazine;
            action.target = nullptr;
            action.param = 0;
            action.param2 = 0;
            action.param3 = ptr;
            action.priority = 0;
            action.showWindow = false;
            action.hideOnUse = false;
            action.Process(unit);
            //			UpdateWeaponsInBriefing();
        }
        else if (unit && unit->GetGroup())
        {
            // send command to unit
            Command cmd;
            cmd._message = Command::Action;
            cmd._action = ATDropMagazine;
            cmd._target = nullptr;
            cmd._destination = unit->Position();
            cmd._param = 0;
            cmd._param2 = 0;
            cmd._param3 = ptr;
            cmd._time = Glob.time + 480.0;
            unit->GetGroup()->SendAutoCommandToUnit(cmd, unit, true);
        }
        return;
    }
}

static RString GetTooltip(UnitWeaponsInfo& info, RString href)
{
    const char* name = "fill1:";
    int n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_FILL_ALL_SLOTS);
    }

    name = "fill2:";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_FILL_ALL_SLOTS);
    }

    name = "fill3:";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_FILL_ALL_SLOTS);
    }

    name = "#eq_";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_INFORMATION);
    }

    name = "dropweapon:";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_DROP);
    }

    name = "dropmagazine:";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        return LocalizeString(IDS_TOOLTIP_DROP);
    }

    name = "slot:";
    n = strlen(name);
    if (strnicmp(href, name, n) == 0)
    {
        const char* ptr = href;
        int slot = *(ptr + n) - 'A';

        if (slot < WEAPON_SLOTS)
        {
            return RString();
        }
        else
        {
            const Magazine* magazine = info.magazines[slot - WEAPON_SLOTS];
            const MagazineType* type = magazine ? magazine->_type : nullptr;
            if (type)
            {
                return type->GetDisplayName();
            }
            else
            {
                return LocalizeString(IDS_EMPTY_SLOT);
            }
        }
    }

    return RString();
}

void DisplayMap::OnDraw(EntityAI* vehicle, float alpha)
{
    // Compass orientation
    Camera* camera = GScene->GetCamera();
    if (camera)
    {
        Vector3Val dir = camera->Direction();
        _compass->SetOrient(Vector3(dir.X(), dir.Z(), dir.Y()), Vector3(0, 0, -1));
    }
    // Radio slots
    SetRadioText();
    // Show / hide mission name
    _name->ShowCtrl(_briefing->ActiveBookmark() >= 0);
    // GPS position
    Object* cameraOn = GWorld->CameraOn();
    if (cameraOn)
    {
        char buffer[16];
        PositionToAA11(cameraOn->Position(), buffer);
        _gpsCtrl->SetText(buffer);
    }
    else
    {
        _gpsCtrl->SetText("----");
    }

    // tooltip
    RString tooltip;
    const HTMLField* field = _briefing->GetActiveField();
    if (_currentUnit >= 0 && field)
    {
        tooltip = GetTooltip(_weaponsInfo._weapons[_currentUnit], field->href);
    }
    _briefing->SetTooltip(tooltip);

    // FIX: do not show notepad for dead units (not for unassigned JIP players)
    Person* player = GWorld->GetRealPlayer();
    AIUnit* playerUnit = player ? player->Brain() : nullptr;
    if (playerUnit && playerUnit->GetLifeState() == AIUnit::LSDead)
    {
        ShowNotepad(false);
    }

    Display::OnDraw(vehicle, alpha);
}

void DisplayMap::OnSimulate(EntityAI* vehicle)
{
    Display::OnSimulate(vehicle);

    // JIP deferred refresh: unit wasn't assigned at init, retry now
    if (_needsHUDRefresh && GWorld->FocusOn())
    {
        _needsHUDRefresh = false;
        // Re-resolve briefing section now that we know the unit's side
        AIUnit* unit = GWorld->FocusOn();
        if (_briefing && _briefing->FindSection("__BRIEFING") < 0)
        {
            int section = FindSectionForUnit(_briefing, "Main", unit);
            if (section >= 0)
                _briefing->AddName(section, "__BRIEFING");
        }
        // Re-resolve plan section
        UpdatePlan();
        // Populate gear and group tabs
        UpdateWeaponsInBriefing();
        UpdateUnitsInBriefing();
    }
    if (_map && _map->IsVisible())
    {
        _map->ProcessCheats();

        float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
        float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;

        saturate(mouseX, 0, 1);
        saturate(mouseY, 0, 1);

        // automatic map movement on edges
        _map->ScrollOnEdges(mouseX, mouseY);

        IControl* ctrl = GetCtrl(mouseX, mouseY);
        if (ctrl && ctrl == _map)
        {
            if (_map->_dragging || _map->_selecting)
            {
                if (_cursor != CursorMove)
                {
                    _cursor = CursorMove;
                    SetCursor("Move");
                }
            }
            else if (_map->_moving)
            {
                if (_cursor != CursorScroll)
                {
                    _cursor = CursorScroll;
                    SetCursor("Scroll");
                }
            }
            else
            {
                if (_cursor != CursorTrack)
                {
                    _cursor = CursorTrack;
                    SetCursor("Track");
                }
            }
        }
        else
        {
            if (_cursor != CursorArrow)
            {
                _cursor = CursorArrow;
                SetCursor("Arrow");
            }
        }
    }
}

void DisplayMap::AdjustMapVisibleRect()
{
    float x = _map->X();
    float y = _map->Y();
    float w = _map->W();
    float h = _map->H();

    if (!IsShownNotepad())
    {
        _map->SetVisibleRect(x, y, w, h);
        return;
    }

    // adjust map visible rectangle
    Vector3 min = _notepad->PositionModelToWorld(_notepad->GetShape()->Min());
    Vector3 max = _notepad->PositionModelToWorld(_notepad->GetShape()->Max());
    DrawCoord pt, ptMin, ptMax;
    ptMin.x = 1;
    ptMin.y = 1;
    ptMax.x = 0;
    ptMax.y = 0;

    pt = SceneToScreen(min);
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(min.X(), min.Y(), max.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(min.X(), max.Y(), min.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(min.X(), max.Y(), max.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(max.X(), min.Y(), min.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(max.X(), min.Y(), max.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(Vector3(max.X(), max.Y(), min.Z()));
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);
    pt = SceneToScreen(max);
    saturateMin(ptMin.x, pt.x);
    saturateMin(ptMin.y, pt.y);
    saturateMax(ptMax.x, pt.x);
    saturateMax(ptMax.y, pt.y);

    float aLeft = ptMin.x > x ? (ptMin.x - x) * h : 0;
    float aRight = ptMax.x < x + w ? (x + w - ptMax.x) * h : 0;
    float aTop = ptMin.y > y ? (ptMin.y - y) * w : 0;
    float aBottom = ptMax.y < y + h ? (y + h - ptMax.y) * w : 0;

    if (aLeft > aRight)
    {
        if (aLeft > aTop)
        {
            if (aLeft > aBottom)
            { // left
                _map->SetVisibleRect(x, y, ptMin.x - x, h);
            }
            else
            { // bottom
                _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
            }
        }
        else if (aTop > aBottom)
        { // top
            _map->SetVisibleRect(x, y, w, ptMin.y - y);
        }
        else
        { // bottom
            _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
        }
    }
    else if (aRight > aTop)
    {
        if (aRight > aBottom)
        { // right
            _map->SetVisibleRect(ptMax.x, y, x + w - ptMax.x, h);
        }
        else
        { // bottom
            _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
        }
    }
    else if (aTop > aBottom)
    { // top
        _map->SetVisibleRect(x, y, w, ptMin.y - y);
    }
    else if (aBottom > 0)
    { // bottom
        _map->SetVisibleRect(x, ptMax.y, w, y + h - ptMax.y);
    }
    else
    {
        _map->SetVisibleRect(x, y, w, h);
    }
}

void DisplayMap::OnChildDestroyed(int idd, int exit)
{
    if (idd == IDD_INSERT_MARKER && exit == IDC_OK)
    {
        DisplayInsertMarker* display = dynamic_cast<DisplayInsertMarker*>((ControlsContainer*)_child);
        PoseidonAssert(display);

        int index = markersMap.Add();
        ArcadeMarkerInfo& marker = markersMap[index];
        marker.position = _map->ScreenToWorld(DrawCoord(display->_x, display->_y));
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "_USER_DEFINED #%d/%d", GetNetworkManager().GetPlayer(), index);
        marker.name = buffer;
        marker.text = display->_text;
        marker.markerType = MTIcon;
        const ParamEntry& markers = Pars >> "CfgMarkers";
        if (display->_picture >= 0 && display->_picture < markers.GetEntryCount())
        {
            const ParamEntry& markerCfg = markers.GetEntry(display->_picture);
            marker.type = markerCfg.GetName();
            marker.OnTypeChanged();
        }

        const ParamEntry& colors = Pars >> "CfgMarkerColors";
        if (display->_color >= 0 && display->_color < colors.GetEntryCount())
        {
            const ParamEntry& colorCfg = colors.GetEntry(display->_color);
            marker.colorName = colorCfg.GetName();
            marker.OnColorChanged();
        }

        marker.fillName = "Solid";
        marker.OnFillChanged();
        marker.a = 1;
        marker.b = 1;
        ChatChannel channel = ActualChatChannel();
        Person* veh = GWorld->GetRealPlayer();
        AIUnit* unit = veh ? veh->Brain() : nullptr;
        if (channel == CCGlobal || unit)
        {
            SendMarker(channel, unit, marker);
        }
    }
    Display::OnChildDestroyed(idd, exit);
}
