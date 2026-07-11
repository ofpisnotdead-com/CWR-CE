
#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/AI/AI.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/platform.hpp>

#if _ENABLE_CHEATS
#define ARROWS 1
#else
#define ARROWS 0
#endif

#include <Poseidon/World/Scene/Camera/Camera.hpp>

namespace Poseidon
{
SeaGull::SeaGull()
    : base(nullptr, VehicleTypes.New("SeaGull"), -1), _mainRotor(1.5), _mainRotorWanted(1.5),

      _rpm(1), _rpmWanted(1), // landing control

      _cyclicForwardWanted(0), _cyclicAsideWanted(0), _wingDive(0), _wingDiveWanted(0),

      _thrust(0), _thrustWanted(0),

      _nextCreek(Glob.time + 10),

      _landContact(false),

      _wingPhase(0), _wingSpeed(0.5), _wingBase(0)
{
    // sea-gull is quite resistant to slow simulation
    SetSimulationPrecision(0.2);
    GetValue(_soundPars, Pars >> "CfgSFX" >> "seagull");
}

SeaGull::~SeaGull() = default;

void SeaGull::Sound(bool inside, float deltaT)
{
    if (_nextCreek < Glob.time)
    {
        _nextCreek = Glob.time + GRandGen.RandomValue() * 30 + 3;

        if (IsLocal())
        {
            bool doSound = false;
            if (!_sound)
            { // load sound if necessary
                _sound = GSoundScene->Open(_soundPars.name);
                doSound = true;
            }
            float volume = _soundPars.vol * 0.5;
            float freq = _soundPars.freq;
            if (_sound)
            {
                _sound->SetPosition(Position(), Speed());
                _sound->SetVolume(volume, freq);
                _sound->Repeat(1);
                if (_sound->IsTerminated())
                {
                    _sound->Restart();
                    doSound = true;
                }
            }
            if (doSound)
            {
                GetNetworkManager().PlaySound(_soundPars.name, Position(), Speed(), volume, freq, _sound);
            }
        }
    }
}

void SeaGull::UnloadSound()
{
    _sound.Free();
}

Vector3 SeaGull::ExternalCameraPosition(CameraType camType) const
{
    return Vector3(0, 0.3, -3);
}

const float MassCorrect = 1.0;
const float PosCorrect = 0.2 / 7.0;
const float SpeedCorrect = 1 / 7.5;
const float InvSpeedCorrect = 1.0 / SpeedCorrect;

static Vector3 BodyFriction(Vector3Val oSpeed)
{
    Vector3 friction;
    friction.Init();
    Vector3 speed = oSpeed * InvSpeedCorrect;
    friction[0] = speed[0] * fabs(speed[0]) * 25 + speed[0] * 1500 + fSign(speed[0]) * 40;
    friction[1] = speed[1] * fabs(speed[1]) * 12 + speed[1] * 700 + fSign(speed[1]) * 20;
    friction[2] = speed[2] * fabs(speed[2]) * 2.5 + speed[2] * 100 + fSign(speed[2]) * 6;
    return friction * MassCorrect;
}

static Vector3 WingUpForce(Vector3Val oSpeed, float coef, float wingDive)
{
    // optimized calculation

    float angle = -wingDive;
    // assume wingDive is small (-0.4 ... +0.4)
    // sin wingDive =.= wingDive (sin 0.4=0.39)
    // cos wingDive =.= 1-wingDive^2/2

    float s = angle, c = 1 - angle * angle * 0.5;
    Matrix3 wingRot;
    wingRot.SetIdentity();
    wingRot(1, 1) = +c, wingRot(1, 2) = -s;
    wingRot(2, 1) = +s, wingRot(2, 2) = +c;

    Matrix3 wingRotInv;
    wingRotInv.SetIdentity();
    wingRotInv(1, 1) = +c, wingRotInv(1, 2) = +s;
    wingRotInv(2, 1) = -s, wingRotInv(2, 2) = +c;

    Vector3 speed = wingRotInv * (0.04 * oSpeed);
    float forceUp = 0;

    float zSpeed = fabs(oSpeed[2]) - 2;
    float spd1 = speed[1] + 6 - 36 * coef;
    forceUp += spd1 * fabs(spd1) * -50 + spd1 * -5000;
    forceUp += zSpeed * 20000;
    Vector3 ret = wingRot.DirectionUp() * (forceUp * MassCorrect * 1.2);

    return ret;
}

#define FAST_COEF (1.0 / (20 * SpeedCorrect)) // use fast/slow simulation mode

void SeaGull::Simulate(float deltaT, SimulationImportance prec)
{
    // Vector3Val position=Position();

    // calculate all forces, frictions and torques
    Vector3Val speed = ModelSpeed();
    Vector3 force(VZero), friction(VZero);
    Vector3 torque(VZero), torqueFriction(VZero);

    // world space center of mass
    Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

    Vector3 pForce(VZero);  // partial force
    Vector3 pCenter(VZero); // partial force application point

    float delta;

    float oldRpm = _rpm;

    float rpmWanted = _rpmWanted;
    if (!_landContact && !GetManual())
    {
        rpmWanted = 1;
    }
    delta = rpmWanted - _rpm;
    Limit(delta, -2 * deltaT, +2 * deltaT);
    _rpm += delta;
    Limit(_rpm, 0, 1);

    if (_rpm < 0.9)
    {
        _wingBase = 1, _wingPhase = (1 - _rpm) * 0.5;
        _cyclicForwardWanted = 0;
        _cyclicAsideWanted = 0;
        _wingDiveWanted = 0;
        _mainRotorWanted = 0;
        _wingDiveWanted = 0;
        _thrustWanted = 0;
    }
    else
    {
        if (oldRpm < 0.9)
        {
            QuickStart();
        }
        const float wingFree = 0.97;
        float wingSpeed = _mainRotor * 2.5;
        if (_wingPhase < wingFree)
        {
            saturateMax(wingSpeed, 0.5);
        }
        _wingPhase += _wingSpeed * wingSpeed * deltaT;
        if (_wingPhase >= 1.0)
        {
            float randomize = GRandGen.RandomValue() * 0.2 + 0.9;
            _wingSpeed = randomize;
            _wingPhase = fastFmod(_wingPhase, 1.0);
        }
        _wingBase = 0;
    }

    // main rotor thrust
    // change main rotor force
    delta = _mainRotorWanted - _mainRotor;
    Limit(delta, -2 * deltaT, +2 * deltaT);
    _mainRotor += delta;
    Limit(_mainRotor, 0, 2);

    delta = _thrustWanted - _thrust;
    Limit(delta, -1.5 * deltaT, +1.5 * deltaT);
    _thrust += delta;
    Limit(_thrust, -1, 1);

    delta = _wingDiveWanted - _wingDive;
    Limit(delta, -4 * deltaT, 4 * deltaT);
    _wingDive += delta;
    Limit(_wingDive, -1, 1);

    // force applied to the center of the rotor
    // it is forced to be aligned with center of mass
    // Vector3 sideOffset(VZero);
    pCenter[0] = _cyclicAsideWanted * 0.8 * PosCorrect;
    pCenter[2] = _cyclicForwardWanted * -0.8 * PosCorrect;
    pCenter[1] = 0; // note: this line was missing - causing QNAN in torque
    // sideOffset[1]=_rotorH.Center()[1]-CenterOfMass()[1];
    //  apply aerodynamics of the main rotor

    pForce = GetMass() * (1.0 / 30000) * WingUpForce(speed, _mainRotor, _wingDive);
#if ARROWS
    AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce) * InvMass());
#endif
    // add simple forward/backward acceleration (1/4 G)

    pForce += VForward * (GetMass() * _thrust * 2.5);
    force += pForce;

    pForce = Vector3(0, G_CONST * 0.3 * GetMass(), 0);
#if ARROWS
    AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce) * InvMass());
#endif
    torque += pCenter.CrossProduct(pForce);

    // side bank causes torque - rotate
    float bank = DirectionAside().Y();
    pForce = Vector3(bank * (34000 * MassCorrect), 0, 0);
    pCenter = Vector3(0, 0, -6 * PosCorrect * 0.03);
    torque += pCenter.CrossProduct(pForce);
#if ARROWS
    AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce) * InvMass());
#endif

    // convert forces to world coordinates

    DirectionModelToWorld(torque, torque);
    DirectionModelToWorld(force, force);

    // angular velocity causes also some angular friction
    // this should be simulated as torque
    torqueFriction = _angMomentum * 3.0;

    // calculate new position
    Matrix4 movePos;
    ApplySpeed(movePos, deltaT);
    Frame moveTrans;
    moveTrans.SetTransform(movePos);

    // body air friction
    DirectionModelToWorld(friction, BodyFriction(speed) * GetMass() * (1.0 / 7000));
    if (_rpm < 1)
    {
        friction *= 10 * (1 - _rpm);
    }
#if ARROWS
    AddForce(wCenter, -pForce * InvMass());
#endif

    // gravity - no torque
    pForce = Vector3(0, -1, 0) * (GetMass() * G_CONST);
    PoseidonAssert(pForce.IsFinite());
    force += pForce;
#if ARROWS
    AddForce(wCenter, pForce * InvMass());
#endif

    // avoid going underground
    Point3 mPos = moveTrans.Position();
    float minAbove = 0.03;
    float minY = GLOB_LAND->RoadSurfaceYAboveWater(mPos[0], mPos[2]);
    saturateMax(mPos[1], minY + minAbove);
    moveTrans.SetPosition(mPos);

    _landContact = (mPos[1] <= minY + minAbove + 0.01);

    // apply all forces
    ApplyForces(deltaT, force, torque, friction, torqueFriction);

    if (_landContact && _rpmWanted < 0.9)
    {
        // make stopped
        _speed = VZero;
        mPos[1] = minY;
    }

    DoAssert(Transform().IsFinite());
    DoAssert(moveTrans.IsFinite());

    Move(moveTrans);
    DirectionWorldToModel(_modelSpeed, _speed);
}

bool SeaGull::IsAnimated(int level) const
{
    return true;
}
bool SeaGull::IsAnimatedShadow(int level) const
{
    return true;
}

void SeaGull::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    shape->SetPhase(_wingPhase + _wingBase, _wingBase);
}

void SeaGull::Deanimate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    shape->SetPhase(0, 0);
}

DEFINE_CASTING(SeaGull)

DEFINE_FAST_ALLOCATOR(SeaGullAuto)

SeaGullAuto::SeaGullAuto(void* pilot)
    : _pilotHeight(0), _pilotSpeed(VZero), _pilotHeading(0), _pilotHeadingSet(false), _pressedForward(false),
      _pressedBack(false), _pressedUp(false), _pressedDown(false), _lastPilotTime(0), _state(AutopilotNear),
      _mouseDirWanted(VZero)
{
    ResetAutopilot();
}

const float MinSpeed = -0.5;

void SeaGullAuto::Simulate(float deltaT, SimulationImportance prec)
{
    if (prec > SimulateVisibleNear)
    { // sea-gulls are visible only near (are very small)
        if (Glob.time > _lastPilotTime)
        {
            // avoid calculating pilot too often
            // pilot calculation can be quite time consuming
            _dirCompensate = 0.5; // how much we compensate for estimated change
            CamControl(deltaT);
            _lastPilotTime = Glob.time + 2.0 + GRandGen.RandomValue();
        }

        // if sea-gull is invisible simulate it very roughly
        // just linear movement with constant speed, no animation
        Vector3 position = Position();
        float surfaceY = GLandscape->SurfaceYAboveWater(position.X(), position.Z());
        DirectionModelToWorld(_speed, _pilotSpeed);
        position += _speed * deltaT;
        position[1] = _pilotHeight + surfaceY;
        Matrix4 trans;
        trans.SetRotationY(-_pilotHeading);
        trans.SetPosition(position);
        Move(trans);
        DirectionWorldToModel(_modelSpeed, _speed);
        _wingPhase = 0;
        _wingBase = 0;
        _mainRotor = 0.5;
        _wingDive = 0;
        _rpmWanted = 1;
        _rpm = 1;
        return;
    }

    if (_manual)
    {
        _dirCompensate = 0.9; // heading compensation
        KeyboardPilot(nullptr, deltaT);
    }
    else
    {
        _dirCompensate = 0.9; // how much we compensate for estimated change
        CamControl(deltaT);
        _lastPilotTime = Glob.time;
    }

// use acceleration to estimate change of position
#define EST_DELTA 1.0
    // estimate vertical acceleration
    // float estY=Position.Y()+_pilotSpeed*EST_DELTA+_acceleration*EST_DELTA*EST_DELTA*0.5;
    // wanted acceleration is a
    float surfaceY = GLandscape->SurfaceYAboveWater(Position().X(), Position().Z());
    float aboveHeight = _pilotHeight + surfaceY - Position().Y();
    if (GetManual())
    {
        // avoid ground in advance
        for (int i = 0; i < 2; i++)
        {
            float estT = i;
            Vector3 estPos = Position() + _speed * estT;
            float estSurfY = GLandscape->SurfaceYAboveWater(estPos.X(), estPos.Z());
            float estAboveHeight = _pilotHeight + estSurfY - Position().Y();
            saturateMax(aboveHeight, estAboveHeight);
        }
    }
    float wantedAY = ((aboveHeight - _speed.Y() * EST_DELTA) * (1 / (0.5 * EST_DELTA * EST_DELTA)));
    float changeAY = wantedAY - _acceleration.Y();
    Vector3 absSpeedWanted(VMultiply, DirModelToWorld(), _pilotSpeed);
    Vector3 changeAccel = (absSpeedWanted - _speed) * (1 / EST_DELTA) - _acceleration;

    _thrustWanted = (_pilotSpeed[2] - ModelSpeed()[2]) * 2;

    changeAccel = DirectionWorldToModel(changeAccel);
    _mainRotorWanted = changeAY * 0.2 + _mainRotor;

    // get simple aproximations of bank and dive
    // we must consider current angular velocity
    float estT = 1.0;
    Matrix3Val orientation = Orientation();
    Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation * estT;
    Vector3 direction = Direction() * (1 - _dirCompensate) + estOrientation.Direction() * _dirCompensate;
    float curHeading = atan2(direction[0], direction[2]);
    float changeHeading = AngleDifference(_pilotHeading, curHeading);

    float bank = estOrientation.DirectionAside().Y();
    float dive = estOrientation.Direction().Y();
    float fastDive = fabs(_pilotSpeed[2]) * (1.0 / 9);
    Limit(fastDive, 0.0, 0.7);
    _wingDiveWanted = (
        // when moving slow, dive corresponds to acceleration
        (dive + changeAccel[2]) * (-1.0 / 75) * (1 - fastDive) +
        // when moving fast, dive corresponds to speed
        _pilotSpeed[2] * (-0.5 / 12) * fastDive);
    float diveWanted = 0; // never dive (except for loosing altitude?)
    if (aboveHeight > 5)
    {
        diveWanted = 0.2;
    }
    else if (aboveHeight < -5)
    {
        diveWanted = -0.3;
    }
    else if (aboveHeight < -10)
    {
        diveWanted = -0.6;
    }
    float bankWanted = -changeHeading * 4;
    Limit(bankWanted, -0.9, +0.9);
    Limit(_wingDiveWanted, -0.4, 0.4);

    _cyclicAsideWanted = +(bankWanted - bank) * 2;
    _cyclicForwardWanted = -(diveWanted - dive) * 2;

    if (_angVelocity.SquareSize() >= 4 * 4)
    { // if we are rotating fast, leave all controls in neutral position
        _cyclicAsideWanted = 0;
        _cyclicForwardWanted = 0;
        _mainRotorWanted = 0.3;
    }

    // perform advanced simulation
    SeaGull::Simulate(deltaT, prec);
}

void SeaGullAuto::AvoidGround(float minHeight)
{
    Point3 estimate = Position();
    for (int i = 0; i < 4; i++)
    {
        float estY = GLOB_LAND->SurfaceYAboveWater(estimate.X(), estimate.Z());
        estY += minHeight;
        float estUnder = estY - estimate.Y();
        if (estUnder > 0 && i == 1)
        {
            float maxSpeed = Interpolativ(estUnder, 0, 10, 20, 0);
            Limit(_pilotSpeed[2], MinSpeed, maxSpeed);
        }
        estimate += _speed * 3.0;
    }
}

const float SpeedCorrect2 = SpeedCorrect * SpeedCorrect;

void SeaGullAuto::Autopilot(Vector3Par target, Vector3Par tgtSpeed, // target
                            Vector3Par direction, Vector3Par speed  // wanted values
)
{
    // point we would like to reach
    float avoidGround = 0;
    Vector3Val position = Position();
    Vector3 absDistance = target - position;
    Vector3 distance = DirectionWorldToModel(absDistance);

    float tgtSurfaceY = GLandscape->SurfaceYAboveWater(target.X(), target.Z());
    float sizeXZ2 = distance[0] * distance[0] + distance[2] * distance[2];
    switch (_state)
    {
        default: // case AutopilotFar:
        {
            Vector3 absDirection = absDistance + tgtSpeed * 5; // "lead target" - est. target position
            _pilotHeading = atan2(absDirection.X(), absDirection.Z());
            avoidGround = 10;
            _pilotHeight = target.Y() - tgtSurfaceY + avoidGround;
            if (sizeXZ2 < 50 * 50)
            {
                _state = AutopilotNear;
            }
            _pilotSpeed[0] = 0; // no side slips
            _pilotSpeed[1] = 0; // vertical speed is ignored anyway
            _pilotSpeed[2] = 20;
            _rpmWanted = 1;
            // target height
        }
        break;
        case AutopilotBrake:
            Fail("Sea gull cannot brake");
            break;
        case AutopilotNear:
        {
            Vector3 absDirection = absDistance + tgtSpeed * 5; // "lead target" - est. target position
            // slow down near the target
            _pilotHeading = atan2(absDirection.X(), absDirection.Z());
            float fast = sqrt(sizeXZ2) * (1.0 / 50);
            Limit(fast, 0, 1);
            float y = 0;
            saturateMax(y, target.Y() - tgtSurfaceY);
            avoidGround = (fast * 2 + 1);
            _pilotHeight = y + avoidGround;
            _pilotSpeed[0] = 0; // no side slips
            _pilotSpeed[1] = 0; // vertical speed is ignored anyway
            _pilotSpeed[2] = 9 * fast + 1;
            _rpmWanted = 1;
            if (sizeXZ2 < 20 * 20 && speed.Distance2(tgtSpeed) < 2 * 2)
            {
                _state = AutopilotAlign;
            }
            else if (distance[2] > fabs(distance[0]) * 2 && sizeXZ2 > 60 * 60)
            {
                // far away, heading to target, target is moving slow
                _state = AutopilotFar;
            }
        }
        break;
        case AutopilotAlign:
        case AutopilotReached:
        {
            float sizeXZ = sqrt(sizeXZ2);
            _pilotHeading = atan2(direction.X(), direction.Z());
            float high = Interpolativ(sizeXZ, 1, 10, 0, 1);
            float highSpeed = Interpolativ(_speed.Distance2(speed), 0, 4 * 4, 0, 1);
            saturateMax(high, highSpeed);
            _pilotHeight = target.Y() - tgtSurfaceY + high;
            _pilotSpeed = distance * 0.5;
            if (high < 0.1)
            {
                _state = AutopilotReached;
                _rpmWanted = 0; // turn off engine (when on ground)
                _pilotSpeed = VZero;
                _pilotHeight -= 0.2;
            }
            if (high > 0.2)
            {
                if (_rpm < 0.5)
                {
                    QuickStart();
                    _rpm = _rpmWanted = 1;
                }
                _state = AutopilotAlign;
            }
            if (sizeXZ2 > 25 * 25 || tgtSpeed.Distance2(speed) >= 4 * 4)
            {
                _state = AutopilotNear;
            }
            if (_state == AutopilotReached)
            {
                avoidGround = -0.5;
            }
            else
            {
                avoidGround = high;
            }
        }
        break;
    }
    _pilotSpeed += speed;
    Limit(_pilotSpeed[2], MinSpeed, 20);
    if (avoidGround > 0)
    {
        AvoidGround(avoidGround);
    }
}

void SeaGullAuto::ResetAutopilot()
{
    // We set state to near. It will go to far automatically (if necessary).
    _state = AutopilotNear;
}

bool SeaGullAuto::IsVirtualX(CameraType camType) const
{
    if (GetManual())
    {
        if (InputSubsystem::Instance().IsLookAroundEnabled())
        {
            return true;
        }
        return camType != CamInternal && camType != CamExternal;
    }
    return true;
}

void SeaGullAuto::AimDriver(Vector3Val val)
{
    // mouse piloting
    _mouseDirWanted = val;
}

void SeaGullAuto::CamControl(float deltaT)
{
    // get camera target - go there
    // decide where to fly to
    if (_camPos.SquareSize() > 0.5)
    {
        Autopilot(_camPos, VZero, VForward, VZero);
    }
    else
    {
        Autopilot(Position(), VZero, VForward, VZero);
    }
}

void SeaGullAuto::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _pilotSpeed[1] = 0; // maintain height
    _pilotSpeed[0] = 0; // no side slip

    bool internalCamera = IsGunner(GWorld->GetCameraType());
    if (internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
    {
        // last input from mouse - use mouse controls
        _pilotHeading = atan2(_mouseDirWanted[0], _mouseDirWanted[2]);
    }
    else
    {
        Vector3Val direction = Direction();
        float turn = (input.GetTurnRight() - input.GetTurnLeft()) * 2;
        if (fabs(turn) > 0.001)
        {
            _pilotHeading = atan2(direction[0], direction[2]) + turn;
            _pilotHeadingSet = false;
        }
        else if (!_pilotHeadingSet)
        {
            _pilotHeadingSet = true;
            // estimate direction in advance
            // player expects some inertia

            float estT = 2.0;
            Matrix3Val orientation = Orientation();
            Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
            Matrix3Val estOrientation = orientation + derOrientation * estT;
            Vector3 estDirection = estOrientation.Direction();

            _pilotHeading = atan2(estDirection[0], estDirection[2]);
        }
    }

    float forward = (input.GetMoveForward() * 0.5 + input.GetMoveFastForward() - input.GetMoveBack() * 0.25);

    _pilotSpeed[2] = forward * 15;
    _pilotSpeed[0] = 5 * (input.GetMoveRight() - input.GetMoveLeft());

    Vector3Val position = Position();

    float surfaceY = GLandscape->SurfaceYAboveWater(Position().X(), Position().Z());

    float curHeight = position.Y() - surfaceY;
    if (input.GetMoveUp())
    {
        if (!_pressedUp)
        {
            _pilotHeight = curHeight, _pressedUp = true;
        }
        _pilotHeight += deltaT * 10 * input.GetMoveUp();
    }
    else
    {
        if (_pressedUp)
        {
            _pilotHeight = curHeight + _speed[1] * 0.5, _pressedUp = false;
        }
    }
    if (input.GetMoveDown())
    {
        if (!_pressedDown)
        {
            _pilotHeight = curHeight, _pressedDown = true;
        }
        _pilotHeight -= deltaT * 10 * input.GetMoveDown();
    }
    else
    {
        if (_pressedDown)
        {
            _pilotHeight = curHeight + _speed[1] * 0.5, _pressedDown = false;
        }
    }
    if (input.IsFirePressed())
    {
        if (_nextCreek > Glob.time + 1)
        {
            _nextCreek = Glob.time + 0.1;
        }
    }
    // float positionY=GLOB_LAND->SurfaceYAboveWater(position[0],position[2]);
    Limit(_pilotSpeed[2], -0.5, +20);
    float noland = floatMax(ModelSpeed().Size(), _pilotSpeed.Size()) * 0.33 - 1;
    saturateMin(noland, 2);
    Limit(_pilotHeight, -0.05 + noland, 250);
    if (_pilotHeight < 0.2 && curHeight < 0.5)
    {
        _rpmWanted = 0, _pilotSpeed = VZero;
    }
    else if (_pilotHeight > 0.4 && _rpmWanted < 0.5)
    {
        _rpmWanted = 1, _pilotSpeed = Vector3(0, 0, 1);
    }
}

void SeaGullAuto::Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(forceLOD, clipFlags, pos);
}

void SeaGullAuto::DrawDiags()
{
    {
        Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
        float scale = 0.5;
        obj->SetPosition(_camPos);
        Color color(0, 1, 1);
        obj->SetScale(scale);
        obj->SetConstantColor(PackedColor(color));
        GScene->ObjectForDrawing(obj);
    }
    // draw pilot heading
    LODShapeWithShadow* forceArrow = GScene->ForceArrow();
    {
        Matrix3 rot(MRotationY, -_pilotHeading);
        Vector3Val dir = rot.Direction();
        Ref<Object> arrow = new ObjectColored(forceArrow, -1);

        float size = 0.1;
        arrow->SetPosition(Position());
        arrow->SetOrient(dir, VUp);
        arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
        arrow->SetScale(size);
        arrow->SetConstantColor(PackedColor(Color(1, 1, 0, 0.5)));

        GScene->ObjectForDrawing(arrow);
    }
}

void SeaGullAuto::MakeLanded()
{
    _wingPhase = 0.5;
    _wingBase = 1;
    _mainRotor = _mainRotorWanted = 0;
    _cyclicAsideWanted = 0;
    _cyclicForwardWanted = 0;
    _wingDive = _wingDiveWanted = 0;
    _thrust = _thrustWanted = 0;
    _rpm = _rpmWanted = 0;
    _state = AutopilotReached;
    _pilotHeight = 0;
    _pilotSpeed = VZero;
}

void SeaGull::QuickStart()
{
    _wingPhase = 0;
    _wingBase = 0;
    _mainRotor = _mainRotorWanted = 1.5;
    _cyclicAsideWanted = 0;
    _cyclicForwardWanted = 0;
    _wingDive = _wingDiveWanted = 0;
    _thrust = _thrustWanted = 0;
    // quickstart
    _mainRotor = _mainRotorWanted = 2;
}

void SeaGullAuto::MakeAirborne(float height)
{
    _wingPhase = 0;
    _wingBase = 0;
    _mainRotor = _mainRotorWanted = 1.5;
    _cyclicAsideWanted = 0;
    _cyclicForwardWanted = 0;
    _pilotHeight = height;
    _wingDive = _wingDiveWanted = 0;
    _thrust = _thrustWanted = 0;
    _rpm = _rpmWanted = 1;
}

void SeaGullAuto::Command(RString mode)
{
    if (!strcmpi(mode, "landed"))
    {
        MakeLanded();
    }
    else if (!strcmpi(mode, "airborne"))
    {
        MakeAirborne(_pilotHeight);
    }
    else
    {
        base::Command(mode);
    }
}

void SeaGullAuto::Commit(float time)
{
    // CameraHolder implementation
    // set movement target
}

NetworkMessageType SeaGullAuto::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateSeagull;
        case NMCUpdatePosition:
            return NMTUpdatePositionSeagull;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_SEAGULL_MSG(XX)                                                                                         \
    XX(Vector3, pilotSpeed, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Wanted speed"), IdxTransfer,        \
       ET_ABS_DIF, 1)                                                                                                  \
    XX(float, pilotHeading, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted heading"), IdxTransfer, ET_ABS_DIF, \
       ERR_COEF_VALUE_MAJOR)                                                                                           \
    XX(float, pilotHeight, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted height"), IdxTransfer, ET_ABS_DIF,   \
       ERR_COEF_VALUE_MAJOR)                                                                                           \
    XX(int, state, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Autopilot state"), IdxTransfer,            \
       ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateSeagull, UpdateVehicle, UPDATE_SEAGULL_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateSeagull, UpdateVehicle, UPDATE_SEAGULL_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateSeagull)

namespace Poseidon
{

#define UPDATE_POSITION_SEAGULL_MSG(XX)                                                                                \
    XX(float, rpmWanted, NDTFloat, NCTFloat0To2, DEFVALUE(float, 0), DOC_MSG("Wanted RPM"), IdxTransfer, ET_ABS_DIF,   \
       ERR_COEF_VALUE_MAJOR)                                                                                           \
    XX(float, mainRotorWanted, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted main rotor state"), IdxTransfer, \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                               \
    XX(float, cyclicForwardWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted forward cyclic"),     \
       IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                  \
    XX(float, cyclicAsideWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted aside cyclic"),         \
       IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                  \
    XX(float, wingDiveWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted wing dive"), IdxTransfer,  \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)                                                                               \
    XX(float, thrustWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted thrust"), IdxTransfer,       \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdatePositionSeagull, UpdatePositionVehicle, UPDATE_POSITION_SEAGULL_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionSeagull, UpdatePositionVehicle, UPDATE_POSITION_SEAGULL_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdatePositionSeagull)

namespace Poseidon
{

NetworkMessageFormat& SeaGullAuto::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_SEAGULL_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            UPDATE_POSITION_SEAGULL_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError SeaGullAuto::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            TMCHECK(base::TransferMsg(ctx))

            PoseidonAssert(dynamic_cast<const IndicesUpdateSeagull*>(ctx.GetIndices()))
                const IndicesUpdateSeagull* indices = static_cast<const IndicesUpdateSeagull*>(ctx.GetIndices());

            ITRANSF(pilotSpeed)
            ITRANSF(pilotHeading)
            ITRANSF(pilotHeight)
            ITRANSF_ENUM(state)
        }
        break;
        case NMCUpdatePosition:
        {
            TMCHECK(base::TransferMsg(ctx))

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionSeagull*>(ctx.GetIndices()))
                const IndicesUpdatePositionSeagull* indices =
                    static_cast<const IndicesUpdatePositionSeagull*>(ctx.GetIndices());

            ITRANSF(rpmWanted)
            ITRANSF(mainRotorWanted)
            ITRANSF(cyclicForwardWanted)
            ITRANSF(cyclicAsideWanted)
            ITRANSF(wingDiveWanted)
            ITRANSF(thrustWanted)
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float SeaGullAuto::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdateSeagull*>(ctx.GetIndices()))
                const IndicesUpdateSeagull* indices = static_cast<const IndicesUpdateSeagull*>(ctx.GetIndices());

            ICALCERR_DIST(pilotSpeed, 1)
            ICALCERR_ABSDIF(float, pilotHeading, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, pilotHeight, ERR_COEF_VALUE_MAJOR)
            ICALCERR_NEQ(int, state, ERR_COEF_MODE)
        }
        break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionSeagull*>(ctx.GetIndices()))
                const IndicesUpdatePositionSeagull* indices =
                    static_cast<const IndicesUpdatePositionSeagull*>(ctx.GetIndices());

            ICALCERR_ABSDIF(float, rpmWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, mainRotorWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, cyclicForwardWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, cyclicAsideWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, wingDiveWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, thrustWanted, ERR_COEF_VALUE_MAJOR)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
