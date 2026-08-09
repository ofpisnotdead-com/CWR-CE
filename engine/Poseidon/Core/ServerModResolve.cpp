#include "ServerModResolve.hpp"

namespace Poseidon
{

void ModCatalog::Add(ModCatalogEntry entry)
{
    const ModId id = entry.Id();
    if (id.Empty() || _index.count(id) != 0)
    {
        return; // skip blank or duplicate ids (first wins)
    }
    _index.emplace(id, _entries.size());
    _entries.push_back(std::move(entry));
}

const ModCatalogEntry* ModCatalog::Find(const ModId& id) const
{
    auto it = _index.find(id);
    return it != _index.end() ? &_entries[it->second] : nullptr;
}

ServerModList::ServerModList(const std::string& modString, bool equalModRequired) : _exact(equalModRequired)
{
    std::size_t start = 0;
    while (start <= modString.size())
    {
        std::size_t sep = modString.find(';', start);
        if (sep == std::string::npos)
        {
            sep = modString.size();
        }
        ModId id(modString.substr(start, sep - start));
        if (!id.Empty() && _set.insert(id).second)
        {
            _required.push_back(std::move(id));
        }
        start = sep + 1;
    }
}

ServerModList::ServerModList(std::vector<std::pair<std::string, int64_t>> packages, bool equalModRequired)
    : _exact(equalModRequired), _hasExactRevisions(true)
{
    for (auto& package : packages)
    {
        ModId id(package.first);
        if (id.Empty() || !_set.insert(id).second)
            continue;
        _required.push_back(id);
        _revisions.emplace(std::move(id), package.second > 0 ? package.second : 1);
    }
}

int64_t ServerModList::RequiredRevision(const ModId& id) const
{
    const auto it = _revisions.find(id);
    return it == _revisions.end() ? 1 : it->second;
}

ServerModResolver::ServerModResolver(const std::vector<ModId>& installed, const std::vector<ModId>& active)
    : _active(active)
{
    for (const ModId& id : installed)
    {
        if (!id.Empty())
        {
            _installed.insert(id);
            _installedRevisions.emplace(id, 1);
        }
    }
}

ServerModResolver::ServerModResolver(const std::vector<InstalledModPackage>& installed,
                                     const std::vector<ModId>& active)
    : _active(active)
{
    for (const InstalledModPackage& package : installed)
    {
        if (package.id.Empty())
            continue;
        _installed.insert(package.id);
        _installedRevisions.emplace(package.id, package.packageRevision > 0 ? package.packageRevision : 1);
    }
}

ServerModResolution ServerModResolver::Resolve(const ServerModList& server, const ModCatalog& catalog) const
{
    ServerModResolution out;

    for (const ModId& id : server.Required())
    {
        const int64_t requiredRevision = server.RequiredRevision(id);
        const auto installed = _installedRevisions.find(id);
        const ModCatalogEntry* catalogEntry = catalog.Find(id);
        if (server.HasExactRevisions() && catalogEntry != nullptr &&
            catalogEntry->PackageRevision() != requiredRevision)
        {
            out._blocked.push_back(id);
            if (catalogEntry->PackageRevision() > requiredRevision &&
                out._blockReason != MpJoinBlockReason::RevisionUnavailable)
                out._blockReason = MpJoinBlockReason::ServerOutdated;
            else
                out._blockReason = MpJoinBlockReason::RevisionUnavailable;
            continue;
        }
        if (server.HasExactRevisions() && catalogEntry == nullptr && catalog.IsReachable())
        {
            out._blocked.push_back(id);
            out._blockReason = MpJoinBlockReason::RevisionUnavailable;
            continue;
        }
        if (installed != _installedRevisions.end() &&
            (!server.HasExactRevisions() || installed->second == requiredRevision))
        {
            out._satisfied.push_back(id);
            continue;
        }
        if (catalogEntry != nullptr)
        {
            out._toDownload.push_back({id, catalogEntry->Name().empty() ? id.Value() : catalogEntry->Name(),
                                       catalogEntry->DownloadUrl(), catalogEntry->FolderName(),
                                       catalogEntry->SizeBytes(), catalogEntry->PackageRevision(),
                                       catalogEntry->Sha256(), catalogEntry->Version()});
        }
        else
        {
            out._blocked.push_back(id);
            out._blockReason = MpJoinBlockReason::RevisionUnavailable;
        }
    }

    // Only an exact-match server forces extras off; otherwise the player keeps them.
    if (server.RequiresExactMatch())
    {
        std::unordered_set<ModId, ModId::Hash> seen;
        for (const ModId& id : _active)
        {
            if (id.Empty() || server.Requires(id))
            {
                continue;
            }
            if (seen.insert(id).second)
            {
                out._toDisable.push_back(id);
            }
        }
    }

    return out;
}

} // namespace Poseidon
