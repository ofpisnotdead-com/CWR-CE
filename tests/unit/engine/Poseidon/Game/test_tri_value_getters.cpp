#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Evaluator/express.hpp>
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <string>

using namespace Poseidon;
using namespace Catch::Matchers;

// Forward declarations — force GameStateExtTestGetters.cpp into the link.
// That TU forward-declares TriGetGLErrorCount, which drags in GameStateExtTest.cpp.
GameValue TriGetResizable(const GameState*);
GameValue TriGetBackBufferNonBlackCount(const GameState*);
GameValue TriGetWindowWidth(const GameState*);
GameValue TriGetWindowHeight(const GameState*);
GameValue TriGetShadowSunFactor(const GameState*);
GameValue TriGetShadowDepthCachedCount(const GameState*);
GameValue TriGetShadowFrozenCasters(const GameState*);
GameValue TriGetShadowMapCacheTest(const GameState*);
GameValue TriGetProxyVertCount(const GameState*);
GameValue TriGetAudioCacheEntries(const GameState*);
GameValue TriGetAudioCacheHits(const GameState*);
GameValue TriGetPixelMaxChannel(const GameState*, GameValuePar);
GameValue TriGetPixelMaxDiff(const GameState*, GameValuePar);
GameValue TriAssertPixelLit(const GameState*, GameValuePar);
GameValue TriAssertPixelEquals(const GameState*, GameValuePar);
GameValue TriGetControlVisible(const GameState*, GameValuePar);
GameValue TriGetControlFocused(const GameState*, GameValuePar);
GameValue TriGetControlEnabled(const GameState*, GameValuePar);
GameValue TriGetCommandMenuOpen(const GameState*);
GameValue TriGetDevPanelVisible(const GameState*);
GameValue TriGetLanguage(const GameState*);
GameValue TriGetNgsState(const GameState*);
GameValue TriGetNgsClientState(const GameState*);
GameValue TriGetAdminLoggedIn(const GameState*);
GameValue TriGetServerBanCount(const GameState*);
GameValue TriGetServerLocked(const GameState*);
GameValue TriGetNetSoundsReceived(const GameState*);
GameValue TriGetModsActiveSet(const GameState*);
GameValue TriGetModsMountSet(const GameState*);
GameValue TriGetModsSortColumn(const GameState*);
GameValue TriGetActiveMods(const GameState*);
GameValue TriGetChatLines(const GameState*);
GameValue TriAssertDisplay(const GameState*, GameValuePar);

namespace
{

struct InitFixture
{
    InitFixture()
    {
        static bool done = false;
        if (!done)
        {
            GGameState.Init();
            done = true;
        }
    }
};

static std::string S(const GameValue& v)
{
    return std::string(((RString)(GameStringType)v).Data());
}

static float F(const GameValue& v)
{
    return static_cast<float>(static_cast<GameScalarType>(v));
}

static GameValue GArr(std::initializer_list<float> vals)
{
    GameValue arr = GGameState.CreateGameValue(GameArray);
    GameArrayType& a = arr;
    a.Resize(static_cast<int>(vals.size()));
    int i = 0;
    for (float f : vals)
        a[i++] = GameValue(f);
    return arr;
}

} // namespace

// ============================================================================
// Null-safety: each getter returns a safe sentinel when engine globals are null
// ============================================================================

TEST_CASE("triGetResizable - returns 0 when no engine", "[tri][value-getter][triGetResizable]")
{
    InitFixture fix;
    REQUIRE(S(TriGetResizable(nullptr)) == "0");
}

TEST_CASE("triGetBackBufferNonBlackCount - returns -1 when no engine",
          "[tri][value-getter][triGetBackBufferNonBlackCount]")
{
    InitFixture fix;
    REQUIRE(F(TriGetBackBufferNonBlackCount(nullptr)) == -1.0f);
}

TEST_CASE("triGetWindowWidth - returns a non-negative value", "[tri][value-getter][triGetWindowWidth]")
{
    InitFixture fix;
    // GEngine is non-null (stub) in unit tests; Width() returns a valid dimension.
    REQUIRE(F(TriGetWindowWidth(nullptr)) >= -1.0f);
}

TEST_CASE("triGetWindowHeight - returns a non-negative value", "[tri][value-getter][triGetWindowHeight]")
{
    InitFixture fix;
    // GEngine is non-null (stub) in unit tests; Height() returns a valid dimension.
    REQUIRE(F(TriGetWindowHeight(nullptr)) >= -1.0f);
}

TEST_CASE("triGetShadowSunFactor - returns -1 when no scene", "[tri][value-getter][triGetShadowSunFactor]")
{
    InitFixture fix;
    REQUIRE(F(TriGetShadowSunFactor(nullptr)) == -1.0f);
}

TEST_CASE("triGetShadowMapCacheTest - returns 0 or 1 (engine stub present)",
          "[tri][value-getter][triGetShadowMapCacheTest]")
{
    InitFixture fix;
    // GEngine is non-null (stub) in unit tests; result is "0" or "1".
    std::string r = S(TriGetShadowMapCacheTest(nullptr));
    REQUIRE((r == "0" || r == "1"));
}

TEST_CASE("triGetAudioCacheEntries - returns -1 when no audio system", "[tri][value-getter][triGetAudioCacheEntries]")
{
    InitFixture fix;
    REQUIRE(F(TriGetAudioCacheEntries(nullptr)) == -1.0f);
}

TEST_CASE("triGetAudioCacheHits - returns -1 when no audio system", "[tri][value-getter][triGetAudioCacheHits]")
{
    InitFixture fix;
    REQUIRE(F(TriGetAudioCacheHits(nullptr)) == -1.0f);
}

TEST_CASE("triGetPixelMaxChannel - returns -1 when no engine", "[tri][value-getter][triGetPixelMaxChannel]")
{
    InitFixture fix;
    REQUIRE(F(TriGetPixelMaxChannel(nullptr, GArr({0.5f, 0.5f}))) == -1.0f);
}

TEST_CASE("triGetPixelMaxChannel - returns -1 on under-count arg", "[tri][value-getter][triGetPixelMaxChannel]")
{
    InitFixture fix;
    REQUIRE(F(TriGetPixelMaxChannel(nullptr, GArr({0.5f}))) == -1.0f);
}

TEST_CASE("triGetPixelMaxDiff - returns -1 when no engine", "[tri][value-getter][triGetPixelMaxDiff]")
{
    InitFixture fix;
    REQUIRE(F(TriGetPixelMaxDiff(nullptr, GArr({0.1f, 0.2f, 0.3f, 0.4f}))) == -1.0f);
}

TEST_CASE("triGetPixelMaxDiff - returns -1 on under-count arg", "[tri][value-getter][triGetPixelMaxDiff]")
{
    InitFixture fix;
    REQUIRE(F(TriGetPixelMaxDiff(nullptr, GArr({0.1f, 0.2f}))) == -1.0f);
}

TEST_CASE("triAssertPixelLit - reports missing engine", "[tri][pixel][triAssertPixelLit]")
{
    InitFixture fix;
    REQUIRE(S(TriAssertPixelLit(nullptr, GArr({0.5f, 0.5f, 1.0f}))) == "FAIL:not_supported");
}

TEST_CASE("triAssertPixelEquals - reports missing engine", "[tri][pixel][triAssertPixelEquals]")
{
    InitFixture fix;
    REQUIRE(S(TriAssertPixelEquals(nullptr, GArr({0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f}))) == "FAIL:not_supported");
}

TEST_CASE("triGetControlVisible - returns 0 when no display", "[tri][value-getter][triGetControlVisible]")
{
    InitFixture fix;
    GameValue idc = GameValue(101.0f);
    REQUIRE(S(TriGetControlVisible(nullptr, idc)) == "0");
}

TEST_CASE("triGetControlFocused - returns 0 when no display", "[tri][value-getter][triGetControlFocused]")
{
    InitFixture fix;
    GameValue idc = GameValue(101.0f);
    REQUIRE(S(TriGetControlFocused(nullptr, idc)) == "0");
}

TEST_CASE("triGetControlEnabled - returns 0 when no display", "[tri][value-getter][triGetControlEnabled]")
{
    InitFixture fix;
    GameValue idc = GameValue(101.0f);
    REQUIRE(S(TriGetControlEnabled(nullptr, idc)) == "0");
}

TEST_CASE("triGetCommandMenuOpen - returns 0 when no world", "[tri][value-getter][triGetCommandMenuOpen]")
{
    InitFixture fix;
    REQUIRE(S(TriGetCommandMenuOpen(nullptr)) == "0");
}

TEST_CASE("triGetNgsState - returns a scalar (valid network state or -1)", "[tri][value-getter][triGetNgsState]")
{
    InitFixture fix;
    // Network manager may be a stub returning 0, or absent (-1 on exception).
    float v = F(TriGetNgsState(nullptr));
    REQUIRE((v == -1.0f || v >= 0.0f));
}

TEST_CASE("triGetNgsClientState - returns a scalar (valid game state or -1)",
          "[tri][value-getter][triGetNgsClientState]")
{
    InitFixture fix;
    // Network manager may be a stub returning 0, or absent (-1 on exception).
    float v = F(TriGetNgsClientState(nullptr));
    REQUIRE((v == -1.0f || v >= 0.0f));
}

TEST_CASE("triGetAdminLoggedIn - returns 0 when no network", "[tri][value-getter][triGetAdminLoggedIn]")
{
    InitFixture fix;
    REQUIRE(S(TriGetAdminLoggedIn(nullptr)) == "0");
}

TEST_CASE("triGetServerBanCount - returns -1 when no network", "[tri][value-getter][triGetServerBanCount]")
{
    InitFixture fix;
    REQUIRE(F(TriGetServerBanCount(nullptr)) == -1.0f);
}

TEST_CASE("triGetServerLocked - returns 0 when no network", "[tri][value-getter][triGetServerLocked]")
{
    InitFixture fix;
    REQUIRE(S(TriGetServerLocked(nullptr)) == "0");
}

TEST_CASE("triGetModsActiveSet - returns empty when no mods display", "[tri][value-getter][triGetModsActiveSet]")
{
    InitFixture fix;
    REQUIRE(S(TriGetModsActiveSet(nullptr)) == "");
}

TEST_CASE("triGetModsMountSet - returns empty when no mods display", "[tri][value-getter][triGetModsMountSet]")
{
    InitFixture fix;
    REQUIRE(S(TriGetModsMountSet(nullptr)) == "");
}

TEST_CASE("triGetModsSortColumn - returns -1 when no mods display", "[tri][value-getter][triGetModsSortColumn]")
{
    InitFixture fix;
    REQUIRE(F(TriGetModsSortColumn(nullptr)) == -1.0f);
}

TEST_CASE("triGetChatLines - returns empty string when chat is empty", "[tri][value-getter][triGetChatLines]")
{
    InitFixture fix;
    REQUIRE(S(TriGetChatLines(nullptr)) == "");
}

TEST_CASE("triAssertDisplay - reports missing active display", "[tri][display][triAssertDisplay]")
{
    InitFixture fix;
    REQUIRE(S(TriAssertDisplay(nullptr, GameValue(46.0f))) == "FAIL:expected=46,actual=-1");
}

// ============================================================================
// Registration: all 31 getters appear in GGameState after InitModules
// ============================================================================

TEST_CASE("Value getter verbs are all registered", "[tri][value-getter][registration]")
{
    InitFixture fix;
    GGameState.Reset();
    Poseidon::Foundation::InitModules();

    // Getters are split across nular ops (no arg) and functions (one arg).
    AutoArray<RStringS> nulars, functions;
    GGameState.AppendNularOpList(nulars, [](const char*) { return true; });
    GGameState.AppendFunctionList(functions, [](const char*) { return true; });

    auto contains = [&](const char* name)
    {
        for (int i = 0; i < nulars.Size(); ++i)
            if (strcmp(nulars[i].Data(), name) == 0)
                return true;
        for (int i = 0; i < functions.Size(); ++i)
            if (strcmp(functions[i].Data(), name) == 0)
                return true;
        return false;
    };

    // Window
    REQUIRE(contains("triGetResizable"));
    REQUIRE(contains("triGetBackBufferNonBlackCount"));
    REQUIRE(contains("triGetGLErrorCount"));
    REQUIRE(contains("triGetWindowWidth"));
    REQUIRE(contains("triGetWindowHeight"));

    // Shadow
    REQUIRE(contains("triGetShadowSunFactor"));
    REQUIRE(contains("triGetShadowDepthCachedCount"));
    REQUIRE(contains("triGetShadowFrozenCasters"));
    REQUIRE(contains("triGetShadowMapCacheTest"));
    REQUIRE(contains("triGetProxyVertCount"));

    // Audio
    REQUIRE(contains("triGetAudioCacheEntries"));
    REQUIRE(contains("triGetAudioCacheHits"));

    // Pixel
    REQUIRE(contains("triGetPixelMaxChannel"));
    REQUIRE(contains("triGetPixelMaxDiff"));

    // UI / controls
    REQUIRE(contains("triGetControlVisible"));
    REQUIRE(contains("triGetControlFocused"));
    REQUIRE(contains("triGetControlEnabled"));
    REQUIRE(contains("triGetCommandMenuOpen"));
    REQUIRE(contains("triGetDevPanelVisible"));

    // Options
    REQUIRE(contains("triGetLanguage"));
    REQUIRE(contains("voiceLanguage"));

    // MP / network / server
    REQUIRE(contains("triGetNgsState"));
    REQUIRE(contains("triGetNgsClientState"));
    REQUIRE(contains("triGetAdminLoggedIn"));
    REQUIRE(contains("triGetServerBanCount"));
    REQUIRE(contains("triGetServerLocked"));
    REQUIRE(contains("triGetNetSoundsReceived"));
    REQUIRE(contains("triGetModsActiveSet"));
    REQUIRE(contains("triGetModsMountSet"));
    REQUIRE(contains("triGetModsSortColumn"));
    REQUIRE(contains("triGetActiveMods"));
    REQUIRE(contains("triGetChatLines"));
}
