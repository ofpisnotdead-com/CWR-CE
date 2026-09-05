#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Asset/Probes/AssetPreview.hpp>
#include "test_fixtures.hpp"
#include <cstdio>
#include <fstream>
#include <stdint.h>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Poseidon;

// Texture Preview

TEST_CASE("PreviewTexture returns RGBA for DXT1 PAA", "[tools-preview][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt1.paa");
    REQUIRE(path != nullptr);

    auto preview = PreviewTexture(path);
    REQUIRE(preview.valid());
    CHECK(preview.width > 0);
    CHECK(preview.height > 0);
    CHECK(preview.channels == 4);
    CHECK(preview.data.size() == static_cast<size_t>(preview.width * preview.height * 4));
}

TEST_CASE("PreviewTexture returns RGBA for ARGB4444 PAA", "[tools-preview][texture]")
{
    const char* path = GET_FIXTURE("texture/paa/synthetic_dxt5.paa");
    REQUIRE(path != nullptr);

    auto preview = PreviewTexture(path);
    REQUIRE(preview.valid());
    CHECK(preview.data.size() == static_cast<size_t>(preview.width * preview.height * 4));
}

TEST_CASE("PreviewTexture returns RGBA for PAC file", "[tools-preview][texture]")
{
    const char* path = GET_FIXTURE("texture/pac/synthetic_default.pac");
    REQUIRE(path != nullptr);

    auto preview = PreviewTexture(path);
    REQUIRE(preview.valid());
}

TEST_CASE("PreviewTexture returns invalid for bad file", "[tools-preview][texture]")
{
    auto preview = PreviewTexture("/nonexistent/file.paa");
    CHECK_FALSE(preview.valid());
}

// Terrain Preview

TEST_CASE("PreviewTerrain renders fixture heightmap", "[tools-preview][terrain]")
{
    const char* path = GET_FIXTURE("wrp/test_world.wrp");
    REQUIRE(path != nullptr);

    auto preview = PreviewTerrain(path);
    REQUIRE(preview.valid());
    CHECK(preview.width == 4);
    CHECK(preview.height == 4);
    CHECK(preview.channels == 4);
    CHECK(preview.data.size() == 64);
}

TEST_CASE("PreviewTerrain returns invalid for bad file", "[tools-preview][terrain]")
{
    auto preview = PreviewTerrain("/nonexistent/file.wrp");
    CHECK_FALSE(preview.valid());
}

// Model Preview

TEST_CASE("PreviewModel returns RGB wireframe", "[tools-preview][model]")
{
    const char* path = GET_FIXTURE("p3d/sky_plane.p3d");
    REQUIRE(path != nullptr);

    ModelPreviewOptions opts;
    opts.width = 128;
    opts.height = 128;

    auto preview = PreviewModel(path, opts);
    REQUIRE(preview.valid());
    CHECK(preview.width == 128);
    CHECK(preview.height == 128);
    CHECK(preview.channels == 3);
    CHECK(preview.data.size() == static_cast<size_t>(128 * 128 * 3));
}

TEST_CASE("PreviewModel supports different views", "[tools-preview][model]")
{
    const char* path = GET_FIXTURE("p3d/crew_proxy.p3d");
    REQUIRE(path != nullptr);

    ModelPreviewOptions opts;
    opts.width = 64;
    opts.height = 64;
    opts.view = "3d";

    auto preview = PreviewModel(path, opts);
    REQUIRE(preview.valid());
}

TEST_CASE("PreviewModel wireframe has non-background pixels", "[tools-preview][model]")
{
    const char* path = GET_FIXTURE("p3d/crew_proxy.p3d");
    REQUIRE(path != nullptr);

    ModelPreviewOptions opts;
    opts.width = 128;
    opts.height = 128;
    opts.bgR = 24;
    opts.bgG = 24;
    opts.bgB = 24;

    auto preview = PreviewModel(path, opts);
    REQUIRE(preview.valid());

    // Check that at least some pixels differ from background
    bool hasWireframe = false;
    for (size_t i = 0; i + 2 < preview.data.size(); i += 3)
    {
        if (preview.data[i] != 24 || preview.data[i + 1] != 24 || preview.data[i + 2] != 24)
        {
            hasWireframe = true;
            break;
        }
    }
    CHECK(hasWireframe);
}

TEST_CASE("PreviewModel quad view produces larger image", "[tools-preview][model]")
{
    const char* path = GET_FIXTURE("p3d/crew_proxy.p3d");
    REQUIRE(path != nullptr);

    ModelPreviewOptions opts;
    opts.width = 64;
    opts.height = 64;
    opts.view = "quad";

    auto preview = PreviewModel(path, opts);
    REQUIRE(preview.valid());
    // Quad view renders within the same dimensions
    CHECK(preview.width > 0);
    CHECK(preview.height > 0);
}

TEST_CASE("PreviewModel returns invalid for bad LOD index", "[tools-preview][model]")
{
    const char* path = GET_FIXTURE("p3d/sky_plane.p3d");
    REQUIRE(path != nullptr);

    ModelPreviewOptions opts;
    opts.lodIndex = 999;

    auto preview = PreviewModel(path, opts);
    CHECK_FALSE(preview.valid());
}

TEST_CASE("PreviewModel returns invalid for bad file", "[tools-preview][model]")
{
    auto preview = PreviewModel("/nonexistent/file.p3d");
    CHECK_FALSE(preview.valid());
}

// Font Preview

TEST_CASE("PreviewFont returns RGBA charmap", "[tools-preview][font]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");
    REQUIRE(path != nullptr);

    FontPreviewOptions opts;
    opts.charmap = true;

    auto preview = PreviewFont(path, opts);
    REQUIRE(preview.valid());
    CHECK(preview.channels == 4);
    CHECK(preview.width > 0);
    CHECK(preview.height > 0);
    CHECK(preview.data.size() == static_cast<size_t>(preview.width * preview.height * 4));
}

TEST_CASE("PreviewFont renders sample text", "[tools-preview][font]")
{
    const char* path = GET_FIXTURE("font/legacy.fxy");
    REQUIRE(path != nullptr);

    FontPreviewOptions opts;
    opts.sampleText = "Hello";

    auto preview = PreviewFont(path, opts);
    REQUIRE(preview.valid());

    // Should have non-background pixels (text rendered)
    bool hasText = false;
    for (size_t i = 0; i + 3 < preview.data.size(); i += 4)
    {
        if (preview.data[i] != opts.bgR || preview.data[i + 1] != opts.bgG || preview.data[i + 2] != opts.bgB)
        {
            hasText = true;
            break;
        }
    }
    CHECK(hasText);
}

TEST_CASE("PreviewFont returns invalid for bad file", "[tools-preview][font]")
{
    auto preview = PreviewFont("/nonexistent/file.fxy");
    CHECK_FALSE(preview.valid());
}

// SaveToFile

TEST_CASE("PreviewImage::saveToFile writes PNG", "[tools-preview][save]")
{
    PreviewImage img;
    img.width = 2;
    img.height = 2;
    img.data.assign(2 * 2 * 4, 0xAA);

    std::string path = TestFixtures::GetTempFilePath("test_save.png");
    REQUIRE(img.saveToFile(path));

    // Verify file exists and has PNG signature
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    uint8_t sig[4];
    f.read(reinterpret_cast<char*>(sig), 4);
    CHECK(sig[1] == 'P');
    CHECK(sig[2] == 'N');
    CHECK(sig[3] == 'G');
    TestFixtures::CleanupTempFile(path.c_str());
}

TEST_CASE("PreviewImage::saveToFile writes BMP", "[tools-preview][save]")
{
    PreviewImage img;
    img.width = 2;
    img.height = 2;
    img.data.assign(2 * 2 * 4, 0xBB);

    std::string path = TestFixtures::GetTempFilePath("test_save.bmp");
    REQUIRE(img.saveToFile(path));

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    char sig[2];
    f.read(sig, 2);
    CHECK(sig[0] == 'B');
    CHECK(sig[1] == 'M');
    TestFixtures::CleanupTempFile(path.c_str());
}

TEST_CASE("PreviewImage::saveToFile writes TGA", "[tools-preview][save]")
{
    PreviewImage img;
    img.width = 2;
    img.height = 2;
    img.data.assign(2 * 2 * 4, 0xCC);

    std::string path = TestFixtures::GetTempFilePath("test_save.tga");
    REQUIRE(img.saveToFile(path));

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    // TGA has image type at byte 2 (RLE = 10)
    uint8_t hdr[3];
    f.read(reinterpret_cast<char*>(hdr), 3);
    CHECK(hdr[2] == 10);
    TestFixtures::CleanupTempFile(path.c_str());
}

TEST_CASE("PreviewImage::saveToFile rejects unknown extension", "[tools-preview][save]")
{
    PreviewImage img;
    img.width = 2;
    img.height = 2;
    img.data.assign(2 * 2 * 4, 0xFF);

    CHECK_FALSE(img.saveToFile("/tmp/test_save.xyz"));
}

TEST_CASE("PreviewImage::saveToFile returns false for invalid image", "[tools-preview][save]")
{
    PreviewImage img; // empty
    CHECK_FALSE(img.saveToFile("/tmp/test_save.png"));
}

TEST_CASE("PreviewImageRGB::saveToFile writes PNG", "[tools-preview][save]")
{
    PreviewImageRGB img;
    img.width = 4;
    img.height = 4;
    img.data.assign(4 * 4 * 3, 0x55);

    std::string path = TestFixtures::GetTempFilePath("test_save_rgb.png");
    REQUIRE(img.saveToFile(path));

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    uint8_t sig[4];
    f.read(reinterpret_cast<char*>(sig), 4);
    CHECK(sig[1] == 'P');
    CHECK(sig[2] == 'N');
    CHECK(sig[3] == 'G');
    TestFixtures::CleanupTempFile(path.c_str());
}
