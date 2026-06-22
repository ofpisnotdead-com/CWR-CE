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
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

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

// Warrant control

CWarrant::CWarrant(ControlsContainer* parent, int idc, const ParamEntry& cls) : Control(parent, CT_USER, idc, cls)
{
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }
    _color = GetPackedColor(cls >> "color");
}

inline PackedColor ModAlpha(PackedColor color, float alpha)
{
    int a = toIntFloor(alpha * color.A8());
    saturate(a, 0, 255);
    return PackedColorRGB(color, a);
}

void CWarrant::OnDraw(float alpha)
{
    int w = GLOB_ENGINE->Width2D();
    int h = GLOB_ENGINE->Height2D();
    float size = _scale * _size;
    PackedColor color = ModAlpha(_color, alpha);

    Person* soldier = GWorld->GetRealPlayer();
    AIUnit* unit = soldier ? soldier->Brain() : nullptr;
    AIStatsCampaign& stats = GStats._campaign;
    // AIUnitHeader &info = stats._playerInfo;

    float height = size;
    float top = _y;
    char buffer[256];

    // rank, name
    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_MAIN_RANK), (const char*)LocalizeString(IDS_PRIVATE));
    GEngine->DrawText(Point2DFloat(_x, top), size, _font, color, buffer);
    top += height;

    // assignment
    snprintf(buffer, sizeof(buffer), "Assignement: %s", "infantry");
    GEngine->DrawText(Point2DFloat(_x, top), size, _font, color, buffer);
    top += height;

    // number in group
    if (unit)
    {
        snprintf(buffer, sizeof(buffer), "Number in group: %d", unit->ID());
        GEngine->DrawText(Point2DFloat(_x, top), size, _font, color, buffer);
        top += height;
    }

    top += height;

    // in combat
    snprintf(buffer, sizeof(buffer), "In combat: %d minutes", toInt(stats._inCombat / 60.0));
    GEngine->DrawText(Point2DFloat(_x, top), size, _font, color, buffer);
    top += height;

    top += height;

    // kills
    GEngine->DrawText(Point2DFloat(_x, top), size, _font, color, "Kills:");
    top += height;
    // draw table
    float w1 = 0.2 * _w;
    float ht = 0.2 * height;
    float h1 = height + 2.0 * ht;
    float x0 = _x, xx0 = x0 * w;
    float x1 = x0 + w1, xx1 = x1 * w;
    float x2 = x1 + w1, xx2 = x2 * w;
    float x3 = x2 + w1, xx3 = x3 * w;
    float x4 = x3 + w1, xx4 = x4 * w;
    float x5 = x4 + w1, xx5 = x5 * w;
    float y0 = top, yy0 = y0 * h;
    float y1 = y0 + h1, yy1 = y1 * h;
    float y2 = y1 + h1, yy2 = y2 * h;
    float y3 = y2 + h1, yy3 = y3 * h;
    float y4 = y3 + h1, yy4 = y4 * h;
    GEngine->DrawLine(Line2DPixel(xx1, yy0, xx5, yy0), color, color);
    GEngine->DrawLine(Line2DPixel(xx0, yy1, xx5, yy1), color, color);
    GEngine->DrawLine(Line2DPixel(xx0, yy2, xx5, yy2), color, color);
    GEngine->DrawLine(Line2DPixel(xx0, yy3, xx5, yy3), color, color);
    GEngine->DrawLine(Line2DPixel(xx0, yy4, xx5, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx0, yy1, xx0, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx1, yy0, xx1, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx2, yy0, xx2, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx3, yy0, xx3, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx4, yy0, xx4, yy4), color, color);
    GEngine->DrawLine(Line2DPixel(xx5, yy0, xx5, yy4), color, color);
    float wt = GEngine->GetTextWidth(size, _font, "Enemies");
    GEngine->DrawText(Point2DFloat(x0 + 0.5 * (w1 - wt), y1 + ht), size, _font, color, "Enemies");
    wt = GEngine->GetTextWidth(size, _font, "Friends");
    GEngine->DrawText(Point2DFloat(x0 + 0.5 * (w1 - wt), y2 + ht), size, _font, color, "Friends");
    wt = GEngine->GetTextWidth(size, _font, "Civilians");
    GEngine->DrawText(Point2DFloat(x0 + 0.5 * (w1 - wt), y3 + ht), size, _font, color, "Civilians");
    wt = GEngine->GetTextWidth(size, _font, "Infantry");
    GEngine->DrawText(Point2DFloat(x1 + 0.5 * (w1 - wt), y0 + ht), size, _font, color, "Infantry");
    wt = GEngine->GetTextWidth(size, _font, "Mobile");
    GEngine->DrawText(Point2DFloat(x2 + 0.5 * (w1 - wt), y0 + ht), size, _font, color, "Mobile");
    wt = GEngine->GetTextWidth(size, _font, "Armored");
    GEngine->DrawText(Point2DFloat(x3 + 0.5 * (w1 - wt), y0 + ht), size, _font, color, "Armored");
    wt = GEngine->GetTextWidth(size, _font, "Air");
    GEngine->DrawText(Point2DFloat(x4 + 0.5 * (w1 - wt), y0 + ht), size, _font, color, "Air");
    int n = stats._kills[SKEnemyInfantry];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x1 + 0.5 * (w1 - wt), y1 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKEnemySoft];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x2 + 0.5 * (w1 - wt), y1 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKEnemyArmor];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x3 + 0.5 * (w1 - wt), y1 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKEnemyAir];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x4 + 0.5 * (w1 - wt), y1 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKFriendlyInfantry];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x1 + 0.5 * (w1 - wt), y2 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKFriendlySoft];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x2 + 0.5 * (w1 - wt), y2 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKFriendlyArmor];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x3 + 0.5 * (w1 - wt), y2 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKFriendlyAir];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x4 + 0.5 * (w1 - wt), y2 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKCivilianInfantry];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x1 + 0.5 * (w1 - wt), y3 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKCivilianSoft];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x2 + 0.5 * (w1 - wt), y3 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKCivilianArmor];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x3 + 0.5 * (w1 - wt), y3 + ht), size, _font, color, "%d", n);
    }
    n = stats._kills[SKCivilianAir];
    if (n > 0)
    {
        wt = GEngine->GetTextWidthF(size, _font, "%d", n);
        GEngine->DrawTextF(Point2DFloat(x4 + 0.5 * (w1 - wt), y3 + ht), size, _font, color, "%d", n);
    }
}

// Main Map display

namespace Poseidon
{
WeatherState GetWeatherState(float overcast)
{
    if (overcast < 0.2)
    {
        return WSClear;
    }
    else if (overcast < 0.4)
    {
        return WSCloudly;
    }
    else if (overcast < 0.6)
    {
        return WSOvercast;
    }
    else if (overcast < 0.8)
    {
        return WSRainy;
    }
    else
    {
        return WSStormy;
    }
}
} // namespace Poseidon

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(ObjectiveStatus dummy)
{
    static const EnumName ObjectiveStatusNames[] = {EnumName(OSActive, "ACTIVE"), EnumName(OSDone, "DONE"),
                                                    EnumName(OSFailed, "FAILED"), EnumName(OSHidden, "HIDDEN"),
                                                    EnumName()};
    return ObjectiveStatusNames;
}

LSError UnitWeaponsInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Unit", unit, 1))
    PARAM_CHECK(ar.Serialize("name", name, 1))
    PARAM_CHECK(ar.Serialize("weaponSlots", weaponSlots, 1))
    for (int i = 0; i < WEAPON_SLOTS; i++)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Weapon%d", i);
        PARAM_CHECK(ar.Serialize(buffer, weapons[i], 1))
    }
    for (int i = 0; i < MAGAZINE_SLOTS; i++)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Magazine%d", i);
        PARAM_CHECK(ar.Serialize(buffer, magazines[i], 1))
    }
    return LSOK;
}

Ref<Magazine> WeaponsInfo::RemovePoolMagazine(const MagazineType* type)
{
    // add new magazine
    for (int i = 0; i < _magazinesPool.Size(); i++)
    {
        if (_magazinesPool[i]->_type == type)
        {
            Ref<Magazine> magazine = _magazinesPool[i];
            _magazinesPool.Delete(i);
            return magazine;
        }
    }
    return nullptr;
}

bool WeaponsInfo::Load(RString filename)
{
    ParamArchiveLoad ar;
    if (!ar.LoadBin(filename) && ar.Load(filename) != LSOK)
    {
        return false;
    }
    ar.FirstPass();
    if (Serialize(ar) != LSOK)
    {
        return false;
    }
    ar.SecondPass();
    if (Serialize(ar) != LSOK)
    {
        return false;
    }

    // assign new ID to all magazines
    for (int i = 0; i < _weapons.Size(); i++)
    {
        UnitWeaponsInfo& info = _weapons[i];
        for (int j = 0; j < MAGAZINE_SLOTS;)
        {
            Magazine* magazine = info.magazines[j++];
            if (!magazine)
            {
                continue;
            }
            int oldId = magazine->_id;
            magazine->_id = GWorld->GetMagazineID();
            while (j < MAGAZINE_SLOTS)
            {
                Magazine* m = info.magazines[j];
                if (!m || m->_id != oldId)
                {
                    break;
                }
                info.magazines[j++] = magazine;
            }
        }
    }

    for (int j = 0; j < _magazinesPool.Size();)
    {
        Magazine* magazine = _magazinesPool[j++];
        if (!magazine)
        {
            continue;
        }
        int oldId = magazine->_id;
        magazine->_id = GWorld->GetMagazineID();
        while (j < _magazinesPool.Size())
        {
            Magazine* m = _magazinesPool[j];
            if (!m || m->_id != oldId)
            {
                break;
            }
            _magazinesPool[j++] = magazine;
        }
    }

    return true;
}

bool WeaponsInfo::Save(RString filename)
{
    ParamArchiveSave ar(UserInfoVersion);
    if (Serialize(ar) != LSOK)
    {
        return false;
    }
#if _ENABLE_CHEATS
    return ar.Save(filename) == LSOK;
#else
    return ar.SaveBin(filename);
#endif
}

LSError WeaponsInfo::Serialize(ParamArchive& ar)
{
    ar.Serialize("Weapons", _weapons, 1);
    ar.Serialize("WeaponsPool", _weaponsPool, 1);
    ar.Serialize("MagazinesPool", _magazinesPool, 1);
    return LSOK;
}

bool WeaponsInfo::Import(const ParamEntry& superclass)
{
    const ParamEntry* cls = superclass.FindEntry("Weapons");
    if (cls)
    {
        int mask = MaskSlotPrimary | MaskSlotSecondary | MaskSlotBinocular | MaskSlotHandGun;
        for (int i = 0; i < cls->GetEntryCount(); i++)
        {
            const ParamEntry& entry = cls->GetEntry(i);
            Ref<WeaponType> weapon = WeaponTypes.New(entry.GetName());
            if (!weapon)
            {
                continue;
            }
            if (weapon->_scope < 2)
            {
                continue;
            }
            if ((weapon->_weaponType & mask) == 0)
            {
                continue;
            }
            int count = entry >> "count";
            for (int j = 0; j < count; j++)
            {
                _weaponsPool.Add(weapon);
            }
        }
    }
    cls = superclass.FindEntry("Magazines");
    if (cls)
    {
        int mask = MaskSlotItem | MaskSlotHandGunItem;
        for (int i = 0; i < cls->GetEntryCount(); i++)
        {
            const ParamEntry& entry = cls->GetEntry(i);
            Ref<MagazineType> type = MagazineTypes.New(entry.GetName());
            if (!type)
            {
                continue;
            }
            if (type->_scope < 2)
            {
                continue;
            }
            if ((type->_magazineType & mask) == 0)
            {
                continue;
            }
            int count = entry >> "count";
            for (int j = 0; j < count; j++)
            {
                Ref<Magazine> magazine = new Magazine(type);
                magazine->_ammo = type->_maxAmmo;
                magazine->_reloadMagazine = 0;
                magazine->_reload = 0;
                if (type->_modes.Size() > 0)
                {
                    float reload = 0.8 + 0.2 * GRandGen.RandomValue();
                    magazine->_reload = reload * type->_modes[0]->_reloadTime;
                }
                _magazinesPool.Add(magazine);
            }
        }
    }
    return true;
}

bool EnabledWeaponPool()
{
    // if (!IsCampaign()) return false;
    const ParamEntry* entry = ExtParsCampaign.FindEntry("weaponPool");
    if (!entry)
    {
        return false;
    }
    return (bool)(*entry);
}

void WeaponsInfo::Apply()
{
    // add weapons
    for (int i = 0; i < _weapons.Size(); i++)
    {
        UnitWeaponsInfo& info = _weapons[i];
        AIUnit* unit = info.unit;
        if (!unit)
        {
            continue;
        }
        Person* veh = unit->GetPerson();
        veh->RemoveAllWeapons();
        veh->RemoveAllMagazines();
        for (int j = 0; j < MAGAZINE_SLOTS; j++)
        {
            Ref<Magazine> magazine = info.magazines[j];
            if (!magazine)
            {
                continue;
            }
            info.RemoveMagazine(magazine);
            veh->AddMagazine(magazine);
        }
        for (int j = 0; j < WEAPON_SLOTS; j++)
        {
            Ref<WeaponType> weapon = info.weapons[j];
            if (!weapon)
            {
                continue;
            }
            info.RemoveWeapon(weapon);
            veh->AddWeapon(weapon);
        }
        veh->AddDefaultWeapons();
        if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == veh)
        {
            GWorld->UI()->ResetVehicle(veh);
        }

        veh->AutoReloadAll();

        // FIX
        if (veh->SelectedWeapon() < 0)
        {
            veh->SelectWeapon(veh->FirstWeapon(), true);
        }
        if (!veh->IsLocal())
        {
            GetNetworkManager().UpdateWeapons(veh);
        }
    }

    if (EnabledWeaponPool())
    {
        void CampaignSaveWeaponPool(WeaponsInfo & pool);
        CampaignSaveWeaponPool(*this);
    }

    _weapons.Clear();
}

DisplayMap::DisplayMap(ControlsContainer* parent, RString resource) : Display(parent)
{
    _alwaysShow = true;

    _cursor = CursorArrow;
    //	_debriefing = false;
    _selectWeapons = false;
    _currentSlot = -1;
    _needsHUDRefresh = (GWorld->FocusOn() == nullptr);

    Load(resource);
    // FIX
    AdjustMapVisibleRect();

    Init();
    ResetHUD();

    ShowMap(true);
    ShowCompass(true);
    ShowWatch(true);
    ShowWalkieTalkie(true);
    ShowNotepad(true);
    //	ShowWarrant(IsCampaign());
    ShowWarrant(false);
    ShowGPS(false);

    if (_langCbToken >= 0)
        UnregisterLanguageChangedCallback(_langCbToken);
    _langCbToken = RegisterLanguageChangedCallback(
        [this]()
        {
            RefreshLocalizedText();
            RefreshLanguage();
        });
}

void DisplayMap::SwitchBriefingSection(RString section)
{
    _briefing->SwitchSection(section);
}

DisplayMap::~DisplayMap()
{
    SaveParams();
}

void DisplayMap::UpdateMissionName()
{
    if (_name)
        _name->SetText(GetCurrentMissionDisplayName());
}

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

void DisplayMap::ReloadBriefingContent(RString activeSection)
{
    if (!_briefing)
        return;

    LoadLocalizedMissionHtml(_briefing, GetBriefingFile());

    AIUnit* unit = GWorld->FocusOn();
    int section = FindSectionForUnit(_briefing, "Main", unit);
    if (section < 0 && !unit)
    {
        const PlayerRole* role = GetNetworkManager().GetMyPlayerRole();
        if (role)
        {
            RString sideName;
            switch (role->side)
            {
                case TWest:
                    sideName = "Main.West";
                    break;
                case TEast:
                    sideName = "Main.East";
                    break;
                case TGuerrila:
                    sideName = "Main.Guerrila";
                    break;
                case TCivilian:
                    sideName = "Main.Civilian";
                    break;
                default:
                    break;
            }
            if (sideName.GetLength() > 0)
                section = _briefing->FindSection(sideName);
        }
    }
    if (section >= 0)
        _briefing->AddName(section, "__BRIEFING");

    UpdatePlan();
    _briefing->Load(GetEquipmentFile(), true);
    _briefing->AddBookmark("__PLAN");
    _briefing->AddBookmark("__BRIEFING");
    _briefing->AddBookmark("Equipment");
    _briefing->AddBookmark("Group");

    UpdateWeaponsInBriefing();
    UpdateUnitsInBriefing();

    if (activeSection.GetLength() > 0 && _briefing->FindSection(activeSection) >= 0)
    {
        SwitchBriefingSection(activeSection);
    }
    else if (_briefing->FindSection("__PLAN") >= 0)
    {
        SwitchBriefingSection("__PLAN");
    }
    else if (_briefing->FindSection("__BRIEFING") >= 0)
    {
        SwitchBriefingSection("__BRIEFING");
    }
    else
    {
        SwitchBriefingSection("Equipment");
    }
}

void DisplayMap::RefreshLanguage()
{
    UpdateMissionName();
    UpdateGetReadyTitleText(dynamic_cast<CStatic*>(GetCtrl(IDC_GETREADY_TITLE)));
    ReloadBriefingContent(GetCurrentHtmlSectionName(_briefing));
    SetRadioText();
}

bool DisplayMap::IsShownMap() const
{
    return _map->IsVisible();
}

bool DisplayMap::IsShownCompass() const
{
    return _compass->IsVisible();
}

bool DisplayMap::IsShownWatch() const
{
    return _watch->IsVisible();
}

bool DisplayMap::IsShownWalkieTalkie() const
{
    return _walkieTalkie->IsVisible();
}

bool DisplayMap::IsShownNotepad() const
{
    return _notepad->IsVisible();
}

bool DisplayMap::IsShownWarrant() const
{
    return _warrant->IsVisible();
}

bool DisplayMap::IsShownGPS() const
{
    return _gps->IsVisible();
}

void DisplayMap::ShowMap(bool show)
{
    _map->ShowCtrl(show);
}

void DisplayMap::ShowCompass(bool show)
{
    _compass->ShowCtrl(show);
}

void DisplayMap::ShowWatch(bool show)
{
    _watch->ShowCtrl(show);
}

void DisplayMap::ShowWalkieTalkie(bool show)
{
    _walkieTalkie->ShowCtrl(show);
}

void DisplayMap::ShowNotepad(bool show)
{
    _notepad->ShowCtrl(show);
    AdjustMapVisibleRect();
}

void DisplayMap::ShowWarrant(bool show)
{
    _warrant->ShowCtrl(show);
}

void DisplayMap::ShowGPS(bool show)
{
    _gps->ShowCtrl(show);
}
RString GetEquipmentFile()
{
    // Preference order: language-specific UTF-8 → language-specific
    // legacy codepage → UTF-8 default (tokenized via STR_EQ_* in the
    // remaster shard) → legacy English default.  UTF-8 variants win
    // so a `equipment.utf8.html` ships once and resolves all 8 UI
    // languages via the stringtable expansion in CHTMLContainer::Load.
    RString prefix("dtaExt\\equip\\equipment.");

    RString name = prefix + GLanguage + RString(".utf8.html");
    if (QIFStreamB::FileExist(name))
        return name;

    name = prefix + GLanguage + RString(".html");
    if (QIFStreamB::FileExist(name))
        return name;

    name = prefix + RString("utf8.html");
    if (QIFStreamB::FileExist(name))
        return name;

    name = prefix + RString("html");
    if (QIFStreamB::FileExist(name))
        return name;

    return RString();
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

void DisplayMap::Init()
{
    // setting default values
    _map->SetScale(-1);
    _map->Center();
    _map->Reset();

    // load values
    LoadParams();

    ReloadBriefingContent();
}

void DisplayMap::ResetHUD()
{
    if (_map)
    {
        _map->Reset();
    }
    if (_briefing)
    {
        UpdateWeaponsInBriefing();
        UpdateUnitsInBriefing();
        //		UpdateDebriefing();
    }
    InputSubsystem::Instance().ResetCursorPosition();
}

ControlObject* DisplayMap::OnCreateObject(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MAP_WATCH:
            _watch = new Watch(this, idc, cls);
            return _watch;
        case IDC_MAP_COMPASS:
            _compass = new Compass(this, idc, cls);
            return _compass;
        case IDC_MAP_WALKIE_TALKIE:
            _walkieTalkie = new ControlObjectContainer(this, idc, cls);
            return _walkieTalkie;
        case IDC_MAP_NOTEPAD:
            _notepad = new Notepad(this, idc, cls);
            _notepad->SetBriefing(_briefing);
            return _notepad;
        case IDC_MAP_WARRANT:
            _warrant = new ControlObjectContainer(this, idc, cls);
            return _warrant;
        case IDC_MAP_GPS:
            _gps = new ControlObjectContainer(this, idc, cls);
            return _gps;
        default:
            return Display::OnCreateObject(type, idc, cls);
    }
}

bool DisplayMap::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (_map && GetCtrl(IDC_MAP)->IsVisible())
    {
        if (_map->OnKeyDown(nChar, nRepCnt, nFlags))
        {
            return true;
        }
    }
    return Display::OnKeyDown(nChar, nRepCnt, nFlags);
}

bool DisplayMap::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (_map && GetCtrl(IDC_MAP)->IsVisible())
    {
        if (_map->OnKeyUp(nChar, nRepCnt, nFlags))
        {
            return true;
        }
    }
    return Display::OnKeyUp(nChar, nRepCnt, nFlags);
}

UnitWeaponsInfo::UnitWeaponsInfo(AIUnit* u)
{
    unit = u;
    name = u->GetPerson()->GetInfo()._name;
    Person* veh = u->GetPerson();
    weaponSlots = veh->GetType()->_weaponSlots;
    for (int i = 0; i < veh->NWeaponSystems(); i++)
    {
        WeaponType* weapon = const_cast<WeaponType*>(veh->GetWeaponSystem(i));
        if (weapon->_scope < 2)
        {
            continue;
        }

        int slots = weapon->_weaponType;
        if (slots & MaskSlotPrimary)
        {
            weapons[0] = weapon;
        }
        if (slots & MaskSlotSecondary)
        {
            weapons[1] = weapon;
        }
        if (slots & MaskSlotBinocular)
        {
            if (!weapons[2])
            {
                weapons[2] = weapon;
            }
            else
            {
                weapons[3] = weapon;
            }
        }
        if (slots & MaskSlotHandGun)
        {
            weapons[4] = weapon;
        }
    }
    // Legacy briefing grid layout: first ten cells are regular magazines,
    // remaining cells are handgun magazines.
    const int handGunMagazineStart = 10;
    int item = 0;
    int itemHandGun = handGunMagazineStart;
    for (int i = 0; i < veh->NMagazines(); i++)
    {
        Magazine* magazine = const_cast<Magazine*>(veh->GetMagazine(i));
        if (!magazine)
        {
            continue;
        }
        if (magazine->_ammo == 0)
        {
            continue;
        }
        MagazineType* type = magazine->_type;
        if (type->_scope < 2)
        {
            continue;
        }
        int slots = type->_magazineType;
        if (slots & MaskSlotItem)
        {
            int nItems = GetItemSlotsCount(slots);
            if (nItems == 0)
            {
                continue;
            }
            for (int j = 0; j < nItems; j++)
            {
                if (item < handGunMagazineStart)
                {
                    magazines[item++] = magazine;
                }
            }
        }
        else if (slots & MaskSlotHandGunItem)
        {
            int nItems = GetHandGunItemSlotsCount(slots);
            if (nItems == 0)
            {
                continue;
            }
            for (int j = 0; j < nItems; j++)
            {
                if (itemHandGun < MAGAZINE_SLOTS)
                {
                    magazines[itemHandGun++] = magazine;
                }
            }
        }
    }
}

bool UnitWeaponsInfo::IsMagazineUsable(const MagazineType* type)
{
    for (int i = 0; i < WEAPON_SLOTS; i++)
    {
        const WeaponType* weapon = weapons[i];
        if (!weapon)
        {
            continue;
        }
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            for (int k = 0; k < muzzle->_magazines.Size(); k++)
            {
                if (muzzle->_magazines[k] == type)
                {
                    return true;
                }
            }
        }
    }
    Ref<const WeaponType> always[2] = {
        WeaponTypes.New("Throw"),
        WeaponTypes.New("Put"),
        //		WeaponTypes.New("PipeBomb")
    };
    for (int i = 0; i < sizeof(always) / sizeof(*always); i++)
    {
        const WeaponType* weapon = always[i];
        if (!weapon)
        {
            continue;
        }
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            for (int k = 0; k < muzzle->_magazines.Size(); k++)
            {
                if (muzzle->_magazines[k] == type)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void UpdateWeaponsInBriefing()
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->UpdateWeaponsInBriefing();
    }
}

void DisplayMap::UpdateWeaponsInBriefing()
{
    // create structure
    AIUnit* me = GWorld->FocusOn();
    if (!me)
    {
        return;
    }
    AIGroup* grp = me->GetGroup();
    if (!grp)
    {
        return;
    }

    AIUnit* oldUnit = nullptr;
    if (_currentUnit >= 0 && _currentUnit < _weaponsInfo._weapons.Size())
    {
        oldUnit = _weaponsInfo._weapons[_currentUnit].unit;
    }

    _currentUnit = -1;
    _currentSlot = -1;
    _weaponsInfo._weapons.Resize(0);

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (unit)
        {
            _weaponsInfo._weapons.Add(UnitWeaponsInfo(unit));
        }
    }
    if (oldUnit)
    {
        for (int i = 0; i < _weaponsInfo._weapons.Size(); i++)
        {
            if (_weaponsInfo._weapons[i].unit == oldUnit)
            {
                _currentUnit = i;
                break;
            }
        }
    }
    if (_currentUnit < 0)
    {
        for (int i = 0; i < _weaponsInfo._weapons.Size(); i++)
        {
            if (_weaponsInfo._weapons[i].unit == me)
            {
                _currentUnit = i;
                break;
            }
        }
    }
    PoseidonAssert(_currentUnit >= 0);

    // create html page
    CreateWeaponsPage();

    if (_selectWeapons)
    {
        // load weapons pool
        if (EnabledWeaponPool())
        {
            void CampaignLoadWeaponPool(WeaponsInfo & pool);
            CampaignLoadWeaponPool(_weaponsInfo);
        }
        else
        {
            _weaponsInfo.Import(ExtParsMission);
        }
    }
}

static RString GetWeaponPicture(const WeaponType* weapon)
{
    RString name = weapon->GetPictureName();
    if (name[0] == '\\')
    {
        return name;
    }
    else
    {
        RString prefixW = "equip\\w\\w_";
        return prefixW + name + RString(".paa");
    }
}

static RString GetMagazinePicture(const MagazineType* type)
{
    RString name = type->GetPictureName();
    if (name[0] == '\\')
    {
        return name;
    }
    else
    {
        RString prefixM = "equip\\m\\m_";
        return prefixM + name + RString(".paa");
    }
}

static RString GetEquipmentSection(RString name)
{
    if (name[0] == '\\')
    {
        return "";
    }
    else
    {
        return RString("#EQ_") + RString(name);
    }
}

#ifndef min
inline int min(int a, int b)
{
    return a < b ? a : b;
}
#endif

void DisplayMap::CreateWeaponsPage()
{
    // create section "Equipment"
    bool actual = false;
    int section = _briefing->FindSection("Equipment");
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
        while ((s = _briefing->FindSection("Equipment")) >= 0)
        {
            if (_briefing->CurrentSection() == s)
            {
                actual = true;
            }
            _briefing->RemoveSection(s);
        }
    }
    _briefing->AddName(section, "Equipment");

    // item sizes
    const int weaponsPerLine = 2;
    const int itemsPerLine = 6;
    const int iconsPerItem = 4;
    const int iconsPerWeapon = iconsPerItem * itemsPerLine / weaponsPerLine;

    const float pageWidth = (640.0f - 3.0f) * _briefing->GetPageWidth(); // avoid round probl.
    const float weaponWidth = (1.0 / weaponsPerLine) * pageWidth;
    const float itemWidth = (1.0 / itemsPerLine) * pageWidth;
    float iconWidth = (1.0 / iconsPerItem) * itemWidth;

    float arrowHeight = 480.0f * 1.5 * _briefing->GetPHeight();

    const float weaponHeight = -1;
    //	const float emptyWeaponHeight = 48;
    const float itemHeight = 32;
    //	const float emptyItemHeight = 32;

    RString emptySlot = LocalizeString(IDS_EMPTY_SLOT);
    RString emptyGun = "equip\\emptygun.paa";
    RString emptySec = "equip\\emptysec.paa";
    RString emptyEq = "equip\\emptyeq.paa";
    RString emptyMag = "equip\\emptymag.paa";
    RString emptyMag2 = "equip\\emptymag2.paa";
    RString emptyHGun = "\\misc\\gun_empty.paa";
    RString emptyHGunMag = "\\misc\\mgun_empty.paa";

    // create page
    UnitWeaponsInfo& info = _weaponsInfo._weapons[_currentUnit];

    bool canDrop = false;
    if (info.unit && IsInGame())
    {
        Person* player = GWorld->GetRealPlayer();
        AIUnit* playerUnit = player ? player->Brain() : nullptr;
        if (playerUnit)
        {
            if (info.unit == playerUnit)
            {
                canDrop = true;
            }
            else
            {
                AIGroup* grp = info.unit->GetGroup();
                canDrop = grp && grp->Leader() == playerUnit;
            }
        }
    }

    // name
    float pw = _briefing->GetPageWidth();
    if (_currentUnit > 0)
    {
        _briefing->AddImage(section, "sipka_left.paa", HACenter, false, -1.0, arrowHeight, "Gear:Prev", RString(),
                            0.1 * pw);
    }
    else
    {
        _briefing->AddImage(section, "", HACenter, false, -1.0, arrowHeight, "", RString(), 0.1 * pw);
    }
    RString name = Format("%d: %s", info.unit ? info.unit->ID() : 0, (const char*)info.name);
    _briefing->AddText(section, name, HFH2, HACenter, false, false, "", 0.8 * pw);
    if (_currentUnit < _weaponsInfo._weapons.Size() - 1)
    {
        _briefing->AddImage(section, "sipka_right.paa", HACenter, false, -1.0, arrowHeight, "Gear:Next", RString(),
                            0.1 * pw);
    }
    else
    {
        _briefing->AddImage(section, "", HACenter, false, -1.0, arrowHeight, "", RString(), 0.1 * pw);
    }
    _briefing->AddBreak(section, false);
    //	_briefing->AddBreak(section, false);

    // weapons
    if ((info.weaponSlots & (MaskSlotPrimary | MaskSlotSecondary)) != 0)
    {
        int slot = -1;
        if ((info.weaponSlots & MaskSlotPrimary) == 0)
        {
            slot = 1; // only secondary weapon
        }
        else if ((info.weaponSlots & MaskSlotSecondary) == 0)
        {
            slot = 0; // only primary weapon
        }
        else if (info.weapons[0] && info.weapons[0] == info.weapons[1])
        {
            slot = 0; // weapon in both slots
        }
        if (slot >= 0)
        {
            const WeaponType* weapon = info.weapons[slot];
            // weapon picture
            RString href = "";
            if (_selectWeapons)
            {
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "%s", (const char*)"Slot:A");
                buffer[5] += slot;
                href = buffer;
            }
            if (weapon)
            {
                RString img = GetWeaponPicture(weapon);
                RString text = weapon->GetDisplayName();
                _briefing->AddImage(section, img, HACenter, false, weaponWidth, weaponHeight, href, text);
            }
            else
            {
                _briefing->AddImage(section, emptyGun, HACenter, false, weaponWidth, weaponHeight, href, emptySlot);
            }
            _briefing->AddBreak(section, false);
            // icons
            int rest = iconsPerWeapon;
            if (weapon)
            {
                href = GetEquipmentSection(weapon->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
            }
            _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
            _briefing->AddBreak(section, false);
        }
        else
        {
            // weapon pictures
            const WeaponType* weapon = info.weapons[0];
            RString href = _selectWeapons ? "Slot:A" : "";
            // RString href("Slot:A");
            if (weapon)
            {
                RString img = GetWeaponPicture(weapon);

                RString text = weapon->GetDisplayName();
                _briefing->AddImage(section, img, HACenter, false, weaponWidth, weaponHeight, href, text);
            }
            else
            {
                _briefing->AddImage(section, emptyGun, HACenter, false, weaponWidth, weaponHeight, href, emptySlot);
            }
            weapon = info.weapons[1];
            href = _selectWeapons ? "Slot:B" : "";
            // href = "Slot:B";
            if (weapon)
            {
                RString img = GetWeaponPicture(weapon);
                RString text = weapon->GetDisplayName();
                _briefing->AddImage(section, img, HACenter, false, weaponWidth, weaponHeight, href, text);
            }
            else
            {
                _briefing->AddImage(section, emptySec, HACenter, false, weaponWidth, weaponHeight, href, emptySlot);
            }
            _briefing->AddBreak(section, false);
            // icons
            weapon = info.weapons[0];
            int rest = iconsPerWeapon;
            if (weapon)
            {
                href = GetEquipmentSection(weapon->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
            }
            _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
            weapon = info.weapons[1];
            rest = iconsPerWeapon;
            if (weapon)
            {
                href = GetEquipmentSection(weapon->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
            }
            _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
            _briefing->AddBreak(section, false);
        }
        // _briefing->AddBreak(section, false);
    }

    // items I
    int item = 0;
    int maxItems = GetItemSlotsCount(info.weaponSlots);
    if (item < maxItems)
    {
        int oldItem = item;
        // items pictures
        int maxItemsMin = maxItems;
        saturateMin(maxItemsMin, 4);
        while (item < maxItemsMin)
        {
            Magazine* magazine = info.magazines[item];
            RString href = "";
            // if (_selectWeapons)
            {
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "%s", (const char*)"Slot:F");
                buffer[5] += item;
                href = buffer;
            }
            if (magazine)
            {
                int nItems = GetItemSlotsCount(magazine->_type->_magazineType);
                RString img = GetMagazinePicture(magazine->_type);
                RString text; // = magazine->_type->_shortName;
                _briefing->AddImage(section, img, HACenter, false, nItems * itemWidth, itemHeight, href, text);
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, emptyMag, HACenter, false, itemWidth, itemHeight, href /*, emptySlot*/);
                item++;
            }
        }
        _briefing->AddBreak(section, false);
        item = oldItem;
        // icons
        while (item < maxItemsMin)
        {
            Magazine* magazine = info.magazines[item];
            if (magazine)
            {
                int nItems = GetItemSlotsCount(magazine->_type->_magazineType);
                int rest = nItems * iconsPerItem;
                RString href = GetEquipmentSection(magazine->_type->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }

                if (_selectWeapons)
                {
                    href = RString("Fill1:") + magazine->_type->GetName();
                    _briefing->AddImage(section, "\\misc\\hvezdicka.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropMagazine:%s", (const char*)magazine->_type->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }

                _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, "", HACenter, false, iconsPerItem * iconWidth, iconWidth, "");
                item++;
            }
        }
        _briefing->AddBreak(section, false);
    }
    // items II
    if (item < maxItems)
    {
        int oldItem = item;
        // items pictures
        while (item < maxItems)
        {
            Magazine* magazine = info.magazines[item];
            RString href = "";
            // if (_selectWeapons)
            {
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "%s", (const char*)"Slot:F");
                buffer[5] += item;
                href = buffer;
            }
            if (magazine)
            {
                int nItems = GetItemSlotsCount(magazine->_type->_magazineType);
                RString img = GetMagazinePicture(magazine->_type);
                RString text; // = magazine->_type->_shortName;
                _briefing->AddImage(section, img, HACenter, false, nItems * itemWidth, itemHeight, href, text);
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, emptyMag2, HACenter, false, itemWidth, itemHeight, href /*, emptySlot*/);
                item++;
            }
        }
        _briefing->AddBreak(section, false);
        item = oldItem;
        // icons
        while (item < maxItems)
        {
            Magazine* magazine = info.magazines[item];
            if (magazine)
            {
                int nItems = GetItemSlotsCount(magazine->_type->_magazineType);
                int rest = nItems * iconsPerItem;
                RString href = GetEquipmentSection(magazine->_type->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }

                if (_selectWeapons)
                {
                    href = RString("Fill2:") + magazine->_type->GetName();
                    _briefing->AddImage(section, "\\misc\\hvezdicka.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropMagazine:%s", (const char*)magazine->_type->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }

                _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, "", HACenter, false, iconsPerItem * iconWidth, iconWidth, "");
                item++;
            }
        }
        _briefing->AddBreak(section, false);
    }
    if (maxItems > 0)
    {
        _briefing->AddBreak(section, false);
    }

    // hand gun with magazines

    if ((info.weaponSlots & MaskSlotHandGun) != 0)
    {
        // hand gun picture
        const WeaponType* weapon = info.weapons[4];
        RString href = _selectWeapons ? "Slot:E" : "";
        //		RString href = "Slot:E";
        if (weapon)
        {
            RString img = GetWeaponPicture(weapon);
            RString text = weapon->GetDisplayName();
            _briefing->AddImage(section, img, HACenter, false, 2 * itemWidth, itemHeight, href, text);
        }
        else
        {
            _briefing->AddImage(section, emptyHGun, HACenter, false, 2 * itemWidth, itemHeight, href, emptySlot);
        }

        // hand gun magazines pictures
        item = 0;
        maxItems = GetHandGunItemSlotsCount(info.weaponSlots);
        int oldItem = item;
        while (item < maxItems)
        {
            Magazine* magazine = info.magazines[10 + item];
            RString href = "";
            //			if (_selectWeapons)
            {
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "%s", (const char*)"Slot:F");
                buffer[5] += 10 + item;
                href = buffer;
            }
            if (magazine)
            {
                int nItems = GetHandGunItemSlotsCount(magazine->_type->_magazineType);
                RString img = GetMagazinePicture(magazine->_type);
                RString text; // = magazine->_type->_shortName;
                _briefing->AddImage(section, img, HACenter, false, nItems * itemWidth, itemHeight, href, text);
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, emptyHGunMag, HACenter, false, itemWidth, itemHeight,
                                    href /*, emptySlot*/);
                item++;
            }
        }
        _briefing->AddBreak(section, false);

        // hand gun icons
        int rest = 2 * iconsPerItem;
        if (weapon)
        {
            href = GetEquipmentSection(weapon->GetPictureName());
            if (href.GetLength() > 0)
            {
                _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                rest--;
            }
            if (canDrop)
            {
                href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                rest--;
            }
        }
        _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");

        // hand gun magazines icons
        item = oldItem;
        while (item < maxItems)
        {
            Magazine* magazine = info.magazines[10 + item];
            if (magazine)
            {
                int nItems = GetHandGunItemSlotsCount(magazine->_type->_magazineType);
                int rest = nItems * iconsPerItem;
                RString href = GetEquipmentSection(magazine->_type->GetPictureName());
                if (href.GetLength() > 0)
                {
                    _briefing->AddImage(section, "i.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (_selectWeapons)
                {
                    href = RString("Fill3:") + magazine->_type->GetName();
                    _briefing->AddImage(section, "\\misc\\hvezdicka.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                if (canDrop)
                {
                    href = Format("DropMagazine:%s", (const char*)magazine->_type->GetName());
                    _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                    rest--;
                }
                _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
                item += nItems;
            }
            else
            {
                _briefing->AddImage(section, "", HACenter, false, iconsPerItem * iconWidth, iconWidth, "");
                item++;
            }
        }

        _briefing->AddBreak(section, false);
        _briefing->AddBreak(section, false);
    }

    // binocular
    if ((info.weaponSlots & MaskSlotBinocular) != 0)
    {
        // binocular picture
        const WeaponType* weapon = info.weapons[2];
        RString href = _selectWeapons ? "Slot:C" : "";
        // RString href = "Slot:C";
        if (weapon)
        {
            RString img = GetWeaponPicture(weapon);
            RString text = weapon->GetDisplayName();
            _briefing->AddImage(section, img, HACenter, false, weaponWidth, weaponHeight, href, text);
        }
        else
        {
            _briefing->AddImage(section, emptyEq, HACenter, false, weaponWidth, weaponHeight, href, emptySlot);
        }
        weapon = info.weapons[3];
        href = _selectWeapons ? "Slot:D" : "";
        // href = "Slot:D";
        if (weapon)
        {
            RString img = GetWeaponPicture(weapon);
            RString text = weapon->GetDisplayName();
            _briefing->AddImage(section, img, HACenter, false, weaponWidth, weaponHeight, href, text);
        }
        else
        {
            _briefing->AddImage(section, emptyEq, HACenter, false, weaponWidth, weaponHeight, href, emptySlot);
        }
        // _briefing->AddBreak(section, false);

        // icons
        weapon = info.weapons[2];
        int rest = iconsPerWeapon;
        if (weapon)
        {
            if (canDrop)
            {
                href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                rest--;
            }
        }
        _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
        weapon = info.weapons[3];
        rest = iconsPerWeapon;
        if (weapon)
        {
            if (canDrop)
            {
                href = Format("DropWeapon:%s", (const char*)weapon->GetName());
                _briefing->AddImage(section, "\\misc\\poloz.paa", HACenter, false, iconWidth, iconWidth, href);
                rest--;
            }
        }
        _briefing->AddImage(section, "", HACenter, false, rest * iconWidth, iconWidth, "");
        _briefing->AddBreak(section, false);
    }

    _briefing->FormatSection(section);
    if (actual)
    {
        SwitchBriefingSection("Equipment");
    }
}

void DisplayMap::CreateWeaponsPoolPage(int slot)
{
    // create section "Pool"
    int section = _briefing->FindSection("Pool");
    if (section < 0)
    {
        section = _briefing->AddSection();
    }
    else
    {
        _briefing->InitSection(section);
        int s;
        while ((s = _briefing->FindSection("Pool")) >= 0)
        {
            _briefing->RemoveSection(s);
        }
    }
    _briefing->AddName(section, "Pool");

    RString title;
    RString empty;
    int mask, maskExclude = 0;

    switch (slot)
    {
        case 0:
            title = LocalizeString(IDS_BRIEF_PRIMARY_WEAPONS);
            empty = "Weapon:";
            mask = MaskSlotPrimary;
            break;
        case 1:
            title = LocalizeString(IDS_BRIEF_SECONDARY_WEAPONS);
            empty = "Weapon:";
            mask = MaskSlotSecondary;
            maskExclude = MaskSlotPrimary;
            break;
        case 2:
        case 3:
            title = LocalizeString(IDS_BRIEF_SPECIAL_ITEMS);
            empty = "Weapon:";
            mask = MaskSlotBinocular;
            break;
        case 4:
            title = LocalizeString(IDS_BRIEF_SPECIAL_HANDGUN);
            empty = "Weapon:";
            mask = MaskSlotHandGun;
            break;
        case 15:
        case 16:
        case 17:
        case 18:
            title = LocalizeString(IDS_BRIEF_SPECIAL_HANDGUN_MAGAZINES);
            empty = "Magazine:";
            mask = MaskSlotHandGunItem;
            break;
        default:
            title = LocalizeString(IDS_BRIEF_SPECIAL_MAGAZINES);
            empty = "Magazine:";
            mask = MaskSlotItem;
            break;
    }

    // generate page for weapon selection
    _briefing->AddText(section, title, HFH1, HALeft, false, false, "");
    _briefing->AddBreak(section, false);
    _briefing->AddText(section, LocalizeString(IDS_BRIEF_POOL_CANCEL), HFP, HALeft, false, false,
                       empty + RString("Cancel"));
    _briefing->AddBreak(section, false);
    _briefing->AddText(section, LocalizeString(IDS_BRIEF_POOL_NONE), HFP, HALeft, false, false, empty);
    _briefing->AddBreak(section, false);
    _briefing->AddBreak(section, false);

    if (mask == MaskSlotItem || mask == MaskSlotHandGunItem)
    {
        // magazines
        int n = _weaponsInfo._magazinesPool.Size();
        for (int i = 0; i < n; i++)
        {
            Magazine* magazine = _weaponsInfo._magazinesPool[i];
            MagazineType* type = magazine->_type;
            int slots = type->_magazineType;
            if ((slots & mask) == 0)
            {
                continue;
            }
            if ((slots & maskExclude) != 0)
            {
                continue;
            }
            int found = 0;
            for (int j = 0; j < i; j++)
            {
                if (_weaponsInfo._magazinesPool[j]->_type == type)
                {
                    found++;
                    break;
                }
            }
            if (found > 0)
            {
                continue;
            }
            for (int j = i; j < n; j++)
            {
                if (_weaponsInfo._magazinesPool[j]->_type == type)
                {
                    found++;
                }
            }
            char text[512];
            snprintf(text, sizeof(text), LocalizeString(IDS_NUM_MAGAZINES), (const char*)type->GetDisplayName(), found);
            RString href = empty + type->GetName();
            _briefing->AddText(section, text, HFP, HALeft, false, false, href);
            _briefing->AddBreak(section, false);
        }
    }
    else
    {
        int n = _weaponsInfo._weaponsPool.Size();
        for (int i = 0; i < n; i++)
        {
            WeaponType* weapon = _weaponsInfo._weaponsPool[i];
            int slots = weapon->_weaponType;
            if ((slots & mask) == 0)
            {
                continue;
            }
            if ((slots & maskExclude) != 0)
            {
                continue;
            }
            int found = 0;
            for (int j = 0; j < i; j++)
            {
                if (_weaponsInfo._weaponsPool[j] == weapon)
                {
                    found++;
                    break;
                }
            }
            if (found > 0)
            {
                continue;
            }
            for (int j = i; j < n; j++)
            {
                if (_weaponsInfo._weaponsPool[j] == weapon)
                {
                    found++;
                }
            }
            char text[512];
            snprintf(text, sizeof(text), LocalizeString(IDS_NUM_MAGAZINES), (const char*)weapon->GetDisplayName(),
                     found);
            RString href = empty + weapon->GetName();
            _briefing->AddText(section, text, HFP, HALeft, false, false, href);
            _briefing->AddBreak(section, false);
        }
    }
    _briefing->FormatSection(section);
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
