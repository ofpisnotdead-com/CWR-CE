#pragma once

// Runtime dumps of what the engine actually loaded: mounted PBO banks, the
// merged config tree, the addon merge order, the name-keyed type banks, and
// the positional index tables that config array order feeds.
//
// The engine builds all of this at start-up and then never shows it again --
// bank priority, addon merge order and override provenance are only visible in
// scattered log lines, and several failure modes (a shadowed bank, an addon
// dropped by a dependency cycle) are silent.  These dumps make the loaded state
// inspectable without a debugger.
//
// Everything here is read-only.  Each dump writes a text file under
// GamePaths::CacheDir()/dumps and returns its path; only a one-line summary
// goes to the log, since a full config tree is far too large to echo.
//
// Exposed through DebugCommands as dumpbanks / dumpconfig / dumpaddons /
// dumptypes / dumpslots.

#include <string>
#include <string_view>

namespace Poseidon::Dev
{
namespace DataDump
{

// The engine keeps five independent config trees; only Pars carries merged
// addon content.  Mission and campaign are separate roots, consulted ahead of
// Pars by a manual cascade at each call site rather than merged into it.
enum class ConfigRoot
{
    Pars,
    Res,
    Remaster,
    Mission,
    Campaign
};

struct ConfigOptions
{
    ConfigRoot root = ConfigRoot::Pars;
    //! Dotted path to start from ("CfgWeapons.M16"); empty walks the whole root.
    std::string path;
    //! Levels below the start point. 0 emits the per-top-level summary only.
    int maxDepth = 2;
    //! Include scalar and array members, not just the class skeleton.
    bool values = true;
};

// Each returns true and sets outPath to the file written, or false and sets
// error.

bool DumpBanks(std::string& outPath, std::string& error);
bool DumpConfig(const ConfigOptions& opt, std::string& outPath, std::string& error);
bool DumpAddons(std::string& outPath, std::string& error);
bool DumpTypes(std::string& outPath, std::string& error);
//! classFilter empty dumps every CfgVehicles type that has weapons or hit points.
bool DumpSlots(std::string_view classFilter, std::string& outPath, std::string& error);

// Add the dump verbs to the DebugCommands registry. Must be called explicitly:
// this translation unit holds no symbol the engine otherwise references, so a
// self-registering static would be dropped when the static library is linked.
void RegisterCommands();

} // namespace DataDump
} // namespace Poseidon::Dev
