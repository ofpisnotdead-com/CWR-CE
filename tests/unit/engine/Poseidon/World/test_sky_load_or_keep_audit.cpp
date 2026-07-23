#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <algorithm>
#include <catch2/catch_message.hpp>

// I-23 / B-023: Asset-load returns valid asset or fails caller.
//
// B-023 was the GL33 `TextBankGL::LoadInterpolated` returning
// nullptr; the result propagated through `Weather::SetSky` into
// `Landscape::SetSkyTexture(nullptr)`, which cleared every sky-
// sphere face's texture and rendered white.
//
// The structural fix: a single template helper `TryReplaceSky`
// in `landscape.cpp` accepts a loader closure and applies it to
// the sky slot ONLY on a non-null load.  Both `Weather::SetSky`
// overloads route through the helper.  The helper is the unique
// site where the null-check lives; the load-or-keep pattern
// cannot be copy-pasted wrong because adding a third SetSky
// overload would either reuse the helper (correctness inherited)
// or get flagged by the audit's "exactly two callers" check.

namespace
{

std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::filesystem::path LandscapeCpp()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" / "World" / "Terrain" /
           "Landscape.cpp";
}

std::filesystem::path LightsCpp()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" / "Graphics" / "Rendering" /
           "Lighting" / "Lights.cpp";
}

int CountOccurrences(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return 0;
    int n = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

} // namespace

TEST_CASE("I-23 T1: TryReplaceSky helper centralizes the null-guard (B-023)", "[World][SkyLoadOrKeep][I-23]")
{
    const std::string body = ReadTextFile(LandscapeCpp());
    REQUIRE_FALSE(body.empty());

    // The helper exists with the documented name and the null-guard.
    const size_t helperPos = body.find("void TryReplaceSky");
    REQUIRE(helperPos != std::string::npos);

    // Within the helper body, `if (!loaded) return;` is the load-or-keep
    // chokepoint.  Scan a window after the helper signature.
    const size_t scanEnd = std::min(body.size(), helperPos + 800);
    const std::string region = body.substr(helperPos, scanEnd - helperPos);
    REQUIRE(region.find("if (!loaded)") != std::string::npos);
    REQUIRE(region.find("return") != std::string::npos);
}

TEST_CASE("I-23 T1: both Weather::SetSky overloads route through TryReplaceSky", "[World][SkyLoadOrKeep][I-23]")
{
    const std::string body = ReadTextFile(LandscapeCpp());
    REQUIRE_FALSE(body.empty());

    // Two Weather::SetSky overloads.  Each body invokes TryReplaceSky
    // exactly once.  Total TryReplaceSky callsites = 2.
    const int callsites = CountOccurrences(body, "TryReplaceSky(_sky");
    CAPTURE(callsites);
    REQUIRE(callsites == 2);

    // Neither overload reads `_sky->SetMaxSize` directly — that's
    // inside the helper.  A caller that copy-pasted the body would
    // re-introduce the per-callsite null-handling pattern and trip
    // the count audit above.
    const std::string overload1 = "void Weather::SetSky(Landscape* land, RStringB name)";
    const size_t o1 = body.find(overload1);
    REQUIRE(o1 != std::string::npos);
    const size_t o1End = body.find('}', o1);
    REQUIRE(o1End != std::string::npos);
    const std::string b1 = body.substr(o1, o1End - o1);
    REQUIRE(b1.find("TryReplaceSky") != std::string::npos);
    REQUIRE(b1.find("SetMaxSize") == std::string::npos);
}

TEST_CASE("I-23 T1: no direct land->SetSkyTexture(nullptr) in landscape.cpp", "[World][SkyLoadOrKeep][I-23]")
{
    // The B-023 failure path was `Landscape::SetSkyTexture(nullptr)`.
    // The structural protection: every SetSkyTexture call in
    // landscape.cpp passes a non-null Ref<Texture>.  Currently the
    // helper passes `slot` which is verified non-null above the call;
    // assert no callsite literally passes `nullptr`.
    const std::string body = ReadTextFile(LandscapeCpp());
    REQUIRE_FALSE(body.empty());
    REQUIRE(body.find("SetSkyTexture(nullptr)") == std::string::npos);
    REQUIRE(body.find("SetSkyTexture(NULL)") == std::string::npos);
    REQUIRE(body.find("SetSkyTexture(0)") == std::string::npos);
}

TEST_CASE("Weather::Init initializes sky and cloud render state", "[World][SkyLoadOrKeep][WeatherInit]")
{
    const std::string body = ReadTextFile(LandscapeCpp());
    REQUIRE_FALSE(body.empty());

    const std::string signature = "void Weather::Init()";
    const size_t begin = body.find(signature);
    REQUIRE(begin != std::string::npos);
    const size_t end = body.find("\n}", begin);
    REQUIRE(end != std::string::npos);
    const std::string initBody = body.substr(begin, end - begin);

    REQUIRE(initBody.find("_fogSet = 0.0f") != std::string::npos);
    REQUIRE(initBody.find("_cloudsAlpha = 0.0f") != std::string::npos);
    REQUIRE(initBody.find("_cloudsBrightness = 1.0f") != std::string::npos);
    REQUIRE(initBody.find("_cloudsSpeed = 0.2f") != std::string::npos);
    REQUIRE(initBody.find("_skyThrough = 1.0f") != std::string::npos);
    REQUIRE(initBody.find("_windSpeed = VZero") != std::string::npos);
    REQUIRE(initBody.find("_lastWindSpeedChange = Glob.time") != std::string::npos);
    REQUIRE(initBody.find("_gust = VZero") != std::string::npos);
    REQUIRE(initBody.find("_gustUntil = Glob.time") != std::string::npos);
}

TEST_CASE("LightSun default state is safe for first-frame cloud lighting", "[World][SkyLoadOrKeep][CloudLighting]")
{
    const std::string body = ReadTextFile(LightsCpp());
    REQUIRE_FALSE(body.empty());

    const std::string signature = "LightSun::LightSun()";
    const size_t begin = body.find(signature);
    REQUIRE(begin != std::string::npos);
    const size_t end = body.find("\n}", begin);
    REQUIRE(end != std::string::npos);
    const std::string ctorBody = body.substr(begin, end - begin);

    REQUIRE(ctorBody.find("_direction =") != std::string::npos);
    REQUIRE(ctorBody.find("_shadowDirection =") != std::string::npos);
    REQUIRE(ctorBody.find("_sunDirection =") != std::string::npos);
    REQUIRE(ctorBody.find("_moonDirection =") != std::string::npos);
    REQUIRE(ctorBody.find("_moonDirectionUp =") != std::string::npos);
    REQUIRE(ctorBody.find("_starsOrientation =") != std::string::npos);
    REQUIRE(ctorBody.find("_moonPhase =") != std::string::npos);
    REQUIRE(ctorBody.find("_nightEffect =") != std::string::npos);
    REQUIRE(ctorBody.find("_starsVisible =") != std::string::npos);
    REQUIRE(ctorBody.find("_colorFull =") != std::string::npos);
    REQUIRE(ctorBody.find("_diffuse =") != std::string::npos);
    REQUIRE(ctorBody.find("_ambient =") != std::string::npos);
    REQUIRE(ctorBody.find("_sunColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_sunObjectColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_sunHaloObjectColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_moonObjectColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_moonHaloObjectColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_skyColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_sunSkyColor =") != std::string::npos);
    REQUIRE(ctorBody.find("_ambientPrecalc =") != std::string::npos);
    REQUIRE(ctorBody.find("_diffusePrecalc =") != std::string::npos);
}
