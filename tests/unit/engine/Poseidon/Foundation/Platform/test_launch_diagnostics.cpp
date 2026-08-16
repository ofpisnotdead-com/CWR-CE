#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Poseidon::Foundation::AppConfig;

namespace
{
std::string Describe(const std::string& cliError, std::vector<const char*> args)
{
    return AppConfig::DescribeLaunchError(cliError, static_cast<int>(args.size()), args.data());
}
} // namespace

TEST_CASE("DescribeLaunchError echoes the raw launch options", "[appconfig][cli]")
{
    const std::string out = Describe("--anim: File does not exist: bc", {"game", "-abc", "-C", "packages/Game"});
    REQUIRE_THAT(out, ContainsSubstring("You launched with these options:\n-abc -C packages/Game"));
}

TEST_CASE("DescribeLaunchError points players at their launcher options", "[appconfig][cli]")
{
    const std::string out = Describe("--width: value out of range", {"game", "-wabc"});
    REQUIRE_THAT(out, ContainsSubstring("Steam or another launcher"));
}

TEST_CASE("DescribeLaunchError explains a bundled short option that expanded", "[appconfig][cli]")
{
    const std::string out = Describe("--anim: File does not exist: bc", {"game", "-abc"});
    REQUIRE_THAT(out, ContainsSubstring("-abc means -a bc"));
    REQUIRE_THAT(out, ContainsSubstring("--anim came from"));
}

TEST_CASE("DescribeLaunchError omits the expansion note for a verbatim long option", "[appconfig][cli]")
{
    const std::string out = Describe("--anim: File does not exist: /bad", {"game", "--anim", "/bad"});
    REQUIRE_THAT(out, ContainsSubstring("You launched with these options"));
    REQUIRE_THAT(out, !ContainsSubstring("came from"));
}

TEST_CASE("DescribeLaunchError omits the expansion note for a spaced short value", "[appconfig][cli]")
{
    const std::string out = Describe("--width: Value abc not in range", {"game", "-w", "abc"});
    REQUIRE_THAT(out, ContainsSubstring("You launched with these options"));
    REQUIRE_THAT(out, !ContainsSubstring("came from"));
}

TEST_CASE("DescribeLaunchError returns the bare error when no options were given", "[appconfig][cli]")
{
    REQUIRE(Describe("some parse error", {"game"}) == "some parse error");
}
