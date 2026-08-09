#pragma once

// GraphicsConfig → live engine bridge.
//
// The Settings/ layer is allowed to know about engine globals
// (GScene + GEngine) here because this module is the boundary
// between "config value" and "running renderer".  Two callers:
//
//   - GameApplication::LoadAndApplyGraphicsConfig at boot.
//   - GraphicsPage's row handlers on every live row change.
//
// Tier→engine-value mappings live in this TU so both callers see the
// same translation table; changing a mapping touches one place.

#include <Poseidon/UI/Settings/GraphicsConfig.hpp>

// Push every cfg field into the live engine state.  Idempotent — safe
// to call repeatedly, e.g. after every row change.  Reads GScene +
// GEngine + writes to gUserFpsCap (the FPS-cap global defined in
// engine/Poseidon/Core/Game/GameLoop.cpp).

namespace Poseidon
{
float TerrainGridForTier(GraphicsConfig::Tier tier);
void ApplyGraphicsConfigToEngine(const GraphicsConfig& cfg);

} // namespace Poseidon
