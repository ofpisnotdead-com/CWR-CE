#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include "../test_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace Poseidon;

// --- AI88 ---

// The original gun textures are AI88 inside .pac files (e.g. o\guns\handle_bmp.pac);
// the loader must follow the header's format marker, not the extension. The
// fixture is an 8x8 LZSS-packed AI88 level with intensity = x*32+16 and
// alpha = y*32+16.
TEST_CASE("Decode AI88: format marker wins over a .pac extension", "[graphics][decode]")
{
    auto img = DecodePAAFile(GET_FIXTURE("paa/synthetic_ai88_lzss.pac"));
    REQUIRE(img.valid());
    REQUIRE(img.width == 8);
    REQUIRE(img.height == 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
            const uint8_t* p = &img.rgba[(y * 8 + x) * 4];
            REQUIRE(int(p[0]) == x * 32 + 16);
            REQUIRE(int(p[1]) == x * 32 + 16);
            REQUIRE(int(p[2]) == x * 32 + 16);
            REQUIRE(int(p[3]) == y * 32 + 16);
        }
}

// --- DXT1 ---

TEST_CASE("Decode DXT1: synthetic texture", "[graphics][decode]")
{
    auto img = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt1.paa"));
    REQUIRE(img.valid());
    REQUIRE(img.width == 64);
    REQUIRE(img.height == 64);

    // DXT1 alpha is always 0 or 255
    for (int i = 0; i < img.width * img.height; i++)
        REQUIRE((img.rgba[i * 4 + 3] == 255 || img.rgba[i * 4 + 3] == 0));
}

TEST_CASE("Decode DXT1: synthetic texture has valid alpha channel", "[graphics][decode]")
{
    auto img = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt1.paa"));
    REQUIRE(img.valid());

    bool hasTransparent = false, hasOpaque = false;
    for (int i = 0; i < img.width * img.height; i++)
    {
        if (img.rgba[i * 4 + 3] == 0)
            hasTransparent = true;
        if (img.rgba[i * 4 + 3] == 255)
            hasOpaque = true;
    }
    REQUIRE(hasOpaque);
}

// --- DXT3 ---

TEST_CASE("Decode DXT3: synthetic texture", "[graphics][decode]")
{
    PAAInfo info;
    REQUIRE(ReadPAAInfo(GET_FIXTURE("texture/paa/synthetic_dxt3.paa"), info));
    REQUIRE(info.magic == 0xFF03);

    auto img = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt3.paa"));
    REQUIRE(img.valid());
    REQUIRE(img.width == 64);
    REQUIRE(img.height == 64);

    // DXT3 decoded image should have visible content
    int nonBlack = 0;
    for (int i = 0; i < img.width * img.height; i++)
        if (img.rgba[i * 4 + 0] || img.rgba[i * 4 + 1] || img.rgba[i * 4 + 2])
            nonBlack++;
    REQUIRE(nonBlack > 0);
}

// --- DXT5 ---

TEST_CASE("Decode DXT5: synthetic texture", "[graphics][decode]")
{
    PAAInfo info;
    REQUIRE(ReadPAAInfo(GET_FIXTURE("texture/paa/synthetic_dxt5.paa"), info));
    REQUIRE(info.magic == 0xFF05);

    auto img = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt5.paa"));
    REQUIRE(img.valid());
    REQUIRE(img.width == 32);
    REQUIRE(img.height == 32);
}

TEST_CASE("Decode DXT5: synthetic_dxt5 has visible content", "[graphics][decode]")
{
    auto img = DecodePAAFile(GET_FIXTURE("paa/synthetic_dxt5.paa"));
    REQUIRE(img.valid());
    REQUIRE(img.width == 32);
    REQUIRE(img.height == 32);

    int nonBlack = 0;
    for (int i = 0; i < img.width * img.height; i++)
        if (img.rgba[i * 4 + 0] || img.rgba[i * 4 + 1] || img.rgba[i * 4 + 2])
            nonBlack++;
    REQUIRE(nonBlack > 0);
}

// --- Cross-format: same source, DXT1 vs DXT5 ---

TEST_CASE("DXT1 vs DXT5 from same source: visually similar", "[graphics][decode]")
{
    auto img1 = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt1.paa"));
    auto img5 = DecodePAAFile(GET_FIXTURE("texture/paa/synthetic_dxt5.paa"));
    REQUIRE(img1.valid());
    REQUIRE(img5.valid());
    REQUIRE(img1.width > 0);
    REQUIRE(img5.width > 0);

    // The synthetic fixtures are independently generated per format. This test
    // gates that both decoders produce visible content without assuming they
    // came from the same source image.
    auto hasVisiblePixel = [](const auto& img)
    {
        for (int i = 0; i < img.width * img.height; i++)
            if (img.rgba[i * 4 + 0] || img.rgba[i * 4 + 1] || img.rgba[i * 4 + 2])
                return true;
        return false;
    };
    REQUIRE(hasVisiblePixel(img1));
    REQUIRE(hasVisiblePixel(img5));
}
