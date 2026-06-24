#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cmath>
#include <cstring>
#include <vector>

namespace Poseidon
{

bool TextureMTL::LoadPixels(EngineMTLBootstrap& bootstrap)
{
    QIFStream in;
    GFileServer->Open(in, Name());
    if (in.fail())
    {
        LOG_WARN(Graphics, "MTL: texture not found: {}", Name());
        return false;
    }

    const int size = in.rest();
    if (size <= 0)
        return false;

    std::vector<char> fileData(static_cast<size_t>(size));
    in.read(fileData.data(), size);

    const char* name = Name();
    const size_t len = name ? std::strlen(name) : 0;
    const bool isPaa = len >= 4 && (name[len - 1] == 'a' || name[len - 1] == 'A'); // .paa vs .pac

    const DecodedImage img = DecodePAABuffer(fileData.data(), static_cast<size_t>(size), isPaa);
    if (!img.valid())
    {
        LOG_WARN(Graphics, "MTL: texture decode failed: {}", name);
        return false;
    }

    _w = img.width;
    _h = img.height;

    const AlphaStats stats = ClassifyAlpha(img.rgba.data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
    _isAlpha = stats.kind != AlphaStats::Opaque;
    _isTransparent = stats.kind == AlphaStats::Cutout;

    _gpuHandle = bootstrap.CreateTexture(_w, _h, img.rgba.data());
    if (_gpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: texture GPU upload failed: {}", name);
        return false;
    }
    _rgba = std::move(img.rgba);
    return true;
}

bool TextureMTL::InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba)
{
    if (rgba == nullptr)
        return false;

    _w = w;
    _h = h;
    _gpuHandle = bootstrap.CreateTexture(w, h, static_cast<const uint8_t*>(rgba));
    if (_gpuHandle == 0)
        return false;
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _rgba.assign(bytes, bytes + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    return true;
}

void TextureMTL::UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba)
{
    if (_gpuHandle == 0 || rgba == nullptr)
        return;
    bootstrap.UpdateTexture(_gpuHandle, _w, _h, static_cast<const uint8_t*>(rgba));
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _rgba.assign(bytes, bytes + static_cast<size_t>(_w) * static_cast<size_t>(_h) * 4);
}

Color TextureMTL::GetPixel(int /*level*/, float u, float v) const
{
    if (_rgba.empty() || _w <= 0 || _h <= 0)
        return HBlack;

    int iu = static_cast<int>(std::floor(u * static_cast<float>(_w)));
    int iv = static_cast<int>(std::floor(v * static_cast<float>(_h)));
    if (iu < 0)
        iu = 0;
    if (iv < 0)
        iv = 0;
    if (iu > _w - 1)
        iu = _w - 1;
    if (iv > _h - 1)
        iv = _h - 1;

    const size_t idx = (static_cast<size_t>(iv) * static_cast<size_t>(_w) + static_cast<size_t>(iu)) * 4;
    return Color(_rgba[idx] / 255.0f, _rgba[idx + 1] / 255.0f, _rgba[idx + 2] / 255.0f, _rgba[idx + 3] / 255.0f);
}

Color TextureMTL::GetColor()
{
    if (_rgba.empty())
        return HBlack;

    const size_t pixelCount = _rgba.size() / 4;
    double r = 0, g = 0, b = 0, a = 0;
    for (size_t i = 0; i < pixelCount; i++)
    {
        r += _rgba[i * 4];
        g += _rgba[i * 4 + 1];
        b += _rgba[i * 4 + 2];
        a += _rgba[i * 4 + 3];
    }
    return Color(static_cast<float>(r / pixelCount / 255.0), static_cast<float>(g / pixelCount / 255.0),
                static_cast<float>(b / pixelCount / 255.0), static_cast<float>(a / pixelCount / 255.0));
}

} // namespace Poseidon
