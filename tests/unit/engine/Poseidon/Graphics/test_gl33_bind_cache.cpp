#include <catch2/catch_test_macros.hpp>

#include <PoseidonGL33/GL33BindCache.hpp>
#include <glad/gl.h>

#include <array>
#include <utility>
#include <vector>

namespace
{
std::vector<std::pair<GLenum, GLuint>> g_bufferBinds;
std::vector<GLenum> g_activeUnits;
std::vector<std::pair<GLenum, GLuint>> g_textureBinds;
std::array<GLuint, 8> g_textures{};
int g_activeUnit = 0;

void GLAD_API_PTR RecordBindBuffer(GLenum target, GLuint buffer)
{
    g_bufferBinds.emplace_back(target, buffer);
}

void GLAD_API_PTR RecordActiveTexture(GLenum texture)
{
    g_activeUnit = static_cast<int>(texture - GL_TEXTURE0);
    g_activeUnits.push_back(texture);
}

void GLAD_API_PTR RecordBindTexture(GLenum target, GLuint texture)
{
    g_textures[static_cast<std::size_t>(g_activeUnit)] = texture;
    g_textureBinds.emplace_back(target, texture);
}

void GLAD_API_PTR RecordGetInteger(GLenum name, GLint* value)
{
    REQUIRE(name == GL_TEXTURE_BINDING_2D);
    *value = static_cast<GLint>(g_textures[static_cast<std::size_t>(g_activeUnit)]);
}

class ScopedGladBindings
{
  public:
    ScopedGladBindings()
        : _bindBuffer(glad_glBindBuffer), _activeTexture(glad_glActiveTexture), _bindTexture(glad_glBindTexture),
          _getInteger(glad_glGetIntegerv)
    {
        glad_glBindBuffer = RecordBindBuffer;
        glad_glActiveTexture = RecordActiveTexture;
        glad_glBindTexture = RecordBindTexture;
        glad_glGetIntegerv = RecordGetInteger;
        g_bufferBinds.clear();
        g_activeUnits.clear();
        g_textureBinds.clear();
        g_textures.fill(0);
        g_activeUnit = 0;
        Poseidon::GL33Bind::Invalidate();
    }

    ~ScopedGladBindings()
    {
        Poseidon::GL33Bind::Invalidate();
        glad_glBindBuffer = _bindBuffer;
        glad_glActiveTexture = _activeTexture;
        glad_glBindTexture = _bindTexture;
        glad_glGetIntegerv = _getInteger;
    }

  private:
    PFNGLBINDBUFFERPROC _bindBuffer;
    PFNGLACTIVETEXTUREPROC _activeTexture;
    PFNGLBINDTEXTUREPROC _bindTexture;
    PFNGLGETINTEGERVPROC _getInteger;
};
} // namespace

TEST_CASE("GL33 bind cache suppresses repeated uniform buffer binds", "[Graphics][GL33][BindCache]")
{
    ScopedGladBindings glad;

    Poseidon::GL33Bind::UniformBuffer(7);
    Poseidon::GL33Bind::UniformBuffer(7);
    Poseidon::GL33Bind::UniformBuffer(9);

    REQUIRE(g_bufferBinds == std::vector<std::pair<GLenum, GLuint>>{{GL_UNIFORM_BUFFER, 7}, {GL_UNIFORM_BUFFER, 9}});

    Poseidon::GL33Bind::Invalidate();
    Poseidon::GL33Bind::UniformBuffer(9);
    REQUIRE(g_bufferBinds.size() == 3);
}

TEST_CASE("GL33 sampling binds skip active texture selection on ordinary cache hits", "[Graphics][GL33][BindCache]")
{
    ScopedGladBindings glad;

    Poseidon::GL33Bind::Tex2DForSampling(3, 17);
    bool skippedSelection = false;
    for (int i = 0; i < 2; ++i)
    {
        Poseidon::GL33Bind::ActiveUnit(0);
        const auto activeCalls = g_activeUnits.size();
        Poseidon::GL33Bind::Tex2DForSampling(3, 17);
        if (g_activeUnits.size() == activeCalls)
        {
            skippedSelection = true;
            break;
        }
    }

    REQUIRE(skippedSelection);
    REQUIRE(g_textureBinds.size() == 1);

    Poseidon::GL33Bind::ActiveUnit(0);
    Poseidon::GL33Bind::Tex2D(3, 17);
    REQUIRE(g_activeUnits.back() == GL_TEXTURE3);
    REQUIRE(g_textureBinds.size() == 1);
}
