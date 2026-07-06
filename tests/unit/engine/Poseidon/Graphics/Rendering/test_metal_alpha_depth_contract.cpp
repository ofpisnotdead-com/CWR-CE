#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return {};

    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}
} // namespace

TEST_CASE("Metal cutouts preserve coverage without transparent depth writes", "[Graphics][Metal][Alpha][Depth]")
{
    const std::filesystem::path repoRoot = std::filesystem::path(TESTS_ROOT_DIR).parent_path();
    const std::string metal = ReadTextFile(repoRoot / "engine" / "PoseidonMTL" / "EngineMTL.cpp");
    REQUIRE_FALSE(metal.empty());

    const size_t prepare = metal.find("void EngineMTL::PrepareTriangleTL");
    REQUIRE(prepare != std::string::npos);
    const size_t prepareEnd = metal.find("void EngineMTL::PrepareMeshTL", prepare);
    REQUIRE(prepareEnd != std::string::npos);
    const std::string prepareRegion = metal.substr(prepare, prepareEnd - prepare);

    REQUIRE(prepareRegion.find("_tlSectionDepthMode = d.depth;") != std::string::npos);
    REQUIRE(prepareRegion.find("isBlend ? render::BlendMode::AlphaBlend : render::BlendMode::Opaque") !=
            std::string::npos);

    const size_t legacyPrepare = metal.find("void EngineMTL::PrepareTriangle(");
    REQUIRE(legacyPrepare != std::string::npos);
    REQUIRE(legacyPrepare < prepare);
    const std::string legacyPrepareRegion = metal.substr(legacyPrepare, prepare - legacyPrepare);
    REQUIRE(legacyPrepareRegion.find("if (d.blend == render::BlendMode::AlphaBlend)") != std::string::npos);
    REQUIRE(legacyPrepareRegion.find("_currentTriDepthMode = render::DepthMode::ReadOnly;") != std::string::npos);
    REQUIRE(legacyPrepareRegion.find("mip._texture->IsTransparent()") != std::string::npos);
    REQUIRE(legacyPrepareRegion.find("!_legacyMeshUiOverlay && mip.IsOK()") != std::string::npos);
    REQUIRE(legacyPrepareRegion.find("_currentTriAlphaRef = 254;") != std::string::npos);

    const std::string bootstrap = ReadTextFile(repoRoot / "engine" / "PoseidonMTL" / "EngineMTLBootstrap.cpp");
    REQUIRE_FALSE(bootstrap.empty());
    const size_t blendShader = bootstrap.find("fragment float4 fsMeshBlend");
    REQUIRE(blendShader != std::string::npos);
    const size_t shadowShader = bootstrap.find("vertex VSOutMesh vsShadow", blendShader);
    REQUIRE(shadowShader != std::string::npos);
    const std::string blendRegion = bootstrap.substr(blendShader, shadowShader - blendShader);
    REQUIRE(blendRegion.find("if (in.color.a * texColor.a < (18.0 / 255.0))") != std::string::npos);
    REQUIRE(blendRegion.find("discard_fragment();") != std::string::npos);

    const size_t cutoutShader = bootstrap.find("fragment float4 fsMeshOpaque");
    REQUIRE(cutoutShader != std::string::npos);
    REQUIRE(cutoutShader < blendShader);
    const std::string cutoutRegion = bootstrap.substr(cutoutShader, blendShader - cutoutShader);
    REQUIRE(bootstrap.find("constant float kSolidCutoutCoverage = 0.5;") != std::string::npos);
    REQUIRE(cutoutRegion.find("float coverage = in.color.a * texColor.a;") != std::string::npos);
    REQUIRE(cutoutRegion.find("if (coverage < kSolidCutoutCoverage)") != std::string::npos);
    REQUIRE(cutoutRegion.find("float coverageThreshold = fract(") != std::string::npos);
    REQUIRE(cutoutRegion.find("if (coverage <= coverageThreshold)") != std::string::npos);
    REQUIRE(bootstrap.find("const bool measuredWorldCutout = state.alphaRef == 254 && state.useDepth;") !=
            std::string::npos);
    REQUIRE(bootstrap.find("measuredWorldCutout ? MTL::CullModeBack : MTL::CullModeNone") != std::string::npos);

    const std::string textureMetal = ReadTextFile(repoRoot / "engine" / "PoseidonMTL" / "TextureMTL.cpp");
    REQUIRE_FALSE(textureMetal.empty());
    REQUIRE(textureMetal.find("decoded.pctPartial < 30.0") != std::string::npos);
    REQUIRE(bootstrap.find("state.alphaRef == 254 ? _impl->depthStateTLLess") != std::string::npos);
}
