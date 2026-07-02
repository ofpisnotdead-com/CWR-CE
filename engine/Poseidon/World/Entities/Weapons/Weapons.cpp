#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
void RandomSound::Load(const ParamEntry& entry, const char* name)
{
    const ParamEntry& list = entry >> name;
    _pars.Realloc(list.GetSize() / 2);
    _pars.Resize(0);
    for (int i = 0; i < list.GetSize(); i += 2)
    {
        RString singleName = list[i];
        // load single sound parameter
        SoundProbab extPars;
        const ParamEntry& single = entry >> singleName;
        GetValue(extPars, single);
        extPars._probability = list[i + 1];
        _pars.Add(extPars);
    }
    _pars.Compact();
}

const SoundPars& RandomSound::SelectSound(float probab) const
{
    PoseidonAssert(_pars.Size() > 0);
    if (_pars.Size() <= 0)
    {
        static const SoundPars nil = {};
        return nil;
    }
    int i;
    for (i = 0; i < _pars.Size() - 1; i++)
    {
        probab -= _pars[i]._probability;
        if (probab <= 0)
        {
            return _pars[i];
        }
    }
    return _pars[i];
}

void AmmoType::InitShape()
{
    base::InitShape();
    RStringB cartridge = ParClass() >> "cartridge";
    if (cartridge.GetLength() > 0)
    {
        VehicleNonAIType* type = VehicleTypes.New(cartridge);
        VehicleType* typeAI = dynamic_cast<VehicleType*>(type);
        if (typeAI)
        {
            _cartridgeType = typeAI;
            _cartridgeType->VehicleAddRef(); // force loading shape
        }
    }
    RStringB proxyShape = ParClass() >> "proxyShape";
    if (proxyShape.GetLength() > 0)
    {
        LODShapeWithShadow* pshape = Shapes.New(::GetShapeName(proxyShape), false, false);
        _proxyShape = pshape;
    }
}

void AmmoType::DeinitShape()
{
    if (_cartridgeType)
    {
        _cartridgeType->VehicleRelease(); // allow releasing shape
        _cartridgeType.Free();
    }
    _proxyShape.Free();
    base::DeinitShape();
}

void AmmoType::Load(const ParamEntry& par)
{
    base::Load(par);

#define GET_PAR(x) x = ParClass(#x)
    GET_PAR(cost);
    GET_PAR(hit);
    GET_PAR(indirectHit);
    GET_PAR(indirectHitRange);
    GET_PAR(minRange), GET_PAR(minRangeProbab);
    GET_PAR(midRange), GET_PAR(midRangeProbab);
    GET_PAR(maxRange), GET_PAR(maxRangeProbab);
    GET_PAR(maxControlRange);
    GET_PAR(maneuvrability);
    GET_PAR(thrust);
    GET_PAR(thrustTime);
    GET_PAR(initTime);
    GET_PAR(maxSpeed);
    GET_PAR(sideAirFriction);

    GET_PAR(simulationStep);

    GET_PAR(visibleFire);
    GET_PAR(audibleFire);
    GET_PAR(visibleFireTime);

    GET_PAR(irLock);
    GET_PAR(laserLock);

    GET_PAR(airLock);
    GET_PAR(manualControl);
    GET_PAR(explosive);

    const ParamEntry* entry = par.FindEntry("defaultMagazine");
    if (entry)
    {
        _defaultMagazine = *entry;
    }

    invMidRangeMinusMinRange = 1 / (midRange - minRange);
    invMidRangeMinusMaxRange = 1 / (midRange - maxRange);

    // premultiply with probabilities
    invMidRangeMinusMinRange *= (midRangeProbab - minRangeProbab);
    invMidRangeMinusMaxRange *= (midRangeProbab - maxRangeProbab);

    // ammo is by default reversed
    _shapeReversed = true;

    _texture = nullptr;
    _hitGround.Load(ParClass(), "hitGround");
    _hitMan.Load(ParClass(), "hitMan");
    _hitArmor.Load(ParClass(), "hitArmor");
    _hitBuilding.Load(ParClass(), "hitBuilding");
    GetValue(_soundFly, ParClass("soundFly"));
    GetValue(_soundEngine, ParClass("soundEngine"));

    _tracerColor = GetPackedColor(ParClass("tracerColor"));
    _tracerColorR = GetPackedColor(ParClass("tracerColorR"));

    RString simName = ParClass("simulation");
    _simulation = AmmoNone;
    if (!strcmpi(simName, "shotShell"))
    {
        _simulation = AmmoShotShell;
    }
    else if (!strcmpi(simName, "shotMissile"))
    {
        _simulation = AmmoShotMissile;
    }
    else if (!strcmpi(simName, "shotRocket"))
    {
        _simulation = AmmoShotMissile;
    }
    else if (!strcmpi(simName, "shotBullet"))
    {
        _simulation = AmmoShotBullet;
    }
    else if (!strcmpi(simName, "shotIlluminating"))
    {
        _simulation = AmmoShotIlluminating;
    }
    else if (!strcmpi(simName, "shotSmoke"))
    {
        _simulation = AmmoShotSmoke;
    }
    else if (!strcmpi(simName, "shotTimeBomb"))
    {
        _simulation = AmmoShotTimeBomb;
    }
    else if (!strcmpi(simName, "shotPipeBomb"))
    {
        _simulation = AmmoShotPipeBomb;
    }
    else if (!strcmpi(simName, "shotMine"))
    {
        _simulation = AmmoShotMine;
    }
    else if (!strcmpi(simName, "shotStroke"))
    {
        _simulation = AmmoShotStroke;
    }
    else if (!strcmpi(simName, "laserDesignate"))
    {
        _simulation = AmmoShotLaser;
    }
    else if (!strcmpi(simName, ""))
    {
        _simulation = AmmoNone;
    }
    else
    {
        LOG_ERROR(Physics, "{}: Bad ammo simulation {}", (const char*)GetName(), (const char*)simName);
    }
}

AmmoType::AmmoType(const ParamEntry* name) : VehicleNonAIType(name) {}

WeaponModeType::WeaponModeType() = default;

void WeaponModeType::Init(const ParamEntry& cls)
{
    _parClass = &cls;

    RStringB ammo = cls >> "ammo";
    if (ammo.GetLength() > 0)
    {
        VehicleNonAIType* type = VehicleTypes.New(ammo);
        if (!type)
        {
            LOG_ERROR(Physics, "No class {}", (const char*)ammo);
        }
        _ammo = dynamic_cast<AmmoType*>(type);
        if (!_ammo)
        {
            LOG_ERROR(Physics, "No ammo class {}", (const char*)ammo);
        }
    }
    else
    {
        _ammo = nullptr;
    }
    _displayName.Bind(cls >> "displayName");
    _mult = cls >> "multiplier";
    _burst = cls >> "burst";

    GetValue(_sound, cls >> "sound");
    _reloadTime = cls >> "reloadTime";
    _dispersion = cls >> "dispersion";
    _ffCount = cls >> "ffCount";
    _recoilName = cls >> "recoil";
    _autoFire = cls >> "autoFire";
    _aiRateOfFire = cls >> "aiRateOfFire";
    _aiRateOfFireDistance = cls >> "aiRateOfFireDistance";
    _soundContinuous = cls >> "soundContinuous";

    _useAction = cls >> "useAction";
    _useActionTitle = cls >> "useActionTitle";
}

MagazineType::MagazineType()
{
    _modelRefCount = 0;
    _magazineShapeRef = 0;
}

void MagazineType::InitShape() const
{
    for (int i = 0; i < _modes.Size(); i++)
    {
        WeaponModeType* mode = _modes[i];
        const AmmoType* ammo = mode->_ammo;
        if (ammo)
        {
            ammo->VehicleAddRef();
        }
    }

    RStringB wModelName = (*_parClass) >> "modelSpecial";
    if (wModelName.GetLength() > 0)
    {
        _model = Shapes.New(GetShapeName(wModelName), false, false);
        if (_model)
        {
            _animFire.Init(_model, "zasleh", nullptr);
        }
    }
    else
    {
        _model.Free();
    }
}
void MagazineType::DeinitShape() const
{
    for (int i = 0; i < _modes.Size(); i++)
    {
        WeaponModeType* mode = _modes[i];
        const AmmoType* ammo = mode->_ammo;
        if (ammo)
        {
            ammo->VehicleRelease();
        }
    }
}

void MagazineType::AmmoAddRef() const
{
    if (_modelRefCount++ == 0)
    {
        InitShape();
    }
}

void MagazineType::AmmoRelease() const
{
    if (--_modelRefCount == 0)
    {
        DeinitShape();
    }
}

void MagazineType::InitMagazineShape() const
{
    RStringB wModelName = (*_parClass) >> "modelMagazine";
    if (wModelName.GetLength() > 0)
    {
        _modelMagazine = Shapes.New(GetShapeName(wModelName), false, false);
    }
    else
    {
        _modelMagazine.Free();
    }
}

void MagazineType::DeinitMagazineShape() const
{
    _modelMagazine.Free();
}

void MagazineType::MagazineShapeAddRef() const
{
    if (_magazineShapeRef++ == 0)
    {
        InitMagazineShape();
    }
}

void MagazineType::MagazineShapeRelease() const
{
    if (--_magazineShapeRef == 0)
    {
        DeinitMagazineShape();
    }
}

RStringB MagazineType::GetPictureName() const
{
    return _picName.GetLength() > 0 ? _picName : GetName();
}

void MagazineType::Init(const char* name)
{
    const ParamEntry& cls = Pars >> "CfgWeapons" >> name;

    _parClass = &cls;

    _picName = cls >> "picture";
    _scope = cls >> "scopeMagazine";
    _displayName.Bind(cls >> "displayNameMagazine");
    _shortName = cls >> "shortNameMagazine";
    _nameSound = cls >> "nameSound";
    _magazineType = cls >> "magazineType";
    _maxAmmo = cls >> "count";
    _maxLeadSpeed = cls >> "maxLeadSpeed";
    _initSpeed = cls >> "initSpeed";
    _invInitSpeed = 1.0 / _initSpeed;
    _reloadAction = ManAction(int(cls >> "reloadAction"));

    const ParamEntry* entry = cls.FindEntry("valueMagazine");
    if (entry)
    {
        _value = *entry;
    }
    else
    {
        _value = 1;
    }

    int n = (cls >> "modes").GetSize();
    _modes.Resize(n);
    for (int i = 0; i < n; i++)
    {
        _modes[i] = new WeaponModeType();
        RStringB mode = (cls >> "modes")[i];
        if (stricmp(mode, "this") == 0)
        {
            _modes[i]->Init(cls);
        }
        else
        {
            _modes[i]->Init(cls >> mode);
        }
    }

    entry = cls.FindEntry("useAction");
    if (entry)
    {
        _useAction = *entry;
    }
    else
    {
        _useAction = false;
    }

    entry = cls.FindEntry("useActionTitle");
    if (entry)
    {
        _useActionTitle = *entry;
    }
    else
    {
        _useActionTitle = RString();
    }
}

LSError MagazineType::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving())
    {
        RString name = GetName();
        PARAM_CHECK(ar.Serialize("name", name, 1));
    }
    return LSOK;
}

MagazineType* MagazineType::CreateObject(ParamArchive& ar)
{
    RString name;
    if (ar.Serialize("name", name, 1) != LSOK)
    {
        return nullptr;
    }
    return MagazineTypes.New(name);
}

MuzzleType::MuzzleType() = default;

MuzzleType::~MuzzleType() = default;

void MuzzleType::Init(const ParamEntry& cls, const WeaponType* weapon)
{
    _parClass = &cls;

    _displayName.Bind(cls >> "displayName");
    _magazineReloadTime = cls >> "magazineReloadTime";
    GetValue(_sound, cls >> "drySound");
    _soundContinuous = cls >> "soundContinuous";
    GetValue(_reloadSound, cls >> "reloadSound");
    GetValue(_reloadMagazineSound, cls >> "reloadMagazineSound");
    _reloadSoundDuration = _reloadMagazineSoundDuration = 0;
    if (_reloadSound.name.GetLength() > 0 && GSoundsys)
    {
        _reloadSoundDuration = GSoundsys->GetWaveDuration(_reloadSound.name);
    }
    if (_reloadMagazineSound.name.GetLength() > 0 && GSoundsys)
    {
        _reloadMagazineSoundDuration = GSoundsys->GetWaveDuration(_reloadMagazineSound.name);
    }
    _aiDispersionCoefX = cls >> "aiDispersionCoefX";
    _aiDispersionCoefY = cls >> "aiDispersionCoefY";

    _canBeLocked = cls >> "canLock";
    _enableAttack = cls >> "enableAttack";
    _optics = cls >> "optics";
    _primary = cls >> "primary";
    _showEmpty = cls >> "showEmpty";
    _autoReload = cls >> "autoReload";
    _backgroundReload = cls >> "backgroundReload";

    _opticsZoomMin = cls >> "opticsZoomMin";
    _opticsZoomMax = cls >> "opticsZoomMax";
    _distanceZoomMin = cls >> "distanceZoomMin";
    _distanceZoomMax = cls >> "distanceZoomMax";

    _opticsFlare = cls >> "opticsFlare";
    _forceOptics = cls >> "forceOptics";

    _muzzlePos = VZero;
    _muzzleDir = VForward;

    _cartridgeOutPosIndex = -1;
    _cartridgeOutEndIndex = -1;

    int n = (cls >> "magazines").GetSize();
    _magazines.Resize(n);
    for (int i = 0; i < n; i++)
    {
        RStringB magazine = (cls >> "magazines")[i];
        if (stricmp(magazine, "this") == 0)
        {
            _magazines[i] = MagazineTypes.New(GetName());
        }
        else
        {
            _magazines[i] = MagazineTypes.New(magazine);
        }
    }
    if (_magazines.Size() > 0)
    {
        _typicalMagazine = _magazines[0];
    }

    _nModes = (cls >> "modes").GetSize();
}

void MuzzleType::InitShape(const WeaponType* weapon)
{
    const ParamEntry& cls = *_parClass;

    RStringB oModelName = cls >> "modelOptics";
    _opticsModel = oModelName.GetLength() > 0 ? Shapes.New(GetShapeName(oModelName), true, false) : nullptr;
    if (_opticsModel && _opticsModel->NLevels() > 0)
    {
        _opticsModel->LevelOpaque(0)->MakeCockpit();
        _opticsModel->OrSpecial(BestMipmap | NoDropdown);
        _animFire.Init(_opticsModel, "zasleh", nullptr);
    }

    RStringB cursorName = cls >> "cursor";
    if (cursorName.GetLength() > 0)
    {
        _cursorTexture = GlobLoadTexture(GetPictureName(cursorName));
    }

    cursorName = cls >> "cursorAim";
    if (cursorName.GetLength() > 0)
    {
        _cursorAimTexture = GlobLoadTexture(GetPictureName(cursorName));
    }

    _muzzlePos = VZero;
    _muzzleDir = VForward;

    _cartridgeOutPosIndex = -1;
    _cartridgeOutEndIndex = -1;

    if (weapon->_model)
    {
        Shape* mem = weapon->_model->MemoryLevel();
        if (mem)
        {
            {
                RString pos = cls >> "muzzlePos";
                RString end = cls >> "muzzleEnd";
                _muzzlePos = mem->NamedPosition(pos);
                Vector3Val vEnd = mem->NamedPosition(end);
                _muzzleDir = (_muzzlePos - vEnd);
                _muzzleDir.Normalize();
            }
            {
                RString pos = cls >> "cartridgePos";
                RString end = cls >> "cartridgeVel";

                _cartridgeOutPosIndex = mem->PointIndex(pos);
                _cartridgeOutEndIndex = mem->PointIndex(end);

                _cartridgeOutPos = mem->NamedPosition(pos);
                Vector3 vEnd = mem->NamedPosition(end);
                _cartridgeOutVel = (vEnd - _cartridgeOutPos) * 50;
            }
        }
    }
}

void MuzzleType::DeinitShape()
{
    _opticsModel.Free();
    _cursorTexture.Free();
    _cursorAimTexture.Free();
    _animFire.Deinit();
}

bool MuzzleType::CanUse(const MagazineType* type) const
{
    for (int i = 0; i < _magazines.Size(); i++)
    {
        if (_magazines[i] == type)
        {
            return true;
        }
    }
    return false;
}

WeaponType::WeaponType()
{
    _shapeRef = 0;
    _shotFromTurret = false;
}

void WeaponType::InitShape() const
{
    RStringB wModelName = (*_parClass) >> "model";

    if (wModelName.GetLength() > 0)
    {
        bool shadow = true;
        _model = Shapes.New(GetShapeName(wModelName), false, shadow);
        if (_model)
        {
            _model->SetAutoCenter(false);
            _model->CalculateMinMax();
            _animFire.Init(_model, "zasleh", nullptr);

            if (_parClass->FindEntry("revolving") && _parClass->FindEntry("revolvingAxis"))
            {
                RStringB selection = (*_parClass) >> "revolving";
                RStringB axis = (*_parClass) >> "revolvingAxis";
                _revolving.Init(_model, selection, nullptr, axis, nullptr, true);

                _model->AllowAnimation();
            }
        }
    }
    else
    {
        _model = nullptr;
    }
}

void WeaponType::DeinitShape() const
{
    _model.Free();
    _animFire.Deinit();
    _revolving.Deinit();
}

void WeaponType::ShapeAddRef() const
{
    if (_shapeRef++ == 0)
    {
        InitShape();
        for (int j = 0; j < _muzzles.Size(); j++)
        {
            MuzzleType* muzzle = _muzzles[j];
            muzzle->InitShape(this);
        }
    }
}

void WeaponType::ShapeRelease() const
{
    if (--_shapeRef == 0)
    {
        DeinitShape();
        for (int j = 0; j < _muzzles.Size(); j++)
        {
            MuzzleType* muzzle = _muzzles[j];
            muzzle->DeinitShape();
        }
    }
}

RStringB WeaponType::GetPictureName() const
{
    return _picName.GetLength() > 0 ? _picName : GetName();
}

bool WeaponType::IsBinocular() const
{
    return (_weaponType & MaskSlotBinocular) != 0 && stricmp(GetName(), "binocular") == 0;
}

const WeaponType* Poseidon::FindBinocularWeapon(const RefArray<WeaponType>& weapons)
{
    for (int i = 0; i < weapons.Size(); i++)
    {
        const WeaponType* weapon = weapons[i];
        if (weapon && weapon->IsBinocular())
        {
            return weapon;
        }
    }
    return nullptr;
}

void WeaponType::Init(const char* name)
{
    const ParamEntry& cls = Pars >> "CfgWeapons" >> name;

    _parClass = &cls;

    _scope = cls >> "scopeWeapon";
    _displayName.Bind(cls >> "displayName");
    _weaponType = cls >> "weaponType";
    _picName = cls >> "picture";

    const ParamEntry* entry = cls.FindEntry("shotFromTurret");
    if (entry)
    {
        _shotFromTurret = *entry;
    }
    else
    {
        _shotFromTurret = false;
    }

    RString picture;
    entry = cls.FindEntry("uiPicture");
    if (entry)
    {
        picture = *entry;
    }
    if (picture.GetLength() > 0)
    {
        _picture = GlobLoadTexture(::GetPictureName(picture));
    }

    entry = cls.FindEntry("canDrop");
    if (entry)
    {
        _canDrop = *entry;
    }
    else
    {
        _canDrop = true;
    }

    entry = cls.FindEntry("dexterity");
    if (entry)
    {
        _dexterity = *entry;
    }
    else
    {
        _dexterity = 1;
    }

    entry = cls.FindEntry("valueWeapon");
    if (entry)
    {
        _value = *entry;
    }
    else
    {
        _value = 1;
    }

    int n = (cls >> "muzzles").GetSize();
    _muzzles.Resize(n);
    for (int i = 0; i < n; i++)
    {
        _muzzles[i] = new MuzzleType();
        RStringB muzzle = (cls >> "muzzles")[i];
        if (stricmp(muzzle, "this") == 0)
        {
            _muzzles[i]->Init(cls, this);
        }
        else
        {
            _muzzles[i]->Init(cls >> muzzle, this);
        }
    }
}

LSError WeaponType::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving())
    {
        RString name = GetName();
        PARAM_CHECK(ar.Serialize("name", name, 1));
    }
    return LSOK;
}

WeaponType* WeaponType::CreateObject(ParamArchive& ar)
{
    RString name;
    if (ar.Serialize("name", name, 1) != LSOK)
    {
        return nullptr;
    }
    if (name.GetLength() == 0)
    {
        return nullptr;
    }
    return WeaponTypes.New(name);
}

DEFINE_FAST_ALLOCATOR(Magazine)

Magazine::Magazine(const MagazineType* type) : _type(const_cast<MagazineType*>(type))
{
    _ammo = 0;
    _reload = 0;
    _reloadMagazine = 0;
    _burstLeft = 0;

    _creator = GetNetworkManager().GetPlayer();
    _id = GWorld->GetMagazineID();
    if (_type)
    {
        _type->AmmoAddRef();
    }
}

Magazine::~Magazine()
{
    if (_type)
    {
        _type->AmmoRelease();
    }
}

Magazine* Magazine::CreateObject(ParamArchive& ar)
{
    MagazineType* type = nullptr;
    RString name;
    if (ar.Serialize("type", name, 1) != LSOK)
    {
        return nullptr;
    }
    if (name.GetLength() > 0)
    {
        type = MagazineTypes.New(name);
    }
    return new Magazine(type);
};

LSError Magazine::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving())
    {
        RString name = _type ? _type->GetName() : "";
        PARAM_CHECK(ar.Serialize("type", name, 1));
    }
    {
        int ammo = _ammo;
        PARAM_CHECK(ar.Serialize("ammo", ammo, 1, 0));
        _ammo = ammo;
    }
    PARAM_CHECK(ar.Serialize("burstLeft", _burstLeft, 1, 0));
    PARAM_CHECK(ar.Serialize("reload", _reload, 1, 0));
    PARAM_CHECK(ar.Serialize("reloadMagazine", _reloadMagazine, 1, 0));
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        // for SaveStatus / LoadStatus leave id unchanged (avoid collision with existing id)
        PARAM_CHECK(ar.Serialize("creator", _creator, 1, 0));
        PARAM_CHECK(ar.Serialize("id", _id, 1, 0));
    }
    return LSOK;
}

IndicesMagazine::IndicesMagazine()
{
    type = -1;
    ammo = -1;
    burstLeft = -1;
    reload = -1;
    reloadMagazine = -1;
    creator = -1;
    id = -1;
}

void IndicesMagazine::Scan(NetworkMessageFormatBase* format){SCAN(type) SCAN(ammo) SCAN(burstLeft) SCAN(reload)
                                                                 SCAN(reloadMagazine) SCAN(creator) SCAN(id)}

NetworkMessageIndices* GetIndicesMagazine()
{
    return new IndicesMagazine();
}

NetworkMessageFormat& Magazine::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("type", NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Magazine type"));
    format.Add("ammo", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Ammo count"));
    format.Add("burstLeft", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
               DOC_MSG("How many shots are there in the burst (auto fired)"));
    format.Add("reload", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Time rest to reload shot"));
    format.Add("reloadMagazine", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Time rest to reload magazine"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Network ID of magazine"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of magazine"));
    return format;
}

Magazine* Magazine::CreateObject(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMagazine*>(ctx.GetIndices())) const IndicesMagazine* indices =
        static_cast<const IndicesMagazine*>(ctx.GetIndices());

    MagazineType* type = nullptr;
    RString name;
    if (ctx.IdxTransfer(indices->type, name) != TMOK)
    {
        return nullptr;
    }
    if (name.GetLength() > 0)
    {
        type = MagazineTypes.New(name);
    }
    return new Magazine(type);
}

TMError Magazine::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMagazine*>(ctx.GetIndices())) const IndicesMagazine* indices =
        static_cast<const IndicesMagazine*>(ctx.GetIndices());

    if (ctx.IsSending())
    {
        RString name = _type ? _type->GetName() : "";
        TMCHECK(ctx.IdxTransfer(indices->type, name))
    }
    int ammo = _ammo;
    TMCHECK(ctx.IdxTransfer(indices->ammo, ammo))
    _ammo = ammo;
    ITRANSF(burstLeft)
    ITRANSF(reload)
    ITRANSF(reloadMagazine)
    ITRANSF(creator)
    ITRANSF(id)
    return TMOK;
}

MagazineSlot::MagazineSlot()
{
    _mode = 0;
}

int EntityAIType::AddWeapon(RStringB name)
{
    Ref<WeaponType> weapon = WeaponTypes.New(name);
    return _weapons.Add(weapon);
}

int EntityAIType::AddMagazine(RStringB name)
{
    Ref<MagazineType> magazine = MagazineTypes.New(name);
    return _magazines.Add(magazine);
}

bool EntityAI::FindWeapon(int weapon, int& slot, int& mode) const
{
    slot = 0;
    mode = weapon;
    for (int i = 0; i < _weapons.Size(); i++)
    {
        const WeaponType* w = _weapons[i];
        for (int j = 0; j < w->_muzzles.Size(); j++)
        {
            int n = w->_muzzles[j]->_nModes;
            if (mode < n)
            {
                return true;
            }

            slot++;
            mode -= n;
        }
    }
    return false;
}

namespace Poseidon
{
MagazineTypeBank MagazineTypes;
WeaponTypeBank WeaponTypes;
} // namespace Poseidon
