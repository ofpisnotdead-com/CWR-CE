#include <Poseidon/Core/Profile/ProfileService.hpp>

#include <Poseidon/Core/Profile/ProfileManager.hpp>

#include <utility>

namespace Poseidon
{

ProfileChoice DecideStartupProfile(const std::string& persistedName, const std::vector<std::string>& existingNames,
                                   const std::string& osLogin)
{
    // A persisted name that still resolves to an existing profile wins outright.
    if (!persistedName.empty())
    {
        for (const auto& name : existingNames)
        {
            if (name == persistedName)
            {
                return {persistedName, false, false};
            }
        }
    }

    // Otherwise prefer an existing profile over creating a new one. Enumeration
    // is sorted, so the first entry is a deterministic choice; persist it so the
    // next start lands on the same profile without re-deciding.
    if (!existingNames.empty())
    {
        return {existingNames.front(), false, true};
    }

    // No profiles at all: create the default — the OS login when it is a valid
    // profile name, otherwise "Player".
    std::string defaultName = ProfileManager::IsValidProfileName(osLogin) ? osLogin : std::string("Player");
    return {defaultName, true, true};
}

ProfileService::ProfileService(Boundaries boundaries) : m_boundaries(std::move(boundaries)) {}

std::string ProfileService::ResolveStartupProfile()
{
    std::vector<std::string> names;
    for (const auto& info : ProfileManager::EnumerateProfiles(m_boundaries.userDir))
    {
        names.push_back(info.name);
    }

    const std::string persisted = m_boundaries.loadPersistedName ? m_boundaries.loadPersistedName() : std::string();
    const std::string osLogin = m_boundaries.osLogin ? m_boundaries.osLogin() : std::string();

    ProfileChoice choice = DecideStartupProfile(persisted, names, osLogin);

    if (choice.create)
    {
        ProfileManager::CreateProfile(m_boundaries.userDir, choice.name);
    }
    if (choice.persist && m_boundaries.savePersistedName)
    {
        m_boundaries.savePersistedName(choice.name);
    }

    return choice.name;
}

} // namespace Poseidon
