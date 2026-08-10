// Startup profile selection policy: prefer an existing usable profile over
// creating or recreating a default one, and remember the last-used profile.
//
// ProfileManager owns the filesystem CRUD on profiles; this layer owns the
// decision of which profile a fresh start should use. The decision is a pure
// function (DecideStartupProfile); ProfileService wires it to injected
// boundaries (where profiles live, how the last-used name is loaded/saved, what
// the OS login name is) so both the policy and the orchestration are testable
// without touching the real user directory or settings file.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Poseidon
{

/// The decision the startup policy reaches — pure data, no I/O.
struct ProfileChoice
{
    std::string name;     ///< profile to use
    bool create = false;  ///< a profile directory must be created for `name`
    bool persist = false; ///< `name` must be saved as the last-used profile
};

/// Pure startup-selection policy. Given the persisted last-used name (empty if
/// none), the names of the profiles that currently exist (sorted), and the OS
/// login name, decide which profile a fresh start should use:
///   - persisted name still exists            -> use it (no create, no persist)
///   - profiles exist but none match persisted -> use the first existing
///                                                (deterministic), persist it
///   - no profiles exist                       -> create + persist the default
///                                                (OS login if a valid profile
///                                                name, otherwise "Player")
ProfileChoice DecideStartupProfile(const std::string& persistedName, const std::vector<std::string>& existingNames,
                                   const std::string& osLogin);

/// Applies DecideStartupProfile against injected boundaries: enumerates and
/// creates profiles under `userDir` (via ProfileManager), reads/writes the
/// last-used name through the persistence callbacks, and asks `osLogin` for the OS
/// login. Game code wires the real implementations; tests wire fakes.
class ProfileService
{
  public:
    struct Boundaries
    {
        std::string userDir;                                       ///< profiles live in userDir/Users/
        std::function<std::string()> loadPersistedName;            ///< read last-used profile
        std::function<void(const std::string&)> savePersistedName; ///< write last-used profile
        std::function<std::string()> osLogin;                      ///< OS login name (may be empty)
    };

    explicit ProfileService(Boundaries boundaries);

    /// Enumerate, decide, create (when needed), persist (when needed). Returns
    /// the selected profile name.
    std::string ResolveStartupProfile();

  private:
    Boundaries m_boundaries;
};

} // namespace Poseidon
