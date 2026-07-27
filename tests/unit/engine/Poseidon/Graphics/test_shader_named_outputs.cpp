#include <catch2/catch_test_macros.hpp>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>

#include <PoseidonGL33/ShaderSources.hpp>

#include <string>
#include <vector>
#include <cstddef>
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
// reference site.
//
// Limitation: we don't run the linker (no `glslang::TProgram`) —
// individual stages compile, cross-stage `in`/`out` matching is
// not verified here.  That's I-NEW territory if it ever becomes a
// failure mode.

using Poseidon::render::gl33::AllShaders;
using Poseidon::render::gl33::PreprocessShaderSource;
using Poseidon::render::gl33::ShaderModule;
using Poseidon::render::gl33::ShaderStage;

namespace
{

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
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 330);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetNone, glslang::EShTargetSpv_1_0);
    const TBuiltInResource* resources = GetDefaultResources();
    const EShMessages rules = EShMsgDefault;
    const bool ok = shader.parse(resources, 330, /*forwardCompatible*/ false, rules);
    CompileOutcome out;
    out.success = ok;
    out.info = shader.getInfoLog();
    return out;
}

EShLanguage ToGlslang(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? EShLangVertex : EShLangFragment;
}

const ShaderModule* FindModule(const char* name)
{
    for (const auto& m : AllShaders())
    {
        if (std::string(m.name) == name)
        {
            return &m;
        }
    }
    return nullptr;
}

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

struct GlslangInit
{
    GlslangInit() { glslang::InitializeProcess(); }
    ~GlslangInit() { glslang::FinalizeProcess(); }
};

} // namespace

TEST_CASE("I-28: every shipped GL33 shader compiles cleanly under glslang", "[Graphics][Shaders][I-28]")
{
    GlslangInit init;

    const auto& shaders = AllShaders();

    for (const auto& m : shaders)
    {
        CAPTURE(m.name);
        const std::string assembled = PreprocessShaderSource(m.source);
        CAPTURE(assembled);
        const auto outcome = CompileGLSL(assembled, ToGlslang(m.stage));
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
    const ShaderModule* vs = FindModule("vsTransform");
    REQUIRE(vs != nullptr);
    const std::string src = vs->source;
    REQUIRE(src.find("vec3 worldNormal") != std::string::npos);
    REQUIRE(src.find("vec4 viewPos") != std::string::npos);
}

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
    struct Requirement
    {
        const char* shaderName;
        std::string identifier;
        int minOccurrences;
    };

    const Requirement requirements[] = {
        // VSTransform: B-025's failure class.  worldNormal: declared
        // once + 2 lighting uses (NdotL, NdotH).  viewPos: declared
        // once + 1 use (gl_Position = proj * viewPos).  Pin minimums
        // a hair below current counts to absorb non-semantic edits.
        {"vsTransform", "worldNormal", 3},
        {"vsTransform", "viewPos", 2},
    };

    for (const auto& r : requirements)
    {
        CAPTURE(r.shaderName, r.identifier);
        const ShaderModule* mod = FindModule(r.shaderName);
        REQUIRE(mod != nullptr);
        const int occurrences = CountIdentifier(mod->source, r.identifier);
        CAPTURE(occurrences);
        REQUIRE(occurrences >= r.minOccurrences);
    }
}
