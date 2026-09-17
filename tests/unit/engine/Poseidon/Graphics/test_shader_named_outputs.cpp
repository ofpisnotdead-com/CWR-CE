#include <catch2/catch_test_macros.hpp>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <stddef.h>
#include <catch2/catch_message.hpp>
#include <cctype>

// I-28 / B-025 — shader source related-computation co-location.
//
// B-025 was a vertex-shader regression where computing the lighting
// term required `worldNormal` and `viewPos` named locals; a "clean
// up unused intermediates" edit could delete one and the dependent
// arithmetic far below would silently compile against shader globals
// or break in subtle ways.
//
// glslang gives us the strongest enforcement available without
// custom codegen: every shipped GLSL block must parse successfully
// against GL 3.30 core profile rules.  A deletion of `vec3 worldNormal`
// (or `vec4 viewPos`, or any other named local the downstream code
// references) trips the parser with "undeclared identifier" at the
// reference site.  The test compiles each `static const char s_*GLSL[]`
// block in EngineGL33_Shaders.cpp and asserts info-log emptiness.
//
// Limitation: we don't run the linker (no `glslang::TProgram`) —
// individual stages compile, cross-stage `in`/`out` matching is
// not verified here.  That's I-NEW territory if it ever becomes a
// failure mode.

namespace
{
std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct ShaderBlock
{
    std::string symbol; // e.g. "s_vsTransformGLSL"
    std::string source; // GLSL body between R"( and )"
};

// Extract every `static const char s_*GLSL[] = R"(...)";` block from the
// shaders source file.  The raw-string delimiter is empty (`R"(`) so we
// can match it without needing to know which delimiter token was used.
std::vector<ShaderBlock> ExtractShaderBlocks(const std::string& src)
{
    std::vector<ShaderBlock> blocks;
    static const std::regex header(R"(static\s+const\s+char\s+(s_\w+GLSL)\[\]\s*=\s*R\"\()");
    auto begin = std::sregex_iterator(src.begin(), src.end(), header);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
    {
        const std::smatch& m = *it;
        const std::string sym = m[1].str();
        const size_t start = static_cast<size_t>(m.position(0)) + m.length(0);
        const size_t close = src.find(")\"", start);
        if (close == std::string::npos)
            continue;
        blocks.push_back({sym, src.substr(start, close - start)});
    }
    return blocks;
}

EShLanguage StageFromSymbol(const std::string& sym)
{
    // "s_vs..." prefix → vertex stage; everything else (PS, FS) → fragment.
    if (sym.rfind("s_vs", 0) == 0)
        return EShLangVertex;
    return EShLangFragment;
}

struct CompileOutcome
{
    bool success;
    std::string info;
};

CompileOutcome CompileGLSL(const std::string& src, EShLanguage stage)
{
    glslang::TShader shader(stage);
    const char* strings[1] = {src.c_str()};
    shader.setStrings(strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientNone, 330);
    const TBuiltInResource* resources = GetDefaultResources();
    const bool ok = shader.parse(resources, 330, /*forwardCompatible*/ false, EShMsgDefault);
    CompileOutcome out;
    out.success = ok;
    out.info = shader.getInfoLog();
    return out;
}

struct GlslangInit
{
    GlslangInit() { glslang::InitializeProcess(); }
    ~GlslangInit() { glslang::FinalizeProcess(); }
};
} // namespace

TEST_CASE("I-28: every shipped GL33 shader compiles cleanly under glslang", "[Graphics][Shaders][I-28]")
{
    GlslangInit init;

    const std::filesystem::path shadersCpp =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "EngineGL33_Shaders.cpp";
    const std::string body = ReadTextFile(shadersCpp);
    REQUIRE_FALSE(body.empty());

    const auto blocks = ExtractShaderBlocks(body);
    REQUIRE(blocks.size() >= 4); // at least VSTransform + PSNormal + VSScreen + PSFlat

    for (const auto& b : blocks)
    {
        CAPTURE(b.symbol);
        const auto outcome = CompileGLSL(b.source, StageFromSymbol(b.symbol));
        CAPTURE(outcome.info);
        REQUIRE(outcome.success);
    }
}

TEST_CASE("I-28: VSTransform exposes worldNormal and viewPos as named locals", "[Graphics][Shaders][I-28]")
{
    // Direct pin: the LIT vertex shader's lighting math depends on
    // `worldNormal` and `viewPos` being declared and computed.  A
    // delete-the-intermediate regression trips the glslang compile
    // above; this test adds a structural sanity check so the names
    // can't drift without an explicit rename touching this list.
    const std::filesystem::path shadersCpp =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "EngineGL33_Shaders.cpp";
    const std::string body = ReadTextFile(shadersCpp);
    REQUIRE(body.find("vec3 worldNormal") != std::string::npos);
    REQUIRE(body.find("vec4 viewPos") != std::string::npos);
}

namespace
{

// Count occurrences of `needle` in `haystack` as a whole identifier
// (boundaries: must not be preceded/followed by alphanumeric or '_').
// This avoids matching `worldNormal` inside `someOtherworldNormal`.
int CountIdentifier(const std::string& haystack, const std::string& needle)
{
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        const bool startOk =
            pos == 0 || !(std::isalnum(static_cast<unsigned char>(haystack[pos - 1])) || haystack[pos - 1] == '_');
        const size_t endPos = pos + needle.size();
        const bool endOk = endPos >= haystack.size() ||
                           !(std::isalnum(static_cast<unsigned char>(haystack[endPos])) || haystack[endPos] == '_');
        if (startOk && endOk)
            ++count;
        pos += needle.size();
    }
    return count;
}

// Find the GLSL block whose `static const char s_<symbol>GLSL[]` symbol
// matches `symbol`.  Returns the body or empty string if not found.
std::string FindShaderBlock(const std::string& src, const std::string& symbol)
{
    const std::string header = "static const char " + symbol + "[] = R\"(";
    const size_t start = src.find(header);
    if (start == std::string::npos)
        return {};
    const size_t bodyStart = start + header.size();
    const size_t bodyEnd = src.find(")\"", bodyStart);
    if (bodyEnd == std::string::npos)
        return {};
    return src.substr(bodyStart, bodyEnd - bodyStart);
}

} // namespace

TEST_CASE("I-28 (Option E): every named intermediate is declared AND used in the same shader block",
          "[Graphics][Shaders][I-28]")
{
    // The B-025 failure shape: a vertex shader computes
    // `worldNormal = normalize(mat3(world) * normal)` as an
    // intermediate, then the lighting math several lines below
    // references it.  A "clean up unused intermediates" edit could
    // delete the declaration; glslang catches that at compile time
    // because the reference is now undefined — already covered by
    // the compile-everything test above.
    //
    // What that compile test does NOT catch: the *reverse* — a
    // declaration left in place after all uses got deleted.  A dead
    // intermediate keeps the shader compiling but signals that the
    // computation it represents has drifted away from its callers.
    // For named outputs the engine's runtime depends on (the lit-
    // family computes worldNormal / viewPos in VSTransform and the
    // lighting code further down reads them), require BOTH:
    //
    //   (1) declaration site exists in this shader block,
    //   (2) name is referenced at least twice elsewhere in the
    //       same block (one decl + one+ uses; we require 3 total
    //       occurrences to insist the intermediate has at least
    //       one consumer).
    //
    // Splitting these intermediates out of VSTransform into a
    // separate block (without porting the lighting math too) trips
    // this test before any visual regression.
    const std::filesystem::path shadersCpp =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "EngineGL33_Shaders.cpp";
    const std::string src = ReadTextFile(shadersCpp);
    REQUIRE_FALSE(src.empty());

    struct Requirement
    {
        std::string shaderSymbol;
        std::string identifier;
        int minOccurrences;
    };

    const Requirement requirements[] = {
        // VSTransform: B-025's failure class.  worldNormal: declared
        // once + 2 lighting uses (NdotL, NdotH).  viewPos: declared
        // once + 1 use (gl_Position = proj * viewPos).  Pin minimums
        // a hair below current counts to absorb non-semantic edits.
        {"s_vsTransformGLSL", "worldNormal", 3},
        {"s_vsTransformGLSL", "viewPos", 2},
    };

    for (const auto& r : requirements)
    {
        CAPTURE(r.shaderSymbol, r.identifier);
        const std::string block = FindShaderBlock(src, r.shaderSymbol);
        REQUIRE_FALSE(block.empty());
        const int occurrences = CountIdentifier(block, r.identifier);
        CAPTURE(occurrences);
        REQUIRE(occurrences >= r.minOccurrences);
    }
}
