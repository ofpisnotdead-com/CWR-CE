#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

const int DefaultNetworkPort = 1985;
const char* DefaultMasterServer = "https://papa-bear.cz";
static int NetworkPort = DefaultNetworkPort;
static int NetworkConnectPort = 0;
static RString NetworkBindAddress = "0.0.0.0";
static RString NetworkAdvertiseAddress;
static RString NetworkPassword;
static RString NetworkMasterServer = DefaultMasterServer;
static bool NetworkPublicServer = false;
RString ClientIP;

int GetNetworkPort()
{
    return NetworkPort;
}

void SetNetworkPort(int port)
{
    NetworkPort = port;
}

int GetNetworkConnectPort()
{
    return NetworkConnectPort;
}

void SetNetworkConnectPort(int port)
{
    NetworkConnectPort = port;
}

RString GetNetworkBindAddress()
{
    return NetworkBindAddress;
}

void SetNetworkBindAddress(const RString& address)
{
    NetworkBindAddress = address;
}

RString GetNetworkAdvertiseAddress()
{
    return NetworkAdvertiseAddress;
}

void SetNetworkAdvertiseAddress(const RString& address)
{
    NetworkAdvertiseAddress = address;
}

RString GetNetworkPassword()
{
    return NetworkPassword;
}

void SetNetworkPassword(const RString& password)
{
    NetworkPassword = password;
}

RString GetNetworkMasterServer()
{
    return NetworkMasterServer;
}

void SetNetworkMasterServer(const RString& host)
{
    NetworkMasterServer = host;
}

bool GetNetworkPublicServer()
{
    return NetworkPublicServer;
}

void SetNetworkPublicServer(bool value)
{
    NetworkPublicServer = value;
}

static RString NetworkProxy;

RString GetNetworkProxy()
{
    return NetworkProxy;
}

void SetNetworkProxy(const RString& host)
{
    NetworkProxy = host;
}

bool IsDedicatedServer()
{
    return ENGINE_CONFIG.doCreateDedicatedServer;
}
