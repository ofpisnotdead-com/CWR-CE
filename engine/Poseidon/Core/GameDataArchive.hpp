#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Poseidon
{

/// Unpacks a user-supplied (downloaded or picked-from-Files) zip archive
/// containing the game-data tree (DTA/, AddOns/, BIN/, ...) into a directory.
/// General zip container -- unlike ModArchive, which only streams a single
/// zstd-wrapped PBO. The source is untrusted (a URL or a file the user picked),
/// so entry paths are validated against directory-escape ("zip slip") before
/// anything is written.
class GameDataArchive
{
public:
    /// onProgress(entriesDone, entriesTotal), called after each entry -- may be
    /// empty. Returns true on success; on failure writes a short message to
    /// *error when non-null and leaves any partially-written output in destDir
    /// (the caller's job to remove -- DetectGameDataStatus will read that as
    /// Partial since no manifest gets written for a failed unpack).
    static bool Unpack(const char* archivePath, const char* destDir,
                       const std::function<void(int64_t entriesDone, int64_t entriesTotal)>& onProgress = {},
                       std::string* error = nullptr);
};

} // namespace Poseidon
