#include <PoseidonGL33/GL33BindCache.hpp>
#include <glad/gl.h>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Graphics/Rendering/Frame/RuntimeChecks.hpp>

namespace Poseidon
{
namespace GL33Bind
{

namespace
{
constexpr unsigned int kUnknown = 0xffffffffu;
constexpr int kUnits = 8;

unsigned int g_vao = kUnknown;
unsigned int g_tex[kUnits] = {kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown};
int g_activeUnit = -1;
unsigned int g_uniformBuffer = kUnknown;

unsigned long long g_vaoReq = 0, g_vaoBind = 0, g_texReq = 0, g_texBind = 0;

// Per-unit skip counter; every 64th cache hit runs the divergence check in Tex2DBind.
unsigned int g_texSkipCtr[kUnits] = {0};
} // namespace

void Vao(unsigned int vao)
{
    ++g_vaoReq;
    if (vao == g_vao)
        return;
    glBindVertexArray(vao);
    g_vao = vao;
    ++g_vaoBind;
}

void ActiveUnit(int unit)
{
    if (unit == g_activeUnit)
        return;
    glActiveTexture(GL_TEXTURE0 + unit);
    g_activeUnit = unit;
}

void UniformBuffer(unsigned int buf)
{
    if (buf == g_uniformBuffer)
        return;
    glBindBuffer(GL_UNIFORM_BUFFER, buf);
    g_uniformBuffer = buf;
}

namespace
{
void Tex2DBind(int unit, unsigned int tex, bool samplingOnly)
{
    ++g_texReq;
    if (unit >= 0 && unit < kUnits && g_tex[unit] == tex)
    {
        if (!samplingOnly)
        {
            ActiveUnit(unit);
        }
        // A cache hit skips glBindTexture, so a stale entry (recycled handle,
        // or a bind that skipped Invalidate()) would sample the wrong texture.
        // Every 64th hit per unit, check the cache against the live GL binding
        // and log any mismatch.
        if ((g_texSkipCtr[unit]++ & 63u) == 0)
        {
            ActiveUnit(unit); // glGetIntegerv reads the active unit; cached no-op if already selected
            GLint live = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &live);
            if (auto v = Poseidon::render::frame::DetectBindCacheDivergence(unit, tex, static_cast<unsigned int>(live)))
                LOG_ERROR(Graphics, "GL33 [{}] {}", v->ruleId, v->detail);
        }
        return;
    }
    ActiveUnit(unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (unit >= 0 && unit < kUnits)
        g_tex[unit] = tex;
    ++g_texBind;
}
} // namespace

void Tex2DForSampling(int unit, unsigned int tex)
{
    Tex2DBind(unit, tex, true);
}

void Tex2D(int unit, unsigned int tex)
{
    Tex2DBind(unit, tex, false);
}

void OnVaoDeleted(unsigned int vao)
{
    if (g_vao == vao)
        g_vao = kUnknown;
}

void OnTexDeleted(unsigned int tex)
{
    for (int i = 0; i < kUnits; i++)
    {
        if (g_tex[i] == tex)
            g_tex[i] = kUnknown;
    }
}

bool IsTexBound(int unit, unsigned int tex)
{
    return unit >= 0 && unit < kUnits && g_tex[unit] == tex;
}

void Invalidate()
{
    g_vao = kUnknown;
    for (int i = 0; i < kUnits; i++)
        g_tex[i] = kUnknown;
    g_activeUnit = -1;
    g_uniformBuffer = kUnknown;
}

unsigned long long VaoRequests() { return g_vaoReq; }
unsigned long long VaoBinds() { return g_vaoBind; }
unsigned long long TexRequests() { return g_texReq; }
unsigned long long TexBinds() { return g_texBind; }

} // namespace GL33Bind
} // namespace Poseidon
