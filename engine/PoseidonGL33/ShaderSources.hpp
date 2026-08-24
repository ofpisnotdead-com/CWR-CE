#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Public view of the GL33 shader catalog for tooling and tests that need the exact source the runtime compiles. 
// The shader bodies, the fragment registry, and the preprocessor logic are in EngineGL33_ShaderSources.cpp.

namespace Poseidon::render::gl33
{

enum class ShaderStage
{
    Vertex,
    Fragment,
};

struct ShaderModule
{
    const char* name; // debug name, e.g. "vsTransform"
    ShaderStage stage;
    const char* source; // raw GLSL, still carrying //#include <...> directives
};

// The shader entry points compiled at startup (see InitVertexShaders / InitPixelShaders).
const std::vector<ShaderModule>& AllShaders();

// Applies `//#include <name>` directives to the source, splicing in the registered GLSL fragments.
std::string PreprocessShaderSource(const char* source);

const char* ShaderSourceByName(const char* name);

// Computes a FNV-1a hash from all GLSL shaders and fragments. Used as the shader cache key.
uint64_t HashShaderSources();

} // namespace Poseidon::render::gl33
