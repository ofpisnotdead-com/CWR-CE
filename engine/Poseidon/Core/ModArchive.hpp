#pragma once

#include <string>

namespace Poseidon
{
/// Unpacks a downloaded mod archive into a directory on disk.
/// Mods ship as zstd-wrapped PBOs (`<name>.pbo.zst`) — the only supported
/// format. The archive is streamed through zstd into a temporary PBO, then each
/// entry is written under destDir using its in-archive path (backslashes
/// normalized to forward slashes); PBO-level compressed entries are
/// transparently decompressed by the bank reader. Returns true on success; on
/// failure writes a short message to *error when non-null.
class ModArchive
{
public:
  static bool IsSafeEntryPath(const std::string& entryName);
  static bool Unpack(const char* archivePath, const char* destDir, std::string* error = nullptr);

  /// Stream-decode a zstd-wrapped archive (`<name>.pbo.zst`) into a plain file at pboPath.
  /// Decodes chunk-by-chunk so a multi-GB mod never lands fully in memory. Returns false with
  /// *error set on a missing source, a truncated frame, or non-zstd input.
  static bool DecompressArchive(const char* archivePath, const char* pboPath, std::string* error = nullptr);
};
} // namespace Poseidon
