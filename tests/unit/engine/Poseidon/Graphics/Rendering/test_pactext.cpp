#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Font/Pactext.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cstring>
#include <vector>

TEST_CASE("pactext.hpp compiles", "[rendering][font]")
{
    SUCCEED("header included successfully");
}

// SelectTextureSourceFactory is the texture-load entry the briefing equipment
// screen hits per gear slot.  An empty optional slot resolves to a directory-
// only path ("dtaext\equip\") with no filename — the original engine skipped it
// silently.  Without the guard the selector logs an "Unrecognized texture type"
// ERROR for every empty slot, which strict mode turns fatal (exit 3) on the
// briefing / credits screens.
//
// Broken-state delta: without the empty-filename guard, the "dtaext\equip\"
// case bumps GetErrorCount() to 1 (the LOG_ERROR fires); with it, count stays 0.
TEST_CASE("SelectTextureSourceFactory: empty-filename path is no-texture, not an error", "[rendering][font]")
{
    using LS = Poseidon::Foundation::LoggingSystem;
    LS logSys;
    logSys.Initialize("trace"); // attaches the ErrorCountingSink to category loggers
    LS::SetStrictMode(false);   // count errors without latching the strict trip

    SECTION("directory-only path (empty equipment slot) is silent")
    {
        LS::ResetErrorCount();
        auto* f = Poseidon::SelectTextureSourceFactory("dtaext\\equip\\");
        CHECK(f == nullptr);
        CHECK(LS::GetErrorCount() == 0); // the fix: no ERROR for a fileless path
    }

    SECTION("a real filename with an unknown extension still errors")
    {
        LS::ResetErrorCount();
        auto* f = Poseidon::SelectTextureSourceFactory("equip\\w\\w_bad.xyz");
        CHECK(f == nullptr);
        CHECK(LS::GetErrorCount() >= 1); // genuinely unrecognized → still reported
    }

    SECTION("a .paa name resolves to the PAC factory")
    {
        LS::ResetErrorCount();
        auto* f = Poseidon::SelectTextureSourceFactory("equip\\w\\w_m16.paa");
        CHECK(f != nullptr);
        CHECK(LS::GetErrorCount() == 0);
    }

    SECTION("empty and null names are silent")
    {
        LS::ResetErrorCount();
        CHECK(Poseidon::SelectTextureSourceFactory("") == nullptr);
        CHECK(Poseidon::SelectTextureSourceFactory(nullptr) == nullptr);
        CHECK(LS::GetErrorCount() == 0);
    }

    LS::ResetErrorCount();
}

// A PAA 16b mipmap is an LZSS stream followed by a 32b checksum over the decoded bytes,
// each summed as a signed char.
TEST_CASE("PacLevelMem::LoadPaa accepts the signed LZW checksum", "[rendering][font]")
{
    const unsigned char pixels[8] = {0x80, 0xff, 0x7f, 0x01, 0x90, 0x00, 0xc0, 0x40};
    const int checksum = -113;

    std::vector<unsigned char> file;
    auto putWord = [&file](int v)
    {
        file.push_back(v & 0xff);
        file.push_back((v >> 8) & 0xff);
    };

    // mipmap header: 16b width, 16b height, 24b compressed size
    putWord(2);
    putWord(2);
    file.push_back(static_cast<unsigned char>(sizeof(pixels)));
    file.push_back(0);
    file.push_back(0);

    file.push_back(0xff); // LZSS flag byte: the next eight tokens are literals
    for (unsigned char b : pixels)
    {
        file.push_back(b);
    }
    for (int i = 0; i < 4; i++)
    {
        file.push_back((checksum >> (i * 8)) & 0xff);
    }

    Poseidon::PacLevelMem mip;
    mip._w = 2;
    mip._h = 2;
    mip._sFormat = Poseidon::PacARGB4444;
    mip.SetDestFormat(Poseidon::PacARGB4444, 1);

    Poseidon::PacPalette palette;
    Poseidon::QIStream in(file.data(), static_cast<int>(file.size()));

    unsigned char decoded[sizeof(pixels)] = {};
    REQUIRE(mip.LoadPaa(in, decoded, &palette) == 0);
    REQUIRE(std::memcmp(decoded, pixels, sizeof(pixels)) == 0);
}
