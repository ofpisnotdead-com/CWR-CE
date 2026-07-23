#include <Poseidon/IO/ParamFile/ParamFile.hpp>

extern ParamFile Remaster;

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/Core/Progress.hpp>
#include <Poseidon/Core/Global.hpp>

#include <glad/gl.h>

#include <Poseidon/Input/InputSubsystem.hpp>

namespace
{

bool HasGlExtension(const char* extensionName)
{
    if (!extensionName || !*extensionName)
        return false;

    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    if (count <= 0)
        return false;

    for (GLint i = 0; i < count; ++i)
    {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && strcmp(ext, extensionName) == 0)
            return true;
    }

    return false;
}

} // namespace

void TextBankGL33::StartFrame()
{
    InitDetailTextures();
    CheckTextureMemory();
    _thisFrameCopied = 0;
    _thisFrameAlloc = 0;
    if (_loadBoostFrames > 0)
        --_loadBoostFrames;

#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat2ToDo(SDL_SCANCODE_E))
    {
        ReportTextures("textures.txt");
    }
#endif
}

void TextBankGL33::FinishFrame()
{
    for (HMipCacheGL33* tc = _thisFramePartialUsed.Last(); tc; tc = _thisFramePartialUsed.Prev(tc))
    {
        TextureGL33* tex = tc->texture;
        tex->_levelNeededLastFrame = tex->_levelNeededThisFrame;
        tex->_levelNeededThisFrame = tex->_nMipmaps;
        tex->ResetMipmap();
    }
    for (HMipCacheGL33* tc = _thisFrameWholeUsed.Last(); tc; tc = _thisFrameWholeUsed.Prev(tc))
    {
        TextureGL33* tex = tc->texture;
        tex->_levelNeededLastFrame = tex->_levelNeededThisFrame;
        tex->_levelNeededThisFrame = tex->_nMipmaps;
        tex->ResetMipmap();
    }
    for (HMipCacheGL33* tc = _lastFramePartialUsed.Last(); tc; tc = _lastFramePartialUsed.Prev(tc))
    {
        TextureGL33* tex = tc->texture;
        tex->_levelNeededLastFrame = tex->_levelNeededThisFrame;
        tex->_levelNeededThisFrame = tex->_nMipmaps;
        tex->ResetMipmap();
    }
    for (HMipCacheGL33* tc = _lastFrameWholeUsed.Last(); tc; tc = _lastFrameWholeUsed.Prev(tc))
    {
        TextureGL33* tex = tc->texture;
        tex->_levelNeededLastFrame = tex->_levelNeededThisFrame;
        tex->_levelNeededThisFrame = tex->_nMipmaps;
        tex->ResetMipmap();
    }

    _previousUsed.Move(_lastFrameWholeUsed);
    PoseidonAssert(_lastFrameWholeUsed.Last() == nullptr);

    _lastFrameWholeUsed.Move(_thisFrameWholeUsed);
    PoseidonAssert(_thisFrameWholeUsed.Last() == nullptr);

    _previousUsed.Move(_lastFramePartialUsed);
    PoseidonAssert(_lastFramePartialUsed.Last() == nullptr);

    _lastFramePartialUsed.Move(_thisFramePartialUsed);
    PoseidonAssert(_thisFramePartialUsed.Last() == nullptr);
}

DEFINE_FAST_ALLOCATOR(HMipCacheGL33);

const int MaxAllocationsPerFrame = 8;
const int MaxCopyPerFrame = 32768;

MipInfo TextBankGL33::UseMipmap(Texture* absTexture, int level, int top)
{
    if (!absTexture)
        return MipInfo(nullptr, 0);

    TextureGL33* texture = static_cast<TextureGL33*>(absTexture);
    texture->LoadHeadersNV();

    // Dynamic textures (CreateDynamic — font atlases, etc.) have no _src to
    // demand-load from. Their content is already in _surface from
    // InitFromRGBA, and any updates go through UpdateRGBA. Routing them
    // through the LoadSmall / LoadLevels paths below allocates an empty
    // _smallSurface, hands its handle to ApplyPassState, and the resulting
    // sample reads garbage / zero alpha — the FPS overlay and FreeType atlas
    // pages never paint. Short-circuit to the existing _surface.
    if (!texture->_src && texture->_surface.GetTexture())
        return MipInfo(texture, 0);

    saturateMin(level, texture->_mipmapNeeded);
    saturateMin(top, texture->_mipmapWanted);

    if (level < 0)
        level = 0;

    saturateMin(level, texture->_nMipmaps - 1);
    saturateMax(top, texture->_largestUsed);
    saturateMin(top, level);
    saturateMax(level, top);

    // Never use mipmaps smaller than some limit
    int limitUse = _maxSmallTexturePixels / 4;
    for (; level > 0; level--)
    {
        PacLevelMem* mipTop = &texture->_mipmaps[level];
        if (mipTop->_w * mipTop->_h >= limitUse)
            break;
    }

    saturateMin(top, level);

    // Budget gate is lifted while a load boost is active — mission load
    // wants every touched texture at its wanted level before the reveal.
    if (_loadBoostFrames <= 0 && (_thisFrameCopied > MaxCopyPerFrame || _thisFrameAlloc > MaxAllocationsPerFrame))
    {
        if (texture->_levelLoaded < texture->_nMipmaps)
        {
            top = level = texture->_levelLoaded;
        }
        else if (texture->_smallLoaded < texture->_nMipmaps)
        {
            top = level = texture->_smallLoaded;
        }
    }

    // Use small texture if adequate
    if (texture->_smallLoaded <= level)
    {
        if (texture->LoadSmall() < 0)
            return MipInfo(texture, -1);
        return MipInfo(texture, texture->_smallLoaded);
    }

    if (texture->_levelLoaded > level)
    {
        level = top;

        if (texture->_levelNeededThisFrame > level)
            texture->_levelNeededThisFrame = level;

        for (;;)
        {
            int ret = texture->LoadLevels(level);
            if (ret >= 0)
                break;
            LOG_DEBUG(Graphics, "GL33: Out of VID: Try next level");
            level++;
            if (level >= texture->_nMipmaps)
                break;
        }
    }
    else
    {
        if (texture->_levelNeededThisFrame > level)
            texture->_levelNeededThisFrame = level;

        PoseidonAssert(texture->_cache);
        if (texture->LevelNeeded() <= texture->_levelLoaded)
        {
            texture->CacheUse(_thisFrameWholeUsed);
        }
        else
        {
            texture->CacheUse(_thisFramePartialUsed);
        }
    }

    level = texture->_levelLoaded;
    if (level >= texture->_nMipmaps)
    {
        if (texture->LoadSmall() < 0)
            return MipInfo(texture, -1);
        return MipInfo(texture, texture->_smallLoaded);
    }

    return MipInfo(texture, level);
}

void TextBankGL33::InitDetailTextures()
{
    if (_detail)
        return;

    const ParamEntry& names = Remaster >> "CfgDetailTextures";
    RStringB detailName = names >> "detail";
    if (QIFStreamB::FileExist(detailName))
    {
        _detail = new TextureGL33;
        _detail->Init(detailName);
        _detail->_isDetail = true;
    }

    RStringB specularName = names >> "specular";
    if (QIFStreamB::FileExist(specularName))
    {
        _specular = new TextureGL33;
        _specular->Init(specularName);
        _specular->_isDetail = true;
    }

    RStringB grassName = names >> "grass";
    if (QIFStreamB::FileExist(grassName))
    {
        _grass = new TextureGL33;
        _grass->Init(grassName);
        _grass->SetMaxSize(1024);
        _grass->_isDetail = true;
    }

    RStringB waterName = names >> "waterBump";
    if (QIFStreamB::FileExist(waterName))
    {
        _waterBump = new TextureGL33;
        _waterBump->Init(waterName);
        _waterBump->SetMaxSize(1024);
        _waterBump->_isDetail = true;
    }
}

void TextBankGL33::FlushTextures()
{
    Compact();
    while (_freeTextures.Size() > 0)
    {
        DeleteLastReleased();
    }
}

void TextBankGL33::ForceReloadAll()
{
    // Drop GPU handles + cached headers + decoded source for every
    // disk-backed texture so the next bind re-reads from disk.  Skip
    // dynamic textures (no ITextureSource — font glyph atlas pages,
    // CreateDynamic / InitFromRGBA callers): clearing _initialized on
    // them routes the next bind through DoLoadHeaders, which then
    // PAA-decodes whatever raw RGBA was uploaded and fails in pactext.
    // Only safe to call when nothing is mid-render.
    int dropped = 0;
    int skipped = 0;
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureGL33* tex = _texture[i];
        if (!tex)
            continue;
        if (!tex->_src)
        {
            ++skipped;
            continue;
        }
        tex->ReleaseMemory(false);
        tex->ReleaseSmall(false);
        tex->_src = nullptr;
        tex->_initialized = false;
        tex->_levelLoaded = MAX_MIPMAPS;
        tex->_smallLoaded = MAX_MIPMAPS;
        tex->_levelNeededThisFrame = MAX_MIPMAPS;
        tex->_levelNeededLastFrame = MAX_MIPMAPS;
        ++dropped;
    }
    LOG_INFO(Graphics, "TextBankGL33::ForceReloadAll: dropped {} textures, kept {} dynamic", dropped, skipped);
}

void TextBankGL33::FlushBank(QFBank* bank)
{
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureGL33* tex = _texture[i];
        if (!tex)
            continue;
        if (!bank->FileExists(tex->GetName()))
            continue;
        _texture.Delete(i);
        i--;
    }
}

static int CompareTextureFileOrderGL33(const LLink<TextureGL33>* tl1, const LLink<TextureGL33>* tl2)
{
    TextureGL33* t1 = *tl1;
    TextureGL33* t2 = *tl2;
    const char* n1 = t1->GetName();
    const char* n2 = t2->GetName();
    QFBank* b1 = QIFStreamB::AutoBank(t1->GetName());
    QFBank* b2 = QIFStreamB::AutoBank(t2->GetName());
    if (b1 > b2)
        return -1;
    if (b1 < b2)
        return +1;
    PoseidonAssert(b1 == b2);
    if (!b1)
        return 0;
    int o1 = b1->GetFileOrder(n1 + strlen(b1->GetPrefix()));
    int o2 = b2->GetFileOrder(n2 + strlen(b2->GetPrefix()));
    return o1 - o2;
}

void TextBankGL33::Preload()
{
    Compact();

    DWORD start = Foundation::GlobalTickCount();
    QSort(_texture.Data(), _texture.Size(), CompareTextureFileOrderGL33);

    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureGL33* tex = _texture[i];
        if (!tex)
            continue;
        tex->LoadHeadersNV();
        ProgressRefresh();
    }
    DWORD end = Foundation::GlobalTickCount();

    LOG_DEBUG(Graphics, "GL33: Preload {} textures - {} ms", _texture.Size(), end - start);
}

int TextBankGL33::FreeTextureMemory()
{
    constexpr int MinSaneMemKB = 32 * 1024; // 32 MB — no real GPU has less
    // Cap at 512 MB — engine doesn't need more, avoids int overflow with large VRAM
    constexpr int MaxBudget = 512 * 1024 * 1024;

    auto capBudget = [&](int64_t memKB) -> int
    {
        int64_t available = memKB * 1024 - _totalAllocated;
        if (available < 0)
            available = 0;
        if (available > MaxBudget)
            available = MaxBudget;
        return static_cast<int>(available);
    };

    // Try GL_NVX_gpu_memory_info (NVIDIA)
    if (HasGlExtension("GL_NVX_gpu_memory_info"))
    {
        GLint totalMemKB = 0;
        glGetIntegerv(0x9048, &totalMemKB); // GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
        GLenum err = glGetError();
        if (err == GL_NO_ERROR && totalMemKB >= MinSaneMemKB)
        {
            LOG_TRACE(Graphics, "GL33: NVIDIA VRAM: {} MB", totalMemKB / 1024);
            return capBudget(totalMemKB);
        }
    }

    // Try GL_ATI_meminfo (AMD)
    if (HasGlExtension("GL_ATI_meminfo"))
    {
        GLint atiInfo[4] = {};
        glGetIntegerv(0x87FC, atiInfo); // TEXTURE_FREE_MEMORY_ATI
        GLenum err = glGetError();
        if (err == GL_NO_ERROR && atiInfo[0] >= MinSaneMemKB)
        {
            LOG_DEBUG(Graphics, "GL33: AMD free VRAM: {} MB", atiInfo[0] / 1024);
            return capBudget(atiInfo[0]);
        }
    }

    // Fallback: 256 MB
    LOG_DEBUG(Graphics, "GL33: Using fallback VRAM budget: 256 MB");
    return 256 * 1024 * 1024;
}

void TextBankGL33::CheckTextureMemory()
{
    int freeMem = FreeTextureMemory();
    _limitAllocatedTextures = _totalAllocated + freeMem;
    _limitAllocatedTextures -= _reserveTextureMemory;
}
