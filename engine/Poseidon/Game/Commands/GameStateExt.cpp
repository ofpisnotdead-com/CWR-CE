
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <Poseidon/World/Scene/Fireplace.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Car.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Air/Airplane.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <ctype.h>
#include <time.h>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using Poseidon::Foundation::FindEnumName;

using namespace Poseidon;
namespace Poseidon
{
GameValue ParticleDrop(const GameState* state, GameValuePar oper1);
} // namespace Poseidon

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/Preproc.h>

#include <Poseidon/Foundation/Platform/VersionNo.h>

#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

DEFINE_FAST_ALLOCATOR(GameDataObject)

RString GameDataObject::GetText() const
{
    if (_value == nullptr)
    {
        return "<nullptr-object>";
    }
    return _value->GetDebugName();
}

bool GameDataObject::IsEqualTo(const GameData* data) const
{
    const Object* val1 = GetObject();
    const Object* val2 = static_cast<const GameDataObject*>(data)->GetObject();
    return val1 == val2;
}

LSError GameDataObject::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));
    PARAM_CHECK(ar.SerializeRef("value", _value, 1))
    return LSOK;
}

DEFINE_FAST_ALLOCATOR(GameDataGroup)

RString GameDataGroup::GetText() const
{
    if (_value == nullptr)
    {
        return "<nullptr-group>";
    }
    return _value->GetDebugName();
}

bool GameDataGroup::IsEqualTo(const GameData* data) const
{
    const AIGroup* val1 = GetGroup();
    const AIGroup* val2 = static_cast<const GameDataGroup*>(data)->GetGroup();
    return val1 == val2;
}

LSError GameDataGroup::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));
    PARAM_CHECK(ar.SerializeRef("value", _value, 1))
    return LSOK;
}

DEFINE_FAST_ALLOCATOR(GameDataSide)

RString GameDataSide::GetText() const
{
    return FindEnumName(_value);
}

bool GameDataSide::IsEqualTo(const GameData* data) const
{
    const TargetSide val1 = GetSide();
    const TargetSide val2 = static_cast<const GameDataSide*>(data)->GetSide();
    return val1 == val2;
}

LSError GameDataSide::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));
    PARAM_CHECK(ar.SerializeEnum("value", _value, 1));
    return LSOK;
}

ConfigFileEntries GFileEntries;

int ConfigFileEntries::Add(const ConfigFileEntry& item)
{
    for (int i = 0; i < Size(); i++)
    {
        if (Get(i).cls == nullptr)
        {
            PoseidonAssert(Get(i).refCount == 0);
            Set(i) = item;
            return i;
        }
    }
    return base::Add(item);
}

static void FileEntryAddRef(ParamClass* cls)
{
    PoseidonAssert(cls);
    const ParamClass* file = cls->GetFile();
    for (int i = 0; i < GFileEntries.Size(); i++)
    {
        if (GFileEntries[i].cls == file)
        {
            GFileEntries[i].refCount++;
            break;
        }
    }
}

static void FileEntryRelease(ParamClass* cls)
{
    PoseidonAssert(cls);
    const ParamClass* file = cls->GetFile();
    for (int i = 0; i < GFileEntries.Size(); i++)
    {
        if (GFileEntries[i].cls == file)
        {
            if (--GFileEntries[i].refCount <= 0)
            {
                for (int j = 0; j < GFileEntries.Size(); j++)
                {
                    if (GFileEntries[j].cls && GFileEntries[j].cls->GetFile() == file)
                    {
                        GFileEntries[j].cls = nullptr;
                        PoseidonAssert(GFileEntries[j].refCount == 0);
                    }
                }
                delete static_cast<const ParamFile*>(file);
            }
            break;
        }
    }
}

GameFileType::GameFileType(const GameFileType& src)
{
    readOnly = src.readOnly;
    _index = -1;
    SetIndex(src.GetIndex());
}

void GameFileType::operator=(const GameFileType& src)
{
    readOnly = src.readOnly;
    SetIndex(src.GetIndex());
}

void GameFileType::SetIndex(int index)
{
    if (_index >= 0)
    {
        ParamClass* cls = GFileEntries[_index].cls;
        if (cls)
        {
            FileEntryRelease(cls);
        }
    }

    _index = index;

    if (index >= 0)
    {
        ParamClass* cls = GFileEntries[index].cls;
        if (cls)
        {
            FileEntryAddRef(cls);
        }
    }
}

GameFileType::~GameFileType()
{
    if (_index >= 0)
    {
        ParamClass* cls = GFileEntries[_index].cls;
        if (cls)
        {
            FileEntryRelease(cls);
        }
    }
}

DEFINE_FAST_ALLOCATOR(GameDataFile)

RString GameDataFile::GetText() const
{
    if (_value.GetIndex() < 0)
    {
        return "No file";
    }
    ParamClass* entry = GFileEntries[_value.GetIndex()].cls;
    if (!entry)
    {
        return "Destructed file";
    }
    return entry->GetName();
}

bool GameDataFile::IsEqualTo(const GameData* data) const
{
    const GameFileType val1 = GetFile();
    const GameFileType val2 = static_cast<const GameDataFile*>(data)->GetFile();
    return val1.GetIndex() == val2.GetIndex();
}

LSError GameDataFile::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));
    // do not serialize
    if (ar.IsLoading())
    {
        _value.SetIndex(-1);
        _value.readOnly = false;
    }

    return LSOK;
}

const GameType GameObjectOrGroup(GameObject | GameGroup);
const GameType GameObjectOrArray(GameObject | GameArray);

GameData* CreateGameDataObject()
{
    return new GameDataObject();
}
GameData* CreateGameDataGroup()
{
    return new GameDataGroup();
}
GameData* CreateGameDataSide()
{
    return new GameDataSide();
}
GameData* CreateGameDataFile()
{
    return new GameDataFile();
}

namespace Poseidon
{
GameValue CreateGameObject(Object* obj)
{
    return GameValueExt(obj);
}
} // namespace Poseidon

#define OBJECT_NULL GameValueExt((Object*)nullptr)
#define GROUP_NULL GameValueExt((AIGroup*)nullptr)

Object* GetObject(GameValuePar oper)
{
    if (oper.GetNil())
    {
        return nullptr;
    }
    if (oper.GetType() == GameObject)
    {
        return static_cast<GameDataObject*>(oper.GetData())->GetObject();
    }
    if (oper.GetType() == GameGroup)
    {
        AIGroup* grp = static_cast<GameDataGroup*>(oper.GetData())->GetGroup();
        if (!grp)
        {
            return nullptr;
        }
        AIUnit* leader = grp->Leader();
        if (!leader)
        {
            return nullptr;
        }
        return leader->GetPerson();
    }
    return nullptr;
}

AIGroup* GetGroup(GameValuePar oper)
{
    if (oper.GetNil())
    {
        return nullptr;
    }
    if (oper.GetType() == GameGroup)
    {
        return static_cast<GameDataGroup*>(oper.GetData())->GetGroup();
    }
    if (oper.GetType() == GameObject)
    {
        Object* obj = static_cast<GameDataObject*>(oper.GetData())->GetObject();
        EntityAI* vai = dyn_cast<EntityAI>(obj);
        if (!vai)
        {
            return nullptr;
        }
        AIUnit* unit = vai->CommanderUnit();
        if (!unit)
        {
            return nullptr;
        }
        return unit->GetGroup();
    }
    return nullptr;
}

Target* FindTargetReveal(AIGroup* grp, EntityAI* veh)
{
    Target* tgt = grp->FindTarget(veh);
    if (tgt)
    {
        return tgt;
    }
    return grp->AddTarget(veh, 0.3, 0.3, 3.0f);
}

#define TABLE_COMMAND(xxx, Xxx, RightType)                                                              \
    GameOperator(GameNothing, "command" #xxx, function, VehCommand##Xxx, GameObjectOrArray, RightType), \
        GameOperator(GameNothing, "do" #xxx, function, VehDo##Xxx, GameObjectOrArray, RightType),

#define TABLE_COMMAND_S(xxx, Xxx)                                                  \
    GameFunction(GameNothing, "command" #xxx, VehCommand##Xxx, GameObjectOrArray), \
        GameFunction(GameNothing, "do" #xxx, VehDo##Xxx, GameObjectOrArray),

bool OpenScript(QIFStreamB& in, RString name);

GameValue ScriptExecute(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ScriptExit(const GameState* state);
GameValue ScriptGoto(const GameState* state, GameValuePar oper1);

GameValue PoolAddWeapon(const GameState* state, GameValuePar oper1);
GameValue PoolAddMagazine(const GameState* state, GameValuePar oper1);
GameValue PoolGetWeapons(const GameState* state, GameValuePar oper1);
GameValue PoolSetWeapons(const GameState* state, GameValuePar oper1);
GameValue PoolClearWeapons(const GameState* state);
GameValue PoolClearMagazines(const GameState* state);
GameValue PoolQueryWeapons(const GameState* state, GameValuePar oper1);
GameValue PoolQueryMagazines(const GameState* state, GameValuePar oper1);

GameValue ShowCinemaBorder(const GameState* state, GameValuePar oper1);

// Forward declarations for split compilation units
// Nular functions
GameValue Benchmark(const GameState* state);
GameValue CadetMode(const GameState* state);
GameValue CameraOn(const GameState* state);
GameValue ConfigListNames(const GameState* state);
GameValue ConfigNew(const GameState* state);
GameValue DayTime(const GameState* state);
GameValue EnableEndDialog(const GameState* state);
GameValue EndGame(const GameState* state);
GameValue ForceEnd(const GameState* state);
GameValue GameTime(const GameState* state);
GameValue GetAcceleratedTime(const GameState* state);
GameValue GetMusicVolume(const GameState* state);
GameValue GetSoundVolume(const GameState* state);
GameValue GrpNull(const GameState* state);
GameValue IsCheatsEnabled(const GameState* state);
GameValue IsCompassShown(const GameState* state);
GameValue IsDialog(const GameState* state);
GameValue IsGPSShown(const GameState* state);
GameValue IsMapShown(const GameState* state);
GameValue IsNotepadShown(const GameState* state);
GameValue IsServer(const GameState* state);
GameValue IsJIP(const GameState* state);
GameValue ServerPause(const GameState* state);
GameValue ServerResume(const GameState* state);
GameValue IsWalkieTalkieShown(const GameState* state);
GameValue IsWarrantShown(const GameState* state);
GameValue IsWatchShown(const GameState* state);
GameValue MapAnimClear(const GameState* state);
GameValue MapAnimCommit(const GameState* state);
GameValue MapAnimDone(const GameState* state);
GameValue MapHeight(const GameState* state);
GameValue MapLeftTopPos(const GameState* state);
GameValue MapWidth(const GameState* state);
GameValue MissionName(const GameState* state);
GameValue MissionStart(const GameState* state);
GameValue ObjNull(const GameState* state);
GameValue Player(const GameState* state);
GameValue PlayerClearInjured(const GameState* state);
GameValue PoolClearMagazines(const GameState* state);
GameValue PoolClearWeapons(const GameState* state);
GameValue SaveGame(const GameState* state);
GameValue ScriptExit(const GameState* state);
GameValue SideCivilian(const GameState* state);
GameValue SideEast(const GameState* state);
GameValue SideEnemy(const GameState* state);
GameValue SideFriendly(const GameState* state);
GameValue SideLogic(const GameState* state);
GameValue SideResistance(const GameState* state);
GameValue SideWest(const GameState* state);
GameValue UsedVersion(const GameState* state);
GameValue VoiceLanguage(const GameState* state);
GameValue WorldName(const GameState* state);
GameValue WorldSize(const GameState* state);

// Unary functions
GameValue BriefingOnGear(const GameState* state, GameValuePar oper1);
GameValue BriefingOnGroup(const GameState* state, GameValuePar oper1);
GameValue BriefingOnNotes(const GameState* state, GameValuePar oper1);
GameValue BriefingOnPlan(const GameState* state, GameValuePar oper1);
GameValue ButtonGetAction(const GameState* state, GameValuePar oper1);
GameValue ButtonSetAction(const GameState* state, GameValuePar oper1);
GameValue CamCommited(const GameState* state, GameValuePar oper1);
GameValue CamDestroy(const GameState* state, GameValuePar oper1);
GameValue CenterCreate(const GameState* state, GameValuePar oper1);
GameValue CenterDelete(const GameState* state, GameValuePar oper1);
GameValue ConfigLoad(const GameState* state, GameValuePar oper1);
GameValue CtrlEnable(const GameState* state, GameValuePar oper1);
GameValue CtrlEnabled(const GameState* state, GameValuePar oper1);
GameValue CtrlGetText(const GameState* state, GameValuePar oper1);
GameValue CtrlSetText(const GameState* state, GameValuePar oper1);
GameValue CtrlShow(const GameState* state, GameValuePar oper1);
GameValue CtrlVisible(const GameState* state, GameValuePar oper1);
GameValue CutObj(const GameState* state, GameValuePar oper1);
GameValue CutRsc(const GameState* state, GameValuePar oper1);
GameValue CutText(const GameState* state, GameValuePar oper1);
GameValue DBG_Screenshot(const GameState* state, GameValuePar oper1);
GameValue DBG_SwitchLandscape(const GameState* state, GameValuePar oper1);
GameValue DebugShow(const GameState* state, GameValuePar oper1);
GameValue DeleteIdentity(const GameState* state, GameValuePar oper1);
GameValue DeleteStatus(const GameState* state, GameValuePar oper1);
GameValue DialogClose(const GameState* state, GameValuePar oper1);
GameValue DialogCreate(const GameState* state, GameValuePar oper1);
GameValue DisableUserInput(const GameState* state, GameValuePar oper1);
GameValue EnableRadio(const GameState* state, GameValuePar oper1);
GameValue EstimatedTimeLeft(const GameState* state, GameValuePar oper1);
GameValue GameTitleObj(const GameState* state, GameValuePar oper1);
GameValue GameTitleRsc(const GameState* state, GameValuePar oper1);
GameValue GameTitleText(const GameState* state, GameValuePar oper1);
GameValue GetLaserTarget(const GameState* state, GameValuePar oper1);
GameValue GetNearestObject(const GameState* state, GameValuePar oper1);
GameValue GetNearestObjectByDistance(const GameState* state, GameValuePar oper1);
GameValue GetNetIdObj(const GameState* state, GameValuePar oper1);
GameValue GetNetworkId(const GameState* state, GameValuePar oper1);
GameValue GetObjFromNetId(const GameState* state, GameValuePar oper1);
GameValue GetObject(const GameState* state, GameValuePar oper1);
GameValue GetUnitById(const GameState* state, GameValuePar oper1);
GameValue GroupCreate(const GameState* state, GameValuePar oper1);
GameValue GroupDelete(const GameState* state, GameValuePar oper1);
GameValue GrpCombatMode(const GameState* state, GameValuePar oper1);
GameValue GrpFormation(const GameState* state, GameValuePar oper1);
GameValue GrpIsNull(const GameState* state, GameValuePar oper1);
GameValue GrpLeader(const GameState* state, GameValuePar oper1);
GameValue GrpSpeedMode(const GameState* state, GameValuePar oper1);
GameValue GrpUnits(const GameState* state, GameValuePar oper1);
GameValue GuardedPointCreate(const GameState* state, GameValuePar oper1);
GameValue LBAdd(const GameState* state, GameValuePar oper1);
GameValue LBClear(const GameState* state, GameValuePar oper1);
GameValue LBDelete(const GameState* state, GameValuePar oper1);
GameValue LBGetColor(const GameState* state, GameValuePar oper1);
GameValue LBGetCurSel(const GameState* state, GameValuePar oper1);
GameValue LBGetData(const GameState* state, GameValuePar oper1);
GameValue LBGetPicture(const GameState* state, GameValuePar oper1);
GameValue LBGetSize(const GameState* state, GameValuePar oper1);
GameValue LBGetText(const GameState* state, GameValuePar oper1);
GameValue LBGetValue(const GameState* state, GameValuePar oper1);
GameValue LBSetColor(const GameState* state, GameValuePar oper1);
GameValue LBSetCurSel(const GameState* state, GameValuePar oper1);
GameValue LBSetData(const GameState* state, GameValuePar oper1);
GameValue LBSetPicture(const GameState* state, GameValuePar oper1);
GameValue LBSetValue(const GameState* state, GameValuePar oper1);
GameValue MapAnimAdd(const GameState* state, GameValuePar oper1);
GameValue MapForce(const GameState* state, GameValuePar oper1);
GameValue MapOnSingleClick(const GameState* state, GameValuePar oper1);
GameValue MarkerCreate(const GameState* state, GameValuePar oper1);
GameValue MarkerDelete(const GameState* state, GameValuePar oper1);
GameValue MarkerGetColor(const GameState* state, GameValuePar oper1);
GameValue MarkerGetDir(const GameState* state, GameValuePar oper1);
GameValue MarkerGetPos(const GameState* state, GameValuePar oper1);
GameValue MarkerGetSize(const GameState* state, GameValuePar oper1);
GameValue MarkerGetType(const GameState* state, GameValuePar oper1);
GameValue ObjAlive(const GameState* state, GameValuePar oper1);
GameValue ObjBehaviour(const GameState* state, GameValuePar oper1);
GameValue ObjCanFire(const GameState* state, GameValuePar oper1);
GameValue ObjCanMove(const GameState* state, GameValuePar oper1);
GameValue ObjCanStand(const GameState* state, GameValuePar oper1);
GameValue ObjCaptive(const GameState* state, GameValuePar oper1);
GameValue ObjClearMagazineCargo(const GameState* state, GameValuePar oper1);
GameValue ObjClearWeaponCargo(const GameState* state, GameValuePar oper1);
GameValue ObjCommander(const GameState* state, GameValuePar oper1);
GameValue ObjDriver(const GameState* state, GameValuePar oper1);
GameValue ObjExperience(const GameState* state, GameValuePar oper1);
GameValue ObjFlee(const GameState* state, GameValuePar oper1);
GameValue ObjFuel(const GameState* state, GameValuePar oper1);
GameValue ObjGetAllMagazines(const GameState* state, GameValuePar oper1);
GameValue ObjGetAllWeapons(const GameState* state, GameValuePar oper1);
GameValue ObjGetDammage(const GameState* state, GameValuePar oper1);
GameValue ObjGetDir(const GameState* state, GameValuePar oper1);
GameValue ObjGetFlag(const GameState* state, GameValuePar oper1);
GameValue ObjGetFlagOwner(const GameState* state, GameValuePar oper1);
GameValue ObjGetHitpointsNames(const GameState* state, GameValuePar oper1);
GameValue ObjGetMass(const GameState* state, GameValuePar oper1);
GameValue ObjGetMove(const GameState* state, GameValuePar oper1);
GameValue ObjGetNearestBuilding(const GameState* state, GameValuePar oper1);
GameValue ObjGetPos(const GameState* state, GameValuePar oper1);
GameValue ObjGetPosASL(const GameState* state, GameValuePar oper1);
GameValue ObjGetPrimaryWeapon(const GameState* state, GameValuePar oper1);
GameValue ObjGetScore(const GameState* state, GameValuePar oper1);
GameValue ObjGetScudState(const GameState* state, GameValuePar oper1);
GameValue ObjGetSecondaryWeapon(const GameState* state, GameValuePar oper1);
GameValue ObjGetSkill(const GameState* state, GameValuePar oper1);
GameValue ObjGetSpeed(const GameState* state, GameValuePar oper1);
GameValue ObjGetTurretDirAndElev(const GameState* state, GameValuePar oper1);
GameValue ObjGetType(const GameState* state, GameValuePar oper1);
GameValue ObjGetVectorDir(const GameState* state, GameValuePar oper1);
GameValue ObjGetVectorUp(const GameState* state, GameValuePar oper1);
GameValue ObjGetVelocity(const GameState* state, GameValuePar oper1);
GameValue ObjGroup(const GameState* state, GameValuePar oper1);
GameValue ObjGroupLeader(const GameState* state, GameValuePar oper1);
GameValue ObjGunner(const GameState* state, GameValuePar oper1);
GameValue ObjHandsHit(const GameState* state, GameValuePar oper1);
GameValue ObjInflamed(const GameState* state, GameValuePar oper1);
GameValue ObjIsLocal(const GameState* state, GameValuePar oper1);
GameValue ObjIsNull(const GameState* state, GameValuePar oper1);
GameValue ObjLeader(const GameState* state, GameValuePar oper1);
GameValue ObjLightSwitched(const GameState* state, GameValuePar oper1);
GameValue ObjList(const GameState* state, GameValuePar oper1);
GameValue ObjListIn(const GameState* state, GameValuePar oper1);
GameValue ObjLocked(const GameState* state, GameValuePar oper1);
GameValue ObjMagazinesArray(const GameState* state, GameValuePar oper1);
GameValue ObjName(const GameState* state, GameValuePar oper1);
GameValue ObjRemoveAllWeapons(const GameState* state, GameValuePar oper1);
GameValue ObjSide(const GameState* state, GameValuePar oper1);
GameValue ObjSomeAmmo(const GameState* state, GameValuePar oper1);
GameValue ObjStopped(const GameState* state, GameValuePar oper1);
GameValue ObjUnassignVehicle(const GameState* state, GameValuePar oper1);
GameValue ObjVehicle(const GameState* state, GameValuePar oper1);
GameValue ObjWeaponsFromPool(const GameState* state, GameValuePar oper1);
GameValue PlayMusic(const GameState* state, GameValuePar oper1);
GameValue PlaySound(const GameState* state, GameValuePar oper1);
GameValue SoundLength(const GameState* state, GameValuePar oper1);
GameValue PlayersNumber(const GameState* state, GameValuePar oper1);
GameValue PoolAddMagazine(const GameState* state, GameValuePar oper1);
GameValue PoolAddWeapon(const GameState* state, GameValuePar oper1);
GameValue PoolGetWeapons(const GameState* state, GameValuePar oper1);
GameValue PoolQueryMagazines(const GameState* state, GameValuePar oper1);
GameValue PoolQueryWeapons(const GameState* state, GameValuePar oper1);
GameValue PoolSetWeapons(const GameState* state, GameValuePar oper1);
GameValue LoadMission(const GameState* state, GameValuePar oper1);
GameValue OnPlayerConnected(const GameState* state, GameValuePar oper1);
GameValue OnPlayerDisconnected(const GameState* state, GameValuePar oper1);
GameValue PublicExec(const GameState* state, GameValuePar oper1);
GameValue RemoteExec(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue RemoteExecCall(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue RemoteExecRemove(const GameState* state, GameValuePar oper1);
GameValue SaveMission(const GameState* state, GameValuePar oper1);
GameValue PublicVariable(const GameState* state, GameValuePar oper1);
GameValue RequiredVersion(const GameState* state, GameValuePar oper1);
GameValue SaveVar(const GameState* state, GameValuePar oper1);
GameValue ScriptGoto(const GameState* state, GameValuePar oper1);
GameValue SetAcceleratedTime(const GameState* state, GameValuePar oper1);
GameValue SetDate(const GameState* state, GameValuePar oper1);
GameValue SetTerrainGrid(const GameState* state, GameValuePar oper1);
GameValue SetViewDistance(const GameState* state, GameValuePar oper1);
GameValue ShellCreate(const GameState* state, GameValuePar oper1);
GameValue ShowCinemaBorder(const GameState* state, GameValuePar oper1);
GameValue ShowCompass(const GameState* state, GameValuePar oper1);
GameValue ShowGPS(const GameState* state, GameValuePar oper1);
GameValue ShowHint(const GameState* state, GameValuePar oper1);
GameValue ShowHintC(const GameState* state, GameValuePar oper1);
GameValue ShowHintCadet(const GameState* state, GameValuePar oper1);
GameValue ShowMap(const GameState* state, GameValuePar oper1);
GameValue ShowNotepad(const GameState* state, GameValuePar oper1);
GameValue ShowWalkieTalkie(const GameState* state, GameValuePar oper1);
GameValue ShowWarrant(const GameState* state, GameValuePar oper1);
GameValue ShowWatch(const GameState* state, GameValuePar oper1);
GameValue SkipDayTime(const GameState* state, GameValuePar oper1);
GameValue SliderGetPosition(const GameState* state, GameValuePar oper1);
GameValue SliderGetRange(const GameState* state, GameValuePar oper1);
GameValue SliderGetSpeed(const GameState* state, GameValuePar oper1);
GameValue SliderSetPosition(const GameState* state, GameValuePar oper1);
GameValue SliderSetRange(const GameState* state, GameValuePar oper1);
GameValue SliderSetSpeed(const GameState* state, GameValuePar oper1);
GameValue StrFormat(const GameState* state, GameValuePar oper1);
GameValue StrLocalize(const GameState* state, GameValuePar oper1);
GameValue StrSize(const GameState* state, GameValuePar oper1);
GameValue StrSub(const GameState* state, GameValuePar oper1);
GameValue StringLoad(const GameState* state, GameValuePar oper1);
GameValue StringPreprocess(const GameState* state, GameValuePar oper1);
GameValue LogInfo(const GameState* state, GameValuePar oper1);
GameValue TextDebugLog(const GameState* state, GameValuePar oper1);
GameValue TextLog(const GameState* state, GameValuePar oper1);
GameValue TriggerCreate(const GameState* state, GameValuePar oper1);
GameValue AddMPReportEvent(const GameState* state, GameValuePar oper1);
GameValue AddMPReportFooter(const GameState* state, GameValuePar oper1);
GameValue AddMPReportHeader(const GameState* state, GameValuePar oper1);
GameValue GetMPReportInjuries(const GameState* state, GameValuePar oper1);
GameValue GetMPReportKilled(const GameState* state, GameValuePar oper1);
GameValue GetMPReportKills(const GameState* state, GameValuePar oper1);
GameValue VehCommandStop(const GameState* state, GameValuePar oper1);
GameValue VehDelete(const GameState* state, GameValuePar oper1);
GameValue VehDoStop(const GameState* state, GameValuePar oper1);
GameValue VehDone(const GameState* state, GameValuePar oper1);
GameValue VehIsEngineOn(const GameState* state, GameValuePar oper1);
GameValue WaypointDelete(const GameState* state, GameValuePar oper1);
GameValue WpGetPos(const GameState* state, GameValuePar oper1);

// Binary functions
GameValue BoolCmpEq(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue BoolCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamCommand(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamCommit(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetDive(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetFOV(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetFOVRange(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetRelPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetTargetObj(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CamSetTargetVec(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue CenterSetFriend(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ClassAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ClassOpen(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ConfigSave(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue EffectSetCamera(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue EffectSetCondition(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue EffectSetMusic(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue EffectSetSound(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue EffectSetTitle(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpAllowFleeing(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpKnowsAbout(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpLeaveVehicle(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpLockWP(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpMove(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpReveal(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetBehaviour(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetCombatMode(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetFormDir(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetFormation(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue GrpSetSpeedMode(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListAllowGetIn(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListCountEnemy(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListCountFriendly(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListCountSide(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListCountType(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListCountUnknown(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListJoin(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ListOrderGetIn(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetBrush(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetColor(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetDir(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetShape(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetSize(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetText(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue MarkerSetType(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddExperience(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddMagazine(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddMagazineCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddMagazinePrecise(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddScore(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAddWeaponCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAllowDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAmmoArray(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAnimate(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAnimationPhase(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAssignAsCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAssignAsCommander(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAssignAsDriver(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjAssignAsGunner(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjCameraEffect(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjChangeModel(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjDisableAI(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjDistance(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjFire(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjFireEx(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGetBuildingPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGetSelectionDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGlobalChat(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGlobalRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGroupChat(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjGroupRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjHasWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjIn(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjInflame(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjLand(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjLoadIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjLoadStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjLock(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjMoveInCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjMoveInCommander(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjMoveInDriver(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjMoveInGunner(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjMuzzleReloadTime(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjPlayMove(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjRemoveMagazine(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjRemoveMagazines(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjRemoveWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSaveIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSaveStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSay(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSelectWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetAmmoCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetCaptive(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetDir(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFace(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFaceAnimation(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFlagOwner(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFlagSide(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFlagTexture(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFlyingHeight(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFuel(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetFuelCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetMimic(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetPosASL(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetRepairCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetSelectionDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetSkill(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetTexture(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetUnitPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetVectorDir(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetVectorDirectionAndUp(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetVectorUp(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSetVelocity(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSideChat(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSideRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjStop(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSwitchCamera(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSwitchLight(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjSwitchMove(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjVehicleChat(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ObjVehicleRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamArrayAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamArrayVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamArrayWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubArrayAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubArrayVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubArrayWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamSubWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ParamWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ScriptExecute(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetFog(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetMusicVolume(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetObjectiveStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetOvercast(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetRadioMessage(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetRain(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SetSoundVolume(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SideCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue SideCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerAttachObject(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerAttachVehicle(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetActivation(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetArea(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetStatements(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetText(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetTimeout(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSetType(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue TriggerSynchronize(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue UnitCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ValueAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ValueGet(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehAddEventHandler(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehAddUserAction(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCommandFire(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCommandFollow(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCommandMove(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCommandTarget(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCommandWatch(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehDoFire(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehDoFollow(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehDoMove(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehDoTarget(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehDoWatch(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehEngineOn(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehProcessAction(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehRemoveAllEventHandlers(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehRemoveEventHandler(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue VehRemoveUserAction(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointAttachObject(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointAttachVehicle(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetBehaviour(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetCombatMode(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetDescription(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetFormation(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetHousePos(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetPosition(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetScript(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetSpeed(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetStatements(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetTimeout(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSetType(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointShow(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WaypointSynchronize(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue WpSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2);

static const GameNular* GetExtNular(int& count)
{
    static const GameNular ExtNular[] = {
        GameNular(GameObject, "player", Player),
        GameNular(GameObject, "objNull", ObjNull),
        GameNular(GameGroup, "grpNull", GrpNull),
        GameNular(GameScalar, "time", GameTime),
        GameNular(GameScalar, "dayTime", DayTime),
        GameNular(GameBool, "cadetMode", CadetMode),
        GameNular(GameScalar, "benchmark", Benchmark),
        GameNular(GameScalar, "accTime", GetAcceleratedTime),

        GameNular(GameBool, "shownMap", IsMapShown),
        GameNular(GameBool, "shownWatch", IsWatchShown),
        GameNular(GameBool, "shownCompass", IsCompassShown),
        GameNular(GameBool, "shownRadio", IsWalkieTalkieShown),
        GameNular(GameBool, "shownPad", IsNotepadShown),
        GameNular(GameBool, "shownWarrant", IsWarrantShown),
        GameNular(GameBool, "shownGps", IsGPSShown),

        GameNular(GameSide, "west", SideWest),
        GameNular(GameSide, "east", SideEast),
        GameNular(GameSide, "civilian", SideCivilian),
        GameNular(GameSide, "resistance", SideResistance),
        GameNular(GameSide, "sideLogic", SideLogic),
        GameNular(GameSide, "sideEnemy", SideEnemy),
        GameNular(GameSide, "sideFriendly", SideFriendly),

        GameNular(GameNothing, "saveGame", SaveGame),
        GameNular(GameNothing, "exit", ScriptExit),

        GameNular(GameNothing, "enableEndDialog", EnableEndDialog),
        GameNular(GameNothing, "forceEnd", ForceEnd),
        GameNular(GameNothing, "endGame", EndGame),
        GameNular(GameScalar, "musicVolume", GetMusicVolume),
        GameNular(GameScalar, "soundVolume", GetSoundVolume),

        GameNular(GameNothing, "mapAnimClear", MapAnimClear),
        GameNular(GameNothing, "mapAnimCommit", MapAnimCommit),
        GameNular(GameBool, "mapAnimDone", MapAnimDone),

        GameNular(GameNothing, "clearWeaponPool", Poseidon::PoolClearWeapons),
        GameNular(GameNothing, "clearMagazinePool", Poseidon::PoolClearMagazines),

        GameNular(GameNothing, "cheatsEnabled", IsCheatsEnabled),

        GameNular(GameBool, "dialog", IsDialog),

        GameNular(GameObject, "cameraOn", CameraOn),

        GameNular(GameFile, "newConfig", ConfigNew),
        GameNular(GameArray, "listConfigNames", ConfigListNames),
        GameNular(GameString, "missionName", MissionName),
        GameNular(GameArray, "missionStart", MissionStart),
        GameNular(GameString, "getWorld", WorldName),
        GameNular(GameString, "voiceLanguage", VoiceLanguage),
        GameNular(GameBool, "isServer", IsServer),
        GameNular(GameBool, "isJIP", IsJIP),
        GameNular(GameNothing, "serverPause", ServerPause),
        GameNular(GameNothing, "serverResume", ServerResume),

        GameNular(GameScalar, "worldSize", WorldSize),
        GameNular(GameScalar, "mapWidth", MapWidth),
        GameNular(GameScalar, "mapHeight", MapHeight),
        GameNular(GameArray, "mapLeftTopPos", MapLeftTopPos),

        // remove player "injured" status
        // it is annoying in MP game (especially in CTI) that injured status won't disappear
        // if accidentally activated "injured" channel
        // todo: maybe this shouldn't be designed as a script command
        GameNular(GameNothing, "playerClearInjured", PlayerClearInjured),
        GameNular(GameScalar, "usedVersion", UsedVersion),
    };
    count = sizeof(ExtNular) / sizeof(*ExtNular);
    return ExtNular;
}

static const GameFunction* GetExtUnary(int& count)
{
    static const GameFunction ExtUnary[] = {
        GameFunction(GameBool, "isNull", ObjIsNull, GameObject),
        GameFunction(GameBool, "isNull", GrpIsNull, GameGroup),
        GameFunction(GameBool, "alive", ObjAlive, GameObject),
        GameFunction(GameBool, "local", ObjIsLocal, GameObject),
        GameFunction(GameBool, "requiredVersion", RequiredVersion, GameString),
        GameFunction(GameArray, "getPos", ObjGetPos, GameObject),
        GameFunction(GameArray, "position", ObjGetPos, GameObject),
        GameFunction(GameArray, "getPosASL", ObjGetPosASL, GameObject),
        GameFunction(GameScalar, "getDir", ObjGetDir, GameObject),
        GameFunction(GameArray, "getTurretDirAndElev", ObjGetTurretDirAndElev, GameObject),
        GameFunction(GameScalar, "direction", ObjGetDir, GameObject),
        GameFunction(GameArray, "velocity", ObjGetVelocity, GameObject),
        GameFunction(GameScalar, "getDammage", ObjGetDammage, GameObject),
        GameFunction(GameScalar, "getDamage", ObjGetDammage, GameObject), // one-M alias (cf. setDammage/setDamage)
        GameFunction(GameScalar, "damage", ObjGetDammage, GameObject),
        GameFunction(GameScalar, "speed", ObjGetSpeed, GameObject),
        GameFunction(GameArray, "getMarkerPos", MarkerGetPos, GameString),
        GameFunction(GameArray, "markerPos", MarkerGetPos, GameString),
        GameFunction(GameString, "getMarkerType", MarkerGetType, GameString),
        GameFunction(GameString, "markerType", MarkerGetType, GameString),
        GameFunction(GameArray, "getMarkerSize", MarkerGetSize, GameString),
        GameFunction(GameArray, "markerSize", MarkerGetSize, GameString),
        GameFunction(GameString, "getMarkerColor", MarkerGetColor, GameString),
        GameFunction(GameString, "markerColor", MarkerGetColor, GameString),
        GameFunction(GameArray, "getWPPos", WpGetPos, GameArray),
        GameFunction(GameArray, "waypointPosition", WpGetPos, GameArray),
        GameFunction(GameObject, "nearestBuilding", ObjGetNearestBuilding, GameObject),
        GameFunction(GameObject, "nearestObject", GetNearestObject, GameArray),
        GameFunction(GameBool, "canMove", ObjCanMove, GameObject),
        GameFunction(GameBool, "canFire", ObjCanFire, GameObject),
        GameFunction(GameBool, "canStand", ObjCanStand, GameObject),
        GameFunction(GameScalar, "handsHit", ObjHandsHit, GameObject),
        GameFunction(GameBool, "fleeing", ObjFlee, GameObject),
        GameFunction(GameBool, "someAmmo", ObjSomeAmmo, GameObject),
        GameFunction(GameScalar, "fuel", ObjFuel, GameObject),
        GameFunction(GameScalar, "rating", ObjExperience, GameObject),
        GameFunction(GameScalar, "score", ObjGetScore, GameObject),
        GameFunction(GameObject, "formLeader", ObjLeader, GameObject),
        GameFunction(GameObject, "leader", ObjGroupLeader, GameObject),
        GameFunction(GameObject, "vehicle", ObjVehicle, GameObject),
        GameFunction(GameObject, "driver", ObjDriver, GameObject),
        GameFunction(GameObject, "commander", ObjCommander, GameObject),
        GameFunction(GameObject, "gunner", ObjGunner, GameObject),

        GameFunction(GameObject, "leader", GrpLeader, GameGroup),
        GameFunction(GameGroup, "group", ObjGroup, GameObject),
        GameFunction(GameArray, "units", GrpUnits, GameGroup),
        GameFunction(GameArray, "units", GrpUnits, GameObject),

        GameFunction(GameArray, "crew", ObjListIn, GameObject),

        GameFunction(GameObject, "flagOwner", ObjGetFlagOwner, GameObject),
        GameFunction(GameObject, "flag", ObjGetFlag, GameObject),

        GameFunction(GameBool, "inflamed", ObjInflamed, GameObject),
        GameFunction(GameString, "lightIsOn", ObjLightSwitched, GameObject),

        GameFunction(GameScalar, "scudState", ObjGetScudState, GameObject),

        GameFunction(GameArray, "list", ObjList, GameObject),
        GameFunction(GameSide, "side", ObjSide, GameObjectOrGroup),
        GameFunction(GameString, "name", ObjName, GameObject),
        GameFunction(GameString, "behaviour", ObjBehaviour, GameObject),
        GameFunction(GameString, "behavior", ObjBehaviour, GameObject), // US-spelling alias
        GameFunction(GameString, "combatMode", GrpCombatMode, GameObjectOrGroup),
        GameFunction(GameString, "formation", GrpFormation, GameObjectOrGroup),
        GameFunction(GameString, "speedMode", GrpSpeedMode, GameObjectOrGroup),
        GameFunction(GameNothing, "titleCut", CutText, GameArray),

        GameFunction(GameNothing, "cutText", CutText, GameArray),
        GameFunction(GameNothing, "cutRsc", CutRsc, GameArray),
        GameFunction(GameNothing, "cutObj", CutObj, GameArray),

        GameFunction(GameNothing, "titleText", GameTitleText, GameArray),
        GameFunction(GameNothing, "titleRsc", GameTitleRsc, GameArray),
        GameFunction(GameNothing, "titleObj", GameTitleObj, GameArray),

        GameFunction(GameNothing, "playSound", PlaySound, GameString),
        GameFunction(GameScalar, "soundLength", SoundLength, GameString),
        GameFunction(GameNothing, "playMusic", PlayMusic, GameString),
        GameFunction(GameNothing, "playMusic", PlayMusic, GameArray),

        GameFunction(GameBool, "locked", ObjLocked, GameObject),
        GameFunction(GameBool, "stopped", ObjStopped, GameObject),
        GameFunction(GameBool, "captive", ObjCaptive, GameObject),

        GameFunction(GameNothing, "unassignVehicle", ObjUnassignVehicle, GameObject),

        GameFunction(GameNothing, "removeAllWeapons", ObjRemoveAllWeapons, GameObject),

        GameFunction(GameNothing, "clearWeaponCargo", ObjClearWeaponCargo, GameObject),
        GameFunction(GameNothing, "clearMagazineCargo", ObjClearMagazineCargo, GameObject),

        GameFunction(GameNothing, "deleteVehicle", VehDelete, GameObject),

        GameFunction(GameNothing, "showMap", ShowMap, GameBool),
        GameFunction(GameNothing, "showWatch", ShowWatch, GameBool),
        GameFunction(GameNothing, "showCompass", ShowCompass, GameBool),
        GameFunction(GameNothing, "showRadio", ShowWalkieTalkie, GameBool),
        GameFunction(GameNothing, "showPad", ShowNotepad, GameBool),
        GameFunction(GameNothing, "showWarrant", ShowWarrant, GameBool),
        GameFunction(GameNothing, "showGps", ShowGPS, GameBool),
        GameFunction(GameNothing, "enableRadio", EnableRadio, GameBool),

        GameFunction(GameNothing, "showCinemaBorder", ShowCinemaBorder, GameBool),
        GameFunction(GameNothing, "disableUserInput", DisableUserInput, GameBool),
        GameFunction(GameNothing, "loadMission", LoadMission, GameString),
        GameFunction(GameNothing, "onPlayerConnected", OnPlayerConnected, GameString),
        GameFunction(GameNothing, "onPlayerDisconnected", OnPlayerDisconnected, GameString),
        GameFunction(GameNothing, "publicVariable", PublicVariable, GameString),
        GameFunction(GameNothing, "saveMission", SaveMission, GameString),
        GameFunction(GameNothing, "publicVariableArray", PublicVariable, GameString),
        GameFunction(GameNothing, "publicVariableString", PublicVariable, GameString),

        GameFunction(GameNothing, "hint", ShowHint, GameString),
        GameFunction(GameNothing, "hintCadet", ShowHintCadet, GameString),
        GameFunction(GameNothing, "hintC", ShowHintC, GameString),

        GameFunction(GameString, "format", StrFormat, GameArray),
        GameFunction(GameString, "localize", StrLocalize, GameString),

        GameFunction(GameNothing, "skipTime", SkipDayTime, GameScalar),
        GameFunction(GameNothing, "setViewDistance", SetViewDistance, GameScalar),
        GameFunction(GameNothing, "setTerrainGrid", SetTerrainGrid, GameScalar),

        GameFunction(GameNothing, "goto", ScriptGoto, GameString),

        GameFunction(GameNothing, "textLog", TextLog, GameVoid),
        GameFunction(GameNothing, "debugLog", TextDebugLog, GameVoid),
        GameFunction(GameNothing, "logInfo", LogInfo, GameVoid),

        GameFunction(GameNothing, "camDestroy", CamDestroy, GameObject),
        GameFunction(GameBool, "camCommitted", CamCommited, GameObject),

        GameFunction(GameBool, "unitReady", VehDone, GameObjectOrArray),

        GameFunction(GameNothing, "saveVar", SaveVar, GameString),

        GameFunction(GameNothing, "setAccTime", SetAcceleratedTime, GameScalar),

        GameFunction(GameNothing, "forceMap", MapForce, GameBool),
        GameFunction(GameNothing, "mapAnimAdd", MapAnimAdd, GameArray),

        GameFunction(GameNothing, "estimatedTimeLeft", EstimatedTimeLeft, GameScalar),

        GameFunction(GameBool, "createDialog", DialogCreate, GameString),
        GameFunction(GameNothing, "closeDialog", DialogClose, GameScalar),

        GameFunction(GameBool, "ctrlVisible", CtrlVisible, GameScalar),
        GameFunction(GameBool, "ctrlEnabled", CtrlEnabled, GameScalar),
        GameFunction(GameNothing, "ctrlShow", CtrlShow, GameArray),
        GameFunction(GameNothing, "ctrlEnable", CtrlEnable, GameArray),

        GameFunction(GameString, "ctrlText", CtrlGetText, GameScalar),
        GameFunction(GameNothing, "ctrlSetText", CtrlSetText, GameArray),

        GameFunction(GameString, "buttonAction", ButtonGetAction, GameScalar),
        GameFunction(GameNothing, "buttonSetAction", ButtonSetAction, GameArray),

        GameFunction(GameScalar, "lbSize", LBGetSize, GameScalar),
        GameFunction(GameScalar, "lbCurSel", LBGetCurSel, GameScalar),
        GameFunction(GameNothing, "lbSetCurSel", LBSetCurSel, GameArray),
        GameFunction(GameNothing, "lbClear", LBClear, GameScalar),
        GameFunction(GameScalar, "lbAdd", LBAdd, GameArray),
        GameFunction(GameNothing, "lbDelete", LBDelete, GameArray),
        GameFunction(GameString, "lbText", LBGetText, GameArray),
        GameFunction(GameString, "lbData", LBGetData, GameArray),
        GameFunction(GameNothing, "lbSetData", LBSetData, GameArray),
        GameFunction(GameScalar, "lbValue", LBGetValue, GameArray),
        GameFunction(GameNothing, "lbSetValue", LBSetValue, GameArray),
        GameFunction(GameString, "lbPicture", LBGetPicture, GameArray),
        GameFunction(GameNothing, "lbSetPicture", LBSetPicture, GameArray),
        GameFunction(GameArray, "lbColor", LBGetColor, GameArray),
        GameFunction(GameNothing, "lbSetColor", LBSetColor, GameArray),

        GameFunction(GameScalar, "sliderPosition", SliderGetPosition, GameScalar),
        GameFunction(GameNothing, "sliderSetPosition", SliderSetPosition, GameArray),
        GameFunction(GameArray, "sliderRange", SliderGetRange, GameScalar),
        GameFunction(GameNothing, "sliderSetRange", SliderSetRange, GameArray),
        GameFunction(GameArray, "sliderSpeed", SliderGetSpeed, GameScalar),
        GameFunction(GameNothing, "sliderSetSpeed", SliderSetSpeed, GameArray),

        GameFunction(GameNothing, "addWeaponPool", Poseidon::PoolAddWeapon, GameArray),
        GameFunction(GameNothing, "addMagazinePool", Poseidon::PoolAddMagazine, GameArray),

        GameFunction(GameNothing, "putWeaponPool", Poseidon::PoolGetWeapons, GameObject),
        GameFunction(GameNothing, "pickWeaponPool", Poseidon::PoolSetWeapons, GameObject),

        GameFunction(GameScalar, "queryWeaponPool", Poseidon::PoolQueryWeapons, GameString),
        GameFunction(GameScalar, "queryMagazinePool", Poseidon::PoolQueryMagazines, GameString),

        GameFunction(GameNothing, "drop", Poseidon::ParticleDrop, GameArray),

        GameFunction(GameNothing, "onBriefingPlan", BriefingOnPlan, GameString),
        GameFunction(GameNothing, "onBriefingNotes", BriefingOnNotes, GameString),
        GameFunction(GameNothing, "onBriefingGear", BriefingOnGear, GameString),
        GameFunction(GameNothing, "onBriefingGroup", BriefingOnGroup, GameString),

        GameFunction(GameNothing, "onMapSingleClick", MapOnSingleClick, GameString),

        GameFunction(GameNothing, "fillWeaponsFromPool", ObjWeaponsFromPool, GameObject),

        GameFunction(GameScalar, "skill", ObjGetSkill, GameObject),

        GameFunction(GameString, "primaryWeapon", ObjGetPrimaryWeapon, GameObject),
        GameFunction(GameString, "secondaryWeapon", ObjGetSecondaryWeapon, GameObject),
        GameFunction(GameArray, "weapons", ObjGetAllWeapons, GameObject),
        GameFunction(GameArray, "magazines", ObjGetAllMagazines, GameObject),

        GameFunction(GameObject, "object", GetObject, GameScalar),

        GameFunction(GameBool, "deleteStatus", DeleteStatus, GameString),
        GameFunction(GameBool, "deleteIdentity", DeleteIdentity, GameString),
        GameFunction(GameNothing, "publicExec", PublicExec, GameString),
        GameFunction(GameNothing, "remoteExecRemove", RemoteExecRemove, GameString),

        GameFunction(GameFile, "loadConfig", ConfigLoad, GameString),

        GameFunction(GameNothing, "VBS_addHeader", AddMPReportHeader, GameString),
        GameFunction(GameNothing, "VBS_addEvent", AddMPReportEvent, GameString),
        GameFunction(GameNothing, "VBS_addFooter", AddMPReportFooter, GameString),

        GameFunction(GameArray, "VBS_kills", GetMPReportKills, GameString),
        GameFunction(GameArray, "VBS_killed", GetMPReportKilled, GameString),
        GameFunction(GameArray, "VBS_injuries", GetMPReportInjuries, GameString),

        GameFunction(GameString, "loadFile", StringLoad, GameString),
        GameFunction(GameString, "preprocessFile", StringPreprocess, GameString),

        GameFunction(GameScalar, "playersNumber", PlayersNumber, GameSide),

        GameFunction(GameBool, "isEngineOn", VehIsEngineOn, GameObject),

        GameFunction(GameString, "netId", GetNetIdObj, GameObject),
        GameFunction(GameObject, "objectFromNetId", GetObjFromNetId, GameString),
        // Array-based netId for compatibility with 2.01-era missions.
        GameFunction(GameArray, "NetworkId", GetNetworkId, GameObject),
        GameFunction(GameObject, "UnitById", GetUnitById, GameArray),
        TABLE_COMMAND_S(stop, Stop)

#if _ENABLE_CHEATS
            GameFunction(GameNothing, "DBG_switchLandscape", DBG_SwitchLandscape, GameString),
        GameFunction(GameNothing, "DBG_screenshot", DBG_Screenshot, GameString),
#endif

        GameFunction(GameNothing, "setDate", SetDate, GameArray),

        GameFunction(GameSide, "createCenter", CenterCreate, GameSide),
        GameFunction(GameNothing, "deleteCenter", CenterDelete, GameSide),
        GameFunction(GameGroup, "createGroup", GroupCreate, GameSide),
        GameFunction(GameNothing, "deleteGroup", GroupDelete, GameGroup),

        GameFunction(GameString, "createMarker", MarkerCreate, GameArray),
        GameFunction(GameNothing, "deleteMarker", MarkerDelete, GameString),
        GameFunction(GameScalar, "getMarkerDir", MarkerGetDir, GameString),
        // todo: add getMarkerText/Shape/Brush
        // todo: markersMap is an AutoArray. Is it possible to realize it by std::map?

        GameFunction(GameObject, "createTrigger", TriggerCreate, GameArray),
        GameFunction(GameNothing, "createGuardedPoint", GuardedPointCreate, GameArray),

        GameFunction(GameNothing, "deleteWaypoint", WaypointDelete, GameArray),
        GameFunction(GameString, "getMove", ObjGetMove, GameObject),

        GameFunction(GameString, "typeOf", ObjGetType, GameObject),

        GameFunction(GameArray, "vectorUp", ObjGetVectorUp, GameObject),
        GameFunction(GameArray, "vectorDir", ObjGetVectorDir, GameObject),

        GameFunction(GameObject, "laserTarget", GetLaserTarget, GameObject),
        GameFunction(GameScalar, "getMass", ObjGetMass, GameObject),
        GameFunction(GameObject, "NearestObjectDistance", GetNearestObjectByDistance, GameArray),
        GameFunction(GameString, "substr", StrSub, GameArray),
        GameFunction(GameScalar, "sizeofstr", StrSize, GameString),
        // todo: add more parameters and adjust parameters to make some of them has default value
        GameFunction(GameObject, "createShell", ShellCreate, GameArray),
        GameFunction(GameArray, "magazinesArray", ObjMagazinesArray, GameObject),
        GameFunction(GameArray, "GetHitpointsNames", ObjGetHitpointsNames, GameObject),
        GameFunction(GameNothing, "showDebug", DebugShow, GameArray),
    };
    count = sizeof(ExtUnary) / sizeof(*ExtUnary);
    return ExtUnary;
}

static const GameOperator* GetExtBinary(int& count)
{
    static const GameOperator ExtBinary[] = {
        GameOperator(GameNothing, "setCaptive", function, ObjSetCaptive, GameObject, GameBool),
        GameOperator(GameNothing, "setIdentity", function, ObjSetIdentity, GameObject, GameString),
        GameOperator(GameNothing, "setFaceanimation", function, ObjSetFaceAnimation, GameObject, GameScalar),
        GameOperator(GameNothing, "setFace", function, ObjSetFace, GameObject, GameString),
        GameOperator(GameNothing, "setMimic", function, ObjSetMimic, GameObject, GameString),
        GameOperator(GameNothing, "setFlagTexture", function, ObjSetFlagTexture, GameObject, GameString),
        GameOperator(GameNothing, "setFlagSide", function, ObjSetFlagSide, GameObject, GameSide),
        GameOperator(GameNothing, "setFlagOwner", function, ObjSetFlagOwner, GameObject, GameObject),
        GameOperator(GameNothing, "remoteExec", function, RemoteExec, GameAny, GameString | GameArray),
        GameOperator(GameNothing, "remoteExecCall", function, RemoteExecCall, GameAny, GameString | GameArray),
        GameOperator(GameArray, "buildingPos", function, ObjGetBuildingPos, GameObject, GameScalar),
        GameOperator(GameNothing, "switchLight", function, ObjSwitchLight, GameObject, GameString),
        GameOperator(GameNothing, "inflame", function, ObjInflame, GameObject, GameBool),
        GameOperator(GameNothing, "addRating", function, ObjAddExperience, GameObject, GameScalar),
        GameOperator(GameNothing, "addScore", function, ObjAddScore, GameObject, GameScalar),
        GameOperator(GameScalar, "distance", function, ObjDistance, GameObject, GameObject),
        GameOperator(GameNothing, "setPos", function, ObjSetPos, GameObject, GameArray),
        GameOperator(GameNothing, "setPosASL", function, ObjSetPosASL, GameObject, GameArray),
        GameOperator(GameNothing, "setDir", function, ObjSetDir, GameObject, GameScalar),
        GameOperator(GameNothing, "setVelocity", function, ObjSetVelocity, GameObject, GameArray),
        GameOperator(GameNothing, "setFormDir", function, GrpSetFormDir, GameObjectOrGroup, GameScalar),
        GameOperator(GameNothing, "setDammage", function, ObjSetDammage, GameObject, GameScalar),
        GameOperator(GameNothing, "setDamage", function, ObjSetDammage, GameObject, GameScalar),
        GameOperator(GameNothing, "allowDammage", function, ObjAllowDammage, GameObject, GameBool),
        // Grammatically-correct one-M alias resolves to the same handler (cf. setDammage/setDamage).
        GameOperator(GameNothing, "allowDamage", function, ObjAllowDammage, GameObject, GameBool),
        GameOperator(GameNothing, "flyInHeight", function, ObjSetFlyingHeight, GameObject, GameScalar),
        GameOperator(GameNothing, "setMarkerPos", function, MarkerSetPos, GameString, GameArray),
        GameOperator(GameNothing, "setMarkerType", function, MarkerSetType, GameString, GameString),
        GameOperator(GameNothing, "setMarkerSize", function, MarkerSetSize, GameString, GameArray),
        GameOperator(GameNothing, "setMarkerColor", function, MarkerSetColor, GameString, GameString),
        GameOperator(GameNothing, "setWPPos", function, WpSetPos, GameArray, GameArray),
        GameOperator(GameBool, "in", function, ObjIn, GameObject, GameObject),
        GameOperator(GameScalar, "ammo", function, ObjAmmo, GameObject, GameString),
        GameOperator(GameBool, "hasWeapon", function, ObjHasWeapon, GameObject, GameString),
        GameOperator(GameNothing, "addWeapon", function, ObjAddWeapon, GameObject, GameString),
        GameOperator(GameNothing, "removeWeapon", function, ObjRemoveWeapon, GameObject, GameString),
        GameOperator(GameNothing, "addMagazine", function, ObjAddMagazine, GameObject, GameString),
        GameOperator(GameNothing, "removeMagazine", function, ObjRemoveMagazine, GameObject, GameString),
        GameOperator(GameNothing, "removeMagazines", function, ObjRemoveMagazines, GameObject, GameString),
        GameOperator(GameNothing, "selectWeapon", function, ObjSelectWeapon, GameObject, GameString),
        GameOperator(GameNothing, "fire", function, ObjFire, GameObject, GameString),
        GameOperator(GameNothing, "fire", function, ObjFireEx, GameObject, GameArray),
        GameOperator(GameScalar, "muzzleReloadTime", function, ObjMuzzleReloadTime, GameObject, GameString),
        GameOperator(GameNothing, "land", function, ObjLand, GameObject, GameString),
        GameOperator(GameScalar, "knowsAbout", function, GrpKnowsAbout, GameObjectOrGroup, GameObject),
        GameOperator(GameNothing, "say", function, ObjSay, GameObject, GameString),
        GameOperator(GameNothing, "say", function, ObjSay, GameObject, GameArray),
        GameOperator(GameNothing, "globalRadio", function, ObjGlobalRadio, GameObject, GameString),
        GameOperator(GameNothing, "sideRadio", function, ObjSideRadio, GameObjectOrArray, GameString),
        GameOperator(GameNothing, "groupRadio", function, ObjGroupRadio, GameObject, GameString),
        GameOperator(GameNothing, "vehicleRadio", function, ObjVehicleRadio, GameObject, GameString),
        GameOperator(GameNothing, "globalChat", function, ObjGlobalChat, GameObject, GameString),
        GameOperator(GameNothing, "sideChat", function, ObjSideChat, GameObjectOrArray, GameString),
        GameOperator(GameNothing, "groupChat", function, ObjGroupChat, GameObject, GameString),
        GameOperator(GameNothing, "vehicleChat", function, ObjVehicleChat, GameObject, GameString),
        GameOperator(GameNothing, "playMove", function, ObjPlayMove, GameObject, GameString),
        GameOperator(GameNothing, "switchMove", function, ObjSwitchMove, GameObject, GameString),
        GameOperator(GameNothing, "setRadioMsg", function, SetRadioMessage, GameScalar, GameString),
        GameOperator(GameBool, "==", function, ObjCmpE, GameObject, GameObject),
        GameOperator(GameBool, "!=", function, ObjCmpNE, GameObject, GameObject),
        GameOperator(GameBool, "==", function, GrpCmpE, GameGroup, GameGroup),
        GameOperator(GameBool, "!=", function, GrpCmpNE, GameGroup, GameGroup),
        GameOperator(GameBool, "==", function, SideCmpE, GameSide, GameSide),
        GameOperator(GameBool, "!=", function, SideCmpNE, GameSide, GameSide),
        // EQ/NE for comparing bool. Operator "==" and "!=" are improper because their results are bool
        // and operator has priority. Use explicitly function instead
        GameOperator(GameBool, "boolEq", function, BoolCmpEq, GameBool, GameBool),
        GameOperator(GameBool, "boolNe", function, BoolCmpNE, GameBool, GameBool),
        GameOperator(GameScalar, "countEnemy", function, ListCountEnemy, GameObject, GameArray),
        GameOperator(GameScalar, "countFriendly", function, ListCountFriendly, GameObject, GameArray),
        GameOperator(GameScalar, "countUnknown", function, ListCountUnknown, GameObject, GameArray),
        GameOperator(GameScalar, "countType", function, ListCountType, GameString, GameArray),
        GameOperator(GameScalar, "countSide", function, ListCountSide, GameSide, GameArray),
        GameOperator(GameNothing, "allowGetIn", function, ListAllowGetIn, GameArray, GameBool),
        GameOperator(GameNothing, "orderGetIn", function, ListOrderGetIn, GameArray, GameBool),
        GameOperator(GameNothing, "join", function, ListJoin, GameArray, GameObjectOrGroup),
        GameOperator(GameNothing, "move", function, GrpMove, GameObjectOrGroup, GameArray),
        GameOperator(GameNothing, "setGroupid", function, GrpSetIdentity, GameObjectOrGroup, GameArray),
        GameOperator(GameNothing, "setBehaviour", function, GrpSetBehaviour, GameObjectOrGroup, GameString),
        GameOperator(GameNothing, "setBehavior", function, GrpSetBehaviour, GameObjectOrGroup,
                     GameString), // US-spelling alias
        GameOperator(GameNothing, "setCombatMode", function, GrpSetCombatMode, GameObjectOrGroup, GameString),
        GameOperator(GameNothing, "setFormation", function, GrpSetFormation, GameObjectOrGroup, GameString),
        GameOperator(GameNothing, "setSpeedMode", function, GrpSetSpeedMode, GameObjectOrGroup, GameString),
        GameOperator(GameNothing, "setUnitPos", function, ObjSetUnitPos, GameObject, GameString),
        GameOperator(GameNothing, "lockWp", function, GrpLockWP, GameObjectOrGroup, GameBool),
        GameOperator(GameNothing, "lock", function, ObjLock, GameObject, GameBool),
        GameOperator(GameNothing, "stop", function, ObjStop, GameObject, GameBool),
        GameOperator(GameNothing, "disableAI", function, ObjDisableAI, GameObject, GameString),
        GameOperator(GameNothing, "assignAsCommander", function, ObjAssignAsCommander, GameObject, GameObject),
        GameOperator(GameNothing, "assignAsDriver", function, ObjAssignAsDriver, GameObject, GameObject),
        GameOperator(GameNothing, "assignAsGunner", function, ObjAssignAsGunner, GameObject, GameObject),
        GameOperator(GameNothing, "assignAsCargo", function, ObjAssignAsCargo, GameObject, GameObject),
        GameOperator(GameNothing, "leaveVehicle", function, GrpLeaveVehicle, GameObject, GameObject),
        GameOperator(GameNothing, "leaveVehicle", function, GrpLeaveVehicle, GameGroup, GameObject),
        GameOperator(GameNothing, "moveInCommander", function, ObjMoveInCommander, GameObject, GameObject),
        GameOperator(GameNothing, "moveInDriver", function, ObjMoveInDriver, GameObject, GameObject),
        GameOperator(GameNothing, "moveInGunner", function, ObjMoveInGunner, GameObject, GameObject),
        GameOperator(GameNothing, "moveInCargo", function, ObjMoveInCargo, GameObject, GameObject),
        GameOperator(GameNothing, "allowFleeing", function, GrpAllowFleeing, GameObjectOrGroup, GameScalar),
        GameOperator(GameNothing, "objStatus", function, SetObjectiveStatus, GameString, GameString),
        GameOperator(GameNothing, "exec", function, ScriptExecute, GameVoid, GameString),

        GameOperator(GameNothing, "setOvercast", function, SetOvercast, GameScalar, GameScalar),
        GameOperator(GameNothing, "setFog", function, SetFog, GameScalar, GameScalar),
        GameOperator(GameNothing, "setRain", function, SetRain, GameScalar, GameScalar),

        GameOperator(GameNothing, "setFuel", function, ObjSetFuel, GameObject, GameScalar),
        GameOperator(GameNothing, "setFuelCargo", function, ObjSetFuelCargo, GameObject, GameScalar),
        GameOperator(GameNothing, "setRepairCargo", function, ObjSetRepairCargo, GameObject, GameScalar),
        GameOperator(GameNothing, "setAmmoCargo", function, ObjSetAmmoCargo, GameObject, GameScalar),
        GameOperator(GameNothing, "addWeaponCargo", function, ObjAddWeaponCargo, GameObject, GameArray),
        GameOperator(GameNothing, "addMagazineCargo", function, ObjAddMagazineCargo, GameObject, GameArray),

        GameOperator(GameObject, "createVehicle", function, VehCreate, GameString, GameArray),
        GameOperator(GameNothing, "createUnit", function, UnitCreate, GameString, GameArray),

        GameOperator(GameNothing, "fadeMusic", function, SetMusicVolume, GameScalar, GameScalar),
        GameOperator(GameNothing, "fadeSound", function, SetSoundVolume, GameScalar, GameScalar),

        GameOperator(GameNothing, "cameraEffect", function, ObjCameraEffect, GameObject, GameArray),
        GameOperator(GameObject, "camCreate", function, CamCreate, GameString, GameArray),
        GameOperator(GameNothing, "camSetPos", function, CamSetPos, GameObject, GameArray),
        GameOperator(GameNothing, "camSetRelPos", function, CamSetRelPos, GameObject, GameArray),
        GameOperator(GameNothing, "camSetFov", function, CamSetFOV, GameObject, GameScalar),
        GameOperator(GameNothing, "camSetFovRange", function, CamSetFOVRange, GameObject, GameArray),

        GameOperator(GameNothing, "camSetDive", function, CamSetDive, GameObject, GameScalar),
        GameOperator(GameNothing, "camSetBank", function, CamSetDive, GameObject, GameScalar),
        GameOperator(GameNothing, "camSetDir", function, CamSetDive, GameObject, GameScalar),
        GameOperator(GameNothing, "camCommit", function, CamCommit, GameObject, GameScalar),
        GameOperator(GameNothing, "camSetTarget", function, CamSetTargetObj, GameObject, GameObject),
        GameOperator(GameNothing, "camSetTarget", function, CamSetTargetVec, GameObject, GameArray),
        GameOperator(GameNothing, "camCommand", function, CamCommand, GameObject, GameString),

        GameOperator(GameNothing, "switchCamera", function, ObjSwitchCamera, GameObject, GameString),

        TABLE_COMMAND(Move, Move, GameArray) TABLE_COMMAND(Watch, Watch, GameArray)
            TABLE_COMMAND(Watch, Watch, GameObject) TABLE_COMMAND(Target, Target, GameObject)
                TABLE_COMMAND(Follow, Follow, GameObject) TABLE_COMMAND(Fire, Fire, GameObject)

                    GameOperator(GameNothing, "action", function, VehProcessAction, GameObject, GameArray),
        GameOperator(GameNothing | GameScalar, "addAction", function, VehAddUserAction, GameObject, GameArray),
        GameOperator(GameNothing, "removeAction", function, VehRemoveUserAction, GameObject, GameScalar),
        GameOperator(GameNothing, "reveal", function, GrpReveal, GameObjectOrGroup, GameObject),

        GameOperator(GameNothing | GameScalar, "addEventHandler", function, VehAddEventHandler, GameObject, GameArray),
        GameOperator(GameNothing, "removeEventHandler", function, VehRemoveEventHandler, GameObject, GameArray),
        GameOperator(GameNothing, "removeAllEventHandlers", function, VehRemoveAllEventHandlers, GameObject,
                     GameString),

        GameOperator(GameNothing, "engineOn", function, VehEngineOn, GameObject, GameBool),

        GameOperator(GameBool, "saveStatus", function, ObjSaveStatus, GameObject, GameString),
        GameOperator(GameBool, "loadStatus", function, ObjLoadStatus, GameObject, GameString),

        GameOperator(GameBool, "saveIdentity", function, ObjSaveIdentity, GameObject, GameString),
        GameOperator(GameBool, "loadIdentity", function, ObjLoadIdentity, GameObject, GameString),

        GameOperator(GameNothing, "animate", function, ObjAnimate, GameObject, GameArray),
        GameOperator(GameScalar, "animationPhase", function, ObjAnimationPhase, GameObject, GameString),

        GameOperator(GameNothing, "setSkill", function, ObjSetSkill, GameObject, GameScalar),

        GameOperator(GameNothing, "setObjectTexture", function, ObjSetTexture, GameObject, GameArray),

        GameOperator(GameNothing, "saveConfig", function, ConfigSave, GameFile, GameString),
        GameOperator(GameFile, "openClass", function, ClassOpen, GameFile, GameString),
        GameOperator(GameFile, "addClass", function, ClassAdd, GameFile, GameString),
        GameOperator(GameVoid, "getValue", function, ValueGet, GameFile, GameString),
        GameOperator(GameNothing, "addValue", function, ValueAdd, GameFile, GameArray),

        GameOperator(GameNothing, "setFriend", function, CenterSetFriend, GameSide, GameArray),

        GameOperator(GameNothing, "setMarkerText", function, MarkerSetText, GameString, GameString),
        GameOperator(GameNothing, "setMarkerShape", function, MarkerSetShape, GameString, GameString),
        GameOperator(GameNothing, "setMarkerBrush", function, MarkerSetBrush, GameString, GameString),
        GameOperator(GameNothing, "setMarkerDir", function, MarkerSetDir, GameString, GameScalar),

        GameOperator(GameNothing, "setTriggerArea", function, TriggerSetArea, GameObject, GameArray),
        GameOperator(GameNothing, "setTriggerActivation", function, TriggerSetActivation, GameObject, GameArray),
        GameOperator(GameNothing, "setTriggerType", function, TriggerSetType, GameObject, GameString),
        GameOperator(GameNothing, "setTriggerTimeout", function, TriggerSetTimeout, GameObject, GameArray),
        GameOperator(GameNothing, "setTriggerText", function, TriggerSetText, GameObject, GameString),
        GameOperator(GameNothing, "setTriggerStatements", function, TriggerSetStatements, GameObject, GameArray),

        GameOperator(GameNothing, "triggerAttachObject", function, TriggerAttachObject, GameObject, GameScalar),
        GameOperator(GameNothing, "triggerAttachVehicle", function, TriggerAttachVehicle, GameObject, GameArray),

        GameOperator(GameNothing, "setEffectCondition", function, EffectSetCondition, GameObject | GameArray,
                     GameString),
        GameOperator(GameNothing, "setCameraEffect", function, EffectSetCamera, GameObject | GameArray, GameArray),
        GameOperator(GameNothing, "setSoundEffect", function, EffectSetSound, GameObject | GameArray, GameArray),
        GameOperator(GameNothing, "setMusicEffect", function, EffectSetMusic, GameObject | GameArray, GameString),
        GameOperator(GameNothing, "setTitleEffect", function, EffectSetTitle, GameObject | GameArray, GameArray),
        GameOperator(GameNothing, "synchronizeWaypoint", function, WaypointSynchronize, GameArray, GameArray),
        GameOperator(GameNothing, "synchronizeWaypoint", function, TriggerSynchronize, GameObject, GameArray),

        GameOperator(GameArray, "addWaypoint", function, WaypointAdd, GameGroup, GameArray),
        GameOperator(GameNothing, "setWaypointPosition", function, WaypointSetPosition, GameArray, GameArray),
        GameOperator(GameNothing, "setWaypointType", function, WaypointSetType, GameArray, GameString),
        GameOperator(GameNothing, "waypointAttachVehicle", function, WaypointAttachVehicle, GameArray, GameObject),
        GameOperator(GameNothing, "waypointAttachObject", function, WaypointAttachObject, GameArray, GameScalar),
        GameOperator(GameNothing, "setWaypointHousePosition", function, WaypointSetHousePos, GameArray, GameScalar),
        GameOperator(GameNothing, "setWaypointCombatMode", function, WaypointSetCombatMode, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointFormation", function, WaypointSetFormation, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointSpeed", function, WaypointSetSpeed, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointBehaviour", function, WaypointSetBehaviour, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointBehavior", function, WaypointSetBehaviour, GameArray,
                     GameString), // US-spelling alias
        GameOperator(GameNothing, "setWaypointDescription", function, WaypointSetDescription, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointStatements", function, WaypointSetStatements, GameArray, GameArray),
        GameOperator(GameNothing, "setWaypointScript", function, WaypointSetScript, GameArray, GameString),
        GameOperator(GameNothing, "setWaypointTimeout", function, WaypointSetTimeout, GameArray, GameArray),
        GameOperator(GameNothing, "showWaypoint", function, WaypointShow, GameArray, GameString),
        GameOperator(GameNothing, "setVectorUp", function, ObjSetVectorUp, GameObject, GameArray),
        GameOperator(GameNothing, "setVectorDir", function, ObjSetVectorDir, GameObject, GameArray),
        GameOperator(GameNothing, "setVectorDirAndUp", function, ObjSetVectorDirectionAndUp, GameObject, GameArray),

        GameOperator(GameAny, "GetAmmoParam", function, ParamAmmo, GameString, GameString),
        GameOperator(GameAny, "GetAmmoParamArray", function, ParamArrayAmmo, GameString, GameString),
        GameOperator(GameAny, "GetAmmoSubParam", function, ParamSubAmmo, GameArray, GameString),
        GameOperator(GameAny, "GetAmmoSubParamArray", function, ParamSubArrayAmmo, GameArray, GameString),
        GameOperator(GameAny, "GetWeaponParam", function, ParamWeapons, GameString, GameString),
        GameOperator(GameAny, "GetWeaponParamArray", function, ParamArrayWeapons, GameString, GameString),
        GameOperator(GameAny, "GetWeaponSubParam", function, ParamSubWeapons, GameArray, GameString),
        GameOperator(GameAny, "GetWeaponSubParamArray", function, ParamSubArrayWeapons, GameArray, GameString),
        GameOperator(GameAny, "GetVehicleParam", function, ParamVehicles, GameString, GameString),
        GameOperator(GameAny, "GetVehicleParamArray", function, ParamArrayVehicles, GameString, GameString),
        GameOperator(GameAny, "GetVehicleSubParam", function, ParamSubVehicles, GameArray, GameString),
        GameOperator(GameAny, "GetVehicleSubParamArray", function, ParamSubArrayVehicles, GameArray, GameString),
        // todo: Special entrance for UserActions/EventHandlers
        // todo: "Cfg..." can be parameter and thus support scripter read config info freely

        GameOperator(GameScalar, "ammoArray", function, ObjAmmoArray, GameObject, GameString),
        GameOperator(GameNothing, "addMagazinePrecise", function, ObjAddMagazinePrecise, GameObject, GameArray),
        GameOperator(GameScalar, "GetSelectionDammage", function, ObjGetSelectionDammage, GameObject, GameString),
        GameOperator(GameScalar, "getSelectionDamage", function, ObjGetSelectionDammage, GameObject,
                     GameString), // one-M alias
        GameOperator(GameNothing, "SetSelectionDammage", function, ObjSetSelectionDammage, GameObject, GameArray),
        GameOperator(GameNothing, "setSelectionDamage", function, ObjSetSelectionDammage, GameObject,
                     GameArray), // one-M alias
    };
    count = sizeof(ExtBinary) / sizeof(*ExtBinary);
    return ExtBinary;
}

#include <Poseidon/Foundation/Modules/Modules.hpp>

INIT_MODULE(GameStateExt, 2)
{
    // stringtable must be loaded
    GGameState.Init();

    GGameState.NewType("OBJECT", GameObject, CreateGameDataObject, LocalizeString(IDS_EVAL_TYPEOBJECT));
    GGameState.NewType("VECTOR", GameVector, nullptr, LocalizeString(IDS_EVAL_TYPEVECTOR));
    GGameState.NewType("TRANS", GameTrans, nullptr, LocalizeString(IDS_EVAL_TYPETRANS));
    GGameState.NewType("ORIENT", GameOrient, nullptr, LocalizeString(IDS_EVAL_TYPEORIENT));
    GGameState.NewType("SIDE", GameSide, CreateGameDataSide, LocalizeString(IDS_EVAL_TYPESIDE));
    GGameState.NewType("GROUP", GameGroup, CreateGameDataGroup, LocalizeString(IDS_EVAL_TYPEGROUP));
    GGameState.NewType("FILE", GameFile, CreateGameDataFile, "File");

    int extUnaryCount;
    const GameFunction* ExtUnary = GetExtUnary(extUnaryCount);
    for (int i = 0; i < extUnaryCount; i++)
    {
        GGameState.NewFunction(ExtUnary[i]);
    }

    int extBinaryCount;
    const GameOperator* ExtBinary = GetExtBinary(extBinaryCount);
    for (int i = 0; i < extBinaryCount; i++)
    {
        GGameState.NewOperator(ExtBinary[i]);
    }

    int extNularCount;
    const GameNular* ExtNular = GetExtNular(extNularCount);
    for (int i = 0; i < extNularCount; i++)
    {
        GGameState.NewNularOp(ExtNular[i]);
    }
};

// Out-of-line: these touch OLink<AIGroup>/<Object>, needing the complete types.
GameDataObject::GameDataObject() : _value(0) {}
GameDataObject::GameDataObject(GameObjectType value) : _value(value) {}
GameObjectType GameDataObject::GetObject() const
{
    return _value;
}
GameData* GameDataObject::Clone() const
{
    return new GameDataObject(*this);
}
GameDataGroup::GameDataGroup() : _value(0) {}
GameDataGroup::GameDataGroup(GameGroupType value) : _value(value) {}
GameGroupType GameDataGroup::GetGroup() const
{
    return _value;
}
GameData* GameDataGroup::Clone() const
{
    return new GameDataGroup(*this);
}
GameValueExt::GameValueExt(AIGroup* value)
{
    _data = new GameDataGroup(value);
}
GameValueExt::GameValueExt(Object* value)
{
    _data = new GameDataObject(value);
}
