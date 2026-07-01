#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/World/Entities/Vehicles/Ground/Car.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{
static const Color CarLightColor(0.9, 0.8, 0.8);
static const Color CarLightAmbient(0.1, 0.1, 0.1);

#if _ENABLE_CHEATS
#define ARROWS 1
#endif

CarType::CarType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;
}

void CarType::Load(const ParamEntry& par)
{
    base::Load(par);

    _hasTurret = par.FindEntry("Turret") != nullptr;
    if (_hasTurret)
    {
        _turret.Load(par >> "Turret");
    }

    _wheelCircumference = par >> "wheelCircumference";
    _turnCoef = par >> "turnCoef";
    _terrainCoef = par >> "terrainCoef";

    _damperSize = par >> "damperSize";
    _damperForce = par >> "damperForce";
}

void CarType::ScanWheels(Buffer<CarWheel>& buffer, int contactLevel)
{
    if (contactLevel < 0)
    {
        return;
    }
    Shape* contact = _shape->Level(contactLevel);
    buffer.Init(contact->NPos()); // conversion from landcontact
    // prepare wheel index information
    for (int i = 0; i < contact->NPos(); i++)
    {
        CarWheel wheel = NoWheel;
        for (int w = 0; w < MaxWheels; w++)
        {
            int selIndex = _wheels[w]->GetSelection(contactLevel);
            if (selIndex < 0)
            {
                continue;
            }
            if (contact->NamedSel(selIndex).IsSelected(i))
            {
                wheel = (CarWheel)w;
            }
        }
        // PoseidonAssert( wheel!=NoWheel );
        buffer[i] = wheel;
    }
}

void CarType::InitShape()
{
    const ParamEntry& par = *_par;
    _scopeLevel = 2;
    base::InitShape();

    // wheel animations
    _frontLeftWheel.Init(_shape, "levy predni", "levy predni tlumic", nullptr);
    _frontRightWheel.Init(_shape, "pravy predni", "pravy predni tlumic", nullptr);
    _front2LeftWheel.Init(_shape, "levy dalsi", "levy dalsi tlumic", nullptr);
    _front2RightWheel.Init(_shape, "pravy dalsi", "pravy dalsi tlumic", nullptr);
    _midLeftWheel.Init(_shape, "levy prostredni", "levy prostredni tlumic", nullptr);
    _midRightWheel.Init(_shape, "pravy prostredni", "pravy prostredni tlumic", nullptr);
    _backLeftWheel.Init(_shape, "levy zadni", "levy zadni tlumic", nullptr);
    _backRightWheel.Init(_shape, "pravy zadni", "pravy zadni tlumic", nullptr);

    _drivingWheel.Init(_shape, "volant", nullptr, "osaVolantKon");

    // check front wheel center in memory level
    int mem = _shape->FindLandContactLevel();
    _steeringPoint = VZero;
    if (mem >= 0)
    {
        _steeringPoint = (_frontLeftWheel.Center(mem) + _frontRightWheel.Center(mem)) * 0.5;
    }

    _plateInfos.Init(_shape, "spz", GetFontID("tahomaB48"), PackedColor(Color(0, 0, 0, 0.75)));

    _toWheelAxis.SetIdentity();

    int level;

    // no dropdown for a cockpit
    level = _shape->FindLevel(VIEW_PILOT);
    if (level >= 0)
    {
        Shape* oShape = _shape->LevelOpaque(level);
        //_cockpit->MakeCockpit();
        // driving wheel animation
        Vector3 wheelAxisBeg = oShape->NamedPosition("osaVolantZac");
        Vector3 wheelAxisEnd = oShape->NamedPosition("osaVolantKon");
        // remmember axis
        _drivingWheel.SetCenter(wheelAxisEnd);
        _toWheelAxis.SetDirectionAndUp(wheelAxisEnd - wheelAxisBeg, VUp);
    }

    for (int i = 0; i < MaxWheels; i++)
    {
        _wheels[i] = nullptr;
    }

    _wheels[FLWheel] = &_frontLeftWheel;
    _wheels[FRWheel] = &_frontRightWheel;
    _wheels[FL2Wheel] = &_front2LeftWheel;
    _wheels[FR2Wheel] = &_front2RightWheel;

    _wheels[MLWheel] = &_midLeftWheel;
    _wheels[MRWheel] = &_midRightWheel;
    _wheels[BLWheel] = &_backLeftWheel;
    _wheels[BRWheel] = &_backRightWheel;

    // prepare contact information
    ScanWheels(_whichWheelContact, _shape->FindLandContactLevel());
    ScanWheels(_whichWheelGeometry, _shape->FindGeometryLevel());

    DEF_HIT(_shape, _glassRHit, "sklo predni P", nullptr, GetArmor() * (float)(par >> "armorGlass"));
    DEF_HIT(_shape, _glassLHit, "sklo predni L", nullptr, GetArmor() * (float)(par >> "armorGlass"));
    // attach hitpoint to convex component corresponding to selection "sklo"
    FindArray<int> hitsL, hitsR;
    _shape->FindHitComponents(hitsR, "sklo predni P");
    _shape->FindHitComponents(hitsL, "sklo predni L");
    _glassRHit.SetIndexCC(hitsR);
    _glassLHit.SetIndexCC(hitsL);
    //

    DEF_HIT(_shape, _bodyHit, "karoserie", nullptr, GetArmor() * (float)(par >> "armorBody"));
    DEF_HIT(_shape, _fuelHit, "palivo", nullptr, GetArmor() * (float)(par >> "armorFuel"));

    DEF_HIT(_shape, _wheelLFHit, "Levy predni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));
    DEF_HIT(_shape, _wheelRFHit, "Pravy predni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

    DEF_HIT(_shape, _wheelLF2Hit, "Levy zadni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));
    DEF_HIT(_shape, _wheelRF2Hit, "Pravy dalsi tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

    DEF_HIT(_shape, _wheelLMHit, "Levy prostredni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));
    DEF_HIT(_shape, _wheelRMHit, "Pravy prostredni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

    DEF_HIT(_shape, _wheelLBHit, "Levy zadni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));
    DEF_HIT(_shape, _wheelRBHit, "Pravy zadni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

    {
        WoundInfo dammageInfo;
        dammageInfo.LoadAndRegister(_shape, par >> "dammageHalf");
        _glassDammageHalf.Init(_shape, dammageInfo, nullptr, nullptr);
    }
    {
        WoundInfo dammageInfo;
        dammageInfo.LoadAndRegister(_shape, par >> "dammageFull");
        _glassDammageFull.Init(_shape, dammageInfo, nullptr, nullptr);
    }

    // turret animations
    if (_hasTurret)
    {
        _turret.InitShape(par >> "Turret", _shape);
    }

    _animFire.Init(_shape, "zasleh", nullptr);

    RString scudLaunch = par >> "scudLaunch";
    RString scudStart = par >> "scudStart";
    if (scudLaunch.GetLength() > 0 || scudStart.GetLength() > 0)
    {
        _skeleton = Skeletons.New("Scud");
        _weights = new WeightInfo;

        AnimationRTName name;
        name.skeleton = _skeleton;

        if (scudLaunch.GetLength() > 0)
        {
            name.name = GetAnimationName(scudLaunch);
            _scudLaunch = new AnimationRT(name);
            _scudLaunch->Prepare(_shape, _skeleton, *_weights, false);

            GetValue(_scudSoundElevate, par >> "scudSoundElevate");
        }

        if (scudStart.GetLength() > 0)
        {
            name.name = GetAnimationName(scudStart);
            _scudStart = new AnimationRT(name);
            _scudStart->Prepare(_shape, _skeleton, *_weights, false);

            GetValue(_scudSound, par >> "scudSound");
        }

        RStringB modelName = par >> "scudModel";
        if (modelName.GetLength() > 0)
        {
            _scudModel = Shapes.New(::GetShapeName(modelName), false, false);
            if (_scudModel)
            {
                _scudModel->SetAutoCenter(false);
                _scudModel->CalculateMinMax();
            }
        }
        else
        {
            _scudModel = nullptr;
        }

        modelName = par >> "scudModelFire";
        if (modelName.GetLength() > 0)
        {
            _scudModelFire = Shapes.New(::GetShapeName(modelName), false, false);
            if (_scudModelFire)
            {
                _scudModelFire->SetAutoCenter(false);
                _scudModelFire->CalculateMinMax();
            }
        }
        else
        {
            _scudModelFire = nullptr;
        }
    }

    for (int level = 0; level < _shape->NLevels(); level++)
    {
        ScudProxy& info = _proxies[level];
        Shape* shape = _shape->LevelOpaque(level);

        // convert shape proxies to my proxies
        for (int i = 0; i < shape->NProxies(); i++)
        {
            const ProxyObject& proxy = shape->Proxy(i);
            Object* obj = proxy.obj;
            const VehicleNonAIType* type = obj->GetVehicleType();
            if (!type)
            {
                continue;
            }
            RString simulation = type->_simName;
            if (stricmp(simulation, "scud") == 0)
            {
                info.obj = obj;
                info.selection = proxy.selection;
                break;
            }
        }
    }
}

int GetTemplateSeed();

Car::Car(VehicleType* name, Person* driver)
    : base(name, driver), _thrustWanted(0), _thrust(0), _turnWanted(0), _turn(0), _turnIncreaseSpeed(1),
      _turnDecreaseSpeed(1), _reverseTimeLeft(0), _forwardTimeLeft(0), _wheelPhase(0), _track(_shape), _scudState(0)
{
    _isStopped = true;

    _rpm = 0, _rpmWanted = 0;
    float gearRatio = 80 / Type()->GetMaxSpeed();
    AutoArray<float> gears;
    gears.Add(0);
    gears.Add(1.0 / 8 * gearRatio);
    gears.Add(1.0 / 13 * gearRatio);
    gears.Add(1.0 / 18 * gearRatio);
    gears.Add(1.0 / 24 * gearRatio);
    _gearBox.SetGears(gears);

    if (HasTurret())
    {
        _mGunClouds.Load((*Type()->_par) >> "MGunClouds");
    }

    _mGunFireFrames = 0;
    _mGunFireTime = UITIME_MIN;
    _mGunFirePhase = 0;

    int i;
    for (i = 0; i < MaxWheels; i++)
    {
        _dampers[i] = 0;
    }

    _leftDust.SetSize(0.35);
    _rightDust.SetSize(0.35);
    _leftDust.SetAlpha(0.25);
    _rightDust.SetAlpha(0.25);

    _head.SetPars("Land");
    _head.Init(Type()->_pilotPos - Vector3(0, 0.2, 0), Type()->_pilotPos, this);

    if (Type()->_scudStart)
    {
        _scudSmoke.Load(Pars >> "CfgCloudlets" >> "CloudletsScud");
        _scudSmoke.Start(FLT_MAX);
    }
    _scudPos = VZero;
    _scudSpeed = VZero;
}

bool Car::IsAnimated(int level) const
{
    return true;
}
bool Car::IsAnimatedShadow(int level) const
{
    // a stopped (unsimulated) vehicle cannot change pose
    return !ShadowPoseFrozen();
}

void Car::AnimateMatrix(Matrix4& mat, int level, int selection) const
{
    if (HasTurret())
    {
        _turret.AnimateMatrix(Type()->_turret, mat, this, level, selection);
    }

    // scud animation — _weights is non-null only when scud animations are configured
    if (Type()->_weights)
    {
        const AnimationRTWeights& wgt = (*Type()->_weights)[level];
        Shape* sShape = _shape->Level(level);
        const NamedSelection& sel = sShape->NamedSel(selection);
        int point = sel[0];
        PoseidonAssert(sel.Size() > 0);
        if (_scudState >= 1 && _scudState < 2 && Type()->_scudLaunch)
        {
            float time = _scudState - 1;
            Type()->_scudLaunch->Matrix(mat, time, wgt[point]);
        }
        else if (_scudState >= 2 && _scudState < 4 && Type()->_scudStart)
        {
            float time = _scudState - 3;
            Type()->_scudStart->Matrix(mat, time, wgt[point]);
        }
    }
}

Vector3 Car::AnimatePoint(int level, int index) const
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return VZero;
    }
    shape->SaveOriginalPos();

    Vector3 pos = shape->OrigPos(index);

    if (HasTurret())
    {
        _turret.AnimatePoint(Type()->_turret, pos, this, level, index);
    }

    return pos;
}

inline float Car::GetGlassBroken() const
{
    const CarType* type = Type();
    float glassDammage = GetHitCont(type->_bodyHit);
    saturateMax(glassDammage, GetTotalDammage());
    saturateMax(glassDammage, GetHitCont(type->_glassLHit));
    saturateMax(glassDammage, GetHitCont(type->_glassRHit));
    return glassDammage;
}

void Car::DammageAnimation(int level)
{
    const CarType* type = Type();
    float glassDammage = GetGlassBroken();
    if (glassDammage >= 0.6)
    {
        type->_glassDammageFull.Apply(_shape, level);
    }
    else if (glassDammage >= 0.3)
    {
        type->_glassDammageHalf.Apply(_shape, level);
    }
}

void Car::DammageDeanimation(int level)
{
    const CarType* type = Type();
    float glassDammage = GetGlassBroken();
    if (glassDammage >= 0.6)
    {
        type->_glassDammageFull.Restore(_shape, level);
    }
    else if (glassDammage >= 0.3)
    {
        type->_glassDammageHalf.Restore(_shape, level);
    }
}

void Car::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }

    if (HasTurret())
    {
        _turret.Animate(Type()->_turret, this, level);
    }

    if (_mGunFireFrames > 0 || Glob.uiTime < _mGunFireTime + 0.05)
    {
        Type()->_animFire.Unhide(_shape, level);
        Type()->_animFire.SetPhase(_shape, level, _mGunFirePhase);
        _mGunFireFrames--;
    }
    else
    {
        Type()->_animFire.Hide(_shape, level);
    }

    Matrix4 transform(MIdentity);
    Matrix4 softTrans(MIdentity);
    Matrix4 turnTrans(MRotationY, _turn * -0.7f);
    if (_shape->Resolution(level) < 900)
    {
        // rotate wheels
        transform = Matrix4(MRotationX, _wheelPhase * (2 * H_PI));
    }
    // if wheel is dammaged and we animate geometry
    // we should make geometry a little bit higher
    // to simulate empty tire

    float dampers[MaxWheels];

    for (int i = 0; i < MaxWheels; i++)
    {
        dampers[i] = _dampers[i];
    }
    // report
    if (level == _shape->FindLandContactLevel() || level == _shape->FindGeometryLevel())
    {
        float dammageOffset = Type()->_wheelCircumference * 0.05;

        if (GetHit(Type()->_wheelLBHit))
        {
            dampers[BLWheel] += dammageOffset;
        }
        if (GetHit(Type()->_wheelRBHit))
        {
            dampers[BRWheel] += dammageOffset;
        }

        if (GetHit(Type()->_wheelLFHit))
        {
            dampers[FLWheel] += dammageOffset;
        }
        if (GetHit(Type()->_wheelRFHit))
        {
            dampers[FRWheel] += dammageOffset;
        }
    }

    softTrans.SetPosition(Vector3(0, dampers[FLWheel], 0));
    Type()->_frontLeftWheel.Apply(_shape, softTrans * turnTrans * transform, level);
    softTrans.SetPosition(Vector3(0, dampers[FRWheel], 0));
    Type()->_frontRightWheel.Apply(_shape, softTrans * turnTrans * transform, level);
    softTrans.SetPosition(Vector3(0, dampers[FL2Wheel], 0));
    Type()->_front2LeftWheel.Apply(_shape, softTrans * turnTrans * transform, level);
    softTrans.SetPosition(Vector3(0, dampers[FR2Wheel], 0));
    Type()->_front2RightWheel.Apply(_shape, softTrans * turnTrans * transform, level);
    for (int w = BackWheels; w < MaxWheels; w++)
    {
        softTrans.SetPosition(Vector3(0, dampers[w], 0));
        Type()->_wheels[w]->Apply(_shape, softTrans * transform, level);
    }
    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Matrix4 turnTrans(MRotationZ, _turn * 8);
        Type()->_drivingWheel.Apply(_shape, Type()->_toWheelAxis * turnTrans * Type()->_toWheelAxis.InverseRotation(),
                                    level);
    }

    DammageAnimation(level);

    if (_scudState >= 1 && _scudState < 2 && Type()->_scudLaunch)
    {
        float time = _scudState - 1;
        saturate(time, 0, 1);
        Type()->_scudLaunch->Apply(*Type()->_weights, _shape, level, time);
    }
    else if (_scudState >= 2 && _scudState < 4 && Type()->_scudStart)
    {
        float time = _scudState - 3;
        saturate(time, 0, 1);
        Type()->_scudStart->Apply(*Type()->_weights, _shape, level, time);
    }

    base::Animate(level);
    // assume min-max box is not changed
    shape->RestoreMinMax();
}

void Car::Deanimate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }

    base::Deanimate(level);

    if (Type()->_scudLaunch)
    {
        Type()->_scudLaunch->Apply(*Type()->_weights, _shape, level, 0);
    }

    if (HasTurret())
    {
        _turret.Deanimate(Type()->_turret, _shape, level);
    }

    DammageDeanimation(level);

    for (int w = 0; w < MaxWheels; w++)
    {
        Type()->_wheels[w]->Restore(_shape, level);
    }

    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Type()->_drivingWheel.Restore(_shape, level);
    }
}

Vector3 Car::Friction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = speed[0] * fabs(speed[0]) * 10 + speed[0] * 20 + fSign(speed[0]) * 10;
    friction[1] = speed[1] * fabs(speed[1]) * 7 + speed[1] * 20 + fSign(speed[1]) * 10;
    friction[2] = speed[2] * fabs(speed[2]) * 5 + speed[2] * 20 + fSign(speed[2]) * 10;
    return friction * GetMass() * (1.0 / 1700);
}

void Car::MoveWeapons(float deltaT)
{
    if (!HasTurret())
    {
        return;
    }

    AIUnit* unit = GunnerUnit();
    if (!unit)
    {
        _turret.Stop(Type()->_turret);
    }
    else
    {
        _turret._gunStabilized = true;
        _turret.MoveWeapons(Type()->_turret, unit, deltaT);
    }
}

bool Car::IsBlocked() const
{
    return _scudState >= 1 && _scudState < 4;
}

#define LimitFriction(f, s, t, im) (f)

void Car::Simulate(float deltaT, SimulationImportance prec)
{
    _isUpsideDown = DirectionUp().Y() < 0.3f;
    _isDead = IsDammageDestroyed();

    MoveWeapons(deltaT);

    if (!SimulateUnits(deltaT))
    {
        _engineOff = true;
        _pilotBrake = true;
    }

    if (IsBlocked())
    {
        _pilotBrake = true;
        _thrustWanted = 0;
        _turnWanted = 0;
    }

    const ScudProxy& proxy = Type()->_proxies[0];
    if (proxy.IsPresent())
    {
        Object* obj = proxy.obj;
        Matrix4 proxyTransform = obj->Transform();
        Vector3 offset(VFastTransform, proxyTransform, obj->GetShape()->MemoryPoint("tryska"));
        proxyTransform.SetPosition(offset);
        AnimateMatrix(proxyTransform, 0, proxy.selection);

        Matrix4 pTransform = Transform() * proxyTransform;

        Vector3Val pos = pTransform.Position();
        Vector3 speed = (pos - _scudPos) / deltaT;
        _scudPos = pos;
        _scudSpeed = speed;
    }
    if (_scudState >= 1 && _scudState < 2)
    {
        _scudState += 0.1f * deltaT; // launching
    }
    else if (_scudState >= 3 && _scudState < 4)
    {
        _scudState += 0.1f * deltaT; // starting
        _scudSmoke.Simulate(_scudPos, _scudSpeed, 1, deltaT);
    }

    base::Simulate(deltaT, prec);

    Vector3Val speed = ModelSpeed();
    float speedSize = fabs(speed.Z());

    ConsumeFuel(deltaT * 0.2 * GetHit(Type()->_fuelHit));
    if (_fuel <= 0)
    {
        _engineOff = true;
    }

    if (
        // tank is going to explode
        GetHit(Type()->_fuelHit) >= 0.9f || GetHit(Type()->_engineHit) >= 0.9f)
    {
        if (IsLocal() && _explosionTime > Glob.time + 60)
        {
            // set some explosion
            _explosionTime = Glob.time + GRandGen.Gauss(2, 5, 20);
        }
    }

    if (!_engineOff)
    {
        ConsumeFuel(deltaT * (0.01 + _rpm * 0.02));

        _rpmWanted = speedSize * _gearBox.Ratio();
        saturateMax(_rpmWanted, fabs(_thrust) * 0.3 + 0.2);
        saturate(_rpmWanted, 0, 2);
    }
    else
    {
        _rpmWanted = 0;
    }

    float delta;
    delta = _rpmWanted - _rpm;
    Limit(delta, -0.5f * deltaT, +0.3f * deltaT);
    _rpm += delta;

    Vector3 force(VZero), friction(VZero);
    Vector3 torque(VZero), torqueFriction(VZero);

    Vector3 pForce(VZero);  // partial force
    Vector3 pCenter(VZero); // partial force application point

    float wheelLen = Type()->_wheelCircumference;
    _wheelPhase += speed.Z() * (1 / wheelLen) * deltaT;
    _wheelPhase = fastFmod(_wheelPhase, 1);

    if (_isDead || _isUpsideDown)
    {
        _engineOff = true, _pilotBrake = true;
    }
    if (_fuel <= 0)
    {
        _engineOff = true;
    }
    if (_engineOff)
    {
        _thrustWanted = 0;
    }

    if (_thrustWanted * _thrust < 0)
    {
        _pilotBrake = true;
    }
    if (ModelSpeed()[2] * _thrustWanted < 0 && fabs(ModelSpeed()[2]) > 0.5f)
    {
        _pilotBrake = true;
    }

    if (_pilotBrake && fabs(ModelSpeed()[2]) > 0.5f)
    {
        _thrustWanted = 0;
    }

    if (fabs(_thrustWanted) > 0.1 || _speed.SquareSize() > 0.5f)
    {
        IsMoved();
    }

    {
        float impulse2 = _impulseForce.SquareSize();
        if (impulse2 > Square(GetMass() * 0.01f))
        {
            IsMoved();
        }
        if (impulse2 > Square(GetMass() * 3))
        {
            // too strong impulse - dammage
            float contact = sqrt(impulse2) / (GetMass() * 10);
            // contact>0
            saturateMin(contact, 5);
            if (contact > 0.1f)
            {
                float radius = GetRadius();
                LocalDammage(nullptr, this, VZero, contact * 0.1, radius * 0.3f);
            }
        }
    }

    if (GetStopped())
    {
        // reset impulse - avoid cummulation
        _impulseForce = VZero;
        _impulseTorque = VZero;
    }

    if (EnableVisualEffects(prec))
    {
        if (_mGunClouds.Active() || _mGunFire.Active())
        {
            Matrix4Val gunTransform = GunTurretTransform();
            Matrix4Val toWorld = Transform() * gunTransform;
            Vector3Val dir = toWorld.Direction();
            Vector3 gunPos(VFastTransform, toWorld, Type()->_turret._pos);
            _mGunClouds.Simulate(gunPos, Speed() * 0.7f + dir * 5.0f, 0.35f, deltaT);
            _mGunFire.Simulate(gunPos, deltaT);
            CancelStop();
        }
    }

    if (!_isStopped && !CheckPredictionFrozen())
    {
        delta = _thrustWanted - _thrust;
        if (_thrust * _thrustWanted <= 0)
        {
            Limit(delta, -2.0 * deltaT, +2.0 * deltaT);
        }
        else
        {
            Limit(delta, -deltaT, +deltaT);
        }
        _thrust += delta;

        // do not allow fast reverse
        float minThrust = Interpolativ(ModelSpeed()[2], -5, 0, 0, -1);
        Limit(_thrust, minThrust, 1.0);

        float limitTurn = 1 - (fabs(speed.Z()) - 3) * (1.0 / Type()->GetMaxSpeedMs());
        limitTurn = limitTurn * limitTurn * 4;
        saturate(limitTurn, 0.05, 1);
        saturate(_turnWanted, -limitTurn, +limitTurn);

        delta = _turnWanted - _turn;
        if (delta * _turn > 0)
        {
            float maxDelta = _turnIncreaseSpeed;
            Limit(delta, -maxDelta * deltaT, +maxDelta * deltaT);
        }
        else
        {
            float maxDelta = _turnDecreaseSpeed;
            Limit(delta, -maxDelta * deltaT, +maxDelta * deltaT);
        }
        _turn += delta;

// simulate front wheel (turning)

// calculate radial velocity of steering point
#if ARROWS
        AddForce(PositionModelToWorld(Type()->_steeringPoint), VUp * 6, Color(1, 0, 0));
#endif

        float turnForward = speed.Z() * 0.7;
        saturate(turnForward, -25, +25); // avoid slips in high speed
        float turnWanted = _turn * turnForward * 0.4 * Type()->_turnCoef;

        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

        const float defSpeed = 50.0; // model tuned at this speed (plain level road)

        bool gearChanged = false;
        if (_engineOff)
        {
            // upside down
            gearChanged = _gearBox.Neutral();
        }
        else
        {
            gearChanged = _gearBox.Change(speedSize);
        }

        float lWheelHit = floatMax(GetHit(Type()->_wheelLFHit), GetHit(Type()->_wheelLBHit));
        float rWheelHit = floatMax(GetHit(Type()->_wheelRFHit), GetHit(Type()->_wheelRBHit));

        if (!_engineOff || fabs(turnWanted) > 0.001)
        {
            if (_landContact || _objectContact || Type()->_canFloat && _waterContact)
            {
                // the more we turn, the more power we loose
                float invSpeedSize;
                const float coefInvSpeed = 3;
                const float minInvSpeed = 8;
                // float power=Type()->GetMaxSpeed()*(1/defSpeed);
                float power = Type()->GetMaxSpeed() * (1 / defSpeed) * 0.75;
                if (power > 1)
                {
                    power = Square(power);
                }
                // float defPower=Square(1);
                if (speedSize < minInvSpeed * power)
                {
                    invSpeedSize = coefInvSpeed / minInvSpeed;
                }
                else
                {
                    invSpeedSize = power * coefInvSpeed / speedSize;
                }
                invSpeedSize *= 3;
                // saturateMax(invSpeedSize,1/power);

                if (!_landContact && !_objectContact)
                {
                    invSpeedSize *= 0.05f;
                    turnWanted *= 9;
                }
                float lAccel = (_thrust * 4 - turnWanted) * invSpeedSize;
                float rAccel = (_thrust * 4 + turnWanted) * invSpeedSize;

                lAccel *= (1 - GetHit(Type()->_engineHit)) * (1 - lWheelHit);
                rAccel *= (1 - GetHit(Type()->_engineHit)) * (1 - rWheelHit);

                pForce = Vector3(0, 0, lAccel * GetMass());
                force += pForce;
                pCenter = Vector3(+2, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                         Color(0, 1, 0));
#endif

                pForce = Vector3(0, 0, rAccel * GetMass());
                force += pForce;
                pCenter = Vector3(-2, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                         Color(0, 1, 0));
#endif
            }
        }

        if (gearChanged)
        {
            if (_gearSound)
            {
                _gearSound->Stop();
            }
            IWave* sound = GSoundScene->OpenAndPlayOnce(Type()->_gearSound.name, Position(), Speed());
            if (sound)
            {
                GSoundScene->SimulateSpeedOfSound(sound);
                _gearSound = sound;
                GSoundScene->AddSound(sound);
            }
        }

        DirectionModelToWorld(torque, torque);
        DirectionModelToWorld(force, force);

        pForce = Vector3(0, -G_CONST * GetMass(), 0);
        force += pForce;

#if ARROWS
        AddForce(wCenter, pForce * InvMass());
#endif

        torqueFriction = _angMomentum * 0.5;
        int tfCount = 1;

        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);
        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // body air friction
        DirectionModelToWorld(friction, Friction(speed));
// friction=Vector3(0,0,0);
#if ARROWS
        AddForce(wCenter, -friction * InvMass());
#endif

        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

        float soft = 0, dust = 0;
        if (deltaT > 0)
        {
            float crash = 0;
            float sFactor = Type()->GetMaxSpeedMs() * 1.3;
            Vector3 fSpeed = speed - Vector3(0, 0, _thrust * sFactor);
            // avoid too fast accel/deccel
            float maxAcc = floatMin(5, Type()->GetMaxSpeedMs() * 0.14);
            saturate(fSpeed[0], -maxAcc, +maxAcc);
            saturate(fSpeed[1], -maxAcc, +maxAcc);
            saturate(fSpeed[2], -maxAcc * 0.6, +maxAcc);
            float brakeFriction = 0;

            saturateMax(brakeFriction, _pilotBrake);
            saturateMax(brakeFriction, DirectionUp().Y() <= 0.3);
            saturateMax(brakeFriction, lWheelHit - 0.5);
            saturateMax(brakeFriction, rWheelHit - 0.5);

            fSpeed = fSpeed * (1 - brakeFriction) + speed * brakeFriction;

            Vector3 objForce(VZero);  // total object force
            Vector3 landForce(VZero); // total land force

            _objectContact = false;
            float maxColSpeed2 = 0;
            float maxCFactor = 0;
            if (prec <= SimulateVisibleFar && IsLocal())
            {
#define MAX_IN 0.2
#define MAX_IN_FORCE 0.1
#define MAX_IN_FRICTION 0.2

                CollisionBuffer collision;
                GLOB_LAND->ObjectCollision(collision, this, moveTrans);
                for (int i = 0; i < collision.Size(); i++)
                {
                    CollisionInfo& info = collision[i];
                    Object* obj = info.object;
                    if (!obj)
                    {
                        continue;
                    }
                    if (obj->IsPassable())
                    {
                        continue;
                    }
                    _objectContact = true;
                    // info.pos is relative to object
                    float cFactor = info.object->GetMass() * InvMass();
                    if (obj->Static())
                    {
                        // fixed object - apply fixed collision routines
                        // calculate his dammage
                        // depending on vehicle speed and mass
                        float dFactor = GetMass() * obj->InvMass();
                        float dSpeed = _speed.SquareSize() + _angVelocity.SquareSize();
                        float dammage = dSpeed * obj->GetInvArmor() * dFactor * 0.2;
                        if (dammage > 0.01)
                        {
                            obj->LocalDammage(nullptr, this, VZero, dammage, obj->GetShape()->GeometrySphere());
                        }
                        if (obj->GetDestructType() == DestructTree || obj->GetDestructType() == DestructTent ||
                            obj->GetDestructType() == DestructMan)
                        {
                            saturate(cFactor, 0.001, 0.5);
                        }
                        else
                        {
                            saturate(cFactor, 0.2, 2);
                        }
                    }
                    else
                    {
                        saturate(cFactor, 0, 10);
                    }

                    Vector3 pos = info.object->PositionModelToWorld(info.pos);
                    Vector3 dirOut = info.object->DirectionModelToWorld(info.dirOut);
                    // create a force pushing "out" of the collision
                    float forceIn = floatMin(info.under, MAX_IN_FORCE);
                    Vector3 pForce = dirOut * GetMass() * 20 * cFactor * forceIn;
                    Vector3 pTorque = pForce * 0.5;
                    // apply proportional part of force in place of impact
                    pCenter = pos - wCenter;
                    if (cFactor > 0.05)
                    {
                        objForce += pForce;
                        torque += pCenter.CrossProduct(pTorque);
                    }
                    Vector3Val objSpeed = info.object->ObjectSpeed();
                    Vector3 colSpeed = _speed - objSpeed;
                    bool isFixed = true;
                    Vehicle* veh = dyn_cast<Vehicle, Object>(obj);
                    if (veh)
                    {
                        // transfer all my intertia to him?
                        Vector3 relDistance = veh->Position() - Position();
                        Vector3 relSpeed = objSpeed - Speed();
                        float speedTransfer = relSpeed * relDistance * -relDistance.InvSize() * 0.2;
                        saturate(speedTransfer, 0, 1);
                        float transferFactor = GetMass() * veh->GetInvMass();
                        saturate(transferFactor, 0, 0.3);
                        transferFactor *= speedTransfer;
                        Vector3 impulse = _speed * GetMass() * deltaT * transferFactor;
                        veh->AddImpulseNetAware(impulse, info.pos.CrossProduct(-pTorque * deltaT));
                        // vehicle is cosidered fixed when it is very heavy and slow moving
                        // or it is static object)
                        isFixed = veh->Static();
                        if (!isFixed)
                        {
                            if (veh->GetMass() > GetMass() * 2)
                            {
                                isFixed = true;
                            }
                        }
                    }

                    saturateMax(maxColSpeed2, colSpeed.SquareSize());
                    saturateMax(maxCFactor, cFactor);

                    if (cFactor < 0.05f)
                    {
                        continue;
                    }

                    // if info.under is bigger than MAX_IN, move out
                    if (isFixed)
                    {
                        if (info.under > MAX_IN)
                        {
                            Matrix4 transform = moveTrans.Transform();
                            Point3 newPos = transform.Position();
                            float moveOut = info.under - MAX_IN;
                            Vector3 move = dirOut * moveOut * 0.1f;
                            newPos += move;
                            transform.SetPosition(newPos);
                            moveTrans.SetTransform(transform);
                        }
                        Vector3 colSpeed = Speed() - obj->ObjectSpeed();
                        // limit relative speed to object we crashed into
                        const float maxRelSpeed = 0.5f;
                        if (colSpeed.SquareSize() > Square(maxRelSpeed))
                        {
                            // adapt _speed to match criterion
                            float crashSpeed = colSpeed.Size() - 2;
                            if (crashSpeed > 0)
                            {
                                crash += crashSpeed * 0.3f;
                            }
                            colSpeed.Normalize();
                            colSpeed *= maxRelSpeed;
                            // only slow down
                            float oldSize = _speed.Size();
                            _speed = colSpeed + objSpeed;
                            if (_speed.SquareSize() > Square(oldSize))
                            {
                                _speed = _speed.Normalized() * oldSize;
                            }
                        }
                    }

                    // second is "land friction" - causing little momentum
                    float frictionIn = floatMin(info.under, MAX_IN_FRICTION);
                    pForce[0] = fSign(speed[0]) * 10000;
                    pForce[1] = speed[1] * fabs(speed[1]) * 1000 + speed[1] * 8000 + fSign(speed[1]) * 10000;
                    pForce[2] = speed[2] * fabs(speed[2]) * 150 + speed[2] * 250 + fSign(speed[2]) * 2000;

                    DirectionModelToWorld(pForce, pForce);
                    pForce *= GetMass() * (4.0f / 10000) * frictionIn;
// saturateMin(pForce[1],0);
// torque-=pCenter.CrossProduct(pForce);
#if ARROWS
                    AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                    friction += pForce;
                    torqueFriction += _angMomentum * 0.15f;
                    tfCount++;
                }
            }

            const float MaxDamper = Type()->_damperSize;
            const float scaleDamper = 0.5f;
            const float initDamper = -2 * scaleDamper / MaxDamper;
            const float adaptDamper = Type()->_damperForce * scaleDamper;
            float damperForces[MaxWheels];
            for (int w = 0; w < MaxWheels; w++)
            {
                damperForces[w] = 0;
            }

            GroundCollisionBuffer gCollision;
            float softFactor = floatMin(4000 / GetMass(), 2.0f);

            if (prec >= SimulateVisibleFar)
            {
                GLOB_LAND->GroundCollisionPlane(gCollision, this, moveTrans, 0.05f, softFactor);
            }
            else
            {
                GLOB_LAND->GroundCollision(gCollision, this, moveTrans, 0.05f, softFactor);
            }

            _landContact = false;
            _objectContact = false;
            _waterContact = false;
            if (gCollision.Size() > 0)
            {
#define MAX_UNDER 0.4f
#define MAX_UNDER_FORCE 0.1f
                float maxUnderWater = 0;

                Shape* landcontact = GetShape()->LandContactLevel();
                int nContactPoint = landcontact ? landcontact->NPos() : 8;
                saturateMax(nContactPoint, gCollision.Size());
                float contactCoef = 8.0f / nContactPoint;
                float maxUnder = 0;
                float totalUnder = 0;
                for (int i = 0; i < gCollision.Size(); i++)
                {
                    UndergroundInfo& info = gCollision[i];
                    if (info.under < 0)
                    {
                        continue;
                    }
                    float under;
                    if (info.type == GroundWater)
                    {
                        _waterContact = true;

                        // simulate swimming force
                        // const float coefNPoints=12.0/12.0;
                        const float coefNPoints = 16.0 / 4.0;
                        // first is water is "pushing" everything up - causing some momentum
                        saturateMax(maxUnderWater, info.under);

                        pForce = Vector3(0, GetMass() * 0.5f * info.under * coefNPoints, 0);
                        if (!Type()->_canFloat)
                        {
                            pForce *= 0.1;
                        }
                        pCenter = info.pos - wCenter;
                        torque += pCenter.CrossProduct(pForce);
                        landForce += pForce;

                        // add stabilizing torque
                        // stabilized means DirectionUp() is (0,1,0)
                        Vector3 stabilize = VUp - moveTrans.DirectionUp();
                        torque += Vector3(0, coefNPoints * 1.5f * GetMass(), 0).CrossProduct(stabilize);

#if ARROWS
                        AddForce(wCenter + pCenter, pForce * InvMass());
#endif

                        // second is "water friction" - causing no momentum
                        pForce[0] = speed[0] * fabs(speed[0]) * 15;
                        pForce[1] = speed[1] * fabs(speed[1]) * 15 + speed[1] * 160;
                        pForce[2] = speed[2] * fabs(speed[2]) * 6;

                        pForce = DirectionModelToWorld(pForce * info.under) * GetMass() * (coefNPoints / 700);
#if ARROWS
                        AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                        friction += pForce;
                        torqueFriction += _angMomentum * 0.3;
                        tfCount++;

                        float colSpeed2 = _speed.SquareSize();
                        if (colSpeed2 > Square(8))
                        {
                            crash = 2;
                            saturateMax(maxColSpeed2, colSpeed2);
                        }
                    }
                    else
                    {
                        _landContact = true;
                        if (maxUnder < info.under)
                        {
                            maxUnder = info.under;
                        }
                        under = floatMin(info.under, MAX_UNDER_FORCE);
                        totalUnder += under;

                        CarWheel wheel = MaxWheels;

                        if (info.level == _shape->FindLandContactLevel())
                        {
                            PoseidonAssert(info.vertex >= 0);
                            PoseidonAssert(info.vertex < Type()->_whichWheelContact.Size());
                            wheel = Type()->_whichWheelContact[info.vertex];
                        }
                        else if (info.level == _shape->FindGeometryLevel())
                        {
                            PoseidonAssert(info.vertex >= 0);
                            PoseidonAssert(info.vertex < Type()->_whichWheelGeometry.Size());
                            wheel = Type()->_whichWheelGeometry[info.vertex];
                        }
                        if (wheel < MaxWheels)
                        {
                            saturateMax(damperForces[wheel], info.under * adaptDamper);
                        }

                        // some friction is caused by moving the land aside
                        // this applies only to soft surfaces
                        if (info.texture)
                        {
                            soft = info.texture->Roughness() * 2.5f;
                            dust = info.texture->Dustness() * 2.5f;
                            saturateMin(dust, 1);
                        }

                        // one is ground "pushing" everything out - causing some momentum
                        Vector3 dirOut = Vector3(0, info.dZ, 1).CrossProduct(Vector3(1, info.dX, 0)).Normalized();
                        pForce = dirOut * GetMass() * 40.0f * contactCoef * under;
                        pCenter = info.pos - wCenter;
                        torque += pCenter.CrossProduct(pForce);
                        landForce += pForce;

#if ARROWS
                        AddForce(wCenter + pCenter, pForce * under * InvMass(), Color(1, 0, 0));
#endif

                        // second is "land friction" - causing little momentum
                        // pForce[0]=fSpeed[0]*500+fSign(fSpeed[0])*30000;
                        pForce[0] = fSpeed[0] * 500 + fSign(fSpeed[0]) * 90000;
                        pForce[1] = fSpeed[1] * 8000 + fSign(fSpeed[1]) * 10000;
                        pForce[2] = fSpeed[2] * 200 + fSign(fSpeed[2]) * 30000;
                        if (brakeFriction < 0.7f)
                        {
                            pForce[2] *= 0.1f;
                        }
                        // friction can not be applied in same direction as speed
                        if (pForce[0] * speed[0] < 0)
                        {
                            pForce[0] = 0;
                        }
                        if (pForce[1] * speed[1] < 0)
                        {
                            pForce[1] = 0;
                        }
                        if (pForce[2] * speed[2] < 0)
                        {
                            pForce[2] = 0;
                        }

                        pForce = DirectionModelToWorld(pForce) * (GetMass() * (1.0 / 40000) * contactCoef);
#if ARROWS
                        AddForce(wCenter + pCenter, -pForce * InvMass(), Color(0.5, 0, 0.5));
#endif

                        friction += LimitFriction(pForce, speed, deltaT, InvMass());

                        torqueFriction += _angMomentum * 0.5f * contactCoef;
                        tfCount++;

                        float landMoved = info.under;
                        saturateMin(landMoved, 0.1);
                        pForce[0] = speed[0] * 4500;
                        pForce[1] = 0;
                        pForce[2] = speed[2] * 2000;
                        pForce = DirectionModelToWorld(pForce) *
                                 (
                                     // GetMass()*(1.0/1000)*contactCoef*landMoved*soft*Type()->_terrainCoef
                                     GetMass() * (1.0 / 1000) * contactCoef * landMoved * soft * Type()->_terrainCoef);
#if ARROWS
                        AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 0, 1));
#endif
                        friction += LimitFriction(pForce, speed, deltaT, InvMass());
                    }
                }

                for (int w = 0; w < MaxWheels; w++)
                {
                    damperForces[w] += _dampers[w] * initDamper;
                }

                // torqueFriction=_angMomentum*1.0;
                if (!Type()->_canFloat)
                {
                    const float maxFord = 1.8;
                    if (maxUnderWater > maxFord)
                    {
                        float dammage = (maxUnderWater - maxFord) * 0.5;
                        saturateMin(dammage, 0.2);
                        LocalDammage(nullptr, this, VZero, dammage * deltaT, GetRadius());
                    }
                }
                if (_waterContact)
                {
                    const SurfaceInfo& info = GLandscape->GetWaterSurface();
                    soft = info._roughness * 2.5f;
                    dust = info._dustness * 2.5f;
                    saturateMin(dust, 1);
                }
                if (maxUnder > MAX_UNDER)
                {
                    // it is neccessary to move object immediatelly
                    Matrix4 transform = moveTrans.Transform();
                    Vector3 newPos = transform.Position();
                    float moveUp = maxUnder - MAX_UNDER;
                    newPos[1] += moveUp;
                    transform.SetPosition(newPos);
                    moveTrans.SetTransform(transform);
                    // we move up - we have to maintain total energy
                    // what potential energy will gain, kinetic must loose
                    const float crashLimit = 0.3f;
                    if (moveUp > crashLimit)
                    {
                        crash += moveUp - crashLimit;
                        saturateMax(maxCFactor, 1);
                    }
                    float potentialGain = moveUp * GetMass();
                    float oldKinetic = GetMass() * _speed.SquareSize() * 0.5f;
                    float crashFactor = (moveUp - crashLimit) * 4 + 1.5f;
                    saturateMax(crashFactor, 2.5f);
                    float newKinetic = oldKinetic - potentialGain * crashFactor;
                    float newSpeedSize2 = newKinetic * InvMass() * 2;
                    if (newSpeedSize2 <= 0 || oldKinetic <= 0)
                    {
                        _speed = VZero;
                    }
                    else
                    {
                        _speed *= sqrt(newSpeedSize2 * _speed.InvSquareSize());
                    }
                }
            }

#define DIAG_DAMPERS 0
#if DIAG_DAMPERS
            RString dampers = "Dampers ";
#endif

            for (int w = 0; w < MaxWheels; w++)
            {
                _dampers[w] += damperForces[w] * deltaT;
                saturate(_dampers[w], -MaxDamper, +MaxDamper);
#if DIAG_DAMPERS
                if (w == FLWheel || w == FRWheel || w == BLWheel || w == BRWheel)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), " %.2f (f %.2f)", _dampers[w], damperForces[w]);
                    dampers = dampers + RString(buf);
                }
#endif
            }
#if DIAG_DAMPERS
            GlobalShowMessage(100, dampers);
            LOG_DEBUG(Physics, "{}", (const char*)dampers);
#endif

            force += objForce;
            force += landForce;

            if (IsLocal())
            {
                float crashTreshold = 10 * GetMass(); // 2G
                float forceCrash = 0;
                if (objForce.SquareSize() > Square(crashTreshold))
                {
                    // crash as g-term
                    forceCrash = (objForce.Size() - crashTreshold) * InvMass() * (1.0f / 10);
                    crash += forceCrash;
                }
                if (crash > 0.1f)
                {
                    float speedCrash = (maxColSpeed2 - 3) * Square(1.0f / 7);
                    if (speedCrash < 0.1)
                    {
                        speedCrash = 0;
                    }
                    if (Glob.time > _disableDammageUntil && speedCrash > 0)
                    {
                        // crash boom bang state - impact speed too high
                        _doCrash = CrashLand;
                        if (_objectContact)
                        {
                            _doCrash = CrashObject;
                        }
                        if (_waterContact)
                        {
                            _doCrash = CrashWater;
                        }
                        _crashVolume = crash * 0.2f;
                        saturateMin(crash, speedCrash);
                        crash *= floatMin(1, maxCFactor);
                        CrashDammage(crash); // 1g -> 5 mm dammage
                        DammageCrew(Driver(), crash * 0.03f, "");
                        /*
                        LOG_DEBUG(Physics,
                            "Crash %g, speed %g, factor %g",
                            crash,sqrt(maxColSpeed2),maxCFactor
                        );
                        */
                    }
                }
            }
        }

        ApplyForces(deltaT, force, torque, friction, torqueFriction);

        bool stopCondition = false;
        if (_pilotBrake && _landContact && !_waterContact && !_objectContact)
        {
            // apply static friction
            float maxSpeed = Square(0.7f);
            if (!Driver())
            {
                maxSpeed = Square(1.2f);
            }
            if (_speed.SquareSize() < maxSpeed && _angVelocity.SquareSize() < maxSpeed * 0.3f)
            {
                stopCondition = true;
            }
        }
        if (stopCondition)
        {
            StopDetected();
        }
        else
        {
            IsMoved();
        }

        if (prec <= SimulateCamera)
        {
            _head.Move(deltaT, moveTrans, *this);
        }

        if (EnableVisualEffects(prec))
        {
            if (DirectionUp().Y() >= 0.3f)
            {
                _track.Update(*this, deltaT, !_landContact);
            }
            if (_landContact)
            {
                Vector3 lcPos = (_track.BackLeftPos() + _track.FrontLeftPos()) * 0.5;
                Vector3 rcPos = (_track.BackRightPos() + _track.FrontRightPos()) * 0.5f;
                Vector3 lPos = PositionModelToWorld(lcPos + Vector3(-0.2f, 0.1f, 0));
                Vector3 rPos = PositionModelToWorld(rcPos + Vector3(+0.2f, 0.1f, 0));
                float dSoft = floatMax(dust, 0.0025f);
                float density = speedSize * (1.0f / 10) * dSoft;
                saturate(density, 0, 1.0);
                float dustColor = dSoft * 8;
                saturate(dustColor, 0, 1);
                Color color = Color(0.51f, 0.46f, 0.33f) * dustColor + Color(0.5f, 0.5f, 0.5f) * (1 - dustColor);
                // color.SetA(0.3);
                _leftDust.SetColor(color);
                _rightDust.SetColor(color);
                _leftDust.Simulate(lPos + _speed * 0.2f, _speed * 0.5f, density, deltaT);
                _rightDust.Simulate(rPos + _speed * 0.2f, _speed * 0.5f, density, deltaT);
            }

            SimulateExhaust(deltaT, prec);
        }

        if (HasTurret())
        {
            _turret.Stabilize(this, Type()->_turret, Transform().Orientation(), moveTrans.Orientation());
        }

        Move(moveTrans);
        DirectionWorldToModel(_modelSpeed, _speed);
    }

    if (!CommanderUnit() || CommanderUnit()->IsInCargo())
    {
        // Note: virtual function LightsOn/Off — needs LightsOn()/LightsOff() overrides in subclasses
        _pilotLight = false;
    }
}

void Car::Eject(AIUnit* unit)
{
    base::Eject(unit);
}

void Car::FakePilot(float deltaT)
{
    _turnIncreaseSpeed = 1;
    _turnDecreaseSpeed = 1;
}

void Car::PerformFF(FFEffects& eff)
{
    base::PerformFF(eff);
    float stiffX = fabs(ModelSpeed().Z()) / GetType()->GetMaxSpeedMs() + 0.3f;
    saturate(stiffX, 0, 1);
    eff.stiffnessX = stiffX;
    eff.stiffnessY = 0.3f;
}

void Car::JoystickPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _thrustWanted = input.GetStickForward();

    // adjust stick left curve
    float turn = -input.GetStickLeft();

    _turnWanted = turn * fabs(turn);

    _turnIncreaseSpeed = 2;
    _turnDecreaseSpeed = 2;

    Limit(_thrustWanted, -1, 1);
    Limit(_turnWanted, -1, 1);
    if (fabs(_thrustWanted) > 0.05f)
    {
        CancelStop();
    }
    if (fabs(_turnWanted) > 0.05f)
    {
        CancelStop();
    }
    if (fabs(_thrustWanted) < 0.2f && fabs(_modelSpeed.Z()) < 4.0f)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = false;
        CancelStop(), EngineOn();
    }
}

void Car::SuspendedPilot(AIUnit* unit, float deltaT)
{
    _pilotBrake = true;
    _thrustWanted = 0;
    _turnWanted = 0;
}

void Car::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    CancelStop();

    if (input.IsJoystickActive())
    {
        JoystickPilot(deltaT);
        return;
    }

    constexpr InputContext ctx = InputContext::CarDriver;
    float forward = (input.GetAction(ctx, UAMoveForward) - input.GetAction(ctx, UAMoveBack)) * 0.75f;
    forward += input.GetAction(ctx, UAMoveFastForward);
    forward += input.GetAction(ctx, UAMoveSlowForward) * 0.33f;
    _thrustWanted = forward;

    float maxThrust = 1 - fabs(_turn) * 0.7;
    saturate(_thrustWanted, -maxThrust, +maxThrust);

    float asz = fabs(ModelSpeed().Z());

    bool internalCamera = IsGunner(GWorld->GetCameraType());
    if (internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
    {
        // last input from mouse - use mouse controls
        Vector3 relDir(VMultiply, DirWorldToModel(), _mouseDirWanted);
        _turnWanted = atan2(relDir.X(), relDir.Z()) * 0.7;

        _turnIncreaseSpeed = 2;
        _turnDecreaseSpeed = 2;

        float maxTurnCoef = 1 - asz * (1.0 / 5);
        saturateMax(maxTurnCoef, 0);

        // limit max turn based on speed
        float maxTurn = maxTurnCoef * 1.0 + (1 - maxTurnCoef) * 0.5;
        saturate(_turnWanted, -maxTurn, +maxTurn);
    }
    else
    {
        // turn with arrows
        _turnWanted = input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft);
        _mouseDirWanted = Direction();

        float slowTurn = 1 - asz * (1.0 / 15);
        saturateMax(slowTurn, 0);
        _turnIncreaseSpeed = slowTurn * 0.3 + 0.2;
        _turnDecreaseSpeed = slowTurn * 0.8 + 0.5;

        float maxTurnCoef = 1 - asz * (1.0 / 10);
        saturateMax(maxTurnCoef, 0);

        // limit max turn based on speed
        float maxTurn = maxTurnCoef * 1.0 + (1 - maxTurnCoef) * 0.3;
        saturate(_turnWanted, -maxTurn, +maxTurn);

        // limit max thrust based on turn
    }

    Limit(_thrustWanted, -1, 1);
    Limit(_turnWanted, -1, 1);
    if (fabs(forward) > 0.05)
    {
        CancelStop(), EngineOn();
    }
    if (fabs(_turnWanted) > 0.05)
    {
        CancelStop();
    }

    if (fabs(_thrustWanted) < 0.2 && fabs(_modelSpeed.Z()) < 4.0 || _thrustWanted * _modelSpeed.Z() < 0)
    {
        _pilotBrake = true;
        if (fabs(_modelSpeed.Z()) > 1)
        {
            _thrustWanted = 0;
        }
    }
    else
    {
        _pilotBrake = false;
    }
}

RString Car::DiagText() const
{
    float dx, dz;
    Vector3 pos = Position();

    pos[1] = GLandscape->SurfaceYAboveWater(pos[0], pos[2], &dx, &dz);

    float slope = Direction().Z() * dz + Direction().X() * dx;
    char buf[256];
    snprintf(buf, sizeof(buf), " Slope %.0f %% %s %.2f,(%.2f)", slope * 100, _pilotBrake ? "B" : "E", _thrustWanted,
             _turnWanted);
    sprintf(buf + strlen(buf), " RF %.1f,%.1f", _reverseTimeLeft, _forwardTimeLeft);
    return base::DiagText() + (RString)buf;
}

#define DIAG_SPEED 0

void Car::AIPilot(AIUnit* unit, float deltaT)
{
    if (unit)
    {
        SelectFireWeapon();
    }

    _turnIncreaseSpeed = 1;
    _turnDecreaseSpeed = 1;

    // Note: limit AIPilot simulation rate (10 Hz)
    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());
    bool isLeader = unit->IsSubgroupLeader();

    Vector3Val speed = ModelSpeed();

    float headChange = 0;
    float speedWanted = 0;
    float turnPredict = 0;

    if (unit->GetState() == AIUnit::Stopping)
    {
        if (fabs(speed[2]) < 1)
        {
            UpdateStopTimeout();
            unit->SendAnswer(AI::StepCompleted);
        }
        speedWanted = 0;
    }
    else if (unit->GetState() == AIUnit::Stopped)
    {
        speedWanted = 0;
    }
    else if (!isLeader)
    {
        FormationPilot(speedWanted, headChange, turnPredict);
    }
    else
    {
        LeaderPilot(speedWanted, headChange, turnPredict);
    }

    float aHC = fabs(headChange);

    bool disableForward = false;
    if (ModelSpeed().Z() < 10)
    {
        if (aHC < (H_PI * 6 / 16))
        {
            if (_reverseTimeLeft > 0)
            {
                CreateFreshPlan();
                _forwardTimeLeft = 2;
            }
            _reverseTimeLeft = 0;
        }
        else if (aHC > (H_PI * 8 / 16))
        {
            _reverseTimeLeft = 2;
        }

        CollisionBuffer retVal;
        GLandscape->ObjectCollision(retVal, this, nullptr, Position(), Position() + CollisionSize() * 1.5 * Direction(),
                                    1.5, ObjIntersectGeom);
        disableForward = (retVal.Size() > 0);
    }

    bool reverse = false;
    if (_forwardTimeLeft > 0)
    {
        _forwardTimeLeft -= deltaT;
    }
    else if (_reverseTimeLeft > 0 || disableForward)
    {
        // check if back is free
        CollisionBuffer retVal;
        GLandscape->ObjectCollision(retVal, this, nullptr, Position(), Position() - CollisionSize() * 2 * Direction(),
                                    1.5, ObjIntersectGeom);
        if (retVal.Size() > 0)
        {
            _reverseTimeLeft = 0;
            _forwardTimeLeft = 1;
        }
        else
        {
            _reverseTimeLeft -= deltaT;
            if (_reverseTimeLeft <= 0)
            {
                CreateFreshPlan();
                _forwardTimeLeft = 1;
            }
            reverse = true;
            if (speedWanted > 0)
            {
                speedWanted = -speedWanted;
                headChange = AngleDifference(H_PI, headChange);
            }
        }
    }

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Pilot speed {:.1f}", speedWanted);
    }
#endif

    AvoidCollision(deltaT, speedWanted, headChange);

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Avoid {:.1f}", speedWanted);
    }
#endif

    float curHeading = atan2(Direction()[0], Direction()[2]);
    float wantedHeading = curHeading + headChange;

    // estimate inertial orientation change
    Matrix3Val orientation = Orientation();
    Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation * 1.0;
    Vector3Val estDirection = estOrientation.Direction();
    float estHeading = atan2(estDirection[0], estDirection[2]);

    headChange = AngleDifference(wantedHeading, estHeading);

    {
        float aTP = fabs(turnPredict);
        if (aTP > H_PI / 64)
        {
            float maxSpeed = GetType()->GetMaxSpeedMs();
            // faster vehicles have better turning radius; clamp to a reasonable range
            saturate(maxSpeed, 50 / 3.6, 100 / 3.6);
            float limitSpeed = Interpolativ(aTP, H_PI / 64, H_PI / 8, maxSpeed, 5);

#if _ENABLE_CHEATS
            if (CHECK_DIAG(DEPath) && this == GWorld->CameraOn())
            {
                GlobalShowMessage(200, "Turn limit %.1f (%.3f, turn %.3f)", limitSpeed * 3.6, headChange, turnPredict);
                // LOG_DEBUG(Physics, "Turn limit {:.1f} ({:.3f}, turn {:.3f})",limitSpeed,headChange,turnPredict);
            }
#endif

            saturate(speedWanted, -limitSpeed, +limitSpeed);
        }
    }

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Turn {:.1f}", speedWanted);
    }
#endif

    if (fabs(speedWanted) > 0.5)
    {
        EngineOn();
    }

    float isSlow = 1 - fabs(speed.Z()) * (1.0 / 17);
    float maxTurn = 1 - fabs(speed.Z()) * (1.0 / 25);
    saturate(isSlow, 0.2, 1);
    saturate(maxTurn, 0.05, 1);

    float isLevel = 1 - fabs(Direction()[1] * (1.0 / 0.6));
    saturate(isLevel, 0.2, 1);
    saturateMax(isSlow, isLevel); // change thrust slowly on steep surfaces

    if (fabs(speedWanted) < 0.1 && speed.SquareSize() < 1)
    {
        _thrustWanted = 0;
    }
    else
    {
        Vector3 relAccel = DirectionWorldToModel(_acceleration);
        float changeAccel = (speedWanted - speed.Z()) * (1 / 0.5) - relAccel.Z();
        changeAccel *= isSlow;
        float thrustOld = _thrust;
        float thrust = thrustOld + changeAccel * 0.33;
        Limit(thrust, -1, 1);
        _thrustWanted = thrust;
    }

    _turnWanted = headChange * 4;
    if (reverse)
    {
        _turnWanted = -_turnWanted;
    }

    // limit turn based on speed (to avoid slips)

    Limit(_thrustWanted, -1, 1);
    Limit(_turnWanted, -maxTurn, +maxTurn);

    if (fabs(speed[2]) < 5)
    { // may be switching from/to reverse
        if (fabs(_turnWanted - _turn) > 0.6 || _turnWanted * _turn < 0 && fabs(_turnWanted - _turn) > 0.2)
        {
            saturate(speedWanted, -0.6, +0.6);
        }
    }

    _pilotBrake = fabs(speedWanted) < 0.1 || fabs(speedWanted) < fabs(speed[2]) - 5;
}

const float RoadFaster = 2.0;

static const float ObjPenalty1[] = {1.0, 1.05, 1.1, 1.4};

static const float ObjPenalty2[] = {1.0, 1.20, 1.5, 2.0};
static const float ObjRoadPenalty2[] = {1.0, 1.10, 1.2, 1.4};

float Car::GetFieldCost(const GeographyInfo& info) const
{
    int nObj = info.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    if (info.u.road || info.u.track)
    {
        return (1.0 / RoadFaster) * ObjRoadPenalty2[nObj];
    }
    else
    {
        return ObjPenalty2[nObj];
    }
}

float Car::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost() * RoadFaster;
    if (geogr.u.waterDepth > 0 && !(geogr.u.road || geogr.u.track))
    {
        if (geogr.u.waterDepth >= 2)
        {
            return (Type()->_canFloat ? 1.0 / 2 : 1e30);
        }
        else
        {
            return (Type()->_canFloat ? 1.0 / 6 : 1.0 / 0.5);
        }
    }
    if (geogr.u.full)
    {
        return 1e30;
    }
    int nObj = geogr.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    cost *= ObjPenalty1[nObj];
    int grad = geogr.u.gradient;
    if (grad >= 6)
    {
        return 1e30;
    }
    static const float gradPenalty[6] = {1.0, 1.02, 1.05, 1.1, 2.0, 3.0};
    cost *= gradPenalty[grad];
    return cost;
}

float Car::GetCostTurn(int difDir) const
{ // in sec
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float aDir2 = aDir * aDir;
    float cost = aDir * 0.15 + aDir2 * 0.02 + aDir2 * aDir * 0.05;
    if (difDir < 0)
    {
        return cost * 0.8;
    }
    return cost;
}

float Car::GetPathCost(const GeographyInfo& geogr, float dist) const
{
    float cost = Type()->GetMinCost();
    int nObj = geogr.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    cost *= ObjRoadPenalty2[nObj];

    return cost * dist;
}

void Car::FillPathCost(Path& path) const
{
    base::FillPathCost(path);
}

bool Car::FireWeapon(int weapon, TargetType* target)
{
    if (GetNetworkManager().IsControlsPaused())
    {
        return false;
    }
    if (HasTurret())
    {
        if (weapon >= NMagazineSlots())
        {
            return false;
        }
        if (!GetWeaponLoaded(weapon))
        {
            return false;
        }
        if (!IsFireEnabled())
        {
            return false;
        }

        const WeaponModeType* mode = GetWeaponMode(weapon);
        if (!mode || !mode->_ammo)
        {
            return false;
        }
        bool fired = false;
        switch (mode->_ammo->_simulation)
        {
            case AmmoShotBullet:
            {
                Matrix4Val shootTrans = GunTurretTransform();
                fired = FireMGun(weapon, shootTrans.FastTransform(Type()->_turret._pos),
                                 shootTrans.Rotate(Type()->_turret._dir), target);
            }
            break;
            case AmmoNone:
                break;
            default:
                Fail("Unknown ammo used.");
                break;
        }
        if (fired)
        {
            VehicleWithAI::FireWeapon(weapon, target);
            return true;
        }
        return false;
    }
    else
    {
        // weapon is horn
        if (weapon < 0 || weapon >= NMagazineSlots())
        {
            return false;
        }
        if (!IsFireEnabled())
        {
            return false;
        }
        if (!_hornSound)
        {
            const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
            if (muzzle)
            {
                IWave* sound = GSoundScene->OpenAndPlayOnce(muzzle->_sound.name, Position(), Speed(),
                                                            muzzle->_sound.vol, muzzle->_sound.freq);
                if (sound)
                {
                    GSoundScene->SimulateSpeedOfSound(sound);
                    GSoundScene->AddSound(sound);
                    _hornSound = sound;
                }
                // Replicate to other players regardless of local audio: the horn
                // fires through the sound path, not NMTFireWeapon (no magazine), so
                // send NMTPlaySound (matches the OFP:Elite engine).
                GetNetworkManager().PlaySound(muzzle->_sound.name, Position(), Speed(), muzzle->_sound.vol,
                                              muzzle->_sound.freq, _hornSound);
            }
            return true;
        }
        return false;
    }
}

void Car::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    if (HasTurret())
    {
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        if (!magazine || slot._magazine != magazine)
        {
            return;
        }

        const WeaponModeType* mode = GetWeaponMode(weapon);
        if (!mode)
        {
            return;
        }
        if (!mode->_ammo)
        {
            return;
        }

        if (EnableVisualEffects(SimulateVisibleNear))
        {
            switch (mode->_ammo->_simulation)
            {
                case AmmoShotBullet:
                    _mGunClouds.Start(0.1);
                    _mGunFire.Start(0.1, 0.4, true);
                    _mGunFireFrames = 1;
                    _mGunFireTime = Glob.uiTime;
                    int newPhase;
                    while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == _mGunFirePhase)
                    {
                        ;
                    }
                    _mGunFirePhase = newPhase;
                    break;
                case AmmoNone:
                    break;
            }
        }
    }
    base::FireWeaponEffects(weapon, magazine, target);
}

Vector3 Car::GetCameraDirection(CameraType camType) const
{
    // in world coordinates
    // aside - based on _turnWanted
    bool isDriver = true;
    AIUnit* unit = GWorld->FocusOn();
    if (unit)
    {
        Person* man = unit->GetPerson();
        isDriver = man == Driver();
    }

    if (!GWorld->LookAroundEnabled() && QIsManual() && isDriver)
    {
        Matrix3 rotY(MRotationY, -_turn * 0.5f);
        return rotY * Direction();
    }
    return base::GetCameraDirection(camType);
}

void Car::LimitCursor(CameraType camType, Vector3& dir) const {}

bool Car::AimWeapon(int weapon, Vector3Par direction)
{
    if (!HasTurret())
    {
        return true;
    }

    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    SelectWeapon(weapon);
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);

    if (_turret.Aim(Type()->_turret, relDir))
    {
        CancelStop();
    }
    return true;
}

bool Car::AimWeapon(int weapon, Target* target)
{
    if (!HasTurret())
    {
        return true;
    }

    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    _fire.SetTarget(CommanderUnit(), target);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    Vector3 weaponPos = Type()->_turret._pos;
    Vector3 myPos = PositionModelToWorld(weaponPos);
    Vector3 tgtPos = target->AimingPosition();

    if (aInfo)
    {
        float dist2 = tgtPos.Distance2(Position());
        float time2 = dist2 * Square(aInfo->_invInitSpeed * 1.2);

        float time = sqrt(time2);
        const float minPredTime = 0.25;
        float predTime = floatMax(time + 0.1, minPredTime);

        myPos += Speed() * minPredTime;
        float fall = 0.5 * G_CONST * time2;
        tgtPos[1] += fall;
        Vector3 speedEst = target->speed;
        const float maxSpeedEst = aInfo->_maxLeadSpeed;
        if (speedEst.SquareSize() > Square(maxSpeedEst))
        {
            speedEst = speedEst.Normalized() * maxSpeedEst;
        }
        tgtPos += speedEst * predTime;
    }
    return AimWeapon(weapon, tgtPos - myPos);
}

Vector3 Car::GetWeaponDirectionWanted(int weapon) const
{
    if (!HasTurret())
    {
        return Direction();
    }

    Vector3 dir = Type()->_turret._dir;
    Matrix3Val aim = _turret.GetAimWanted();
    return Transform().Rotate(aim * dir);
}

Vector3 Car::GetWeaponDirection(int weapon) const
{
    if (!HasTurret())
    {
        return Direction();
    }

    Vector3 dir = Type()->_turret._dir;
    return Transform().Rotate(GunTurretTransform().Rotate(dir));
}

Vector3 Car::GetWeaponCenter(int weapon) const
{
    if (!HasTurret())
    {
        return VZero;
    }

    return _turret.GetCenter(Type()->_turret);
}

float Car::GetEngineVol(float& freq) const
{
    freq = (_randomizer * 0.05 + 0.95) * _rpm * 1.2;
    return fabs(_thrust) * 0.5 + 0.5;
}

float Car::GetEnvironVol(float& freq) const
{
    freq = 1;
    return _speed.SquareSize() / Square(Type()->GetMaxSpeedMs());
}

bool Car::IsAbleToMove() const
{
    float lWheelHit = floatMax(GetHit(Type()->_wheelLFHit), GetHit(Type()->_wheelLBHit));
    float rWheelHit = floatMax(GetHit(Type()->_wheelRFHit), GetHit(Type()->_wheelRBHit));
    if (lWheelHit >= 0.9)
    {
        return false;
    }
    if (rWheelHit >= 0.9)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9)
    {
        return false;
    }
    return base::IsAbleToMove();
}

bool Car::IsPossibleToGetIn() const
{
    float lWheelHit = floatMax(GetHit(Type()->_wheelLFHit), GetHit(Type()->_wheelLBHit));
    float rWheelHit = floatMax(GetHit(Type()->_wheelRFHit), GetHit(Type()->_wheelRBHit));
    if (lWheelHit >= 0.9)
    {
        return false;
    }
    if (rWheelHit >= 0.9)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9)
    {
        return false;
    }
    return base::IsPossibleToGetIn();
}

bool Car::IsCautious() const
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return false;
    }
    CombatMode mode = unit->GetCombatMode();
    return mode == CMStealth || mode == CMCombat; // in AWARE state - lights is on and move on road
}

void Car::Sound(bool inside, float deltaT)
{
    if (HasTurret())
    {
        _turret.Sound(Type()->_turret, inside, deltaT, *this, Speed());
    }
    if (_gearSound)
    {
        float gearVol = Type()->_gearSound.vol;
        _gearSound->SetVolume(gearVol);
        _gearSound->SetPosition(Position(), Speed());
        _gearSound->Set3D(!inside);
    }
    if (_hornSound)
    {
        _hornSound->SetPosition(Position(), Speed());
        _hornSound->Set3D(!inside);
    }
    if (_scudState >= 1 && _scudState < 2)
    {
        const SoundPars& sound = Type()->_scudSoundElevate;
        if (!_scudSound && sound.name.GetLength() > 0)
        {
            _scudSound = GSoundScene->OpenAndPlay(sound.name, _scudPos, _scudSpeed);
            if (_scudSound)
            {
                _scudSound->SetVolume(sound.vol, sound.freq);
            }
        }
    }
    else if (_scudState >= 3 && _scudState < 4)
    {
        const SoundPars& sound = Type()->_scudSound;
        if (!_scudSound && sound.name.GetLength() > 0)
        {
            _scudSound = GSoundScene->OpenAndPlay(sound.name, _scudPos, _scudSpeed);
            if (_scudSound)
            {
                _scudSound->SetVolume(sound.vol, sound.freq);
            }
        }
    }
    else
    {
        _scudSound.Free();
    }

    if (_scudSound)
    {
        _scudSound->SetPosition(_scudPos, _scudSpeed);
    }

    base::Sound(inside, deltaT);
}

void Car::UnloadSound()
{
    base::UnloadSound();
    if (HasTurret())
    {
        _turret.UnloadSound();
    }

    _scudSound.Free();
}

Matrix4 Car::InsideCamera(CameraType camType) const
{
    Matrix4 transf;
    if (!GetProxyCamera(transf, camType))
    {
        Vector3 pos = Type()->_pilotPos;
        transf.SetTranslation(pos);

        Vector3 up = _head.Position() - _head.Neck();
        up = up * 0.25 + Vector3(0, 0.75, 0);
        transf.SetUpAndAside(up, VAside);
    }

    Vector3 dir = transf.Direction();

    bool isDriver = true;
    AIUnit* unit = GWorld->FocusOn();
    if (unit)
    {
        Person* man = unit->GetPerson();
        isDriver = man == Driver();
    }

    if (!GWorld->LookAroundEnabled() && isDriver)
    {
        Matrix3 rotY(MRotationY, -_turn * 0.5f);
        dir = rotY.Direction();
    }
    transf.SetDirectionAndUp(dir, transf.DirectionUp());
    return transf;
}

Vector3 Car::ExternalCameraPosition(CameraType camType) const
{
    return Type()->_extCameraPosition;
}

int Car::InsideLOD(CameraType camType) const
{
    int level = -1;
    // Note: select level based on proxy camera
    if (level < 0)
    {
        level = GetShape()->FindLevel(VIEW_PILOT);
    }
    return level;
}

bool Car::HasFlares(CameraType camType) const
{
    if (camType == CamGunner || camType == CamInternal)
    {
        return false;
    }
    return base::HasFlares(camType);
}

Matrix4 Car::TurretTransform() const
{
    if (HasTurret())
    {
        int memory = GetShape()->FindMemoryLevel();
        int sel = Type()->_turret._body.GetSelection(memory);
        if (sel >= 0)
        {
            Matrix4 mat = MIdentity;
            AnimateMatrix(mat, memory, sel);
            return mat;
        }
    }
    return MIdentity;
}

Matrix4 Car::GunTurretTransform() const
{
    if (HasTurret())
    {
        int memory = GetShape()->FindMemoryLevel();
        int sel = Type()->_turret._gun.GetSelection(memory);
        if (sel >= 0)
        {
            Matrix4 mat = MIdentity;
            AnimateMatrix(mat, memory, sel);
            return mat;
        }
    }
    return MIdentity;
}

void Car::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);

    Type()->_plateInfos.Draw(level, clipFlags, pos, GetPlateNumber());
}

void Car::DrawProxies(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                      float dist2, float z2, const LightList& lights)
{
    const ScudProxy& proxy = Type()->_proxies[level];
    if (proxy.IsPresent() && _scudState < 4)
    {
        Object* obj = proxy.obj;

        Matrix4 proxyTransform = obj->Transform();
        AnimateMatrix(proxyTransform, level, proxy.selection);

        // smart clipping par of obj->Draw
        Matrix4Val pTransform = transform * proxyTransform;

        // LOD detection
        LODShapeWithShadow* pshape = nullptr;
        if (_scudState >= 3)
        {
            pshape = Type()->_scudModelFire;
        }
        else
        {
            pshape = Type()->_scudModel;
        }
        if (pshape)
        {
            int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                                   GScene->GetCamera()->Direction());
            if (level != LOD_INVISIBLE)
            {
                // construct FrameWithInverse from transform and invTransform
                Matrix4Val invPTransform = pTransform.InverseScaled();
                Shape* shape = pshape->LevelOpaque(level);
                shape->PrepareTextures(z2, shape->Special());
                shape->Draw(this, lights, ClipAll, shape->Special(), pTransform, invPTransform);
            }
        }
    }

    base::DrawProxies(level, clipFlags, transform, invTransform, dist2, z2, lights);
}

void Car::SimulateHUD(CameraType camType, float deltaT) {}

RString Car::GetActionName(const UIAction& action)
{
    switch (action.type)
    {
        case ATScudLaunch:
            return LocalizeString(IDS_ACTION_SCUDLAUNCH);
        case ATScudStart:
            return LocalizeString(IDS_ACTION_SCUDSTART);
        default:
            return base::GetActionName(action);
    }
}

void Car::PerformAction(const UIAction& action, AIUnit* unit)
{
    switch (action.type)
    {
        case ATScudLaunch:
            _scudState = 1.0f;
            break;
        case ATScudStart:
            _scudState = 3.0f;
            break;
        default:
            base::PerformAction(action, unit);
            break;
    }
}

void Car::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    base::GetActions(actions, unit, now);

    if (!unit)
    {
        return;
    }
    if (unit == CommanderUnit() && unit->GetLifeState() == AIUnit::LSAlive)
    {
        if (_scudState < 1.0 && Type()->_scudLaunch)
        {
            actions.Add(ATScudLaunch, this, 0.04);
        }
        else if (_scudState >= 2.0 && _scudState < 3.0 && Type()->_scudStart)
        {
            actions.Add(ATScudStart, this, 0.04, 0, true);
        }
    }
}

bool Car::IsContinuous(CameraType camType) const
{
    return false;
}

bool Car::IsGunner(CameraType camType) const
{
    return camType == CamGunner || camType == CamInternal || camType == CamExternal;
}

LSError Car::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.Serialize("thrustWanted", _thrustWanted, 1, 0))
        PARAM_CHECK(ar.Serialize("thrust", _thrust, 1, 0))
        PARAM_CHECK(ar.Serialize("turnWanted", _turnWanted, 1, 0))
        PARAM_CHECK(ar.Serialize("turn", _turn, 1, 0))
    }

    return LSOK;
}

NetworkMessageType Car::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateCar;
        case NMCUpdatePosition:
            return NMTUpdatePositionCar;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_CAR_MSG(XX)                                                                                   \
    XX(RString, plateNumber, NDTString, NCTNone, DEFVALUE(RString, "Debug markerDebug markerXX"),            \
       DOC_MSG("Plate number"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR)                             \
    XX(float, scudState, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Scud launcher state"), IdxTransfer, \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateCar, UpdateTankOrCar, UPDATE_CAR_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateCar, UpdateTankOrCar, UPDATE_CAR_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateCar)

namespace Poseidon
{

#define UPDATE_POSITION_CAR_MSG(XX)                                                                                   \
    XX(Turret, turret, NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret),                                             \
       DOC_MSG("Turret object (for example for cars with MG)"), IdxTransferObject, ET_ABS_DIF, 1)                     \
    XX(float, rpmWanted, NDTFloat, NCTFloat0To2, DEFVALUE(float, 0), DOC_MSG("Wanted value of RPM"), IdxTransfer,     \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                              \
    XX(float, thrustWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted thrust"), IdxTransfer,      \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                              \
    XX(float, turnWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted turning angle"), IdxTransfer, \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdatePositionCar, UpdatePositionVehicle, UPDATE_POSITION_CAR_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionCar, UpdatePositionVehicle, UPDATE_POSITION_CAR_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdatePositionCar)

namespace Poseidon
{

NetworkMessageFormat& Car::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_CAR_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            UPDATE_POSITION_CAR_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Car::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateCar*>(ctx.GetIndices()))
                    const IndicesUpdateCar* indices = static_cast<const IndicesUpdateCar*>(ctx.GetIndices());

                ITRANSF(plateNumber)
                ITRANSF(scudState)
            }
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionCar*>(ctx.GetIndices()))
                const IndicesUpdatePositionCar* indices =
                    static_cast<const IndicesUpdatePositionCar*>(ctx.GetIndices());

            Matrix3 oldTrans = Orientation();
            TMCHECK(base::TransferMsg(ctx))
            if (ctx.IsSending() || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->turret, _turret))
            _turret.Stabilize(this, Type()->_turret, oldTrans, Orientation());
            ITRANSF(rpmWanted)
            ITRANSF(thrustWanted)
            ITRANSF(turnWanted)
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float Car::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateCar*>(ctx.GetIndices()))
                    const IndicesUpdateCar* indices = static_cast<const IndicesUpdateCar*>(ctx.GetIndices());

                ICALCERR_NEQSTR(plateNumber, ERR_COEF_VALUE_MAJOR)
                ICALCERR_ABSDIF(float, scudState, ERR_COEF_VALUE_MAJOR)
            }
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionCar*>(ctx.GetIndices()))
                const IndicesUpdatePositionCar* indices =
                    static_cast<const IndicesUpdatePositionCar*>(ctx.GetIndices());

            int index = indices->turret;
            if (index >= 0)
            {
                NetworkMessageFormatBase* format = const_cast<NetworkMessageFormatBase*>(ctx.GetFormat());
                NetworkMessageFormatItem& item = format->GetItem(index);
                CHECK_ASSIGN(typeVal, item.defValue, const RefNetworkDataTyped<int>);
                int type = typeVal.GetVal();
                NetworkMessageFormatBase* subformat = ctx.GetComponent()->GetFormat((NetworkMessageType)type);
                if (subformat)
                {
                    const RefNetworkData& val = ctx.GetMessage()->values[index];
                    CHECK_ASSIGN(msgVal, val, const RefNetworkDataTyped<NetworkMessage>);
                    NetworkMessage& submsg = msgVal.GetVal();
                    NetworkMessageContext subctx(&submsg, subformat, ctx);
                    error += _turret.CalculateError(subctx);
                }
            }
            ICALCERR_ABSDIF(float, rpmWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, thrustWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, turnWanted, ERR_COEF_VALUE_MAJOR)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
