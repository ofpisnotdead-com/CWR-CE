#pragma once

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

namespace Poseidon
{

// Max size in bytes of a client custom file (custom radio sound, custom face) the
// server accepts; a client transferring a larger file is kicked (see
// NetworkServerMsgOnMessage "too big custom file"). 0 blocks all custom files.
//
// Read from the main game config (FlashpointCfg) so it applies to both dedicated and
// self-hosted (listen) servers, mirroring the original OFP "MaxCustomFileSize"
// Flashpoint.cfg key. Returns `fallback` (the built-in default) when the key is absent.
inline int NetworkMaxCustomFileSizeFromCfg(const ParamFile& cfg, int fallback)
{
    const ParamEntry* entry = cfg.FindEntry("MaxCustomFileSize");
    return entry != nullptr ? entry->GetInt() : fallback;
}

} // namespace Poseidon
