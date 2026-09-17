#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/LicensePlateTextTuning.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Poseidon/World/Terrain/Roads.hpp>
#include <Poseidon/World/Scene/Thing.hpp>
#include <Poseidon/AI/Path/PathSteer.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>

#include <Poseidon/Graphics/Core/TLVertex.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>

#include <Poseidon/World/Detection/Detector.hpp> // flag

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#pragma warning(disable : 4355)

namespace Poseidon
{
using namespace Foundation;

namespace
{
constexpr LicensePlateTextTuning kDefaultLicensePlateTextTuning{};
LicensePlateTextTuning s_licensePlateTextTuning = kDefaultLicensePlateTextTuning;
} // namespace

LicensePlateTextTuning GetLicensePlateTextTuning()
{
    return s_licensePlateTextTuning;
}

void SetLicensePlateTextTuning(const LicensePlateTextTuning& tuning)
{
    s_licensePlateTextTuning.widthScale = floatMax(0.1f, tuning.widthScale);
    s_licensePlateTextTuning.horizontalOffset = tuning.horizontalOffset;
    s_licensePlateTextTuning.verticalOffset = tuning.verticalOffset;
    s_licensePlateTextTuning.surfaceOffset = floatMax(0.0f, tuning.surfaceOffset);
    s_licensePlateTextTuning.softness = floatMax(0.0f, tuning.softness);
}

void ResetLicensePlateTextTuning()
{
    s_licensePlateTextTuning = kDefaultLicensePlateTextTuning;
}

bool LoadLicensePlateTextTuningFromConfig(const ParamEntry* cfg)
{
    LicensePlateTextTuning tuning = kDefaultLicensePlateTextTuning;
    if (!cfg)
    {
        SetLicensePlateTextTuning(tuning);
        return false;
    }

    if (const ParamEntry* entry = cfg->FindEntry("widthScale"))
        tuning.widthScale = static_cast<float>(*entry);
    if (const ParamEntry* entry = cfg->FindEntry("horizontalOffset"))
        tuning.horizontalOffset = static_cast<float>(*entry);
    if (const ParamEntry* entry = cfg->FindEntry("verticalOffset"))
        tuning.verticalOffset = static_cast<float>(*entry);
    if (const ParamEntry* entry = cfg->FindEntry("surfaceOffset"))
        tuning.surfaceOffset = static_cast<float>(*entry);
    if (const ParamEntry* entry = cfg->FindEntry("softness"))
        tuning.softness = static_cast<float>(*entry);

    SetLicensePlateTextTuning(tuning);
    return true;
}

LicensePlateTextFrame BuildLicensePlateTextFrame(Vector3 center, Vector3 normal, Vector3 modelUp, float size,
                                                 const LicensePlateTextTuning& tuning)
{
    normal = normal.Normalized();
    Vector3 up = modelUp;
    Vector3 right = up.CrossProduct(normal).Normalized();
    up = normal.CrossProduct(right).Normalized();

    up *= size;
    right *= size * tuning.widthScale;
    center -= normal * tuning.surfaceOffset;

    LicensePlateTextFrame frame;
    frame.up = up;
    frame.right = right;
    frame.origin = center + up * tuning.verticalOffset + right * tuning.horizontalOffset;
    return frame;
}

static const Color MarkerRedColor(1.0, 0.1, 0.1);
static const Color MarkerRedAmbient(0.1, 0.01, 0.01);
static const float MarkerRedBrightness = 0.002;

static const Color MarkerGreenColor(0.1, 1.0, 0.1);
static const Color MarkerGreenAmbient(0.01, 0.1, 0.01);
static const float MarkerGreenBrightness = 0.002;

static const Color MarkerBlueColor(0.1, 0.1, 1.0);
static const Color MarkerBlueAmbient(0.01, 0.01, 0.1);
static const float MarkerBlueBrightness = 0.002;

static const Color MarkerWhiteColor(1.0, 1.0, 1.0);
static const Color MarkerWhiteAmbient(0.1, 0.1, 0.1);
static const float MarkerWhiteBrightness = 0.001;

void ReflectorInfo::Load(EntityAIType& type, const ParamEntry& cls, float armor)
{
    LODShape* shape = type.GetShape();
    color = GetColor(cls >> "color");
    colorAmbient = GetColor(cls >> "ambient");
    positionIndex = -1;
    directionIndex = -1;
    position = VZero;
    direction = VForward;
    Shape* memory = shape->MemoryLevel();
    if (memory)
    {
        RString name = cls >> "position";
        positionIndex = memory->PointIndex(name);
        if (positionIndex >= 0)
        {
            position = memory->Pos(positionIndex);
        }
        name = cls >> "direction";
        directionIndex = memory->PointIndex(name);
        if (directionIndex >= 0 && positionIndex >= 0)
        {
            direction = memory->Pos(directionIndex) - position;
            direction.Normalize();
        }
    }
    size = cls >> "size";
    brightness = cls >> "brightness";
    RString selName = cls >> "selection";
    selection.Init(shape, selName, nullptr);
    RString hitName = cls >> "hitpoint";
    hitPoint = HitPoint(shape->HitpointsLevel(), hitName, nullptr, armor, -1);
    hitPoint.SetIndex(type.GetHitPoints().Add(&hitPoint));
};

int MuzzleNameCachedMagazineSlots::IdxOfMuzzle(const RString& muzzle) const
{
    // for few muzzles, search one by one would be faster
    if (m_array.Size() < 8)
    {
        for (int i = 0, c = m_array.Size(); i < c; ++i)
        {
            if (stricmp(m_array[i]._muzzle->GetName(), muzzle) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    if (m_muzzleName2Idx.empty())
    {
        using CacheRef = std::unordered_map<RStringB, int, StrHashCI, StrEqualCI>&;
        // Only in this logical branch the m_muzzleName2Idx is editable
        CacheRef cache = const_cast<CacheRef>(m_muzzleName2Idx);
        // build up the cache
        for (int i = 0, c = m_array.Size(); i < c; ++i)
        {
            // if key exists, insert will fail
            // this behaviour is coincide with existed codes
            cache.try_emplace(m_array[i]._muzzle->GetName(), i);
        }
    }
    auto it = m_muzzleName2Idx.find(muzzle);
    if (it == m_muzzleName2Idx.cend())
    {
        return -1;
    }
    return it->second;
}

void PlateInfo::Init(Shape* shape, Offset face)
{
    _face = face;
    const Poly& f = shape->Face(face);

    _center = VZero;
    for (int v = 0; v < f.N(); v++)
    {
        int vertex = f.GetVertex(v);
        _center += shape->Pos(vertex);
    }
    float invFN = 1.0f / f.N();
    _center *= invFN;
    Vector3 normal = f.GetNormal(*shape);
    _normal = normal.Normalized();
    _size = sqrt(normal.Size()) * 0.4;
}

void PlateInfos::Init(LODShape* shape, RString name, FontID font, PackedColor color)
{
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* sShape = shape->Level(level);
        int sel = sShape->FindNamedSel(name);
        if (sel >= 0)
        {
            const FaceSelection& faces = sShape->NamedSel(sel).FaceOffsets(sShape);
            int n = faces.Size();
            _plates[level].Resize(n);
            for (int f = 0; f < n; f++)
            {
                Offset off = faces[f];
                _plates[level][f].Init(sShape, off);
            }
        }
    }
    _font = GEngine->LoadFont(font);
    _color = color;
}

void PlateInfos::Draw(int level, ClipFlags clipFlags, const FrameBase& pos, const char* text) const
{
    // find position to draw at (based on _plate selection)
    for (int plate = 0; plate < _plates[level].Size(); plate++)
    {
        const PlateInfo& pi = _plates[level][plate];

        Vector3 center = pi._center;
        Vector3 normal = pi._normal;

        pos.PositionModelToWorld(center, center);
        pos.DirectionModelToWorld(normal, normal);

        Vector3 up = pos.DirectionUp();

        const LicensePlateTextFrame frame =
            BuildLicensePlateTextFrame(center, normal, up, pi._size, s_licensePlateTextTuning);

        if (s_licensePlateTextTuning.softness > 0.0f)
        {
            PackedColor softColor = PackedColorRGB(_color, toIntFloor(_color.A8() * 0.18f));
            const Vector3 softRight = frame.right * s_licensePlateTextTuning.softness;
            const Vector3 softUp = frame.up * s_licensePlateTextTuning.softness;
            GEngine->DrawText3D(frame.origin + softRight, frame.up, frame.right, clipFlags, _font, softColor, 0, text);
            GEngine->DrawText3D(frame.origin - softRight, frame.up, frame.right, clipFlags, _font, softColor, 0, text);
            GEngine->DrawText3D(frame.origin + softUp, frame.up, frame.right, clipFlags, _font, softColor, 0, text);
            GEngine->DrawText3D(frame.origin - softUp, frame.up, frame.right, clipFlags, _font, softColor, 0, text);
        }
        GEngine->DrawText3D(frame.origin, frame.up, frame.right, clipFlags, _font, _color, 0, text);
    }
}

EntityAIType::EntityAIType(const ParamEntry* param) : EntityType(param)
{
    _scopeLevel = 0;
}

EntityAIType::~EntityAIType()
{
    // make sure all vehicles of this type are already released
    AI_ERROR(_refVehicles == 0);
}

void ViewPars::Load(const ParamEntry& cfg)
{
#define GET_PAR(x) _##x = cfg >> #x
    GET_PAR(initAngleY);
    GET_PAR(minAngleY);
    GET_PAR(maxAngleY);
    GET_PAR(initAngleX);
    GET_PAR(minAngleX);
    GET_PAR(maxAngleX);
    GET_PAR(initFov);
    GET_PAR(minFov);
    GET_PAR(maxFov);
#undef GET_PAR
    saturateMax(_maxFov, _initFov); // force: fov is always in valid range
    saturateMin(_minFov, _initFov);
}

void ViewPars::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    fov = _initFov;
    dive = _initAngleX * (H_PI / 180);
    heading = _initAngleY * (H_PI / 180);
}

void ViewPars::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    float minHeading = _minAngleY * (H_PI / 180);
    float maxHeading = _maxAngleY * (H_PI / 180);
    if (maxHeading - minHeading < H_PI * 15 / 8)
    {
        float initHeading = _initAngleY * (H_PI / 180);
        minHeading = AngleDifference(minHeading, initHeading);
        maxHeading = AngleDifference(maxHeading, initHeading);
        heading = AngleDifference(heading, initHeading);

        saturate(heading, minHeading, maxHeading);

        heading += initHeading;
    }
    float minDive = _minAngleX * (H_PI / 180);
    float maxDive = _maxAngleX * (H_PI / 180);
    saturate(dive, minDive, maxDive);
    saturate(fov, _minFov, _maxFov);
}

void EntityAIType::Load(const ParamEntry& par)
{
    base::Load(par);

#define GET_PAR(x) _##x = par >> #x

    GET_PAR(accuracy);
    GET_PAR(camouflage);
    GET_PAR(audible);
    GET_PAR(camouflage);
    // LocalizedString: bind to the config node so the name tracks runtime language switches
    _displayName.Bind(par >> "displayName");

    GET_PAR(weaponSlots);

    GET_PAR(spotableNightLightsOff);
    GET_PAR(spotableNightLightsOn);

    GET_PAR(visibleNightLightsOff);
    GET_PAR(visibleNightLightsOn);

    _nameSound = par >> "nameSound";
    _typicalSide = (TargetSide)(par >> "side").GetInt();
    _destrType = (DestructType)(par >> "destrType").GetInt();

    RString picture = par >> "picture";
    // some parent vehicle classes have no model or picture defined
    if (picture.GetLength() > 0)
    {
        _picture = GlobLoadTexture(GetPictureName(picture));
    }
    RString icon = par >> "icon";
    if (icon.GetLength() > 0)
    {
        _icon = GlobLoadTexture(GetPictureName(icon));
    }

    GET_PAR(cost);
    GET_PAR(fuelCapacity);
    GET_PAR(armor);

    _extCameraPosition.Init();
    _extCameraPosition[0] = (par >> "extCameraPosition")[0];
    _extCameraPosition[1] = (par >> "extCameraPosition")[1];
    _extCameraPosition[2] = (par >> "extCameraPosition")[2];

    GET_PAR(maxSpeed);
    GET_PAR(secondaryExplosion);
    GET_PAR(sensitivity);
    GET_PAR(sensitivityEar);

    GET_PAR(brakeDistance);
    GET_PAR(precision);
    GET_PAR(formationX);
    GET_PAR(formationZ);
    GET_PAR(formationTime);
    GET_PAR(steerAheadSimul);
    GET_PAR(steerAheadPlan);

    // get camera limits / initial values
    _viewPilot.Load(par >> "ViewPilot");

    // get control / simulation properties
    GET_PAR(predictTurnSimul);
    GET_PAR(predictTurnPlan);
    GET_PAR(minFireTime);

    if (_armor > 1e-10)
    {
        _invArmor = 1 / _armor;
        _logArmor = log(_armor);
    }
    else
    {
        _invArmor = 1e10;
        _logArmor = 25;
    }

    _invFormationTime = 1 / _formationTime;
    _structuralDammageCoef = 1 / (float)(par >> "armorStructural");

    // cargo definition in config
    _maxFuelCargo = par >> "transportFuel";
    _maxRepairCargo = par >> "transportRepair";
    _maxAmmoCargo = par >> "transportAmmo";
    _maxWeaponsCargo = par >> "transportMaxWeapons";
    _maxMagazinesCargo = par >> "transportMaxMagazines";

    const ParamEntry& weaponCargo = par >> "TransportWeapons";
    int n = weaponCargo.GetEntryCount();
    _weaponCargo.Resize(n);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& par = weaponCargo.GetEntry(i);
        RStringB weapon = par >> "weapon";
        _weaponCargo[i].weapon = WeaponTypes.New(weapon);
        _weaponCargo[i].count = par >> "count";
    }
    const ParamEntry& magazineCargo = par >> "TransportMagazines";
    n = magazineCargo.GetEntryCount();
    _magazineCargo.Resize(n);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& par = magazineCargo.GetEntry(i);
        RStringB magazine = par >> "magazine";
        _magazineCargo[i].magazine = MagazineTypes.New(magazine);
        _magazineCargo[i].count = par >> "count";
    }

    _minCost = _maxSpeed > 0 ? 3.6 / _maxSpeed : 1e10;

    GET_PAR(irTarget);
    if (!par.FindEntry("irScanRange"))
    {
        GET_PAR(irScanToEyeFactor);
        GET_PAR(irScanRangeMin);
        GET_PAR(irScanRangeMax);
    }
    else
    {
        // compatibility settings
        float irRange = par >> "irScanRange";
        _irScanRangeMin = irRange;
        _irScanRangeMax = irRange;
        _irScanToEyeFactor = 1;
    }

    GET_PAR(irScanGround);
    GET_PAR(nightVision);

    GET_PAR(laserScanner);
    GET_PAR(laserTarget);

    GET_PAR(commanderCanSee);
    GET_PAR(driverCanSee);
    GET_PAR(gunnerCanSee);

    GET_PAR(attendant);
    GET_PAR(preferRoads);

    _unitInfoType = (UnitInfoType)(par >> "unitInfoType").GetInt();
    GET_PAR(hideUnitInfo);

    GetValue(_mainSound, par >> "soundEngine");
    GetValue(_envSound, par >> "soundEnviron");
    GetValue(_dmgSound, par >> "soundDammage");
    GetValue(_crashSound, par >> "soundCrash");
    GetValue(_landCrashSound, par >> "soundLandCrash");
    GetValue(_waterCrashSound, par >> "soundWaterCrash");
    GetValue(_getInSound, par >> "soundGetIn");
    GetValue(_getOutSound, par >> "soundGetOut");
    GetValue(_servoSound, par >> "soundServo");

    const ParamEntry& extSounds = par >> "SoundEnvironExt";
    n = extSounds.GetEntryCount();
    _extEnvSounds.Realloc(n);
    _extEnvSounds.Resize(n);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& par = extSounds.GetEntry(i);
        // par should be array of arrays
        _extEnvSounds[i].name = par.GetName();
        _extEnvSounds[i].pars.Realloc(par.GetSize());
        _extEnvSounds[i].pars.Resize(0);
        for (int j = 0; j < par.GetSize(); j++)
        {
            int index = _extEnvSounds[i].pars.Add();
            GetValue(_extEnvSounds[i].pars[index], par[j]);
        }
        _extEnvSounds[i].pars.Compact();
    }
    _extEnvSounds.Compact();

    // load weapon info
    const ParamEntry& weapons = par >> "weapons";
    _weapons.Resize(0);
    _weapons.Realloc(weapons.GetSize());
    for (int i = 0; i < weapons.GetSize(); i++)
    {
        AddWeapon(weapons[i].GetValue());
    }
    _weapons.Compact();
    _magazines.Resize(0);
    const ParamEntry& magazines = par >> "magazines";
    for (int i = 0; i < magazines.GetSize(); i++)
    {
        AddMagazine(magazines[i].GetValue());
    }
    _magazines.Compact();

    // threat
    const ParamEntry& threat = par >> "threat";
    _threat[VSoft] = threat[0];
    _threat[VArmor] = threat[1];
    _threat[VAir] = threat[2];

    // cargo

    _kind = (VehicleKind)(par >> "type").GetInt();

    const ParamEntry* entry = par.FindEntry("forceSupply");
    if (entry)
    {
        _forceSupply = *entry;
    }
    else
    {
        _forceSupply = false;
    }

    entry = par.FindEntry("showWeaponCargo");
    if (entry)
    {
        _showWeaponCargo = *entry;
    }
    else
    {
        _showWeaponCargo = false;
    }

    _showDmgPoint = VZero;
    _shapeReversed = par >> "reversed";
    EntityType::_shapeReversed = _shapeReversed;
}

const SoundPars& EntityAIType::GetEnvSoundExt(RStringB name) const
{
    for (int i = 0; i < _extEnvSounds.Size(); i++)
    {
        const ExtSoundInfo& info = _extEnvSounds[i];
        if (stricmp(info.name, name) == 0)
        {
            return info.pars[0];
        }
    }
    return _envSound;
}

const SoundPars& EntityAIType::GetEnvSoundExtRandom(RStringB name) const
{
    for (int i = 0; i < _extEnvSounds.Size(); i++)
    {
        const ExtSoundInfo& info = _extEnvSounds[i];
        if (stricmp(info.name, name) == 0)
        {
            // select random
            int n = info.pars.Size();
            if (n <= 1)
            {
                return info.pars[0];
            }
            float val = GRandGen.RandomValue();
            int i = toIntFloor(val * n);
            saturate(i, 0, n - 1);
            return info.pars[i];
        }
    }
    return _envSound;
}

bool EntityAI::CanLock(TargetType* type, int weapon) const
{
    // check current ammo

    if (weapon < 0)
    {
        weapon = _currentWeapon;
    }
    if (weapon < 0)
    {
        return false;
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        // FIX
        // if there is no mode, use some other way to check if it is LAW
        // check weapon typical magazine
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        const MuzzleType* muzzle = slot._muzzle;
        if (!muzzle)
        {
            return false;
        }
        if (muzzle->_magazines.Size() <= 0)
        {
            return false;
        }
        MagazineType* magazine = muzzle->_magazines[0];
        if (!magazine)
        {
            magazine = muzzle->_typicalMagazine;
        }
        if (!magazine)
        {
            return false;
        }
        // check first magazine mode
        if (magazine->_modes.Size() <= 0)
        {
            return false;
        }
        mode = magazine->_modes[0];
    }
    if (!mode)
    {
        return false;
    }
    if (!mode->_ammo)
    {
        return false;
    }
    return type->LockPossible(mode->_ammo);
}

bool EntityAI::LockPossible(const AmmoType* ammo) const
{
    const EntityAIType* type = GetType();
    // laser target can locked with laser capable weapon only
    if (type->GetLaserTarget())
    {
        return ammo->laserLock;
    }
    if (ammo->laserLock && !ammo->irLock)
    {
        if (!type->GetLaserTarget())
        {
            return false;
        }
    }
    // disable locking of airborne targets
    if (Airborne())
    {
        if (!ammo->airLock)
        {
            return false;
        }
    }
    // some weapons can lock only ir targets
    if (!type->GetIRTarget() && ammo->irLock)
    {
        return false;
    }
    return true;
}

void EntityAIType::AddHitPoints(const ParamEntry& par)
{
    // _shape is already valid
    // we want to load hitpoints
    // each hitpoint is defined by:
    //
    for (int i = 0; i < par.GetSize(); i++)
    {
    }
}

void EntityAIType::InitShape()
{
    EntityType::InitShape();
    if (!_shape)
        return;
    // after shape is loaded
    _unitNumber.Init(_shape, "cislo", nullptr);
    _groupSign.Init(_shape, "grupa", nullptr);
    _sectorSign.Init(_shape, "sektor", nullptr);
    _sideSign.Init(_shape, "side", nullptr);
    _clan.Init(_shape, "clan", nullptr);
    _dashboard.Init(_shape, "podsvit pristroju", nullptr);
    _showDmg.Init(_shape, "poskozeni", nullptr);

    _backLights.Init(_shape, "zadni svetlo", nullptr);

    _squadTitles.Init(_shape, "clan_sign", GetFontID("tahomaB48"), PackedColor(Color(0, 0, 0, 0.75)));

    int pilot = _shape->FindSpecLevel(VIEW_PILOT);
    if (pilot >= 0)
    {
        Shape* pilotLevel = _shape->LevelOpaque(pilot);
        AI_ERROR(pilotLevel);
        _showDmgPoint = pilotLevel->NamedPosition("poskozeni");
    }

    _lights.Clear();
    if (_shape->MemoryPointExists("doplnovani"))
    {
        Vector3Val d1 = _shape->MemoryPoint("doplnovani");
        Vector3Val d2 = _shape->MemoryPoint("doplnovani2");
        _supplyPoint = d1;
        _supplyRadius = d2.Distance(d1);

        // check if point is outside of the model
        // if not, issue warning
        if (_shape->IsInside(_supplyPoint))
        {
            LOG_DEBUG(AI, "{}: supply point inside the model.", (const char*)_shape->Name());
            // we should find some solution
            // quick patch is moving it to bounding sphere
            Vector3 supplyDir = _supplyPoint - _shape->GeometryCenter();
            float minDist = _shape->GeometrySphere();

            if (supplyDir.SquareSize() < Square(minDist))
            {
                supplyDir.Normalize();
                supplyDir *= minDist;
                _supplyPoint = _shape->GeometryCenter() + supplyDir;
                LOG_DEBUG(AI, "  supply point patched.");
            }
        }
    }
    else
    {
        _supplyPoint = VZero;
        _supplyRadius = (_shape->GeometrySphere() + 1) * 1.1;
    }
    Shape* memory = _shape->MemoryLevel();
    if (memory)
    {
        int index = memory->FindNamedSel("cerveny pozicni");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarker;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerRedColor;
                light.ambient = MarkerRedAmbient;
                light.brightness = MarkerRedBrightness;
            }
        }

        index = memory->FindNamedSel("zeleny pozicni");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarker;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerGreenColor;
                light.ambient = MarkerGreenAmbient;
                light.brightness = MarkerGreenBrightness;
            }
        }

        index = memory->FindNamedSel("bily pozicni blik");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarkerBlink;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerWhiteColor;
                light.ambient = MarkerWhiteAmbient;
                light.brightness = MarkerWhiteBrightness;
            }
        }

        index = memory->FindNamedSel("cerveny pozicni blik");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarkerBlink;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerRedColor;
                light.ambient = MarkerRedAmbient;
                light.brightness = MarkerRedBrightness;
            }
        }

        index = memory->FindNamedSel("zeleny pozicni blik");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarkerBlink;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerGreenColor;
                light.ambient = MarkerGreenAmbient;
                light.brightness = MarkerGreenBrightness;
            }
        }

        index = memory->FindNamedSel("modry pozicni blik");
        if (index >= 0)
        {
            const NamedSelection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                int pt = sel[i];
                const V3& point = memory->Pos(pt);
                LightInfo& light = _lights.Set(_lights.Add());
                light.type = LightTypeMarkerBlink;
                light.position = point;
                light.direction = VUp;
                light.color = MarkerBlueColor;
                light.ambient = MarkerBlueAmbient;
                light.brightness = MarkerBlueBrightness;
            }
        }
    }
    _hitPoints.Clear();
    // must be first of InitShape chain

    // reflectors
    const ParamEntry& par = *_par;
    float armor = GetArmor() * (float)(par >> "armorLights");
    const ParamEntry& reflectors = par >> "Reflectors";
    int n = reflectors.GetEntryCount();
    _reflectors.Resize(n);
    for (int i = 0; i < n; i++)
    {
        _reflectors[i].Load(*this, reflectors.GetEntry(i), armor);
    }

    // hidden selections
    const ParamEntry& hiddenSelections = par >> "hiddenSelections";
    _hiddenSelections.Clear();
    _hiddenSelections.Realloc(hiddenSelections.GetSize());
    for (int i = 0; i < hiddenSelections.GetSize(); i++)
    {
        RString name = hiddenSelections[i];
        int index = _hiddenSelections.Add();
        _hiddenSelections[index].Init(_shape, name, nullptr);
    }

    // reload animations
    const ParamEntry* array = par.FindEntry("ReloadAnimations");
    if (array)
    {
        int n = array->GetEntryCount();
        _reloadAnimations.Realloc(n);
        _reloadAnimations.Resize(n);
        for (int i = 0; i < n; i++)
        {
            const ParamEntry& entry = array->GetEntry(i);
            RStringB weapon = entry >> "weapon";
            _reloadAnimations[i].weapon = WeaponTypes.New(weapon);
            _reloadAnimations[i].animation = AnimationType::CreateObject(entry, _shape);
            _reloadAnimations[i].multiplier = entry >> "multiplier";
        }
    }
    array = par.FindEntry("EventHandlers");
    if (array)
    {
        for (int i = 0; i < NEntityEvent; i++)
        {
            EntityEvent e = (EntityEvent)i;
            RString name = FindEnumName(e);
            const ParamEntry* entry = array->FindEntry(name);
            if (entry)
            {
                RStringB expression = *entry;
                _eventHandlers[e] = expression;
            }
        }
    }

    // user type actions
    array = par.FindEntry("UserActions");
    if (array)
    {
        int n = array->GetEntryCount();
        _userTypeActions.Realloc(n);
        _userTypeActions.Resize(n);
        for (int i = 0; i < n; i++)
        {
            const ParamEntry& entry = array->GetEntry(i);
            _userTypeActions[i].displayName = entry >> "displayName";
            RString pos = entry >> "position";
            _userTypeActions[i].modelPosition = _shape->MemoryPoint(pos);
            _userTypeActions[i].radius = entry >> "radius";
            _userTypeActions[i].condition = entry >> "condition";
            _userTypeActions[i].statement = entry >> "statement";
        }
    }
}

void EntityAIType::DeinitShape()
{
    _hiddenSelections.Clear();
    _reloadAnimations.Clear();

    EntityType::DeinitShape();
}

float EntityAIType::GetIRScanRange() const
{
    float eyeRange = ENGINE_CONFIG.tacticalZ;
    float irRange = eyeRange * _irScanToEyeFactor;
    saturate(irRange, _irScanRangeMin, _irScanRangeMax);
    return irRange;
}

LSError Serialize(ParamArchive& ar, RString name, const EntityType*& value, int minVersion)
{
    if (ar.GetArVersion() < minVersion)
    {
        return LSOK;
    }
    if (ar.IsSaving())
    {
        RString str = value ? value->GetName() : "";
        PARAM_CHECK(ar.Serialize(name, str, minVersion));
    }
    else
    {
        if (ar.GetPass() != ParamArchive::PassFirst)
        {
            return LSOK;
        }
        RString str;
        PARAM_CHECK(ar.Serialize(name, str, minVersion));
        if (str.GetLength() > 0)
        {
            value = VehicleTypes.New(str);
        }
        else
        {
            value = nullptr;
        }
    }
    return LSOK;
}

LSError Serialize(ParamArchive& ar, RString name, const EntityType*& value, int minVersion, const EntityType* defValue)
{
    if (ar.GetArVersion() < minVersion)
    {
        if (!ar.IsSaving())
        {
            value = defValue;
        }
        return LSOK;
    }
    if (ar.IsSaving())
    {
        RString str = value ? value->GetName() : "";
        PARAM_CHECK(ar.Serialize(name, str, minVersion));
    }
    else
    {
        if (ar.GetPass() != ParamArchive::PassFirst)
        {
            return LSOK;
        }
        RString str;
        PARAM_CHECK(ar.Serialize(name, str, minVersion));
        if (str.GetLength() > 0)
        {
            value = VehicleTypes.New(str);
        }
        else
        {
            value = nullptr;
        }
    }
    return LSOK;
}

LSError Serialize(ParamArchive& ar, RString name, const EntityAIType*& value, int minVersion)
{
    const EntityType* tValue = value;
    LSError ok = Poseidon::Serialize(ar, name, tValue, minVersion);
    value = static_cast<const EntityAIType*>(tValue);
    return ok;
}

LSError Serialize(ParamArchive& ar, RString name, const EntityAIType*& value, int minVersion,
                  const EntityAIType* defValue)
{
    const EntityType* tValue = value;
    LSError ok = Poseidon::Serialize(ar, name, tValue, minVersion, defValue);
    value = static_cast<const EntityAIType*>(tValue);
    return ok;
}

VehicleTypeBank VehicleTypes;

DEFINE_CASTING(EntityAI)

/*!
\param type Entity type
\param fullCreate	When fullCreate is false, entity will be serialized
     or transfered over network and many thing need not be initalized
     (e.g. weapon list).
*/

EntityAI::EntityAI(EntityAIType* type, bool fullCreate)
    : Entity(type->_shape, type, -1),

      _lockedBeg(VZero), _lockedSoldier(false), _stratGoToPos(VZero),

      _avoidSpeed(1e5), _avoidSpeedTime(TIME_MIN),

      _avoidAside(0), _avoidAsideWanted(0), // obstacle avoidance offset
      _lastSimplePath(TIME_MIN),

      _isDead(false), _isStopped(false), _isUpsideDown(false), _userStopped(false), _pilotLight(false),

      _showFlag(true), _laserTargetOn(false),

      _inFormation(true),

      _shootVisible(0), _shootAudible(0), _shootTimeRest(1e10), _allowDammage(true),

      _locked(false), _tempLocked(false), _locker(new AILocker()),

      // when we scanned for visible targets
      _trackTargetsTime(TIME_MIN), _trackNearTargetsTime(TIME_MIN), _newTargetsTime(TIME_MIN),
      _nearestEnemy(1e10), // nearest known enemy
      _sensorColID(-1),

      _lastDammageTime(Glob.time - 120), _lastShotTime(Glob.time - 120), _limitSpeed(GetType()->GetMaxSpeedMs() * 2),

      _lastWeaponNotReady(Glob.time), _lastWeaponReady(Glob.time - 10),

      _currentWeapon(-1), _forceFireWeapon(-1), _rearmCredit(0), // used during rearm

      // attack position search
      _attackEngageTime(Glob.time - 60), _attackRefreshTime(Glob.time - 60),

      _attackAggresivePos(VZero), _attackEconomicalPos(VZero),

      // goto automat
      _fireState(FireDone), _fireStateDelay(Glob.time - 60),

      _aimObserverAsked(VZero),

      _nextUserActionId(0)

// fireat/goto automat
{
    AI_ERROR(type == GetType())

    _hit.Realloc(type->_hitPoints.Size());
    _hit.Resize(type->_hitPoints.Size());
    for (int i = 0; i < type->_hitPoints.Size(); i++)
    {
        _hit[i] = 0;
    }

    AI_ERROR(type);
    // Islands legitimately place scope-0 objects.
    if (type && type->IsAbstract())
    {
        LOG_WARN_ONCE_PER(AI, type->GetName(), "Entity created from abstract type '{}' (logged once per type)",
                          (const char*)type->GetName());
    }
    AI_ERROR(_shape == type->_shape);

    if (fullCreate)
    {
        // get default weapon information
        int n = type->NMagazines();
        _magazines.Resize(n);
        for (int i = 0; i < n; i++)
        {
            const MagazineType* mType = type->GetMagazine(i);
            _magazines[i] = new Magazine(mType);
            _magazines[i]->_ammo = mType->_maxAmmo;
            _magazines[i]->_reload = 0;
            if (mType->_modes.Size() > 0)
            {
                float reload = 0.8 + 0.2 * GRandGen.RandomValue();
                _magazines[i]->_reload = reload * mType->_modes[0]->_reloadTime;
            }
            _magazines[i]->_reloadMagazine = 0;
        }

        for (int i = 0; i < type->NWeaponSystems(); i++)
        {
            AddWeapon(const_cast<WeaponType*>(type->GetWeaponSystem(i)));
        }

        AutoReloadAll();
    }

    _targetSide = type->_typicalSide;
    _lastMovement = Glob.time;

    // create reflectors
    int n = type->_reflectors.Size();
    _reflectors.Resize(n);
    LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(CobraLight);
    for (int i = 0; i < n; i++)
    {
        const ReflectorInfo& info = type->_reflectors[i];
        LightReflectorOnVehicle* light = new LightReflectorOnVehicle(shape, info.color, info.colorAmbient, this,
                                                                     info.position, info.direction, info.size);
        light->SetBrightness(info.brightness);
        GScene->AddLight(light);
        _reflectors[i] = light;
    }

    _hiddenSelectionsTextures.Realloc(type->_hiddenSelections.Size());
    _hiddenSelectionsTextures.Resize(type->_hiddenSelections.Size());
}

EntityAI::~EntityAI()
{
    // if there are any weapons left, release their shapes
    for (int i = 0; i < _weapons.Size(); i++)
    {
        WeaponType* weapon = _weapons[i];
        weapon->ShapeRelease();
    }
    _weapons.Clear();

    PerformUnlock();
}

void EntityAI::SetObjectTexture(int index, Texture* texture)
{
    if (index >= 0 && index < _hiddenSelectionsTextures.Size())
    {
        _hiddenSelectionsTextures[index] = texture;
    }
}

bool EntityAI::FindWeapon(const WeaponType* weapon) const
{
    for (int i = 0; i < _weapons.Size(); i++)
    {
        if (_weapons[i] == weapon)
        {
            return true;
        }
    }
    return false;
}

bool EntityAI::EmptySlot(const MagazineSlot& slot) const
{
    const Magazine* magazine = slot._magazine;
    if (!magazine)
    {
        return true;
    }
    if (magazine->_ammo > 0)
    {
        return false;
    }
    const MagazineType* type = magazine->_type;
    if (!type || slot._mode < 0 || slot._mode >= type->_modes.Size())
    {
        return true;
    }
    if (!magazine->_type->_modes[slot._mode]->_ammo)
    {
        return false;
    }
    for (int i = 0; i < NMagazines(); i++)
    {
        const Magazine* reserve = GetMagazine(i);
        if (!reserve || reserve == magazine)
        {
            continue;
        }
        if (reserve->_type != magazine->_type)
        {
            continue;
        }
        if (reserve->_ammo > 0)
        {
            return false;
        }
    }
    return true;
}

int EntityAI::FirstWeapon() const
{
    int maxPrimary = MaxPrimaryLevel();
    // check max. primary level available
    // try to find primary weapon with max level
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = GetMagazineSlot(i);
        if (slot._muzzle->_primary < maxPrimary)
        {
            continue;
        }
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        if (IsHandGunSelected())
        {
            if (slot._weapon->_weaponType & MaskSlotHandGun)
            {
                return i;
            }
        }
        else
        {
            if (slot._weapon->_weaponType & MaskSlotPrimary)
            {
                return i;
            }
        }
    }
    // find any weapon with max level
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = GetMagazineSlot(i);
        if (slot._muzzle->_primary < maxPrimary)
        {
            continue;
        }
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        if (IsHandGunSelected())
        {
            // ignore primary weapon
            if (slot._weapon->_weaponType & MaskSlotPrimary)
            {
                continue;
            }
        }
        else
        {
            // ignore handgun
            if (slot._weapon->_weaponType & MaskSlotHandGun)
            {
                continue;
            }
        }
        return i;
    }
    return -1;
}

int EntityAI::MaxPrimaryLevel() const
{
    int maxPrimary = 0;
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = GetMagazineSlot(i);
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        if (!slot._magazine)
        {
            continue;
        }
        if (slot._magazine->_ammo <= 0)
        {
            continue;
        }
        saturateMax(maxPrimary, slot._muzzle->_primary);
    }
    if (maxPrimary > 0)
    {
        return maxPrimary;
    }
    // check max. primary level of all weapons
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = GetMagazineSlot(i);
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        saturateMax(maxPrimary, slot._muzzle->_primary);
    }
    return maxPrimary;
}

int EntityAI::NextWeapon(int weapon) const
{
    // check max. primary level
    int maxPrimary = MaxPrimaryLevel();

    if (weapon >= 0)
    {
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        if (slot._muzzle->_primary < maxPrimary)
        {
            return FirstWeapon();
        }
    }

    int n = NMagazineSlots();

    for (int i = 0; i < n; i++)
    {
        weapon++;
        if (weapon > n - 1)
        {
            weapon = 0;
        }
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        if (slot._muzzle->_primary < maxPrimary)
        {
            continue;
        }
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        if (IsHandGunSelected())
        {
            // ignore primary weapon
            if (slot._weapon->_weaponType & MaskSlotPrimary)
            {
                continue;
            }
        }
        else
        {
            // ignore handgun
            if (slot._weapon->_weaponType & MaskSlotHandGun)
            {
                continue;
            }
        }
        return weapon;
    }
    return -1;
}

int EntityAI::PrevWeapon(int weapon) const
{
    int maxPrimary = MaxPrimaryLevel();
    if (weapon >= 0)
    {
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        if (slot._muzzle->_primary < maxPrimary)
        {
            return FirstWeapon();
        }
    }

    int n = NMagazineSlots();

    for (int i = 0; i < n; i++)
    {
        weapon--;
        if (weapon < 0)
        {
            weapon = n - 1;
        }
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        if (slot._muzzle->_primary < maxPrimary)
        {
            continue;
        }
        if (!slot._muzzle->_showEmpty && EmptySlot(slot))
        {
            continue;
        }
        if (IsHandGunSelected())
        {
            // ignore primary weapon
            if (slot._weapon->_weaponType & MaskSlotPrimary)
            {
                continue;
            }
        }
        else
        {
            // ignore handgun
            if (slot._weapon->_weaponType & MaskSlotHandGun)
            {
                continue;
            }
        }
        return weapon;
    }
    return -1;
}

int EntityAI::FindWeaponType(int maskInclude, int maskExclude) const
{
    for (int i = 0; i < NWeaponSystems(); i++)
    {
        const WeaponType* weapon = GetWeaponSystem(i);
        if ((weapon->_weaponType & maskInclude) != 0 && (weapon->_weaponType & maskExclude) == 0)
        {
            return i;
        }
    }
    return -1;
}

/*!
\param magazine checked magazine
\return true if magazine found, false otherwise
*/
bool EntityAI::FindMagazine(const Magazine* magazine) const
{
    for (int i = 0; i < _magazines.Size(); i++)
    {
        if (_magazines[i] == magazine)
        {
            return true;
        }
    }
    return false;
}

/*!
\param weapon checked type of weapon
\return true, if weapon can be added - in this case conflict contains weapons must be removed
*/
bool EntityAI::CheckWeapon(const WeaponType* weapon, AutoArray<Ref<const WeaponType>, MemAllocSA>& conflict) const
{
    if (FindWeapon(weapon))
    {
        return false;
    }

    int slots = weapon->_weaponType;
    if (slots == 0)
    {
        return true;
    }

    int maxSlots = GetType()->_weaponSlots;

    AI_ERROR((slots & MaskSlotItem) == 0);
    const int primaryMax = maxSlots & MaskSlotPrimary;
    const int secondaryMax = maxSlots & MaskSlotSecondary;
    const int binocularMax = maxSlots & MaskSlotBinocular;
    const int handgunMax = maxSlots & MaskSlotHandGun;
    int primary = slots & MaskSlotPrimary;
    int secondary = slots & MaskSlotSecondary;
    int binocular = slots & MaskSlotBinocular;
    int handgun = slots & MaskSlotHandGun;
    if (primary > primaryMax)
    {
        return false;
    }
    if (secondary > secondaryMax)
    {
        return false;
    }
    if (binocular > binocularMax)
    {
        return false;
    }
    if (handgun > handgunMax)
    {
        return false;
    }

    // check weapons
    for (int i = 0; i < _weapons.Size(); i++)
    {
        // check duplicity
        const WeaponType* weaponI = _weapons[i];
        // check slots
        int slotsI = weaponI->_weaponType;
        int primaryI = slotsI & MaskSlotPrimary;
        int secondaryI = slotsI & MaskSlotSecondary;
        int binocularI = slotsI & MaskSlotBinocular;
        int handgunI = slotsI & MaskSlotHandGun;

        if (primary + primaryI > primaryMax)
        {
            conflict.Add(weaponI);
        }
        else
        {
            primary += primaryI;
        }
        if (secondary + secondaryI > secondaryMax)
        {
            conflict.Add(weaponI);
        }
        else
        {
            secondary += secondaryI;
        }
        if (binocular + binocularI > binocularMax)
        {
            conflict.Add(weaponI);
        }
        else
        {
            binocular += binocularI;
        }
        if (handgun + handgunI > handgunMax)
        {
            conflict.Add(weaponI);
        }
        else
        {
            handgun += handgunI;
        }
    }
    return true;
}

int CmpMagazines(const Ref<const Magazine>* pm1, const Ref<const Magazine>* pm2, const EntityAI* veh)
{
    const Magazine* m1 = *pm1;
    const Magazine* m2 = *pm2;

    // first remove empty magazines
    bool empty1 = m1->_ammo == 0;
    bool empty2 = m2->_ammo == 0;
    if (empty1 && !empty2)
    {
        return -1;
    }
    if (empty2 && !empty1)
    {
        return 1;
    }

    // then unused magazines
    bool used1 = false;
    bool used2 = false;
    for (int i = 0; i < veh->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = veh->GetWeaponSystem(i);
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            for (int k = 0; k < muzzle->_magazines.Size(); k++)
            {
                const MagazineType* type = muzzle->_magazines[k];
                if (type == m1->_type)
                {
                    used1 = true;
                }
                if (type == m2->_type)
                {
                    used2 = true;
                }
            }
        }
    }
    if (used1 && !used2)
    {
        return 1;
    }
    if (used2 && !used1)
    {
        return -1;
    }

    // then less full
    float full1 = m1->_type->_maxAmmo > 0 ? m1->_ammo / m1->_type->_maxAmmo : 1;
    float full2 = m2->_type->_maxAmmo > 0 ? m2->_ammo / m2->_type->_maxAmmo : 1;
    return sign(full1 - full2);
}

/*!
\param magazine checked magazine
\return true, if magazine can be added - in this case conflict contains magazines must be removed
*/
bool EntityAI::CheckMagazine(const Magazine* magazine, AutoArray<Ref<const Magazine>, MemAllocSA>& conflict) const
{
    int slots = magazine->_type->_magazineType;
    if (slots == 0)
    {
        return true;
    }

    int maxSlots = GetType()->_weaponSlots;

    AI_ERROR((slots & (MaskSlotItem | MaskSlotHandGunItem)) == slots);
    const int itemMax = maxSlots & MaskSlotItem;
    const int itemMaxHandGun = maxSlots & MaskSlotHandGunItem;
    int item = slots & MaskSlotItem;
    int itemHandGun = slots & MaskSlotHandGunItem;
    int reserve = itemMax - item;
    int reserveHandGun = itemMaxHandGun - itemHandGun;
    if (reserve <= 0 && reserveHandGun <= 0)
    {
        return false;
    }

    // check magazines
    for (int i = 0; i < _magazines.Size(); i++)
    {
        const Magazine* magazineI = _magazines[i];
        // check slots
        int itemI = magazineI->_type->_magazineType & MaskSlotItem;
        int itemIHandGun = magazineI->_type->_magazineType & MaskSlotHandGunItem;
        if (itemI > 0 || itemIHandGun > 0)
        {
            reserve -= itemI;
            reserveHandGun -= itemIHandGun;
            // check if not used
            if (IsMagazineUsed(magazineI))
            {
                continue;
            }
            // do not change magazines of the same type (except empty)
            if (magazineI->_type != magazine->_type || magazineI->_ammo == 0)
            {
                conflict.Add(magazineI); // candidate for remove
            }
        }
    }

    if (reserve >= 0 && reserveHandGun >= 0)
    {
        conflict.Clear();
        return true;
    }

    QSort(conflict.Data(), conflict.Size(), this, CmpMagazines);
    for (int i = 0; i < conflict.Size();)
    {
        const Magazine* magazineI = conflict[i];
        int itemI = magazineI->_type->_magazineType & MaskSlotItem;
        int itemIHandGun = magazineI->_type->_magazineType & MaskSlotHandGunItem;
        reserve += itemI;
        reserveHandGun += itemIHandGun;
        if (itemI != 0)
        {
            AI_ERROR((itemI & MaskSlotItem) == itemI);
            if (reserve >= itemI)
            {
                conflict.Delete(i);
            }
            else
            {
                i++;
            }
        }
        else if (itemIHandGun != 0)
        {
            AI_ERROR((itemIHandGun & MaskSlotHandGunItem) == itemIHandGun);
            if (reserveHandGun >= itemIHandGun)
            {
                conflict.Delete(i);
            }
            else
            {
                i++;
            }
        }
        else
        {
            Fail("Unexpected magazine type");
            conflict.Delete(i);
        }
    }
    if (reserve >= 0 && reserveHandGun >= 0)
    {
        return true;
    }
    conflict.Clear();
    return false;
}

/*!
\param magazine checked magazine taype
\return true, if magazine fits (can be loaded) in some weapon
*/
bool EntityAI::IsMagazineUsable(const MagazineType* magazine) const
{
    for (int i = 0; i < _weapons.Size(); i++)
    {
        const WeaponType* weapon = _weapons[i];
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            for (int k = 0; k < muzzle->_magazines.Size(); k++)
            {
                if (muzzle->_magazines[k] == magazine)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool CheckAccess(const ParamEntry& entry);
bool CheckAccessCreate(const ParamEntry& entry);

/*!
\param weapon weapon type
\param force add weapon even if no free slots (bypass check)
\param reload try to reload weapon immediatelly
\return index of new weapon in array
*/
int EntityAI::AddWeapon(WeaponType* weapon, bool force, bool reload, bool checkSelected)
{
    if (!CheckAccessCreate(*weapon->_parClass))
    {
        return -1;
    }
    // add weapon
    if (!force)
    {
        AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
        if (!CheckWeapon(weapon, conflict))
        {
            return -1;
        }
        if (conflict.Size() > 0)
        {
            return -1;
        }
    }
    weapon->ShapeAddRef();
    int index = _weapons.Add(weapon);

    // add magazine slots
    for (int j = 0; j < weapon->_muzzles.Size(); j++)
    {
        MuzzleType* muzzle = weapon->_muzzles[j];

        Magazine* magazine = nullptr;
        if (reload)
        {
            int index = FindMagazineByType(muzzle);
            if (index >= 0)
            {
                magazine = GetMagazine(index);
            }
        }
        for (int mode = 0; mode < muzzle->_nModes; mode++)
        {
            int slot = _magazineSlots.Add();
            _magazineSlots[slot]._weapon = weapon;
            _magazineSlots[slot]._muzzle = muzzle;
            _magazineSlots[slot]._mode = mode;
            _magazineSlots[slot]._magazine = magazine;
        }
    }
    if (checkSelected)
    {
        OnWeaponAdded();
    }
    return index;
}

void EntityAI::OnWeaponAdded() {}

void EntityAI::OnWeaponRemoved() {}

void EntityAI::OnWeaponChanged()
{
    if (_weapons.Size() > 0)
    {
        SelectWeapon(FirstWeapon(), true);
    }
    else
    {
        SelectWeapon(-1, true);
    }
    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
    {
        GWorld->UI()->ResetVehicle(this);
    }
}

void EntityAI::OnDanger() {}

/*!
\param name weapon type name
\param force add weapon even if no free slots (bypass check)
\param reload try to reload weapon immediatelly
\return index of new weapon in array
*/
int EntityAI::AddWeapon(RStringB name, bool force, bool reload, bool checkSelected)
{
    Ref<WeaponType> weapon = WeaponTypes.New(name);
    return AddWeapon(weapon, force, reload, checkSelected);
}

/*!
\param name weapon type name
*/
void EntityAI::RemoveWeapon(RStringB name, bool checkSelected)
{
    // find and remove weapon
    Ref<const WeaponType> weapon;
    for (int i = 0; i < _weapons.Size(); i++)
    {
        if (stricmp(_weapons[i]->GetName(), name) == 0)
        {
            weapon = _weapons[i];
            // deinit muzzles
            weapon->ShapeRelease();
            // remove weapon
            _weapons.Delete(i);
            break;
        }
    }
    if (!weapon)
    {
        return;
    }
    // remove attached magazine slots
    for (int i = 0; i < _magazineSlots.Size();)
    {
        if (_magazineSlots[i]._weapon == weapon)
        {
            _magazineSlots.Delete(i);
        }
        else
        {
            i++;
        }
    }
    if (checkSelected)
    {
        OnWeaponRemoved();
        if (_weapons.Size() > 0)
        {
            SelectWeapon(FirstWeapon(), true);
        }
        else
        {
            SelectWeapon(-1, true);
        }
        if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
        {
            GWorld->UI()->ResetVehicle(this);
        }
    }
}

/*!
\param weapon weapon type
*/
void EntityAI::RemoveWeapon(const WeaponType* weapon, bool checkSelected)
{
    // find and remove weapon
    bool found = false;
    for (int i = 0; i < _weapons.Size(); i++)
    {
        if (_weapons[i] == weapon)
        {
            found = true;
            // deinit muzzle
            weapon->ShapeRelease();

            _weapons.Delete(i);
            break;
        }
    }
    if (!found)
    {
        return;
    }
    // remove attached magazine slots
    for (int i = 0; i < _magazineSlots.Size();)
    {
        if (_magazineSlots[i]._weapon == weapon)
        {
            _magazineSlots.Delete(i);
        }
        else
        {
            i++;
        }
    }
    if (checkSelected)
    {
        OnWeaponRemoved();
        if (_weapons.Size() > 0)
        {
            SelectWeapon(FirstWeapon(), true);
        }
        else
        {
            SelectWeapon(-1, true);
        }
        if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
        {
            GWorld->UI()->ResetVehicle(this);
        }
    }
}

void EntityAI::RemoveAllWeapons()
{
    for (int i = 0; i < _weapons.Size(); i++)
    {
        _weapons[i]->ShapeRelease();
    }
    _weapons.Clear();
    _magazineSlots.Clear();
    OnWeaponRemoved();
    SelectWeapon(-1, true);
    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
    {
        GWorld->UI()->ResetVehicle(this);
    }
}

void EntityAI::MinimalWeapons()
{
    RemoveAllMagazines();
}

/*!
\param name magazine type name
\param force add magazine even if no free slots (bypass check)
\param autoreload reload magazine into weapon immediatelly
\return index of new weapon in array
*/
int EntityAI::AddMagazine(Magazine* magazine, bool force, bool autoload)
{
    if (!CheckAccessCreate(*magazine->_type->_parClass))
    {
        return -1;
    }
    // add magazine type
    if (!force)
    {
        AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
        if (!CheckMagazine(magazine, conflict))
        {
            return -1;
        }
        if (conflict.Size() > 0)
        {
            return -1;
        }
    }
    int index = _magazines.Add(magazine);

#if _DEBUG
    CheckMagazines();
#endif

    if (autoload)
    {
        for (int weapon = 0; weapon < NMagazineSlots(); weapon++)
        {
            const MagazineSlot& slot = GetMagazineSlot(weapon);
            const MuzzleType* muzzle = slot._muzzle;
            if (!muzzle)
            {
                continue;
            }
            // if slot is not empty, there is no need to reload
            if (slot._magazine && slot._magazine->_ammo > 0)
            {
                continue;
            }

            for (int k = 0; k < muzzle->_magazines.Size(); k++)
            {
                if (muzzle->_magazines[k] == magazine->_type)
                {
                    AutoReload(weapon);
                    return index;
                }
            }
        }
    }

    return index;
}

/*!
\param name magazine type name
\param force add magazine even if no free slots (bypass check)
\return index of new weapon in array
*/
int EntityAI::AddMagazine(RStringB name, bool force, int ammunition /*= 0*/)
{
    Ref<MagazineType> magazineType = MagazineTypes.New(name);
    Ref<Magazine> magazine = new Magazine(magazineType);
    magazine->_ammo = magazineType->_maxAmmo;
    if (ammunition > 0)
    {
        magazine->_ammo = ammunition;
    }
    int index = AddMagazine(magazine, force);
    if (index < 0)
    {
        return index;
    }

    magazine->_reload = 0;
    if (magazine->_type->_modes.Size() > 0)
    {
        float reload = 0.8 + 0.2 * GRandGen.RandomValue();
        magazine->_reload = reload * magazine->_type->_modes[0]->_reloadTime;
    }
    magazine->_reloadMagazine = 0;
    return index;
}

/*!
\param name magazine type name
*/
void EntityAI::RemoveMagazine(RStringB name)
{
    for (int i = 0; i < _magazines.Size(); i++)
    {
        Ref<Magazine> magazine = _magazines[i];
        if (stricmp(magazine->_type->GetName(), name) == 0)
        {
            _magazines.Delete(i);
            for (int j = 0; j < _magazineSlots.Size();)
            {
                if (_magazineSlots[j]._magazine == magazine)
                {
                    _magazineSlots[j]._magazine = nullptr;
                }
                else
                {
                    j++;
                }
            }
            return;
        }
    }
}

/*!
\param magazine magazine to remove
*/
void EntityAI::RemoveMagazine(const Magazine* magazine)
{
    for (int i = 0; i < _magazines.Size(); i++)
    {
        if (_magazines[i] == magazine)
        {
            _magazines.Delete(i);
            for (int j = 0; j < _magazineSlots.Size();)
            {
                if (_magazineSlots[j]._magazine == magazine)
                {
                    _magazineSlots[j]._magazine = nullptr;
                }
                else
                {
                    j++;
                }
            }
            return;
        }
    }
}

/*!
\param name name of magazine type to remove
*/
void EntityAI::RemoveMagazines(RStringB name)
{
    for (int i = 0; i < _magazines.Size();)
    {
        Ref<Magazine> magazine = _magazines[i];
        if (stricmp(magazine->_type->GetName(), name) == 0)
        {
            _magazines.Delete(i);
            for (int j = 0; j < _magazineSlots.Size(); j++)
            {
                if (_magazineSlots[j]._magazine == magazine)
                {
                    _magazineSlots[j]._magazine = nullptr;
                }
            }
        }
        else
        {
            i++;
        }
    }
}

void EntityAI::RemoveAllMagazines()
{
    _magazines.Clear();
    for (int i = 0; i < _magazineSlots.Size(); i++)
    {
        _magazineSlots[i]._magazine = nullptr;
    }
}

/*!
\param muzzle muzzle where magazine will be attached
\param magazine attached magazine
*/
void EntityAI::AttachMagazine(const MuzzleType* muzzle, Magazine* magazine)
{
    if (!muzzle->CanUse(magazine->_type))
    {
        RptF("Cannot use magazine %s in muzzle %s", (const char*)magazine->_type->GetName(),
             (const char*)muzzle->GetName());
        return;
    }
    for (int i = 0; i < _magazineSlots.Size(); i++)
    {
        if (_magazineSlots[i]._muzzle == muzzle)
        {
            _magazineSlots[i]._magazine = magazine;
        }
    }
}

const MagazineSlot* EntityAI::GetmagazineSlotByMuzzle(const RString& name) const
{
    const int idx = _magazineSlots.IdxOfMuzzle(name);
    if (idx == -1)
    {
        return nullptr;
    }
    return &_magazineSlots[idx];
}

bool EntityAI::IsActionInProgress(MoveFinishF action) const
{
    return false;
}

void EntityAI::SelectWeaponCommander(AIUnit* unit, int weapon)
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        weapon = -1;
    }
    if (IsLocal())
    {
        SelectWeapon(weapon);
    }
    else if (unit == GunnerUnit() && unit->GetPerson()->IsLocal())
    {
        SelectWeapon(weapon);
        GetNetworkManager().UpdateWeapons(this);
    }
    else
    {
        GetNetworkManager().AskForSelectWeapon(this, weapon);
    }
}

void EntityAI::SelectWeapon(int weapon, bool changed)
{
    if (!changed && _currentWeapon == weapon)
    {
        return;
    }
    if (_currentWeapon >= 0 && _currentWeapon < NMagazineSlots())
    {
        // safefy patch:
        // if new weapon is selected, cancel rest of the burst
        const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
        Magazine* magazine = slot._magazine;

        if (magazine)
        {
            magazine->_burstLeft = 0;
        }
    }

    _currentWeapon = weapon;

    // check if handgun or primary weapon was selected
    if (_currentWeapon >= 0 && _currentWeapon < NMagazineSlots())
    {
        const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
        WeaponType* weaponType = slot._weapon;
        if (weaponType)
        {
            int mask = weaponType->_weaponType;
            if (mask & MaskSlotPrimary)
            {
                SelectHandGun(false);
            }
            else if (mask & MaskSlotHandGun)
            {
                SelectHandGun(true);
            }
        }
    }
}

/*!
During some operations weapon manipulation is not possible
like during burst or during reload animation
*/

bool EntityAI::EnableWeaponManipulation() const
{
    /**/
    // during some operations weapon manipulation is not possible
    // like during burst or during reload animation
    // check if we are in the middle of the burst
    if (_currentWeapon >= 0 && _currentWeapon < NMagazineSlots())
    {
        // safefy patch:
        // if new weapon is selected, cancel rest of the burst
        const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
        Magazine* magazine = slot._magazine;

        if (magazine)
        {
            if (magazine->_burstLeft > 0)
            {
                return false;
            }
        }
    }
    /**/

    return true;
}

bool EntityAI::EnableViewThroughOptics() const
{
    // check if unit is able to use optics
    return true;
}

float EntityAI::GetHit(const HitPoint& hitpoint) const
{
    int index = hitpoint.GetIndex();
    if (index >= 0)
    {
        // discreete hit simulation
        if (_hit[index] >= 0.9)
        {
            return 1;
        }
    }
    return 0;
}

float EntityAI::GetHitCont(const HitPoint& hitpoint) const
{
    int index = hitpoint.GetIndex();
    if (index >= 0)
    {
        // continuous hit simulation
        return _hit[index];
    }
    return 0;
}

/*!\param kind
 * - 0 hull
 * - 1 engine
 * - 2 left track
 * - 3 right track
 * - 4 turret
 * - 5 gun
 */

float EntityAI::GetHitForDisplay(int kind) const
{
    return 0;
}

bool EntityAI::CanFire() const
{
    if (_isDead)
    {
        return false;
    }
    if (_isUpsideDown)
    {
        return false;
    }
    return true;
}

bool EntityAI::IsAbleToMove() const
{
    return CanFire();
}

bool EntityAI::IsAbleToFire() const
{
    // do we have some ammo
    if (NMagazineSlots() == 0)
    {
        return false;
    }
    return CanFire();
}

bool EntityAI::IsCautious() const
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return false;
    }
    CombatMode mode = unit->GetCombatMode();
    return mode == CMStealth || mode == CMCombat || mode == CMAware;
}

bool EntityAI::IsCautiousOrDanger() const
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return false;
    }
    if (unit->IsDanger())
    {
        return true;
    }
    return IsCautious();
}

float EntityAI::CalculateTotalCost() const
{
    return GetType()->GetCost();
}

float EntityAI::CalculateExposure(int x, int z) const
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return 0;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return 0;
    }
    AICenter* center = grp->GetCenter();
    if (!center)
    {
        return 0;
    }

    float expField = unit->IsHoldingFire() ? center->GetExposurePessimistic(x, z) : center->GetExposureOptimistic(x, z);
    expField *= 1.0f / 60.0f;

    float cost = CalculateTotalCost();
    float dammage = expField * GetType()->GetInvArmor();

    bool ableToDefend = GetAmmoHit() > 50 && !unit->IsHoldingFire();
    if (!ableToDefend)
    {
        dammage *= 4;
    }

    return dammage * cost;
}

void EntityAI::ForgetAimTarget()
{
    _fire._fireTarget = nullptr;
}

bool EntityAI::StopAtStrategicPos() const
{
    return true;
}

void EntityAI::GoToStrategic(Vector3Par pos)
{
    // if destionation has changed, we cannot be in position
    if (_stratGoToPos.Distance2(pos) > 0.5)
    {
        _inFormation = false;
    }
    _stratGoToPos = pos;
}

} // namespace Poseidon
