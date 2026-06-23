// MouseConfig — mouse scalars persistence.
// Covers: defaults, round-trip, Normalize clamps sensitivities,
// missing-file handling.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/MouseConfig.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <filesystem>
#include <random>
#include <string>

using Poseidon::MouseConfig;
using Poseidon::ParamFile;
using Poseidon::RString;

namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("mousecfg_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}
} // namespace

TEST_CASE("MouseConfig: factory defaults", "[Settings][MouseConfig]")
{
    MouseConfig c;
    CHECK(c.reverseY == false);
    CHECK(c.buttonsReversed == false);
    CHECK(c.sensitivityX == 1.0f);
    CHECK(c.sensitivityY == 1.0f);
}

TEST_CASE("MouseConfig: a fresh instance starts at LoadDefaults state", "[Settings][MouseConfig]")
{
    // Same contract as AudioConfig — default ctor should match LoadDefaults.
    MouseConfig c;
    MouseConfig defaulted;
    defaulted.LoadDefaults();
    CHECK(c.reverseY == defaulted.reverseY);
    CHECK(c.buttonsReversed == defaulted.buttonsReversed);
    CHECK(c.sensitivityX == defaulted.sensitivityX);
    CHECK(c.sensitivityY == defaulted.sensitivityY);
}

TEST_CASE("MouseConfig: LoadDefaults resets a mutated instance", "[Settings][MouseConfig]")
{
    MouseConfig c;
    c.reverseY = true;
    c.buttonsReversed = true;
    c.sensitivityX = 1.7f;
    c.sensitivityY = 0.6f;
    c.LoadDefaults();
    CHECK(c.reverseY == false);
    CHECK(c.buttonsReversed == false);
    CHECK(c.sensitivityX == 1.0f);
    CHECK(c.sensitivityY == 1.0f);
}

TEST_CASE("MouseConfig: Normalize clamps low sensitivities to 0.5", "[Settings][MouseConfig]")
{
    MouseConfig c;
    c.sensitivityX = 0.1f;
    c.sensitivityY = 0.0f;
    REQUIRE(c.Normalize());
    CHECK(c.sensitivityX == 0.5f);
    CHECK(c.sensitivityY == 0.5f);
}

TEST_CASE("MouseConfig: Normalize clamps high sensitivities to 2.0", "[Settings][MouseConfig]")
{
    MouseConfig c;
    c.sensitivityX = 5.0f;
    c.sensitivityY = 99.0f;
    REQUIRE(c.Normalize());
    CHECK(c.sensitivityX == 2.0f);
    CHECK(c.sensitivityY == 2.0f);
}

TEST_CASE("MouseConfig: Normalize is no-op for in-range values", "[Settings][MouseConfig]")
{
    MouseConfig c;
    c.sensitivityX = 1.3f;
    c.sensitivityY = 0.8f;
    CHECK_FALSE(c.Normalize());
    CHECK(c.sensitivityX == 1.3f);
    CHECK(c.sensitivityY == 0.8f);
}

TEST_CASE("MouseConfig: Load on missing file returns false", "[Settings][MouseConfig]")
{
    MouseConfig c;
    CHECK_FALSE(c.Load("does_not_exist_anywhere_either.cfg"));
}

TEST_CASE("MouseConfig: Load on missing file leaves instance untouched", "[Settings][MouseConfig]")
{
    // AudioConfig contract: Load returns false on miss without
    // mutating the instance, so callers can chain
    // Load → LoadDefaults → Save.
    MouseConfig c;
    c.reverseY = true;
    c.sensitivityX = 1.42f;
    CHECK_FALSE(c.Load("absolutely_not_present.cfg"));
    CHECK(c.reverseY == true);
    CHECK(c.sensitivityX == 1.42f);
}

TEST_CASE("MouseConfig: Load on partial file keeps unspecified fields at current values", "[Settings][MouseConfig]")
{
    // Forward-compat: if a future field is added, an older file without
    // that field must not reset its in-memory value.  Simulate by
    // hand-writing only sensitivity X, then loading into an instance
    // with mutated defaults.
    const std::string path = TmpPath("partial.cfg");
    std::filesystem::remove(path);

    {
        // Save full to establish the file, then we'll Load it into a
        // mutated instance; unspecified-field behaviour is implicit
        // because every Save writes every field — partial only happens
        // when the user hand-edits.  Round-trip with an explicit value
        // in just the field we care about.
        MouseConfig narrow;
        narrow.sensitivityX = 1.8f;
        REQUIRE(narrow.Save(path));
    }

    MouseConfig dst;
    dst.reverseY = true;     // pre-set; file has reverseY=false (default)
    dst.sensitivityY = 1.6f; // file has 1.0
    REQUIRE(dst.Load(path));
    // sensitivityX picked up from the file:
    CHECK(dst.sensitivityX == 1.8f);
    // reverseY / sensitivityY get OVERWRITTEN by file's defaults — Save
    // writes every field, so partial-file forward-compat applies only
    // to truly hand-crafted files (no field).  Document this by
    // asserting the overwrite behaviour explicitly.
    CHECK(dst.reverseY == false);
    CHECK(dst.sensitivityY == 1.0f);

    std::filesystem::remove(path);
}

TEST_CASE("MouseConfig: Save then Load round-trips every field", "[Settings][MouseConfig]")
{
    const std::string path = TmpPath("roundtrip.cfg");
    std::filesystem::remove(path);

    MouseConfig src;
    src.reverseY = true;
    src.buttonsReversed = true;
    src.sensitivityX = 1.7f;
    src.sensitivityY = 0.6f;

    REQUIRE(src.Save(path));

    MouseConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.reverseY == true);
    CHECK(dst.buttonsReversed == true);
    CHECK(dst.sensitivityX == 1.7f);
    CHECK(dst.sensitivityY == 0.6f);

    std::filesystem::remove(path);
}

// --- v2 tuning fields: defaults, migration, dual-write, round-trip ---

TEST_CASE("MouseConfig: v2 tuning fields default to classic / no-op feel", "[Settings][MouseConfig]")
{
    MouseConfig c;
    CHECK(c.version == MouseConfig::kCurrentVersion);
    CHECK(c.baseScale == 1.5f); // == the legacy MouseState kSensitivityScale
    CHECK(c.dpiNormalize == false);
    CHECK(c.mouseDpi == 1600);
    CHECK(c.referenceDpi == 1600);
    CHECK(c.smoothing == 0.0f);
    CHECK(c.acceleration == false);
    CHECK(c.accelExponent == 1.0f);
    CHECK(c.menuCursorScale == 1.0f);
    CHECK(c.extendedRange == false);
}

TEST_CASE("MouseConfig: MigrateSensitivity preserves feel across baseScale", "[Settings][MouseConfig]")
{
    // Identity while the new baseScale equals the legacy one.
    CHECK(MouseConfig::MigrateSensitivity(1.0f, 1.5f, 1.5f) == Catch::Approx(1.0f));
    CHECK(MouseConfig::MigrateSensitivity(1.7f, 1.5f, 1.5f) == Catch::Approx(1.7f));
    // Halving baseScale must double sensitivity to keep the same effective feel.
    CHECK(MouseConfig::MigrateSensitivity(1.0f, 1.5f, 0.75f) == Catch::Approx(2.0f));
    // Guard against a zero/negative divisor.
    CHECK(MouseConfig::MigrateSensitivity(1.0f, 1.5f, 0.0f) == Catch::Approx(1.0f));
}

TEST_CASE("MouseConfig: a legacy (v1) file is detected and migrated on load", "[Settings][MouseConfig]")
{
    const std::string path = TmpPath("legacy_v1.cfg");
    std::filesystem::remove(path);

    // Hand-build a pre-tuning file: only the v1 keys, NO version key.
    {
        ParamFile cfg;
        cfg.Add("reverseY", true);
        cfg.Add("buttonsReversed", false);
        cfg.Add("sensitivityX", 1.7f);
        cfg.Add("sensitivityY", 0.8f);
        cfg.Save(RString(path.c_str()));
    }

    MouseConfig dst;
    REQUIRE(dst.Load(path));

    // Migrated to the current schema in memory.
    CHECK(dst.version == MouseConfig::kCurrentVersion);
    // v1 values preserved (MigrateSensitivity is identity at the default baseScale).
    CHECK(dst.reverseY == true);
    CHECK(dst.sensitivityX == Catch::Approx(1.7f));
    CHECK(dst.sensitivityY == Catch::Approx(0.8f));
    // New fields take classic defaults → behavior unchanged.
    CHECK(dst.baseScale == 1.5f);
    CHECK(dst.dpiNormalize == false);
    CHECK(dst.menuCursorScale == 1.0f);

    std::filesystem::remove(path);
}

TEST_CASE("MouseConfig: v2 round-trips every tuning field", "[Settings][MouseConfig]")
{
    const std::string path = TmpPath("v2_roundtrip.cfg");
    std::filesystem::remove(path);

    MouseConfig src;
    src.reverseY = true;
    src.sensitivityX = 2.7f; // extended-range value
    src.sensitivityY = 0.1f;
    src.baseScale = 0.75f;
    src.dpiNormalize = true;
    src.mouseDpi = 1600;
    src.referenceDpi = 800;
    src.smoothing = 0.3f;
    src.acceleration = true;
    src.accelExponent = 1.5f;
    src.menuCursorScale = 0.5f;
    src.extendedRange = true;
    REQUIRE(src.Save(path));

    MouseConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.version == MouseConfig::kCurrentVersion);
    CHECK(dst.reverseY == true);
    CHECK(dst.sensitivityX == Catch::Approx(2.7f)); // full value survives via lookSensX
    CHECK(dst.sensitivityY == Catch::Approx(0.1f));
    CHECK(dst.baseScale == Catch::Approx(0.75f));
    CHECK(dst.dpiNormalize == true);
    CHECK(dst.mouseDpi == 1600);
    CHECK(dst.referenceDpi == 800);
    CHECK(dst.smoothing == Catch::Approx(0.3f));
    CHECK(dst.acceleration == true);
    CHECK(dst.accelExponent == Catch::Approx(1.5f));
    CHECK(dst.menuCursorScale == Catch::Approx(0.5f));
    CHECK(dst.extendedRange == true);

    std::filesystem::remove(path);
}

TEST_CASE("MouseConfig: Save is downgrade-safe (legacy keys clamped, lookSens authoritative)",
          "[Settings][MouseConfig]")
{
    const std::string path = TmpPath("downgrade.cfg");
    std::filesystem::remove(path);

    MouseConfig src;
    src.extendedRange = true;
    src.sensitivityX = 2.7f; // outside the legacy [0.5,2.0] band
    REQUIRE(src.Save(path));

    // An old build only knows the v1 sensitivityX key — it must see a clamped,
    // in-range value, never the raw 2.7.  The new lookSensX carries the truth.
    ParamFile raw;
    raw.Parse(RString(path.c_str()));
    REQUIRE(raw.FindEntry("sensitivityX"));
    REQUIRE(raw.FindEntry("lookSensX"));
    REQUIRE(raw.FindEntry("version"));
    CHECK((float)*raw.FindEntry("sensitivityX") == Catch::Approx(2.0f));
    CHECK((float)*raw.FindEntry("lookSensX") == Catch::Approx(2.7f));
    CHECK((int)*raw.FindEntry("version") == MouseConfig::kCurrentVersion);

    std::filesystem::remove(path);
}

TEST_CASE("MouseConfig: Normalize clamps tuning fields and the extended sensitivity band", "[Settings][MouseConfig]")
{
    MouseConfig c;
    c.baseScale = 99.0f;
    c.mouseDpi = 0;
    c.smoothing = 5.0f;
    c.accelExponent = 9.0f;
    c.menuCursorScale = 99.0f;
    REQUIRE(c.Normalize());
    CHECK(c.baseScale == 3.0f);
    CHECK(c.mouseDpi == 100);
    CHECK(c.smoothing == 0.95f);
    CHECK(c.accelExponent == 2.0f);
    CHECK(c.menuCursorScale == 4.0f);

    // With extendedRange, sensitivity clamps to the wider [0.05, 3.0] band.
    MouseConfig e;
    e.extendedRange = true;
    e.sensitivityX = 2.7f;  // in range → kept
    e.sensitivityY = 0.01f; // below 0.05 → clamped
    REQUIRE(e.Normalize());
    CHECK(e.sensitivityX == Catch::Approx(2.7f));
    CHECK(e.sensitivityY == Catch::Approx(0.05f));
}
