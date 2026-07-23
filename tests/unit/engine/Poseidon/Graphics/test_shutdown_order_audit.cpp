#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <algorithm>
#include <catch2/catch_message.hpp>

// I-05 / B-019: Lifecycle teardown order is honored.
//
// B-019 was an OpenGL shutdown crash — `FontCache` held
// `Ref<Texture>` slots that referenced `TextureBank` entries.  If
// `TextureBank` was destroyed before `FontCache::Clear()` ran, the
// font cache's references became dangling.  The shipping
// `EngineGL33::ShutdownGL` clears `_fonts` *before* deleting
// `_textBank`; ASAN / a real shutdown smoke test catches the
// reverse order, but we can also pin the structural property by
// asserting the source-file line order.

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

std::filesystem::path Gl33Dir()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33";
}

std::filesystem::path PoseidonDir()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon";
}

} // namespace

TEST_CASE("I-05: ShutdownGL clears font cache BEFORE deleting texture bank (B-019)",
          "[Graphics][GL33][ShutdownOrder][I-05]")
{
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33_Lifecycle.cpp");
    REQUIRE_FALSE(body.empty());

    // Find ShutdownGL's body.  The function is short and self-contained.
    const std::string fnTag = "void EngineGL33::ShutdownGL()";
    const size_t fnPos = body.find(fnTag);
    REQUIRE(fnPos != std::string::npos);

    // Scan a generous window after the function header for the two
    // calls we care about.  Both fall inside the same function
    // body (which is ~25 lines).
    const size_t scanEnd = std::min(body.size(), fnPos + 2000);
    const std::string region = body.substr(fnPos, scanEnd - fnPos);

    const size_t fontsClearPos = region.find("_fonts.Clear()");
    const size_t bankDeletePos = region.find("delete _textBank");
    CAPTURE(fontsClearPos);
    CAPTURE(bankDeletePos);

    REQUIRE(fontsClearPos != std::string::npos);
    REQUIRE(bankDeletePos != std::string::npos);

    // The structural invariant: _fonts must release its texture refs
    // BEFORE the texture bank is destroyed.  Source-line order is the
    // most direct expression of the runtime order.
    REQUIRE(fontsClearPos < bankDeletePos);
}

TEST_CASE("I-05: BackToFront / FinishFrame path does not destroy texture bank", "[Graphics][GL33][ShutdownOrder][I-05]")
{
    // Adjacent concern: the per-frame teardown helpers (FinishFrame,
    // BackToFront, etc.) must not delete `_textBank` — only
    // ShutdownGL does.  A stray `delete _textBank` in the hot path
    // would intermittently surface as B-019 mid-game.
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33_Lifecycle.cpp");
    REQUIRE_FALSE(body.empty());

    // Count `delete _textBank` occurrences — must be exactly one
    // (inside ShutdownGL).
    int count = 0;
    size_t pos = 0;
    while ((pos = body.find("delete _textBank", pos)) != std::string::npos)
    {
        ++count;
        pos += std::string("delete _textBank").size();
    }
    REQUIRE(count == 1);
}

TEST_CASE("I-05 T1: ShutdownGuard is the last-declared EngineGL33 member", "[Graphics][GL33][ShutdownOrder][I-05]")
{
    // T1 lift: a private `ShutdownGuard` RAII member is declared
    // LAST in `class EngineGL33`.  C++ destroys members in reverse
    // declaration order, so the guard's destructor runs FIRST in
    // EngineGL33 teardown — before `_textBank` is destroyed.  The
    // guard calls `engine->ClearFontCache()` so the FontCache's
    // Ref<Texture> slots release while the texture bank is still
    // alive.  B-019's dangling-pointer path is structurally blocked.
    const std::string hdr = ReadTextFile(Gl33Dir() / "EngineGL33.hpp");
    REQUIRE_FALSE(hdr.empty());

    REQUIRE(hdr.find("struct ShutdownGuard") != std::string::npos);
    REQUIRE(hdr.find("ShutdownGuard _shutdownGuard") != std::string::npos);

    // _shutdownGuard appears AFTER `_textBank` in the header, so its
    // destructor runs BEFORE _textBank's.
    const size_t bankDecl = hdr.find("TextBankGL33* _textBank");
    const size_t guardDecl = hdr.find("ShutdownGuard _shutdownGuard");
    REQUIRE(bankDecl != std::string::npos);
    REQUIRE(guardDecl != std::string::npos);
    REQUIRE(bankDecl < guardDecl);
}

TEST_CASE("I-05 T1: ShutdownGuard destructor calls ClearFontCache", "[Graphics][GL33][ShutdownOrder][I-05]")
{
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33.cpp");
    REQUIRE_FALSE(body.empty());

    const size_t dtorPos = body.find("EngineGL33::ShutdownGuard::~ShutdownGuard");
    REQUIRE(dtorPos != std::string::npos);

    const size_t scanEnd = std::min(body.size(), dtorPos + 1000);
    const std::string region = body.substr(dtorPos, scanEnd - dtorPos);
    REQUIRE(region.find("ClearFontCache") != std::string::npos);
}

TEST_CASE("I-05: EngineGL33 destructor delegates to ShutdownGL", "[Graphics][GL33][ShutdownOrder][I-05]")
{
    // The destructor needs to call ShutdownGL (or equivalent) so the
    // ordered-teardown path runs even when an exception or early
    // return skips the explicit shutdown call.
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33.cpp");
    REQUIRE_FALSE(body.empty());

    const std::string dtorTag = "EngineGL33::~EngineGL33()";
    const size_t dtorPos = body.find(dtorTag);
    REQUIRE(dtorPos != std::string::npos);

    // Scan a small window after the destructor declaration for a
    // ShutdownGL call.  Window covers the destructor body even if
    // it grows to ~40 lines.
    const size_t scanEnd = std::min(body.size(), dtorPos + 2000);
    const std::string region = body.substr(dtorPos, scanEnd - dtorPos);

    CAPTURE(region);
    REQUIRE(region.find("ShutdownGL") != std::string::npos);
}

TEST_CASE("I-05: FontSystem shutdown clears FreeType caches before engine destruction",
          "[Graphics][GL33][ShutdownOrder][I-05]")
{
    const std::string fontSystem = ReadTextFile(PoseidonDir() / "Graphics" / "Rendering" / "Draw" / "FontSystem.cpp");
    REQUIRE_FALSE(fontSystem.empty());
    REQUIRE(fontSystem.find("ClearFreeTypeCaches()") != std::string::npos);

    const std::string shutdown = ReadTextFile(PoseidonDir() / "Foundation" / "Platform" / "Shutdown.cpp");
    REQUIRE_FALSE(shutdown.empty());

    // The ordered teardown lives in UnloadGameData (DDTerm delegates to it, and
    // a mod re-mount reuses it with keepEngine=true). The font-cache-before-
    // engine-destroy invariant must hold there.
    const size_t unloadPos = shutdown.find("void UnloadGameData(bool keepEngine)");
    REQUIRE(unloadPos != std::string::npos);

    const size_t scanEnd = std::min(shutdown.size(), unloadPos + 2000);
    const std::string region = shutdown.substr(unloadPos, scanEnd - unloadPos);

    const size_t clearFontCachePos = region.find("GEngine->ClearFontCache()");
    const size_t fontSystemShutdownPos = region.find("FontSystem::Instance().Shutdown()");
    const size_t destroyEnginePos = region.find("DestroyEngine()");
    CAPTURE(clearFontCachePos);
    CAPTURE(fontSystemShutdownPos);
    CAPTURE(destroyEnginePos);
    CAPTURE(region);

    REQUIRE(clearFontCachePos != std::string::npos);
    REQUIRE(fontSystemShutdownPos != std::string::npos);
    REQUIRE(destroyEnginePos != std::string::npos);
    REQUIRE(clearFontCachePos < fontSystemShutdownPos);
    REQUIRE(fontSystemShutdownPos < destroyEnginePos);
}

TEST_CASE("I-05: Font destructor tolerates shutdown after GEngine is gone", "[Graphics][GL33][ShutdownOrder][I-05]")
{
    const std::string body = ReadTextFile(PoseidonDir() / "Graphics" / "Rendering" / "Draw" / "Font.cpp");
    REQUIRE_FALSE(body.empty());

    const std::string dtorTag = "Font::~Font()";
    const size_t dtorPos = body.find(dtorTag);
    REQUIRE(dtorPos != std::string::npos);

    const size_t scanEnd = std::min(body.size(), dtorPos + 400);
    const std::string region = body.substr(dtorPos, scanEnd - dtorPos);
    CAPTURE(region);

    REQUIRE(region.find("if (GLOB_ENGINE)") != std::string::npos);
    REQUIRE(region.find("GLOB_ENGINE->FontDestroyed(this)") != std::string::npos);
}

TEST_CASE("I-05: FreeType renderer cache survives texture cache clears", "[Graphics][Font][ShutdownOrder][I-05]")
{
    const std::string body = ReadTextFile(PoseidonDir() / "Graphics" / "Rendering" / "Draw" / "Font.cpp");
    REQUIRE_FALSE(body.empty());

    const std::string fnTag = "void ClearFreeTypeCaches()";
    const size_t fnPos = body.find(fnTag);
    REQUIRE(fnPos != std::string::npos);

    const size_t scanEnd = std::min(body.size(), fnPos + 700);
    const std::string region = body.substr(fnPos, scanEnd - fnPos);
    CAPTURE(region);

    REQUIRE(region.find("ClearFreeTypeAtlasTextures()") != std::string::npos);
    REQUIRE(region.find("GetFTRenderers().clear()") == std::string::npos);
}
