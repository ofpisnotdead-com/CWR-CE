#include <PoseidonMTL/TextBankMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

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
    if (!mtlTexture->EnsureBigSurface(*_bootstrap, *this, selectedLevel) && mtlTexture->HasBigSurface())
    {
        // EnsureBigSurface returns false both when nothing needed to change
        // AND when allocation failed -- HasBigSurface() distinguishes "no-op
        // because already sufficient" (still want to refresh its LRU
        // position, it's genuinely in use this frame) from "no big surface
        // at all" (small-surface-only texture, or a promotion that failed
        // even after reserving -- nothing to touch in the LRU either way).
        TouchLRU(mtlTexture);
    }
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

void TextBankMTL::EnsureBudgetInitialized()
{
    if (_maxTextureMemory >= 0)
        return;
    // No GL33-style 256MB fallback needed -- RecommendedMaxWorkingSetSize is
    // always available once the device exists (EngineMTLBootstrap.hpp's doc
    // comment). If the device genuinely isn't ready yet, stay uninitialized
    // (-1) and retry next call rather than locking in a bogus 0-byte budget.
    const uint64_t recommended = _bootstrap->RecommendedMaxWorkingSetSize();
    if (recommended == 0)
        return;
    _maxTextureMemory = static_cast<int64_t>(recommended);
}

void TextBankMTL::ReleaseAllTextures()
{
    _texture.Clear();
    _bigSurfaceLRU.Clear();
    _totalBigSurfaceBytes = 0;
    _bootstrap->ClearTexturePool();
    _totalPooledBytes = 0;
}

void TextBankMTL::ReserveMemory(int64_t neededBytes)
{
    EnsureBudgetInitialized();
    if (_maxTextureMemory < 0)
        return; // budget not available yet -- nothing to enforce against
    // Phase 1 (Milestone 3): drain the GPU-surface pool first -- mirrors
    // GL33's TextBankGL33::ReserveMemory always draining _freeTextures
    // before touching any active/in-use texture. Destroying a pooled-but-
    // unused surface costs nothing visually (nothing on screen references
    // it), so it's strictly cheaper than evicting something currently
    // displayed -- exhaust this option first.
    while (_totalBigSurfaceBytes + _totalPooledBytes + neededBytes > _maxTextureMemory)
    {
        const int64_t freed = _bootstrap->TrimOldestPooledTexture();
        if (freed <= 0)
            break; // pool empty
        _totalPooledBytes -= freed;
    }
    // Phase 2 (Milestone 2, unchanged): evict active big surfaces, oldest
    // (least-recently-used) first. This always genuinely destroys -- never
    // pools -- since it's forcibly taking memory from a texture that may
    // still be on screen, not a texture voluntarily upgrading its own
    // detail level (see TextureMTL::EnsureBigSurface's doc comment for that
    // distinction).
    while (_totalBigSurfaceBytes + _totalPooledBytes + neededBytes > _maxTextureMemory)
    {
        HMipCacheMTL* victim = _bigSurfaceLRU.Last();
        if (victim == nullptr)
            break; // nothing left to evict
        TextureMTL* texture = victim->texture;
        const int64_t freed = texture->BigSurfaceBytes();
        texture->EvictBigSurface(*_bootstrap); // also unlinks `victim` from this list
        _totalBigSurfaceBytes -= freed;
        // Safety net: a zero-byte victim means EvictBigSurface had nothing
        // to free (e.g. some other bug left a stale LRU entry pointing at a
        // texture with no big surface). Without this, that entry could
        // become a permanent no-progress loop -- confirmed live once
        // already (a different bug, now fixed) when this hung the game.
        // Bail out rather than attempt to patch the list by hand here --
        // a failed promotion (texture stays on its small surface) is a far
        // smaller problem than risking a double-free/leak in a fallback
        // path that should never trigger now the real bug is fixed.
        if (freed <= 0)
        {
            LOG_WARN(Graphics, "MTL texture streaming: zero-byte eviction, bailing out of ReserveMemory");
            break;
        }
    }
}

} // namespace Poseidon
