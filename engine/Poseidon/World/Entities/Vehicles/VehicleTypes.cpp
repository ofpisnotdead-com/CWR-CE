#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/Network/Network.hpp>

#include <Poseidon/World/Entities/Vehicles/AllVehicles.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <string.h>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{
VehicleTypeBank::VehicleTypeBank() {}

VehicleTypeBank::~VehicleTypeBank()
{
    UnlockAllTypes();
}

static bool VehicleSimulationRequiresModel(RString simName)
{
    simName.Lower();
    return !stricmp(simName, "soldier") || !stricmp(simName, "soldierold") || !stricmp(simName, "tank") ||
           !stricmp(simName, "zsu") || !stricmp(simName, "car") || !stricmp(simName, "motorcycle") ||
           !stricmp(simName, "helicopter") || !stricmp(simName, "parachute") || !stricmp(simName, "airplane") ||
           !stricmp(simName, "ship");
}

static bool CfgVehicleVisibleInEditor(const ParamEntry& entry)
{
    if ((entry >> "scope").GetInt() < 2)
    {
        return false;
    }
    if (!entry.FindEntry("vehicleClass"))
    {
        return false;
    }
    RString vehClass = entry >> "vehicleClass";
    if (vehClass.GetLength() == 0)
    {
        return false;
    }
    if (!stricmp(vehClass, "Sounds") || !stricmp(vehClass, "Mines"))
    {
        return false;
    }
    return true;
}

static bool ShapeFileExists(RString shapeName)
{
    if (shapeName.GetLength() == 0)
    {
        return false;
    }
    if (QIFStreamB::FileExist(shapeName))
    {
        return true;
    }
    const char* name = shapeName;
    const char* slash = strrchr(name, '\\');
    if (!slash)
    {
        slash = strrchr(name, '/');
    }
    return slash && QIFStreamB::FileExist(slash + 1);
}

void VehicleTypeBank::LockAllTypes()
{
    // LOG_DEBUG(Physics, "Lock all types");
    for (int i = 0; i < Size(); i++)
    {
        EntityType* type = Set(i);
        type->VehicleLock();
        // LOG_DEBUG(Physics, "  lock {}",(const char *)type->GetName());
    }
}

void VehicleTypeBank::UnlockAllTypes()
{
    // LOG_DEBUG(Physics, "Unlock all types");
    for (int i = 0; i < Size(); i++)
    {
        EntityType* type = Set(i);
        // LOG_DEBUG(Physics, "  unlock {}",(const char *)type->GetName());
        type->VehicleUnlock();
    }
}

EntityType* VehicleTypeBank::New(const char* name)
{
    if (!name || !*name)
    {
        return nullptr;
    }
    int index = Find(name);
    if (index < 0)
    {
        index = Load(name);
    }
    if (index < 0)
    {
        return nullptr;
    }
    return Get(index);
}

EntityType* VehicleTypeBank::FindShape(const char* shape)
{
    for (int i = 0; i < Size(); i++)
    {
        EntityType* type = Get(i);
        if (type->IsAbstract())
        {
            continue;
        }
        if (strcmpi(type->GetShapeName(), shape))
        {
            continue;
        }
        return type;
    }
    return nullptr;
}

EntityType* VehicleTypeBank::FindShapeAndSimulation(const char* shape, const char* sim)
{
    for (int i = 0; i < Size(); i++)
    {
        EntityType* type = Get(i);
        if (type->IsAbstract())
        {
            continue;
        }
        if (strcmpi(type->GetShapeName(), shape))
        {
            continue;
        }
        if (strcmpi(type->_simName, sim))
        {
            continue;
        }
        return type;
    }
    return nullptr;
}

void GlobalAlive();

void VehicleTypeBank::Preload()
{
    static const char* names[] = {"CfgVehicles", "CfgAmmo", "CfgNonAIVehicles"};
    for (int n = 0; n < sizeof(names) / sizeof(*names); n++)
    {
        const char* name = names[n];
        const ParamEntry& vehicles = Pars >> name;
        for (int i = 0; i < vehicles.GetEntryCount(); i++)
        {
            I_AM_ALIVE(); // this loop can take quite long
            const ParamEntry& entry = vehicles.GetEntry(i);
            const ParamClass* eClass = dynamic_cast<const ParamClass*>(&entry);
            if (!eClass)
            {
                continue;
            }
            if (entry.FindEntry("vehicleClass"))
            {
                RString vehClass = entry >> "vehicleClass";
                if (stricmp(vehClass, "Sounds") == 0)
                {
                    continue;
                }
                if (stricmp(vehClass, "Mines") == 0)
                {
                    continue;
                }
            }
            New(entry.GetName());
        }
    }
    if (GSoundsys)
    {
        GSoundsys->FlushBank(nullptr);
    }
    GFileServer->FlushBank(nullptr);
}

int VehicleTypeBank::AuditEditorVisibleModels() const
{
    int missing = 0;
    const ParamEntry& vehicles = Pars >> "CfgVehicles";
    for (int i = 0; i < vehicles.GetEntryCount(); i++)
    {
        I_AM_ALIVE();
        const ParamEntry& entry = vehicles.GetEntry(i);
        const ParamClass* eClass = dynamic_cast<const ParamClass*>(&entry);
        if (!eClass || !CfgVehicleVisibleInEditor(entry))
        {
            continue;
        }

        RString model = entry >> "model";
        RString simulation = entry >> "simulation";
        RString crew = entry >> "crew";
        if (crew.GetLength() > 0 && !stricmp(simulation, "parachute") && !vehicles.FindEntry(crew))
        {
            LOG_ERROR(World, "CfgVehicles '{}': crew '{}' is missing", (const char*)entry.GetName(), (const char*)crew);
            missing++;
        }
        if (model.GetLength() == 0)
        {
            if (VehicleSimulationRequiresModel(simulation))
            {
                LOG_ERROR(World, "CfgVehicles '{}': simulation '{}' is editor-visible but model is empty",
                          (const char*)entry.GetName(), (const char*)simulation);
                missing++;
            }
            continue;
        }

        RString shapeName = GetShapeName(model);
        if (!ShapeFileExists(shapeName))
        {
            LOG_ERROR(World, "CfgVehicles '{}': model '{}' resolved to '{}' is missing", (const char*)entry.GetName(),
                      (const char*)model, (const char*)shapeName);
            missing++;
        }
    }
    if (missing > 0)
    {
        LOG_ERROR(World, "CfgVehicles editor model audit found {} missing model(s)", missing);
    }
    else
    {
        LOG_INFO(World, "CfgVehicles editor model audit found no missing models");
    }
    return missing;
}

class EntityAITypePlain : public EntityAIType
{
    typedef EntityAIType base;

  public:
    EntityAITypePlain(const ParamEntry* param);
    ~EntityAITypePlain() override;
};

EntityAITypePlain::EntityAITypePlain(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;
}

EntityAITypePlain::~EntityAITypePlain() = default;

int VehicleTypeBank::Load(const char* name)
{
    EntityType* type = nullptr;

    if ((Pars >> "CfgVehicles").FindEntry(name))
    {
        const ParamEntry& cfg = Pars >> "CfgVehicles" >> name;
        if ((cfg >> "scope").GetInt() > 0)
        {
            // LOG_DEBUG(Physics, "Preload {}",(const char *)name);
            RString simulation = cfg >> "simulation";
            const ParamEntry* par = &cfg;
            simulation.Lower();
            if (!strcmp(simulation, "tank"))
            {
                type = new TankType(par);
            }
            else if (!strcmp(simulation, "zsu"))
            {
                type = new TankType(par);
            }
            else if (!strcmp(simulation, "car"))
            {
                type = new CarType(par);
            }
            else if (!strcmp(simulation, "motorcycle"))
            {
                type = new MotorcycleType(par);
            }
            else if (!strcmp(simulation, "ship"))
            {
                type = new ShipType(par);
            }
            else if (!strcmp(simulation, "soldierold"))
            {
                type = new ManType(par);
            }
            else if (!strcmp(simulation, "soldier"))
            {
                type = new ManType(par);
            }
            else if (!strcmp(simulation, "helicopter"))
            {
                type = new HelicopterType(par);
            }
            else if (!strcmp(simulation, "parachute"))
            {
                type = new ParachuteType(par);
            }
            else if (!strcmp(simulation, "airplane"))
            {
                type = new AirplaneType(par);
            }
            else if (!strcmp(simulation, "lasertarget"))
            {
                type = new LaserTargetType(par);
            }
            if (!strcmp(simulation, "house"))
            {
                type = new BuildingType(par);
            }
            else if (!strcmp(simulation, "thing"))
            {
                type = new ThingType(par);
            }
            else if (!strcmp(simulation, "thingeffect"))
            {
                type = new ThingType(par);
            }
            else if (!strcmp(simulation, "cameratarget"))
            {
                Fail("cameratarget obsolete");
                type = new BuildingType(par);
            }
            else if (!strcmp(simulation, "church"))
            {
                type = new ChurchType(par);
            }
            else if (!strcmp(simulation, "fire"))
            {
                type = new BuildingType(par);
            }
            else if (!strcmp(simulation, "forest"))
            {
                type = new BuildingType(par);
            }
            else if (!strcmp(simulation, "seagull"))
            {
                type = new EntityAITypePlain(par);
            }
            else if (!strcmp(simulation, "camera"))
            {
                type = new EntityAITypePlain(par);
                /*
                else if( !strcmp(simulation,"flag") ) type=new EntityType(par);
                else if( !strcmp(simulation,"detector") ) type=new EntityType(par);
                else if( !strcmp(simulation,"detectorflag") ) type=new EntityType(par);
                */
            }
            else if (!strcmp(simulation, "flagcarrier"))
            {
                type = new EntityAITypePlain(par);
            }
            else if (!strcmp(simulation, "fountain"))
            {
                type = new FountainType(par);
            }
            else if (!strcmp(simulation, "invisible"))
            {
                type = new InvisibleVehicleType(par);
            }
            PoseidonAssert(type);
            if (type)
            {
                PoseidonAssert(!type->IsAbstract());
                type->Load(cfg);
                // Log("New public type %s (%s)",name,(const char *)type->GetDisplayName());
                return Add(type);
            }
        }
        type = new EntityAIType(&cfg);
        type->Load(cfg);
        PoseidonAssert(type->IsAbstract());
        PoseidonAssert(dynamic_cast<EntityAIType*>(type));
        // Log("New private type %s (%s)",name,(const char *)type->GetDisplayName());
        return Add(type);
    }
    if ((Pars >> "CfgAmmo").FindEntry(name))
    {
        const ParamEntry& cfg = Pars >> "CfgAmmo" >> name;
        AmmoType* aType = new AmmoType(&cfg);
        type = aType;
        type->Load(cfg);
        // aType->InitShape();
        //  new types
        return Add(type);
    }
    if ((Pars >> "CfgNonAIVehicles").FindEntry(name))
    {
        const ParamEntry& cfg = Pars >> "CfgNonAIVehicles" >> name;
        RString simulation = cfg >> "simulation";

        const ParamEntry* par = &cfg;
        simulation.Lower();
        if (!strcmp(simulation, "proxyweapon"))
        {
            type = new ProxyWeaponType(par);
        }
        else if (!strcmp(simulation, "proxysecweapon"))
        {
            type = new ProxyWeaponType(par);
        }
        else if (!strcmp(simulation, "proxyhandgun"))
        {
            type = new ProxyWeaponType(par);
        }
        else if (!strcmp(simulation, "streetlamp"))
        {
            type = new StreetLampType(par);
#if SUPPORT_RANDOM_SHAPES
            else if (!strcmp(simulation, "randomshape")) type = new RandomShapeType(par);
#endif
        }
        else if (!strcmp(simulation, "proxycrew"))
        {
            type = new ProxyCrewType(par);
        }
        else
        {
            type = new EntityType(par);
        }

        type->Load(cfg);
        // type->InitShape();
        //  new types
        return Add(type);
    }
    // Fail("Type");
    // RptF("Unknown type name %s",name);
    return -1;
}

static Ref<LODShapeWithShadow> TempShape(RString shapeName)
{
    if (shapeName.GetLength() > 0)
    {
        return Shapes.New(shapeName, false, true);
    }
    return nullptr;
}

bool CheckAccess(const ParamEntry& entry)
{
    if (!GWorld->CheckAddon(entry))
    {
        RString message = LocalizeString(IDS_MSG_ADDON_MISSING);
        bool first = true;
        if (first)
        {
            first = false;
        }
        else
        {
            message = message + RString(", ");
        }
        message = message + entry.GetOwner();
        WarningMessage(message);
        return false;
    }
    return true;
}

//! return false if function should really fail
bool CheckAccessCreate(const ParamEntry& entry)
{
    if (CheckAccess(entry))
    {
        return true;
    }
    // type cannot be accessed
    // what now?
    // if we are network client, we will continue
    // we will always continue
    // in any mode there should be some reaction to error message that was displayed
    /*
    if (GWorld->GetMode()==GModeNetware)
    {
        // if unaccessible entry is detected on server
        // we want server admin to fix issue
        // we will therefore not allow creating given unit
        if (GetNetworkManager().IsServer())
        {
            return false;
        }
    }
    else
    {
        // singleplayer
        // we would like to detect if we are in mission editor
        // if not, we may want to continue
        // we continue for now, but we may want to be more strict in future
    }
    */
    return true;
}

EntityAI* NewVehicle(EntityAIType* type, RString shapeName, bool fullCreate)
{
    // check if type can be accessed
    if (!CheckAccessCreate(type->GetParamEntry()))
    {
        return nullptr;
    }

    RString simName = type->_simName;
    EntityAI* v = nullptr;

    // Crewed/AI simulations dereference the type's shape during construction (Man,
    // Car, Tank, ...). An empty model="" in config leaves the type shapeless, so
    // refuse to create such a vehicle (broken config) rather than crash on the null
    // shape. Genuinely shapeless sims (e.g. "invisible") are not in this list.
    RString configuredModel = type->GetParamEntry() >> "model";
    if ((configuredModel.GetLength() == 0 || type->GetShapeName().GetLength() == 0) &&
        VehicleSimulationRequiresModel(simName))
    {
        LOG_ERROR(World, "Cannot create '{}': simulation '{}' requires a model but model is empty",
                  (const char*)type->GetName(), (const char*)simName);
        return nullptr;
    }

    type->VehicleAddRef();
    if (!type->GetShape() && VehicleSimulationRequiresModel(simName))
    {
        LOG_ERROR(World, "Cannot create '{}': simulation '{}' requires a loadable model", (const char*)type->GetName(),
                  (const char*)simName);
        type->VehicleRelease();
        return nullptr;
    }

    if (!stricmp(simName, "tank"))
    {
        v = new TankWithAI(type, nullptr);
    }
    else if (!stricmp(simName, "soldierold"))
    {
        v = new Soldier(type, fullCreate);
    }
    else if (!stricmp(simName, "soldier"))
    {
        v = new Soldier(type, fullCreate);
    }
    else if (!stricmp(simName, "car"))
    {
        v = new Car(type, nullptr);
    }
    else if (!stricmp(simName, "motorcycle"))
    {
        v = new Motorcycle(type, nullptr);
    }
    else if (!stricmp(simName, "helicopter"))
    {
        v = new HelicopterAuto(type, nullptr);
    }
    else if (!stricmp(simName, "parachute"))
    {
        v = new ParachuteAuto(type, nullptr);
    }
    else if (!stricmp(simName, "airplane"))
    {
        v = new AirplaneAuto(type, nullptr);
    }
    else if (!stricmp(simName, "ship"))
    {
        v = new ShipWithAI(type, nullptr);
    }
    else if (!stricmp(simName, "lasertarget"))
    {
        v = new LaserTarget(type);
    }
    else if (!stricmp(simName, "flagcarrier"))
    {
        v = new FlagCarrier(type);
    }
    else if (!stricmp(simName, "thing"))
    {
        v = new Thing(type);
    }
    else if (!stricmp(simName, "invisible"))
    {
        v = new InvisibleVehicle(type);
    }
    else if (!stricmp(simName, "fire"))
    {
        v = new Fireplace(type);
    }
    else if (!stricmp(simName, "house"))
    {
        v = new Building(type, -1, TempShape(shapeName));
    }
    else if (!stricmp(simName, "cameratarget"))
    {
        Fail("cameratarget obsolete");
        v = new Building(type, -1, TempShape(shapeName));
    }
    else if (!stricmp(simName, "fountain"))
    {
        v = new Fountain(type, -1, TempShape(shapeName));
    }
    else if (!stricmp(simName, "church"))
    {
        v = new Church(type, -1, TempShape(shapeName));
    }
    else
    {
        LOG_ERROR(Physics, "Unknown vehicle type: {} {}", (const char*)type->GetName(), (const char*)simName);
        v = nullptr;
    }
    type->VehicleRelease();
    /*
    LOG_DEBUG(Physics,
        "Vehicle created %x:%s,%s",
        v,(const char *)v->GetDebugName(),(const char *)v->GetShape()->Name()
    );
    */
    return v;
}

EntityAI* NewVehicle(RString typeName, RString shapeName, bool fullCreate)
{
    // create AI vehicle
    // RString simName = Pars >> "CfgVehicles" >> typeName >> "simulation";
    EntityType* vType = VehicleTypes.New(typeName);
    EntityAIType* aiType = dynamic_cast<EntityAIType*>(vType);

    // FIX: better handling of invalid crew specification
    if (!aiType)
    {
        Foundation::ErrorMessage(Foundation::EMError, "Bad vehicle type %s", (const char*)typeName);
        return nullptr;
    }
    EntityAI* v = NewVehicle(aiType, shapeName, fullCreate);

    if (v)
    {
        if (v->Object::GetType() == Primary)
        {
            v->SetType(TypeVehicle);
        }
    }
    return v;
}

Object* NewNonAIVehicleQuiet(RString typeName, RString shapeName, bool fullCreate)
{
    // create AI vehicle
    Object* v = nullptr;
    // RString simName = Pars >> "CfgVehicles" >> typeName >> "simulation";
    EntityType* type = VehicleTypes.New(typeName);

    if (!type)
    {
        // LOG_DEBUG(Physics, "No non-AI type {}",(const char *)typeName);
        // RptF("Type: %s",(const char *)typeName);
        return nullptr;
    }

    // some types must be catched before trying to create EntityAI
    RString simName = type->_simName;
    simName.Lower();

    if (!stricmp(simName, "thingeffect"))
    {
        ThingType* tType = dynamic_cast<ThingType*>(type);
        if (tType)
        {
            type->VehicleAddRef();
            v = new ThingEffectLight(tType);
            type->VehicleRelease();
            return v;
        }
    }

    EntityAIType* vType = dynamic_cast<EntityAIType*>(type);
    if (vType)
    {
        return NewVehicle(vType, shapeName, fullCreate);
    }

    AmmoType* aType = dynamic_cast<AmmoType*>(type);
    if (aType)
    {
        return NewShot(nullptr, aType, nullptr);
    }

    /*
    Ref<LODShapeWithShadow> shape=nullptr;
    if( shapeName.GetLength()>0 )
    {
        shape = Shapes.New(shapeName,false,true);
    }
    */

    type->VehicleAddRef();
    if (!stricmp(simName, "detector"))
    {
        v = new Detector(type, -1);
    }
    else if (!strcmp(simName, "flag"))
    {
        v = new Flag(type, -1);
    }
    else if (!strcmp(simName, "smokesource"))
    {
        v = new SmokeSourceVehicle();
    }
    else if (!strcmp(simName, "explosion"))
    {
        v = new Explosion(TempShape(shapeName));
    }
    else if (!strcmp(simName, "crater"))
    {
        v = new Crater(TempShape(shapeName), type);
    }
    else if (!strcmp(simName, "crateronvehicle"))
    {
        v = new CraterOnVehicle(TempShape(shapeName), type);
    }
    else if (!strcmp(simName, "slop"))
    {
        v = new Slop(TempShape(shapeName), type, M4Identity);
    }
    else if (!strcmp(simName, "smoke"))
    {
        v = new Smoke(TempShape(shapeName), type, 0.5);
    }
    else if (!strcmp(simName, "streetlamp"))
    {
        v = new StreetLamp(TempShape(shapeName), dynamic_cast<StreetLampType*>(type), -1);
    }
    else if (!strcmp(simName, "seagull"))
    {
        v = new SeaGullAuto(nullptr);
    }
    else if (!strcmp(simName, "camera"))
    {
        v = new CameraVehicle();
    }
    else if (!strcmp(simName, "dynamicsound"))
    {
        v = new DynSoundSource(nullptr);
    }
    else if (!strcmp(simName, "objectdestructed"))
    {
        v = new ObjectDestructed(TempShape(shapeName));
    }
    else if (!strcmp(simName, "proxyweapon"))
    {
        v = new ProxyWeapon(type, TempShape(shapeName));
    }
    else if (!strcmp(simName, "proxysecweapon"))
    {
        v = new ProxySecWeapon(type, TempShape(shapeName));
    }
    else if (!strcmp(simName, "proxyhandgun"))
    {
        v = new ProxyHandGun(type, TempShape(shapeName));
#if SUPPORT_RANDOM_SHAPES
        else if (!strcmp(simName, "randomshape")) v = new RandomShape(static_cast<RandomShapeType*>(type), -1);
#endif
    }
    else if (!strcmp(simName, "proxycrew"))
    {
        v = new ProxyCrew(type);
    }
    else if (!strcmp(simName, "alwaysshow"))
    {
        v = new ObjectTyped(type->GetShape(), type, -1);
    }
    else if (!strcmp(simName, "maverickweapon"))
    {
        v = new ObjectTyped(type->GetShape(), type, -1);
    }
    else if (!strcmp(simName, "scud"))
    {
        v = new ObjectTyped(type->GetShape(), type, -1);
    }
    else
    {
        LOG_DEBUG(Physics, "Unknown vehicle simulation.");
        LOG_DEBUG(Physics, "type {}: {}", (const char*)typeName, (const char*)simName);
        v = nullptr;
    }
    type->VehicleRelease();
    return v;
}

Vehicle* NewNonAIVehicle(RString typeName, RString shapeName, bool fullCreate)
{
    Object* obj = NewNonAIVehicleQuiet(typeName, shapeName, fullCreate);
    Vehicle* v = dyn_cast<Vehicle>(obj);
    if (!v)
    {
        RptF("Cannot create non-ai vehicle %s,%s", (const char*)typeName, (const char*)shapeName);
        Ref<Object> temp = obj; // guarantee destroying Ref (it may be the only one)
    }
    else
    {
        if (v->Object::GetType() == Primary)
        {
            v->SetType(TypeVehicle);
        }
    }
    return v;
}

Object* NewObject(RString typeName, RString shapeName)
{
    // object created this way are considered temporary
    Object* v = NewNonAIVehicleQuiet(typeName, "", true);
    if (v)
    {
        v->SetType(Temporary);
        return v;
    }
    // create plain object with given shape
    Ref<LODShapeWithShadow> shape = Shapes.New(shapeName, false, true);
    Object* obj = new ObjectPlain(shape, -1);
    obj->SetType(Temporary);
    return obj;
}

} // namespace Poseidon
