#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/OperMap.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#define ARROWS 0

namespace Poseidon
{
TankType::TankType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;

    // no dropdown for a cockpit
    _gunnerPilotPos = VZero;
}

void TankType::Load(const ParamEntry& par)
{
    base::Load(par);

    _mainTurret.Load(par >> "Turret");
    _comTurret.Load(par >> "ComTurret");
}

void TankType::InitShape()
{
    _scopeLevel = 2;
    base::InitShape();

    const ParamEntry& par = *_par;

    // turret animations
    _mainTurret.InitShape(par >> "Turret", _shape);
    _comTurret.InitShape(par >> "ComTurret", _shape);

    _comTurretOnMainTurret = true;
    {
        // check if commander turret is part of main turret
        // check in memory
        int level = _shape->FindMemoryLevel();
        Shape* shape = _shape->MemoryLevel();

        int mainBody = _mainTurret.GetBodySelection(level);
        int comBody = _comTurret.GetBodySelection(level);
        if (mainBody >= 0 && comBody >= 0)
        {
            const NamedSelection& mBody = shape->NamedSel(mainBody);
            const NamedSelection& cBody = shape->NamedSel(comBody);
            if (!mBody.IsSubset(cBody))
            {
                _comTurretOnMainTurret = false;
            }
        }
    }

    _radarIndicator.Init(_shape, par >> "IndicatorRadar");
    _turretIndicator.Init(_shape, par >> "IndicatorTurret");
    _watch.Init(_shape, par >> "IndicatorWatch");

    // track animations
    _leftOffset.Init(_shape, "PasOffsetP");
    _rightOffset.Init(_shape, "PasOffsetL");

    _hatchDriver.Init(_shape, par >> "HatchDriver");
    _hatchCommander.Init(_shape, par >> "HatchCommander");
    _hatchGunner.Init(_shape, par >> "HatchGunner");

    _animFire.Init(_shape, "zasleh", nullptr);

    // weapon directions and positions
    Point3 beg, end;
    beg = _shape->MemoryPoint("spice rakety", "usti hlavne");
    end = _shape->MemoryPoint("konec rakety", "konec hlavne");
    _missilePos = (beg + end) * 0.5f;
    _missileDir = (beg - end);
    _missileDir.Normalize();

    _gunDir = _mainTurret._dir;
    _gunPos = _shape->MemoryPoint("kulas");
    if (_gunPos.SquareSize() < 0.1f)
    {
        _gunPos = _mainTurret._pos;
    }

    int level;
    level = _shape->FindLevel(VIEW_GUNNER);
    if (level >= 0)
    {
        Shape* cockpit = _shape->LevelOpaque(level);
        cockpit->MakeCockpit();
        if (_gunnerPilotPos.SquareSize() <= 1e-6)
        {
            _gunnerPilotPos = cockpit->NamedPosition("pilot");
        }
    }

    _cargoLightPos = VZero;
    Shape* memory = _shape->MemoryLevel();
    if (memory)
    {
        _cargoLightPos = memory->NamedPosition("cargo light");
    }

    DEF_HIT_CFG(_shape, _hullHit, par >> "HitHull", GetArmor());
    DEF_HIT_CFG(_shape, _turretHit, par >> "HitTurret", GetArmor());
    DEF_HIT_CFG(_shape, _gunHit, par >> "HitGun", GetArmor());

    DEF_HIT_CFG(_shape, _trackLHit, par >> "HitLTrack", GetArmor());
    DEF_HIT_CFG(_shape, _trackRHit, par >> "HitRTrack", GetArmor());

    // scan wheels - load from cfg
    const ParamEntry& wheels = par >> "Wheels";
    const ParamEntry& rotLWheels = wheels >> "rotL";
    const ParamEntry& rotRWheels = wheels >> "rotR";
    const ParamEntry& upDownRWheels = wheels >> "upDownR";
    const ParamEntry& upDownLWheels = wheels >> "upDownL";

    _wheelsRotL.Clear();
    _wheelsRotR.Clear();
    _wheelsUpDownL.Clear();
    _wheelsUpDownR.Clear();
    _tracksUpDownL.Clear();
    _tracksUpDownR.Clear();

    _wheelsRotL.Realloc(rotLWheels.GetSize());
    for (int i = 0; i < rotLWheels.GetSize(); i++)
    {
        RStringB wheel = rotLWheels[i];
        AnimationWithCenter& anim = _wheelsRotL.Append();
        anim.Init(_shape, wheel, nullptr, nullptr);
        // check if Animation is non-empty in some LOD
        if (anim.IsEmpty())
        {
            _wheelsRotL.Delete(_wheelsRotL.Size() - 1);
        }
    }
    _wheelsRotR.Compact();

    _wheelsRotR.Realloc(rotRWheels.GetSize());
    for (int i = 0; i < rotRWheels.GetSize(); i++)
    {
        RStringB wheel = rotRWheels[i];
        AnimationWithCenter& anim = _wheelsRotR.Append();
        anim.Init(_shape, wheel, nullptr, nullptr);
        // check if Animation is non-empty in some LOD
        if (anim.IsEmpty())
        {
            _wheelsRotR.Delete(_wheelsRotR.Size() - 1);
        }
    }
    _wheelsRotL.Compact();

    _wheelsUpDownL.Realloc(upDownLWheels.GetSize() / 2);
    _tracksUpDownL.Realloc(upDownLWheels.GetSize() / 2);
    for (int i = 0; i < upDownLWheels.GetSize() - 1; i += 2)
    {
        RStringB wheel = upDownLWheels[i];
        RStringB track = upDownLWheels[i + 1];
        AnimationWithCenter& animW = _wheelsUpDownL.Append();
        AnimationWithCenter& animT = _tracksUpDownL.Append();
        animW.Init(_shape, wheel, nullptr, nullptr);
        animT.Init(_shape, track, nullptr, nullptr);
        // check if Animation is non-empty in some LOD
        if (animW.IsEmpty() != animT.IsEmpty())
        {
            RptF("Model %s: selections not corresponding: %s<->%s", _shape->Name(), (const char*)wheel,
                 (const char*)track);
        }
        if (animW.IsEmpty())
        {
            _wheelsUpDownL.Delete(_wheelsUpDownL.Size() - 1);
        }
        if (animT.IsEmpty())
        {
            _tracksUpDownL.Delete(_tracksUpDownL.Size() - 1);
        }
    }
    _wheelsUpDownL.Compact();
    _tracksUpDownL.Compact();

    _wheelsUpDownR.Realloc(upDownRWheels.GetSize() / 2);
    _tracksUpDownR.Realloc(upDownRWheels.GetSize() / 2);
    for (int i = 0; i < upDownRWheels.GetSize() - 1; i += 2)
    {
        RStringB wheel = upDownRWheels[i];
        RStringB track = upDownRWheels[i + 1];
        AnimationWithCenter& animW = _wheelsUpDownR.Append();
        AnimationWithCenter& animT = _tracksUpDownR.Append();
        animW.Init(_shape, wheel, nullptr, nullptr);
        animT.Init(_shape, track, nullptr, nullptr);
        // check if Animation is non-empty in some LOD
        if (animW.IsEmpty() != animT.IsEmpty())
        {
            RptF("Model %s: selections not corresponding: %s<->%s", _shape->Name(), (const char*)wheel,
                 (const char*)track);
        }
        if (animW.IsEmpty())
        {
            _wheelsUpDownR.Delete(_wheelsUpDownR.Size() - 1);
        }
        if (animT.IsEmpty())
        {
            _tracksUpDownR.Delete(_tracksUpDownR.Size() - 1);
        }
    }
    _wheelsUpDownR.Compact();
    _tracksUpDownR.Compact();
}

TurretType::TurretType() = default;

void TurretType::Load(const ParamEntry& cfg)
{
    _minElev = (float)(cfg >> "minElev") * (H_PI / 180);
    _maxElev = (float)(cfg >> "maxElev") * (H_PI / 180);
    _minTurn = (float)(cfg >> "minTurn") * (H_PI / 180);
    _maxTurn = (float)(cfg >> "maxTurn") * (H_PI / 180);
    GetValue(_servoSound, cfg >> "soundServo");
}

void TurretType::InitShape(const ParamEntry& cfg, LODShape* shape)
{
    // get selection names
    RStringB gunAxis = cfg >> "gunAxis";       //"OsaHlavne";
    RStringB turretAxis = cfg >> "turretAxis"; //"OsaVeze";

    RStringB gunBeg = cfg >> "gunBeg"; // "usti hlavne"
    RStringB gunEnd = cfg >> "gunEnd"; // "konec hlavne"

    Shape* memory = shape->MemoryLevel();
    if (memory)
    {
        _yAxisIndex = memory->PointIndex(turretAxis);
        _xAxisIndex = memory->PointIndex(gunAxis);
        if (_xAxisIndex < 0)
        {
            _xAxisIndex = _yAxisIndex;
        }
    }
    else
    {
        _yAxisIndex = -1;
        _xAxisIndex = -1;
    }

    _yAxis = shape->MemoryPoint(turretAxis);
    if (shape->MemoryPointExists(gunAxis))
    {
        _xAxis = shape->MemoryPoint(gunAxis);
    }
    else
    {
        _xAxis = _yAxis;
    }

    if (shape->MemoryPointExists(gunBeg))
    {
        Vector3Val beg = shape->MemoryPoint(gunBeg);
        Vector3Val end = shape->MemoryPoint(gunEnd);
        _pos = (beg + end) * 0.5f;
        _dir = (beg - end);
        _dir.Normalize();
    }
    else
    {
        _pos = VZero;
        _dir = VForward;
    }

    _neutralXRot = atan2(_dir.Y(), _dir.SizeXZ());
    _neutralYRot = 0;

    RString bodyName = cfg >> "body";
    RString gunName = cfg >> "gun";

    _body.Init(shape, bodyName, nullptr);
    _gun.Init(shape, gunName, nullptr);
}

Turret::Turret()
    : _yRot(0), _yRotWanted(0), _xRot(0), _xRotWanted(0), _xSpeed(0), _ySpeed(0), _servoVol(0), _gunStabilized(true)
{
}

LSError Turret::Serialize(ParamArchive& ar)
{
    SerializeBitBool(ar, "gunStabilized", _gunStabilized, 1, false) PARAM_CHECK(ar.Serialize("yRot", _yRot, 1, 0))
        PARAM_CHECK(ar.Serialize("xRot", _xRot, 1, 0)) PARAM_CHECK(ar.Serialize("yRotWanted", _yRotWanted, 1, 0))
            PARAM_CHECK(ar.Serialize("xRotWanted", _xRotWanted, 1, 0))
                PARAM_CHECK(ar.Serialize("xSpeed", _xSpeed, 1, 0)) PARAM_CHECK(ar.Serialize("ySpeed", _ySpeed, 1, 0))

                    return LSOK;
}

void Turret::Sound(const TurretType& type, bool inside, float deltaT, FrameBase& pos,
                   Vector3Val speed // parent position
)
{
    if (_servoVol > 0.001f)
    {
        const SoundPars& pars = type._servoSound;
        if (!_servoSound && pars.name.GetLength() > 0)
        {
            _servoSound = GSoundScene->OpenAndPlay(pars.name, pos.Position(), speed);
        }
        if (_servoSound)
        {
            float vol = pars.vol * _servoVol;
            float freq = pars.freq;
            _servoSound->SetVolume(vol, freq); // volume, frequency
            _servoSound->Set3D(!inside);
            _servoSound->SetPosition(pos.Position(), speed);
        }
    }
    else
    {
        _servoSound.Free();
    }
}

void Turret::UnloadSound()
{
    _servoSound.Free();
}

void Turret::Animate(const TurretType& type, const Object* obj, int level)
{
    int gunSel = type._gun.GetSelection(level);
    int bodySel = type._body.GetSelection(level);
    // check if there is something to animate
    if (bodySel < 0 && gunSel < 0)
    {
        return;
    }

    LODShape* lShape = obj->GetShape();
    Shape* shape = lShape->Level(level);

    if (bodySel >= 0 && shape->NamedSel(bodySel).Size() > 0)
    {
        Matrix4 mat = MIdentity;
        obj->AnimateMatrix(mat, level, bodySel);
        type._body.Transform(lShape, mat, level);
    }
    if (gunSel >= 0 && shape->NamedSel(gunSel).Size() > 0)
    {
        Matrix4 mat = MIdentity;
        obj->AnimateMatrix(mat, level, gunSel);
        type._gun.Transform(lShape, mat, level);
    }
}

void Turret::Deanimate(const TurretType& type, LODShape* shape, int level)
{
    if (type._body.GetSelection(level) >= 0)
    {
        type._body.Restore(shape, level);
    }
    if (type._gun.GetSelection(level) >= 0)
    {
        type._gun.Restore(shape, level);
    }
}

Matrix3 Turret::GetAimWanted() const
{
    return (Matrix3(MRotationY, _yRotWanted) * Matrix3(MRotationX, -_xRotWanted));
}

bool Turret::Aim(const TurretType& type, Vector3Val relDir)
{
    _yRotWanted = AngleDifference(-atan2(relDir.X(), relDir.Z()), type._neutralYRot);
    // float neutralXRot=atan2(_missileDir.Y(),_missileDir.Z());
    float sizeXZ = relDir.SizeXZ();
    _xRotWanted = atan2(relDir.Y(), sizeXZ) - type._neutralXRot;

    if (type._maxTurn - type._minTurn < H_PI * 2)
    {
        // if turning is limited, saturate around turning midpoint
        float midTurn = (type._maxTurn + type._minTurn) * 0.5f;
        _yRotWanted = AngleDifference(_yRotWanted, midTurn) + midTurn;
    }
    else
    {
        _yRotWanted = AngleDifference(_yRotWanted, 0);
    }

    Limit(_xRotWanted, type._minElev, type._maxElev);
    Limit(_yRotWanted, type._minTurn, type._maxTurn);
    float xToAim = fabs(_xRotWanted - _xRot);
    float yToAim = fabs(_yRotWanted - _yRot);
    if (xToAim + yToAim > 1e-6)
    {
        return true; // enable simulation
    }
    return false;
}

void Turret::MoveWeapons(const TurretType& type, AIUnit* unit, float deltaT)
{
    float maxSpeed = 0;
    float ability = unit->GetAbility();
    float delta;
    float speed;

    speed = (_xRotWanted - _xRot) * 4;
    saturateMax(maxSpeed, fabs(speed));
    delta = speed - _xSpeed;
    Limit(delta, -1 * deltaT, +1 * deltaT);
    _xSpeed += delta;
    Limit(_xSpeed, -0.3f * ability, 0.3f * ability);
    _xRot += _xSpeed * deltaT;

    speed = AngleDifference(_yRotWanted, _yRot) * 4;
    saturateMax(maxSpeed, fabs(speed));
    delta = speed - _ySpeed;
    Limit(delta, -3 * deltaT, +3 * deltaT);
    _ySpeed += delta;
    Limit(_ySpeed, -1.2f * ability, +1.2f * ability);
    _yRot += _ySpeed * deltaT;
    if (type._maxTurn - type._minTurn < H_PI * 2)
    {
        // if turning is limited, saturate around turning midpoint
        float midTurn = (type._maxTurn + type._minTurn) * 0.5f;
        _yRot = AngleDifference(_yRot, midTurn) + midTurn;
    }
    else
    {
        _yRot = AngleDifference(_yRot, 0);
    }

    Limit(_xRot, type._minElev, type._maxElev);
    Limit(_yRot, type._minTurn, type._maxTurn);

    float servoVolWanted = 0;
    if (maxSpeed > 0.01f)
    {
        servoVolWanted = 1;
    }
    delta = servoVolWanted - _servoVol;
    Limit(delta, -2 * deltaT, +2 * deltaT);
    _servoVol += delta;
}

Matrix4 Turret::TurretTransform(const TurretType& type) const
{
    Vector3 yAxis = type._yAxis;
    return (Matrix4(MTranslation, yAxis) * Matrix4(MRotationY, _yRot) * Matrix4(MTranslation, -yAxis));
}

Matrix4 Turret::GunTransform(const TurretType& type) const
{
    Vector3 xAxis = type._xAxis;
    return (Matrix4(MTranslation, xAxis) * Matrix4(MRotationX, -_xRot) * Matrix4(MTranslation, -xAxis));
}

Vector3 Turret::GetCenter(const TurretType& type) const
{
    const Vector3& yAxis = type._yAxis; // rotate around this point
    const Vector3& xAxis = type._xAxis; // rotate around this point
    return Vector3(yAxis[0], xAxis[1], yAxis[2]);
}

void Turret::AnimatePoint(const TurretType& type, Vector3& pos, const Object* obj, int level, int index) const
{
    Shape* shape = obj->GetShape()->Level(level);
    int selI = type._gun.GetSelection(level);
    if (selI >= 0)
    {
        const NamedSelection& sel = shape->NamedSel(selI);
        if (sel.IsSelected(index))
        {
            // apply gun transformation
            Matrix4Val gunTransform = GunTransform(type);
            pos = gunTransform.FastTransform(pos);
        }
    }

    selI = type._body.GetSelection(level);
    if (selI >= 0)
    {
        const NamedSelection& sel = shape->NamedSel(selI);
        if (sel.IsSelected(index))
        {
            // apply turret transformation
            Matrix4Val turTransform = TurretTransform(type);
            pos = turTransform.FastTransform(pos);
        }
    }
}

void Turret::AnimateMatrix(const TurretType& type, Matrix4& mat, const Object* obj, int level, int selection) const
{
    if (selection < 0)
    {
        return;
    }
    Shape* shape = obj->GetShape()->Level(level);
    const NamedSelection& sel = shape->NamedSel(selection);

    int selI = type._gun.GetSelection(level);
    if (selI >= 0)
    {
        // check if sel is whole contained in particular selection
        const NamedSelection& tSel = shape->NamedSel(selI);
        if (tSel.IsSubset(sel))
        {
            // apply turret animation to this proxy
            Matrix4Val transform = GunTransform(type);
            mat = transform * mat;
        }
    }
    selI = type._body.GetSelection(level);
    if (selI >= 0)
    {
        // check if sel is whole contained in particular selection
        const NamedSelection& tSel = shape->NamedSel(selI);
        if (tSel.IsSubset(sel))
        {
            // apply turret animation to this proxy
            Matrix4Val transform = TurretTransform(type);
            mat = transform * mat;
        }
    }
}

void Turret::Stabilize(const Object* obj, const TurretType& type, Matrix3Val oldTrans, Matrix3Val newTrans)
{
    if (_gunStabilized && type._yAxisIndex >= 0)
    {
        Vector3Val gunDir = TurretTransform(type).Rotate(GunTransform(type).Direction());

        // move with _xRot, _yRot so that newDir is equal to oldDir

        // adjust newDir to be equal to oldDir

        Matrix3 invTrans = newTrans.InverseRotation();

        // assume oldDirection and newDirection are very near

        Vector3 gunDirNew = invTrans * (oldTrans * gunDir);

        float yRotNew = -atan2(gunDirNew.X(), gunDirNew.Z());
        float xRotNew = atan2(gunDirNew.Y(), gunDirNew.SizeXZ());
        _yRot += AngleDifference(yRotNew, _yRot);
        _xRot += AngleDifference(xRotNew, _xRot);

        Limit(_xRot, type._minElev, type._maxElev);
        Limit(_yRot, type._minTurn, type._maxTurn);
    }
}

void Turret::Stop(const TurretType& type)
{
    _gunStabilized = false;
    _servoVol = 0;
    _xSpeed = 0;
    _ySpeed = 0;
}

void Turret::GunBroken(const TurretType& type)
{
    _xRotWanted = type._minElev;
    _gunStabilized = false;
}

void Turret::TurretBroken(const TurretType& type)
{
    _yRotWanted = _yRot; // no rotation
    _gunStabilized = false;
}

#define UPDATE_TURRET_MSG(XX) \
	XX(bool, gunStabilized, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Gun is stabilized"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(float, yRotWanted, NDTFloat, NCTFloatAngle, DEFVALUE(float, 0), DOC_MSG("Wanted rotation in y axis"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \
	XX(float, xRotWanted, NDTFloat, NCTFloatAngle, DEFVALUE(float, 0), DOC_MSG("Wanted rotation in x axis"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_ERR(UpdateTurret, UPDATE_TURRET_MSG)
DEFINE_NET_INDICES_ERR(UpdateTurret, UPDATE_TURRET_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateTurret)

namespace Poseidon
{

NetworkMessageFormat& Turret::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    UPDATE_TURRET_MSG(MSG_FORMAT_ERR)
    return format;
}

TMError Turret::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesUpdateTurret*>(ctx.GetIndices())) const IndicesUpdateTurret* indices =
        static_cast<const IndicesUpdateTurret*>(ctx.GetIndices());

    ITRANSF(gunStabilized)
    ITRANSF(yRotWanted)
    ITRANSF(xRotWanted)
    return TMOK;
}

float Turret::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;

    PoseidonAssert(dynamic_cast<const IndicesUpdateTurret*>(ctx.GetIndices())) const IndicesUpdateTurret* indices =
        static_cast<const IndicesUpdateTurret*>(ctx.GetIndices());

    ICALCERR_NEQ(bool, gunStabilized, ERR_COEF_MODE)
    ICALCERR_ABSDIF(float, yRotWanted, ERR_COEF_VALUE_MAJOR)
    ICALCERR_ABSDIF(float, xRotWanted, ERR_COEF_VALUE_MAJOR)
    return error;
}

Hatch::Hatch()
{
    _openAngle = 0;
}

void Hatch::Init(LODShape* shape, const ParamEntry& par)
{
    RString selection = par >> "selection";
    RString axis = par >> "axis";
    _animation.Init(shape, selection, nullptr, axis, nullptr);
    _openAngle = HDegree(par >> "angle");
}

void Hatch::Open(Matrix4Par parent, LODShape* shape, int level, float value) const
{
    if (_animation.GetSelection(level) < 0)
    {
        return;
    }
    float angle = value * _openAngle;
    Matrix4 rot;
    _animation.GetRotation(rot, angle, level);

    rot = parent * rot;
    _animation.Transform(shape, rot, level);
}

void Hatch::Restore(LODShape* shape, int level) const
{
    _animation.Restore(shape, level);
}

} // namespace Poseidon
