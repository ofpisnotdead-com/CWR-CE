#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#ifdef _MSC_VER
#pragma once
#endif

#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

// Description of multiplayer session
struct SessionInfo
{
	// URL of host
	RString guid;
	// Name of session
	RString name;
	// Last time session was accessed
	DWORD lastTime;
	// Host's current version
	int actualVersion;
	// Host's required version
	int requiredVersion;
	// Session is pasword protected
	bool password;
	// Host's version is too old
	bool badActualVersion;
	// Host's version is too new
	bool badRequiredVersion;
        // Mod lists differs
        bool badMod;
        // version tag differs
        bool badTag;
	// Mission name
	RString mission;
	// State of game on host
	int gameState;
	// Ping time (in ms)
	int ping;
	// Current number of players
	int numPlayers;
	// Maximal number of players
	int maxPlayers;
	// Estimated time to mission end (in minutes)
	int timeleft;
  // mod list on host
  RString mod;
  // equal mod list is required by host
  bool equalModRequired;
  // host's version tag (must match to join)
  RString versionTag;
};

#define MOD_LENGTH  80
#define VERSION_TAG_LENGTH 24

// Basic information sent in session description as user data
struct MPVersionInfoOld
{
	// actual application version
	int versionActual;
	// required application version
	int versionRequired;
	// name of running mission
	char mission[40];
	// state of multiplayer game
	int gameState;
};

struct MPVersionInfo : public MPVersionInfoOld
{
  // -mod list
  char mod[MOD_LENGTH];
  //! build version tag (must match to join)
  char versionTag[VERSION_TAG_LENGTH];
};

// Result of connecting client to server
enum ConnectResult
{
	// connect in progress
	CRNone = -1,
	// connected
	CROK,
	// bad password
	CRPassword,
	// incompatible program version
	CRVersion,
	// other error
	CRError,
	// session full (maximum number of players allotted for the session has been reached)
	CRSessionFull,
};

// flags for message
enum NetMsgFlags
{
	NMFNone=0,
	NMFGuaranteed=1,
	NMFHighPriority=2,
	NMFStatsAlreadyDone=4
};

enum NetTerminationReason
{
	NTRTimeout,
	NTRDisconnected,
	NTRKicked,
	NTRBanned,
	NTRMissingAddon,
	NTROther, // must be last
};

__forceinline NetMsgFlags operator | (NetMsgFlags a, NetMsgFlags b)
{
	return NetMsgFlags((int)a|(int)b);
}

// Send-completion info marshalled from the receive thread to the main application thread.
struct SendCompleteInfo
{
	DWORD msgID;
	bool ok;
};

// New-player info marshalled from the receive thread to the main application thread.
struct CreatePlayerInfo
{
	int player;
	bool botClient;
	char name[56];
  char mod[MOD_LENGTH];
  char versionTag[VERSION_TAG_LENGTH];
};

// Removed-player info marshalled from the receive thread to the main application thread.
struct DeletePlayerInfo
{
	int player;
};

// additional remote host info
struct RemoteHostAddress
{
	RString name;
	RString ip;
	int port;
};

typedef void UserMessageClientCallback(char *buffer, int bufferSize, void *context);
typedef void UserMessageServerCallback(int from, char *buffer, int bufferSize, void *context);
typedef void SendCompleteCallback(DWORD msgID, bool ok, void *context);
typedef void CreatePlayerCallback(int player, bool botClient, const char *name, const char *mod, void *context);
typedef void DeletePlayerCallback(int player, void *context);
typedef bool CreateVoicePlayerCallback(int player, void *context);

// Interface for class supporting session enumeration
class NetTranspSessionEnum
{
public:
	NetTranspSessionEnum() {}
	virtual ~NetTranspSessionEnum() {}

	virtual bool Init( int magic =0 )= 0;

	virtual bool RunningEnumHosts() = 0;
	virtual bool StartEnumHosts(RString ip, int port, AutoArray<RemoteHostAddress> *hosts) = 0;
	virtual void StopEnumHosts() = 0;

	virtual int NSessions() = 0;
	virtual void GetSessions(AutoArray<SessionInfo> &sessions) = 0;

	// Transfer IP address and port into URL address
	virtual RString IPToGUID(RString ip, int port) = 0;
};

// Interface for 3D sound buffer
class NetTranspSound3DBuffer
{
public:
	NetTranspSound3DBuffer() {}
	virtual ~NetTranspSound3DBuffer() {}

	virtual void SetPosition(float x, float y, float z) = 0;
};

// Interface for network transport client class
class NetTranspClient
{
public:
	NetTranspClient() {}
	virtual ~NetTranspClient() {}

	// Connect to a host. port is in/out: on input the expected host port (from
	// session enumeration or user input), on output the real session port
	// connected to. address, password, player, versionInfo and botClient are
	// forwarded in the connection packet; magic identifies the application.
	virtual ConnectResult Init
	(
		RString address, RString password, bool botClient, int &port,
		RString player, MPVersionInfo &versionInfo, int magic =0
	)= 0;
	virtual bool InitVoice() = 0;

	virtual bool SendMsg(BYTE *buffer, int bufferSize, DWORD &msgID, NetMsgFlags flags) = 0;
	virtual void GetSendQueueInfo(int &nMsg, int &nBytes, int &nMsgG, int &nBytesG) = 0;
	virtual bool GetConnectionInfo(int &latencyMS, int &throughputBPS) = 0;
	virtual bool GetConnectionInfoRaw(int &latencyMS, int &throughputBPS)
	{
		return GetConnectionInfo(latencyMS, throughputBPS);
	}

    virtual void SetNetworkParams ( const ParamEntry &cfg )
    {}

	virtual float GetLastMsgAge() = 0;
	virtual float GetLastMsgAgeReliable() = 0;
	virtual float GetLastMsgAgeReported() = 0;
	virtual void LastMsgAgeReported() = 0;

	virtual bool IsSessionTerminated() = 0;
	virtual NetTerminationReason GetWhySessionTerminated() = 0;

	virtual bool IsVoicePlaying(int player) = 0;
	virtual bool IsVoiceRecording() = 0;
	virtual NetTranspSound3DBuffer *Create3DSoundBuffer(int player) = 0;
	virtual void SetVoiceChannel(int channel) {}
	virtual void SetVoiceTransmit(bool on) {}
	virtual int SendVoiceTestTone(int frames, int amplitude) { return 0; }

	virtual void ProcessUserMessages(UserMessageClientCallback *callback, void *context) = 0;
	virtual void RemoveUserMessages() = 0;
	virtual void ProcessSendComplete(SendCompleteCallback *callback, void *context) = 0;
	virtual void RemoveSendComplete() = 0;

    virtual RString GetStatistics ()
    { return RString(); }

    virtual unsigned FreeMemory ()
    { return 0; }

};

// Interface for network transport server class
class NetTranspServer
{
public:
	NetTranspServer() {}
	virtual ~NetTranspServer() {}

	// Start hosting. port is the recommended IP port. sessionNameFormat is a
	// printf-style template (playerName and hostName as parameters);
	// sessionNameInit is used when the host address cannot be obtained.
	// versionInfo decides whether a client may connect; password and maxPlayers
	// are advertised in the enumeration response.
	virtual bool Init
	(
		int port, RString password, char *hostname, int maxPlayers,
		RString sessionNameInit, RString sessionNameFormat, RString playerName,
    MPVersionInfo &versionInfo, bool equalModRequired, int magic =0
	)= 0;
	virtual bool InitVoice() = 0;

	virtual RString GetSessionName() = 0;
	// Return real local port of session
	virtual int GetSessionPort() = 0;

	virtual bool SendMsg(int to, BYTE *buffer, int bufferSize, DWORD &msgID, NetMsgFlags flags) = 0;
	virtual void CancelAllMessages() = 0;
	virtual void GetSendQueueInfo(int to, int &nMsg, int &nBytes, int &nMsgG, int &nBytesG) = 0;
	virtual bool GetConnectionInfo(int to, int &latencyMS, int &throughputBPS) = 0;
	virtual bool GetConnectionInfoRaw(int to, int &latencyMS, int &throughputBPS)
	{
		return GetConnectionInfo(to, latencyMS, throughputBPS);
	}
    virtual void SetNetworkParams ( const ParamEntry &cfg )
    {}
	virtual float GetLastMsgAgeReliable(int player) = 0;

	virtual void UpdateSessionDescription(int state, RString mission) = 0;
	virtual void KickOff(int player, NetTerminationReason reason) = 0;
	// Dotted IPv4 of a connected player's remote address ("" if unknown). Used for IP bans.
	virtual RString GetPlayerHostIP(int player) { return RString(); }
	// Retrieves URL of server
	virtual bool GetURL(char *address, DWORD addressLen) = 0;

	virtual void GetTransmitTargets(int from, AutoArray<int, Poseidon::Foundation::MemAllocSA> &to) = 0;
	virtual void SetTransmitTargets(int from, AutoArray<int, Poseidon::Foundation::MemAllocSA> &to, int channel) = 0;

	virtual void ProcessUserMessages(UserMessageServerCallback *callback, void *context) = 0;
	virtual void RemoveUserMessages() = 0;
	virtual void ProcessSendComplete(SendCompleteCallback *callback, void *context) = 0;
	virtual void RemoveSendComplete() = 0;
	virtual void ProcessPlayers(CreatePlayerCallback *callbackCreate, DeletePlayerCallback *callbackDelete, void *context) = 0;
	virtual void RemovePlayers() = 0;
	virtual void ProcessVoicePlayers(CreateVoicePlayerCallback *callback, void *context) = 0;
	virtual void RemoveVoicePlayers() = 0;

    virtual RString GetStatistics (int player)
    { return RString(); }

    virtual unsigned FreeMemory ()
    { return 0; }

};

// Create Net implementation of NetTranspSessionEnum class
NetTranspSessionEnum *CreateNetSessionEnum( const ParamEntry &cfg );
// Create Net implementation of NetTranspClient class
NetTranspClient *CreateNetClient( const ParamEntry &cfg );
// Create Net implementation of NetTranspServer class
NetTranspServer *CreateNetServer( const ParamEntry &cfg );

