#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/Game/OperMap.hpp>

namespace Poseidon
{
using namespace Dev;

#define DAMMAGE_DIAGS 0

Vehicle* Object::GetSmoke() const
{
    if (!_dammage)
    {
        return nullptr;
    }
    return _dammage->_smoke;
}
void Object::SetSmoke(Vehicle* smoke)
{
    if (!_dammage)
    {
        _dammage = new DammageRegions;
    }
    _dammage->_smoke = smoke;
    _canSmoke = false;
}

void Object::AddSmokeSource()
{
    if(CanSmoke() && !GetSmoke())
    {
        // start destruction
        Vector3Val com = _shape->CenterOfMass();
        float objSize = GetRadius();
        float density = objSize * (1.0 / 3);
        float size = objSize * (1.0 / 3);
        saturate(density, 0.25, 2.0);
        saturate(size, 0.25, 4.0);
        size *= GRandGen.RandomValue() * 0.4 + 0.8;
        density *= GRandGen.RandomValue() * 0.4 + 0.8;
        float rndTime = GRandGen.RandomValue() * 0.4 + 0.8;
        LODShapeWithShadow* smokeShape = GLOB_SCENE->Preloaded(CloudletBasic);
        SmokeSourceVehicle* smoke = new SmokeSourceOnVehicle(smokeShape, density, size, nullptr, this, com);
        smoke->SetClimbRate(-0.8, 0, 5);
        float smokeSourceCoef = 0.2;
        rndTime *= smokeSourceCoef;
        smoke->SetSourceTimes(5 * rndTime, 30 * rndTime, 30 * rndTime);
        float color = GRandGen.RandomValue();
        smoke->SetColor(Color(0.15, 0.15, 0.10) * color);
        smoke->SetPosition(PositionModelToWorld(com));
        smoke->SetTimes(0.5, 3);
        smoke->SetFades(1.5, 0.5, 3);
        GLOB_WORLD->AddAnimal(smoke);
        SetSmoke(smoke);
        GetNetworkManager().CreateVehicle(smoke, VLTAnimal, "", -1);
    }
}

float Object::GetExplosives() const
{
    return 0;
}

bool Object::HasGeometry() const
{
    if (!_isDestroyed)
    {
        return true;
    }
    return (DestructType)_destrType != DestructTree && (DestructType)_destrType != DestructTent;
}

#if DAMMAGE_DIAGS
#define ReportRegion(reg, text)                                                                                   \
    LOG_DEBUG(Physics, "Dammage {} {:.3f}:({:.1f},{:.1f},{:.1f} x {:.1f})", text, reg.Dammage(), reg.Center()[0], \
              reg.Center()[1], reg.Center()[2], reg.Radius());
#else
#define ReportRegion(reg, text)
#endif

DEFINE_FAST_ALLOCATOR(DammageRegions)

DammageRegions::DammageRegions() : _totalDammage(0) {}

float DammageRegions::GetTotalDammage() const
{
    return _totalDammage;
}

void DammageRegions::SetTotalDammage(float val)
{
    // used for fake units
    _totalDammage = val;
}

float DammageRegions::Repair(float ammount)
{
    _totalDammage -= ammount;
    saturate(_totalDammage, 0, 1);
    return _totalDammage;
}

bool DammageRegions::MustBeSaved() const
{
    return _totalDammage > 0;
}

inline float CalcDammage(float distance2, float valRange2)
{
    if (distance2 <= valRange2)
    {
        return 1;
    }
    else
    {
        return Square(valRange2) / Square(distance2);
    }
}

void Object::IndirectDammage(Shot* shot, EntityAI* owner, Vector3Par pos, float val, float valRange)
{
    if (val <= 0)
    {
        return;
    }
    if ((ObjectType)_type == Temporary)
    {
        return;
    }
    if ((ObjectType)_type == TypeTempVehicle)
    {
        return;
    }
    if (GetDestructType() == DestructNo)
    {
        return;
    }
    Matrix4Val invTransform = GetInvTransform();
    Vector3Val center = invTransform.FastTransform(pos);
    // use armor
    val *= GetInvArmor();
    PoseidonAssert(valRange > 0);
#if DAMMAGE_DIAGS
    LOG_DEBUG(Physics, "Indirect dammage {},{} armor {:.1f}: {:.3f} (range {:.1f})", _shape->Name(),
              (const char*)GetDebugName(), GetArmor(), val, valRange);
#endif
    LocalDammage(shot, owner, center, val, valRange);
}

void Object::DirectDammage(Shot* shot, EntityAI* owner, Vector3Par pos, float val)
{
    if (val <= 0)
    {
        return;
    }
    if ((ObjectType)_type == Temporary)
    {
        return;
    }
    if ((ObjectType)_type == TypeTempVehicle)
    {
        return;
    }
    if (GetDestructType() == DestructNo)
    {
        return;
    }
    if (!GetShape())
    {
        return;
    }

    Matrix4Val invTransform = WorldInvTransform();
    Vector3Val center = invTransform.FastTransform(pos);
    float valRange = sqrt(val) * 0.27;
    val *= GetInvArmor();
#if DAMMAGE_DIAGS
    LOG_DEBUG(Physics, "Direct dammage {} armor {:.1f}: {:.3f}:({:.1f})", _shape->Name(), GetArmor(), val, valRange);
#endif
    LocalDammage(shot, owner, center, val, -valRange);
}

float Object::LocalHit(Vector3Par pos, float val, float valRange)
{
    return 1;
}

float Object::DirectLocalHit(int component, float val)
{
    return 1;
}

void Object::LocalDammage(Shot* shot, EntityAI* owner, Vector3Par modelPos, float val, float valRange)
{
    if (shot)
    {
        // no dammage from fake (remote) shots
        if (!shot->IsLocal())
        {
            return;
        }
    }
    else if (owner)
    {
        // no dammage from fake (remote) vehicles
        if (!owner->IsLocal())
        {
            return;
        }
    }
    else
    {
        // Fail("No owner nor shot");
    }

    RString name = shot ? shot->Type()->ParClass().GetName() : RString();
    if (IsLocal())
    {
        // process dammage
        DoDammage(owner, modelPos, val, valRange, name);
        if (GetNetworkId().creator == 1)
        {
            // broadcast dammage of static object over network
            GetNetworkManager().AskForDammage(this, owner, modelPos, val, valRange, name);
        }
    }
    else
    {
        // ask owner for dammage
        GetNetworkManager().AskForDammage(this, owner, modelPos, val, valRange, name);
    }
}

void Object::DoDammage(EntityAI* owner, Vector3Par pos, float val, float valRange, RString ammo)
{
    if (dyn_cast<Shot>(this))
    {
        RptF("Dammaged shot %s", (const char*)GetDebugName());
    }

    if (GWorld)
    {
        Person* player = GWorld->GetRealPlayer();
        if (player)
        {
            if (DebugCheats::Cmd_God::IsActive() && player == this)
                return;
            if (DebugCheats::Cmd_InfiniteArmor::IsActive() && player->Brain() &&
                player->Brain()->GetVehicleIn() == this)
                return;
        }
    }
    bool diffArmor = USER_CONFIG.IsEnabled(DTArmor);

    if (
#if _ENABLE_CHEATS
        ENGINE_CONFIG.super ||
#endif
        diffArmor)
    {
        VehicleWithBrain* player = GWorld->PlayerOn();
        AIUnit* playerUnit = player ? player->Brain() : nullptr;
        if (playerUnit)
        {
            if (playerUnit->GetVehicle() == this)
            {
#if _ENABLE_CHEATS
                if (ENGINE_CONFIG.super)
                    val = 0;
                else
#endif
                    val *= 1.0 / 3.0;
            }
            else if (diffArmor)
            {
                AIGroup* playerGroup = playerUnit->GetGroup();
                if (playerGroup)
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = playerGroup->UnitWithID(i + 1);
                        if (unit && unit->GetVehicle() == this)
                        {
                            val *= 1.0 / 1.5;
                            break;
                        }
                    }
                }
            }
        }
    }

    val *= LocalHit(pos, val * GetArmor(), valRange);

    saturateMin(val, 2000);

#if DAMMAGE_DIAGS
    LOG_DEBUG(Physics, "  {}: Do dammage {:.3f} (range {:.1f})", (const char*)GetDebugName(), val, valRange);
#endif

    float objRadius = _shape->GeometrySphere();
    float dist2ToCenter = pos.SquareSize();

    float oldDammage = GetRawTotalDammage();
    bool oldDestroyed = IsDammageDestroyed();
    if (valRange <= 0)
    {
        if (valRange == 0)
        {
            Fail("Obsolete direct dammage value");
            valRange = 2 * val;
            saturateMin(valRange, objRadius);
        }
        else
        {
            valRange = -valRange;
        }
#if DAMMAGE_DIAGS
        LOG_DEBUG(Physics, "  val {:.3f} valRange {:.3f} objRadius {:.3f}", val, valRange, objRadius);
#endif

        float avgDammage = val * Square(valRange / objRadius);
        if (avgDammage > 1e-3)
        {
            SetTotalDammage(GetTotalDammage() + avgDammage);
        }
    }
    else
    {
        float distToCenter = sqrt(dist2ToCenter);
        float minDist = floatMax(distToCenter - objRadius, 0);
        float valRange2 = Square(valRange);

        float minDammage = CalcDammage(Square(minDist), valRange2);
        if (minDammage * val <= 1e-3)
        {
#if DAMMAGE_DIAGS
            LOG_DEBUG(Physics, "  Dist: min {:.3f}", minDist);
            LOG_DEBUG(Physics, "  Damm: min {:.3f} (ignored)", minDammage);
#endif
        }
        else
        {
            float midDist = distToCenter;
            float maxDist = distToCenter + objRadius;
            float midDammage = CalcDammage(Square(midDist), valRange2);
            float maxDammage = CalcDammage(Square(maxDist), valRange2);
#if DAMMAGE_DIAGS
            LOG_DEBUG(Physics, "  Dist: min {:.3f} mid {:.3f} max {:.3f}", minDist, midDist, maxDist);
            LOG_DEBUG(Physics, "  Damm: min {:.3f} mid {:.3f} max {:.3f}", minDammage, midDammage, maxDammage);
#endif

            float avgDammage = (minDammage + midDammage + maxDammage) * (0.33 * val);
            if (avgDammage > 1e-3)
            {
                SetTotalDammage(GetTotalDammage() + avgDammage);
            }
        }
    }

#if DAMMAGE_DIAGS
    LOG_DEBUG(Physics, "  Total {:.4f}", GetRawTotalDammage());
#endif
    HitBy(owner, GetRawTotalDammage() - oldDammage, ammo);
    if (!oldDestroyed && IsDammageDestroyed())
    {
        Destroy(owner, GetRawTotalDammage(), 0.05, 0.1);
    }
}

void Object::SetTotalDammage(float val)
{
    if (fabs(val - GetTotalDammage()) < 1e-6)
    {
        return;
    }
    if (val > 0)
    {
        if (!_dammage)
        {
            _dammage = new DammageRegions;
        }
        _dammage->SetTotalDammage(val);
    }
    else
    {
        _dammage.Free();
    }
}

void Object::SetTotalDammageValue(float val)
{
    if (fabs(val - GetTotalDammage()) < 1e-6)
    {
        return;
    }
    if (val > 0)
    {
        if (!_dammage)
        {
            _dammage = new DammageRegions;
        }
        _dammage->SetTotalDammage(val);
    }
    else
    {
        _dammage.Free();
    }
}

void Object::SetDammageNetAware(float dammage)
{
    if (IsLocal())
    {
        SetDammage(dammage);
    }
    else
    {
        GetNetworkManager().AskForSetDammage(this, dammage);
    }
}

void Object::SetDammage(float dammage)
{
    if (GWorld)
    {
        Person* player = GWorld->GetRealPlayer();
        if (player)
        {
            if (DebugCheats::Cmd_God::IsActive() && player == this)
                return;
            if (DebugCheats::Cmd_InfiniteArmor::IsActive() && player->Brain() &&
                player->Brain()->GetVehicleIn() == this)
                return;
        }
    }

    bool oldDestroyed = IsDammageDestroyed();

    SetTotalDammageValue(dammage);

    if (!oldDestroyed)
    {
        if (IsDammageDestroyed())
        {
            EntityAI* owner = dyn_cast<EntityAI>(this);
            if (!owner)
            {
                LOG_ERROR(Physics, "SetDammage applied on non-EntityAI {}", (const char*)GetDebugName());
            }
            Destroy(owner, dammage, 0.05, 0.1);
        }
    }
    else if (GetTotalDammage() < 1.0)
    {
        Vehicle* smoke = GetSmoke();
        if (smoke)
        {
            smoke->SetDelete();
        }
        _canSmoke = true;
        _isDestroyed = false;
        SetDestroyPhase(0);
    }
}

void Object::SetDestroyPhase(int phase)
{
    if (GetDestructType() == DestructTree && phase != _destroyPhase)
    {
        float newAngle = sin(phase * H_PI / 2 / 255) * (H_PI / 2);
        float oldAngle = sin(_destroyPhase * H_PI / 2 / 255) * (H_PI / 2);
        float animChange = newAngle - oldAngle;
        Vector3 bCenter = GetShape()->BoundingCenter();
        Matrix4 rot =
            (Matrix4(MTranslation, -bCenter) * Matrix4(MRotationX, animChange) * Matrix4(MTranslation, bCenter));
        Matrix4 trans = Transform() * rot;
        Move(trans);
        GScene->GetShadowCache().ShadowChanged(this);
    }

    _destroyPhase = phase;
}

void Object::SetDestroyed(float anim)
{
    int phase = toIntFloor(anim * 255);
    saturate(phase, 0, 255);
    SetDestroyPhase(phase);
    bool wasDestroyed = _isDestroyed;
    _isDestroyed = phase > 0;
    if (_isDestroyed && !wasDestroyed)
    {
        if (GetType() == Primary)
        {
            int xLand = toIntFloor(Position().X() * InvLandGrid);
            int zLand = toIntFloor(Position().Z() * InvLandGrid);
            for (int x = xLand - 1; x <= xLand + 1; x++)
            {
                for (int z = zLand - 1; z <= zLand + 1; z++)
                {
                    GLandscape->OperationalCache()->RemoveField(x, z);
                }
            }
        }
    }
}

void Object::Repair(float ammount)
{
    if (GetTotalDammage() <= 0 && ammount >= 0)
    {
        return;
    }
    if (!_dammage)
    {
        _dammage = new DammageRegions;
    }
    if (_dammage->Repair(ammount) <= 0)
    {
        _dammage.Free();
    }

    float tot = GetTotalDammage();
    if (tot < 0.90)
    {
        Vehicle* smoke = GetSmoke();
        if (smoke)
        {
            smoke->SetDelete();
        }
        _canSmoke = true;
    }
    _isDestroyed = false;
    SetDestroyPhase(0);
}

void Object::HitBy(EntityAI* killer, float howMuch, RString ammo) {}

bool Object::IsDammageDestroyed() const
{
    return GetRawTotalDammage() >= 1;
}

void Object::Destroy(EntityAI* owner, float overkill, float minExp, float maxExp)
{
    switch (GetDestructType())
    {
        case DestructTree:
        case DestructTent:
        {
            const ParamEntry& par = Pars >> "CfgDestroy" >> "TreeHit";
            SoundPars soundPars;
            GetValue(soundPars, par >> "sound");
            // temporary destruction vehicle
            float time = 3;
            if (GetDestructType() == DestructTent)
            {
                time = 1.5;
            }
            ObjectDestructed* destroyer = new ObjectDestructed(this, soundPars, time);
            destroyer->SetPosition(Position());
            GLOB_WORLD->AddAnimal(destroyer);
            GetNetworkManager().CreateVehicle(destroyer, VLTAnimal, "", -1);
        }
        break;
        case DestructBuilding:
        case DestructDefault:
        {
            const ParamEntry& par = Pars >> "CfgDestroy" >> "BuildingHit";
            SoundPars soundPars;
            GetValue(soundPars, par >> "sound");
            // temporary destruction vehicle
            ObjectDestructed* destroyer = new ObjectDestructed(this, soundPars, 2);
            destroyer->SetPosition(Position());
            GLOB_WORLD->AddAnimal(destroyer);
            GetNetworkManager().CreateVehicle(destroyer, VLTAnimal, "", -1);
        }
        break;
        case DestructEngine:
        {
            if (CanSmoke() && !GetSmoke())
            {
                AddSmokeSource();
                GetNetworkManager().AddSmokeSource(this);
                SoundPars soundPars;
                soundPars.name = RString(nullptr); // no sound
                soundPars.vol = soundPars.freq = 0;
                ObjectDestructed* destroyer = new ObjectDestructed(this, soundPars, 1);
                destroyer->SetPosition(Position());
                GLOB_WORLD->AddAnimal(destroyer);
                GetNetworkManager().CreateVehicle(destroyer, VLTAnimal, "", -1);
            }
        }
        break;
    }
}

DEF_RSB(id)
DEF_RSB(canSmoke)
DEF_RSB(isDestroyed)
DEF_RSB(destroyed)
DEF_RSB(destroyPhase)
DEF_RSB(dammage)

LSError Object::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving())
        PARAM_CHECK(ar.Serialize(RSB(id), _id, 1))
    SerializeBitBool(ar, RSB(canSmoke), _canSmoke, 1, true)
        SerializeBitBool(ar, RSB(isDestroyed), _isDestroyed, 1, false) if (ar.IsSaving())
    {
        float destroyed = GetDestroyed();
        PARAM_CHECK(ar.Serialize(RSB(destroyed), destroyed, 1, 0))
        float dammage = GetRawTotalDammage();
        // note: dammage is sometimes infinite
        if (!_finite(dammage))
        {
            RptF("Saving inifinite dammage in %s", (const char*)GetDebugName());
        }
        PARAM_CHECK(ar.Serialize(RSB(dammage), dammage, 1, 0))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        float destroyed;
        PARAM_CHECK(ar.Serialize(RSB(destroyed), destroyed, 1, 0))
        SetDestroyed(destroyed);
        float dammage;
        PARAM_CHECK(ar.Serialize(RSB(dammage), dammage, 1, 0))
        SetTotalDammage(dammage);
    }

    {
        int t = _destroyPhase;
        PARAM_CHECK(ar.Serialize(RSB(destroyPhase), t, 1, 0));
        SetDestroyPhase(t);
    }

    return LSOK;
}

Object* Object::CreateObject(ParamArchive& ar)
{
    int id;
    if (ar.Serialize(RSB(id), id, 1) != LSOK)
    {
        return nullptr;
    }
    return GLandscape->FindObject(id);
}

bool Object::MustBeSaved() const
{
    if (_isDestroyed)
    {
        return true;
    }
    if (!_canSmoke)
    {
        return true;
    }
    if (_dammage && _dammage->MustBeSaved())
    {
        return true;
    }
    return false;
}

NetworkId Object::GetNetworkId() const
{
    if ((ObjectType)_type != Primary && (ObjectType)_type != Network)
    {
        LOG_ERROR(Physics, "Type is not primary for object {}", (const char*)GetDebugName());
    }
    return NetworkId(STATIC_OBJECT, _id);
}

void Object::SetNetworkId(NetworkId& id)
{
    Fail("Cannot set network id");
}

bool Object::IsLocal() const
{
    return true;
}

void Object::SetLocal(bool local)
{
    Fail("Object is always local");
}

NetworkMessageType Object::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateObject;
        case NMCUpdateGeneric:
            return NMTUpdateObject;
        case NMCUpdateDammage:
            return NMTUpdateDammageObject;
        default:
            return NetworkObject::GetNMType(cls);
    }
}

IndicesCreateObject::IndicesCreateObject() = default;

void IndicesCreateObject::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateObject()
{
    using namespace Poseidon;
    return new IndicesCreateObject();
}
namespace Poseidon
{

IndicesUpdateObject::IndicesUpdateObject()
{
    canSmoke = -1;
    destroyed = -1;
}

void IndicesUpdateObject::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(canSmoke);
    SCAN(destroyed);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateObject()
{
    using namespace Poseidon;
    return new IndicesUpdateObject();
}
namespace Poseidon
{

IndicesUpdateDammageObject::IndicesUpdateDammageObject()
{
    isDestroyed = -1;
    dammage = -1;
}

void IndicesUpdateDammageObject::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(isDestroyed);
    SCAN(dammage);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateDammageObject()
{
    using namespace Poseidon;
    return new IndicesUpdateDammageObject();
}
namespace Poseidon
{

NetworkMessageFormat& Object::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            break;
        case NMCUpdateDammage:
            NetworkObject::CreateFormat(cls, format);
            format.Add("isDestroyed", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Object is already destructed"),
                       ET_NOT_EQUAL, ERR_COEF_STRUCTURE);
            format.Add("dammage", NDTFloat, NCTFloatMostly0To1, DEFVALUE(float, 0.0f),
                       DOC_MSG("Total damage of object"), ET_ABS_DIF, ERR_COEF_MODE);
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("canSmoke", NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Object may smoke when destructed"));
            format.Add("destroyed", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0.0f), DOC_MSG("Shape destruction state"));
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

Object* Object::CreateObject(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices())) const IndicesNetworkObject* indices =
        static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

    int id;
    if (ctx.IdxTransfer(indices->objectId, id) != TMOK)
    {
        return nullptr;
    }
    return GLandscape->FindObject(id);
}

void Object::DestroyObject()
{
    Fail("Cannot remove primary object");
}

TMError Object::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
        case NMCUpdateDammage:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdateDammageObject*>(ctx.GetIndices()))
                const IndicesUpdateDammageObject* indices =
                    static_cast<const IndicesUpdateDammageObject*>(ctx.GetIndices());
            TMCHECK(NetworkObject::TransferMsg(ctx))
            ITRANSF_BITBOOL(isDestroyed)
            if (ctx.IsSending())
            {
                float dammage = GetRawTotalDammage();
                TMCHECK(ctx.IdxTransfer(indices->dammage, dammage))
            }
            else
            {
                float dammage;
                TMCHECK(ctx.IdxTransfer(indices->dammage, dammage))
                SetTotalDammageValue(dammage);
            }
        }
        break;
        case NMCUpdateGeneric:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdateObject*>(ctx.GetIndices()))
                const IndicesUpdateObject* indices = static_cast<const IndicesUpdateObject*>(ctx.GetIndices());

            TMCHECK(NetworkObject::TransferMsg(ctx))
            ITRANSF_BITBOOL(canSmoke)
            if (ctx.IsSending())
            {
                float destroyed = GetDestroyed();
                TMCHECK(ctx.IdxTransfer(indices->destroyed, destroyed))
            }
            else
            {
                float destroyed;
                TMCHECK(ctx.IdxTransfer(indices->destroyed, destroyed))
                SetDestroyed(destroyed);
            }
        }
        break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Object::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateDammage:
        {
            error += NetworkObject::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdateDammageObject*>(ctx.GetIndices()))
                const IndicesUpdateDammageObject* indices =
                    static_cast<const IndicesUpdateDammageObject*>(ctx.GetIndices());

            ICALCERR_NEQ(bool, isDestroyed, ERR_COEF_STRUCTURE)
            ICALCERRE_ABSDIF(float, dammage, GetRawTotalDammage(), ERR_COEF_VALUE_MAJOR)
        }
        break;
        case NMCUpdateGeneric:
        {
            error += NetworkObject::CalculateError(ctx);
        }
        break;
        default:
            error += NetworkObject::CalculateError(ctx);
            break;
    }
    return error;
}

void Object::ResetStatus()
{
    _dammage.Free();
    Vehicle* smoke = GetSmoke();
    if (smoke)
    {
        smoke->SetDelete();
    }
    _canSmoke = true;
    _isDestroyed = false;
    SetDestroyPhase(0);
}

void Object::OnTimeSkipped() {}

void Object::OnPositionChanged() {}

} // namespace Poseidon
