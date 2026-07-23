// Unit tests for the stb_image-based JPEG texture source.

#include <catch2/catch_test_macros.hpp>

#include "test_fixtures.hpp"

#include <Poseidon/Graphics/Textures/JpgImport.hpp>
#include <Poseidon/Graphics/Rendering/Font/Pactext.hpp>

#include <fstream>
#include <vector>
#include <stddef.h>
#include <stdint.h>
#include <string>

using Poseidon::GTextureSourceJPEGFactory;
using Poseidon::PacARGB1555;
using Poseidon::PacARGB8888;
using Poseidon::SelectTextureSourceFactory;
using Poseidon::TextureSourceJPEG;
using Poseidon::TextureSourceJPEGFactory;

namespace
{

constexpr int kMaxMips = 32;

std::vector<uint8_t> ReadFixtureFile(const char* path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    REQUIRE(in);
    auto size = in.tellg();
    REQUIRE(size > 0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);
    REQUIRE(in);
    return data;
}

} // namespace

TEST_CASE("JPG: decodes 32x32 checkerboard via Init()", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/checker_32x32.jpg"), mips, kMaxMips));

    // 32 = 2^5, so expect mips for 32,16,8,4 = 4 levels (floor(log2(32))+1 = 6,
    // minus the "<4px" floor = 4).
    REQUIRE(src.GetMipmapCount() == 4);
    REQUIRE(mips[0]._w == 32);
    REQUIRE(mips[0]._h == 32);
    REQUIRE(mips[1]._w == 16);
    REQUIRE(mips[2]._w == 8);
    REQUIRE(mips[3]._w == 4);

    // Source format matches the legacy IJL implementation.
    REQUIRE(src.GetFormat() == PacARGB1555);

    // Average of 50/50 red+green checker should be roughly (127, 127, 0).
    PackedColor avg = src.GetAverageColor();
    REQUIRE(avg.R8() >= 110);
    REQUIRE(avg.R8() <= 140);
    REQUIRE(avg.G8() >= 110);
    REQUIRE(avg.G8() <= 140);
    REQUIRE(avg.B8() <= 30);
}

TEST_CASE("JPG: Init fails on non-power-of-two dimensions", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE_FALSE(src.Init(GET_FIXTURE("jpg/nonpow2_33x33.jpg"), mips, kMaxMips));
}

TEST_CASE("JPG: Init fails on missing file", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    // GET_FIXTURE aborts on missing files, so assemble a "definitely-missing"
    // path alongside an existing fixture instead.
    std::string missing = GET_FIXTURE("jpg/checker_32x32.jpg");
    missing.replace(missing.size() - 17, 17, "surely_missing.jpg");
    REQUIRE_FALSE(src.Init(missing.c_str(), mips, kMaxMips));
}

TEST_CASE("JPG: generates full mip chain for 64x64", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/gradient_64x64.jpg"), mips, kMaxMips));

    REQUIRE(src.GetMipmapCount() == 5); // 64, 32, 16, 8, 4
    REQUIRE(mips[0]._w == 64);
    REQUIRE(mips[4]._w == 4);
}

TEST_CASE("JPG: flat_quadangular 128x64 mips halve both dimensions", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/gradient_128x64.jpg"), mips, kMaxMips));

    // max dim drives mip count; min dim still halves each step.
    REQUIRE(src.GetMipmapCount() >= 5);
    REQUIRE(mips[0]._w == 128);
    REQUIRE(mips[0]._h == 64);
    REQUIRE(mips[1]._w == 64);
    REQUIRE(mips[1]._h == 32);
    REQUIRE(mips[4]._w == 8);
    REQUIRE(mips[4]._h == 4);
}

TEST_CASE("JPG: GetMipmapData decodes to ARGB1555", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/checker_32x32.jpg"), mips, kMaxMips));

    // Set destination format so the decoder knows how to convert the payload.
    mips[0].SetDestFormat(PacARGB1555, 8);

    std::vector<uint16_t> buf(static_cast<size_t>(mips[0]._w) * mips[0]._h);
    REQUIRE(src.GetMipmapData(buf.data(), mips[0], 0));

    // Alpha bit must be set on every pixel (JPEG has no alpha → opaque).
    for (uint16_t pix : buf)
        REQUIRE((pix & 0x8000) != 0);
}

TEST_CASE("JPG: source metadata is opaque for custom faces", "[Graphics][JPG][CustomFace]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/checker_32x32.jpg"), mips, kMaxMips));

    REQUIRE_FALSE(src.IsAlpha());
    REQUIRE_FALSE(src.IsTransparent());
}

TEST_CASE("JPG: GetMipmapData decodes to ARGB8888", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/checker_32x32.jpg"), mips, kMaxMips));

    mips[0].SetDestFormat(PacARGB8888, 8);

    std::vector<uint8_t> buf(static_cast<size_t>(mips[0]._w) * mips[0]._h * 4);
    REQUIRE(src.GetMipmapData(buf.data(), mips[0], 0));

    // Every pixel opaque; checker cells red and green should both exist.
    bool sawRed = false, sawGreen = false;
    for (int i = 0; i < 32 * 32; ++i)
    {
        // BGRA layout
        uint8_t b = buf[i * 4 + 0];
        uint8_t g = buf[i * 4 + 1];
        uint8_t r = buf[i * 4 + 2];
        uint8_t a = buf[i * 4 + 3];
        REQUIRE(a == 0xFF);
        if (r > 200 && g < 50 && b < 50)
            sawRed = true;
        if (g > 200 && r < 50 && b < 50)
            sawGreen = true;
    }
    REQUIRE(sawRed);
    REQUIRE(sawGreen);
}

TEST_CASE("JPG: GetMipmapData rejects out-of-range mip level", "[Graphics][JPG]")
{
    TextureSourceJPEG src;
    PacLevelMem mips[kMaxMips];
    REQUIRE(src.Init(GET_FIXTURE("jpg/checker_32x32.jpg"), mips, kMaxMips));

    // 32x32 → 4 mips; asking for level 5 must fail cleanly rather than crash.
    mips[0].SetDestFormat(PacARGB1555, 8);
    std::vector<uint16_t> buf(4);
    REQUIRE_FALSE(src.GetMipmapData(buf.data(), mips[0], src.GetMipmapCount()));
}

TEST_CASE("JPG: InitFromMemory matches Init() on the same bytes", "[Graphics][JPG]")
{
    const char* path = GET_FIXTURE("jpg/gradient_64x64.jpg");
    auto bytes = ReadFixtureFile(path);

    TextureSourceJPEG a;
    PacLevelMem aMips[kMaxMips];
    REQUIRE(a.Init(path, aMips, kMaxMips));

    TextureSourceJPEG b;
    REQUIRE(b.InitFromMemory(bytes.data(), bytes.size(), "<mem>"));

    REQUIRE(a.GetMipmapCount() == b.GetMipmapCount());
    REQUIRE(a.GetFormat() == b.GetFormat());
}

TEST_CASE("JPG: factory Check accepts existing file, rejects missing", "[Graphics][JPG]")
{
    TextureSourceJPEGFactory factory;
    REQUIRE(factory.Check(GET_FIXTURE("jpg/checker_32x32.jpg")));
    std::string missing = GET_FIXTURE("jpg/checker_32x32.jpg");
    missing.replace(missing.size() - 17, 17, "surely_missing.jpg");
    REQUIRE_FALSE(factory.Check(missing.c_str()));
}

TEST_CASE("JPG: SelectTextureSourceFactory routes .jpg and .jpeg", "[Graphics][JPG]")
{
    // Route on extension alone — file need not exist here, we're only checking
    // the dispatcher.
    REQUIRE(SelectTextureSourceFactory("some.jpg") == GTextureSourceJPEGFactory);
    REQUIRE(SelectTextureSourceFactory("some.jpeg") == GTextureSourceJPEGFactory);
    REQUIRE(SelectTextureSourceFactory("some.pac") != GTextureSourceJPEGFactory);
}
