#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Poseidon
{

/// Install state of the licensed game-data package (packages/Combined on
/// desktop; user-supplied on iOS) relative to what's on disk.
enum class GameDataStatus
{
    Missing, ///< no game-data directory at all
    Partial, ///< directory exists but the manifest/required files don't -- an
             ///< interrupted or tampered unpack; not resumable, re-import.
    Ready    ///< manifest exists and every required path is present
};

/// Default directory the game-data package lives in / is imported into:
/// <UserContentDir>/GameData (same Documents/<appName>/ tree ModsDir/WorkshopDir
/// already root on iOS -- see Foundation::GamePaths). Production call sites use
/// this; the functions below take the directory explicitly so tests can point
/// them at an arbitrary temp dir instead of touching the real GamePaths
/// singleton (same pattern ModInstall.hpp's modsRoot-parameterized API uses).
std::string GameDataDir();

/// Paths (relative to a game-data dir), any one of which missing/empty means
/// the install is not usable. Deliberately hardcoded rather than read back out
/// of the manifest -- a user-editable or stale-version manifest shouldn't be
/// the source of truth for what the *current* app build needs.
std::vector<std::string> RequiredGameDataPaths();

/// Missing / Partial / Ready -- cheap enough to call on every launch. Ready
/// requires both <gameDataDir>/manifest.json and every RequiredGameDataPaths()
/// entry to exist (a file must exist and be non-empty; a directory must exist
/// and contain at least one entry).
GameDataStatus DetectGameDataStatus(const std::string& gameDataDir);

/// Writes <gameDataDir>/manifest.json after a successful unpack -- the only
/// thing that ever writes it, so its presence is the completion signal
/// DetectGameDataStatus relies on (no support for raw folder drops). sourceUrl
/// is empty when the archive came from "Import from Files" rather than a
/// download.
bool WriteGameDataManifest(const std::string& gameDataDir, const std::string& sourceUrl, int64_t sizeBytes,
                           int64_t fileCount, std::string* error = nullptr);

/// Deletes gameDataDir entirely (used by both the in-app "Clear Data" button
/// and the Settings.bundle reset flag). Status is Missing immediately after.
bool DeleteGameData(const std::string& gameDataDir, std::string* error = nullptr);

} // namespace Poseidon
