// FontSystem — gate for FreeType text rendering.  Apps that want text
// call `FontSystem::Initialize()` (loud failure if any required TTF is
// missing).  Apps that don't, leave it untouched and `Font::Load`
// short-circuits to an empty Font.

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontSystem.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Poseidon::FontSystem;

namespace
{
// Re-execute test cases in a clean working directory so a previous case's
// staged fonts don't bleed into the next.
class ScopedCwd
{
  public:
    explicit ScopedCwd(const std::filesystem::path& dir) : _prev(std::filesystem::current_path())
    {
        std::filesystem::create_directories(dir);
        std::filesystem::current_path(dir);
    }
    ~ScopedCwd() { std::filesystem::current_path(_prev); }

  private:
    std::filesystem::path _prev;
};

// Write a non-empty placeholder file at the path AutoOpen will look for.
// FontSystem::Initialize only checks `rest() > 0`, so any non-empty
// content is enough to pass the existence gate.
void StageFont(const std::string& relPath)
{
    std::string nativePath = relPath;
    // `std::filesystem::path::preferred_separator` is `wchar_t` on Windows
    // and can't feed `std::replace` on a `std::string`; use an explicit char.
#ifdef _WIN32
    constexpr char nativeSep = '\\';
#else
    constexpr char nativeSep = '/';
#endif
    std::replace(nativePath.begin(), nativePath.end(), '\\', nativeSep);
    std::filesystem::path p = nativePath;
    if (!p.parent_path().empty())
        std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << "stub";
}
} // namespace

TEST_CASE("FontSystem reports the slot-0 required font set", "[font][system]")
{
    auto required = FontSystem::RequiredFonts();
    REQUIRE(required.size() == 5);

    // Match the slot-0 entries in font.cpp.  If font.cpp's table grows
    // a row, this list must too — otherwise Initialize would happily
    // succeed against an incomplete set.
    const std::vector<std::string> expected = {
        "Fonts\\cwr_title.ttf", "Fonts\\cwr_body.ttf", "Fonts\\cwr_mono.ttf",
        "Fonts\\cwr_serif.ttf", "Fonts\\cwr_hand.ttf",
    };
    for (const auto& path : expected)
    {
        REQUIRE(std::find(required.begin(), required.end(), path) != required.end());
    }
}

TEST_CASE("FontSystem::IsAvailable is false before Initialize", "[font][system]")
{
    // Use a fresh empty cwd so any prior test's stubs are gone.
    auto tmp = std::filesystem::temp_directory_path() / "ofpr-fontsystem-prebar";
    std::filesystem::remove_all(tmp);
    ScopedCwd cwd(tmp);

    FontSystem::Instance().Shutdown(); // reset singleton state
    CHECK_FALSE(FontSystem::Instance().IsAvailable());

    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(tmp);
}

TEST_CASE("FontSystem::RequiredFontsMissing lists absent files", "[font][system]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ofpr-fontsystem-missing";
    std::filesystem::remove_all(tmp);
    ScopedCwd cwd(tmp);

    auto missing = FontSystem::RequiredFontsMissing();
    CHECK(missing.size() == 5);

    StageFont("Fonts\\cwr_body.ttf");
    auto stillMissing = FontSystem::RequiredFontsMissing();
    CHECK(stillMissing.size() == 4);
    CHECK(std::find(stillMissing.begin(), stillMissing.end(), "Fonts\\cwr_body.ttf") == stillMissing.end());

    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(tmp);
}

TEST_CASE("FontSystem::Initialize succeeds with all fonts present", "[font][system]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ofpr-fontsystem-ok";
    std::filesystem::remove_all(tmp);
    ScopedCwd cwd(tmp);

    for (const auto& path : FontSystem::RequiredFonts())
        StageFont(path);

    FontSystem::Instance().Shutdown(); // reset singleton from previous tests
    FontSystem::Instance().Initialize();
    CHECK(FontSystem::Instance().IsAvailable());

    FontSystem::Instance().Shutdown();
    CHECK_FALSE(FontSystem::Instance().IsAvailable());

    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(tmp);
}

TEST_CASE("FontSystem::Initialize is idempotent", "[font][system]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ofpr-fontsystem-idem";
    std::filesystem::remove_all(tmp);
    ScopedCwd cwd(tmp);

    for (const auto& path : FontSystem::RequiredFonts())
        StageFont(path);

    FontSystem::Instance().Shutdown();
    FontSystem::Instance().Initialize();
    CHECK(FontSystem::Instance().IsAvailable());
    FontSystem::Instance().Initialize(); // second call should be a no-op
    CHECK(FontSystem::Instance().IsAvailable());

    FontSystem::Instance().Shutdown();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(tmp);
}

TEST_CASE("FontSystem supports Shutdown then re-Initialize", "[font][system]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ofpr-fontsystem-reinit";
    std::filesystem::remove_all(tmp);
    ScopedCwd cwd(tmp);

    for (const auto& path : FontSystem::RequiredFonts())
        StageFont(path);

    FontSystem::Instance().Shutdown();
    FontSystem::Instance().Initialize();
    CHECK(FontSystem::Instance().IsAvailable());

    FontSystem::Instance().Shutdown();
    CHECK_FALSE(FontSystem::Instance().IsAvailable());

    FontSystem::Instance().Initialize();
    CHECK(FontSystem::Instance().IsAvailable());

    FontSystem::Instance().Shutdown();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(tmp);
}
