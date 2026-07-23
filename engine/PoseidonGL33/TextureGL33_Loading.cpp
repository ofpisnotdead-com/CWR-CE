#include <Poseidon/Core/Application.hpp>

#include <PoseidonGL33/TextureGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <glad/gl.h>

#include <Poseidon/Graphics/Core/MipmapLayout.hpp>

extern int MipmapSizeGL33(PacFormat format, int w, int h);
extern void InitGLPixelFormat(TextureDescGL33& desc, PacFormat format, bool enableDXT);

namespace
{
bool IsCompressedInterpolationFormat(PacFormat format)
{
    switch (format)
    {
        case PacDXT1:
        case PacDXT2:
        case PacDXT3:
        case PacDXT4:
        case PacDXT5:
            return true;
        default:
            return false;
    }
}

} // namespace

PacFormat UploadFormatForTextureGL33(PacFormat format, bool interpolate)
{
    if (interpolate && IsCompressedInterpolationFormat(format))
        return PacARGB1555;
    return format;
}

void TextureGL33::InitDesc(TextureDescGL33& desc, int levelMin, bool enableDXT)
{
    memset(&desc, 0, sizeof(desc));

    PacFormat format = UploadFormatForTextureGL33(_mipmaps[levelMin].DstFormat(), _interpolate);
    InitGLPixelFormat(desc, format, enableDXT);

    desc.w = _mipmaps[levelMin]._w;
    desc.h = _mipmaps[levelMin]._h;
    desc.nMipmaps = _nMipmaps - levelMin;
}

int TextureGL33::TotalSize(int levelMin) const
{
    int totalSize = 0;
    for (int i = levelMin; i < _nMipmaps; i++)
    {
        const PacLevelMem& mip = _mipmaps[i];
        totalSize += MipmapSizeGL33(UploadFormatForTextureGL33(_mipmaps[levelMin].DstFormat(), _interpolate), mip._w,
                                    mip._h);
    }
    return totalSize;
}

int TextureGL33::UploadToGPU(SurfaceInfoGL33& surface, int levelMin)
{
    if (!_src)
    {
        RptF("No texture source for %s", Name());
        return -1;
    }

    unsigned int tex = surface.GetTexture();
    if (!tex)
        return -1;

    // Upload via the dedicated upload unit so cached unit-0/1 bindings
    // tracked by ApplyPassState/_lastHandle remain accurate (otherwise a
    // demand-load between two draws of the same texture leaves GL bound to
    // the just-uploaded handle while the cache claims the previous binding,
    // and the next ApplyPassState skips the rebind — visible as the
    // M113-track-wheel "blink").
    GL33Bind::Tex2D(EngineGL33::kUploadUnit - GL_TEXTURE0, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Capture the format once at level-min and reuse for every mip.
    // Per-mip DstFormat() can occasionally diverge from the level-min
    // (mixed-format PAAs are rare but legal), and CreateSurface in
    // TextureGL33_Init.cpp allocates *every* mip level with the
    // level-min format — uploading a sub-image with a different
    // compressed internalFormat trips GL_INVALID_OPERATION (0x0502).
    // Treat level-min as authoritative; the data buffer for each mip
    // is the right size for that format (size math below uses it).
    const PacFormat sharedFmt = UploadFormatForTextureGL33(_mipmaps[levelMin].DstFormat(), _interpolate);

    for (int i = levelMin; i < _nMipmaps; i++)
    {
        PacLevelMem& srcMip = _mipmaps[i];
        PacLevelMem mip = srcMip;
        if (mip.DstFormat() != sharedFmt)
            mip.SetDestFormat(sharedFmt, 8);
        int aLevel = i - levelMin;

        PacFormat dstFmt = sharedFmt;
        int tightPitch = 0;
        int rowCount = 0;
        int dataSize = 0;

        // Per-mip pitch / size — must use this mip's dimensions, not
        // base mip's (B-016 / B-021).  See I-15 / I-16 in
        // render-invariants.md.
        const auto layout = Poseidon::render::mipmap::ComputeLayout(dstFmt, srcMip._w, srcMip._h);
        tightPitch = layout.tightPitch;
        rowCount = layout.rowCount;
        dataSize = layout.dataSize;

        AUTO_STATIC_ARRAY(char, pixelData, 256 * 256 * 4);
        pixelData.Realloc(dataSize);
        pixelData.Resize(dataSize);

        int ret = _src->GetMipmapData(pixelData.Data(), mip, i);

        if (_interpolate)
        {
            PoseidonAssert(_interpolate->_nMipmaps == _nMipmaps);
            PacLevelMem imip = _interpolate->_mipmaps[i];
            if (imip.DstFormat() != sharedFmt)
                imip.SetDestFormat(sharedFmt, 8);

            AUTO_STATIC_ARRAY(char, imem, 256 * 256 * 4);
            imem.Realloc(dataSize);
            imem.Resize(dataSize);

            _interpolate->_src->GetMipmapData(imem.Data(), imip, i);
            mip.Interpolate(pixelData.Data(), imem.Data(), imip, _iFactor);
        }

        if (!ret)
        {
            memset(pixelData.Data(), 0, dataSize);
            Poseidon::Foundation::WarningMessage("Cannot load mipmap %s", Name());
        }

        // Upload to GL
        TextureDescGL33 fmtDesc;
        InitGLPixelFormat(fmtDesc, dstFmt, true);

        if (fmtDesc.compressed)
        {
            glCompressedTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, srcMip._w, srcMip._h, fmtDesc.internalFormat, dataSize,
                                      pixelData.Data());
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                LOG_ERROR(
                    Graphics,
                    "GL33: glCompressedTexSubImage2D FAILED err=0x{:04X} tex={} level={} {}x{} fmt=0x{:04X} size={}",
                    err, tex, aLevel, srcMip._w, srcMip._h, fmtDesc.internalFormat, dataSize);
            }
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, aLevel, 0, 0, srcMip._w, srcMip._h, fmtDesc.pixelFormat, fmtDesc.pixelType,
                            pixelData.Data());
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                LOG_ERROR(Graphics, "GL33: glTexSubImage2D FAILED err=0x{:04X} tex={} level={} {}x{} fmt=0x{:04X}", err,
                          tex, aLevel, srcMip._w, srcMip._h, fmtDesc.pixelFormat);
            }
        }
    }

    GL33Bind::ActiveUnit(0);
    return 0;
}

int TextureGL33::LoadLevels(int levelMin)
{
    if (levelMin < 0)
        return 0;

    TextBankGL33* bank = static_cast<TextBankGL33*>(GEngine->TextBank());

    PoseidonAssert(levelMin < _nMipmaps);
    PoseidonAssert(levelMin >= 0);

    int ret = 0;

    if (_interpolate)
        _interpolate->_inUse++;

    if (_levelLoaded > levelMin)
    {
        ReleaseMemory(true);

        _inUse++;

        TextureDescGL33 desc;
        InitDesc(desc, levelMin, true);

        PacFormat format = UploadFormatForTextureGL33(_mipmaps[levelMin].DstFormat(), _interpolate);
        bank->UseReleased(_surface, desc, format);

        if (!_surface.GetTexture())
        {
            if (bank->_totalAllocated > bank->_limitAllocatedTextures - 512 * 1024)
                bank->Reuse(_surface, desc, format);
        }

        int totalSize = TotalSize(levelMin);

        if (!_surface.GetTexture())
        {
            bank->ReserveMemory(totalSize);

            if (bank->CreateGPUSurface(_surface, desc, format, totalSize) < 0)
            {
                _inUse--;
                if (_interpolate)
                    _interpolate->_inUse--;
                return -1;
            }

            bank->_thisFrameAlloc++;
        }

        ret = UploadToGPU(_surface, levelMin);

        _inUse--;

        CacheUse(bank->_thisFrameWholeUsed);
        _levelLoaded = levelMin;
    }

    if (_interpolate)
        _interpolate->_inUse--;

    if (ret < 0)
        ReleaseMemory(true);

    return ret;
}

void TextureGL33::ReleaseSmall(bool store)
{
    TextBankGL33* bank = static_cast<TextBankGL33*>(GEngine->TextBank());
    if (_smallSurface.GetTexture())
    {
        if (store)
        {
            bank->AddReleased(_smallSurface);
            _smallSurface.Free(false);
        }
        else
        {
            bank->_totalAllocated -= _smallSurface.SizeUsed();
            bank->_thisFrameAlloc++;
            _smallSurface.Free(true);
        }
        _smallLoaded = _nMipmaps;
    }
}

int TextureGL33::LoadSmall()
{
    if (_smallSurface.GetTexture())
        return 0;

    TextBankGL33* bank = static_cast<TextBankGL33*>(GEngine->TextBank());

    if (_nMipmaps <= 0)
        return -1;

    int i;
    for (i = 0; i < _nMipmaps; i++)
    {
        int pixels = _mipmaps[i]._w * _mipmaps[i]._h;
        if (pixels <= bank->_maxSmallTexturePixels)
            break;
    }
    int levelMin = i;
    if (levelMin >= _nMipmaps)
        levelMin = _nMipmaps - 1;

    PacFormat format = UploadFormatForTextureGL33(_mipmaps[levelMin].DstFormat(), _interpolate);
    TextureDescGL33 desc;
    InitDesc(desc, levelMin, false);

    bank->UseReleased(_smallSurface, desc, format);

    int totalSize = TotalSize(levelMin);

    if (!_smallSurface.GetTexture())
    {
        bank->ReserveMemory(totalSize);

        if (bank->CreateGPUSurface(_smallSurface, desc, format, totalSize) < 0)
            return -1;
    }

    int ret = UploadToGPU(_smallSurface, levelMin);
    if (ret >= 0)
    {
        _smallLoaded = levelMin;
        return 0;
    }
    return -1;
}

void TextureGL33::MemoryReleased()
{
    if (_cache)
    {
        _cache->Delete();
        delete _cache;
        _cache = nullptr;
    }
    _levelLoaded = _nMipmaps;
}

void TextureGL33::ReleaseMemory(bool store)
{
    TextBankGL33* bank = static_cast<TextBankGL33*>(GEngine->TextBank());
    if (_surface.GetTexture())
    {
        if (store)
        {
            bank->AddReleased(_surface);
        }
        else
        {
            bank->_totalAllocated -= _surface.SizeUsed();
            bank->_thisFrameAlloc++;
        }
        _surface.Free(!store);
        MemoryReleased();
    }
}

void TextureGL33::ReuseMemory(SurfaceInfoGL33& surf)
{
    if (_surface.GetTexture())
    {
        surf = _surface;
        _surface.Free(false);
    }
    MemoryReleased();
}

void TextureGL33::CacheUse(GL33MipCacheRoot& list)
{
    HMipCacheGL33* first;
    if (_cache)
    {
        _cache->Delete();
        first = _cache;
    }
    else
    {
        first = new HMipCacheGL33;
    }
    first->texture = this;
    list.Insert(first);
    _cache = first;
}
