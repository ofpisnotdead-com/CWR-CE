#include <PoseidonMTL/TextBankMTL.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

extern ParamFile Remaster;

namespace Poseidon
{

TextBankMTL::~TextBankMTL()
{
    UnlockAllTextures();
    DeleteAllAnimated();
    // Individual GPU textures are not explicitly released here --
    // EngineMTLBootstrap::Shutdown() (called right after this destructor, by
    // EngineMTL's teardown order) releases every texture it owns
    // unconditionally.
}

int TextBankMTL::Find(RStringB name) const
{
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureMTL* texture = _texture[i];
        if (texture && texture->GetName() == name)
            return i;
    }
    return -1;
}

Ref<Texture> TextBankMTL::Load(RStringB name)
{
    int index = Find(name);
    if (index >= 0)
        return (Texture*)_texture[index];

    TextureMTL* texture = new TextureMTL();
    texture->SetName(name);
    texture->LoadPixels(*_bootstrap); // false on failure -- texture stays valid, renders as fallback white

    int iFree = _texture.Add();
    _texture[iFree] = texture;
    return texture;
}

// Weather sky cross-fade (see TextureMTL::LoadPixelsInterpolated's doc
// comment). Mirrors GL33's eps shortcuts at the extremes; otherwise builds
// one new CPU-blended texture per call rather than GL33's Find()-and-reuse
// caching -- Metal's "load once, keep forever" texture model (no memory
// budget/LRU) makes that caching an optimization, not a correctness
// requirement, so it's skipped for now.
Ref<Texture> TextBankMTL::LoadInterpolated(RStringB n1, RStringB n2, float factor)
{
    const float eps = 1.0f / 256;
    if (factor >= 1.0f - eps)
        return Load(n2);
    if (factor <= eps)
        return Load(n1);

    TextureMTL* texture = new TextureMTL();
    texture->SetName(n1); // matches GL33's Copy(index1): the blend's identity is n1's
    if (!texture->LoadPixelsInterpolated(*_bootstrap, n1, n2, factor))
    {
        delete texture;
        return nullptr;
    }

    int iFree = _texture.Add();
    _texture[iFree] = texture;
    return texture;
}

void TextBankMTL::InitDetailTextures()
{
    if (_detail || _grass || _specular || _waterBump)
        return;

    const ParamEntry& names = Remaster >> "CfgDetailTextures";
    auto loadDetail = [this](RStringB name, int maxSize = 4096) -> Ref<TextureMTL> {
        if (!QIFStreamB::FileExist(name))
            return nullptr;
        Ref<Texture> loaded = Load(name);
        TextureMTL* tex = dynamic_cast<TextureMTL*>(loaded.GetRef());
        if (tex)
            tex->SetMaxSize(maxSize);
        return tex;
    };

    _detail = loadDetail(names >> "detail");
    _specular = loadDetail(names >> "specular");
    _grass = loadDetail(names >> "grass", 1024);
    _waterBump = loadDetail(names >> "waterBump", 1024);
}

TextureMTL* TextBankMTL::GetDetailTexture()
{
    InitDetailTextures();
    return _detail;
}

TextureMTL* TextBankMTL::GetGrassTexture()
{
    InitDetailTextures();
    return _grass;
}

TextureMTL* TextBankMTL::GetSpecularTexture()
{
    InitDetailTextures();
    return _specular;
}

TextureMTL* TextBankMTL::GetWaterBumpMap()
{
    InitDetailTextures();
    return _waterBump;
}

MipInfo TextBankMTL::UseMipmap(Texture* texture, int level, int levelTop)
{
    if (texture == nullptr)
        return MipInfo(nullptr, 0);

    TextureMTL* mtlTexture = static_cast<TextureMTL*>(texture);
    const int selectedLevel = mtlTexture->NoteMipmapUse(level, levelTop);
    mtlTexture->EnsureBigSurface(*_bootstrap, selectedLevel);
    return MipInfo(texture, selectedLevel);
}

Texture* TextBankMTL::CreateDynamic(int w, int h, const void* rgba, uint32_t /*size*/, bool /*mipmap*/)
{
    // No mip-chain support (TextureMTL is single-mip by design); `mipmap` is
    // accepted to match the interface but ignored, same simplification
    // LoadPixels already makes.
    TextureMTL* texture = new TextureMTL();
    if (!texture->InitFromRGBA(*_bootstrap, w, h, rgba))
    {
        LOG_WARN(Graphics, "MTL: failed to create dynamic texture {}x{}", w, h);
        delete texture;
        return nullptr;
    }

    int iFree = _texture.Add();
    _texture[iFree] = texture;
    return texture;
}

void TextBankMTL::UpdateDynamic(Texture* texture, const void* rgba, uint32_t /*size*/)
{
    if (texture == nullptr)
        return;
    static_cast<TextureMTL*>(texture)->UpdateRGBA(*_bootstrap, rgba);
}

void TextBankMTL::FinishFrame()
{
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureMTL* texture = _texture[i];
        if (texture)
            texture->FinishFrameUseTracking();
    }
}

} // namespace Poseidon
