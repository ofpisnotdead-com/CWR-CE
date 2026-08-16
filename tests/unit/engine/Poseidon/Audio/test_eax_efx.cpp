// test_eax_efx.cpp -- EAX/EFX preset mapping and config validation
// Verifies the OpenAL EFX implementation matches original DX8 EAX behavior
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stddef.h>
#include <string>

using namespace Poseidon;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Environment preset mapping (1:1 with soundDX8.cpp:2570-2578)
// ---------------------------------------------------------------------------

// These values encode the original DX8 DoSetEAXEnvironment mapping.
// EAX_ENVIRONMENT_GENERIC=0, ROOM=2, FOREST=15, CITY=16, MOUNTAINS=17, PLAIN=19
enum EAXEnvironment
{
    EAX_ENV_GENERIC = 0,
    EAX_ENV_ROOM = 2,
    EAX_ENV_FOREST = 15,
    EAX_ENV_CITY = 16,
    EAX_ENV_MOUNTAINS = 17,
    EAX_ENV_PLAIN = 19,
};

static int MapSEToEAX(SoundEnvironmentType type)
{
    switch (type)
    {
        case SEPlain:
            return EAX_ENV_PLAIN;
        case SEMountains:
            return EAX_ENV_MOUNTAINS;
        case SEForest:
            return EAX_ENV_CITY; // intentional -- matches DX8
        case SECity:
            return EAX_ENV_CITY;
        case SERoom:
            return EAX_ENV_ROOM;
        default:
            return EAX_ENV_GENERIC;
    }
}

TEST_CASE("EAX: SEPlain maps to EAX_ENVIRONMENT_PLAIN", "[Audio][EAX]")
{
    CHECK(MapSEToEAX(SEPlain) == EAX_ENV_PLAIN);
}

TEST_CASE("EAX: SEForest maps to EAX_ENVIRONMENT_CITY (intentional DX8 parity)", "[Audio][EAX]")
{
    // Original soundDX8.cpp:2574 -- SEForest mapped to CITY, not FOREST
    CHECK(MapSEToEAX(SEForest) == EAX_ENV_CITY);
    CHECK(MapSEToEAX(SEForest) != EAX_ENV_FOREST);
}

TEST_CASE("EAX: SECity maps to EAX_ENVIRONMENT_CITY", "[Audio][EAX]")
{
    CHECK(MapSEToEAX(SECity) == EAX_ENV_CITY);
}

TEST_CASE("EAX: SEMountains maps to EAX_ENVIRONMENT_MOUNTAINS", "[Audio][EAX]")
{
    CHECK(MapSEToEAX(SEMountains) == EAX_ENV_MOUNTAINS);
}

TEST_CASE("EAX: SERoom maps to EAX_ENVIRONMENT_ROOM", "[Audio][EAX]")
{
    CHECK(MapSEToEAX(SERoom) == EAX_ENV_ROOM);
}

// ---------------------------------------------------------------------------
// Size clamping (DX8:2584-2586)
// ---------------------------------------------------------------------------

static float ClampSize(float size)
{
    if (size < 2.f)
        size = 2.f;
    if (size > 100.f)
        size = 100.f;
    return size;
}

TEST_CASE("EAX: size clamped to minimum 2", "[Audio][EAX]")
{
    CHECK(ClampSize(0.f) == 2.f);
    CHECK(ClampSize(1.f) == 2.f);
    CHECK(ClampSize(-10.f) == 2.f);
}

TEST_CASE("EAX: size clamped to maximum 100", "[Audio][EAX]")
{
    CHECK(ClampSize(150.f) == 100.f);
    CHECK(ClampSize(100.1f) == 100.f);
}

TEST_CASE("EAX: size in range passes through", "[Audio][EAX]")
{
    CHECK(ClampSize(2.f) == 2.f);
    CHECK(ClampSize(50.f) == 50.f);
    CHECK(ClampSize(100.f) == 100.f);
}

// ---------------------------------------------------------------------------
// Size scaling (decay time proportional to size)
// ---------------------------------------------------------------------------

static float ScaleDecay(float baseDecay, float envSize, float presetDefaultSize)
{
    float sizeRatio = envSize / presetDefaultSize;
    float decay = baseDecay * sizeRatio;
    if (decay < 0.1f)
        decay = 0.1f;
    if (decay > 20.0f)
        decay = 20.0f;
    return decay;
}

TEST_CASE("EAX: decay time scales with size ratio", "[Audio][EAX]")
{
    float base = 1.49f;                                     // generic decay
    CHECK(ScaleDecay(base, 15.f, 7.5f) == Approx(2.98f));   // 2x size = 2x decay
    CHECK(ScaleDecay(base, 3.75f, 7.5f) == Approx(0.745f)); // half size = half decay
}

TEST_CASE("EAX: decay time clamped to [0.1, 20.0]", "[Audio][EAX]")
{
    CHECK(ScaleDecay(0.1f, 2.f, 100.f) == 0.1f);   // very small -> clamp at min
    CHECK(ScaleDecay(10.f, 100.f, 7.5f) == 20.0f); // very large -> clamp at max
}

TEST_CASE("EAX: density from environment size / 100", "[Audio][EAX]")
{
    auto density = [](float size)
    {
        float d = size / 100.f;
        if (d < 0.f)
            d = 0.f;
        if (d > 1.f)
            d = 1.f;
        return d;
    };

    CHECK(density(2.f) == Approx(0.02f));
    CHECK(density(50.f) == Approx(0.5f));
    CHECK(density(100.f) == Approx(1.0f));
    CHECK(density(200.f) == 1.0f); // clamped
}

// ---------------------------------------------------------------------------
// Change detection (DX8:2596-2614)
// ---------------------------------------------------------------------------

static bool EnvChanged(const SoundEnvironment& a, const SoundEnvironment& b)
{
    if (a.type != b.type)
        return true;
    if (fabs(a.size - b.size) >= 1.f)
        return true;
    if (fabs(a.density - b.density) >= 0.05f)
        return true;
    return false;
}

TEST_CASE("EAX: identical environment -> no change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEPlain, 75.f, 0.5f};
    CHECK_FALSE(EnvChanged(a, b));
}

TEST_CASE("EAX: different type -> change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEForest, 75.f, 0.5f};
    CHECK(EnvChanged(a, b));
}

TEST_CASE("EAX: size change >1 -> change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEPlain, 76.5f, 0.5f};
    CHECK(EnvChanged(a, b));
}

TEST_CASE("EAX: size change <1 -> no change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEPlain, 75.8f, 0.5f};
    CHECK_FALSE(EnvChanged(a, b));
}

TEST_CASE("EAX: density change >0.05 -> change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEPlain, 75.f, 0.56f};
    CHECK(EnvChanged(a, b));
}

TEST_CASE("EAX: density change <0.05 -> no change", "[Audio][EAX]")
{
    SoundEnvironment a{SEPlain, 75.f, 0.5f};
    SoundEnvironment b{SEPlain, 75.f, 0.54f};
    CHECK_FALSE(EnvChanged(a, b));
}

// ---------------------------------------------------------------------------
// Default state
// ---------------------------------------------------------------------------

TEST_CASE("EAX: SoundEnvironment struct default initializes", "[Audio][EAX]")
{
    SoundEnvironment env{SEPlain, 100.f, 0.5f};
    CHECK(env.type == SEPlain);
    CHECK(env.size == 100.f);
    CHECK(env.density == Approx(0.5f));
}

TEST_CASE("EAX: OpenAL software EFX is not gated by hardware acceleration", "[Audio][EAX][oal]")
{
    const std::filesystem::path sourcePath =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonOpenAL" / "SoundSystemOAL.cpp";
    std::ifstream in(sourcePath);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    CHECK(source.find("if (!_canEAX || !_hwAccel)") == std::string::npos);
    CHECK(source.find("EnableEAX(true): rejected — EFX not available") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Game-side environment values (from worldImpl.cpp)
// ---------------------------------------------------------------------------

TEST_CASE("EAX: worldImpl forest values", "[Audio][EAX]")
{
    // worldImpl.cpp:862 -- forest: size=38, density=0.5
    SoundEnvironment env{SEForest, 38.f, 0.5f};
    CHECK(MapSEToEAX(env.type) == EAX_ENV_CITY);
    CHECK(ClampSize(env.size) == 38.f);
}

TEST_CASE("EAX: worldImpl city values", "[Audio][EAX]")
{
    // worldImpl.cpp:875 -- city with 3 objects: size=(4-3)*15=15
    SoundEnvironment env{SECity, 15.f, 1.0f};
    CHECK(MapSEToEAX(env.type) == EAX_ENV_CITY);
    CHECK(ClampSize(env.size) == 15.f);
}

TEST_CASE("EAX: worldImpl mountains values", "[Audio][EAX]")
{
    // worldImpl.cpp:889 -- altitude 200m: size=clamp(200-120, 50, 100)=80
    SoundEnvironment env{SEMountains, 80.f, 0.5f};
    CHECK(MapSEToEAX(env.type) == EAX_ENV_MOUNTAINS);
    CHECK(ClampSize(env.size) == 80.f);
}

TEST_CASE("EAX: worldImpl plain values", "[Audio][EAX]")
{
    // worldImpl.cpp:902 -- plain with 0 objects: size=75-0*15=75
    SoundEnvironment env{SEPlain, 75.f, 0.5f};
    CHECK(MapSEToEAX(env.type) == EAX_ENV_PLAIN);
    CHECK(ClampSize(env.size) == 75.f);
}

// ---------------------------------------------------------------------------
// EFX Preset Registry (EFXPresets.hpp)
// ---------------------------------------------------------------------------

#include <PoseidonOpenAL/EFXPresets.hpp>

TEST_CASE("EFX: registry has 113 presets", "[Audio][EAX][EFX]")
{
    CHECK(EFX::kPresetCount == 113);
}

TEST_CASE("EFX: FindPreset returns correct entries", "[Audio][EAX][EFX]")
{
    CHECK(EFX::FindPreset("generic") != nullptr);
    CHECK(EFX::FindPreset("cave") != nullptr);
    CHECK(EFX::FindPreset("castle-hall") != nullptr);
    CHECK(EFX::FindPreset("nonexistent") == nullptr);
}

TEST_CASE("EFX: FindPreset is case-insensitive", "[Audio][EAX][EFX]")
{
    CHECK(EFX::FindPreset("CAVE") != nullptr);
    CHECK(EFX::FindPreset("Cave") != nullptr);
    CHECK(EFX::FindPreset("cave") != nullptr);
}

TEST_CASE("EFX: game presets are in registry", "[Audio][EAX][EFX]")
{
    const char* gamePresets[] = {"plain", "city", "mountains", "room", "generic"};
    for (const char* name : gamePresets)
    {
        auto* entry = EFX::FindPreset(name);
        REQUIRE(entry != nullptr);
        CHECK(entry->defaultSize > 0.0f);
    }
}

TEST_CASE("EFX: preset entries have valid default sizes", "[Audio][EAX][EFX]")
{
    for (size_t i = 0; i < EFX::kPresetCount; ++i)
    {
        CHECK(EFX::kPresets[i].defaultSize > 0.0f);
        CHECK(EFX::kPresets[i].name != nullptr);
        CHECK(EFX::kPresets[i].name[0] != '\0');
    }
}
