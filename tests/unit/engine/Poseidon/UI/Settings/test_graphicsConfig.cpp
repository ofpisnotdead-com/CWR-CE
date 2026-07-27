// GraphicsConfig — system-global graphics settings persistence.
// Mirrors test_audioConfig + test_displayConfig case structure plus
// new cases for the tier-table write-through behaviour and the
// RAM-bucket autodetect helper.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/GraphicsConfig.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <initializer_list>

using Poseidon::GraphicsConfig;

namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("graphicscfg_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}

struct FakeEnvironment : GraphicsConfig::Environment
{
    int ramMB = 16 * 1024; // default to "Ultra-class" so Normalize doesn't trip on Preset
    int GetSystemRamMB() const override { return ramMB; }
};
} // namespace

TEST_CASE("GraphicsConfig: factory defaults match spec", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    c.LoadDefaults();

    CHECK(c.qualityPreset == GraphicsConfig::PresetUltra);
    CHECK(c.terrainDetail == GraphicsConfig::TierUltra);
    CHECK(c.objectLod == GraphicsConfig::TierUltra);
    CHECK(c.shadowQuality == GraphicsConfig::TierHigh); // Ultra preset caps shadow at High
    CHECK(c.particlesQuality == GraphicsConfig::TierHigh);
    CHECK(c.vsync == GraphicsConfig::VsyncOn);
    CHECK(c.fpsCap == 0); // Unlimited
    CHECK(c.brightness == 1.6f);
    CHECK(c.gamma == 1.2f);
    CHECK(c.version == GraphicsConfig::kVersion);
}

// Migrate only touches a version-0 file whose gamma is still the stamped 1.0.
TEST_CASE("GraphicsConfig: Migrate lifts an untouched gamma once", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    c.version = 0;
    c.gamma = 1.0f;

    REQUIRE(c.Migrate());
    CHECK(c.gamma == 1.2f);
    CHECK(c.version == GraphicsConfig::kVersion);

    CHECK_FALSE(c.Migrate());
    CHECK(c.gamma == 1.2f);
}

TEST_CASE("GraphicsConfig: Migrate keeps a gamma the user chose", "[Settings][GraphicsConfig]")
{
    for (float chosen : {0.5f, 0.9f, 1.1f, 1.4f, 2.3f})
    {
        GraphicsConfig c;
        c.version = 0;
        c.gamma = chosen;

        CHECK(c.Migrate());
        CHECK(c.gamma == chosen);
        CHECK(c.version == GraphicsConfig::kVersion);
    }
}

TEST_CASE("GraphicsConfig: a versioned file is never migrated", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    c.version = GraphicsConfig::kVersion;
    c.gamma = 1.0f;

    CHECK_FALSE(c.Migrate());
    CHECK(c.gamma == 1.0f);
}

TEST_CASE("GraphicsConfig: a file without a version reloads as version 0", "[Settings][GraphicsConfig]")
{
    const std::string path = TmpPath("graphics.cfg");
    GraphicsConfig saved;
    saved.LoadDefaults();
    REQUIRE(saved.Save(path));

    std::string text;
    {
        std::ifstream in(path);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string::size_type at = text.find("version=");
    REQUIRE(at != std::string::npos);
    text.erase(at, text.find('\n', at) - at + 1);
    {
        std::ofstream out(path, std::ios::trunc);
        out << text;
    }

    GraphicsConfig loaded;
    REQUIRE(loaded.Load(path));
    CHECK(loaded.version == 0);
}

TEST_CASE("GraphicsConfig: Save then Load round-trips every field", "[Settings][GraphicsConfig]")
{
    const std::string path = TmpPath("roundtrip.cfg");
    std::filesystem::remove(path);

    GraphicsConfig src;
    src.qualityPreset = GraphicsConfig::PresetMedium;
    src.terrainDetail = GraphicsConfig::TierMedium;
    src.objectLod = GraphicsConfig::TierMedium;
    src.shadowQuality = GraphicsConfig::TierLow;
    src.particlesQuality = GraphicsConfig::TierLow;
    src.vsync = GraphicsConfig::VsyncAdaptive;
    src.fpsCap = 144;
    src.brightness = 1.2f;
    src.gamma = 0.9f;
    REQUIRE(src.Save(path));

    GraphicsConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.qualityPreset == src.qualityPreset);
    CHECK(dst.terrainDetail == src.terrainDetail);
    CHECK(dst.objectLod == src.objectLod);
    CHECK(dst.shadowQuality == src.shadowQuality);
    CHECK(dst.particlesQuality == src.particlesQuality);
    CHECK(dst.vsync == src.vsync);
    CHECK(dst.fpsCap == src.fpsCap);
    CHECK(dst.brightness == src.brightness);
    CHECK(dst.gamma == src.gamma);
}

TEST_CASE("GraphicsConfig: Load on missing file returns false, leaves instance untouched", "[Settings][GraphicsConfig]")
{
    const std::string path = TmpPath("nope.cfg");
    std::filesystem::remove(path);
    GraphicsConfig c;
    c.fpsCap = 240;
    CHECK_FALSE(c.Load(path));
    CHECK(c.fpsCap == 240);
}

TEST_CASE("GraphicsConfig: PickPresetFromRam buckets correctly", "[Settings][GraphicsConfig]")
{
    using G = GraphicsConfig;
    CHECK(G::PickPresetFromRam(0) == G::PresetHigh);           // unknown → High
    CHECK(G::PickPresetFromRam(2 * 1024) == G::PresetLow);     // 2 GB
    CHECK(G::PickPresetFromRam(4 * 1024 - 1) == G::PresetLow); // just under 4 GB
    CHECK(G::PickPresetFromRam(4 * 1024) == G::PresetMedium);  // exact 4 GB
    CHECK(G::PickPresetFromRam(8 * 1024) == G::PresetHigh);    // exact 8 GB
    CHECK(G::PickPresetFromRam(16 * 1024) == G::PresetUltra);  // exact 16 GB
    CHECK(G::PickPresetFromRam(32 * 1024) == G::PresetUltra);  // 32 GB
}

TEST_CASE("GraphicsConfig: ApplyPresetToTiers stamps tier rows from bundle", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    // Knock the tiers off-default so ApplyPreset has work to do.
    c.terrainDetail = GraphicsConfig::TierLow;
    c.shadowQuality = GraphicsConfig::TierOff;

    c.ApplyPresetToTiers(GraphicsConfig::PresetHigh);
    CHECK(c.terrainDetail == GraphicsConfig::TierHigh);
    CHECK(c.objectLod == GraphicsConfig::TierHigh);
    CHECK(c.shadowQuality == GraphicsConfig::TierMedium);
    CHECK(c.particlesQuality == GraphicsConfig::TierLow);
}

TEST_CASE("GraphicsConfig: ApplyPresetToTiers does NOT touch per-user knobs", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    c.vsync = GraphicsConfig::VsyncAdaptive;
    c.fpsCap = 60;
    c.brightness = 1.4f;
    c.gamma = 0.7f;

    c.ApplyPresetToTiers(GraphicsConfig::PresetLow);

    CHECK(c.vsync == GraphicsConfig::VsyncAdaptive);
    CHECK(c.fpsCap == 60);
    CHECK(c.brightness == 1.4f);
    CHECK(c.gamma == 0.7f);
}

TEST_CASE("GraphicsConfig: DerivePresetFromTiers returns matching preset", "[Settings][GraphicsConfig]")
{
    GraphicsConfig c;
    c.LoadDefaults();
    CHECK(c.DerivePresetFromTiers() == GraphicsConfig::PresetUltra);

    c.ApplyPresetToTiers(GraphicsConfig::PresetLow);
    CHECK(c.DerivePresetFromTiers() == GraphicsConfig::PresetLow);

    c.ApplyPresetToTiers(GraphicsConfig::PresetMedium);
    CHECK(c.DerivePresetFromTiers() == GraphicsConfig::PresetMedium);
}

TEST_CASE("GraphicsConfig: DerivePresetFromTiers returns Custom when tiers diverge", "[Settings][GraphicsConfig]")
{
    // Touching one tier row should make the preset Custom.  This is
    // what drives the UI's "preset display drops to Custom on any
    // tier-row change" behaviour — it derives, doesn't snap.
    GraphicsConfig c;
    c.ApplyPresetToTiers(GraphicsConfig::PresetHigh);
    c.objectLod = GraphicsConfig::TierLow; // High preset has objectLod=High; mismatch
    CHECK(c.DerivePresetFromTiers() == GraphicsConfig::PresetCustom);
}

TEST_CASE("GraphicsConfig: Normalize is a no-op when every field is valid", "[Settings][GraphicsConfig]")
{
    FakeEnvironment env;
    GraphicsConfig c;
    c.LoadDefaults();
    CHECK_FALSE(c.Normalize(env));
}

TEST_CASE("GraphicsConfig: Normalize clamps out-of-range volumes / fps to allowed values", "[Settings][GraphicsConfig]")
{
    FakeEnvironment env;
    GraphicsConfig c;
    c.brightness = 5.0f; // far above 1.8
    c.gamma = 0.1f;      // below 0.5
    c.fpsCap = 75;       // not in {0,30,60,90,120,144,240}
    CHECK(c.Normalize(env));
    CHECK(c.brightness == 1.8f);
    CHECK(c.gamma == 0.5f);
    CHECK(c.fpsCap == 60); // 75 → nearest allowed = 60
}

TEST_CASE("GraphicsConfig: Normalize resets unknown tier values to per-row defaults", "[Settings][GraphicsConfig]")
{
    FakeEnvironment env;
    GraphicsConfig c;
    // Hand-edited file with bogus values.
    c.terrainDetail = static_cast<GraphicsConfig::Tier>(99);
    c.shadowQuality = static_cast<GraphicsConfig::Tier>(99);
    c.objectLod = static_cast<GraphicsConfig::Tier>(99);
    c.particlesQuality = static_cast<GraphicsConfig::Tier>(99);
    CHECK(c.Normalize(env));
    CHECK(c.terrainDetail == GraphicsConfig::TierUltra);
    CHECK(c.objectLod == GraphicsConfig::TierUltra);
    CHECK(c.shadowQuality == GraphicsConfig::TierHigh);
    CHECK(c.particlesQuality == GraphicsConfig::TierHigh);
}

TEST_CASE("GraphicsConfig: Off is valid for Shadow and Particles, NOT for Terrain or Object LOD",
          "[Settings][GraphicsConfig]")
{
    // Field invariant: "Off" is a meaningful state for Shadow /
    // Particles (those features can be entirely disabled) but not for
    // Terrain (the world has to render at some grid) or Object LOD
    // (entities have to render at some bias).  The Low preset's
    // bundle reflects this: shadows + particles Off, terrain Low,
    // objectLod Low.
    FakeEnvironment env;
    GraphicsConfig c;
    c.terrainDetail = GraphicsConfig::TierOff;
    c.objectLod = GraphicsConfig::TierOff;
    c.shadowQuality = GraphicsConfig::TierOff;    // legal
    c.particlesQuality = GraphicsConfig::TierOff; // legal
    CHECK(c.Normalize(env));
    CHECK(c.terrainDetail != GraphicsConfig::TierOff);
    CHECK(c.objectLod != GraphicsConfig::TierOff);
    CHECK(c.shadowQuality == GraphicsConfig::TierOff);
    CHECK(c.particlesQuality == GraphicsConfig::TierOff);
}

TEST_CASE("GraphicsConfig: Normalize on already-normalized values reports no change", "[Settings][GraphicsConfig]")
{
    FakeEnvironment env;
    GraphicsConfig c;
    c.fpsCap = 75;  // first Normalize snaps to 60
    c.gamma = 0.3f; // first Normalize clamps to 0.5
    c.Normalize(env);
    CHECK_FALSE(c.Normalize(env));
}

TEST_CASE("GraphicsConfig: Allowed FPS cap values pass through unchanged", "[Settings][GraphicsConfig]")
{
    // Iterate the allowed set explicitly so the snap-to-nearest logic
    // can't silently corrupt a legal value.
    FakeEnvironment env;
    for (int allowed : {0, 30, 60, 90, 120, 144, 240})
    {
        GraphicsConfig c;
        c.fpsCap = allowed;
        c.Normalize(env);
        CHECK(c.fpsCap == allowed);
    }
}
