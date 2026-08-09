#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ModId.hpp"

namespace Poseidon
{

/// One downloadable mod in the workshop catalog, in the resolver's own terms (the
/// network/UI layer maps `MasterServerServiceModCatalogEntry` onto this so the
/// resolver stays a pure Core unit with no Network dependency).
class ModCatalogEntry
{
  public:
    ModCatalogEntry(ModId id, std::string name, std::string downloadUrl, int64_t sizeBytes)
        : _id(std::move(id)), _name(std::move(name)), _downloadUrl(std::move(downloadUrl)), _sizeBytes(sizeBytes)
    {
    }
    ModCatalogEntry(ModId id, std::string name, std::string downloadUrl, int64_t sizeBytes, std::string folderName,
                    int64_t packageRevision, std::string sha256, std::string version = {})
        : _id(std::move(id)), _name(std::move(name)), _downloadUrl(std::move(downloadUrl)),
          _folderName(std::move(folderName)), _sha256(std::move(sha256)), _version(std::move(version)),
          _sizeBytes(sizeBytes), _packageRevision(packageRevision)
    {
    }
    ModCatalogEntry(ModId id, std::string name, std::string downloadUrl, int64_t sizeBytes, std::string folderName)
        : _id(std::move(id)), _name(std::move(name)), _downloadUrl(std::move(downloadUrl)),
          _folderName(std::move(folderName)), _sizeBytes(sizeBytes)
    {
    }

    const ModId& Id() const { return _id; }
    const std::string& Name() const { return _name; }
    const std::string& DownloadUrl() const { return _downloadUrl; }
    const std::string& FolderName() const { return _folderName; }
    int64_t SizeBytes() const { return _sizeBytes; }
    int64_t PackageRevision() const { return _packageRevision; }
    const std::string& Sha256() const { return _sha256; }
    const std::string& Version() const { return _version; }

  private:
    ModId _id;
    std::string _name;
    std::string _downloadUrl;
    std::string _folderName;
    std::string _sha256;
    std::string _version;
    int64_t _sizeBytes = 0;
    int64_t _packageRevision = 1;
};

/// The set of mods available to download, looked up by id.
class ModCatalog
{
  public:
    void Add(ModCatalogEntry entry);
    const ModCatalogEntry* Find(const ModId& id) const; ///< nullptr if absent
    bool Contains(const ModId& id) const { return Find(id) != nullptr; }
    bool Empty() const { return _entries.empty(); }
    std::size_t Size() const { return _entries.size(); }
    const std::vector<ModCatalogEntry>& All() const { return _entries; }
    void MarkReachable() { _reachable = true; }
    bool IsReachable() const { return _reachable; }

  private:
    std::vector<ModCatalogEntry> _entries;
    std::unordered_map<ModId, std::size_t, ModId::Hash> _index;
    bool _reachable = false;
};

/// What a server requires of a joining client: a set of mod ids and whether the
/// match must be exact (no extra local mods). Parsed from the server's `mod` string.
class ServerModList
{
  public:
    ServerModList() = default;
    ServerModList(const std::string& modString, bool equalModRequired);
    ServerModList(std::vector<std::pair<std::string, int64_t>> packages, bool equalModRequired);

    const std::vector<ModId>& Required() const { return _required; }
    bool RequiresExactMatch() const { return _exact; }
    bool Requires(const ModId& id) const { return _set.count(id) != 0; }
    bool Empty() const { return _required.empty(); }
    int64_t RequiredRevision(const ModId& id) const;
    bool HasExactRevisions() const { return _hasExactRevisions; }

  private:
    std::vector<ModId> _required;
    std::unordered_set<ModId, ModId::Hash> _set;
    bool _exact = false;
    bool _hasExactRevisions = false;
    std::unordered_map<ModId, int64_t, ModId::Hash> _revisions;
};

/// A required mod that is absent on disk but present in the catalog — what we'd
/// download to satisfy the server.
struct ModDownload
{
    ModId id;
    std::string name; ///< catalog display name (falls back to the id)
    std::string downloadUrl;
    std::string folderName;
    int64_t sizeBytes = 0;
    int64_t packageRevision = 1;
    std::string sha256;
    std::string version;
};

struct InstalledModPackage
{
    ModId id;
    int64_t packageRevision = 1;
};

/// The diff between a server's required set and the player's state: the answer the
/// join UI renders and acts on.
class ServerModResolution
{
  public:
    const std::vector<ModId>& Satisfied() const { return _satisfied; }
    const std::vector<ModDownload>& ToDownload() const { return _toDownload; }
    const std::vector<ModId>& ToDisable() const { return _toDisable; }
    const std::vector<ModId>& Blocked() const { return _blocked; }

    /// Anything to do before joining with the right set.
    bool NeedsWork() const { return !_toDownload.empty() || !_toDisable.empty() || !_blocked.empty(); }
    /// Can we download + apply + join, or is a required mod simply unavailable?
    bool CanProceed() const { return _blocked.empty(); }

  private:
    friend class ServerModResolver;
    std::vector<ModId> _satisfied;
    std::vector<ModDownload> _toDownload;
    std::vector<ModId> _toDisable;
    std::vector<ModId> _blocked;
};

/// What the MP browser should do when the player clicks Join on a server, given the
/// resolution and whether the required set is already mounted.
enum class MpJoinAction
{
    ConnectDirect,    ///< required set already satisfied + mounted → just connect
    Download,         ///< some required mods are missing → download them first
    ApplyThenConnect, ///< mods are present but not the mounted set → re-mount, then connect
    Blocked           ///< a required mod isn't in the catalog → can't auto-join
};

/// Decide the join action. `alreadyMounted` is whether the engine's current mount path
/// already equals the server's required set (so no re-mount is needed). Pure.
inline MpJoinAction DecideMpJoinAction(const ServerModResolution& res, bool alreadyMounted)
{
    if (!res.CanProceed())
    {
        return MpJoinAction::Blocked;
    }
    if (!res.ToDownload().empty())
    {
        return MpJoinAction::Download;
    }
    return alreadyMounted ? MpJoinAction::ConnectDirect : MpJoinAction::ApplyThenConnect;
}

/// Resolves a server's required mod list against a player's installed + active mods.
/// Constructed from the player's state so one resolver can answer for many servers.
class ServerModResolver
{
  public:
    ServerModResolver(const std::vector<ModId>& installed, const std::vector<ModId>& active);
    ServerModResolver(const std::vector<InstalledModPackage>& installed, const std::vector<ModId>& active);

    ServerModResolution Resolve(const ServerModList& server, const ModCatalog& catalog) const;

  private:
    std::unordered_set<ModId, ModId::Hash> _installed;
    std::unordered_map<ModId, int64_t, ModId::Hash> _installedRevisions;
    std::vector<ModId> _active;
};

} // namespace Poseidon
