#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// Presentation is THE single aspect decision point.  Two properties,
// audited at the source level so they can only shrink:
//
//   1. Engine::SetAspectSettings has exactly one production caller —
//      Presentation::Apply.  A second caller reintroduces the diverging
//      resolve+map paths this module replaced (the Options page's
//      private mapper dropped the world crop flat_quad and ignored the live
//      dev override).
//
//   2. The AspectRatio resolver functions (ResolvePolicy, ResolveLive,
//      SafeDefaultPolicy, BuildSettingsForRatio) are implementation
//      detail of the AspectRatio/Presentation module — no other
//      production code calls them.

namespace
{

std::string ReadStripped(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return std::regex_replace(ss.str(), std::regex("//[^\n]*"), "");
}

std::filesystem::path RepoRoot()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path();
}

template <typename Fn>
void ForEachProductionCpp(Fn&& fn)
{
    for (const char* top : {"engine", "apps"})
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(RepoRoot() / top))
        {
            if (entry.path().extension() != ".cpp" && entry.path().extension() != ".hpp")
                continue;
            fn(entry.path());
        }
    }
}

bool IsAllowed(const std::string& generic, const std::vector<std::string>& allowed)
{
    for (const auto& a : allowed)
    {
        if (generic.size() >= a.size() && generic.compare(generic.size() - a.size(), a.size(), a) == 0)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("Presentation: SetAspectSettings has exactly one production caller", "[Settings][Presentation][boundary]")
{
    const std::vector<std::string> allowed = {
        "engine/Poseidon/Graphics/Core/Engine.hpp",     // the definition
        "engine/Poseidon/UI/Settings/Presentation.cpp", // the one apply site
    };
    int applySites = 0;
    ForEachProductionCpp(
        [&](const std::filesystem::path& p)
        {
            const std::string body = ReadStripped(p);
            if (body.find("SetAspectSettings(") == std::string::npos)
                return;
            const std::string generic = std::filesystem::relative(p, RepoRoot()).generic_string();
            INFO(generic << " calls SetAspectSettings");
            REQUIRE(IsAllowed(generic, allowed));
            ++applySites;
        });
    REQUIRE(applySites == 2); // definition + the one caller
}

TEST_CASE("Presentation: aspect resolvers have no callers outside the module", "[Settings][Presentation][boundary]")
{
    const std::vector<std::string> allowed = {
        "engine/Poseidon/UI/Settings/AspectRatio.hpp",
        "engine/Poseidon/UI/Settings/AspectRatio.cpp",
        "engine/Poseidon/UI/Settings/Presentation.cpp",
    };
    const std::regex callRe("\\b(ResolvePolicy|ResolveLive|SafeDefaultPolicy|BuildSettingsForRatio)\\s*\\(");
    ForEachProductionCpp(
        [&](const std::filesystem::path& p)
        {
            const std::string body = ReadStripped(p);
            if (!std::regex_search(body, callRe))
                return;
            const std::string generic = std::filesystem::relative(p, RepoRoot()).generic_string();
            INFO(generic << " calls an AspectRatio resolver directly");
            REQUIRE(IsAllowed(generic, allowed));
        });
}

TEST_CASE("Presentation: profile FOV migration has one production caller", "[Settings][Presentation][boundary]")
{
    const std::vector<std::string> allowed = {
        "engine/Poseidon/Core/Profile/UserConfig.hpp",
        "engine/Poseidon/Core/Profile/UserConfig.cpp",
        "engine/Poseidon/UI/Settings/Presentation.cpp",
    };
    int sites = 0;
    ForEachProductionCpp(
        [&](const std::filesystem::path& p)
        {
            const std::string body = ReadStripped(p);
            if (body.find("MigrateFov(") == std::string::npos)
                return;
            const std::string generic = std::filesystem::relative(p, RepoRoot()).generic_string();
            INFO(generic << " calls UserConfig::MigrateFov");
            REQUIRE(IsAllowed(generic, allowed));
            ++sites;
        });
    REQUIRE(sites == 3);
}
