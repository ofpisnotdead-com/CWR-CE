#pragma once

// AutoTest is a GLOBAL variable (defined in Foundation/Platform/Shutdown.cpp).
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Network/Network.hpp>
extern bool AutoTest;

namespace Poseidon
{
void CopyDirectoryStructure(const char* dst, const char* src);
void RunInitScript();
void CreatePath(RString path);
void __cdecl CreateEditor(ControlsContainer* parent, bool multiplayer = false);

inline PackedColor ModAlpha(PackedColor color, float alpha)
{
    int a = toInt(alpha * color.A8());
    saturate(a, 0, 255);
    return PackedColorRGB(color, a);
}

} // namespace Poseidon

using ::Poseidon::CreatePath;

// Defined at global scope: GetIdentityText/ShowCinemaBorder in Network/World,
// GetNetworkPort/GetNetworkPassword in Network/NetworkConfig.cpp.
RString GetIdentityText(const PlayerIdentity& identity);
void ShowCinemaBorder(bool show);
int GetNetworkPort();
RString GetNetworkPassword();
