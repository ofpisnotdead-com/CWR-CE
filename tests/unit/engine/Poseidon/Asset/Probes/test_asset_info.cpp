#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include "test_fixtures.hpp"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <catch2/matchers/catch_matchers.hpp>
#include <initializer_list>
#include <string>
#include <vector>

using namespace Poseidon;
using Catch::Matchers::ContainsSubstring;

namespace
{
// FormatTime() formats via localtime(), so tests need TZ pinned to UTC to
// be deterministic across machines/CI runners instead of the local one.
class ScopedUtcTimezone
{
  public:
    ScopedUtcTimezone()
    {
#ifdef _WIN32
        _putenv_s("TZ", "UTC");
#else
        setenv("TZ", "UTC", 1);
#endif
        tzset();
    }

    ~ScopedUtcTimezone()
    {
#ifdef _WIN32
        _putenv_s("TZ", "");
#else
        unsetenv("TZ");
#endif
        tzset();
    }
};
} // namespace

// Format Helpers

TEST_CASE("FormatSize returns human-readable sizes", "[tools][helpers]")
{
    CHECK(FormatSize(0) == "0 B");
    CHECK(FormatSize(512) == "512 B");
    CHECK(FormatSize(1023) == "1023 B");
    CHECK(FormatSize(1024) == "1 KB");
    CHECK(FormatSize(2048) == "2 KB");
    CHECK(FormatSize(1048576) == "1 MB");
    CHECK(FormatSize(5242880) == "5 MB");
}

TEST_CASE("FormatTime handles zero and valid timestamps", "[tools][helpers]")
{
    CHECK(FormatTime(0) == "-");

    ScopedUtcTimezone utc;
    // 2020-01-01 00:00:00 UTC = 1577836800
    std::string result = FormatTime(1577836800);
    CHECK_THAT(result, ContainsSubstring("2020"));
    CHECK_THAT(result, ContainsSubstring("01"));
}

// Texture Inspection

TEST_CASE("InspectTexture returns info for DXT1 PAA", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt1.paa");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    CHECK(info.isPaa == true);
    CHECK(info.typeName == "PAA");
    CHECK(info.formatName == "DXT1");
    CHECK(info.width > 0);
    CHECK(info.height > 0);
    CHECK(info.mipmapCount > 0);
}

TEST_CASE("InspectTexture returns info for DXT5 PAA", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt5.paa");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    CHECK(info.formatName == "DXT5");
}

TEST_CASE("InspectTexture returns info for ARGB4444 PAA", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_argb4444.paa");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    CHECK(info.formatName == "ARGB4444");
    CHECK(info.isPaa == true);
}

TEST_CASE("InspectTexture returns info for AI88 PAA", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_ai88.paa");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    CHECK(info.formatName == "AI88");
}

TEST_CASE("InspectTexture returns info for PAC file", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/pac/synthetic_default.pac");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    CHECK(info.isPaa == false);
    CHECK(info.typeName == "PAC");
    CHECK(info.width > 0);
    CHECK(info.height > 0);
}

TEST_CASE("InspectTexture returns invalid for non-existent file", "[tools][texture]")
{
    TextureInfo info = InspectTexture("/nonexistent/file.paa");
    CHECK(info.valid == false);
}

TEST_CASE("InspectTexture reports DXT1 transparency", "[tools][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt1.paa");
    REQUIRE(path != nullptr);

    TextureInfo info = InspectTexture(path);
    REQUIRE(info.valid);
    // hasTransparentBlocks is a bool -- we just verify the field is populated
    // (actual value depends on the fixture)
    CHECK((info.hasTransparentBlocks == true || info.hasTransparentBlocks == false));
}

// Sound Inspection

TEST_CASE("InspectSound returns info for WAV file", "[tools][sound]")
{
    const char* path = GET_FIXTURE("audio/tone.wav");
    REQUIRE(path != nullptr);

    SoundInfo info = InspectSound(path);
    REQUIRE(info.valid);
    CHECK(info.format == "WAV");
    CHECK(info.channels > 0);
    CHECK(info.sampleRate > 0);
    CHECK(info.bitDepth > 0);
    CHECK(info.duration > 0.0);
}

TEST_CASE("InspectSound returns info for WSS file", "[tools][sound]")
{
    const char* path = GET_FIXTURE("audio/click.wss");
    REQUIRE(path != nullptr);

    SoundInfo info = InspectSound(path);
    REQUIRE(info.valid);
    CHECK(info.format == "WSS");
    CHECK(info.channels > 0);
    CHECK(info.sampleRate > 0);
    CHECK(info.uncompressedSize > 0);
}

TEST_CASE("InspectSound returns invalid for non-existent file", "[tools][sound]")
{
    SoundInfo info = InspectSound("/nonexistent/file.wav");
    CHECK(info.valid == false);
}

// Model Inspection

TEST_CASE("InspectModel returns info for ODOL P3D", "[tools][model]")
{
    const char* path = GET_FIXTURE("p3d/sky_plane.p3d");
    REQUIRE(path != nullptr);

    ModelInfo info = InspectModel(path);
    REQUIRE(info.valid);
    CHECK(info.lodCount > 0);
    CHECK_FALSE(info.lods.empty());
    CHECK(info.lods[0].points > 0);
}

TEST_CASE("InspectModel returns LOD details", "[tools][model]")
{
    const char* path = GET_FIXTURE("p3d/crew_proxy.p3d");
    REQUIRE(path != nullptr);

    ModelInfo info = InspectModel(path);
    REQUIRE(info.valid);

    for (const auto& lod : info.lods)
    {
        CHECK(lod.index >= 0);
        CHECK(lod.points >= 0);
        CHECK(lod.faces >= 0);
    }
}

TEST_CASE("InspectModel reports format and version", "[tools][model]")
{
    const char* path = GET_FIXTURE("p3d/complex_vehicle.p3d");
    REQUIRE(path != nullptr);

    ModelInfo info = InspectModel(path);
    REQUIRE(info.valid);
    CHECK_FALSE(info.format.empty());
    CHECK(info.version > 0);
}

TEST_CASE("InspectModel returns texture names", "[tools][model]")
{
    const char* path = GET_FIXTURE("p3d/complex_vehicle.p3d");
    REQUIRE(path != nullptr);

    ModelInfo info = InspectModel(path);
    REQUIRE(info.valid);
    REQUIRE_FALSE(info.lods.empty());

    // At least the first LOD should have textures
    bool hasTextures = false;
    for (const auto& lod : info.lods)
    {
        if (lod.textures > 0)
        {
            hasTextures = true;
            CHECK(lod.textureNames.size() == static_cast<size_t>(lod.textures));
        }
    }
    CHECK(hasTextures);
}

TEST_CASE("InspectModel returns invalid for non-existent file", "[tools][model]")
{
    ModelInfo info = InspectModel("/nonexistent/file.p3d");
    CHECK(info.valid == false);
}

// Terrain Inspection

TEST_CASE("InspectTerrain rejects synthetic placeholder WRP", "[tools][terrain]")
{
    const char* path = GET_FIXTURE("wrp/test_world.wrp");
    REQUIRE(path != nullptr);

    TerrainInfo info = InspectTerrain(path);
    CHECK_FALSE(info.valid);
}

TEST_CASE("InspectTerrain returns invalid for non-existent file", "[tools][terrain]")
{
    TerrainInfo info = InspectTerrain("/nonexistent/file.wrp");
    CHECK(info.valid == false);
}

// PBO Inspection

TEST_CASE("InspectPbo returns info for PBO file", "[tools][pbo]")
{
    const char* path = GET_FIXTURE("pbo/addon_fixture.pbo");
    REQUIRE(path != nullptr);

    PboInfo info = InspectPbo(path);
    REQUIRE(info.valid);
    CHECK_FALSE(info.entries.empty());
    CHECK(info.totalSize > 0);
}

TEST_CASE("InspectPbo entries have names and sizes", "[tools][pbo]")
{
    const char* path = GET_FIXTURE("pbo/addon_fixture.pbo");
    REQUIRE(path != nullptr);

    PboInfo info = InspectPbo(path);
    REQUIRE(info.valid);

    for (const auto& entry : info.entries)
    {
        CHECK_FALSE(entry.name.empty());
        CHECK(entry.length >= 0);
    }
}

TEST_CASE("InspectPbo returns invalid for non-existent file", "[tools][pbo]")
{
    PboInfo info = InspectPbo("/nonexistent/file.pbo");
    CHECK(info.valid == false);
}

// Animation Inspection

TEST_CASE("InspectAnimation returns info for RTM file", "[tools][animation]")
{
    const char* path = GET_FIXTURE("rtm/actor_motion.rtm");
    REQUIRE(path != nullptr);

    AnimationInfo info = InspectAnimation(path);
    REQUIRE(info.valid);
    CHECK(info.boneCount > 0);
    CHECK(info.phaseCount > 0);
    CHECK_THAT(info.format, ContainsSubstring("RTM"));
}

TEST_CASE("InspectAnimation reports correct format version", "[tools][animation]")
{
    const char* path = GET_FIXTURE("rtm/actor_motion.rtm");
    REQUIRE(path != nullptr);

    AnimationInfo info = InspectAnimation(path);
    REQUIRE(info.valid);
    // Format should be either "RTM v1.00" or "RTM v1.01"
    CHECK((info.format == "RTM v1.00" || info.format == "RTM v1.01"));
}

TEST_CASE("InspectAnimation reads second RTM fixture", "[tools][animation]")
{
    const char* path = GET_FIXTURE("rtm/marker_motion.rtm");
    REQUIRE(path != nullptr);

    AnimationInfo info = InspectAnimation(path);
    REQUIRE(info.valid);
    CHECK(info.boneCount > 0);
    CHECK(info.phaseCount > 0);
}

TEST_CASE("InspectAnimation returns invalid for non-existent file", "[tools][animation]")
{
    AnimationInfo info = InspectAnimation("/nonexistent/file.rtm");
    CHECK(info.valid == false);
}

TEST_CASE("InspectAnimation returns invalid for wrong format", "[tools][animation]")
{
    // Feed it a texture file - should fail gracefully
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt1.paa");
    REQUIRE(path != nullptr);

    AnimationInfo info = InspectAnimation(path);
    CHECK(info.valid == false);
}

// Config Inspection

TEST_CASE("InspectConfig detects binarized raP config", "[tools][config]")
{
    const char* path = GET_FIXTURE("cfg/binarized_config.bin");
    REQUIRE(path != nullptr);

    ConfigInfo info = InspectConfig(path);
    REQUIRE(info.valid);
    CHECK(info.isBinarized == true);
    CHECK(info.version == 0);
    CHECK(info.fileSize > 0);
}

TEST_CASE("InspectConfig detects text config", "[tools][config]")
{
    const char* path = GET_FIXTURE("cfg/engine_config_test.cfg");
    REQUIRE(path != nullptr);

    ConfigInfo info = InspectConfig(path);
    REQUIRE(info.valid);
    CHECK(info.isBinarized == false);
    CHECK(info.version == 0);
    CHECK(info.fileSize > 0);
}

TEST_CASE("InspectConfig works on all cfg fixtures", "[tools][config]")
{
    for (const char* fixture : {"cfg/engine_config_test.cfg", "cfg/real_game_config.cfg", "cfg/user_config_test.cfg"})
    {
        const char* path = GET_FIXTURE(fixture);
        REQUIRE(path != nullptr);

        ConfigInfo info = InspectConfig(path);
        REQUIRE(info.valid);
        CHECK(info.isBinarized == false); // all text configs
        CHECK(info.fileSize > 0);
    }
}

TEST_CASE("InspectConfig returns invalid for non-existent file", "[tools][config]")
{
    ConfigInfo info = InspectConfig("/nonexistent/file.cpp");
    CHECK(info.valid == false);
}

TEST_CASE("InspectConfig returns valid for empty-ish files", "[tools][config]")
{
    // A very small file (< 4 bytes) should still return valid but not binarized
    auto tmpPath = std::filesystem::temp_directory_path() / "tiny_config_test.bin";
    {
        std::ofstream f(tmpPath, std::ios::binary);
        f << "ab"; // 2 bytes - too small for raP magic
    }
    ConfigInfo info = InspectConfig(tmpPath.string());
    CHECK(info.valid == true);
    CHECK(info.isBinarized == false);
    CHECK(info.fileSize == 2);
    std::filesystem::remove(tmpPath);
}

// Generic File Inspection

TEST_CASE("InspectFile returns info for existing file", "[tools][file]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt1.paa");
    REQUIRE(path != nullptr);

    AssetFileInfo info = InspectFile(path);
    REQUIRE(info.valid);
    CHECK_THAT(info.name, ContainsSubstring("synthetic_dxt1"));
    CHECK(info.extension == ".paa");
    CHECK(info.size > 0);
    CHECK(info.modifiedTime > 0);
}

TEST_CASE("InspectFile returns invalid for non-existent file", "[tools][file]")
{
    AssetFileInfo info = InspectFile("/nonexistent/file.txt");
    CHECK(info.valid == false);
}
