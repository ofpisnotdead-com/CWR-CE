#pragma once

#include <Poseidon/Network/NetworkImplComponent.hpp>
#include <Poseidon/Network/IpBan.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>

#include <atomic>

// Info about single type of update of network object
struct NetworkCurrentInfo
{
    // type of message
    NetworkMessageType type = NMTNone;
    // sender of message
    int from = 0;
    // last received state of object
    Ref<NetworkMessage> message;
};

// info about state of sent updates of network object for single client
struct NetworkPlayerObjectInfo : public RefCount
{
    // ID of player
    int player;
    // state of updates
    NetworkUpdateInfo updates[NMCUpdateN];
    // allocated quite often
    USE_FAST_ALLOCATOR
};

// Info about network object on server

struct NetworkObjectInfo : public RefCount
{
    // ID of object
    NetworkId id;
    // DirectPlay ID of owner client
    int owner;
    // stored creation message for JIP world state sync
    NetworkCurrentInfo create;
    // state of last received updates
    NetworkCurrentInfo current[NMCUpdateN];
    // state of last sent updates (for each player)
    RefArray<NetworkPlayerObjectInfo> playerObjects;

    // Find info about network object for given player
    NetworkPlayerObjectInfo* GetPlayerObjectInfo(int player);
    // Create info (register) of new network object for given player
    NetworkPlayerObjectInfo* CreatePlayerObjectInfo(int player);
    // Delete info (unregister) network object for given player
    void DeletePlayerObjectInfo(int player);
    // Objects of this type will be allocated quite often
    USE_FAST_ALLOCATOR
};

// stores info about integrity question
struct IntegrityQuestion
{
    // question name
    RString name;
    // region in question
    int offset, size;
    IntegrityQuestion(const RString& n, int o, int s) : name(n), offset(o), size(s) {}
    IntegrityQuestion() : name(""), offset(0), size(INT_MAX) {}
};

class NetworkServer;
struct NetworkPlayerInfo;
struct NetworkConnectionSnapshot;
namespace Poseidon
{
struct MsgDescriptor;
struct MasterServerServiceRegistration;
} // namespace Poseidon

// inteface for more detailed integrity check tests
// when integrity check failed, we may wish to peform more detailed test
class IntegrityInvestigation : public RefCount
{
  public:
    // after test is finished, return test result information (if any)
    virtual RString GetResult() const = 0;
    // perform test iteration, return false if test finished
    virtual bool Proceed(NetworkServer* server, NetworkPlayerInfo* pi) = 0;
    // check if investigation asked given question
    virtual bool QuestionMatching(const IntegrityQuestion& q) const = 0;
    // tell investigation its question have been answered
    virtual void QuestionAnswered(bool answerOK) = 0;
};

class IntegrityInvestigationConfig : public IntegrityInvestigation
{
    // class where investigation started
    const ParamClass* _root;
    // class currently being tested
    const ParamClass* _class;
    // test result, if test is not fisnished yet, empty string
    RString _result;
    // last question - we are expecting an aswer to it
    IntegrityQuestion _question;
    // when we are expecting an answer to our question
    DWORD _questionTimeout;

  public:
    IntegrityInvestigationConfig(const char* path);
    RString GetResult() const override;
    bool Proceed(NetworkServer* server, NetworkPlayerInfo* pi) override;
    bool QuestionMatching(const IntegrityQuestion& q) const override;
    void QuestionAnswered(bool answerOK) override;
};

// calculate min, max and average from set of values
class CalculateMinMaxAvg
{
    float _min, _max, _sum;
    float _weight;

  public:
    CalculateMinMaxAvg();
    // add sample
    void Sample(float value, float weight = 1);
    // reset
    void Reset();
    // get current results (min)
    float GetMin() const { return _weight > 0 ? _min : 0; }
    // get current results (max)
    float GetMax() const { return _weight > 0 ? _max : 0; }
    // get current results (average)
    float GetAvg() const { return _weight > 0 ? _sum / _weight : 0; }
};

// complete information about one question asked
struct IntegrityQuestionInfo
{
    int id;
    IntegrityQuestionType type;
    IntegrityQuestion q;
    // Timeout for questions, UINT_MAX if question was never asked
    DWORD timeout;
    // constructor
    IntegrityQuestionInfo()
    {
        id = 0;
        timeout = UINT_MAX;
    }
};

// Server info about player
struct NetworkPlayerInfo
{
    // DirectPlay ID of player (client)
    int dpid;
    // Voice over net ID of player (client)
    int dvid;

    // chat channel player transmits
    int channel;

    // name of player
    RString name;
    // game state for given player
    NetworkGameState state;
    // message queue (candidates for agregation)
    NetworkMessageQueue _messageQueue, _messageQueueNonGuaranteed;

    // ping statistics
    CalculateMinMaxAvg _ping;
    // bandwidth statistics
    CalculateMinMaxAvg _bandwidth;
    // desync statistics
    CalculateMinMaxAvg _desync;

    // player's person
    NetworkId person;
    // player's unit
    NetworkId unit;
    // player's group
    NetworkId group;

    // client has actual (valid) mission pbo file
    bool missionFileValid;

    // kick-off already requested
    bool kickedOff;

    // Connection loosing (no message for 10 seconds) was already reported to other users
    bool connectionProblemsReported;

    // Player is joining into a game already in progress (JIP)
    bool jip;

    // Questions asked
    AutoArray<IntegrityQuestionInfo> integrityQuestions;
    // IntegrityQuestion integrityQuestion[IntegrityQuestionTypeCount];
    // DWORD integrityQuestionTimeout[IntegrityQuestionTypeCount];
    // when integrity check failed, we may wish to ask more detailed question
    Ref<IntegrityInvestigation> integrityInvestigation[IntegrityQuestionTypeCount];
    // time when next random integrity check should be performed
    DWORD integrityCheckNext;

    // position of camera on given client
    Vector3 cameraPosition;
    // time of last update of cameraPosition
    Poseidon::Foundation::Time cameraPositionTime;

    //{ DEDICATED SERVER SUPPORT
    // next message of the day index (-1 if done)
    int motdIndex;
    // next message of the day time
    Poseidon::Foundation::UITime motdTime;

    int nextQuestionId;

    int FindIntegrityQuestion(IntegrityQuestionType type, const IntegrityQuestion& q) const;
    int FindIntegrityQuestion(IntegrityQuestionType type) const;
    int FindIntegrityQuestion(int id) const;

    //}
};

// map person to unit in server database
struct PersonUnitPair
{
    // id of person
    NetworkId person;
    // id of unit
    NetworkId unit;
};

// free Win32 allocated memory when variable is destructed

class AutoFree
{
    // handle to Win32 memory obtained by GlobalAlloc call
    void* _mem;

  private:
    // no copy constructor
    AutoFree(const AutoFree& src);

  public:
    // attach Win32 memory handle
    void operator=(void* mem)
    {
        if (mem == _mem)
            return;
        Free();
        _mem = mem;
    }
    // copy - reassing Win32 handle to new object
    // source will be nullptr after assignement
    void operator=(AutoFree& src)
    {
        if (src._mem == _mem)
            return;
        Free();
        _mem = src._mem;
        src._mem = nullptr;
    }
    // empty constructor
    AutoFree() { _mem = nullptr; }
    // construct from Win32 memory handle
    AutoFree(void* mem) { _mem = mem; }
    // convert to pointer
    operator void*() { return _mem; }
    // destruct - free pointer
    ~AutoFree() { Free(); }
#ifdef _WIN32
    // free pointer explicitelly
    void Free()
    {
        if (_mem)
            GlobalFree(_mem), _mem = nullptr;
    }
#else
    // free pointer explicitelly
    void Free()
    {
        if (_mem)
            free(_mem), _mem = nullptr;
    }
#endif
};

// context structure for DownloadToFile function
struct DownloadToFileContext
{
    const char* url;                    // in: url to download from
    const char* file;                   // in: path of download destination
    bool result;                        // out: true when donwload was successful
    Poseidon::Foundation::Event* event; // in: event that should be signalled when download terminated
    const char* proxy;                  // in: address of proxy server
    size_t maxSize;                     // in: optional maximum downloaded bytes, 0 means unlimited
    std::atomic_bool done{false};       // out: true when download terminated
};

// context structure for DownloadToMem function

struct DownloadToMemContext
{
    const char* url;                    // in: url to download from
    AutoFree result;                    // out: memory with downloaded file
    size_t* size;                       // in: pointer to variable that will receive size of downloaded file
    Poseidon::Foundation::Event* event; // in: event that should be signalled when download terminated
    const char* proxy;                  // in: address of proxy server
    size_t maxSize;                     // in: optional maximum downloaded bytes, 0 means unlimited
    std::atomic_bool done{false};       // out: true when download terminated
};

// Single vote in votings
struct Vote
{
    // DirectPlay ID of player
    int player;
    // Voted value
    AutoArray<char> value;

    bool HasValue(const char* val, int valueSize) const;
};

// Single voting
class Voting : public AutoArray<Vote>
{
    friend class Votings;
    typedef AutoArray<Vote> base;

  protected:
    // Unique id of voting
    AutoArray<char> _id;
    // Threshold when voting is valid
    float _threshold;
    // Check if more than half players has the same value
    bool _selection;

  public:
    // Add new vote
    int Add(int player, char* value = nullptr, int valueSize = 0);

    // Return ID of Voting
    const AutoArray<char>& GetID() const { return _id; }

    // Check if voting is valid
    bool Check(const AutoArray<PlayerIdentity>& identities) const;
    // Get voted value
    const AutoArray<char>* GetValue(int* n = nullptr, int* n2 = nullptr) const;

    bool HasID(char* id, int idSize) const;
};

// information for client code verification
struct ExeCRCBlock
{
    int offset, size, crc;
};

class NetworkServer;

// Voting system
class Votings : public AutoArray<Voting>
{
    typedef AutoArray<Voting> base;

  public:
    // Add new vote
    void Add(NetworkServer* server, char* id, int idSize, float threshold, int player, char* value = nullptr,
             int valueSize = 0, bool selection = false);

    // Check all votings (apply if satisfied)
    void Check(NetworkServer* server);
};

// pending download of squad information
class CheckSquadObject
{
    friend class NetworkServer;

  protected:
    // identify player
    PlayerIdentity _identity;
    // out: squad information
    Ref<SquadIdentity> _squad;
    // out: squad was created during download
    bool _newSquad;

    // signalled when particular download step is done
    Poseidon::Foundation::Event _stateDone;
    enum State
    {
        DownloadingSquad,
        DownloadingLogo,
        AllDone
    };
    // state of download
    State _state;
    // XML data of the squad
    AutoFree _squadXMLData;
    // size of XML data
    size_t _squadXMLSize;
    // URL of squad logo picture
    RString _logoUrl;
    // path to squad logo picture (download destination)
    RString _logoFile;
    // address of proxy server
    RString _proxy;

    // state specific information (download logo file)
    DownloadToFileContext _logoContext;
    // state specific information (donwload squad XML to memory)
    DownloadToMemContext _squadContext;

  public:
    // start download
    CheckSquadObject(PlayerIdentity& identity, Ref<SquadIdentity> squad, bool newSquad, RString proxy);
    // destruct - wait until current download step is terminated
    ~CheckSquadObject();
    // check if download is terminated
    bool IsDone();

    // start XML data background download
    void StartDownloadingXMLSource();
    // end XML data background download
    void EndDownloadingXMLSource();

    // start logo file background download
    void StartDownloadingLogo();

    // process XML data, set default in case of error
    void ProcessXML();

  protected:
    // process XML data
    // \return true if data are present and good
    bool DoProcessXML();
};

struct UpdateObjectInfo;

// Network Server
struct NetworkCommandMessage;

class NetworkServer : public NetworkComponent
{
  protected:
    // low-level server implementation
    SRef<NetTranspServer> _server;

    // list of players (server specific info)
    AutoArray<NetworkPlayerInfo> _players;
    // list of network objects (actual state)
    RefArray<NetworkObjectInfo> _objects;

    // used to store pending messages
    struct NetPendingMessage
    {
        NetworkObjectInfo* info;
        NetworkPlayerObjectInfo* player;
        NetworkUpdateInfo* update;
        DWORD msgID;
    };

    // list of pending messages - used for fast message lookup
    AutoArray<NetPendingMessage> _pendingMessages;
    // map persons to units
    AutoArray<PersonUnitPair> _mapPersonUnit;

    // JIP: messages queued for late-joining players (publicVariable, etc.)
    struct JIPInitMessage
    {
        NetworkMessageType type;
        Ref<NetworkMessage> msg;
        RString key; // deduplication key (variable name for publicVariable)
    };
    AutoArray<JIPInitMessage> _jipMessages;
    Poseidon::RemoteExecPolicyMode _remoteExecPolicyMode = Poseidon::RemoteExecPolicyMode::DenyClient;
    AutoArray<RString> _remoteExecAllowedNames;

    // DirectPlay ID of bot client
    int _botClient;
    // original name of current mission
    RString _originalName;

    // no other players are enabled to connect
    bool _sessionLocked;

    // password protected session
    bool _password;

    // equal mod list required by host
    bool _equalModRequired;

    // GlobalTickCount when _state was last entered (published as state-elapsed)
    DWORD _stateEnteredTime;

    //{ DEDICATED SERVER SUPPORT
    // DirectPlay ID of logged gamemaster (AI_PLAYER if no gamemaster)
    int _gameMaster;
    // Latched true the first time an admin login is granted this session (never reset). A
    // session-scoped audit signal: lets a test observe an admin grant regardless of whether
    // the grantee is still connected when checked.
    bool _adminLoginGranted = false;
    // selected mission name
    RString _mission;
    // selected mission cadet / veteran mode
    bool _cadetMode;

    // gamemaster is only voted admin (cannot do shutdown etc.)
    bool _admin;

    // kick off players with duplicate ID
    bool _kickDuplicate;

    // is server dedicated
    bool _dedicated;
    // server is asked to restart mission
    bool _restart;
    // server is asked to reassign players
    bool _reassign;
    // index of next mission in dedicated server config file
    int _missionIndex;
    // preloaded dedicated server config file
    ParamFile _serverCfg;

    // Info about network trafic on dedicated server
    float _monitorInterval;
    DWORD _monitorNext;
    int _monitorFrames;
    int _monitorIn;
    int _monitorOut;

    DWORD _debugNext;
    float _debugInterval;
    // switched on debug flags
    FindArrayKey<int> _debugOn;

    // messages of the day
    AutoArray<RString> _motd;
    // messages of the day interval
    float _motdInterval;

    //}

    // time when next ping status should be broadcasted
    DWORD _pingUpdateNext;

    // full path to mission pbo file
    RString _missionBank;

    // global (static) ban list — player ids (ban.txt)
    FindArray<__int64> _banListGlobal;
    // local (dynamic) ban list — player ids (user-dir ban.txt)
    FindArray<__int64> _banListLocal;
    // global / local IPv4 ban lists (ipban.txt). Login checks both id and IP lists.
    FindArray<uint32_t> _banListIPGlobal;
    FindArray<uint32_t> _banListIPLocal;
    // what a runtime Ban() records; login always enforces both lists regardless
    Poseidon::BanMode _banMode = Poseidon::BanMode::Both;

    // actually tested identities (squad competence)
    AutoArray<SRef<CheckSquadObject>> _squadChecks;

    // next available (short) id for player
    int _nextPlayerId;

    // votings currently in progress
    Votings _votings;

    // threshold for votings
    float _voteThreshold;

    // address of proxy server
    RString _proxy;

  public:
    NetworkServer(NetworkManager* parent, int port, RString password);
    ~NetworkServer() override;

    // True if an admin login was granted at any point this session (latched, never reset).
    bool AdminLoginGranted() const { return _adminLoginGranted; }

    // Creation of DirectPlay server and MP session
    bool Init(int port, RString password);
    // Destroying of MP session and DirectPlay server
    void Done();

    // Kick off given player from game
    void KickOff(int dpnid, KickOffReason reason);
    // Ban given player (kick off + add to dynamic ban list)
    void Ban(int dpnid);
    // Remove a player id (decimal) or IPv4 from the dynamic ban lists
    void Unban(const char* idOrIp);
    // Broadcast a global system chat line to every connected player. Works on a
    // dedicated server, where GNetworkManager.Chat() is a no-op (no local client).
    void ChatToAllPlayers(RString message);
    // Number of dynamic (runtime) ban entries — id + IP. Used by tests.
    int GetBanCount() const { return _banListLocal.Size() + _banListIPLocal.Size(); }

    // Return if session was created
    bool IsValid() const { return _server != nullptr; }

    // Set DirectPlay ID of bot client
    void SetBotClient(int client) { _botClient = client; }

    bool IsDedicatedBotClient(int dpnid) const;

    bool IsLocalClient(int client) const override
    {
        // Assume bot client is always local
        return client == _botClient;
    }

    //{ DEDICATED SERVER SUPPORT
    // Mark server as dedicated
    void SetDedicated(RString config);
    //}

    // disable/enable connecting of further clients
    void LockSession(bool lock = true) { _sessionLocked = lock; }
    bool IsSessionLocked() const { return _sessionLocked; }

    // Retrieves URL of server
    bool GetURL(char* address, DWORD addressLen);

    // Creates pbo file for selected mission
    void CreateMission(RString mission, RString world);
    // Initialize mission
    // - create and send mission header structure (including difficulty settings)
    // - create and send slots for sides and roles
    void InitMission(bool cadetMode);

    void OnSimulate() override;
    void OnSendComplete(DWORD msgID, bool ok);
    void OnSendStarted(DWORD msgID, const NetworkMessageQueueItem& item);
    void OnCreatePlayer(int player, bool botClient, const char* name);
    bool OnCreateVoicePlayer(int player);
    void OnMessage(int from, NetworkMessage* msg, NetworkMessageType type) override;

    // Central per-message authorization, consulted by OnMessage before dispatch.
    // Enforces the MsgDescriptor's MsgAuth class; currently rejects ServerOrigin
    // messages (server -> client only) sent by a client.
    bool Authorize(const Poseidon::MsgDescriptor& d, int from);

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
    // Perform all active integrity investigations
    void PerformIntegrityInvestigations();
    // Perform initial (overall) integrity check
    void PerformInitialIntegrityCheck(NetworkPlayerInfo& pi);
    // Perform quick random integrity check
    void PerformRandomIntegrityCheck(NetworkPlayerInfo& pi);
    // perform check on single file
    void PerformFileIntegrityCheck(const char* file, int dpid = -1);
    // perform check on exe image in the memory
    void PerformExeIntegrityCheck(const char* location, int dpid = -1);

    // Check if given question is part of any investigation
    bool IntegrityAnswerReceived(NetworkPlayerInfo* pi, IntegrityQuestionType qType, const IntegrityQuestion& q,
                                 bool answerOK);

    DWORD SendMsg(int to, NetworkSimpleObject* object, NetMsgFlags dwFlags) override;
    bool DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags dwFlags) override;

    // Retrieve list of players
    void GetPlayers(AutoArray<NetPlayerInfo, Poseidon::Foundation::MemAllocSA>& players);

    // Snapshot per-connection state (dpid, name, game state, JIP flag) for the
    // harness `connections` query. Connection latency/bandwidth stats are
    // reported as zero pending the per-connection stats bookkeeping.
    void GetConnectionSnapshots(AutoArray<NetworkConnectionSnapshot, Poseidon::Foundation::MemAllocSA>& connections);

    NetworkMessageFormatBase* GetFormat(/*int client, */ int type) override;

    // Set current state of game (+ send to clients)
    void SetGameState(NetworkGameState state);
    // Return game state for given player
    NetworkGameState GetPlayerState(int dpid);
    // Return name for given player
    RString GetPlayerName(int dpid);
    // Return camera position for given player
    Vector3 GetCameraPosition(int dpid);

    NetworkObject* GetObject(NetworkId& id) override;

    // Unregister all objects
    void DestroyAllObjects();

    // Send mission pbo file to clients
    void SendMissionFile();

    // Check if all internal structures are in correct state
    bool CheckIntegrity() const;

    // Check if pending message structures are in correct state
    bool CheckIntegrityOfPendingMessages() const;

    const char* GetDebugName() const override { return "Server"; }

    // Ask client for check of integrity
    bool IntegrityCheck(int dpnid, IntegrityQuestionType type, const IntegrityQuestion& q);
    // Reaction when check of integrity failed for given client
    void OnIntegrityCheckFailed(int dpnid, IntegrityQuestionType type, const char* info, bool final);

    // Creates player identity
    // Called asynchronously when identity is confirmed
    void CreateIdentity(PlayerIdentity& ident, Ref<SquadIdentity> squad);

    // Process result of voting when voting is valid
    void ApplyVoting(const AutoArray<char>& id, const AutoArray<char>* value);

    // some admin state change - scan admin status of all players
    void UpdateAdminState();

    // Delete player info (unregister player)
    void OnPlayerDestroy(int dpid);

    // Set estimated end of mission time
    void SetEstimatedEndTime(Poseidon::Foundation::Time time);

    // Return maximal allowed number of players on server
    int GetMaxPlayers();

  protected:
    // Find info about given player
    NetworkPlayerInfo* GetPlayerInfo(int dpid);
    // Create player info (register player)
    NetworkPlayerInfo* OnPlayerCreate(int dpid, const char* name);

    // find pending message in pending message list based on ID
    int FindPendingMessage(DWORD msgID) const;
    // find pending message in pending message list based on update
    int FindPendingMessage(NetworkUpdateInfo* update) const;
    // add new pending message
    void AddPendingMessage(DWORD msgID, NetworkObjectInfo* info, NetworkUpdateInfo* update,
                           NetworkPlayerObjectInfo* player);

    // Find info about given network object
    NetworkObjectInfo* GetObjectInfo(NetworkId& id);
    // Create object info (register object), optionally store creation message for JIP
    NetworkObjectInfo* OnObjectCreate(NetworkId& id, int owner, NetworkMessage* msg = nullptr,
                                      NetworkMessageType type = NMTNone);
    // Delete object info (unregister object), return owner id
    int PerformObjectDestroy(const NetworkId& id);
    // Delete object info (unregister object) and send message about destruction
    void OnObjectDestroy(const NetworkId& id);

    // Called when update message received
    NetworkObjectInfo* OnObjectUpdate(NetworkId& id, int from, NetworkMessage* msg, NetworkMessageType type,
                                      NetworkMessageClass cls);
    // Send update message
    int UpdateObject(NetworkPlayerInfo* pInfo, NetworkObjectInfo* oInfo, NetworkMessageClass cls, NetMsgFlags dwFlags);
    // Calculate error multiplier
    float CalculateErrorCoef(NetworkMessageType type, Vector3Par cameraPosition, Vector3Val position);
    // Calculate error (difference) between two stored states (messages)
    float CalculateError(NetworkMessageType type, int dpid1, NetworkMessage* msg1, int dpid2, NetworkMessage* msg2);

    // Owner of some object changes
    void ChangeOwner(NetworkId& id, int from, int to);
    // Check and update objects ownership when group leader changes
    void UpdateGroupLeader(NetworkId& group, NetworkId& leader);

    // Send mission info (mission heade, side slots and role slots) to client
    void SendMissionInfo(int to, bool onlyPlayers = false);

    // Send complete world state to a JIP player (5-pass ordered object sync)
    void SendWorldState(int dpnid);

    // Set game state of given player
    void SetPlayerState(int dpid, NetworkGameState state);

    // Send all agregated and update messages
    void SendMessages();
    // Check if fade out players are to kicked off
    void CheckFadeOut();
    // Calculate limits for message sending
    void EstimateBandwidth(NetworkPlayerInfo& pInfo, int nPlayers, int& nMsgMax, int& nBytesMax);
    // Calculate errors and place object in list sorted by error
    void CreateObjectsList(AutoArray<UpdateObjectInfo, Poseidon::Foundation::MemAllocSA>& objects,
                           NetworkPlayerInfo& pInfo);
    // Prepare next object update message to message queue
    bool PrepareNextUpdate(NetworkPlayerInfo& pInfo,
                           AutoArray<UpdateObjectInfo, Poseidon::Foundation::MemAllocSA>& objects, int& next);

    // Dedicated server simulation
    void SimulateDS();

    // Show message in both console and chat
    void ServerMessage(const char* text);

    // Find unit id for given person id
    NetworkId PersonToUnit(NetworkId& person);
    // Find person id for given unit id
    NetworkId UnitToPerson(NetworkId& unit);
    // Add pair to map persons to unit
    int AddPersonUnitPair(NetworkId& person, NetworkId& unit);

    // Find players on chat channel
    void GetPlayersOnChannel(AutoArray<int, Poseidon::Foundation::MemAllocSA>& players, AutoArray<NetworkId> units,
                             DWORD from, bool voice);
    // Find players on chat channel
    void GetPlayersOnChannel(AutoArray<int, Poseidon::Foundation::MemAllocSA>& players, int channel, DWORD from,
                             bool voice);

    // Begin publishing this server to the master server
    void StartMasterServerReporting();
    // Stop publishing this server to the master server
    void StopMasterServerReporting();
    // Periodic master-server publish/heartbeat tick
    void UpdateMasterServerReporting();

  public:
    // Fill the MasterServerService registration record for this server
    // (publisher service-registration callback). Returns false if no
    // master-server host is configured or the server isn't up yet.
    bool FillMasterServerServiceRegistration(Poseidon::MasterServerServiceRegistration& registration);
    // Save world state to file for later restore
    bool SaveWorldState(const char* filename);
    // Load world state from file (populates JIP queue for replay)
    bool LoadWorldState(const char* filename);

  protected:
    // Update session description server sending
    void UpdateSessionDescription();
    void EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type) override;
    void EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type) override;

    //{ DEDICATED SERVER SUPPORT
    // Set interval in which dedicated server sends info about transfer rates to gamemaster
    void Monitor(float interval);
    void OnMonitorIn(int size) override;

    void DebugAsk(RString str, int from, bool fullAccess);
    void DebugAnswer(RString str);

    int ConsoleF(const char* format, ...);
    //}

    // Process message NMTPlayerRole
    void OnMessagePlayerRole(int from, NetworkMessageType type, NetworkMessageContext& ctx);

    // Process message NMTNetworkCommand (the NCMT* admin/command sub-dispatch)
    void OnNetworkCommand(int from, NetworkCommandMessage& cmd);

    // Process message NMTGameState (per-player game-state transitions)
    void OnGameStateMessage(int from, NetworkMessage* msg, NetworkMessageType type, NetworkMessageContext& ctx);

    // Process the askFor* / vehicle-command family (relays each to the target object's owner)
    void OnAskForMessage(int from, NetworkMessage* msg, NetworkMessageType type, NetworkMessageContext& ctx);

    // Process the object create*/update* family (object lifecycle + state sync)
    void OnObjectMessage(int from, NetworkMessage* msg, NetworkMessageType type, NetworkMessageContext& ctx);
};
