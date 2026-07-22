#include <catch2/catch_test_macros.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/World/Terrain/WrpReader.hpp>
#include "test_fixtures.hpp"
#include <stdio.h>
#include <catch2/catch_message.hpp>
#include <string>
#include <vector>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;

#ifdef GetObject
#undef GetObject
#endif

// OPRW magic used by serialized landscape binaries
#ifdef _MSC_VER
static const int OPRW_MAGIC = 'WRPO';
#else
static const int OPRW_MAGIC = StrToInt("OPRW");
#endif

TEST_CASE("WRP: synthetic placeholder is rejected as non-OPRW", "[Formats][WRP]")
{
    const char* path = GET_FIXTURE("wrp/test_world.wrp");

    QIFStream file;
    file.open(path);
    REQUIRE(!file.fail());

    int magic = 0;
    file.read(&magic, sizeof(magic));
    REQUIRE(!file.fail());
    REQUIRE(magic == OPRW_MAGIC);
}

TEST_CASE("WRP: WrpReader rejects synthetic placeholder terrain", "[Formats][WRP]")
{
    const char* path = GET_FIXTURE("wrp/test_world.wrp");

    WrpReader reader;
    REQUIRE_FALSE(reader.Load(path));
    REQUIRE(reader.GetError() != nullptr);
}

TEST_CASE("WRP: WrpReader loads large OPRW with objects", "[Formats][WRP][GameData]")
{
    // Game data lives under GAME_DATA_DIR/Worlds/.
    char abelPath[MAX_PATH];
    snprintf(abelPath, sizeof(abelPath), "%s/Worlds/abel.wrp", GAME_DATA_DIR);

    QIFStream probe;
    probe.open(abelPath);
    if (probe.fail())
    {
        SKIP("Game world abel.wrp not available");
        return;
    }

    WrpReader reader;
    REQUIRE(reader.Load(abelPath));
    REQUIRE((reader.GetFormat() == WrpReader::OPRW_V2 || reader.GetFormat() == WrpReader::OPRW_V3));
    REQUIRE(reader.GetObjectCount() > 0);
    REQUIRE(reader.GetObjectNameCount() > 0);
    REQUIRE(reader.GetTextureCount() > 0);

    // Verify first object has valid data
    const auto& obj = reader.GetObject(0);
    REQUIRE(obj.name.GetLength() > 0);
    REQUIRE(obj.hasMatrix == true);
}

TEST_CASE("WRP: WrpReader reports error for missing file", "[Formats][WRP]")
{
    WrpReader reader;
    REQUIRE_FALSE(reader.Load("nonexistent.wrp"));
    REQUIRE(reader.GetError() != nullptr);
}

TEST_CASE("WRP: WrpReader reports error for invalid data", "[Formats][WRP]")
{
    // Create a local temp file with invalid magic. Keep this relative so it
    // works under Windows ctest runs without relying on /tmp.
    const char* tmpPath = "test_invalid.wrp";
    {
        QOFStream f;
        f.open(tmpPath);
        int badMagic = 0xDEADBEEF;
        f.write(&badMagic, sizeof(badMagic));
        f.close();
    }

    WrpReader reader;
    REQUIRE_FALSE(reader.Load(tmpPath));
    REQUIRE(std::string(reader.GetError()) == "Unknown file format");

    remove(tmpPath);
}

static std::vector<char> BuildRvw4Blob()
{
    std::vector<char> blob;
    auto put = [&blob](const void* p, size_t n)
    {
        const char* c = static_cast<const char*>(p);
        blob.insert(blob.end(), c, c + n);
    };

    put("4WVR", 4);
    int range = 4;
    put(&range, 4); // xRange
    put(&range, 4); // zRange

    short cell = 0;
    for (int i = 0; i < 4 * 4; i++)
        put(&cell, sizeof(cell)); // heightmap
    for (int i = 0; i < 4 * 4; i++)
        put(&cell, sizeof(cell)); // texture indices

    char texName[32] = "landtext\mo.pac";
    put(texName, sizeof(texName));
    char emptyTex[32] = {};
    for (int i = 1; i < 512; i++)
        put(emptyTex, sizeof(emptyTex));

    for (int id = 0; id < 2; id++)
    {
        float matrix[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 100.0f + id, 0, 200.0f};
        put(matrix, sizeof(matrix));
        put(&id, sizeof(id));
        char name[76] = {};
        snprintf(name, sizeof(name), "data3d\dummy%d.p3d", id);
        put(name, sizeof(name));
    }
    return blob;
}

TEST_CASE("WRP: RVW re-parse after a probe-to-EOF and seekg sees all objects", "[Formats][WRP]")
{
    std::vector<char> blob = BuildRvw4Blob();

    QIStream s(blob.data(), (int)blob.size());

    WrpReader probe;
    REQUIRE(probe.Load(s));
    REQUIRE(probe.GetFormat() == WrpReader::RVW_V4);
    REQUIRE(probe.GetObjectCount() == 2);

    s.seekg(0, QIOS::beg);
    REQUIRE_FALSE(s.fail());
    REQUIRE_FALSE(s.eof());

    WrpReader fallback;
    REQUIRE(fallback.Load(s));
    REQUIRE(fallback.GetFormat() == WrpReader::RVW_V4);
    REQUIRE(fallback.GetObjectCount() == 2);
    REQUIRE(std::string((const char*)fallback.GetObject(0).name) == "data3d\dummy0.p3d");
}
