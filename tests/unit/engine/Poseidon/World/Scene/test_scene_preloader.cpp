// ScenePreloader — fills Scene._preloaded[] from CfgScenePreload.  Apps
// that render scene chrome (cloudlets, craters, footsteps, etc.) call
// Initialize; tool apps that don't render scene content leave it alone.

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/World/Scene/ScenePreloader.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace Poseidon;

TEST_CASE("ScenePreloader reports the slot class set", "[scene][preloader]")
{
    auto names = ScenePreloader::SlotClassNames();
    // 15 simple slots + 4 cloud slots + 1 CollisionStar = 20 entries.
    REQUIRE(names.size() == 20);

    const std::vector<std::string> expected = {
        "CraterShell",  "SlopBlood",  "CloudletBasic",   "CloudletFire", "CloudletWater",
        "CinemaBorder", "CobraLight", "SphereLight",     "HalfLight",    "Marker",
        "FootStepL",    "FootStepR",  "ForceArrowModel", "SphereModel",  "RectangleModel",
        "Cloud1",       "Cloud2",     "Cloud3",          "Cloud4",       "CollisionStar",
    };
    for (const auto& cls : expected)
        REQUIRE(std::find(names.begin(), names.end(), cls) != names.end());
}

TEST_CASE("ScenePreloader::IsAvailable is false before Initialize", "[scene][preloader]")
{
    // The Initialize / Shutdown lifecycle is exercised end-to-end by the
    // integration suite (PoseidonGame --check against packages/Combined
    // and PoseidonTetris against a stripped fixture).  Initialize needs
    // a live Scene, an open ParamFile, and the shape factory; standing
    // those up in a Catch2 process isn't worth the harness work — the
    // pre-init flag check is what unit-testable here.
    ScenePreloader::Instance().Shutdown();
    CHECK_FALSE(ScenePreloader::Instance().IsAvailable());
}
