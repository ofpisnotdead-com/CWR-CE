
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
#include <Poseidon/Core/Application.hpp>
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
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <ctype.h>
#include <time.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::GetEnumValue;

using namespace Poseidon;
namespace Poseidon
{
} // namespace Poseidon

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/Preproc.h>

#include <Poseidon/Foundation/Platform/VersionNo.h>

#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

namespace Poseidon
{
void CreatePath(RString);
void AddDeadIdentity(RString);
RString FindScript(RString name);
RString GetUserDirectory();
} // namespace Poseidon

static RString ConfigFullName(RString filename)
{
    if (filename.GetLength() == 0)
    {
        return RString();
    }

    // avoid path in filename
    if (strchr(filename, '\\'))
    {
        return RString();
    }
    if (strchr(filename, '/'))
    {
        return RString();
    }
    return Poseidon::GetUserDirectory() + RString("Config/") + filename;
}

static GameFileType GetFile(GameValuePar oper)
{
    PoseidonAssert(oper.GetType() == GameFile);
    return static_cast<GameDataFile*>(oper.GetData())->GetFile();
}

GameValue ConfigNew(const GameState* state)
{
    GameFileType file;
    file.SetIndex(GFileEntries.Add(new ParamFile));
    file.readOnly = false;
    return GameValueExt(file);
}

GameValue ConfigLoad(const GameState* state, GameValuePar oper1)
{
    GameFileType file;

    RString fullname = ConfigFullName(oper1);
    if (fullname.GetLength() > 0)
    {
        ParamFile* cfg = new ParamFile();
        if (cfg->Parse(fullname) == LSOK)
        {
            file.SetIndex(GFileEntries.Add(cfg));
        }
        else
        {
            delete cfg;
        }
    }
    return GameValueExt(file);
}

GameValue ConfigSave(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    RString fullname = ConfigFullName(oper2);
    if (fullname.GetLength() == 0)
    {
        return NOTHING;
    }

    GameFileType file = GetFile(oper1);
    if (file.GetIndex() < 0)
    {
        return NOTHING;
    }
    if (file.readOnly)
    {
        return NOTHING;
    }
    ParamClass* cfg = GFileEntries[file.GetIndex()].cls;
    if (!cfg)
    {
        return NOTHING;
    }
    if (cfg->GetFile() != cfg)
    {
        return NOTHING;
    }
    CreatePath(fullname);
    static_cast<ParamFile*>(cfg)->Save(fullname);

    return NOTHING;
}

GameValue ClassOpen(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameFileType file = GetFile(oper1);

    GameFileType result;
    result.readOnly = file.readOnly;

    if (file.GetIndex() < 0)
    {
        return GameValueExt(result);
    }
    ParamClass* cfg = GFileEntries[file.GetIndex()].cls;
    if (!cfg)
    {
        return GameValueExt(result);
    }

    RString name = oper2;
    ParamClass* cls = const_cast<ParamClass*>(cfg->GetClass(name));
    if (cls)
    {
        result.SetIndex(GFileEntries.Add(cls));
    }

    return GameValueExt(result);
}

GameValue ClassAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameFileType file = GetFile(oper1);

    GameFileType result;
    result.readOnly = file.readOnly;

    if (file.GetIndex() < 0)
    {
        return GameValueExt(result);
    }
    if (file.readOnly)
    {
        return GameValueExt(result);
    }
    ParamClass* cfg = GFileEntries[file.GetIndex()].cls;
    if (!cfg)
    {
        return GameValueExt(result);
    }

    RString name = oper2;
    ParamClass* cls = cfg->AddClass(name);
    if (cls)
    {
        result.SetIndex(GFileEntries.Add(cls));
    }

    return GameValueExt(result);
}

static GameValue EntryToGameValue(const GameState* state, const IParamArrayValue& entry)
{
    if (entry.IsTextValue())
    {
        return entry.GetValueRaw();
    }
    else if (entry.IsFloatValue() || entry.IsIntValue())
    {
        return entry.GetFloat();
    }
    else if (entry.IsArrayValue())
    {
        GameValue value = state->CreateGameValue(GameArray);
        GameArrayType& array = value;
        array.Resize(entry.GetItemCount());
        for (int i = 0; i < entry.GetItemCount(); i++)
        {
            array[i] = EntryToGameValue(state, *entry.GetItem(i));
        }
        return value;
    }
    else
    {
        return RString();
    }
}

static GameValue EntryToGameValue(const GameState* state, const ParamEntry& entry)
{
    if (entry.IsArray())
    {
        GameValue value = state->CreateGameValue(GameArray);
        GameArrayType& array = value;
        array.Resize(entry.GetSize());
        for (int i = 0; i < entry.GetSize(); i++)
        {
            array[i] = EntryToGameValue(state, entry[i]);
        }
        return value;
    }
    else if (entry.IsClass())
    {
        return RString();
    }
    else if (entry.IsTextValue())
    {
        return entry.GetValueRaw();
    }
    else if (entry.IsFloatValue() || entry.IsIntValue())
    {
        return (float)entry;
    }
    else
    {
        return RString();
    }
}

GameValue ValueGet(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameFileType file = GetFile(oper1);
    if (file.GetIndex() < 0)
    {
        return RString();
    }
    ParamClass* cfg = GFileEntries[file.GetIndex()].cls;
    if (!cfg)
    {
        return RString();
    }

    RString name = oper2;
    const ParamEntry* entry = cfg->FindEntry(name);
    if (!entry)
    {
        return RString();
    }

    return EntryToGameValue(state, *entry);
}

void GameValueToEntry(IParamArrayValue& entry, GameValuePar value)
{
    switch (value.GetType())
    {
        case GameScalar:
        {
            float val = value;
            entry.AddValue(val);
        }
        break;
        case GameString:
        {
            RString val = value;
            entry.AddValue(val);
        }
        break;
        case GameArray:
        {
            const GameArrayType& array = value;
            IParamArrayValue* subEntry = entry.AddArrayValue();
            if (!subEntry)
            {
                break;
            }
            for (int i = 0; i < array.Size(); i++)
            {
                GameValueToEntry(*subEntry, array[i]);
            }
        }
        break;
        default:
            LOG_ERROR(Script, "Unsupported type {}", value.GetType());
            break;
    }
}

void GameValueToEntry(ParamEntry& entry, GameValuePar value)
{
    switch (value.GetType())
    {
        case GameScalar:
        {
            float val = value;
            entry.AddValue(val);
        }
        break;
        case GameString:
        {
            RString val = value;
            entry.AddValue(val);
        }
        break;
        case GameArray:
        {
            const GameArrayType& array = value;
            IParamArrayValue* subEntry = entry.AddArrayValue();
            if (!subEntry)
            {
                break;
            }
            for (int i = 0; i < array.Size(); i++)
            {
                GameValueToEntry(*subEntry, array[i]);
            }
        }
        break;
        default:
            LOG_ERROR(Script, "Unsupported type {}", value.GetType());
            break;
    }
}

void GameValueToEntry(ParamClass& cls, RString name, GameValuePar value)
{
    switch (value.GetType())
    {
        case GameBool:
        case GameScalar:
        {
            float val = value;
            cls.Add(name, val);
        }
        break;
        case GameString:
        {
            RString val = value;
            cls.Add(name, val);
        }
        break;
        case GameArray:
        {
            const GameArrayType& array = value;
            ParamEntry* entry = cls.AddArray(name);
            for (int i = 0; i < array.Size(); i++)
            {
                GameValueToEntry(*entry, array[i]);
            }
        }
        break;
        default:
            LOG_ERROR(Script, "Unsupported type {}", value.GetType());
            break;
    }
}

GameValue ValueAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        return NOTHING;
    }
    if (array[0].GetType() != GameString)
    {
        return NOTHING;
    }

    GameFileType file = GetFile(oper1);
    if (file.GetIndex() < 0)
    {
        return NOTHING;
    }
    if (file.readOnly)
    {
        return NOTHING;
    }
    ParamClass* cfg = GFileEntries[file.GetIndex()].cls;
    if (!cfg)
    {
        return NOTHING;
    }

    RString name = array[0];
    GameValueToEntry(*cfg, name, array[1]);
    return NOTHING;
}

GameValue ConfigListNames(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
#ifdef _WIN32
    RString mask = Poseidon::GetUserDirectory() + RString("Config\\*.*");

    _finddata_t info;
    intptr_t h = _findfirst(mask, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) == 0)
            {
                array.Add(RString(info.name));
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
#else
    RString dir = Poseidon::GetUserDirectory() + RString("Config");
    DIR* d = opendir(dir);
    if (d)
    {
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr)
        {
            if (ent->d_type != DT_DIR)
            {
                array.Add(RString(ent->d_name));
            }
        }
        closedir(d);
    }
#endif

    return value;
}

GameValue AddMPReportHeader(const GameState* state, GameValuePar oper1)
{
    GStats.AddReportHeader(oper1);
    return NOTHING;
}

GameValue AddMPReportEvent(const GameState* state, GameValuePar oper1)
{
    GStats.AddReportEvent(oper1);
    return NOTHING;
}

GameValue AddMPReportFooter(const GameState* state, GameValuePar oper1)
{
    GStats.AddReportFooter(oper1);
    return NOTHING;
}

void FillReportUnitInfo(const GameState* state, GameArrayType& value, MPReportUnitInfo& unit)
{
    value.Realloc(5);
    value.Resize(5);

    value[0] = unit.name;
    value[1] = unit.player;
    value[2] = state->CreateGameValue(GameArray);
    GameArrayType& pos = value[2];
    pos.Realloc(2);
    pos.Resize(2);
    pos[0] = unit.position.X();
    pos[1] = unit.position.Z();
    value[3] = GameValueExt(unit.side);
    value[4] = unit.type;
}

GameValue GetMPReportKills(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    RString name = oper1;
    for (int i = 0; i < GStats._mission._reportEvents.Size(); i++)
    {
        MPReportEvent& event = GStats._mission._reportEvents[i];
        if (event.type != MPRETKill)
            continue;
        if (stricmp(event.unit2.name, name) != 0)
            continue;

        int index = array.Add(state->CreateGameValue(GameArray));
        GameArrayType& row = array[index];
        row.Add(event.t.toFloat());
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit1);
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit2);
        row.Add(event.unit1.position.Distance(event.unit2.position));
    }

    return value;
}

GameValue GetMPReportKilled(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    RString name = oper1;
    for (int i = 0; i < GStats._mission._reportEvents.Size(); i++)
    {
        MPReportEvent& event = GStats._mission._reportEvents[i];
        if (event.type != MPRETKill)
            continue;
        if (stricmp(event.unit1.name, name) != 0)
            continue;

        int index = array.Add(state->CreateGameValue(GameArray));
        GameArrayType& row = array[index];
        row.Add(event.t.toFloat());
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit1);
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit2);
        row.Add(event.unit1.position.Distance(event.unit2.position));
    }

    return value;
}

GameValue GetMPReportInjuries(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    RString name = oper1;
    for (int i = 0; i < GStats._mission._reportEvents.Size(); i++)
    {
        MPReportEvent& event = GStats._mission._reportEvents[i];
        if (event.type != MPRETInjury)
            continue;
        if (stricmp(event.unit1.name, name) != 0)
            continue;

        int index = array.Add(state->CreateGameValue(GameArray));
        GameArrayType& row = array[index];
        row.Add(event.t.toFloat());
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit1);
        index = row.Add(state->CreateGameValue(GameArray));
        FillReportUnitInfo(state, row[index], event.unit2);
        row.Add(event.value);
        row.Add(event.text);
    }

    return value;
}

#if _ENABLE_CHEATS
GameValue DBG_SwitchLandscape(const GameState* state, GameValuePar oper1)
{
    RString landscape = oper1;
    strcpy(Glob.header.worldname, landscape);
    GWorld->SwitchLandscape(GetWorldName(landscape));
    void InitWorld();
    InitWorld();
    return NOTHING;
}

GameValue DBG_Screenshot(const GameState* state, GameValuePar oper1)
{
    GEngine->Screenshot(oper1);
    return NOTHING;
}
#endif

GameValue SetDate(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 5))
    {
        return NOTHING;
    }
    for (int i = 0; i < 5; i++)
    {
        if (!CheckType(state, array[i], GameScalar))
        {
            return NOTHING;
        }
    }

    GWorld->SetDate(toInt((float)array[0]), toInt((float)array[1]), toInt((float)array[2]), toInt((float)array[3]),
                    toInt((float)array[4]));
    return NOTHING;
}

GameValue CenterCreate(const GameState* state, GameValuePar oper1)
{
    GameSideType side = GetSide(oper1);
    if (!GWorld->GetCenter(side))
    {
        AICenter* center = GWorld->CreateCenter(side);
        if (center)
        {
            GetNetworkManager().CreateObject(center);
        }
    }
    return oper1;
}

GameValue CenterDelete(const GameState* state, GameValuePar oper1)
{
    GameSideType side = GetSide(oper1);
    AICenter* center = GWorld->GetCenter(side);
    if (center && center->NGroups() == 0)
    {
        GWorld->DeleteCenter(side);
    }
    return NOTHING;
}

GameValue CenterSetFriend(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameSide))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }

    GameSideType side = GetSide(oper1);
    AICenter* center = GWorld->GetCenter(side);
    if (center)
    {
        GameSideType side = GetSide(array[0]);
        center->SetFriendship(side, array[1]);

        GetNetworkManager().UpdateObject(center);
    }
    return NOTHING;
}

GameValue GroupCreate(const GameState* state, GameValuePar oper1)
{
    GameSideType side = GetSide(oper1);
    AICenter* center = GWorld->GetCenter(side);
    if (center && center->NGroups() < MaxGroups)
    {
        Ref<AIGroup> group = new AIGroup();
        center->AddGroup(group);

        group->AddFirstWaypoint(VZero);

        Mission mis;
        mis._action = Mission::Arcade;
        center->SendMission(group, mis);

        GetNetworkManager().CreateObject(group);
        return GameValueExt(group);
    }
    return GROUP_NULL;
}

GameValue GroupDelete(const GameState* state, GameValuePar oper1)
{
    GameGroupType group = GetGroup(oper1);
    if (group && group->NUnits() == 0)
    {
        group->RemoveFromCenter();
    }
    return NOTHING;
}

GameValue MarkerCreate(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return NOTHING;
    }

    GameStringType name = array[0];
    Vector3 pos;
    if (!GetPos(state, pos, array[1]))
    {
        return RString();
    }

    // Important: The same-name validation applies only to MarkerCreate, not to markers from mission.sqm.
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        return RString();
    }

    ArcadeMarkerInfo marker{};
    marker.name = name;
    marker.position = pos;
    markersMap.Add(marker);

    return array[0];
}

GameValue MarkerDelete(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;
    const int i = markersMap.FindIdx(name);
    if (markersMap.ValidIdx(i))
    {
        markersMap.Delete(i);
    }
    return NOTHING;
}

GameValue MarkerSetText(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        pInfo->text = oper2;
    }
    return NOTHING;
}

GameValue MarkerSetShape(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        GameStringType type = oper2;
        pInfo->markerType = GetEnumValue<MarkerType>((const char *)type);
    }
    return NOTHING;
}

GameValue MarkerSetBrush(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        pInfo->fillName = oper2;
        pInfo->OnFillChanged();
    }
    return NOTHING;
}

GameValue MarkerGetDir(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        return pInfo->angle;
    }
    return 0.0f;
}

GameValue MarkerSetDir(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    ArcadeMarkerInfo* pInfo = markersMap.Find(name);
    if (pInfo)
    {
        pInfo->angle = oper2;
    }
    return NOTHING;
}

GameValue ObjGetMove(const GameState* state, GameValuePar oper1)
{
    EntityAI* obj = dyn_cast<EntityAI>(GetObject(oper1));
    if (!obj)
    {
        return RString();
    }
    return obj->GetCurrentMove();
}

GameValue MissionName(const GameState* state)
{
    const MissionHeader* header = GetNetworkManager().GetMissionHeader();
    if (!header)
    {
        return Glob.header.filenameReal;
    }
    if (header->name.GetLength() == 0)
    {
        return Glob.header.filenameReal;
    }
    return header->name;
}

GameValue WorldName(const GameState* state)
{
    return Glob.header.worldname;
}

GameValue MissionStart(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(6);

    tm* t = localtime(&GStats._mission._time);
    array[0] = (float)(t->tm_year + 1900);
    array[1] = (float)(t->tm_mon + 1);
    array[2] = (float)t->tm_mday;
    array[3] = (float)t->tm_hour;
    array[4] = (float)t->tm_min;
    array[5] = (float)t->tm_sec;

    return value;
}

GameValue IsServer(const GameState* state)
{
    return GetNetworkManager().IsServer();
}

GameValue IsJIP(const GameState* state)
{
    return GetNetworkManager().IsJIP();
}

GameValue ServerPause(const GameState* state)
{
    if (GetNetworkManager().IsServer() && GWorld)
    {
        GWorld->SetSimulationEnabled(false);
        LOG_INFO(Network, "Server simulation paused");
    }
    return NOTHING;
}

GameValue ServerResume(const GameState* state)
{
    if (GetNetworkManager().IsServer() && GWorld)
    {
        GWorld->SetSimulationEnabled(true);
        LOG_INFO(Network, "Server simulation resumed");
    }
    return NOTHING;
}

GameValue SaveMission(const GameState* state, GameValuePar oper1)
{
    if (GetNetworkManager().IsServer())
    {
        GameStringType filename = oper1;
        GetNetworkManager().SaveWorldState(filename);
    }
    return NOTHING;
}

GameValue LoadMission(const GameState* state, GameValuePar oper1)
{
    if (GetNetworkManager().IsServer())
    {
        GameStringType filename = oper1;
        GetNetworkManager().LoadWorldState(filename);
    }
    return NOTHING;
}

GameValue OnPlayerConnected(const GameState* state, GameValuePar oper1)
{
    GameState* gs = GWorld->GetGameState();
    gs->VarSet("onplayerconnectedcode", oper1, false, false);
    return NOTHING;
}

GameValue OnPlayerDisconnected(const GameState* state, GameValuePar oper1)
{
    GameState* gs = GWorld->GetGameState();
    gs->VarSet("onplayerdisconnectedcode", oper1, false, false);
    return NOTHING;
}

namespace
{
float sizeLand()
{
    return LandGrid * static_cast<float>(LandRange);
}
} // namespace

GameValue WorldSize(const GameState* state)
{
    return sizeLand();
}

GameValue MapWidth(const GameState* state)
{
    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return -1.0f;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return -1.0f;
    }

    return sizeLand() * map->_scaleX;
}

GameValue MapHeight(const GameState* state)
{
    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return -1.0f;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return -1.0f;
    }

    return sizeLand() * map->_scaleY;
}

GameValue MapLeftTopPos(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return array;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return array;
    }

    float landSize = sizeLand();
    array.Resize(2);
    array[0] = -map->_scaleX * map->_mapX * landSize;
    array[1] = (1 + map->_scaleY * map->_mapY) * landSize;
    return array;
}

int PlayersNumber(TargetSide side)
{
    int count = 0;
    for (int i = 0; i < GetNetworkManager().NPlayerRoles(); i++)
    {
        const PlayerRole* role = GetNetworkManager().GetPlayerRole(i);
        if (!role)
        {
            continue;
        }
        if (role->side != side)
        {
            continue;
        }

        if (role->player != NO_PLAYER)
        {
            count++;
        }
    }
    return count;
}

GameValue PlayersNumber(const GameState* state, GameValuePar oper1)
{
    return (float)PlayersNumber(GetSide(oper1));
}

GameValue StringLoad(const GameState* state, GameValuePar oper1)
{
    QIFStreamB in;
    if (OpenScript(in, oper1))
    {
        return RString(in.act(), in.rest());
    }
    return RString();
}

class FilePreprocessor : public Preproc
{
  protected:
    QIStream* OnEnterInclude(const char* filename) override
    {
        if (!QIFStreamB::FileExist(filename))
        {
            return nullptr;
        }
        QIFStreamB* stream = new QIFStreamB();
        stream->AutoOpen(filename);
        return stream;
    }
    void OnExitInclude(QIStream* stream) override
    {
        if (stream)
        {
            delete (QIFStreamB*)stream;
        }
    }
};
GameValue StringPreprocess(const GameState* state, GameValuePar oper1)
{
    RString filename = Poseidon::FindScript(oper1);
    FilePreprocessor preproc;
    QOStream processed;
    if (preproc.Process(&processed, filename))
    {
        return RString(processed.str(), processed.pcount());
    }
    return RString();
}

GameValue PublicExec(const GameState* state, GameValuePar oper1)
{
    RString command = (RString)oper1;
    if (GWorld->GetMode() == GModeNetware)
    {
        GetNetworkManager().PublicExec(command);
    }

    return NOTHING;
}

namespace
{
struct RemoteExecDescriptor
{
    RString name;
    int target = 0;
    AutoArray<char> targetSpec;
    bool jip = false;
    RString jipKey;
};

bool ParseRemoteExecDescriptor(const GameState* state, GameValuePar descriptor, bool allowJip,
                               RemoteExecDescriptor& out)
{
    if (descriptor.GetType() == GameString)
    {
        out.name = (RString)descriptor;
        RemoteExecTargetSelector selector;
        selector.kind = RemoteExecTargetKind::Scalar;
        selector.scalar = out.target;
        EncodeRemoteExecTargetSelector(out.targetSpec, selector);
        return out.name.GetLength() > 0;
    }
    if (descriptor.GetType() != GameArray)
    {
        if (state)
        {
            state->TypeError(GameString | GameArray, descriptor.GetType(), "remoteExec");
        }
        return false;
    }

    const GameArrayType& array = descriptor.GetData()->GetArray();
    if (array.Size() < 1 || array[0].GetType() != GameString)
    {
        return false;
    }
    if (array.Size() > (allowJip ? 3 : 2))
    {
        return false;
    }

    out.name = (RString)(GameStringType)array[0];
    if (array.Size() > 1)
    {
        RemoteExecTargetSelector selector;
        if (!BuildRemoteExecTargetSelector(selector, array[1]) ||
            !EncodeRemoteExecTargetSelector(out.targetSpec, selector))
        {
            return false;
        }
        out.target = selector.kind == RemoteExecTargetKind::Scalar ? selector.scalar : 0;
    }
    else
    {
        RemoteExecTargetSelector selector;
        selector.kind = RemoteExecTargetKind::Scalar;
        selector.scalar = out.target;
        EncodeRemoteExecTargetSelector(out.targetSpec, selector);
    }
    if (allowJip && array.Size() > 2)
    {
        if (array[2].GetType() == GameBool)
        {
            out.jip = (bool)array[2];
        }
        else if (array[2].GetType() == GameString)
        {
            out.jip = true;
            out.jipKey = (RString)(GameStringType)array[2];
        }
        else
        {
            return false;
        }
    }
    return out.name.GetLength() > 0;
}

GameValue RemoteExecImpl(const GameState* state, GameValuePar params, GameValuePar descriptor, bool callMode)
{
    if (GWorld->GetMode() != GModeNetware)
        return NOTHING;

    RemoteExecDescriptor parsed;
    if (!ParseRemoteExecDescriptor(state, descriptor, !callMode, parsed))
        return NOTHING;

    if (!Poseidon::WireBounds::ValidIdentifier(parsed.name, 256))
    {
        LOG_WARN(Network, "remoteExec rejected invalid identifier '{}'", (const char*)parsed.name);
        return NOTHING;
    }

    AutoArray<char> encodedParams;
    if (!Poseidon::EncodeScriptValue(encodedParams, params))
    {
        LOG_WARN(Network, "remoteExec rejected unsupported payload for '{}'", (const char*)parsed.name);
        return NOTHING;
    }

    GetNetworkManager().RemoteExec(parsed.name, encodedParams, parsed.target, parsed.targetSpec, parsed.jip,
                                   parsed.jipKey, callMode);
    return NOTHING;
}

} // namespace

GameValue RemoteExec(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return RemoteExecImpl(state, oper1, oper2, false);
}

GameValue RemoteExecCall(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return RemoteExecImpl(state, oper1, oper2, true);
}

GameValue RemoteExecRemove(const GameState* state, GameValuePar oper1)
{
    if (GWorld->GetMode() != GModeNetware)
        return NOTHING;

    RString key = (RString)oper1;
    if (key.GetLength() > 0)
    {
        GetNetworkManager().RemoteExecRemove(key);
    }
    return NOTHING;
}

GameValue ObjGetVectorUp(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    if (obj)
    {
        Vector3Val up = obj->DirectionUp();
        array[0] = up.X();
        array[1] = up.Z();
        array[2] = up.Y();
    }
    else
    {
        array[0] = 0.0f;
        array[1] = 0.0f;
        array[2] = 0.0f;
    }
    return value;
}
GameValue ObjGetVectorDir(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    if (obj)
    {
        Vector3Val dir = obj->Direction();
        array[0] = dir.X();
        array[1] = dir.Z();
        array[2] = dir.Y();
    }
    else
    {
        array[0] = 0.0f;
        array[1] = 0.0f;
        array[2] = 0.0f;
    }
    return value;
}
GameValue ObjSetVectorUp(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 3))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[2], GameScalar))
    {
        return NOTHING;
    }

    Vector3 up(array[0], array[2], array[1]);

    Vector3 dir = obj->Direction();

    // check dependency
    if (up[0] * dir[1] == up[1] * dir[0] && up[0] * dir[2] == up[2] * dir[0] && up[1] * dir[2] == up[2] * dir[1])
    {
        return NOTHING;
    }

    obj->SetUpAndDirection(up, dir);

    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (ai)
    {
        ai->IsMoved();
    }
    return NOTHING;
}
GameValue ObjSetVectorDir(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 3))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[2], GameScalar))
    {
        return NOTHING;
    }

    Vector3 dir(array[0], array[2], array[1]);
    Vector3Val oldDir = obj->Direction();
    Vector3Val oldUp = obj->DirectionUp();

    // treat as an azimuth-only change: rotate the existing up vector by the azimuth delta
    float oldAngle = atan2(oldDir.X(), oldDir.Z());
    float angle = atan2(dir.X(), dir.Z());
    float angleDif = -AngleDifference(angle, oldAngle);
    Vector3 up = Matrix3(MRotationY, angleDif) * oldUp;

    // check dependency - if dependent, orientation is impossible and we cannot set it
    if (up[0] * dir[1] == up[1] * dir[0] && up[0] * dir[2] == up[2] * dir[0] && up[1] * dir[2] == up[2] * dir[1])
    {
        return NOTHING;
    }

    obj->SetUpAndDirection(up, dir);
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (ai)
    {
        ai->IsMoved();
    }

    return NOTHING;
}
GameValue ObjSetVectorDirectionAndUp(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameArray))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameArray))
    {
        return NOTHING;
    }
    const GameArrayType& arrayDirection = array[0];
    if (!CheckSize(state, arrayDirection, 3))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayDirection[0], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayDirection[1], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayDirection[2], GameScalar))
    {
        return NOTHING;
    }
    const GameArrayType& arrayUp = array[1];
    if (!CheckSize(state, arrayUp, 3))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayUp[0], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayUp[1], GameScalar))
    {
        return NOTHING;
    }
    if (!CheckType(state, arrayUp[2], GameScalar))
    {
        return NOTHING;
    }

    Vector3 dir(arrayDirection[0], arrayDirection[2], arrayDirection[1]);
    Vector3 up(arrayUp[0], arrayUp[2], arrayUp[1]);

    // check dependency
    if (up[0] * dir[1] == up[1] * dir[0] && up[0] * dir[2] == up[2] * dir[0] && up[1] * dir[2] == up[2] * dir[1])
    {
        return NOTHING;
    }

    obj->SetDirectionAndUp(dir, up);

    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (ai)
    {
        ai->IsMoved();
    }
    return NOTHING;
}

// ammo/weapon/vehicle (sub)param(array) config readers
namespace
{
GameValue getValueParam(const ParamEntry& entry, const GameStringType& paramName)
{
    const ParamEntry* param = entry.FindEntry(paramName);
    if (!param || (param->IsClass() || param->IsArray()))
    {
        return NOTHING;
    }

    // Note: could reuse EntryToGameValue here
    if (param->IsFloatValue() || param->IsIntValue())
    {
        // return as string even for numeric config values, matching the original engine
        return GameValue(param->operator float()).GetText();
    }
    if (param->IsTextValue())
    {
        return param->operator RStringB();
    }
    return NOTHING;
}

GameValue getArrayParam(const GameState* state, const ParamEntry& entry, const GameStringType& paramName)
{
    const ParamEntry* param = entry.FindEntry(paramName);
    if (!param || !param->IsArray())
    {
        return NOTHING;
    }

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(param->GetSize());
    for (int i = 0; i < param->GetSize(); i++)
    {
        array[i] = EntryToGameValue(state, (*param)[i]);
    }
    return value;
}

GameValue getValueParam(const GameState* state, GameValuePar oper1, GameValuePar oper2, const char* cfgName)
{
    GameStringType ammoName = oper1;
    const ParamEntry* ammoClass = (Pars >> cfgName).FindEntry(ammoName);
    if (!ammoClass)
    {
        return NOTHING;
    }
    GameStringType paramName = oper2;
    return getValueParam(*ammoClass, paramName);
}

GameValue getArrayParam(const GameState* state, GameValuePar oper1, GameValuePar oper2, const char* cfgName)
{
    GameStringType ammoName = oper1;
    const ParamEntry* ammoClass = (Pars >> cfgName).FindEntry(ammoName);
    if (!ammoClass)
    {
        return NOTHING;
    }
    GameStringType paramName = oper2;
    return getArrayParam(state, *ammoClass, paramName);
}

GameValue getSubValueParam(const GameState* state, GameValuePar oper1, GameValuePar oper2, const char* cfgName)
{
    const GameArrayType& lhsArr = oper1;
    if (!CheckSize(state, lhsArr, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, lhsArr[0], GameString))
    {
        return NOTHING;
    }
    if (!CheckType(state, lhsArr[1], GameString))
    {
        return NOTHING;
    }

    GameStringType ammoName = lhsArr[0];
    const ParamEntry* ammoClass = (Pars >> cfgName).FindEntry(ammoName);
    if (!ammoClass)
    {
        return NOTHING;
    }

    GameStringType subParamName = lhsArr[1];
    const ParamEntry* subClass = ammoClass->FindEntry(subParamName);
    if (!subClass || !subClass->IsClass())
    {
        return NOTHING;
    }

    GameStringType paramName = oper2;
    return getValueParam(*subClass, paramName);
}

GameValue getSubArrayParam(const GameState* state, GameValuePar oper1, GameValuePar oper2, const char* cfgName)
{
    const GameArrayType& lhsArr = oper1;
    if (!CheckSize(state, lhsArr, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, lhsArr[0], GameString))
    {
        return NOTHING;
    }
    if (!CheckType(state, lhsArr[1], GameString))
    {
        return NOTHING;
    }

    GameStringType ammoName = lhsArr[0];
    const ParamEntry* ammoClass = (Pars >> cfgName).FindEntry(ammoName);
    if (!ammoClass)
    {
        return NOTHING;
    }

    GameStringType subParamName = lhsArr[1];
    const ParamEntry* subClass = ammoClass->FindEntry(subParamName);
    if (!subClass || !subClass->IsClass())
    {
        return NOTHING;
    }

    GameStringType paramName = oper2;
    return getArrayParam(state, *subClass, paramName);
}
} // namespace

GameValue ParamAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getValueParam(state, oper1, oper2, "CfgAmmo");
}
GameValue ParamArrayAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getArrayParam(state, oper1, oper2, "CfgAmmo");
}
GameValue ParamSubAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubValueParam(state, oper1, oper2, "CfgAmmo");
}
GameValue ParamSubArrayAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubArrayParam(state, oper1, oper2, "CfgAmmo");
}
GameValue ParamWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getValueParam(state, oper1, oper2, "CfgWeapons");
}
GameValue ParamArrayWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getArrayParam(state, oper1, oper2, "CfgWeapons");
}
GameValue ParamSubWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubValueParam(state, oper1, oper2, "CfgWeapons");
}
GameValue ParamSubArrayWeapons(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubArrayParam(state, oper1, oper2, "CfgWeapons");
}
GameValue ParamVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getValueParam(state, oper1, oper2, "CfgVehicles");
}
GameValue ParamArrayVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getArrayParam(state, oper1, oper2, "CfgVehicles");
}
GameValue ParamSubVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubValueParam(state, oper1, oper2, "CfgVehicles");
}
GameValue ParamSubArrayVehicles(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return getSubArrayParam(state, oper1, oper2, "CfgVehicles");
}

// If a class name exists in both CfgVehicles and CfgAmmo, VehicleTypeBank::Load can never create
// the CfgAmmo object, so createShell on such a class is unsupported.
GameValue ShellCreate(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 5))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[1], GameObject) && !CheckType(state, array[1], GameArray))
    {
        return OBJECT_NULL;
    }
    state->SetError(EvalOK);
    if (!CheckType(state, array[2], GameArray))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[3], GameObject))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[4], GameObject))
    {
        return OBJECT_NULL;
    }

    const GameArrayType& arrayVelocity = array[2];
    if (!CheckSize(state, arrayVelocity, 3))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, arrayVelocity[0], GameScalar))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, arrayVelocity[1], GameScalar))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, arrayVelocity[2], GameScalar))
    {
        return OBJECT_NULL;
    }

    RString typeName = array[0];
    if (!(Pars >> "CfgAmmo").FindEntry(typeName))
    {
        return OBJECT_NULL;
    }

    EntityType* type = VehicleTypes.New(typeName);

    AmmoType* aType = dynamic_cast<AmmoType*>(type);
    if (!aType)
    {
        return OBJECT_NULL;
    }

    Object* obj = GetObject(array[3]);
    EntityAI* vehAI = dyn_cast<EntityAI>(obj);
    Object* pTarget = GetObject(array[4]);
    Vehicle* shot = NewShot(vehAI, aType, pTarget);

    Vector3 pos;
    if (!GetPos(state, pos, array[1]))
    {
        return OBJECT_NULL;
    }
    Vector3 speed(arrayVelocity[0], arrayVelocity[2], arrayVelocity[1]);

    // orient the shell along its velocity
    shot->SetOrient(speed.Normalized(), VUp);
    shot->SetSpeed(speed);
    shot->SetPosition(pos);
    GLOB_WORLD->AddFastVehicle(shot);

    return GameValueExt(shot);
}

GameValue GetLaserTarget(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return OBJECT_NULL;
    }
    EntityAI* vehicle = dyn_cast<EntityAI>(obj);
    if (!vehicle)
    {
        return OBJECT_NULL;
    }

    return GameValueExt(vehicle->GetLaserTarget());
}
