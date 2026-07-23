#include <Poseidon/Core/ModCollection.hpp>

#include <Poseidon/Foundation/Strings/LocaleCollate.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#include <cjson/cJSON.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace Poseidon
{
namespace
{
std::string LowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Read a top-level string field from <modDir>/mod.json, or "" if absent/unreadable.
std::string ReadModJsonString(const std::filesystem::path& modDir, const char* key)
{
    const std::filesystem::path metadata = modDir / "mod.json";
    std::error_code ec;
    if (!std::filesystem::exists(metadata, ec))
        return {};
    std::ifstream in(metadata, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    cJSON* root = cJSON_Parse(buffer.str().c_str());
    std::string out;
    if (root != nullptr)
    {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (cJSON_IsString(item) && item->valuestring != nullptr)
            out = item->valuestring;
        cJSON_Delete(root);
    }
    return out;
}

// Total size of every regular file under dir (recursive). Best-effort: I/O errors
// stop the walk and return what was summed so far.
int64_t DirSizeBytes(const std::filesystem::path& dir)
{
    std::error_code ec;
    int64_t total = 0;
    std::filesystem::recursive_directory_iterator it(dir, ec), end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
            break;
        std::error_code fec;
        if (it->is_regular_file(fec))
            total += static_cast<int64_t>(it->file_size(fec));
    }
    return total;
}

// Display name: the mod.json "name" when present, else the folder name with a
// leading '@' (the OFP convention) trimmed for readability.
std::string ModDisplayName(const std::string& folder, const std::string& jsonName)
{
    if (!jsonName.empty())
        return jsonName;
    if (!folder.empty() && folder[0] == '@')
        return folder.substr(1);
    return folder;
}

// Last path component, verbatim (keeps the '@' and case). Handles both '/' and '\'
// regardless of host OS so a mount-path string round-trips identically everywhere
// (mount paths are native — '\' on Windows — but the unit tests pin both forms).
std::string FolderBasename(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    const std::size_t slash = p.find_last_of("/\\");
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

std::string Trim(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string AbsoluteResolvedModPath(const std::filesystem::path& candidate)
{
    std::error_code ec;
    const std::string abs = std::filesystem::absolute(candidate, ec).string();

    char real[1024];
    if (ResolveFilePath(abs.c_str(), real, sizeof(real)) && LooksLikeMod(real))
        return real;

    if (LooksLikeMod(abs))
    {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(abs, ec);
        return canonical.empty() ? abs : canonical.string();
    }

    return {};
}

std::string JoinCandidatesForError(const std::vector<std::filesystem::path>& candidates)
{
    std::string out;
    std::error_code ec;
    for (const auto& candidate : candidates)
    {
        if (!out.empty())
            out += ", ";
        out += std::filesystem::absolute(candidate, ec).string();
    }
    return out;
}

bool ModMatchesToken(const Mod& mod, const std::string& token)
{
    const std::string key = LowerAscii(token);
    if (LowerAscii(mod.id) == key)
        return true;
    if (!mod.catalogId.empty() && LowerAscii(mod.catalogId) == key)
        return true;
    return ModId(mod.id) == ModId(token) || (!mod.catalogId.empty() && ModId(mod.catalogId) == ModId(token));
}

std::string ResolveFromRootByMetadata(const std::filesystem::path& root, const std::string& token)
{
    ModLoader loader;
    loader.AddRoot(root.string(), ModSource::Local);
    const ModCollection mods = loader.Load();
    const Mod* mod = mods.Find(token);
    return mod != nullptr ? mod->path : std::string();
}
} // namespace

bool LooksLikeMod(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec))
    {
        if (ec)
            break;
        const std::string name = LowerAscii(it->path().filename().string());
        std::error_code mec;
        // addons/dta/campaigns + a bin/ config override are what a mod changes.
        // Missions/MPMissions are deliberately excluded — mission packs are a
        // separate content type, not mods.
        if (it->is_directory(mec) && (name == "addons" || name == "dta" || name == "bin" || name == "campaigns"))
            return true;
        if (it->is_regular_file(mec) && name == "mod.json")
            return true;
    }
    return false;
}

std::vector<Mod> ScanModsRoot(const std::string& root, ModSource source)
{
    namespace fs = std::filesystem;
    std::vector<Mod> mods;
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return mods;

    for (fs::directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
            break;
        std::error_code dec;
        if (!it->is_directory(dec))
            continue;
        const std::string folder = it->path().filename().string();
        if (folder.empty() || !LooksLikeMod(it->path().string()))
            continue;

        Mod m;
        m.id = folder; // the folder name verbatim — any name, '@' optional
        m.catalogId = ReadModJsonString(it->path(), "modId");
        m.name = ModDisplayName(folder, ReadModJsonString(it->path(), "name"));
        m.version = ReadModJsonString(it->path(), "version");
        m.path = it->path().string();
        m.sizeBytes = DirSizeBytes(it->path());
        m.source = source;
        mods.push_back(std::move(m));
    }

    std::sort(mods.begin(), mods.end(),
              [](const Mod& a, const Mod& b) { return Foundation::CollateUtf8(a.name.c_str(), b.name.c_str()) < 0; });
    return mods;
}

void ModCollection::Add(Mod mod)
{
    _mods.push_back(std::move(mod));
}

bool ModCollection::Contains(const std::string& id) const
{
    return Find(id) != nullptr;
}

const Mod* ModCollection::Find(const std::string& id) const
{
    for (const Mod& m : _mods)
        if (ModMatchesToken(m, id))
            return &m;
    return nullptr;
}

std::string ModCollection::MountPath(const std::vector<std::string>& enabledIds) const
{
    std::string out;
    for (const std::string& id : enabledIds)
    {
        const Mod* m = Find(id);
        if (m == nullptr)
            continue;
        if (!out.empty())
            out += ';';
        out += m->path;
    }
    return out;
}

std::string ModCollection::MountPathForIds(const std::vector<ModId>& ids) const
{
    std::string out;
    for (const ModId& id : ids)
    {
        for (const Mod& m : _mods)
        {
            if (ModId(m.id) == id || (!m.catalogId.empty() && ModId(m.catalogId) == id))
            {
                if (!out.empty())
                    out += ';';
                out += m.path;
                break;
            }
        }
    }
    return out;
}

std::string ModCollection::FormatMountPath() const
{
    std::string out;
    for (const Mod& m : _mods)
    {
        if (!out.empty())
            out += ';';
        out += m.path;
    }
    return out;
}

std::string ModCollection::FormatNames() const
{
    std::string out;
    for (const Mod& m : _mods)
    {
        if (!out.empty())
            out += ';';
        out += !m.catalogId.empty() ? m.catalogId : m.id;
    }
    return out;
}

ModCollection ActiveModsFromMountPath(const std::string& mountPath)
{
    ModCollection col;
    std::stringstream ss(mountPath);
    std::string entry;
    while (std::getline(ss, entry, ';'))
    {
        if (entry.empty())
            continue;
        Mod m;
        m.id = FolderBasename(entry); // the folder name verbatim — the mod identity, 1:1
        m.catalogId = ReadModJsonString(std::filesystem::path(entry), "modId");
        m.name = ModDisplayName(m.id, ReadModJsonString(std::filesystem::path(entry), "name"));
        m.version = ReadModJsonString(std::filesystem::path(entry), "version");
        m.path = entry;
        col.Add(std::move(m));
    }
    return col;
}

ModPathResolveResult ResolveModPathList(const ModPathResolveRequest& request)
{
    ModPathResolveResult result;

    std::istringstream ss(request.modPaths);
    std::string segment;
    while (std::getline(ss, segment, ';'))
    {
        segment = Trim(segment);
        if (segment.empty())
            continue;

        const std::filesystem::path requested(segment);
        std::vector<std::filesystem::path> candidates;

        if (requested.is_absolute())
        {
            candidates.push_back(requested);
        }
        else if (!request.explicitModsDir.empty())
        {
            candidates.push_back(std::filesystem::path(request.explicitModsDir) / requested);
        }
        else
        {
            candidates.push_back(std::filesystem::path(request.currentDir.empty() ? "." : request.currentDir) /
                                 requested);
            if (!request.gameDir.empty())
                candidates.push_back(std::filesystem::path(request.gameDir) / requested);
            if (!request.managedModsDir.empty())
                candidates.push_back(std::filesystem::path(request.managedModsDir) / requested);
        }

        std::string resolved;
        for (const auto& candidate : candidates)
        {
            resolved = AbsoluteResolvedModPath(candidate);
            if (!resolved.empty())
                break;
        }

        if (resolved.empty() && !requested.is_absolute())
        {
            std::vector<std::filesystem::path> roots;
            if (!request.explicitModsDir.empty())
                roots.push_back(request.explicitModsDir);
            else
            {
                roots.push_back(request.currentDir.empty() ? "." : request.currentDir);
                if (!request.gameDir.empty())
                    roots.push_back(request.gameDir);
                if (!request.managedModsDir.empty())
                    roots.push_back(request.managedModsDir);
            }
            for (const auto& root : roots)
            {
                resolved = ResolveFromRootByMetadata(root, segment);
                if (!resolved.empty())
                    break;
            }
        }

        if (resolved.empty())
        {
            result.errors.push_back("Mod '" + segment + "' was not found or is not a valid mod. Checked: " +
                                    JoinCandidatesForError(candidates));
            continue;
        }

        if (!result.mountPath.empty())
            result.mountPath += ';';
        result.mountPath += resolved;
    }

    return result;
}

void ModLoader::AddRoot(std::string path, ModSource source)
{
    _roots.push_back({std::move(path), source});
}

ModCollection ModLoader::Load() const
{
    ModCollection col;
    for (const Root& r : _roots)
    {
        for (Mod& m : ScanModsRoot(r.path, r.source))
        {
            if (!col.Contains(m.id))
                col.Add(std::move(m));
        }
    }
    return col;
}
} // namespace Poseidon
