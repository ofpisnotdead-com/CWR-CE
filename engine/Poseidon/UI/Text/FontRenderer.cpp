#include <utility>

namespace Poseidon
{

} // namespace Poseidon
#include <Poseidon/UI/Text/FontRenderer.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
namespace Poseidon
{

namespace ui
{

FontColor FontColor::FromFloat(float r, float g, float b, float a)
{
    return {static_cast<uint8_t>(r * 255.0f + 0.5f), static_cast<uint8_t>(g * 255.0f + 0.5f),
            static_cast<uint8_t>(b * 255.0f + 0.5f), static_cast<uint8_t>(a * 255.0f + 0.5f)};
}

uint32_t FontRenderer::DecodeUTF8(const char*& ptr)
{
    uint32_t cp = 0xFFFD;
    int consumed = DecodeUtf8Codepoint(ptr, &cp);
    if (consumed <= 0)
        consumed = 1;
    ptr += consumed;
    return cp;
}

FontRenderer::FontRenderer()
{
    ::FT_Init_FreeType(&_library);
}

FontRenderer::~FontRenderer()
{
    if (_face)
        ::FT_Done_Face(_face);
    if (_library)
        ::FT_Done_FreeType(_library);
}

void FontRenderer::SetSyntheticOblique(bool enable)
{
    if (!_face)
        return;
    if (enable)
    {
        // 15-degree shear: x' = x + 0.268 * y (tan(15deg) ~= 0.2679)
        FT_Matrix m{static_cast<FT_Fixed>(0x10000), static_cast<FT_Fixed>(0x10000 * 0.268), 0,
                    static_cast<FT_Fixed>(0x10000)};
        ::FT_Set_Transform(_face, &m, nullptr);
    }
    else
    {
        ::FT_Set_Transform(_face, nullptr, nullptr);
    }
}

void FontRenderer::SetSyntheticBold(float strengthPx)
{
    // Snapshot, applied per-glyph at RasterizeGlyph time via FT_Outline_Embolden.
    _syntheticBoldPx = strengthPx;
}

bool FontRenderer::LoadFont(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    auto size = file.tellg();
    if (size <= 0)
        return false;
    _fontData.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(_fontData.data()), size);
    return LoadFontFromMemory(_fontData.data(), _fontData.size());
}

bool FontRenderer::LoadFontFromStream(QIStream& stream)
{
    int size = stream.rest();
    if (size <= 0)
        return false;
    _fontData.resize(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(_fontData.data()), size);
    return LoadFontFromMemory(_fontData.data(), _fontData.size());
}

bool FontRenderer::LoadFontFromMemory(const uint8_t* data, size_t size)
{
    if (_face)
    {
        ::FT_Done_Face(_face);
        _face = nullptr;
    }
    _glyphCache.clear();
    _pages.clear();
    if (data != _fontData.data())
    {
        _fontData.assign(data, data + size);
    }
    FT_Error err = ::FT_New_Memory_Face(_library, _fontData.data(), static_cast<FT_Long>(_fontData.size()), 0, &_face);
    return err == 0 && _face != nullptr;
}

const GlyphMetrics* FontRenderer::GetGlyph(uint32_t codepoint, int pixelSize)
{
    GlyphKey key{codepoint, pixelSize};
    auto it = _glyphCache.find(key);
    if (it != _glyphCache.end())
        return &it->second;
    return RasterizeGlyph(codepoint, pixelSize);
}

const GlyphMetrics* FontRenderer::RasterizeGlyph(uint32_t codepoint, int pixelSize)
{
    if (!_face)
        return nullptr;

    ::FT_Set_Pixel_Sizes(_face, 0, static_cast<FT_UInt>(pixelSize));
    FT_UInt glyphIndex = ::FT_Get_Char_Index(_face, codepoint);
    if (glyphIndex == 0 && codepoint != 0)
        glyphIndex = ::FT_Get_Char_Index(_face, 0xFFFD);

    // TARGET_LIGHT: vertical-only grid fit preserves horizontal outline metrics.
    // NORMAL's 2-axis snap ghosts the top strokes of condensed display faces.
    if (::FT_Load_Glyph(_face, glyphIndex, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT) != 0)
        return nullptr;

    FT_GlyphSlot slot = _face->glyph;
    bool hasOutline = slot->format == FT_GLYPH_FORMAT_OUTLINE && slot->outline.n_points > 0;
    if (hasOutline)
    {
        // Synthetic bold: thicken (positive) or thin (negative) strokes by N
        // atlas pixels (1 px = 64 in 26.6).  Must happen before FT_Render_Glyph
        // so the rasterizer sees the (de)emboldened outline.  No-op when value
        // is essentially zero.
        if (std::fabs(_syntheticBoldPx) > 0.05f)
        {
            FT_Pos strength = static_cast<FT_Pos>(_syntheticBoldPx * 64.0f);
            ::FT_Outline_Embolden(&slot->outline, strength);
            // Embolden also offsets the metrics; reflect that in advance so
            // the next glyph doesn't visually collide with this one.
            slot->advance.x += strength;
        }
        if (::FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0)
            return nullptr;
    }
    FT_Bitmap& bmp = slot->bitmap;
    int w = hasOutline ? static_cast<int>(bmp.width) : 0;
    int h = hasOutline ? static_cast<int>(bmp.rows) : 0;

    GlyphMetrics gm;
    gm.width = w;
    gm.height = h;
    gm.bearingX = slot->bitmap_left;
    gm.bearingY = slot->bitmap_top;
    gm.advance = static_cast<int>(slot->advance.x); // 26.6 format for LayoutText

    if (w > 0 && h > 0)
    {
        int pageIdx, ax, ay;
        if (!PackGlyph(w, h, pageIdx, ax, ay))
            return nullptr;
        gm.atlasX = ax;
        gm.atlasY = ay;
        gm.atlasPage = pageIdx;

        AtlasPage& page = _pages[static_cast<size_t>(pageIdx)];
        for (int row = 0; row < h; row++)
        {
            for (int col = 0; col < w; col++)
            {
                uint8_t alpha = bmp.buffer[row * bmp.pitch + col];
                size_t offset = static_cast<size_t>((ay + row) * page.width + (ax + col)) * 4;
                page.pixels[offset + 0] = 255;
                page.pixels[offset + 1] = 255;
                page.pixels[offset + 2] = 255;
                page.pixels[offset + 3] = alpha;
            }
        }
        page.dirty = true;
    }

    GlyphKey key{codepoint, pixelSize};
    auto [it, _] = _glyphCache.emplace(key, gm);
    return &it->second;
}

bool FontRenderer::PackGlyph(int glyphW, int glyphH, int& outPage, int& outX, int& outY)
{
    int padW = glyphW + kGlyphPadding;
    int padH = glyphH + kGlyphPadding;

    // Try existing pages
    for (size_t i = 0; i < _pages.size(); i++)
    {
        AtlasPage& page = _pages[i];
        if (page.cursorX + padW <= page.width)
        {
            if (page.cursorY + std::max(page.rowHeight, padH) <= page.height)
            {
                outPage = static_cast<int>(i);
                outX = page.cursorX;
                outY = page.cursorY;
                page.cursorX += padW;
                page.rowHeight = std::max(page.rowHeight, padH);
                return true;
            }
        }
        // Try next row
        int nextY = page.cursorY + page.rowHeight;
        if (nextY + padH <= page.height && padW <= page.width)
        {
            page.cursorX = padW;
            page.cursorY = nextY;
            page.rowHeight = padH;
            outPage = static_cast<int>(i);
            outX = 0;
            outY = nextY;
            return true;
        }
    }

    // Allocate new page — white RGB + zero alpha so bilinear bleed stays correct color
    AtlasPage newPage;
    newPage.width = kAtlasWidth;
    newPage.height = kAtlasHeight;
    auto totalPixels = static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4;
    newPage.pixels.resize(totalPixels);
    for (size_t i = 0; i < totalPixels; i += 4)
    {
        newPage.pixels[i + 0] = 255;
        newPage.pixels[i + 1] = 255;
        newPage.pixels[i + 2] = 255;
        newPage.pixels[i + 3] = 0;
    }
    newPage.cursorX = padW;
    newPage.cursorY = 0;
    newPage.rowHeight = padH;
    newPage.dirty = true;
    _pages.push_back(std::move(newPage));

    outPage = static_cast<int>(_pages.size()) - 1;
    outX = 0;
    outY = 0;
    return true;
}

TextMetrics FontRenderer::MeasureText(const char* utf8Text, int pixelSize, float widthScale, float letterSpacing) const
{
    if (!_face)
        return {};

    ::FT_Set_Pixel_Sizes(_face, 0, static_cast<FT_UInt>(pixelSize));

    float totalWidth = 0;
    float maxHeight = static_cast<float>(pixelSize);

    // Space advance: prefer ' ' glyph's own advance, fall back to 'o' (some faces
    // have a space glyph with no metrics). Mirrored in LayoutText.
    const GlyphMetrics* spaceGm = const_cast<FontRenderer*>(this)->GetGlyph(' ', pixelSize);
    float spaceAdv = spaceGm && spaceGm->advance > 0 ? static_cast<float>(spaceGm->advance) / 64.0f : 0.0f;
    if (spaceAdv <= 0.0f)
    {
        const GlyphMetrics* oMeas = const_cast<FontRenderer*>(this)->GetGlyph('o', pixelSize);
        spaceAdv = oMeas && oMeas->advance > 0 ? static_cast<float>(oMeas->advance) / 64.0f
                                               : static_cast<float>(pixelSize) * 0.3f;
    }
    spaceAdv *= widthScale;

    const char* p = utf8Text;
    while (*p)
    {
        uint32_t cp = DecodeUTF8(p);
        if (cp == ' ')
        {
            totalWidth += spaceAdv;
            continue;
        }
        const GlyphMetrics* gm = const_cast<FontRenderer*>(this)->GetGlyph(cp, pixelSize);
        if (gm)
            totalWidth += static_cast<float>(gm->advance) / 64.0f * widthScale + letterSpacing;
    }
    return {totalWidth, maxHeight};
}

float FontRenderer::GetAscent(int pixelSize) const
{
    if (!_face)
        return static_cast<float>(pixelSize);
    ::FT_Set_Pixel_Sizes(_face, 0, static_cast<FT_UInt>(pixelSize));
    return static_cast<float>(_face->size->metrics.ascender) / 64.0f;
}

float FontRenderer::GetDescent(int pixelSize) const
{
    if (!_face)
        return 0.0f;
    ::FT_Set_Pixel_Sizes(_face, 0, static_cast<FT_UInt>(pixelSize));
    return static_cast<float>(-_face->size->metrics.descender) / 64.0f;
}

float FontRenderer::GetCapHeight(int pixelSize)
{
    // Returns actual capital letter height (bearingY of 'H') for visual centering.
    const GlyphMetrics* gm = GetGlyph('H', pixelSize);
    if (gm && gm->bearingY > 0)
        return static_cast<float>(gm->bearingY);
    return GetAscent(pixelSize);
}

GlyphBounds FontRenderer::MeasureGlyphBounds(const std::vector<GlyphQuad>& quads) const
{
    if (quads.empty())
        return {};

    GlyphBounds bounds;
    bounds.top = quads[0].y;
    bounds.bottom = quads[0].y + quads[0].h;

    for (const GlyphQuad& q : quads)
    {
        bounds.top = std::min(bounds.top, q.y);
        bounds.bottom = std::max(bounds.bottom, q.y + q.h);
    }

    return bounds;
}

std::vector<GlyphQuad> FontRenderer::LayoutText(const char* utf8Text, float x, float y, int pixelSize, float widthScale,
                                                float letterSpacing)
{
    std::vector<GlyphQuad> quads;
    if (!_face)
        return quads;

    float penX = x;
    float penY = y;

    // advance is 26.6 fixed-point; see MeasureText for the ' '/'o' fallback rationale.
    const GlyphMetrics* spaceGm = const_cast<FontRenderer*>(this)->GetGlyph(' ', pixelSize);
    float spaceAdv = spaceGm && spaceGm->advance > 0 ? static_cast<float>(spaceGm->advance) / 64.0f : 0.0f;
    if (spaceAdv <= 0.0f)
    {
        const GlyphMetrics* oGlyph = const_cast<FontRenderer*>(this)->GetGlyph('o', pixelSize);
        spaceAdv = oGlyph && oGlyph->advance > 0 ? static_cast<float>(oGlyph->advance) / 64.0f
                                                 : static_cast<float>(pixelSize) * 0.3f;
    }
    spaceAdv *= widthScale;

    const char* p = utf8Text;
    while (*p)
    {
        uint32_t cp = DecodeUTF8(p);
        if (cp == ' ')
        {
            penX += spaceAdv;
            continue;
        }
        const GlyphMetrics* gm = const_cast<FontRenderer*>(this)->GetGlyph(cp, pixelSize);
        if (!gm)
            continue;

        if (gm->width > 0 && gm->height > 0)
        {
            const AtlasPage& page = _pages[static_cast<size_t>(gm->atlasPage)];
            float invW = 1.0f / static_cast<float>(page.width);
            float invH = 1.0f / static_cast<float>(page.height);

            // Glyph bitmap origin: (penX + bearingX, penY - bearingY).
            GlyphQuad q;
            q.x = penX + static_cast<float>(gm->bearingX) * widthScale;
            q.y = penY - static_cast<float>(gm->bearingY);
            q.w = static_cast<float>(gm->width) * widthScale;
            q.h = static_cast<float>(gm->height);
            q.u0 = static_cast<float>(gm->atlasX) * invW;
            q.v0 = static_cast<float>(gm->atlasY) * invH;
            q.u1 = static_cast<float>(gm->atlasX + gm->width) * invW;
            q.v1 = static_cast<float>(gm->atlasY + gm->height) * invH;
            q.atlasPage = gm->atlasPage;
            quads.push_back(q);
        }
        penX += static_cast<float>(gm->advance) / 64.0f * widthScale + letterSpacing;
    }
    return quads;
}

} // namespace ui

} // namespace Poseidon
