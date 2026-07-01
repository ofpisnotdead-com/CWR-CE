#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>

extern const int DefaultNetworkPort;
extern const char* DefaultMasterServer;
extern RString ClientIP;

int GetNetworkPort();
void SetNetworkPort(int port);
int GetNetworkConnectPort();
void SetNetworkConnectPort(int port);
RString GetNetworkBindAddress();
void SetNetworkBindAddress(const RString& address);
RString GetNetworkAdvertiseAddress();
void SetNetworkAdvertiseAddress(const RString& address);
RString GetNetworkPassword();
void SetNetworkPassword(const RString& password);
RString GetNetworkMasterServer();
void SetNetworkMasterServer(const RString& host);
RString FormatNetworkMasterServerAttribution(const RString& host);
bool GetNetworkPublicServer();
void SetNetworkPublicServer(bool value);
RString GetNetworkProxy();
void SetNetworkProxy(const RString& host);
bool IsDedicatedServer();

