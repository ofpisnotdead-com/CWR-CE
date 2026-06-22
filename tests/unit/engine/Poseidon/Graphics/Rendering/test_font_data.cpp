#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontData.hpp>
#include "test_fixtures.hpp"
#include <fstream>
#include <stddef.h>
#include <set>
#include <string>
#include <vector>

using Poseidon::FXYData;

// ParseFXY - basic parsing

TEST_CASE("ParseFXY parses single-set font (legacy)", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), size);

    QIStream stream(buf.data(), static_cast<int>(buf.size()));
    FXYData fxy = ParseFXY(stream, "legacy");

    REQUIRE(fxy.valid());
    CHECK(fxy.nChars == 224);
    CHECK(fxy.maxHeight == 3);
    CHECK(fxy.maxWidth == 3);
    CHECK(fxy.textureSetNums.size() == 1);
    CHECK(fxy.textureSetNums.count(1) == 1);
    CHECK(fxy.name == "legacy"); // lowercased
}

// FXY file structure

TEST_CASE("FXY file is exactly 2688 bytes (224 * 12)", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    CHECK(file.tellg() == 2688);
}

// Glyph properties

TEST_CASE("ParseFXY sets space char width to 3/4 of maxWidth", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), size);

    QIStream stream(buf.data(), static_cast<int>(buf.size()));
    FXYData fxy = ParseFXY(stream, "legacy");

    REQUIRE(fxy.valid());
    REQUIRE(fxy.nChars > 0);
    // Glyph 0 is space (char 32), width = maxW * 3/4
    CHECK(fxy.glyphs[0].w == fxy.maxWidth * 3 / 4);
}

TEST_CASE("ParseFXY produces valid glyph coordinates", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), size);

    QIStream stream(buf.data(), static_cast<int>(buf.size()));
    FXYData fxy = ParseFXY(stream, "legacy");

    REQUIRE(fxy.valid());
    for (size_t i = 1; i < fxy.glyphs.size(); i++) // skip space
    {
        const auto& g = fxy.glyphs[i];
        if (g.w <= 0 && g.h <= 0)
            continue; // empty glyph
        CHECK(g.x >= 0);
        CHECK(g.y >= 0);
        CHECK(g.w > 0);
        CHECK(g.h > 0);
        CHECK(g.setNum >= 1);
        CHECK(g.wTex >= g.w);
        CHECK(g.hTex >= g.h);
    }
}

// GetTextureNames

TEST_CASE("GetTextureNames returns correct names for single-set font", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), size);

    QIStream stream(buf.data(), static_cast<int>(buf.size()));
    FXYData fxy = ParseFXY(stream, "legacy");

    REQUIRE(fxy.valid());
    auto names = fxy.GetTextureNames();
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "legacy-01.paa");
}

// Edge cases

TEST_CASE("ParseFXY returns invalid for empty stream", "[font][parser]")
{
    QIStream stream(nullptr, 0);
    FXYData fxy = ParseFXY(stream, "empty");
    CHECK_FALSE(fxy.valid());
}

TEST_CASE("ParseFXY lowercases font name", "[font][parser]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), size);

    QIStream stream(buf.data(), static_cast<int>(buf.size()));
    FXYData fxy = ParseFXY(stream, "TaHoMaB24");

    REQUIRE(fxy.valid());
    CHECK(fxy.name == "tahomab24");
}
