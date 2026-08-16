#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ModId.hpp"

namespace Poseidon
{
/// Where a mod was found on disk. The source is preserved by location, so a
/// locally-authored mod and a downloaded one never lose their provenance.
enum class ModSource
{
    Local,   ///< user's local mods root (e.g. <UserContent>/Mods, --mods-dir)
    Workshop ///< downloaded mods root (e.g. <UserContent>/Workshop, --workshop-dir)
};

/// One mod on disk: a folder the engine mounts on the mod path to CHANGE the game
/// (addons, dta, campaigns, a bin/ config override). Mission packs are a separate
/// content type, not mods. The id is the folder name verbatim — any name, no '@'
/// prefix required — and is what the mount path uses.
struct Mod
{
    std::string id;        ///< folder name verbatim (the mount uses it), e.g. "CSLA" or "@fixturemod"
    std::string catalogId; ///< mod.json "modId" from PapaBear/workshop installs, else ""
    std::string name;      ///< display name: mod.json "name", else id with a leading '@' trimmed
    std::string version;   ///< mod.json "version", else ""
    int64_t packageRevision = 1;
    std::string sha256;
    std::string path;      ///< absolute folder path, mounted onto the engine mod path
    int64_t sizeBytes = 0; ///< total bytes on disk (recursive), 0 if unknown
    ModSource source = ModSource::Local;
};

/// Whether a folder carries any structure the engine loads from a mod: an
/// addons/dta/Campaigns tree, a bin/ config override, or a mod.json manifest
/// (markers matched case-insensitively). The folder NAME is irrelevant — this is
/// what lets a mod be named anything, with or without the classic '@' prefix.
/// Missions/MPMissions are intentionally NOT markers: a mod changes the game and
/// mounts on the mod path; mission packs are separate content, not mods.
bool LooksLikeMod(const std::string& dir);

/// Scan a single root directory into a flat list of mods tagged `source`, sorted
/// by display name. Pure: path in, vector out; empty when the path isn't a
/// directory.
std::vector<Mod> ScanModsRoot(const std::string& root, ModSource source);

/// An ordered set of mods with the lookups the UI and loader need. Ids compare
/// case-insensitively (the filesystem is, on Windows).
class ModCollection
{
  public:
    void Add(Mod mod);
    bool Contains(const std::string& id) const;
    const Mod* Find(const std::string& id) const; ///< nullptr if absent
    const std::vector<Mod>& All() const { return _mods; }
    std::size_t Size() const { return _mods.size(); }
    bool Empty() const { return _mods.empty(); }

    /// ';'-joined absolute folder paths for `enabledIds`, in the given order,
    /// skipping ids not in the collection. This is the string the engine mod path
    /// consumes; "" when nothing resolves.
    std::string MountPath(const std::vector<std::string>& enabledIds) const;

    /// Like MountPath, but matches each mod by its normalized `ModId` (so a server's
    /// required ids — bare, no '@', any case — resolve to the on-disk `@folder`).
    /// Order follows `ids`; ids not present on disk are skipped.
    std::string MountPathForIds(const std::vector<ModId>& ids) const;

    /// ';'-joined absolute folder paths of every mod, in order — the string the engine
    /// mod path / addon-bank loader mounts. "" when empty. (Same content MountPath
    /// produces for all ids; the no-argument form for an already-resolved active set.)
    std::string FormatMountPath() const;

    /// ';'-joined mod identities of every mod, in order. PapaBear-installed mods use
    /// mod.json `modId`; unmanaged local mods fall back to the verbatim folder name.
    /// This is the identity a server advertises to clients and the master service.
    /// It is not the local disk path and not the VFS prefix of the mod's content.
    std::string FormatNames() const;

  private:
    std::vector<Mod> _mods;
};

/// Build an active-set collection from a ';'-joined mount-path string (what AppConfig
/// resolves `--mod` into, and what `ModSystem` holds): each non-empty entry becomes a
/// Mod whose `id` is the folder basename (the verbatim `@folder`) and whose `path` is
/// the entry. Pure; empty entries are skipped.
ModCollection ActiveModsFromMountPath(const std::string& mountPath);

struct ModPathResolveRequest
{
    std::string modPaths;       ///< CLI --mod value, semicolon-separated
    std::string explicitModsDir;///< CLI --mods-dir, if supplied
    std::string currentDir;     ///< Process CWD before -C changes it
    std::string gameDir;        ///< CLI -C/--work-dir target, if supplied
    std::string managedModsDir; ///< Default user Mods directory
};

struct ModPathResolveResult
{
    std::string mountPath;          ///< Semicolon-separated absolute mod folders
    std::vector<std::string> errors;///< One entry per unresolved --mod segment

    bool Ok() const { return errors.empty(); }
};

/// Resolve a CLI --mod string into mountable mod folders.
///
/// Relative entries use --mods-dir when supplied. Without --mods-dir they prefer
/// currentDir, then gameDir (-C), then managedModsDir. A candidate only resolves when it is an
/// existing folder that LooksLikeMod() accepts; typos and empty stray folders are
/// reported as errors instead of silently mounting dead paths.
ModPathResolveResult ResolveModPathList(const ModPathResolveRequest& request);

/// Loads mods from one or more roots on disk into a ModCollection. Registering
/// roots and loading are separate steps so the scan stays a pure, testable unit.
class ModLoader
{
  public:
    void AddRoot(std::string path, ModSource source);

    /// Scan every registered root in order, appending each mod folder found.
    /// De-duplicated by id (case-insensitive): the first root that holds an id
    /// wins, so a local mod shadows a downloaded one of the same name.
    ModCollection Load() const;

  private:
    struct Root
    {
        std::string path;
        ModSource source;
    };
    std::vector<Root> _roots;
};
} // namespace Poseidon
