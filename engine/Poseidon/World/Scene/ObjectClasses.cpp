
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Core/Global.hpp>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

namespace Poseidon::Foundation
{
template class Ref<RoadType>;
} // namespace Poseidon::Foundation

// template VerySmallArray<int,MAX_LOD_LEVELS>; // bug is MSVC 5.0 - force instantiation

namespace Poseidon
{
template <>
const ::Poseidon::Foundation::EnumName* ::Poseidon::Foundation::GetEnumNames(StreetLamp::LightState dummy)
{
    static const ::Poseidon::Foundation::EnumName LightStateNames[] = {
        ::Poseidon::Foundation::EnumName(StreetLamp::LSOff, "OFF"),
        ::Poseidon::Foundation::EnumName(StreetLamp::LSOn, "ON"),
        ::Poseidon::Foundation::EnumName(StreetLamp::LSAuto, "AUTO"), ::Poseidon::Foundation::EnumName()};
    return LightStateNames;
}

EntityHitType::EntityHitType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 0;
}
EntityHitType::~EntityHitType() = default;

void EntityHitType::Load(const ParamEntry& par)
{
    base::Load(par);
}
void EntityHitType::InitShape()
{
    base::InitShape();
}

void EntityHitType::DeinitShape()
{
    base::DeinitShape();
}

EntityHit::EntityHit(LODShapeWithShadow* shape, const EntityHitType* type, int id) : base(shape, type, id)
{
    PoseidonAssert(type == GetNonAIType());

    _hit.Realloc(type->_hitPoints.Size());
    _hit.Resize(type->_hitPoints.Size());
    for (int i = 0; i < type->_hitPoints.Size(); i++)
    {
        _hit[i] = 0;
    }

    PoseidonAssert(type);
    PoseidonAssert(!type->IsAbstract());
    PoseidonAssert(_shape == type->_shape);
}

EntityHit::~EntityHit() = default;

inline float CalcHitDammage(float distance2, float valRange2)
{
    if (distance2 <= valRange2)
    {
        return 1;
    }
    else
    {
        return valRange2 * valRange2 / (distance2 * distance2);
    }
    // else return valRange2*valRange2*valRange2/(distance2*distance2*distance2);
}
float EntityHit::GetHit(const HitPoint& hitpoint) const
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

float EntityHit::GetHitCont(const HitPoint& hitpoint) const
{
    int index = hitpoint.GetIndex();
    if (index >= 0)
    {
        // continuous hit simulation
        return _hit[index];
    }
    return 0;
}

float EntityHit::LocalHit(Vector3Par pos, float val, float valRange)
{
    // scan all hitpoints and dammage them
    Shape* hitShape = _shape->HitpointsLevel();
    if (!hitShape)
    {
        return 1;
    }

    LOG_DEBUG(Graphics, "{} hit ({:.1f},{:.1f},{:.1f}) (val {:.2f},{:.2f})", (const char*)GetDebugName(), pos[0],
              pos[1], pos[2], val, valRange);

    Animate(_shape->FindHitpoints());

    if (valRange < 0)
    {
        // narrower hit area improves hit locality for soldiers; suits all vehicles
        valRange *= 0.25; // smaller area around direct hit
                          // val *= 2; // but stronger effect
    }

    float valRange2 = Square(valRange);
    const HitPointList& hitpoints = GetType()->GetHitPoints();
    for (int i = 0; i < hitpoints.Size(); i++)
    {
        const HitPoint& hit = *hitpoints[i];
        int index = hit.GetSelection();
        if (index >= 0)
        {
            const NamedSelection& sel = hitShape->NamedSel(index);
            for (int j = 0; j < sel.Size(); j++)
            {
                int pIndex = sel[j];
                Vector3Val hitPt = hitShape->Pos(pIndex);
                float distance2 = hitPt.Distance2(pos);
                float dammage = val * CalcHitDammage(distance2, valRange2);
                LOG_DEBUG(Graphics, "hitPt {:.1f},{:.1f},{:.1f}", hitPt[0], hitPt[1], hitPt[2]);
                LOG_DEBUG(Graphics, " dist {:.1f}: dammage {:.6f}", sqrt(distance2), dammage);
                if (dammage > 1e-4)
                {
                    float hitVal = dammage / hit.GetArmor();
                    float oldHit = _hit[i];
                    float newHit = oldHit + hitVal;
                    _hit[i] = newHit;
                    LOG_DEBUG(Graphics, " hit {}: {:.6f}", (const char*)sel.GetName(), newHit);
                    saturateMin(_hit[i], 1);
                }
            }
        }
    }
    Deanimate(_shape->FindHitpoints());
    return GetType()->GetStructuralDammageCoef();
}

void EntityHit::ResetStatus()
{
    for (int i = 0; i < _hit.Size(); i++)
    {
        _hit[i] = 0;
    }
    base::ResetStatus();
}

StreetLampType::StreetLampType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;
}

StreetLampType::~StreetLampType() = default;

void StreetLampType::Load(const ParamEntry& par)
{
    base::Load(par);

    _colorDiffuse = GetColor(par >> "colorDiffuse");
    _colorAmbient = GetColor(par >> "colorAmbient");
    _brightness = par >> "brightness";
}

void StreetLampType::InitShape()
{
    const ParamEntry& par = *_par;
    _scopeLevel = 2;
    base::InitShape();
    DEF_HIT(_shape, _bulbHit, "lampa", nullptr, par >> "armorBulb");
}
void StreetLampType::DeinitShape()
{
    base::DeinitShape();
}

StreetLamp::StreetLamp(LODShapeWithShadow* shape, StreetLampType* type, int id)
    : base(shape, type, id), _pilotLight(true), _lightPos(shape->MemoryPoint("light"))
{
    SetSimulationPrecision(12.1256);
    _destrType = DestructTree;
    _static = true;
    Object::_type = Primary;
    _lightState = LSAuto;
}

static const Color StreetLightColor(0.9, 0.8, 0.6);
static const Color StreetLightAmbient(0.1, 0.1, 0.1);

void StreetLamp::SwitchLight(LightState state)
{
    _lightState = state;
    SimulateSwitch();
    // check if coordinates make sense
    if (Position().SquareSize() > 100)
    {
        CreateLight(*this);
    }
    else
    {
        Fail("Switched lamp not in landscape");
    }
}

void StreetLamp::Init(Matrix4Par pos)
{
    base::Init(pos);
    SimulateSwitch();
    CreateLight(pos);
}

void StreetLamp::CreateLight(Matrix4Par pos)
{
    if (!_light)
    {
        if (_pilotLight)
        {
            Ref<LODShapeWithShadow> shape = GLOB_SCENE->Preloaded(HalfLight);
            _light = new LightPointVisible(shape, Type()->_colorDiffuse, Type()->_colorAmbient);
            _light->SetPosition(pos.FastTransform(_lightPos));
            _light->SetBrightness(Type()->_brightness);
            //_light->SetDirection(Vector3(0,-1,0),Vector3(0,0,1));
            //	this,,Vector3(0,-1,0),0.5
            GLOB_SCENE->AddLight(_light);
        }
    }
    else
    {
        if (!_pilotLight)
        {
            _light.Free();
        }
    }
}

void StreetLamp::SimulateSwitch()
{
    if (GetTotalDammage() < 0.3 && GetHit(Type()->_bulbHit) < 0.5f)
    {
        switch (_lightState)
        {
            case LSOff:
                _pilotLight = false;
                break;
            case LSOn:
                _pilotLight = true;
                break;
            default:
                Fail("Light state");
            case LSAuto:
            {
                float timeOfDay = Glob.clock.GetTimeOfDay();
                _pilotLight = (timeOfDay < 0.3 || timeOfDay > 0.7);
            }
            break;
        }
    }
    else
    {
        _pilotLight = false;
    }
}

void StreetLamp::ResetStatus()
{
    // force update as soon as possible
    _simulationSkipped = _simulationPrecision;
    base::ResetStatus();
}

void StreetLamp::HitBy(EntityAI* killer, float howMuch, RString ammo)
{
    SimulateSwitch();
    CreateLight(*this);
}

void StreetLamp::OnTimeSkipped()
{
    SimulateSwitch();
    CreateLight(*this);
}

void StreetLamp::Simulate(float deltaT, SimulationImportance prec)
{
    SimulateSwitch();
    CreateLight(*this);
    // if( !_object ) _delete=true;
}

LSError StreetLamp::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.SerializeEnum("lightState", _lightState, 1, LSAuto))
    PARAM_CHECK(ar.Serialize("pilotLight", _pilotLight, 1, false))
    if (ar.IsLoading())
    {
        SimulateSwitch();
        CreateLight(*this);
    }
    return LSOK;
}

DEFINE_FAST_ALLOCATOR(ForestPlain)

DEFINE_CASTING(ForestPlain)

static void InitSkewedShape(LODShape* lShape)
{
    if (lShape->GetOrHints() & ClipLandMask)
    {
        // adjust shape flags so that KeepHeight is not set
        for (int i = 0; i < lShape->NLevels(); i++)
        {
            Shape* shape = lShape->Level(i);
            for (int v = 0; v < shape->NVertex(); v++)
            {
                ClipFlags clip = shape->Clip(v);
                clip &= ~ClipLandMask;
                shape->SetClip(v, clip);
            }
            shape->CalculateHints();
        }
        lShape->AllowAnimation(false);
        lShape->CalculateHints();
        // LOG_DEBUG(Graphics, "InitSkewedShape {}",lShape->Name());
    }
}

ForestPlain::ForestPlain(LODShapeWithShadow* shape, int id) : base(shape, id)
{
    _singleMatrixT1 = false;
    _singleMatrixT2 = false;
    // check - some forest may use single matrix
    const char* name = shape->Name();
    if (strstr(name, "t1"))
    {
        _singleMatrixT1 = true;
        InitSkewedShape(shape);
    }
    else if (strstr(name, "t2"))
    {
        _singleMatrixT2 = true;
        InitSkewedShape(shape);
    }
}

DEFINE_FAST_ALLOCATOR(Forest)

Forest::Forest(BuildingType* type, int id) : base(type->GetShape(), id)
{
    _type = type;
    _type->VehicleAddRef();

#if FOREST_PATHS
    int n = NPos();
    _locks.Resize(n);
    for (int i = 0; i < n; i++)
        _locks[i] = 0;
#endif
}

Forest::~Forest()
{
    _type->VehicleRelease();
}

#define ANIM_FOREST 0

void ForestPlain::Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(forceLOD, clipFlags, pos);
}

bool ForestPlain::IsAnimated(int level) const
{
    return !(_singleMatrixT1 || _singleMatrixT2);
}
bool ForestPlain::IsAnimatedShadow(int level) const
{
    return false;
}

bool ForestPlain::HasLandClip(int level) const
{
    return base::HasLandClip(level) || !(_singleMatrixT1 || _singleMatrixT2);
}

Object::LandClipMode ForestPlain::GetLandClipMode(int level) const
{
    if (_singleMatrixT1 || _singleMatrixT2)
    {
        return LandClipNone;
    }
    return LandClipPlane;
}

Matrix4 ForestPlain::GetInvTransform() const
{
    if (!_singleMatrixT1 && !_singleMatrixT2)
    {
        return base::GetInvTransform();
    }
    // matrix contains skew
    return Frame::InverseGeneral();
}

void ForestPlain::InitSkew(Landscape* land)
{
    if (!_singleMatrixT1 && !_singleMatrixT2)
    {
        return;
    }

    float xC = Position().X();
    float zC = Position().Z();
    // fine rectangles are not used - use rough instead
    // calculate surface level on given coordinates
    float xRel = xC * InvLandGrid;
    float zRel = zC * InvLandGrid;

    int x = toIntFloor(xRel);
    int z = toIntFloor(zRel);

    int subdivLog = land->GetTerrainRangeLog() - land->GetLandRangeLog();
    int subdiv = 1 << subdivLog;

    int xs = x * subdiv;
    int zs = z * subdiv;

    float y00 = land->GetHeight(zs, xs);
    float y01 = land->GetHeight(zs, xs + subdiv);
    float y10 = land->GetHeight(zs + subdiv, xs);
    float y11 = land->GetHeight(zs + subdiv, xs + subdiv);

    float d1000 = y10 - y00;
    float d0100 = y01 - y00;

    float d1011 = y10 - y11;
    float d0111 = y01 - y11;

    if (_singleMatrixT2)
    {
        // create a skew matrix for T2
        // T1 dy y00+d1000*zIn+d0100*xIn :
        _skewX = d0100 * InvLandGrid;
        _skewZ = d1000 * InvLandGrid;
    }
    else
    {
        // T2 dy y10+d0111-d1011*xIn-zIn*d0111
        // create a skew matrix for T1
        _skewX = -d1011 * InvLandGrid;
        _skewZ = -d0111 * InvLandGrid;
    }

    // calculate (0,0,0) current height
    // model coordinates of original (0,0,0) point are -_shape->BoundingCenter()

    // apply skew settings
    Matrix4 skew = MIdentity;
    skew(1, 0) = _skewX;
    skew(1, 2) = _skewZ;

    // calculate where will be the bounding center transformed
    Vector3 bcT = skew.FastTransform(-_shape->BoundingCenter());
    // calculate world coord. position
    Vector3 bcW = PositionModelToWorld(bcT);

    // calculate surface position using current plane equation
    float xIn = bcW.X() * InvLandGrid - x; // relative 0..1 in square
    float zIn = bcW.Z() * InvLandGrid - z;
    float y = (_singleMatrixT2 ? y00 + d1000 * zIn + d0100 * xIn : y10 + d0111 - d1011 * xIn - zIn * d0111);

    _offsetY = y - bcW.Y();

    Vector3 pos = skew.Position();
    pos[1] += _offsetY;
    skew.SetPosition(pos);

    // append skew matrix to object matrix
    Matrix4 transform = Transform() * skew;
    SetTransform(transform);
}

const float ForestViewDensity = -0.024079456087; // log(0.3)*0.02
float ForestPlain::ViewDensity() const
{
    return ForestViewDensity;
}
} // namespace Poseidon

#pragma optimize("t", on) // optimize for speed
namespace Poseidon
{
void ForestPlain::Animate(int level)
{
    if (RenderHandlesLandClip())
    {
        return;
    }

    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }

    PoseidonAssert(GLandscape);
    // save original position
    shape->SaveOriginalPos();

    if (_singleMatrixT1 || _singleMatrixT2)
    {
        // matrix included in object matrix
    }
    else
    {
        float xC = Position().X();
        float zC = Position().Z();
        // fine rectangles are not used - use rough instead
        // calculate surface level on given coordinates
        float xRel = xC * InvLandGrid;
        float zRel = zC * InvLandGrid;
        int x = toIntFloor(xRel);
        int z = toIntFloor(zRel);
        float xf = x;
        float zf = z;

        int subdivLog = TerrainRangeLog - LandRangeLog;
        int subdiv = 1 << subdivLog;

        int xs = x * subdiv;
        int zs = z * subdiv;

        Landscape* land = GLandscape;
        float y00 = land->GetHeight(zs, xs);
        float y01 = land->GetHeight(zs, xs + subdiv);
        float y10 = land->GetHeight(zs + subdiv, xs);
        float y11 = land->GetHeight(zs + subdiv, xs + subdiv);

        float d1000 = y10 - y00;
        float d0100 = y01 - y00;

        float d1011 = y10 - y11;
        float d0111 = y01 - y11;

        shape->InvalidateNormals();
        shape->InvalidateBuffer();

        Matrix4Val toWorld = Transform();
        Matrix4Val fromWorld = GetInvTransform();

        // convert plane equations to model space
        // change object shape to reflect surface
        // most forest objects are not rotated, only offseted - this can be easily optimized
        bool rotated = Direction() * VForward < 0.99;
        float yOffset = -_shape->BoundingCenter().Y();
        if (!rotated)
        {
            for (int i = 0; i < shape->NPos(); i++)
            {
                Vector3Val pos = shape->OrigPos(i);
                // shape y is relative to surface
                // calculate world coordinates
                float yPos = pos[1] - yOffset;
                Vector3 tPos = pos + Position();

                float xIn = tPos.X() * InvLandGrid - xf; // relative 0..1 in square
                float zIn = tPos.Z() * InvLandGrid - zf;
                float y = (xIn <= 1 - zIn ? y00 + d1000 * zIn + d0100 * xIn : y10 + d0111 - d1011 * xIn - zIn * d0111);

                V3& dPos = shape->SetPos(i);
                dPos[1] = y + yPos - Position().Y();
            }
        }
        else
        {
            for (int i = 0; i < shape->NPos(); i++)
            {
                Vector3Val pos = shape->OrigPos(i);
                // shape y is relative to surface
                // calculate world coordinates
                Vector3 tPos(VFastTransform, toWorld, pos);
                float xIn = tPos.X() * InvLandGrid - xf; // relative 0..1 in square
                float zIn = tPos.Z() * InvLandGrid - zf;

                float yPos = pos[1] - yOffset;
                float y = (xIn <= 1 - zIn ? y00 + d1000 * zIn + d0100 * xIn : y10 + d0111 - d1011 * xIn - zIn * d0111);
                tPos[1] = y + yPos;

                V3& dPos = shape->SetPos(i);
                dPos.SetFastTransform(fromWorld, tPos);
            }
        }

        // animate bounding box
        // i.e. animate all 8 bbox corners

        Vector3 min(1e10, 1e10, 1e10), max(-1e10, -1e10, -1e10);
        for (int lr = 0; lr < 2; lr++)
        {
            for (int ud = 0; ud < 2; ud++)
            {
                for (int fb = 0; fb < 2; fb++)
                {
                    // assume generic (rotated) case here
                    Vector3 pos = shape->MinMaxOrigCorner(lr, ud, fb);
                    // corner is source model coordinates
                    // convert to animated coordinates
                    Vector3 tPos(VFastTransform, toWorld, pos);
                    float xIn = tPos.X() * InvLandGrid - xf; // relative 0..1 in square
                    float zIn = tPos.Z() * InvLandGrid - zf;
                    //
                    float yPos = pos[1] - yOffset;
                    float y =
                        (xIn <= 1 - zIn ? y00 + d1000 * zIn + d0100 * xIn : y10 + d0111 - d1011 * xIn - zIn * d0111);
                    tPos[1] = y + yPos;

                    Vector3 dPos(VFastTransform, fromWorld, tPos);
                    CheckMinMax(min, max, dPos);
                }
            }
        }
        // calculate bsphere and bcenter estimation
        Vector3 bCenter = (min + max) * 0.5;
        // calculate bradius
        float bRadius = min.Distance(bCenter);
        shape->SetMinMax(min, max, bCenter, bRadius);
    }
}

Vector3 ForestPlain::AnimatePoint(int level, int index) const
{
    if (_singleMatrixT1 || _singleMatrixT2)
    {
        Shape* shape = _shape->LevelOpaque(level);
        // return original position
        return PositionModelToWorld(shape->Pos(index));
    }
    else
    {
        return base::AnimatePoint(level, index);
    }
}
} // namespace Poseidon

#pragma optimize("", on) // default optimization
namespace Poseidon
{
void ForestPlain::Deanimate(int level)
{
    if (RenderHandlesLandClip() || !GEngine->LandClipInVS() || _singleMatrixT1 || _singleMatrixT2)
    {
        return;
    }
    Shape* shape = _shape->Level(level);
    if (!shape || !shape->OriginalPosValid())
    {
        return;
    }
    shape->RestoreOriginalPos();
    shape->RestoreMinMax();
    shape->InvalidateNormals();
    shape->InvalidateBuffer();
}

RoadType::RoadType() = default;

RoadType::RoadType(const char* name)
{
    _shape = Shapes.New(name, false, true);
    // scan shape for selections to hide
    Shape* geom = _shape->GeometryLevel();
    if (geom && geom->NFaces() > 0)
    {
        // some shapes (bridges) should not be land-following
        LOG_DEBUG(Graphics, "Road with geometry {}", name);
    }
    else
    {
        if ((_shape->Special() & OnSurface) == 0)
        {
            LOG_DEBUG(Graphics, "Missing OnSurface {}", name);
        }
        _shape->OrSpecial(NoShadow | OnSurface | NoZWrite);
        // no occlusions on roads
        _shape->SetCanBeOccluded(false);
        for_each_alpha for (int level = 0; level < _shape->NLevels(); level++)
        {
            Shape* shape = _shape->Level(level);
            if (!shape)
            {
                continue;
            }
            for (int i = 0; i < shape->NVertex(); i++)
            {
                shape->SetClip(i, shape->Clip(i) | ClipLandOn);
            }
            shape->CalculateHints();
        }
    }
}

RoadType::~RoadType() = default;

DEFINE_FAST_ALLOCATOR(Road)
DEFINE_CASTING(Road)

Road::Road(LODShapeWithShadow* shape, int id) : Object(shape, id)
{
    _roadType = RoadTypes.New(shape->Name());
    SetType(Network);
    if (shape->GeometryLevel())
    {
        SetDestructType(DestructBuilding);
    }
    else
    {
        SetDestructType(DestructNo);
    }
}

void Road::DrawDiags()
{
    // draw star on position of
    // GScene->DrawCollisionStar(Position(),3);
}

bool Road::IsAnimated(int level) const
{
    if (GetDestructType() == DestructNo)
    {
        return false;
    }
    return base::IsAnimated(level);
}
bool Road::IsAnimatedShadow(int level) const
{
    if (GetDestructType() == DestructNo)
    {
        return false;
    }
    return base::IsAnimatedShadow(level);
}

const float roadArmor = 1200;
const float roadInvArmor = 1 / roadArmor;
const float roadLogArmor = log(roadArmor);

float Road::GetArmor() const
{
    return roadArmor;
}
float Road::GetInvArmor() const
{
    return roadInvArmor;
}
float Road::GetLogArmor() const
{
    return roadLogArmor;
}

DEF_RSB(forest);
DEF_RSB(road);
DEF_RSB(streetlamp);
DEF_RSB(house);
DEF_RSB(vehicle);
DEF_RSB(church);

Object* NewObject(LODShapeWithShadow* shape, int id)
{
    const RStringB& className = shape->GetPropertyClass();
    if (className.GetLength() > 0)
    {
        if (className == RSB(forest))
        {
            BuildingType* type = dynamic_cast<BuildingType*>(VehicleTypes.FindShape(shape->Name()));
            if (type)
            {
                type->VehicleAddRef();
                Object* obj = new Forest(type, id);
                type->VehicleRelease();
                return obj;
            }
            // RptF("%s: no forest in config",(const char *)shape->Name());
            return new ForestPlain(shape, id);
        }
        else if (className == RSB(road))
        {
            return new Road(shape, id);
        }
        else if (className == RSB(streetlamp))
        {
            // EntityType *vehType = VehicleTypes.New("StreetLamp");
            EntityType* vehType = VehicleTypes.FindShapeAndSimulation(shape->Name(), className);
            if (!vehType)
            {
                vehType = VehicleTypes.FindShape(shape->Name());
            }
            if (!vehType)
            {
                LOG_DEBUG(Graphics, "{}: {}, config class missing", shape->Name(), (const char*)className);
                return new ObjectPlain(shape, id);
            }
            StreetLampType* type = dynamic_cast<StreetLampType*>(vehType);
            if (!type)
            {
                LOG_DEBUG(Graphics, "{}: {}, config class not StreetLamp", shape->Name(), (const char*)className);
                return new ObjectPlain(shape, id);
            }
            return new StreetLamp(shape, type, id);
        }
        else if (className == RSB(house) || className == RSB(vehicle) || className == RSB(church))
        {
            // search vehicle type bank for given shape
            // prefer type with the same simulation
            EntityType* vType = VehicleTypes.FindShapeAndSimulation(shape->Name(), className);
            if (!vType)
            {
                // if not found, resort to any type with this shape
                vType = VehicleTypes.FindShape(shape->Name());
            }
            if (!vType)
            {
                // it is not in config: ignore it
                LOG_DEBUG(Graphics, "{}: {}, config class missing", shape->Name(), (const char*)className);
                return new ObjectPlain(shape, id);
            }
            EntityAIType* type = dynamic_cast<EntityAIType*>(vType);
            if (!type)
            {
                Fail("Non-ai EntityAIType");
                return new ObjectPlain(shape, id);
            }
            RString sim = type->_simName;
            if (sim.GetLength() <= 0)
            {
                LOG_ERROR(Graphics, "No simulation: {}", (const char*)type->GetName());
                return new ObjectPlain(shape, id);
            }
            type->VehicleAddRef();
            Building* building = nullptr;
            // else if( !strcmpi(sim,"flag") ) building=new Flag(type,id);
            if (!strcmpi(sim, "house"))
            {
                building = new Building(type, id, shape);
            }
            else if (!strcmpi(sim, "church"))
            {
                building = new Church(type, id, shape);
            }
            else if (!strcmpi(sim, "fountain"))
            {
                building = new Fountain(type, id, shape);
            }
            else
            {
                LOG_ERROR(Graphics, "Unknown sim class {}", (const char*)sim);
                building = new Building(type, id, shape);
            }
            type->VehicleRelease();
            if (building)
            {
                return building;
            }
        }
        else
        {
            Log("Unknown object class '%s'", (const char*)className);
        }
    }
    return new ObjectPlain(shape, id);
}

} // namespace Poseidon
Object* NewProxyObject(RString shapeName)
{
    using namespace Poseidon;
    Ref<LODShapeWithShadow> shape = Shapes.New(GetShapeName(shapeName), false, true);
    return NewObject(shape, -1);
}
namespace Poseidon
{

RoadTypeBank RoadTypes;
} // namespace Poseidon
