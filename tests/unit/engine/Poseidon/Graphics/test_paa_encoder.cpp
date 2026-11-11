#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Textures/PAAEncoder.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Graphics/Textures/Image.hpp>
#include "test_fixtures.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdint.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace Poseidon;

// --- Helper: PSNR ---

static double computePSNR(const uint8_t* a, const uint8_t* b, int n)
{
    double mse = 0;
    for (int i = 0; i < n; ++i)
    {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        mse += d * d;
    }
    mse /= n;
    if (mse == 0.0)
        return 100.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// --- Helper: gradient image ---

static Image makeGradient(int w, int h)
{
    std::vector<uint8_t> rgba(w * h * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int i = y * w + x;
            rgba[i * 4 + 0] = static_cast<uint8_t>(x * 255 / (std::max)(w - 1, 1));
            rgba[i * 4 + 1] = static_cast<uint8_t>(y * 255 / (std::max)(h - 1, 1));
            rgba[i * 4 + 2] = 128;
            rgba[i * 4 + 3] = 255;
        }
    }
    return Image::FromRGBA(w, h, std::move(rgba));
}

// RAII temp file
struct TempFile
{
    std::string path;
    TempFile(const char* name) : path(TestFixtures::GetTempFilePath(name)) {}
    ~TempFile() { TestFixtures::CleanupTempFile(path.c_str()); }
};

// --- Write DXT1 PAA, read back ---

TEST_CASE("PAAEncoder: write DXT1 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("dxt1.paa");
    auto img = makeGradient(32, 32);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::DXT1));

    // Read back with PAADecoder
    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 32);
    REQUIRE(info.height == 32);
    REQUIRE(std::string(info.formatName) == "DXT1");
    REQUIRE(info.mipmapCount >= 1);

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width == 32);
    REQUIRE(decoded.height == 32);
}

// --- Write DXT5 PAA, read back ---

TEST_CASE("PAAEncoder: write DXT5 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("dxt5.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::DXT5));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(std::string(info.formatName) == "DXT5");

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
}

// --- Write ARGB4444 PAA, read back ---

TEST_CASE("PAAEncoder: write ARGB4444 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("argb4444.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::ARGB4444));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(std::string(info.formatName) == "ARGB4444");
}

// --- Write ARGB1555 PAA, read back ---

TEST_CASE("PAAEncoder: write ARGB1555 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("argb1555.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::ARGB1555));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(std::string(info.formatName) == "ARGB1555");
}

// --- Write AI88 PAA, read back ---

TEST_CASE("PAAEncoder: write AI88 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("ai88.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::AI88));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(std::string(info.formatName) == "AI88");
}

// --- Write ARGB8888 PAA (0x8888), read back ---

TEST_CASE("PAAEncoder: write ARGB8888 PAA and read back", "[Graphics][PAAEncoder]")
{
    TempFile tmp("argb8888.paa");
    auto img = makeGradient(32, 32);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::ARGB8888));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == 32);
    REQUIRE(info.height == 32);
    REQUIRE(std::string(info.formatName) == "ARGB8888");

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width == 32);
    REQUIRE(decoded.height == 32);
}

TEST_CASE("PAAEncoder: ARGB8888 pixel round-trip preserves data", "[Graphics][PAAEncoder]")
{
    TempFile tmp("argb8888_rt.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::ARGB8888));

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());

    double psnr = computePSNR(img.data().data(), decoded.rgba.data(), 16 * 16 * 4);
    REQUIRE(psnr > 40.0); // ARGB8888 is lossless (no quantization)
}

// --- Mipmaps are generated ---

TEST_CASE("PAAEncoder: generates multiple mipmap levels", "[Graphics][PAAEncoder]")
{
    TempFile tmp("mipmaps.paa");
    auto img = makeGradient(64, 64);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::DXT1));

    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.mipmapCount >= 4); // 64->32->16->8->4->2->1
}

// --- Round-trip pixel comparison (DXT1) ---

TEST_CASE("PAAEncoder: DXT1 round-trip PSNR", "[Graphics][PAAEncoder]")
{
    TempFile tmp("roundtrip_dxt1.paa");
    auto img = makeGradient(32, 32);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::DXT1));

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width == 32);
    REQUIRE(decoded.height == 32);

    double psnr = computePSNR(img.data().data(), decoded.rgba.data(), 32 * 32 * 4);
    REQUIRE(psnr > 25.0); // DXT1 single compression
}

// --- Round-trip pixel comparison (DXT5) ---

TEST_CASE("PAAEncoder: DXT5 round-trip PSNR", "[Graphics][PAAEncoder]")
{
    TempFile tmp("roundtrip_dxt5.paa");
    auto img = makeGradient(32, 32);

    REQUIRE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::DXT5));

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());

    double psnr = computePSNR(img.data().data(), decoded.rgba.data(), 32 * 32 * 4);
    REQUIRE(psnr > 25.0);
}

// --- Image::Save to PAA ---

TEST_CASE("Image: Save to .paa", "[Graphics][Image][PAAEncoder]")
{
    TempFile tmp("image_save.paa");
    auto img = makeGradient(16, 16);

    REQUIRE(img.Save(tmp.path));

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width == 16);
    REQUIRE(decoded.height == 16);
}

// --- Invalid inputs ---

TEST_CASE("PAAEncoder: rejects invalid image", "[Graphics][PAAEncoder]")
{
    TempFile tmp("invalid.paa");
    Image empty;
    REQUIRE_FALSE(PAAEncoder::WritePAA(tmp.path, empty, PixelFormat::DXT1));
}

TEST_CASE("PAAEncoder: rejects unsupported format (RGBA8888 has no PAA magic)", "[Graphics][PAAEncoder]")
{
    TempFile tmp("rgba.paa");
    auto img = makeGradient(16, 16);
    // RGBA8888 has paaMagic=0, not valid as PAA pixel format
    REQUIRE_FALSE(PAAEncoder::WritePAA(tmp.path, img, PixelFormat::RGBA8888));
}

// --- Real fixture round-trip ---

TEST_CASE("PAAEncoder: re-encode existing PAA fixture", "[Graphics][PAAEncoder]")
{
    TempFile tmp("reencode.paa");

    auto original = Image::FromFile(GET_FIXTURE("texture/paa/synthetic_dxt1.paa"));
    REQUIRE(original.valid());

    // Re-encode as DXT5
    REQUIRE(PAAEncoder::WritePAA(tmp.path, original, PixelFormat::DXT5));

    // Read back
    PAAInfo info;
    REQUIRE(ReadPAAInfo(tmp.path, info));
    REQUIRE(info.width == original.width());
    REQUIRE(info.height == original.height());
    REQUIRE(std::string(info.formatName) == "DXT5");

    auto decoded = DecodePAAFile(tmp.path);
    REQUIRE(decoded.valid());
}
