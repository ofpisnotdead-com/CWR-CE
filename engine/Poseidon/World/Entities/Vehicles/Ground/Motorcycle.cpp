#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/World/Entities/Vehicles/Ground/Motorcycle.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{
static const Color MotorcycleLightColor(0.9, 0.8, 0.8);
static const Color MotorcycleLightAmbient(0.1, 0.1, 0.1);

#if _ENABLE_CHEATS
#define ARROWS 1
#endif

const float MaxDamper = 0.2;
const float InvMaxDamper = 1 / MaxDamper;

MotorcycleType::MotorcycleType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;
}

void MotorcycleType::Load(const ParamEntry& par)
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
    _isBicycle = par >> "isBicycle";
}

void MotorcycleType::InitShape()
{
    const ParamEntry& par = *_par;
    _scopeLevel = 2;
    base::InitShape();

    // wheel animations
    _frontWheel.Init(_shape, "pravy predni", "pravy predni tlumic", nullptr);
    _backWheel.Init(_shape, "pravy zadni", "pravy zadni tlumic", nullptr);
    _pedals.Init(_shape, "slapky", nullptr, "osa slapek");
    _pedalL.Init(_shape, "slapka L", nullptr, "osa slapky L");
    _pedalR.Init(_shape, "slapka P", nullptr, "osa slapky P");
    _frontWheelDamper.Init(_shape, "pravy predni tlumic", nullptr, nullptr);
    _backWheelDamper.Init(_shape, "pravy zadni tlumic", nullptr);
    _support.Init(_shape, "stojanek", nullptr, "osa stojanku");

    _drivingWheel.Init(_shape, "volant", nullptr, "osaVolantKon");

    // check front wheel center in memory level
    int mem = _shape->FindLandContactLevel();
    _steeringPoint = VZero;
    if (mem >= 0)
    {
        _steeringPoint = _frontWheel.Center(mem);
    }

    _plateInfos.Init(_shape, "spz", GetFontID("tahomaB48"), PackedColor(Color(0, 0, 0, 0.75)));

    _toWheelAxis.SetIdentity();

    Shape* memory = _shape->MemoryLevel();
    if (memory)
    {
        // driving wheel animation
        Vector3 wheelAxisBeg = memory->NamedPosition("osaVolantZac");
        Vector3 wheelAxisEnd = memory->NamedPosition("osaVolantKon");
        _drivingWheel.SetCenter(wheelAxisEnd);
        _toWheelAxis.SetDirectionAndUp(wheelAxisEnd - wheelAxisBeg, VUp);
    }

    int contactLevel = _shape->FindLandContactLevel();
    PoseidonAssert(contactLevel >= 0);
    Shape* contact = _shape->LandContactLevel();

    int i, w;
    for (i = 0; i < MaxMCWheels; i++)
    {
        _wheels[i] = nullptr;
    }

    _wheels[MCFWheel] = &_frontWheel;
    _wheels[MCBWheel] = &_backWheel;

    // prepare contact information
    _whichWheelContact.Init(contact->NPos()); // conversion from landcontact

    // prepare wheel index information
    for (i = 0; i < contact->NPos(); i++)
    {
        MotorcycleWheel wheel = MCNoWheel;
        for (w = 0; w < MaxMCWheels; w++)
        {
            int selIndex = _wheels[w]->GetSelection(contactLevel);
            if (selIndex < 0)
            {
                continue;
            }
            if (contact->NamedSel(selIndex).IsSelected(i))
            {
                wheel = (MotorcycleWheel)w;
            }
        }
        _whichWheelContact[i] = wheel;
    }

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

    DEF_HIT(_shape, _wheelFHit, "Pravy predni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

    DEF_HIT(_shape, _wheelBHit, "Pravy zadni tlumic", nullptr, GetArmor() * (float)(par >> "armorWheels"));

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
}

Motorcycle::Motorcycle(VehicleType* name, Person* driver)
    : base(name, driver),

      // pilot controls
      _thrustWanted(0), _thrust(0), _turnWanted(0), _turn(0), _turnIncreaseSpeed(1), _turnDecreaseSpeed(1),

      _reverseTimeLeft(0), _forwardTimeLeft(0),

      _support(0),

      // gearbox
      _wheelPhase(0),

      _track(_shape)

{
    SetSimulationPrecision(1.0 / 15);
    _rpm = 0, _rpmWanted = 0;
    float gearRatio = 80 / Type()->GetMaxSpeed();
    AutoArray<float> gears;
    gears.Add(0);
    gears.Add(1.0 / 8 * gearRatio);
    gears.Add(1.0 / 15 * gearRatio);
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
    for (i = 0; i < MaxMCWheels; i++)
    {
        _dampers[i] = 0;
    }

    _rightDust.SetSize(0.35);
    _rightDust.SetAlpha(0.25);

    _head.SetPars("Land");
    _head.Init(Type()->_pilotPos - Vector3(0, 0.2, 0), Type()->_pilotPos, this);
}

bool Motorcycle::IsAnimated(int level) const
{
    return true;
}
bool Motorcycle::IsAnimatedShadow(int level) const
{
    // a stopped (unsimulated) vehicle cannot change pose
    return !ShadowPoseFrozen();
}

void Motorcycle::AnimateSpeedIndicator(Matrix4& trans, int level)
{
    Matrix4 rotTrans(MRotationZ, _turn * 0.6f);
    Matrix4 turnTrans = Type()->_toWheelAxis * rotTrans * Type()->_toWheelAxis.InverseRotation();

    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Vector3 center = Type()->_drivingWheel.Center(level);
        trans = (Matrix4(MTranslation, center) * turnTrans * Matrix4(MTranslation, -center));
    }
    else
    {
        trans = MIdentity;
    }
}

void Motorcycle::AnimateMatrix(Matrix4& mat, int level, int selection) const
{
    if (HasTurret())
    {
        _turret.AnimateMatrix(Type()->_turret, mat, this, level, selection);
    }
}

Vector3 Motorcycle::AnimatePoint(int level, int index) const
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

inline float Motorcycle::GetGlassBroken() const
{
    const MotorcycleType* type = Type();
    float glassDammage = GetHitCont(type->_bodyHit);
    saturateMax(glassDammage, GetTotalDammage());
    saturateMax(glassDammage, GetHitCont(type->_glassLHit));
    saturateMax(glassDammage, GetHitCont(type->_glassRHit));
    return glassDammage;
}

void Motorcycle::DammageAnimation(int level)
{
    const MotorcycleType* type = Type();
    // scan corresponding wound

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

void Motorcycle::DammageDeanimation(int level)
{
    const MotorcycleType* type = Type();
    // scan corresponding wound
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

void Motorcycle::Animate(int level)
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
    if (_shape->Resolution(level) < 900)
    {
        // rotate wheels
        transform = Matrix4(MRotationX, _wheelPhase * (2 * H_PI));
    }
    // if wheel is dammaged and we animate geometry
    // we should make geometry a little bit higher
    // to simulate empty tire

    Matrix4 support(MRotationX, _support * H_PI / 2);
    Type()->_support.Apply(_shape, support, level);

    float dampers[MaxMCWheels];
    for (int i = 0; i < MaxMCWheels; i++)
    {
        dampers[i] = _dampers[i];
    }
    if (level == _shape->FindLandContactLevel() || level == _shape->FindGeometryLevel())
    {
        float dammageOffset = Type()->_wheelCircumference * 0.05;

        if (GetHit(Type()->_wheelBHit))
        {
            dampers[MCBWheel] += dammageOffset;
        }

        if (GetHit(Type()->_wheelFHit))
        {
            dampers[MCFWheel] += dammageOffset;
        }
    }

    Matrix4 rotTrans(MRotationZ, _turn * 0.6f);
    Matrix4 turnTrans = Type()->_toWheelAxis * rotTrans * Type()->_toWheelAxis.InverseRotation();

    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Type()->_drivingWheel.Apply(_shape, turnTrans, level);
    }
    softTrans.SetPosition(Vector3(0, dampers[MCFWheel], 0));
    Type()->_frontWheelDamper.Apply(_shape, softTrans * turnTrans, level);
    Type()->_frontWheel.Apply(_shape, softTrans * turnTrans * transform, level);
    softTrans.SetPosition(Vector3(0, dampers[MCBWheel], 0));
    Type()->_backWheelDamper.Transform(_shape, softTrans, level);
    Type()->_backWheel.Apply(_shape, softTrans * transform, level);

    if (Type()->_isBicycle)
    {
        float pedalPos = _wheelPhase * (2 * H_PI);

        if (_driver)
        {
            pedalPos = _driver->GetLegPhase() * (2 * H_PI);
        }

        Type()->_pedals.Rotate(_shape, pedalPos, level);

        Matrix4 pedalRot;
        Type()->_pedals.GetRotation(pedalRot, pedalPos, level);

        Matrix4 pedalL, pedalR;
        Type()->_pedalL.GetRotation(pedalL, -pedalPos, level);
        Type()->_pedalR.GetRotation(pedalR, -pedalPos, level);

        Type()->_pedalL.Transform(_shape, pedalRot * pedalL, level);
        Type()->_pedalR.Transform(_shape, pedalRot * pedalR, level);
    }

    DammageAnimation(level);

    base::Animate(level);
    // assume min-max box is not changed
    shape->RestoreMinMax();
}

void Motorcycle::Deanimate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }

    base::Deanimate(level);

    if (HasTurret())
    {
        _turret.Deanimate(Type()->_turret, _shape, level);
    }

    DammageDeanimation(level);

    for (int w = 0; w < MaxMCWheels; w++)
    {
        Type()->_wheels[w]->Restore(_shape, level);
    }
    if (Type()->_isBicycle)
    {
        Type()->_pedalL.Restore(_shape, level);
        Type()->_pedalR.Restore(_shape, level);
        Type()->_pedals.Restore(_shape, level);
    }
    Type()->_frontWheelDamper.Restore(_shape, level);
    Type()->_backWheelDamper.Restore(_shape, level);

    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Type()->_drivingWheel.Restore(_shape, level);
    }

    Type()->_support.Restore(_shape, level);
}

Vector3 Motorcycle::Friction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = speed[0] * fabs(speed[0]) * 10 + speed[0] * 20 + fSign(speed[0]) * 10;
    friction[1] = speed[1] * fabs(speed[1]) * 7 + speed[1] * 20 + fSign(speed[1]) * 10;
    friction[2] = speed[2] * fabs(speed[2]) * 5 + speed[2] * 20 + fSign(speed[2]) * 10;
    return friction * GetMass() * (1.0 / 4500);
}

void Motorcycle::MoveWeapons(float deltaT)
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
        {
            _turret._gunStabilized = true;
        }
        _turret.MoveWeapons(Type()->_turret, unit, deltaT);
    }
}

bool Motorcycle::IsBlocked() const
{
    return false;
}

void Motorcycle::PlaceOnSurface(Matrix4& trans)
{
    if (!GetShape())
    {
        return;
    }

    // place in steady position
    Vector3 pos = trans.Position();
    Matrix3 orient = trans.Orientation();

    float dx, dz;
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos, &dx, &dz);

    Matrix3 vertical, invVertical;
    vertical.SetUpAndDirection(VUp, orient.Direction());
    invVertical = vertical.InverseRotation();

    // front-back direction based on terrain normal
    Vector3Val landNormal = Vector3(-dx, 1, -dz).Normalized();

    // convert upWanted into vertical coordinate space
    Vector3 relUpWanted = invVertical * landNormal;
    relUpWanted[0] = 0;

    orient.SetUpAndDirection(vertical * relUpWanted, trans.Direction());
    trans.SetOrientation(orient);

    Shape* geom = _shape->LandContactLevel();
    if (!geom)
    {
        geom = _shape->GeometryLevel();
    }
    if (!geom)
    {
        geom = _shape->Level(0);
    }

    // dynamic vehicle
    if (geom)
    {
        Vector3 minC(0, geom->Min().Y(), 0);
        pos -= orient * minC;
    }

    trans.SetPosition(pos);
}

void Motorcycle::SimulateOneIter(float deltaT, SimulationImportance prec)
{
    _isUpsideDown = false;
    _isDead = IsDammageDestroyed();

    if (_isDead)
    {
        SmokeSourceVehicle* smoke = dyn_cast<SmokeSourceVehicle>(GetSmoke());
        if (smoke)
        {
            smoke->Explode();
        }
        NeverDestroy();
    }

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
        GetHit(Type()->_fuelHit) >= 0.9 || GetHit(Type()->_engineHit) >= 0.9)
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

        // calculate engine rpm
        _rpmWanted = speedSize * _gearBox.Ratio();
        saturateMax(_rpmWanted, fabs(_thrust) * 0.3 + 0.2);
    }
    else
    {
        _rpmWanted = 0;
    }

    float delta;

    if (!IsDammageDestroyed() && Driver())
    {
        float supportWanted = fabs(ModelSpeed().Z());
        saturate(supportWanted, 0, 1);

        delta = supportWanted - _support;
        Limit(delta, -deltaT, +deltaT);
        _support += delta;
    }

    delta = _rpmWanted - _rpm;
    Limit(delta, -0.5 * deltaT, +0.3 * deltaT);
    _rpm += delta;

    // calculate all forces, frictions and torques
    Vector3 force(VZero), friction(VZero);
    Vector3 torque(VZero), torqueFriction(VZero);

    Vector3 pForce(VZero);  // partial force
    Vector3 pCenter(VZero); // partial force application point

    // simulate left/right engine

    // main engine
    // if we go too slow we cannot turn at all
    // turn wheels
    float wheelLen = Type()->_wheelCircumference;
    _wheelPhase += speed.Z() * (1 / wheelLen) * deltaT;
    // while( _wheelPhase>=1 )
    _wheelPhase = fastFmod(_wheelPhase, 1);
    // while( _wheelPhase<0 ) _wheelPhase+=1;

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
    if (ModelSpeed()[2] * _thrustWanted < 0 && fabs(ModelSpeed()[2]) > 0.5)
    {
        _pilotBrake = true;
    }

    if (_pilotBrake && fabs(ModelSpeed()[2]) > 0.5)
    {
        _thrustWanted = 0;
    }

    if (fabs(_thrustWanted) > 0.1)
    {
        IsMoved();
    }

    {
        // handle impulse
        float impulse2 = _impulseForce.SquareSize();
        if (impulse2 > Square(GetMass() * 0.01))
        {
            IsMoved();
        }
        if (impulse2 > Square(GetMass() * 3))
        {
            // too strong impulse - dammage
            float contact = sqrt(impulse2) / (GetMass() * 10);
            // contact>0
            saturateMin(contact, 5);
            if (contact > 0.1)
            {
                float radius = GetRadius();
                LocalDammage(nullptr, this, VZero, contact * 0.1, radius * 0.3);
            }
        }
    }

    if (EnableVisualEffects(prec))
    {
        if (_mGunClouds.Active() || _mGunFire.Active())
        {
            Matrix4Val gunTransform = GunTurretTransform();
            Matrix4Val toWorld = Transform() * gunTransform;
            Vector3Val dir = toWorld.Direction();
            Vector3 gunPos(VFastTransform, toWorld, Type()->_turret._pos);
            _mGunClouds.Simulate(gunPos, Speed() * 0.7 + dir * 5.0, 0.35, deltaT);
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
            Limit(delta, -0.5 * deltaT, +0.5 * deltaT);
        }
        _thrust += delta;

        // do not allow fast reverse
        float minThrust = Interpolativ(ModelSpeed()[2], -5, 0, 0, -1);
        Limit(_thrust, minThrust, 1.0);

        float asz = fabs(speed.Z());
        float limitTurn = 2;
        if (asz > 1)
        {
            limitTurn /= asz;
        }
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

        float turnForward = speed.Z() * 0.7;
        saturate(turnForward, -25, +25); // avoid slips in high speed
        float turnWanted = _turn * turnForward * 0.4 * Type()->_turnCoef;

        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

        const float defSpeed = 50.0; // model tuned at this speed (plain level road)

        // apply left/right thrust
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

        float wheelFHit = GetHit(Type()->_wheelFHit);
        float wheelBHit = GetHit(Type()->_wheelBHit);
        float wheelHit = floatMax(wheelFHit, wheelBHit) * 0.9;

        if (!_engineOff || fabs(turnWanted) > 0.001)
        {
            if (_landContact || _objectContact)
            {
                float invSpeedSize;
                const float coefInvSpeed = 3;
                const float minInvSpeed = 8;
                float power = Type()->GetMaxSpeed() * (1 / defSpeed);
                if (power > 1)
                {
                    power = Square(power);
                }
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

                float thrust = _thrust * 4 * (1 - GetHit(Type()->_engineHit)) * (1 - wheelHit);
                float lAccel = (thrust - turnWanted) * invSpeedSize;
                float rAccel = (thrust + turnWanted) * invSpeedSize;

                pForce = Vector3(0, 0, lAccel * GetMass() * 0.5);
                force += pForce;
                pCenter = Vector3(+4, -0.5, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                if (CHECK_DIAG(DEForce))
                    AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                             Color(0, 0, 0));
#endif

                pForce = Vector3(0, 0, rAccel * GetMass() * 0.5);
                force += pForce;
                pCenter = Vector3(-4, -0.5, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                if (CHECK_DIAG(DEForce))
                    AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                             Color(0, 0, 0));
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

        // convert forces to world coordinates
        DirectionModelToWorld(torque, torque);
        DirectionModelToWorld(force, force);

        // apply gravity
        pForce = Vector3(0, -G_CONST * GetMass(), 0);
        force += pForce;

#if ARROWS
        if (CHECK_DIAG(DEForce))
            AddForce(wCenter, pForce * InvMass(), Color(1, 1, 0));
#endif

        // angular velocity causes also some angular friction
        // this should be simulated as torque
        if (!Driver())
        {
            torqueFriction = _angMomentum * 2;
        }
        else
        {
            torqueFriction = _angMomentum * 0.5;
        }

        // calculate new position
        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);
        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // body air friction
        DirectionModelToWorld(friction, Friction(speed));
#if ARROWS
        if (CHECK_DIAG(DEForce))
            AddForce(wCenter, friction * InvMass());
#endif

        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

        float soft = 0, dust = 0;
        if (deltaT > 0)
        {
            // check collision on new position
            float crash = 0;
            float sFactor = Type()->GetMaxSpeedMs() * 1.3;
            Vector3 fSpeed = speed - Vector3(0, 0, _thrust * sFactor);
            // avoid too fast accel/deccel
            float maxAcc = floatMin(10, Type()->GetMaxSpeedMs() * 0.14);
            saturate(fSpeed[0], -maxAcc, +maxAcc);
            saturate(fSpeed[1], -maxAcc, +maxAcc);
            saturate(fSpeed[2], -maxAcc * 0.6, +maxAcc);
            float brakeFriction = 0;

            saturateMax(brakeFriction, _pilotBrake);
            saturateMax(brakeFriction, DirectionUp().Y() <= 0.3);
            saturateMax(brakeFriction, wheelHit - 0.5);

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

                    if (cFactor < 0.05)
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
                            Vector3 move = dirOut * moveOut * 0.1;
                            newPos += move;
                            transform.SetPosition(newPos);
                            moveTrans.SetTransform(transform);
                        }
                        Vector3 colSpeed = Speed() - obj->ObjectSpeed();
                        // limit relative speed to object we crashed into
                        const float maxRelSpeed = 0.5;
                        if (colSpeed.SquareSize() > Square(maxRelSpeed))
                        {
                            // adapt _speed to match criterion
                            float crashSpeed = colSpeed.Size() - 2;
                            if (crashSpeed > 0)
                            {
                                crash += crashSpeed * 0.3;
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
                    pForce *= GetMass() * (4.0 / 10000) * frictionIn;
// saturateMin(pForce[1],0);
// torque-=pCenter.CrossProduct(pForce);
#if ARROWS
                    if (CHECK_DIAG(DEForce))
                        AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                    friction += pForce;
                    torqueFriction += _angMomentum * 0.15;
                }
            } // if( object collisions enabled )

            // simulate damper forces
            const float scaleDamper = 0.5;
            const float initDamper = -2 * scaleDamper * InvMaxDamper;
            const float adaptDamper = 7.5 * scaleDamper;
            // const float adaptDamper=15*scaleDamper;
            float damperForces[MaxMCWheels];
            int w;
            for (w = 0; w < MaxMCWheels; w++)
            {
                damperForces[w] = _dampers[w] * initDamper;
            }
            // check for collisions

            GroundCollisionBuffer gCollision;
            float softFactor = floatMin(1000 / GetMass(), 0.5);

            /*
            if( prec>=SimulateVisibleFar )
            {
                GLOB_LAND->GroundCollisionPlane(gCollision,this,moveTrans,0.05,softFactor);
            }
            else
            */
            {
                // force using landcontact - use "soldier=true"
                // check if motorcycle is already up-down
                bool forceLandContact = DirectionUp().Y() > 0.1f && !IsDammageDestroyed() && Driver();
                GLandscape->GroundCollision(gCollision, this, moveTrans, 0.05f, softFactor, true, forceLandContact);
            }

            _landContact = false;
            _objectContact = false;
            _waterContact = false;
            if (gCollision.Size() > 0)
            {
                if (!Driver())
                {
                    torqueFriction += _angMomentum * 2;
                }
#define MAX_UNDER 0.2
#define MAX_UNDER_FORCE 0.05

                Shape* landcontact = GetShape()->LandContactLevel();
                int nContactPoint = landcontact ? landcontact->NPos() : 2;
                // if there are more collision points than possible contact points,
                // number of contact points must be low
                // this may be due to object geometry used instead of landcontact
                saturateMax(nContactPoint, gCollision.Size());
                float contactCoef = 8.0f / nContactPoint;

                float maxUnder = 0;
                // scan all contact points
                for (int i = 0; i < gCollision.Size(); i++)
                {
                    // info.pos is world space
                    UndergroundInfo& info = gCollision[i];
                    // we consider two forces
                    if (info.under < 0)
                    {
                        continue;
                    }
                    float under;
                    if (info.type == GroundWater)
                    {
                        under = info.under * 0.002;
                        _waterContact = true;
                    }
                    else
                    {
                        _landContact = true;
                        if (maxUnder < info.under)
                        {
                            maxUnder = info.under;
                        }
                        under = floatMin(info.under, MAX_UNDER_FORCE);

                        if (info.level == _shape->FindLandContactLevel())
                        {
                            // check which damper is affected
                            PoseidonAssert(info.vertex >= 0);
                            PoseidonAssert(info.vertex < Type()->_whichWheelContact.Size());
                            MotorcycleWheel wheel = Type()->_whichWheelContact[info.vertex];
                            if (wheel < MaxMCWheels)
                            {
                                // some wheel affected
                                // simulate damper
                                damperForces[wheel] += info.under * adaptDamper;
                            }
                        }
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
                    pForce = dirOut * GetMass() * 80.0f * contactCoef * under;
                    pCenter = info.pos - wCenter;

                    // land should not cause any torque that would change heading and bank
                    // ie. force

                    //************************
                    if (Driver())
                    {
                        // make sure this torque does not effect lateral stability
                        torque += pCenter.CrossProduct(Vector3(0, pForce[1], pForce[2]));
                    }
                    else
                    {
                        torque += pCenter.CrossProduct(pForce);
                    }
                    landForce += pForce;

#if ARROWS
                    if (CHECK_DIAG(DEForce))
                        AddForce(wCenter + pCenter, pForce * under * InvMass(), Color(0, 1, 0));
#endif

                    // second is "land friction" - causing little momentum
                    pForce[0] = fSpeed[0] * 6000 + fSign(fSpeed[0]) * 180000;
                    pForce[1] = fSpeed[1] * 8000 + fSign(fSpeed[1]) * 10000;
                    pForce[2] = fSpeed[2] * 200 + fSign(fSpeed[2]) * 30000;
                    if (brakeFriction < 0.7)
                    {
                        pForce[2] *= 0.1;
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

                    friction += pForce;

                    torqueFriction += _angMomentum * 0.5 * contactCoef;
                    //**************************************
                    // torqueFriction+=_angMomentum*1.5*contactCoef;

                    float landMoved = info.under;
                    saturateMin(landMoved, 0.1);
                    pForce[0] = speed[0] * 4500;
                    pForce[1] = 0;
                    pForce[2] = speed[2] * 2000;
                    pForce = DirectionModelToWorld(pForce) *
                             (GetMass() * (1.0 / 1000) * contactCoef * landMoved * soft * Type()->_terrainCoef);
#if ARROWS
                    if (CHECK_DIAG(DEForce))
                        AddForce(wCenter + pCenter, -pForce * InvMass(), Color(0, 1, 1));
#endif
                    friction += pForce;
                }
                // stabilize (advanced approach would be to simulate gyroscopis forces)
                // but we think simple approach should be good enough

                float minDirY = Driver() ? 0.1 : 0.8;
                // if there is no driver, stabilization cen be done only by support
                if (DirectionUp().Y() > minDirY && !IsDammageDestroyed())
                {
                    if (Driver() || _support < 0.1)
                    {
                        float dx, dz;
                        GLandscape->RoadSurfaceY(moveTrans.Position(), &dx, &dz);

                        float turnA = _turn;
                        saturate(turnA, -0.15f, +0.15f);
                        float asideWanted = turnA * 0.2f * fabs(ModelSpeed().Z());

                        saturate(asideWanted, -0.86f, +0.86f);

#if 1
                        // predict direction up in some time
                        float dirEstT = 0.1f;
                        const Matrix3& orientation = moveTrans.Orientation();
                        Matrix3 derOrientation = _angVelocity.Tilda() * orientation;
                        Matrix3Val estOrientation = orientation + derOrientation * dirEstT;

                        Vector3Val estDirectionUp = estOrientation.DirectionUp().Normalized();
#else
                        Vector3Val estDirectionUp = moveTrans.DirectionUp();
#endif

                        // but with up direction vertical
                        Matrix3 orient = moveTrans.Orientation();
                        Matrix3 vertical, invVertical;
                        vertical.SetUpAndDirection(VUp, orient.Direction());
                        invVertical = vertical.InverseRotation();

                        // front-back direction based on terrain normal
                        Vector3Val landNormal = Vector3(-dx, 1, -dz).Normalized();

                        const float maxCosA = 0.17; // cos 80
                        if (landNormal * DirectionUp() < maxCosA)
                        {
                            EjectAllNotFixed();
                        }
                        // convert upWanted into vertical coordinate space
                        Vector3 upDirWanted = landNormal;
                        Vector3 relUpWanted = invVertical * upDirWanted;
                        relUpWanted[0] = asideWanted;

                        relUpWanted.Normalize();

                        upDirWanted = vertical * relUpWanted;

                        Vector3 stabilize = upDirWanted - estDirectionUp;
                        // stabilize[1]=0;
                        const float maxStabForce = 0.8f;
                        if (stabilize.SquareSize() > Square(maxStabForce))
                        {
                            stabilize = stabilize.Normalized() * maxStabForce;
                        }

                        pForce = stabilize * 30 * GetMass();
                        /**/
                        DirectionWorldToModel(pForce, pForce);
                        pForce[2] = 0;
                        DirectionModelToWorld(pForce, pForce);
                        /**/

                        pCenter = DirectionUp();
#if ARROWS
                        if (CHECK_DIAG(DEForce))
                            AddForce(pCenter + wCenter, pForce * InvMass(), Color(1, 0, 1));
#endif
                        torque += pCenter.CrossProduct(pForce);

                        /*
                        pForce[0] = -GetMass()*asideWanted*20;
                        pForce[1] = 0 ;
                        pForce[2] = 0 ;
                        DirectionModelToWorld(pForce,pForce);
                        #if ARROWS
                            if (CHECK_DIAG(DEForce)) AddForce
                            (
                                pCenter+wCenter,pForce*InvMass(),Color(0,0,1)
                            );
                        #endif
                        torque += pCenter.CrossProduct(pForce);
                        */

#if ARROWS
                        if (CHECK_DIAG(DEForce))
                            AddForce(pCenter + wCenter + VUp * 2, upDirWanted * 3, Color(0.2, 0.2, 0.2));
                        if (CHECK_DIAG(DEForce))
                            AddForce(pCenter + wCenter + VUp * 2, DirectionUp() * 4, Color(0.4, 0.0, 0.0));
                        if (CHECK_DIAG(DEForce))
                            AddForce(pCenter + wCenter + VUp * 2, estDirectionUp * 5, Color(0.5, 0.5, 0.0));
#endif
                    }
                }
                else
                {
                    EjectAllNotFixed();
                }

                // torqueFriction=_angMomentum*1.0;
                if (_waterContact)
                {
                    const SurfaceInfo& info = GLandscape->GetWaterSurface();
                    soft = info._roughness * 2.5;
                    dust = info._dustness * 2.5;
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
                    const float crashLimit = 0.3;
                    if (moveUp > crashLimit)
                    {
                        crash += moveUp - crashLimit;
                        saturateMax(maxCFactor, 1);
                    }
                    float potentialGain = moveUp * GetMass();
                    float oldKinetic = GetMass() * _speed.SquareSize() * 0.5; // E=0.5*m*v^2
                    // kinetic to potential conversion is not 100% effective
                    float crashFactor = (moveUp - crashLimit) * 4 + 1.5;
                    saturateMax(crashFactor, 2.5);
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

            for (w = 0; w < MaxMCWheels; w++)
            {
                _dampers[w] += damperForces[w] * deltaT;
                saturate(_dampers[w], -MaxDamper, +MaxDamper);
            }
            force += objForce;
            force += landForce;

            float crashTreshold = 10 * GetMass(); // 2G
            float forceCrash = 0;
            if (objForce.SquareSize() > Square(crashTreshold))
            {
                // crash as g-term
                forceCrash = (objForce.Size() - crashTreshold) * InvMass() * (1.0 / 10);
                crash += forceCrash;
            }
            if (crash > 0.1)
            {
                float speedCrash = (maxColSpeed2 - 3) * Square(1.0 / 7);
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
                    _crashVolume = crash * 0.2;
                    saturateMin(crash, speedCrash);
                    crash *= floatMin(1, maxCFactor);
                    CrashDammage(crash); // 1g -> 5 mm dammage
                    DammageCrew(Driver(), crash * 0.03, "");
                    /*
                    LOG_DEBUG(Physics,
                        "Crash %g, speed %g, factor %g",
                        crash,sqrt(maxColSpeed2),maxCFactor
                    );
                    */
                }
            }
        }

        // apply all forces
        /*
        LOG_DEBUG(Physics,
            "torq %.3f,%.3f,%.3f, torqFric %.3f,%.3f,%.3f, angM %.3f,%.3f,%.3f",
            torque[0],torque[1],torque[2],
            torqueFriction[0],torqueFriction[1],torqueFriction[2],
            _angMomentum[0],_angMomentum[1],_angMomentum[2]
        );
        */
        ApplyForces(deltaT, force, torque, friction, torqueFriction);

        bool stopCondition = false;
        if (_pilotBrake && _landContact && !_waterContact && !_objectContact)
        {
            // apply static friction
            float maxSpeed = Square(0.7);
            if (!Driver())
            {
                maxSpeed = Square(1.2);
            }
            if (_speed.SquareSize() < maxSpeed && _angVelocity.SquareSize() < maxSpeed * 0.3)
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

        // simulate pilot's head movement
        if (prec <= SimulateCamera)
        {
            _head.Move(deltaT, moveTrans, *this);
        }

        // simulate track drawing
        if (EnableVisualEffects(prec))
        {
            if (DirectionUp().Y() >= 0.3)
            {
                _track.Update(*this, deltaT, !_landContact);
            }
            if (_landContact)
            {
                Vector3 lcPos = _track.RightPos(); // consider only back pos
                Vector3 lPos = PositionModelToWorld(lcPos + Vector3(0, 0.1, 0));
                float dSoft = floatMax(dust, 0.0025);
                float density = speedSize * (1.0 / 10) * dSoft;
                saturate(density, 0, 1.0);
                float dustColor = dSoft * 8;
                saturate(dustColor, 0, 1);
                Color color = Color(0.51, 0.46, 0.33) * dustColor + Color(0.5, 0.5, 0.5) * (1 - dustColor);
                // color.SetA(0.3);
                _rightDust.SetColor(color);
                _rightDust.Simulate(lPos + _speed * 0.2, _speed * 0.5, density, deltaT);
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
        _pilotLight = false;
    }
}

bool Motorcycle::IsStopped() const
{
    /*
    if (!base::IsStopped()) return false;
    if (!Driver()) return true;
    return _support<0.05f;
    */
    return base::IsStopped();
}

void Motorcycle::Simulate(float deltaT, SimulationImportance prec)
{
    if (prec <= SimulateVisibleNear && !GetStopped())
    {
        const float maxStep = 1.0f / 30;
        while (deltaT > maxStep)
        {
            SimulateOneIter(maxStep, prec);
            if (ToDelete())
            {
                return;
            }
            deltaT -= maxStep;
        }
    }
    SimulateOneIter(deltaT, prec);
}

void Motorcycle::Eject(AIUnit* unit)
{
    base::Eject(unit);
}

void Motorcycle::FakePilot(float deltaT)
{
    _turnIncreaseSpeed = 1;
    _turnDecreaseSpeed = 1;
}

void Motorcycle::JoystickPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _thrustWanted = input.GetStickForward();
    _turnWanted = -input.GetStickLeft();

    _turnIncreaseSpeed = 2;
    _turnDecreaseSpeed = 2;

    Limit(_thrustWanted, -1, 1);
    Limit(_turnWanted, -1, 1);
    if (fabs(_thrustWanted) > 0.05)
    {
        CancelStop();
    }
    if (fabs(_turnWanted) > 0.05)
    {
        CancelStop();
    }
    _pilotBrake = fabs(_thrustWanted) < 0.2 && fabs(_modelSpeed.Z()) < 4.0;
}

void Motorcycle::SuspendedPilot(AIUnit* unit, float deltaT)
{
    _pilotBrake = true;
    _thrustWanted = 0;
    _turnWanted = 0;
}

void Motorcycle::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    if (input.IsJoystickActive())
    {
        CancelStop();
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
// predict position
#if 1
        // ignore bank
        float estT = 0.3;
        Matrix3Val orientation = Orientation();
        Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
        Matrix3 estOrientation = orientation + derOrientation * estT;

        Matrix3 orientVert;
        orientVert.SetUpAndDirection(VUp, estOrientation.Direction());

        // Vector3Val estDirection=estOrientation.Direction().Normalized();
        Vector3 relDir(VMultiply, orientVert.InverseRotation(), _mouseDirWanted);

#else
        Matrix3 orientVert;
        orientVert.SetUpAndDirection(VUp, Direction());
        Vector3 relDir(VMultiply, orientVert.InverseRotation(), _mouseDirWanted);
#endif
        float turn = atan2(relDir.X(), relDir.Z()) * 0.3f;
        _turnWanted = turn * (fabs(turn) * 8 + 0.33f);

        _turnIncreaseSpeed = 2;
        _turnDecreaseSpeed = 2;

        float maxTurnCoef = 1 - asz * (1.0f / 20);
        saturateMax(maxTurnCoef, 0);

        // limit max turn based on speed
        float maxTurn = maxTurnCoef * 1.0f + (1 - maxTurnCoef) * 0.1f;
        saturate(_turnWanted, -maxTurn, +maxTurn);
    }
    else
    {
        _turnWanted = input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft);
        _mouseDirWanted = Direction();

        float slowTurn = 1 - asz * 0.03f;
        float maxTurnCoef = 1 - asz * 0.03f;
        saturateMax(slowTurn, 0);
        saturateMax(maxTurnCoef, 0);
        _turnIncreaseSpeed = slowTurn * 0.4f + 0.05f;
        _turnDecreaseSpeed = slowTurn * 0.8f + 0.1f;

        // limit max turn based on speed
        float maxTurn = maxTurnCoef * 1.0f + (1 - maxTurnCoef) * 0.03f;
        saturate(_turnWanted, -maxTurn, +maxTurn);

        // limit max thrust based on turn
    }

    if (fabs(_thrustWanted) > 0.05)
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

RString Motorcycle::DiagText() const
{
    char buf[256];
    snprintf(buf, sizeof(buf), " %s %.2f,(%.2f)", _pilotBrake ? "B" : "E", _thrustWanted, _turnWanted);
    sprintf(buf + strlen(buf), " RF %.1f,%.1f", _reverseTimeLeft, _forwardTimeLeft);
    /*
    bool onroad=GRoadNet->IsOnRoad(Position(),CollisionSize()) != nullptr;
    sprintf
    (
        buf+strlen(buf)," %s",
        onroad ? "OnRoad" : "OffRoad"
    );
    */
    return base::DiagText() + (RString)buf;
}

#define DIAG_SPEED 0

void Motorcycle::AIPilot(AIUnit* unit, float deltaT)
{
    if (unit)
    {
        SelectFireWeapon();
    }

    _turnIncreaseSpeed = 1;
    _turnDecreaseSpeed = 1;

    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());
    bool isLeader = unit->IsSubgroupLeader();

    // move to given point
    // check goto/fire at command status

    Vector3Val speed = ModelSpeed();

    float headChange = 0;
    float speedWanted = 0;
    float turnPredict = 0;

    if (unit->GetState() == AIUnit::Stopping)
    {
        // special handling of stop state
        if (fabs(speed[2]) < 0.1 && _support < 0.05f)
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
    float estT = 0.2;
    Matrix3Val orientation = Orientation();
    Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation * estT;
    Vector3Val estDirection = estOrientation.Direction();
    float estHeading = atan2(estDirection[0], estDirection[2]);

    headChange = AngleDifference(wantedHeading, estHeading);

    {
        float aTP = fabs(turnPredict);
        if (aTP > H_PI / 64)
        {
            // limit speed only when turning significantly
            float maxSpeed = GetType()->GetMaxSpeedMs();
            // even with very slow or very fast car use some normal brakes
            // note: actualy in-turn speed is not much dependent on max speed
            // but we can assume faster vehicles have better turning radius
            saturate(maxSpeed, 50 / 3.6, 200 / 3.6);
            float limitSpeed = Interpolativ(aTP, H_PI / 64, H_PI / 4, maxSpeed, 5);

#if _ENABLE_CHEATS
            if (CHECK_DIAG(DEPath) && this == GWorld->CameraOn())
            {
                GlobalShowMessage(200, "Turn limit %.1f (%.3f, turn %.3f)", limitSpeed * 3.6, headChange, turnPredict);
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

    // some thrust is needed to keep speed
    float isSlow = 1 - fabs(speed.Z()) * (1.0 / 17);
    float maxTurn = 1 - fabs(speed.Z()) * (1.0 / 60);
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

    _turnWanted = headChange * 0.5f;
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

float Motorcycle::GetFieldCost(const GeographyInfo& info) const
{
    // road fields are expected to be faster
    // fields with objects will be passed through slower
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

float Motorcycle::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost() * RoadFaster;
    // avoid any water
    if (geogr.u.waterDepth > 0 && !(geogr.u.road || geogr.u.track))
    {
        return 1e30; // no movement in water
    }
    // avoid forests
    if (geogr.u.full)
    {
        return 1e30;
    }
    // penalty for objects
    int nObj = geogr.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    cost *= ObjPenalty1[nObj];
    // avoid steep hills
    // penalty for hills
    int grad = geogr.u.gradient;
    if (grad >= 6)
    {
        return 1e30;
    }
    // static const float gradPenalty[6]={1.0,1.05,1.1,1.5,2.0,3.0};
    static const float gradPenalty[6] = {1.0, 1.02, 1.05, 1.1, 2.0, 3.0};
    cost *= gradPenalty[grad];
    return cost;
}

float Motorcycle::GetCostTurn(int difDir) const
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

float Motorcycle::GetPathCost(const GeographyInfo& geogr, float dist) const
{
    // cost based only on distance
    float cost = Type()->GetMinCost();
    // avoid any water
    // penalty for objects
    int nObj = geogr.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    cost *= ObjRoadPenalty2[nObj];

    return cost * dist;
}

void Motorcycle::FillPathCost(Path& path) const
{
    base::FillPathCost(path);
}

bool Motorcycle::FireWeapon(int weapon, TargetType* target)
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

void Motorcycle::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
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

Vector3 Motorcycle::GetCameraDirection(CameraType camType) const
{
    if (!QIsManual())
    {
        return Direction();
    }
    // in world coordinates
    // aside - based on _turnWanted
    if (GWorld->LookAroundEnabled())
    {
        return Direction();
    }
    Matrix3 rotY(MRotationY, -_turn * 0.5f);
    return rotY * Direction();
}

void Motorcycle::LimitCursor(CameraType camType, Vector3& dir) const {}

bool Motorcycle::AimWeapon(int weapon, Vector3Par direction)
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
    // move turret/gun accordingly to direction
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);
    // calculate current gun direction
    // compensate for neutral gun position

    if (_turret.Aim(Type()->_turret, relDir))
    {
        CancelStop();
    }
    return true;
}

bool Motorcycle::AimWeapon(int weapon, Target* target)
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
    // const WeaponInfo &info=GetWeapon(weapon);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    Vector3 weaponPos = Type()->_turret._pos;
    Vector3 tgtPos = target->AimingPosition();
    // predict his and my movement
    float dist2 = tgtPos.Distance2(Position());
    float time2 = dist2 * Square(aInfo->_invInitSpeed * 1.2);

    // const float predTime=0.25;
    float time = sqrt(time2);
    const float minPredTime = 0.25;
    float predTime = floatMax(time + 0.1, minPredTime);
    Vector3 myPos = PositionModelToWorld(weaponPos);
    // tgtPos+=target->ObjectSpeed()*predTime;
    myPos += Speed() * minPredTime;
    float fall = 0.5 * G_CONST * time2;
    // calculate balistics
    tgtPos[1] += fall; // consider balistics
    if (aInfo)
    {
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

Vector3 Motorcycle::GetWeaponDirection(int weapon) const
{
    if (!HasTurret())
    {
        return Direction();
    }

    Vector3 dir = Type()->_turret._dir;
    return Transform().Rotate(GunTurretTransform().Rotate(dir));
}

Vector3 Motorcycle::GetWeaponCenter(int weapon) const
{
    if (!HasTurret())
    {
        return VZero;
    }

    return _turret.GetCenter(Type()->_turret);
}

float Motorcycle::DriverAnimSpeed() const
{
    if (Type()->_isBicycle)
    {
        return floatMax(_thrust, 0);
    }
    return 1;
}
float Motorcycle::CommanderAnimSpeed() const
{
    return 1;
}
float Motorcycle::GunnerAnimSpeed() const
{
    return 1;
}
float Motorcycle::CargoAnimSpeed(int position) const
{
    return 1;
}

float Motorcycle::GetEngineVol(float& freq) const
{
    freq = (_randomizer * 0.05 + 0.95) * _rpm * 1.2;
    if (Type()->_isBicycle)
    {
        return floatMax(_thrust, 0);
    }
    else
    {
        return fabs(_thrust) * 0.5 + 0.5;
    }
}

float Motorcycle::GetEnvironVol(float& freq) const
{
    freq = 1;
    return _speed.SquareSize() / Square(Type()->GetMaxSpeedMs());
}

bool Motorcycle::IsPossibleToGetIn() const
{
    float wheelHit = floatMax(GetHit(Type()->_wheelFHit), GetHit(Type()->_wheelBHit));
    if (wheelHit >= 0.9)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9)
    {
        return false;
    }
    return base::IsPossibleToGetIn();
}

bool Motorcycle::IsAbleToMove() const
{
    float wheelHit = floatMax(GetHit(Type()->_wheelFHit), GetHit(Type()->_wheelBHit));
    if (wheelHit >= 0.9)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9)
    {
        return false;
    }
    return base::IsAbleToMove();
}

bool Motorcycle::IsCautious() const
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return false;
    }
    CombatMode mode = unit->GetCombatMode();
    return mode == CMStealth || mode == CMCombat; // in AWARE state - lights is on and move on road
}

void Motorcycle::Sound(bool inside, float deltaT)
{
    if (HasTurret())
    {
        _turret.Sound(Type()->_turret, inside, deltaT, *this, Speed());
    }
    if (_gearSound)
    {
        float gearVol = Type()->_gearSound.vol;
        _gearSound->SetVolume(gearVol); // volume, frequency
        _gearSound->SetPosition(Position(), Speed());
        _gearSound->Set3D(!inside);
    }
    if (_hornSound)
    {
        _hornSound->SetPosition(Position(), Speed());
        _hornSound->Set3D(!inside);
    }

    base::Sound(inside, deltaT);
}

void Motorcycle::UnloadSound()
{
    base::UnloadSound();
    if (HasTurret())
    {
        _turret.UnloadSound();
    }
}

Matrix4 Motorcycle::InsideCamera(CameraType camType) const
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
    if (!GWorld->LookAroundEnabled())
    {
        Matrix3 rotY(MRotationY, -_turn * 0.5f);
        dir = rotY.Direction();
    }
    transf.SetDirectionAndUp(dir, transf.DirectionUp());

    return transf;
}

Vector3 Motorcycle::ExternalCameraPosition(CameraType camType) const
{
    return Type()->_extCameraPosition;
}

int Motorcycle::InsideLOD(CameraType camType) const
{
    int level = -1;
    if (level < 0)
    {
        level = GetShape()->FindLevel(VIEW_PILOT);
    }
    return level;
}

bool Motorcycle::HasFlares(CameraType camType) const
{
    if (camType == CamGunner || camType == CamInternal)
    {
        return false;
    }
    return base::HasFlares(camType);
}

Matrix4 Motorcycle::TurretTransform() const
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

Matrix4 Motorcycle::GunTurretTransform() const
{
    if (HasTurret())
    {
        // animate matrix connected with selection Type()->_mainTurret._gun
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

void Motorcycle::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);

    Type()->_plateInfos.Draw(level, clipFlags, pos, GetPlateNumber());
}

void Motorcycle::DrawProxies(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                             float dist2, float z2, const LightList& lights)
{
    base::DrawProxies(level, clipFlags, transform, invTransform, dist2, z2, lights);
}

void Motorcycle::SimulateHUD(CameraType camType, float deltaT)
{
    if (QIsManual() && IsGunner(camType))
    {
    }
}

RString Motorcycle::GetActionName(const UIAction& action)
{
    return base::GetActionName(action);
}

void Motorcycle::PerformAction(const UIAction& action, AIUnit* unit)
{
    base::PerformAction(action, unit);
}

void Motorcycle::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    base::GetActions(actions, unit, now);
}

bool Motorcycle::IsContinuous(CameraType camType) const
{
    return false;
}

bool Motorcycle::IsGunner(CameraType camType) const
{
    return camType == CamGunner || camType == CamInternal || camType == CamExternal;
}

LSError Motorcycle::Serialize(ParamArchive& ar)
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

NetworkMessageType Motorcycle::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateMotorcycle;
        case NMCUpdatePosition:
            return NMTUpdatePositionMotorcycle;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_MOTORCYCLE_MSG(XX) \
	XX(RString, plateNumber, NDTString, NCTNone, DEFVALUE(RString, "XXXXXXXX"), DOC_MSG("Plate number"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
	XX(float, thrustWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted thrust"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \
	XX(float, turnWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted turning angle"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateMotorcycle, UpdateTankOrCar, UPDATE_MOTORCYCLE_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateMotorcycle, UpdateTankOrCar, UPDATE_MOTORCYCLE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateMotorcycle)

namespace Poseidon
{

#define UPDATE_POSITION_MOTORCYCLE_MSG(XX) \
	XX(Turret, turret, NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret), DOC_MSG("Turret object"), IdxTransferObject, ET_ABS_DIF, 1)

DECLARE_NET_INDICES_EX_ERR(UpdatePositionMotorcycle, UpdatePositionVehicle, UPDATE_POSITION_MOTORCYCLE_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionMotorcycle, UpdatePositionVehicle, UPDATE_POSITION_MOTORCYCLE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdatePositionMotorcycle)

namespace Poseidon
{

NetworkMessageFormat& Motorcycle::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_MOTORCYCLE_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            UPDATE_POSITION_MOTORCYCLE_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Motorcycle::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateMotorcycle*>(ctx.GetIndices()))
                    const IndicesUpdateMotorcycle* indices =
                        static_cast<const IndicesUpdateMotorcycle*>(ctx.GetIndices());

                ITRANSF(plateNumber)
                ITRANSF(thrustWanted)
                ITRANSF(turnWanted)
            }
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionMotorcycle*>(ctx.GetIndices()))
                const IndicesUpdatePositionMotorcycle* indices =
                    static_cast<const IndicesUpdatePositionMotorcycle*>(ctx.GetIndices());

            Matrix3 oldTrans = Orientation();
            TMCHECK(base::TransferMsg(ctx))
            if (ctx.IsSending() || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->turret, _turret))
            _turret.Stabilize(this, Type()->_turret, oldTrans, Orientation());
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float Motorcycle::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateMotorcycle*>(ctx.GetIndices()))
                    const IndicesUpdateMotorcycle* indices =
                        static_cast<const IndicesUpdateMotorcycle*>(ctx.GetIndices());

                ICALCERR_NEQSTR(plateNumber, ERR_COEF_VALUE_MAJOR)
                ICALCERR_ABSDIF(float, thrustWanted, ERR_COEF_VALUE_MAJOR)
                ICALCERR_ABSDIF(float, turnWanted, ERR_COEF_VALUE_MAJOR)
            }
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionMotorcycle*>(ctx.GetIndices()))
                const IndicesUpdatePositionMotorcycle* indices =
                    static_cast<const IndicesUpdatePositionMotorcycle*>(ctx.GetIndices());

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
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
