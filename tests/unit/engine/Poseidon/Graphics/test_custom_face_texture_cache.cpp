#include <catch2/catch_test_macros.hpp>

#include "test_fixtures.hpp"

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::filesystem::path CustomFacePath()
{
    return std::filesystem::path(
        static_cast<const char*>(Poseidon::BuildNetworkPlayerAssetTmpPath(42, RString("face.jpg"))));
}

void InstallFixtureFace(const char* fixture)
{
    const std::filesystem::path face = CustomFacePath();
    std::filesystem::create_directories(face.parent_path());
    std::filesystem::copy_file(GET_FIXTURE(fixture), face, std::filesystem::copy_options::overwrite_existing);
}

struct TextureLoadingGuard
{
    bool previous;

    TextureLoadingGuard() : previous(Poseidon::NoTextures) { Poseidon::NoTextures = false; }
    ~TextureLoadingGuard() { Poseidon::NoTextures = previous; }
};

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    REQUIRE(in.is_open());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string ExtractFunctionBody(const std::string& source, const std::string& signature)
{
    const size_t signaturePos = source.find(signature);
    REQUIRE(signaturePos != std::string::npos);
    const size_t openBrace = source.find('{', signaturePos);
    REQUIRE(openBrace != std::string::npos);

    int depth = 0;
    for (size_t pos = openBrace; pos < source.size(); ++pos)
    {
        if (source[pos] == '{')
        {
            ++depth;
        }
        else if (source[pos] == '}')
        {
            --depth;
            if (depth == 0)
            {
                return source.substr(openBrace + 1, pos - openBrace - 1);
            }
        }
    }
    FAIL("Function body not closed: " << signature);
    return {};
}

} // namespace

TEST_CASE("Texture bank flush drops unreferenced custom face cache entries", "[Graphics][CustomFace]")
{
    TextureLoadingGuard textureLoading;
    const std::filesystem::path face = CustomFacePath();
    std::filesystem::remove_all(face.parent_path());

    InstallFixtureFace("jpg/checker_32x32.jpg");
    PackedColor firstColor;
    {
        Ref<Texture> first = Poseidon::GlobLoadTexture(face.string().c_str());
        REQUIRE(first);
        firstColor = PackedColor(first->GetColor());
    }

    REQUIRE(Poseidon::GEngine);
    const int beforeFlush = Poseidon::GEngine->TextBank()->NTextures();
    REQUIRE(beforeFlush > 0);
    Poseidon::GEngine->TextBank()->FlushTextures();
    CHECK(Poseidon::GEngine->TextBank()->NTextures() < beforeFlush);

    InstallFixtureFace("jpg/gradient_64x64.jpg");
    Ref<Texture> second = Poseidon::GlobLoadTexture(face.string().c_str());
    REQUIRE(second);
    const PackedColor secondColor(second->GetColor());

    const bool colorChanged = secondColor.R8() != firstColor.R8() || secondColor.G8() != firstColor.G8() ||
                              secondColor.B8() != firstColor.B8();
    CHECK(colorChanged);

    std::filesystem::remove_all(face.parent_path());
}

TEST_CASE("GL33 texture flush keeps world cleanup semantics", "[Graphics][CustomFace][GL33]")
{
    const std::filesystem::path source =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "TextureBankGL33_Cache.cpp";
    const std::string body = ExtractFunctionBody(ReadTextFile(source), "void TextBankGL33::FlushTextures()");

    CHECK(body.find("Compact();") != std::string::npos);
    CHECK(body.find("DeleteLastReleased();") != std::string::npos);
    CHECK(body.find("No-op") == std::string::npos);
}

TEST_CASE("Copied network unit info preserves player id before custom face assignment", "[Network][CustomFace]")
{
    const std::filesystem::path source = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" /
                                         "Network" / "NetworkClientOnMessage.cpp";
    const std::string body = ExtractFunctionBody(ReadTextFile(source), "void NetworkClient::OnMessage");
    const size_t copyCase = body.find("case NMTCopyUnitInfo:");
    REQUIRE(copyCase != std::string::npos);
    const size_t remotePlayer = body.find("SetRemotePlayer(copy._from->GetRemotePlayer())", copyCase);
    const size_t setFace = body.find("SetFace(info._face, info._name)", copyCase);

    REQUIRE(remotePlayer != std::string::npos);
    REQUIRE(setFace != std::string::npos);
    CHECK(remotePlayer < setFace);
}
