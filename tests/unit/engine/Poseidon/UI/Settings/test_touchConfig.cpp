// TouchConfig — touch scalar settings persistence.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/TouchConfig.hpp>

#include <filesystem>
#include <random>
#include <string>

using Poseidon::TouchConfig;

namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("touchcfg_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}
} // namespace

TEST_CASE("TouchConfig: factory defaults", "[Settings][TouchConfig]")
{
    TouchConfig c;
    CHECK(c.aimSensitivity == 1.0f);
    CHECK(c.cursorSensitivity == 1.0f);
    CHECK(c.displayMode == 0);
}

TEST_CASE("TouchConfig: LoadDefaults resets a mutated instance", "[Settings][TouchConfig]")
{
    TouchConfig c;
    c.aimSensitivity = 2.25f;
    c.cursorSensitivity = 0.4f;
    c.displayMode = 1;
    c.LoadDefaults();
    CHECK(c.aimSensitivity == 1.0f);
    CHECK(c.cursorSensitivity == 1.0f);
    CHECK(c.displayMode == 0);
}

TEST_CASE("TouchConfig: Normalize resets an out-of-range displayMode to Auto", "[Settings][TouchConfig]")
{
    TouchConfig c;
    c.displayMode = 7;
    REQUIRE(c.Normalize());
    CHECK(c.displayMode == 0);
}

TEST_CASE("TouchConfig: Normalize clamps sensitivities to supported range", "[Settings][TouchConfig]")
{
    TouchConfig c;
    c.aimSensitivity = 0.01f;
    c.cursorSensitivity = 99.0f;
    REQUIRE(c.Normalize());
    CHECK(c.aimSensitivity == 0.25f);
    CHECK(c.cursorSensitivity == 3.0f);
}

TEST_CASE("TouchConfig: Normalize is no-op for in-range values", "[Settings][TouchConfig]")
{
    TouchConfig c;
    c.aimSensitivity = 1.7f;
    c.cursorSensitivity = 0.75f;
    CHECK_FALSE(c.Normalize());
    CHECK(c.aimSensitivity == 1.7f);
    CHECK(c.cursorSensitivity == 0.75f);
}

TEST_CASE("TouchConfig: Load on missing file returns false", "[Settings][TouchConfig]")
{
    TouchConfig c;
    CHECK_FALSE(c.Load("does_not_exist_touch.cfg"));
}

TEST_CASE("TouchConfig: Save then Load round-trips every field", "[Settings][TouchConfig]")
{
    const std::string path = TmpPath("roundtrip.cfg");
    std::filesystem::remove(path);

    TouchConfig src;
    src.aimSensitivity = 2.4f;
    src.cursorSensitivity = 0.6f;
    src.displayMode = 2;
    REQUIRE(src.Save(path));

    TouchConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.aimSensitivity == 2.4f);
    CHECK(dst.cursorSensitivity == 0.6f);
    CHECK(dst.displayMode == 2);

    std::filesystem::remove(path);
}
