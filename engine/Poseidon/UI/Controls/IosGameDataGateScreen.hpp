#pragma once

namespace Poseidon
{

// Blocks (via a nested NSRunLoop pump -- see IosBootModalSpike, which this
// supersedes) until the game-data package (GameDataDir(), see
// Core/GameDataInstall.hpp) is Ready, showing an on-device import screen
// (paste a URL or pick a local zip via Files) if it isn't already. Checks and
// clears the Settings.bundle "reset on next launch" flag first. No-op on
// non-iOS and returns immediately when data is already Ready.
void RunIosGameDataGate();

} // namespace Poseidon
