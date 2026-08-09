#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <Poseidon/Core/ModArchive.hpp>
#include "test_fixtures.hpp"

#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <catch2/catch_message.hpp>
#include <system_error>
#include <vector>

using namespace Poseidon;

namespace
{
// Per-process-unique temp dir so parallel ctest workers never collide.
std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() /
               ("cwr_modarchive_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string SlashNormalize(std::string s)
{
    for (char& c : s)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return s;
}

// Deterministic semi-compressible bytes: a few long runs interleaved with an LCG-driven
// pseudo-random stream, so the data is bigger than one zstd in/out buffer in *both*
// compressed and decompressed form — forcing DecompressArchive's outer fread loop and inner
// decompressStream loop to iterate many times.
std::vector<uint8_t> MakePayload(size_t bytes)
{
    std::vector<uint8_t> out;
    out.reserve(bytes);
    uint32_t state = 0x1234abcdu;
    while (out.size() < bytes)
    {
        state = state * 1664525u + 1013904223u;
        if ((state & 0xff) < 0x30)
        {
            out.insert(out.end(), 257, static_cast<uint8_t>(state >> 16)); // run -> compressible
        }
        else
        {
            out.push_back(static_cast<uint8_t>(state >> 24)); // noise -> incompressible
        }
    }
    out.resize(bytes);
    return out;
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path)
{
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.string().c_str(), "rb");
    if (f == nullptr)
    {
        return data;
    }
    uint8_t buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        data.insert(data.end(), buf, buf + n);
    }
    std::fclose(f);
    return data;
}

void WriteFileBytes(const std::filesystem::path& path, const uint8_t* data, size_t size)
{
    FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    if (size > 0)
    {
        REQUIRE(std::fwrite(data, 1, size, f) == size);
    }
    std::fclose(f);
}
} // namespace

TEST_CASE("ModArchive rejects paths outside the staging directory", "[mods][pbo][paths]")
{
    CHECK(ModArchive::IsSafeEntryPath("addons/data.bin"));
    CHECK(ModArchive::IsSafeEntryPath("addons\\data.bin"));
    CHECK_FALSE(ModArchive::IsSafeEntryPath("../outside.bin"));
    CHECK_FALSE(ModArchive::IsSafeEntryPath("addons/../../outside.bin"));
    CHECK_FALSE(ModArchive::IsSafeEntryPath("/absolute.bin"));
    CHECK_FALSE(ModArchive::IsSafeEntryPath("C:\\absolute.bin"));
}

TEST_CASE("ModArchive::Unpack extracts every PBO entry to disk", "[mods][pbo]")
{
    // Mods ship as zstd-wrapped PBOs (`<name>.pbo.zst`) — the only format
    // ModArchive accepts. addon_fixture.pbo.zst is `zstd -19 addon_fixture.pbo`; the expected
    // entry list still comes from inspecting the raw PBO.
    // GET_FIXTURE returns a pointer into a shared static buffer, so copy the
    // first path out before resolving the second.
    const std::string archive = GET_FIXTURE("pbo/addon_fixture.pbo.zst");
    REQUIRE_FALSE(archive.empty());
    const char* pbo = GET_FIXTURE("pbo/addon_fixture.pbo");
    REQUIRE(pbo != nullptr);

    const PboInfo info = InspectPbo(pbo);
    REQUIRE(info.valid);
    REQUIRE_FALSE(info.entries.empty());

    const std::filesystem::path dest = MakeTempDir();

    std::string error;
    const bool ok = ModArchive::Unpack(archive.c_str(), dest.string().c_str(), &error);
    INFO("unpack error: " << error);
    REQUIRE(ok);

    // Every catalogued entry must land on disk at its (slash-normalized) path,
    // sized to its *uncompressed* length — the bank reader decompresses on read,
    // so a broken decompress or short write trips the size check.
    for (const auto& entry : info.entries)
    {
        const std::filesystem::path path = dest / SlashNormalize(entry.name);
        INFO("entry: " << entry.name);
        REQUIRE(std::filesystem::exists(path));

        const long expected = entry.compressed ? entry.uncompressedSize : entry.length;
        CHECK(static_cast<long>(std::filesystem::file_size(path)) == expected);
    }

    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
}

TEST_CASE("ModArchive::Unpack fails cleanly on a missing archive", "[mods][pbo]")
{
    const std::filesystem::path dest = MakeTempDir();

    std::string error;
    const bool ok =
        ModArchive::Unpack((dest / "does_not_exist.pbo.zst").string().c_str(), dest.string().c_str(), &error);
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
}

TEST_CASE("ModArchive::Unpack rejects a non-zstd archive", "[mods][pbo]")
{
    // A raw PBO is no longer a valid input — only `.pbo.zst` is. Feeding the
    // uncompressed fixture must fail at the zstd decode, not silently unpack.
    const char* rawPbo = GET_FIXTURE("pbo/addon_fixture.pbo");
    REQUIRE(rawPbo != nullptr);

    const std::filesystem::path dest = MakeTempDir();

    std::string error;
    const bool ok = ModArchive::Unpack(rawPbo, dest.string().c_str(), &error);
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
}

TEST_CASE("ModArchive::Unpack rejects a valid archive wrapping a corrupt PBO", "[mods][pbo]")
{
    // The realistic "bad / unsupported Workshop package" case behind the mod-browser
    // re-mount crash report: the zstd frame decodes fine but the payload is not a valid
    // PBO. Unpack must report a clean error (InspectPbo rejects it), not crash or leave a
    // half-mod for the re-mount to choke on. Broken-state delta: a missing valid/error
    // check here lets garbage reach the bank loader.
    const std::filesystem::path dir = MakeTempDir();
    const std::filesystem::path zst = dir / "corrupt.pbo.zst";
    const std::filesystem::path dest = dir / "out";

    // Deterministic non-PBO bytes, validly zstd-wrapped.
    const std::vector<uint8_t> garbage = MakePayload(64 * 1024);
    std::vector<uint8_t> compressed(ZSTD_compressBound(garbage.size()));
    const size_t csz = ZSTD_compress(compressed.data(), compressed.size(), garbage.data(), garbage.size(), 19);
    REQUIRE_FALSE(ZSTD_isError(csz));
    WriteFileBytes(zst, compressed.data(), csz);

    std::string error;
    const bool ok = ModArchive::Unpack(zst.string().c_str(), dest.string().c_str(), &error);
    INFO("unpack error: " << error);
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("ModArchive::DecompressArchive streams a multi-chunk frame byte-exactly", "[mods][pbo][zstd]")
{
    // The committed addon_fixture.pbo.zst is <1 KB — a single pass through both decode loops. This
    // payload is several MB and several zstd in/out buffers wide, so the outer fread loop and
    // the inner decompressStream loop must each iterate many times. Broken-state delta: if the
    // inner loop only drained the first output buffer, the decoded file would be truncated to
    // ZSTD_DStreamOutSize (~128 KB) and the byte-compare below would fail.
    const std::filesystem::path dir = MakeTempDir();
    const std::filesystem::path zst = dir / "payload.pbo.zst";
    const std::filesystem::path out = dir / "payload.pbo";

    const std::vector<uint8_t> payload = MakePayload(5 * 1024 * 1024);
    REQUIRE(payload.size() > 4 * ZSTD_DStreamOutSize()); // guarantees multi-chunk decode

    std::vector<uint8_t> compressed(ZSTD_compressBound(payload.size()));
    const size_t csz = ZSTD_compress(compressed.data(), compressed.size(), payload.data(), payload.size(), 19);
    REQUIRE_FALSE(ZSTD_isError(csz));
    REQUIRE(csz > ZSTD_DStreamInSize()); // guarantees multi-chunk read
    WriteFileBytes(zst, compressed.data(), csz);

    std::string error;
    const bool ok = ModArchive::DecompressArchive(zst.string().c_str(), out.string().c_str(), &error);
    INFO("decode error: " << error);
    REQUIRE(ok);
    CHECK(ReadFileBytes(out) == payload);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("ModArchive::DecompressArchive rejects a truncated frame", "[mods][pbo][zstd]")
{
    // Half a valid frame must be rejected, not silently produce a partial PBO. Broken-state
    // delta: without the `lastRet != 0` end-of-frame check the decode returns success on a
    // truncated archive and a corrupt half-mod lands on disk.
    const std::filesystem::path dir = MakeTempDir();
    const std::filesystem::path zst = dir / "partial.pbo.zst";
    const std::filesystem::path out = dir / "partial.pbo";

    const std::vector<uint8_t> payload = MakePayload(2 * 1024 * 1024);
    std::vector<uint8_t> compressed(ZSTD_compressBound(payload.size()));
    const size_t csz = ZSTD_compress(compressed.data(), compressed.size(), payload.data(), payload.size(), 19);
    REQUIRE_FALSE(ZSTD_isError(csz));
    WriteFileBytes(zst, compressed.data(), csz / 2); // keep only the first half of the frame

    std::string error;
    const bool ok = ModArchive::DecompressArchive(zst.string().c_str(), out.string().c_str(), &error);
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("ModArchive::DecompressArchive fails cleanly on a missing source", "[mods][pbo][zstd]")
{
    const std::filesystem::path dir = MakeTempDir();
    std::string error;
    const bool ok = ModArchive::DecompressArchive((dir / "nope.pbo.zst").string().c_str(),
                                                  (dir / "out.pbo").string().c_str(), &error);
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
