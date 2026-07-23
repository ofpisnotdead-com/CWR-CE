#pragma once

#include <Poseidon/Network/NetworkImplComponent.hpp>

struct UpdateLocalObjectInfo;

// Network Client
class NetworkClient : public NetworkComponent
{
  protected:
    // low-level client implementation
    SRef<NetTranspClient> _client;

    // info about objects local on client
    AutoArray<NetworkLocalObjectInfo> _localObjects;
    // info about objects non local (remote) on client
    AutoArray<NetworkRemoteObjectInfo> _remoteObjects;
    // units waiting for respawn
    AutoArray<RespawnQueueItem> _respawnQueue;
    // dead bodies (max MAX_LOCAL_BODIES can be simulated)
    AutoArray<BodyInfo> _bodies;

    // message formats
    AutoArray<NetworkMessageFormatBase> _formats;
    // message queues (candidates for agregation)
    NetworkMessageQueue _messageQueue, _messageQueueNonGuaranteed;

    // map directly speaing player to object and sound buffer
    AutoArray<Network3DSoundBuffer> _soundBuffers;

    // connection quality to server
    ConnectionQuality _connectionQuality;
    // result of _dp->Connect
    ConnectResult _connectResult;

    // current state on server
    NetworkGameState _serverState;
    int _voiceChannel = -1;

    // true if server state ever reached NGSPlay (for disconnect exit-code logic)
    bool _serverReachedPlay = false;

    // mission pbo file is valid (actual)
    bool _missionFileValid;

    // player is game master of dedicated server
    bool _gameMaster;
    // gamemaster is only voted admin (cannot do shutdown etc.)
    bool _admin;
    // show "select mission" dialog for dedicated server
    bool _selectMission;
    // show "vote mission" dialog for dedicated server
    bool _voteMission;

    // Game is paused due to disconnection state of game
    bool _controlsPaused;

    // Player joined in progress (connected during NGSPlay)
    bool _jip;

    // missions on dedicated server
    AutoArray<RString> _serverMissions;

    // name of local player
    RString _localPlayerName;

    // id of next sound - incremented for each PlaySound
    int _soundId;
    // list of sent sounds
    AutoArray<PlaySoundInfo> _sentSounds;
    // list of received sounds
    AutoArray<PlaySoundInfo> _receivedSounds;

    // sound introducing chat message
    SoundPars _chatSound;

    bool _pendingSelectPlayer;
    SelectPlayerMessage _pendingSelectPlayerMessage;
    AutoArray<ChangeOwnerMessage> _pendingChangeOwners;
    DWORD _missionRawLastRequestTime = 0;
    int _missionRawLastRequestFirstSegment = -1;
    int _missionRawLastRequestSegmentCount = 0;
    DWORD _missionRawFirstSegmentTime = 0;
    DWORD _missionRawLastSegmentTime = 0;
    int _missionRawExpectedSize = 0;
    int _missionRawHighestReceivedSegment = -1;
    int _missionRawReceivedSegments = 0;
    int _missionRawDuplicateSegments = 0;
    int _missionRawRequestedDuplicateSegments = 0;
    int _missionRawRequestedDuplicateUniqueSegments = 0;
    int _missionRawRequestedSegments = 0;
    int _missionRawRequestCount = 0;
    AutoArray<bool> _missionRawRequestedSegmentMap;
    AutoArray<bool> _missionRawRequestedDuplicateSegmentMap;
    bool _missionTransferHeaderStatsLogged = false;

    // used for transfer of client info (for example camera position) to server
    Ref<ClientInfoObject> _clientInfo;

    // number of bodies we want to hide
    int _hideBodies;

  public:
    NetworkClient(NetworkManager* parent, RString address, RString password, bool botClient);
    ~NetworkClient() override;

    // Connect to server
    bool Init(RString address, RString password, bool botClient);
    // Disconnect from server
    void Done();

    bool IsValid() const { return _client != nullptr; }

    // Return name of local player
    RString GetLocalPlayerName() const { return _localPlayerName; }
    // Return result of _dp->Connect
    ConnectResult GetConnectResult() const { return _connectResult; }

    // Return if player is game master of dedicated server
    bool IsGameMaster() const { return _gameMaster; }

    // Return if gamemaster is only voted admin (cannot do shutdown etc.)
    bool IsAdmin() const { return _admin; }

    // Return if player joined in progress (JIP)
    bool IsJIP() const { return _jip; }

    // Return if client is bot client
    inline bool IsBotClient() const;

    // Return connection quality to server
    ConnectionQuality GetConnectionQuality() const { return _connectionQuality; }

    // Return current state on server
    NetworkGameState GetServerState() const { return _serverState; }

    // Return true if server state ever reached NGSPlay
    bool WasPlaying() const { return _serverReachedPlay; }

    // Return recommended chat priorty based on current game state
    NetMsgFlags GetChatPriority() const;

    // Set parameters for current mission
    void SetParams(float param1, float param2);

    void OnSimulate() override;
    void OnSendComplete(DWORD msgID, bool ok);
    void OnRawMagicMessage(int magic, const char* buffer, int bufferSize);
    void RequestMissingMissionRawSegments();
    void OnMessage(int from, NetworkMessage* msg, NetworkMessageType type) override;

    // perform regular memory clean-up
    unsigned CleanUpMemory() override;

    // Process received system messages
    void ReceiveSystemMessages();
    // Process received user messages
    void ReceiveUserMessages();
    // Destroy all received system messages
    void RemoveSystemMessages();
    // Destroy all received user messages
    void RemoveUserMessages();

    // Hi-level send of message to server
    DWORD SendMsg(NetworkSimpleObject* object, NetMsgFlags dwFlags);
    bool DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags dwFlags) override;
    NetworkMessageFormatBase* GetFormat(/*int client, */ int type) override;

    // Retrieve list of players
    void GetPlayers(AutoArray<NetPlayerInfo, Poseidon::Foundation::MemAllocSA>& players);
    // Assign player to role slot
    void AssignPlayer(int role, int player);

    // Select person as player
    void SelectPlayer(int player, Person* person, bool respawn = false);
    // Attach person to unit
    // Send message to server, which person has which brain.
    void AttachPerson(Person* person);
    // Play sound on all clients (except itself)
    void PlaySound(RString name, Vector3Par position, Vector3Par speed, float volume, float freq, IWave* wave);
    // Change state of played sound on all clients (except itself)
    void SoundState(IWave* wave, SoundStateType state);
    // Register vehicle on client and send message to server (and other clients)
    bool CreateVehicle(Vehicle* veh, VehicleListType type, RString name, int idVeh);
    // Register whole AI structure for given AICenter (center, groups, subgroups, units)
    bool CreateCenter(AICenter* center); // whole AI structure
    // Register generic network object and send message to server (and other clients)
    bool CreateObject(NetworkObject* object);
    // Send first update of all created objects over network (as guaranteed message)
    void CreateAllObjects();
    // Unregister all objects, both local and remote
    void DestroyAllObjects();
    // Register command as network object, send message to others
    bool CreateCommand(AISubgroup* subgrp, int index, Command* cmd);
    // Unregister command, send message to others
    void DeleteCommand(AISubgroup* subgrp, int index, Command* cmd);
    // Send message to server that player changed his state
    void ClientReady(NetworkGameState state);
    // Request code execution
    bool PublicExec(RString command);
    // Targeted remote code execution
    bool RemoteExec(RString name, const AutoArray<char>& params, int target, const AutoArray<char>& targetSpec,
                    bool jip, RString jipKey, bool callMode);
    bool RemoteExecRemove(RString jipKey);

    // Ask object owner for damage of object
    void AskForDammage(Object* who, EntityAI* owner, Vector3Par modelPos, float val, float valRange, RString ammo);
    // Ask object owner for set of total damage of object
    void AskForSetDammage(Object* who, float dammage);
    // Ask vehicle owner for get in person
    void AskForGetIn(Person* soldier, Transport* vehicle, GetInPosition position);
    // Ask vehicle owner for get out person
    void AskForGetOut(Person* soldier, Transport* vehicle, bool parachute);
    // Ask vehicle owner for change person position
    void AskForChangePosition(Person* soldier, Transport* vehicle, UIActionType type);
    // Ask vehicle owner for aim weapon
    void AskForAimWeapon(EntityAI* vehicle, int weapon, Vector3Par dir);
    // Ask vehicle owner for aim observer turret
    void AskForAimObserver(EntityAI* vehicle, Vector3Par dir);
    // Ask vehicle owner for select weapon
    void AskForSelectWeapon(EntityAI* vehicle, int weapon);
    // Ask vehicle owner for change ammo state
    void AskForAmmo(EntityAI* vehicle, int weapon, int burst);
    // Ask vehicle owner for add impulse
    void AskForAddImpulse(Vehicle* vehicle, Vector3Par force, Vector3Par torque);
    // Ask object owner for move object
    void AskForMove(Object* vehicle, Vector3Par pos);
    // Ask object owner for move object
    void AskForMove(Object* vehicle, Matrix4Par trans);
    // Ask group owner for join groups
    void AskForJoin(AIGroup* join, AIGroup* group);
    // Ask group owner for join groups
    void AskForJoin(AIGroup* join, OLinkArray<AIUnit>& units);
    // Ask person owner for hide body
    void AskForHideBody(Person* vehicle);
    // Transfer explosion effects (explosion, smoke, etc.) to other clients
    void ExplosionDammageEffects(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos, Vector3Par dir,
                                 const AmmoType* type, bool enemyDammage);
    // Transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients
    void FireWeapon(EntityAI* vehicle, int weapon, const Magazine* magazine, EntityAI* target);
    // Send update of weapons to vehicle owner
    void UpdateWeapons(EntityAI* vehicle);
    // Ask vehicle to add weapon into cargo
    void AddWeaponCargo(VehicleSupply* vehicle, RString weapon);
    // Ask vehicle to remove weapon from cargo
    void RemoveWeaponCargo(VehicleSupply* vehicle, RString weapon);
    // Ask vehicle to add magazine into cargo
    void AddMagazineCargo(VehicleSupply* vehicle, const Magazine* magazine);
    // Ask vehicle to remove magazine from cargo
    void RemoveMagazineCargo(VehicleSupply* vehicle, int creator, int id);
    // Transfer init expression to other clients and execute it
    void VehicleInit(VehicleInitCmd& init);
    // Transfer message who is responsible to destroy vehicle to other clients
    void OnVehicleDestroyed(EntityAI* killed, EntityAI* killer);
    // Transfer message about damage of vehicle to other clients
    void OnVehicleDamaged(EntityAI* damaged, EntityAI* killer, float damage, RString ammo);
    // Transfer message about fired missile to other clients
    void OnIncomingMissile(EntityAI* target, RString ammo, EntityAI* owner);
    // Transfer info about (user made) marker creation to other clients
    void MarkerCreate(int channel, AIUnit* sender, RefArray<NetworkObject>& units, ArcadeMarkerInfo& info);
    // Transfer info about (user made) marker was deleted to other clients
    void MarkerDelete(RString name);
    // Ask flag (carrier) owner for assign new owner
    void SetFlagOwner(Person* owner, EntityAI* carrier);
    // Ask client owns flag owner for change of flag ownership
    void SetFlagCarrier(Person* owner, EntityAI* carrier);

    // Public variable to other clients
    void PublicVariable(RString name);
    // Send chat message
    void Chat(int channel, RString text);
    // Send chat message
    void Chat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text);
    // Send chat message
    void Chat(int channel, RString sender, RefArray<NetworkObject>& units, RString text);
    // Send radio message as chat (text and sentence)
    void RadioChat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text, RadioSentence& sentence);
    // Send text radio message as chat (sound and title)
    void RadioChatWave(int channel, RefArray<NetworkObject>& units, RString wave, AIUnit* sender, RString senderName);
    // Set channel for Voice Over Net
    void SetVoiceChannel(int channel);
    // Set channel for Voice Over Net
    void SetVoiceChannel(int channel, RefArray<NetworkObject>& units);
    int GetVoiceChannelForTests() const { return _voiceChannel; }
    // Send synthetic VoN frames through the normal client transport for tests.
    int SendVoiceTestTone(int frames, int amplitude);
    void GetVoiceSpeakers(AutoArray<NetVoiceSpeakerInfo, Poseidon::Foundation::MemAllocSA>& speakers) const;
    // Poseidon::VoNTransmitHealth as int (0 = Off / voice not initialized).
    int GetVoiceTransmitHealth() const;

    // Transfer file to server
    void TransferFileToServer(RString dest, RString source);
    // Retrieve state of file transfer
    void GetTransferStats(int& curBytes, int& totBytes);
    // Ask player to show target
    void ShowTarget(Person* vehicle, TargetType* target);
    // Ask player to show group direction
    void ShowGroupDir(Person* vehicle, Vector3Par dir);
    // Transfer activation of group synchronization
    /*
    \param grp synchronized group
    \param active state of synchronization
    */
    void GroupSynchronization(AIGroup* grp, int synchronization, bool active);
    // Transfer activation of detector (through radio)
    /*
    \param grp synchronized group
    \param active state of synchronization
    */
    void DetectorActivation(Detector* det, bool active);
    // Ask group owner to create new unit
    void AskForCreateUnit(AIGroup* group, RString type, Vector3Par position, RString init, float skill, Rank rank);
    // Ask vehicle owner to destroy vehicle
    void AskForDeleteVehicle(Entity* veh);

    // Ask subgroup to receive answer from unit
    void AskForReceiveUnitAnswer(AIUnit* from, AISubgroup* to, int answer);
    // Ask group owner to respawn player
    void AskForGroupRespawn(Person* person, EntityAI* killer);
    // Ask mine owner to activate it
    void AskForActivateMine(Mine* mine, bool activate);
    // Ask fireplace to inflame / put down
    void AskForInflameFire(Fireplace* fireplace, bool fire);
    // Ask vehicle for user defined animation
    void AskForAnimationPhase(Entity* vehicle, RString animation, float phase);
    // Copy unit info from one person to other
    void CopyUnitInfo(Person* from, Person* to);
    // Returns respawn mode
    RespawnMode GetRespawnMode() const { return _missionHeader.respawn; }
    // Returns respawn delay
    float GetRespawnDelay() const { return _missionHeader.respawnDelay; }
    // Add person to respawn queue (respawn after delay)
    void Respawn(Person* soldier, Vector3Par pos);

    // Checks if gamemaster is to select mission on dedicated server
    bool CanSelectMission() const { return _selectMission; }
    // Checks if client is to vote mission on dedicated server
    bool CanVoteMission() const { return _voteMission; }
    // Returns array of missions available on dedicated server
    const AutoArray<RString>& GetServerMissions() const { return _serverMissions; }
    // Select mission on dedicated server
    void SelectMission(RString mission, bool cadetMode);
    // Vote mission on dedicated server
    void VoteMission(RString mission, bool cadetMode);

    NetworkObject* GetObject(NetworkId& id) override;
    // Unregister given network object, send message to others
    void DeleteObject(NetworkId& id);

    // Process chat command for remote control of dedicated server
    bool ProcessCommand(RString command);

    // Ask server to kick off player
    void SendKick(int player);

    // Ask server to disable/enable connection of further clients
    void SendLockSession(bool lock = true);

    // Ask server to ban (and kick) a connected player by their network id
    void SendBan(int player);

    // Ask server to remove a player id (decimal) or IPv4 from its ban lists
    void SendUnban(const char* idOrIp);

    // player must disconnect due to some mission loading error
    void Disconnect(RString message);

    // Force update of network object
    void UpdateObject(NetworkObject* object, NetMsgFlags dwFlags);

    const char* GetDebugName() const override { return "Client"; }

    // Return estimated end of mission time
    Poseidon::Foundation::Time GetEstimatedEndTime() const;

    // Body can be hidden (for better performance)
    void DisposeBody(Person* body);

    // Game is paused due to disconnection state of game
    bool IsControlsPaused() { return _controlsPaused; }

    // Last received message's age in seconds (used to eliminate "disconnect cheat")
    float GetLastMsgAgeReliable();

    // implementation
  protected:
    // Find info about local object with given id (nullptr if not found)
    NetworkLocalObjectInfo* GetLocalObjectInfo(NetworkId& id);
    // Find info about remote object with given id (nullptr if not found)
    NetworkRemoteObjectInfo* GetRemoteObjectInfo(NetworkId& id);

    // Register network object as local object, assign id to them
    NetworkId CreateLocalObject(NetworkObject* object);

    // Force update of network object (single type of update)
    int UpdateObject(NetworkObject* object, NetworkMessageClass cls, NetMsgFlags dwFlags);
    // Update all network objects in AI structure for given AICenter (center, groups, subgroups, units)
    bool UpdateCenter(AICenter* center);

    // Prepare multiplayer game
    // Switch landscape, parse mission sqm file, initialize GWorld.
    bool PrepareGame();

    // Respawn unit by info given by item of respawn queue
    void DoRespawn(RespawnQueueItem& item);

    bool TryApplySelectPlayer(const SelectPlayerMessage& message, bool allowPending);
    void TryApplyPendingSelectPlayer(NetworkId id);
    bool TryApplyChangeOwner(const ChangeOwnerMessage& message, bool allowPending);
    void TryApplyPendingChangeOwner(NetworkId id);
    void TryApplyPendingChangeOwners();

    // Create network object and register as remote object
    template <class Type>
    bool CreateRemoteObject(NetworkMessageContext& ctx, Type* dummy)
    {
        if (_state < NGSLoadIsland)
            return false; // updates from the last session

        // check if object already exist
        NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
            const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());
        if (!indices)
            return false;

        NetworkId id;
        ctx.IdxTransfer(indices->objectCreator, id.creator);
        ctx.IdxTransfer(indices->objectId, id.id);

        if (GetObject(id))
        {
            RptF("Object %d:%d already exists", id.creator, id.id);
            return false;
        }

        // create object
        ctx.SetClass(NMCCreate);
        NetworkObject* obj = Type::CreateObject(ctx);
        if (!obj)
        {
            RptF("Cannot create object %d:%d", id.creator, id.id);
            return false;
        }

        // add to remote objects
        int index = _remoteObjects.Add();
        NetworkRemoteObjectInfo& info = _remoteObjects[index];
        info.id = id;
        info.object = obj;
        TryApplyPendingSelectPlayer(id);
        if (DiagLevel >= 1)
            DiagLogF("Client: remote object created %d:%d", id.creator, id.id);
        return true;
    }
    // Unregister and destroy remote network object
    void DestroyRemoteObject(NetworkId id);

    // Send all agregated and update messages
    void SendMessages();
    // Calculate limits for message sending
    void EstimateBandwidth(int& nMsgMax, int& nBytesMax);
    // Calculate errors and place object in list sorted by error
    void CreateObjectsList(AutoArray<UpdateLocalObjectInfo, Poseidon::Foundation::MemAllocSA>& objects);
    // Prepare next object update message to message queue
    bool PrepareNextUpdate(AutoArray<UpdateLocalObjectInfo, Poseidon::Foundation::MemAllocSA>& objects, int& next);

    // Return number of auto destroyed local objects - used for debugging purposes
    int NLocalObjectsNULL() const;
    // Check database of local objects - used for debugging purposes
    bool CheckLocalObjects() const;

    void EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type) override;
    void EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type) override;
};
