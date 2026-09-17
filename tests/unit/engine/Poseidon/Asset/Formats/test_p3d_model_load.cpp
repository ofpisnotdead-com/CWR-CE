#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Asset/Formats/P3D/P3DModelLoad.hpp>
#include "test_fixtures.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace
{

// Removes the file even when an assertion aborts the case.
class TempModelFile
{
  public:
    explicit TempModelFile(const char* name) : _path(GET_TEMP_FILE(name)) {}
    ~TempModelFile() { TestFixtures::CleanupTempFile(_path.c_str()); }

    TempModelFile(const TempModelFile&) = delete;
    TempModelFile& operator=(const TempModelFile&) = delete;

    const std::string& Path() const { return _path; }

  private:
    std::string _path;
};

void Put32(std::vector<char>& out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// A model that passes format detection - MLOD 1.1 with one SP3X level - but carries
// rubbish where the LOD's TAGG section belongs, which is how the readers are made to
// throw mid-parse.
std::string WriteTruncatedMlod(const std::string& path)
{
    std::vector<char> bytes;
    const char magic[4] = {'M', 'L', 'O', 'D'};
    bytes.insert(bytes.end(), magic, magic + 4);
    Put32(bytes, 0x101);
    Put32(bytes, 1);
    const char lodMagic[4] = {'S', 'P', '3', 'X'};
    bytes.insert(bytes.end(), lodMagic, lodMagic + 4);
    Put32(bytes, 0x1C);
    Put32(bytes, 0);
    Put32(bytes, 0);
    Put32(bytes, 0);
    Put32(bytes, 0);
    for (int i = 0; i < 64; i++)
    {
        bytes.push_back(static_cast<char>(0xCD));
    }

    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return path;
}

} // namespace

TEST_CASE("TryLoadP3D reports a malformed model instead of throwing", "[asset][p3d]")
{
    const TempModelFile temp("truncated_mlod.p3d");
    const std::string path = WriteTruncatedMlod(temp.Path());

    Poseidon::Model::Model model;
    Poseidon::Asset::Formats::FormatInfo format;
    std::string error;
    bool loaded = true;
    REQUIRE_NOTHROW(loaded = Poseidon::Asset::Formats::TryLoadP3D(path, model, format, error));
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("TryLoadP3D reports a missing model instead of throwing", "[asset][p3d]")
{
    Poseidon::Model::Model model;
    Poseidon::Asset::Formats::FormatInfo format;
    std::string error;
    bool loaded = true;
    REQUIRE_NOTHROW(loaded = Poseidon::Asset::Formats::TryLoadP3D("no_such_model.p3d", model, format, error));
    REQUIRE_FALSE(loaded);
}
