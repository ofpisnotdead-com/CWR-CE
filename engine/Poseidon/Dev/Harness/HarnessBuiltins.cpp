#include <Poseidon/Dev/Harness/HarnessBuiltins.hpp>
using namespace Poseidon;
#include <Poseidon/Dev/Harness/HarnessServer.hpp>
#include <Poseidon/Dev/Harness/HarnessProtocol.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <cjson/cJSON.h>
#include <stdint.h>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
// wingdi.h defines GetObject as a macro (GetObjectA). Undef it before the
// network headers so INetworkManager's virtual GetObject keeps its real name
// consistently across the interface (NetworkIface.hpp) and its overrides
// (NetworkImpl.hpp) — otherwise the macro mangles one but not the other and
// NetworkManager looks abstract.
#ifdef GetObject
#undef GetObject
#endif
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkImplServer.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>
#include <Poseidon/Network/MasterServerBrowser.hpp>
#include <Poseidon/AI/AIUnit.hpp>
#include <Poseidon/Core/ModId.hpp>
#include <Poseidon/Core/ServerModResolve.hpp>
#include <Poseidon/Core/PendingConnect.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/AI/EntityAI.hpp>
#include <Evaluator/express.hpp>

#include <SDL3/SDL.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace Poseidon::Dev
{
namespace HarnessBuiltins
{

void RegisterScreenshot(HarnessServer& hs)
{
    hs.RegisterCommand({"screenshot", "Capture screenshot to file", {{"path", "string", true}}},
                       [](const std::string&, cJSON* root) -> std::string
                       {
                           const char* path = HarnessProtocol::GetString(root, "path");
                           if (!path || !path[0])
                               return HarnessProtocol::ErrorResponse("screenshot requires path");
                           if (!GEngine)
                               return HarnessProtocol::ErrorResponse("no engine available");
                           GEngine->Screenshot(path);
                           cJSON* resp = cJSON_CreateObject();
                           cJSON_AddStringToObject(resp, "path", path);
                           return HarnessProtocol::JsonResponse(resp);
                       });
}

// Session-scoped local variable space shared across all eval/exec calls for
// the lifetime of the process (each test runs in its own game process, so
// there is no cross-test bleed).  Provides a local scope so that SQF using
// `private _x = value` and subsequent `_x` references work across eval calls.
static GameVarSpace s_evalScope;

void RegisterSqf(HarnessServer& hs)
{
    hs.RegisterCommand({"eval", "Evaluate SQF expression and return result as string", {{"code", "string", true}}},
                       [](const std::string&, cJSON* root) -> std::string
                       {
                           const char* code = HarnessProtocol::GetString(root, "code");
                           if (!code || !code[0])
                               return HarnessProtocol::ErrorResponse("code required");
                           GameState* gs = GWorld ? GWorld->GetGameState() : nullptr;
                           if (!gs)
                               return HarnessProtocol::ErrorResponse("no game state available");
                           gs->BeginContext(&s_evalScope);
                           GameValue result = gs->EvaluateMultiple(code);
                           gs->EndContext();
                           cJSON* resp = cJSON_CreateObject();
                           RString text = result.GetText();
                           cJSON_AddStringToObject(resp, "result", (const char*)text);
                           return HarnessProtocol::JsonResponse(resp);
                       });
    hs.RegisterCommand({"exec", "Execute SQF code (fire-and-forget)", {{"code", "string", true}}},
                       [](const std::string&, cJSON* root) -> std::string
                       {
                           const char* code = HarnessProtocol::GetString(root, "code");
                           if (!code || !code[0])
                               return HarnessProtocol::ErrorResponse("code required");
                           GameState* gs = GWorld ? GWorld->GetGameState() : nullptr;
                           if (!gs)
                               return HarnessProtocol::ErrorResponse("no game state available");
                           gs->BeginContext(&s_evalScope);
                           gs->Execute(code);
                           gs->EndContext();
                           return HarnessProtocol::OkResponse();
                       });
}

std::string AnswerNetworkQuery(const char* what)
{
    if (!what)
        return {};
    if (std::strcmp(what, "players") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        cJSON* arr = cJSON_AddArrayToObject(resp, "players");
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            AutoArray<NetPlayerInfo, Foundation::MemAllocSA> players;
            netMgr.GetPlayers(players);
            for (int i = 0; i < players.Size(); i++)
            {
                cJSON* pobj = cJSON_CreateObject();
                cJSON_AddNumberToObject(pobj, "dpid", players[i].dpid);
                cJSON_AddStringToObject(pobj, "name", players[i].name);
                cJSON_AddItemToArray(arr, pobj);
            }
        }
        catch (...)
        {
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "von_state") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        cJSON* arr = cJSON_AddArrayToObject(resp, "speakers");
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            AutoArray<NetVoiceSpeakerInfo, Foundation::MemAllocSA> speakers;
            netMgr.GetVoiceSpeakers(speakers);
            for (int i = 0; i < speakers.Size(); i++)
            {
                cJSON* pobj = cJSON_CreateObject();
                cJSON_AddNumberToObject(pobj, "dpid", speakers[i].player);
                cJSON_AddBoolToObject(pobj, "active", speakers[i].active);
                cJSON_AddNumberToObject(pobj, "level", speakers[i].level);
                const PlayerIdentity* identity = netMgr.FindIdentity(speakers[i].player);
                cJSON_AddStringToObject(pobj, "name", identity ? identity->GetName() : "");
                cJSON_AddItemToArray(arr, pobj);
            }
        }
        catch (...)
        {
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "mission") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            const MissionHeader* mh = netMgr.GetMissionHeader();
            if (mh)
            {
                cJSON_AddStringToObject(resp, "name", mh->name);
                cJSON_AddStringToObject(resp, "island", mh->island);
                cJSON_AddStringToObject(resp, "fileName", mh->fileName);
                cJSON_AddBoolToObject(resp, "jip", mh->joinInProgress);
            }
        }
        catch (...)
        {
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "ngs") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            cJSON_AddNumberToObject(resp, "server_state", static_cast<int>(netMgr.GetServerState()));
            cJSON_AddNumberToObject(resp, "client_state", static_cast<int>(netMgr.GetGameState()));
        }
        catch (...)
        {
            cJSON_AddNumberToObject(resp, "server_state", -1);
            cJSON_AddNumberToObject(resp, "client_state", -1);
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "play_state") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            cJSON_AddNumberToObject(resp, "server_state", static_cast<int>(netMgr.GetServerState()));
            cJSON_AddNumberToObject(resp, "client_state", static_cast<int>(netMgr.GetGameState()));
            cJSON_AddBoolToObject(resp, "has_role", netMgr.GetMyPlayerRole() != nullptr);
        }
        catch (...)
        {
            cJSON_AddNumberToObject(resp, "server_state", -1);
            cJSON_AddNumberToObject(resp, "client_state", -1);
            cJSON_AddBoolToObject(resp, "has_role", false);
        }

        UITestEngine ui;
        ControlsContainer* display = ui.GetActiveDisplay();
        cJSON_AddNumberToObject(resp, "display", display ? display->IDD() : -1);
        cJSON_AddBoolToObject(resp, "in_gameplay", GApp && GApp->IsInGameplay());
        cJSON_AddBoolToObject(resp, "has_world", GWorld != nullptr);
        cJSON_AddNumberToObject(resp, "world_mode", GWorld ? static_cast<int>(GWorld->GetMode()) : -1);
        cJSON_AddNumberToObject(resp, "frame", GEngine ? GEngine->GetFrameCounter() : 0);
        cJSON_AddNumberToObject(resp, "time_ms", Glob.time.toInt());

        Person* player = GWorld ? GWorld->GetRealPlayer() : nullptr;
        AIUnit* brain = player ? player->Brain() : nullptr;
        EntityAI* vehicle = brain ? brain->GetVehicle() : nullptr;
        cJSON_AddBoolToObject(resp, "has_player", player != nullptr);
        cJSON_AddBoolToObject(resp, "player_local", player && player->IsLocal());
        cJSON_AddBoolToObject(resp, "player_destroyed", player && player->IsDammageDestroyed());
        cJSON_AddBoolToObject(resp, "has_brain", brain != nullptr);
        cJSON_AddBoolToObject(resp, "has_vehicle", vehicle != nullptr);
        cJSON_AddBoolToObject(resp, "vehicle_local", vehicle && vehicle->IsLocal());
        cJSON_AddBoolToObject(resp, "player_active", GWorld && player && GWorld->PlayerOn() == player);
        if (player)
        {
            Vector3Val pos = player->Position();
            Vector3Val speed = player->Speed();
            cJSON_AddNumberToObject(resp, "player_x", pos.X());
            cJSON_AddNumberToObject(resp, "player_z", pos.Z());
            cJSON_AddNumberToObject(resp, "player_speed_x", speed.X());
            cJSON_AddNumberToObject(resp, "player_speed_z", speed.Z());
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "connections") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        cJSON* arr = cJSON_AddArrayToObject(resp, "connections");
        cJSON_AddNumberToObject(resp, "server_state", static_cast<int>(GNetworkManager.GetServerState()));
        NetworkServer* server = GNetworkManager.GetServer();
        if (!server)
            return HarnessProtocol::JsonResponse(resp);

        AutoArray<NetworkConnectionSnapshot, Foundation::MemAllocSA> connections;
        server->GetConnectionSnapshots(connections);
        for (int i = 0; i < connections.Size(); i++)
        {
            const NetworkConnectionSnapshot& connection = connections[i];
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "dpid", connection.dpid);
            cJSON_AddStringToObject(item, "name", connection.name);
            cJSON_AddNumberToObject(item, "state", static_cast<int>(connection.state));
            cJSON_AddBoolToObject(item, "jip", connection.jip);
            cJSON_AddBoolToObject(item, "botClient", connection.botClient);
            cJSON_AddBoolToObject(item, "hasConnectionInfo", connection.hasConnectionInfo);
            cJSON_AddNumberToObject(item, "latencyMs", connection.latencyMs);
            cJSON_AddNumberToObject(item, "throughputBps", connection.throughputBps);
            cJSON_AddNumberToObject(item, "avgPingMs", connection.avgPingMs);
            cJSON_AddNumberToObject(item, "avgBandwidthBps", connection.avgBandwidthBps);
            cJSON_AddItemToArray(arr, item);
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    if (std::strcmp(what, "roles") == 0)
    {
        cJSON* resp = cJSON_CreateObject();
        cJSON* arr = cJSON_AddArrayToObject(resp, "roles");
        try
        {
            INetworkManager& netMgr = GetNetworkManager();
            cJSON_AddNumberToObject(resp, "player", netMgr.GetPlayer());
            cJSON_AddNumberToObject(resp, "game_state", static_cast<int>(netMgr.GetGameState()));
            cJSON_AddNumberToObject(resp, "server_state", static_cast<int>(netMgr.GetServerState()));
            const int n = netMgr.NPlayerRoles();
            cJSON_AddNumberToObject(resp, "count", n);
            for (int i = 0; i < n; ++i)
            {
                const PlayerRole* role = netMgr.GetPlayerRole(i);
                if (!role)
                    continue;
                cJSON* item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "index", i);
                cJSON_AddNumberToObject(item, "side", static_cast<int>(role->side));
                cJSON_AddNumberToObject(item, "group", role->group);
                cJSON_AddNumberToObject(item, "unit", role->unit);
                cJSON_AddNumberToObject(item, "player", role->player);
                cJSON_AddBoolToObject(item, "locked", role->roleLocked);
                cJSON_AddItemToArray(arr, item);
            }
        }
        catch (...)
        {
        }
        return HarnessProtocol::JsonResponse(resp);
    }
    return {};
}

namespace
{
RString ResolveHarnessMasterServerHost()
{
    return GetNetworkMasterServer();
}

void AddMasterServerServiceSession(cJSON* object, const char* serverId, const MasterServerServiceSession& session)
{
    cJSON_AddStringToObject(object, "serverId", serverId != nullptr ? serverId : "");
    cJSON_AddStringToObject(object, "address", session.address.c_str());
    cJSON_AddNumberToObject(object, "hostport", session.hostPort);
    cJSON_AddStringToObject(object, "hostname", session.hostName.c_str());
    cJSON_AddStringToObject(object, "gametype", session.mission.c_str());
    cJSON_AddNumberToObject(object, "actver", session.actualVersion);
    cJSON_AddNumberToObject(object, "reqver", session.requiredVersion);
    cJSON_AddNumberToObject(object, "state", session.gameState);
    cJSON_AddNumberToObject(object, "numplayers", session.numPlayers);
    cJSON_AddNumberToObject(object, "maxplayers", session.maxPlayers);
    cJSON_AddBoolToObject(object, "password", session.password);
    cJSON_AddStringToObject(object, "impl", session.transportImplementation.c_str());
    cJSON_AddStringToObject(object, "mod", session.mod.c_str());
    cJSON_AddBoolToObject(object, "equalModRequired", session.equalModRequired);
}

void AddMasterServerServiceMod(cJSON* object, const MasterServerServiceModCatalogEntry& mod)
{
    cJSON_AddStringToObject(object, "modId", mod.modId.c_str());
    cJSON_AddStringToObject(object, "name", mod.name.c_str());
    cJSON_AddStringToObject(object, "version", mod.version.c_str());
    cJSON_AddStringToObject(object, "description", mod.description.c_str());
    cJSON* authors = cJSON_AddArrayToObject(object, "authors");
    for (const auto& author : mod.authors)
    {
        cJSON_AddItemToArray(authors, cJSON_CreateString(author.c_str()));
    }
    cJSON_AddStringToObject(object, "homepageUrl", mod.homepageUrl.c_str());
    cJSON_AddStringToObject(object, "downloadUrl", mod.downloadUrl.c_str());
    cJSON_AddNumberToObject(object, "sizeBytes", static_cast<double>(mod.sizeBytes));
}

void AddMasterServerServiceUsageServer(cJSON* object, const MasterServerServiceModUsageServer& server)
{
    cJSON_AddStringToObject(object, "serverId", server.serverId.c_str());
    cJSON_AddStringToObject(object, "hostname", server.hostName.c_str());
    cJSON_AddStringToObject(object, "gametype", server.mission.c_str());
    cJSON_AddNumberToObject(object, "players", server.players);
    cJSON_AddNumberToObject(object, "maxPlayers", server.maxPlayers);
    cJSON_AddBoolToObject(object, "password", server.password);
}

std::string AnswerMasterServerServerDetailQuery(cJSON* root)
{
    const char* serverId = HarnessProtocol::GetString(root, "serverId");
    if (!serverId || !serverId[0])
    {
        return HarnessProtocol::ErrorResponse("serverId required");
    }

    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    MasterServerServiceServerDetail detail;
    if (!FetchMasterServerServiceServerDetail(masterServer, serverId, nullptr, detail))
    {
        return HarnessProtocol::ErrorResponse("master-server detail fetch failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* server = cJSON_AddObjectToObject(resp, "server");
    AddMasterServerServiceSession(server, serverId, detail.server);

    cJSON* players = cJSON_AddArrayToObject(resp, "players");
    for (const auto& player : detail.players)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", player.name.c_str());
        cJSON_AddStringToObject(item, "role", player.role.c_str());
        cJSON_AddItemToArray(players, item);
    }

    cJSON* mods = cJSON_AddArrayToObject(resp, "mods");
    for (const auto& mod : detail.mods)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "modId", mod.modId.c_str());
        cJSON_AddStringToObject(item, "name", mod.name.c_str());
        cJSON_AddBoolToObject(item, "known", mod.known);
        cJSON_AddStringToObject(item, "version", mod.version.c_str());
        cJSON_AddStringToObject(item, "description", mod.description.c_str());
        cJSON_AddStringToObject(item, "homepageUrl", mod.homepageUrl.c_str());
        cJSON_AddStringToObject(item, "downloadUrl", mod.downloadUrl.c_str());
        cJSON_AddItemToArray(mods, item);
    }

    cJSON* playerHistory = cJSON_AddArrayToObject(resp, "playerHistory");
    for (const auto& sample : detail.playerHistory)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "observedUnixMs", static_cast<double>(sample.observedUnixMs));
        cJSON_AddNumberToObject(item, "players", sample.players);
        cJSON_AddItemToArray(playerHistory, item);
    }

    cJSON* recentSessions = cJSON_AddArrayToObject(resp, "recentSessions");
    for (const auto& session : detail.recentSessions)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "mission", session.mission.c_str());
        cJSON_AddStringToObject(item, "label", session.label.c_str());
        cJSON_AddNumberToObject(item, "playedMinutes", session.playedMinutes);
        cJSON_AddNumberToObject(item, "peakPlayers", session.peakPlayers);
        cJSON_AddNumberToObject(item, "endedUnixMs", static_cast<double>(session.endedUnixMs));
        cJSON_AddItemToArray(recentSessions, item);
    }

    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerMasterServerModDetailQuery(cJSON* root)
{
    const char* modId = HarnessProtocol::GetString(root, "modId");
    if (!modId || !modId[0])
    {
        return HarnessProtocol::ErrorResponse("modId required");
    }

    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    MasterServerServiceModCatalogEntry mod;
    if (!FetchMasterServerServiceModDetail(masterServer, modId, nullptr, mod))
    {
        return HarnessProtocol::ErrorResponse("master-server mod detail fetch failed");
    }

    cJSON* resp = cJSON_CreateObject();
    AddMasterServerServiceMod(resp, mod);
    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerMasterServerModVersionsQuery(cJSON* root)
{
    const char* modId = HarnessProtocol::GetString(root, "modId");
    if (!modId || !modId[0])
    {
        return HarnessProtocol::ErrorResponse("modId required");
    }

    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    std::vector<MasterServerServiceModCatalogEntry> versions;
    if (!FetchMasterServerServiceModVersions(masterServer, modId, nullptr, versions))
    {
        return HarnessProtocol::ErrorResponse("master-server mod versions fetch failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* items = cJSON_AddArrayToObject(resp, "versions");
    for (const auto& version : versions)
    {
        cJSON* item = cJSON_CreateObject();
        AddMasterServerServiceMod(item, version);
        cJSON_AddItemToArray(items, item);
    }
    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerMasterServerModServersQuery(cJSON* root)
{
    const char* modId = HarnessProtocol::GetString(root, "modId");
    if (!modId || !modId[0])
    {
        return HarnessProtocol::ErrorResponse("modId required");
    }

    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    std::vector<MasterServerServiceModUsageServer> servers;
    if (!FetchMasterServerServiceModServers(masterServer, modId, nullptr, servers))
    {
        return HarnessProtocol::ErrorResponse("master-server mod servers fetch failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* items = cJSON_AddArrayToObject(resp, "servers");
    for (const auto& server : servers)
    {
        cJSON* item = cJSON_CreateObject();
        AddMasterServerServiceUsageServer(item, server);
        cJSON_AddItemToArray(items, item);
    }
    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerMasterServerModListQuery(cJSON* root)
{
    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    const char* query = HarnessProtocol::GetString(root, "q");

    std::vector<MasterServerServiceModCatalogEntry> mods;
    if (!FetchMasterServerServiceModList(masterServer, query, nullptr, mods))
    {
        return HarnessProtocol::ErrorResponse("master-server mod list fetch failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* items = cJSON_AddArrayToObject(resp, "mods");
    for (const auto& mod : mods)
    {
        cJSON* item = cJSON_CreateObject();
        AddMasterServerServiceMod(item, mod);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddNumberToObject(resp, "count", static_cast<int>(mods.size()));
    return HarnessProtocol::JsonResponse(resp);
}

// Read a JSON string array into normalized ModIds (skips blanks).
static std::vector<ModId> ReadModIdArray(cJSON* root, const char* field)
{
    std::vector<ModId> ids;
    cJSON* arr = cJSON_GetObjectItem(root, field);
    if (arr != nullptr && cJSON_IsArray(arr))
    {
        cJSON* it = nullptr;
        cJSON_ArrayForEach(it, arr)
        {
            if (cJSON_IsString(it) && it->valuestring != nullptr)
            {
                ModId id(it->valuestring);
                if (!id.Empty())
                {
                    ids.push_back(std::move(id));
                }
            }
        }
    }
    return ids;
}

// mp_resolve: fetch the live catalog, then resolve a server's required mod list
// (`mod` + `equalmod`) against the player's `installed`/`active` arrays via the OOP
// ServerModResolver. Returns the diff the MP-join UI would render/act on.
std::string AnswerMpResolveQuery(cJSON* root)
{
    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    std::vector<MasterServerServiceModCatalogEntry> mods;
    if (!FetchMasterServerServiceModList(masterServer, nullptr, nullptr, mods))
    {
        return HarnessProtocol::ErrorResponse("master-server mod list fetch failed");
    }

    ModCatalog catalog;
    for (const auto& m : mods)
    {
        catalog.Add(ModCatalogEntry(ModId(m.modId), m.name, m.downloadUrl, m.sizeBytes));
    }

    const char* serverMod = HarnessProtocol::GetString(root, "mod");
    const ServerModList server(serverMod != nullptr ? serverMod : "",
                               HarnessProtocol::GetBool(root, "equalmod", false));
    const ServerModResolver resolver(ReadModIdArray(root, "installed"), ReadModIdArray(root, "active"));
    const ServerModResolution res = resolver.Resolve(server, catalog);

    cJSON* resp = cJSON_CreateObject();
    cJSON* sat = cJSON_AddArrayToObject(resp, "satisfied");
    for (const ModId& id : res.Satisfied())
    {
        cJSON_AddItemToArray(sat, cJSON_CreateString(id.Value().c_str()));
    }
    cJSON* dl = cJSON_AddArrayToObject(resp, "toDownload");
    for (const ModDownload& d : res.ToDownload())
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", d.id.Value().c_str());
        cJSON_AddStringToObject(item, "name", d.name.c_str());
        cJSON_AddStringToObject(item, "url", d.downloadUrl.c_str());
        cJSON_AddNumberToObject(item, "size", static_cast<double>(d.sizeBytes));
        cJSON_AddItemToArray(dl, item);
    }
    cJSON* dis = cJSON_AddArrayToObject(resp, "toDisable");
    for (const ModId& id : res.ToDisable())
    {
        cJSON_AddItemToArray(dis, cJSON_CreateString(id.Value().c_str()));
    }
    cJSON* blk = cJSON_AddArrayToObject(resp, "blocked");
    for (const ModId& id : res.Blocked())
    {
        cJSON_AddItemToArray(blk, cJSON_CreateString(id.Value().c_str()));
    }
    cJSON_AddBoolToObject(resp, "needsWork", res.NeedsWork());
    cJSON_AddBoolToObject(resp, "canProceed", res.CanProceed());
    cJSON_AddNumberToObject(resp, "catalogCount", static_cast<int>(mods.size()));
    return HarnessProtocol::JsonResponse(resp);
}

// mp_join: arm the deferred connect to <address>:<port> and request a mod-apply
// re-mount with <modpath> (the required set, already resolved + downloaded via
// mp_resolve + download). The AppIdle pending-connect hook finishes the join once
// the rebuilt menu is up. Returns immediately; the join completes over later frames.
std::string AnswerMpJoinQuery(cJSON* root)
{
    const char* address = HarnessProtocol::GetString(root, "address");
    if (address == nullptr || address[0] == '\0')
    {
        return HarnessProtocol::ErrorResponse("mp_join requires address");
    }
    const int port = HarnessProtocol::GetInt(root, "port", 2302);
    const char* password = HarnessProtocol::GetString(root, "password");
    const char* modpath = HarnessProtocol::GetString(root, "modpath");

    GPendingConnect().Arm(address, port, password != nullptr ? password : "");
    // Re-mount only when a modpath KEY is present; absent → arm + connect at the menu
    // without re-mounting (isolates the connect path from the re-mount in tests).
    const bool remount = cJSON_GetObjectItem(root, "modpath") != nullptr;
    if (remount && GApp != nullptr)
    {
        GApp->RequestRemountWithMods(modpath != nullptr ? modpath : "");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "armed", true);
    cJSON_AddBoolToObject(resp, "remount", remount);
    cJSON_AddStringToObject(resp, "address", address);
    cJSON_AddNumberToObject(resp, "port", port);
    cJSON_AddStringToObject(resp, "modpath", modpath != nullptr ? modpath : "");
    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerMasterServerListQuery(cJSON* root)
{
    const RString& masterServer = ResolveHarnessMasterServerHost();
    if (masterServer.GetLength() == 0)
    {
        return HarnessProtocol::ErrorResponse("no master-server configured");
    }

    // Optional filter — pointers stay valid for the synchronous fetch below.
    MasterServerBrowserFilter filter{};
    const char* serverName = HarnessProtocol::GetString(root, "serverName");
    const char* missionName = HarnessProtocol::GetString(root, "missionName");
    if (serverName)
        filter.serverName = serverName;
    if (missionName)
        filter.missionName = missionName;
    filter.minPlayers = HarnessProtocol::GetInt(root, "minPlayers");
    filter.maxPlayers = HarnessProtocol::GetInt(root, "maxPlayers");
    cJSON* includeFull = cJSON_GetObjectItem(root, "includeFullServers");
    filter.includeFullServers = includeFull == nullptr || cJSON_IsTrue(includeFull);

    MasterServerBrowser* browser = CreateMasterServerBrowser(masterServer, nullptr, nullptr);
    if (!browser)
    {
        return HarnessProtocol::ErrorResponse("master-server browser create failed");
    }

    // The browser fetch runs on a worker thread; this harness query is
    // synchronous, so pump Think until it lands (bounded ~6s wait).
    UpdateMasterServerBrowser(browser, filter);
    for (int i = 0; i < 600 && GetMasterServerBrowserState(browser) != MasterServerBrowserState::Idle; ++i)
    {
        ThinkMasterServerBrowser(browser);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* items = cJSON_AddArrayToObject(resp, "servers");
    const int count = GetMasterServerBrowserCount(browser);
    for (int i = 0; i < count; i++)
    {
        MasterServerSessionInfo info;
        if (!TryGetMasterServerBrowserSession(browser, i, info))
        {
            continue;
        }
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "address", info.address);
        cJSON_AddNumberToObject(item, "hostport", info.hostPort);
        cJSON_AddStringToObject(item, "hostname", info.hostName);
        cJSON_AddStringToObject(item, "gametype", info.mission);
        cJSON_AddNumberToObject(item, "actver", info.actualVersion);
        cJSON_AddNumberToObject(item, "reqver", info.requiredVersion);
        cJSON_AddStringToObject(item, "vertag", info.versionTag);
        cJSON_AddNumberToObject(item, "state", info.gameState);
        cJSON_AddNumberToObject(item, "ping", info.ping);
        cJSON_AddNumberToObject(item, "numplayers", info.numPlayers);
        cJSON_AddNumberToObject(item, "maxplayers", info.maxPlayers);
        cJSON_AddBoolToObject(item, "password", info.password);
        cJSON_AddStringToObject(item, "mod", info.mod);
        cJSON_AddBoolToObject(item, "equalmod", info.equalModRequired);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddNumberToObject(resp, "count", count);

    DestroyMasterServerBrowser(browser);
    return HarnessProtocol::JsonResponse(resp);
}

std::string AnswerDownloadQuery(cJSON* root)
{
    const char* url = HarnessProtocol::GetString(root, "url");
    const char* dest = HarnessProtocol::GetString(root, "dest");
    if (!url || !url[0])
    {
        return HarnessProtocol::ErrorResponse("url required");
    }
    if (!dest || !dest[0])
    {
        return HarnessProtocol::ErrorResponse("dest required");
    }

    struct DownloadProgress
    {
        int64_t received = 0;
        int64_t total = 0;
        int calls = 0;
    };
    DownloadProgress progress;

    const bool ok = DownloadMasterServerServiceFile(url, nullptr, dest, &progress,
                                                    [](void* instance, int64_t received, int64_t total)
                                                    {
                                                        auto* p = static_cast<DownloadProgress*>(instance);
                                                        p->received = received;
                                                        p->total = total;
                                                        ++p->calls;
                                                    });
    if (!ok)
    {
        return HarnessProtocol::ErrorResponse("download failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "received", static_cast<double>(progress.received));
    cJSON_AddNumberToObject(resp, "total", static_cast<double>(progress.total));
    cJSON_AddNumberToObject(resp, "progressCalls", progress.calls);
    return HarnessProtocol::JsonResponse(resp);
}
} // namespace

std::string AnswerServiceQuery(const char* what, cJSON* root)
{
    if (what && std::strcmp(what, "master_server_server_detail") == 0)
    {
        return AnswerMasterServerServerDetailQuery(root);
    }
    if (what && std::strcmp(what, "master_server_mod_detail") == 0)
    {
        return AnswerMasterServerModDetailQuery(root);
    }
    if (what && std::strcmp(what, "master_server_mod_versions") == 0)
    {
        return AnswerMasterServerModVersionsQuery(root);
    }
    if (what && std::strcmp(what, "master_server_mod_servers") == 0)
    {
        return AnswerMasterServerModServersQuery(root);
    }
    if (what && std::strcmp(what, "master_server_mod_list") == 0)
    {
        return AnswerMasterServerModListQuery(root);
    }
    if (what && std::strcmp(what, "master_server_list") == 0)
    {
        return AnswerMasterServerListQuery(root);
    }
    if (what && std::strcmp(what, "mp_resolve") == 0)
    {
        return AnswerMpResolveQuery(root);
    }
    if (what && std::strcmp(what, "mp_join") == 0)
    {
        return AnswerMpJoinQuery(root);
    }
    if (what && std::strcmp(what, "download") == 0)
    {
        return AnswerDownloadQuery(root);
    }
    return {};
}

void RegisterKeyInjection(HarnessServer& hs)
{
    hs.RegisterCommand(
        {"key", "Inject SDL scancode key press", {{"sc", "int", true}, {"mod", "int", false}, {"hold", "bool", false}}},
        [](const std::string&, cJSON* root) -> std::string
        {
            unsigned sc = static_cast<unsigned>(HarnessProtocol::GetInt(root, "sc"));
            unsigned mod = static_cast<unsigned>(HarnessProtocol::GetInt(root, "mod"));
            bool hold = HarnessProtocol::GetBool(root, "hold");
            SDL_Event ev = {};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = static_cast<SDL_Scancode>(sc);
            ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
            ev.key.mod = static_cast<SDL_Keymod>(mod);
            ev.key.down = true;
            SDL_PushEvent(&ev);
            if (!hold)
            {
                ev.type = SDL_EVENT_KEY_UP;
                ev.key.down = false;
                SDL_PushEvent(&ev);
            }
            return HarnessProtocol::OkResponse();
        });
    hs.RegisterCommand({"key_up", "Release a held key", {{"sc", "int", true}}},
                       [](const std::string&, cJSON* root) -> std::string
                       {
                           unsigned sc = static_cast<unsigned>(HarnessProtocol::GetInt(root, "sc"));
                           SDL_Event ev = {};
                           ev.type = SDL_EVENT_KEY_UP;
                           ev.key.scancode = static_cast<SDL_Scancode>(sc);
                           ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
                           ev.key.down = false;
                           SDL_PushEvent(&ev);
                           return HarnessProtocol::OkResponse();
                       });
}

namespace
{
// Build the JSON for `query what=display` from the active display.
// Walks idcs -1..1999 — covers the entire harness / production idc
// range with room to spare; ranges higher than that aren't used.
std::string AnswerUIDisplayQuery(UITestEngine& uiTestEngine)
{
    cJSON* resp = cJSON_CreateObject();
    auto* container = uiTestEngine.GetActiveDisplay();
    cJSON_AddNumberToObject(resp, "idd", container ? container->IDD() : -1);
    if (container)
    {
        cJSON* controls = cJSON_AddArrayToObject(resp, "controls");
        for (int idc = -1; idc < 2000; idc++)
        {
            IControl* ctrl = container->GetCtrl(idc);
            if (!ctrl)
                continue;
            cJSON* c = cJSON_CreateObject();
            cJSON_AddNumberToObject(c, "idc", ctrl->IDC());
            cJSON_AddStringToObject(c, "type", UITestEngine::GetControlTypeName(ctrl));
            cJSON_AddBoolToObject(c, "visible", ctrl->IsVisible());
            cJSON_AddBoolToObject(c, "enabled", ctrl->IsEnabled());
            auto* control = dynamic_cast<Control*>(ctrl);
            if (control)
            {
                cJSON* pos = cJSON_CreateObject();
                cJSON_AddNumberToObject(pos, "x", control->X());
                cJSON_AddNumberToObject(pos, "y", control->Y());
                cJSON_AddNumberToObject(pos, "w", control->W());
                cJSON_AddNumberToObject(pos, "h", control->H());
                cJSON_AddItemToObject(c, "pos", pos);
            }
            std::string text = UITestEngine::GetControlText(ctrl);
            if (!text.empty())
                cJSON_AddStringToObject(c, "text", text.c_str());
            cJSON_AddItemToArray(controls, c);
        }
    }
    return HarnessProtocol::JsonResponse(resp);
}
} // namespace

void RegisterUIQuery(HarnessServer& hs, UITestEngine& uiTestEngine)
{
    // Capture the underlying pointer by value rather than the parameter
    // reference — the parameter goes out of scope when this function
    // returns, but the lambda lives on inside HarnessServer's command
    // map.  The pointed-to objects (m_uiTestEngine in the calling app)
    // outlive the server, so the captured pointer stays valid.
    UITestEngine* ui = &uiTestEngine;
    hs.RegisterCommand({"query", "Query UI state (what: display)", {{"what", "string", true}}},
                       [ui](const std::string&, cJSON* root) -> std::string
                       {
                           const char* what = HarnessProtocol::GetString(root, "what");
                           if (what && std::strcmp(what, "display") == 0)
                               return AnswerUIDisplayQuery(*ui);
                           return HarnessProtocol::ErrorResponse("unknown query target");
                       });
}

void RegisterWaitDisplay(HarnessServer& hs, UITestEngine& uiTestEngine)
{
    HarnessServer* hsPtr = &hs;
    UITestEngine* ui = &uiTestEngine;
    hs.RegisterCommand(
        {"wait_display", "Block until display IDD appears", {{"idd", "int", true}, {"timeout_ms", "int", false}}},
        [hsPtr, ui](const std::string& name, cJSON* root) -> std::string
        {
            int targetIDD = HarnessProtocol::GetInt(root, "idd", -1);
            int timeoutMs = HarnessProtocol::GetInt(root, "timeout_ms", 30000);
            int currentIDD = ui->GetActiveDisplayIDD();
            if (currentIDD == targetIDD)
            {
                cJSON* resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "idd", currentIDD);
                return HarnessProtocol::JsonResponse(resp);
            }
            if (timeoutMs <= 0)
                return HarnessProtocol::ErrorResponse("wait_display timeout");
            cJSON_ReplaceItemInObjectCaseSensitive(root, "timeout_ms", cJSON_CreateNumber(timeoutMs - 16));
            char* updated = cJSON_PrintUnformatted(root);
            HarnessCommand retry;
            retry.name = name;
            retry.raw = updated;
            cJSON_free(updated);
            hsPtr->ReEnqueueCommand(retry);
            return "";
        });
}

} // namespace HarnessBuiltins
} // namespace Poseidon::Dev
