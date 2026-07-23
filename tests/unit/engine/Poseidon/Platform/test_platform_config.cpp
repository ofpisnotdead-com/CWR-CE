#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <string>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
void ApplyGamePathsToLegacyGlobals();
}

TEST_CASE("appConfig: singleton defaults", "[platform][appConfig]")
{
    auto& config = AppConfig::Instance();
    REQUIRE(config.GetWindowWidth() == 800);
    REQUIRE(config.GetWindowHeight() == 600);
    REQUIRE(config.NoSound() == false);
    REQUIRE(config.IsHostServer() == false);
    REQUIRE(config.IsPublicServer() == false);
    REQUIRE(config.GetNetworkPort() == 1985);
    REQUIRE(config.BenchmarkMode() == false);
    REQUIRE(config.NoTextures() == false);
    REQUIRE(config.NoMap() == false);
}

TEST_CASE("config chain: FlashpointCfg defaults to UserDir + CfgName", "[platform][configChain]")
{
    // GamePaths must be initialized (the compatibility test harness may have already initialized it)
    auto& gp = GamePaths::Instance();
    if (!gp.IsInitialized())
        gp.Initialize("TestConfigChain", "TestCfg");

    Poseidon::ApplyGamePathsToLegacyGlobals();

    std::string result(FlashpointCfg.Data());
    std::string expected = gp.UserDir() + gp.CfgName();

    CHECK(result == expected);
    CHECK(result.find(gp.Codename()) != std::string::npos);
    CHECK(result.find(".cfg") != std::string::npos);
}
