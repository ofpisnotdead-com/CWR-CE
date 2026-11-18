#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


struct FT_LibraryRec_;
struct FT_FaceRec_;
namespace Poseidon { class QIStream; }
namespace Poseidon
{
using Poseidon::QIStream;

namespace ui
{

struct GlyphMetrics
{
    int width = 0;
    int height = 0;
    int bearingX = 0; // offset from pen to left edge
    int bearingY = 0; // offset from baseline to top edge
    int advance = 0;  // horizontal advance in 1/64th pixels
    // Atlas location
    int atlasX = 0;
    int atlasY = 0;
    int atlasPage = 0;
};

// Packed RGBA color
struct FontColor
{
    uint8_t r = 255, g = 255, b = 255, a = 255;
    static FontColor White() { return {255, 255, 255, 255}; }
    static FontColor Black() { return {0, 0, 0, 255}; }
    static FontColor FromFloat(float r, float g, float b, float a = 1.0f);
};

// Result of measuring text
struct TextMetrics
{
    float width = 0;  // in pixels
    float height = 0; // in pixels (line height)
};

// Atlas page — a single texture holding cached glyphs
struct AtlasPage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA (4 bytes per pixel)
    bool dirty = false;          // needs re-upload to GPU
    // Row packer state
    int cursorX = 0;
    int cursorY = 0;
    int rowHeight = 0;
};

// Key for glyph cache: codepoint + pixel size
struct GlyphKey
{
    uint32_t codepoint;
    int pixelSize;
    bool operator==(const GlyphKey& o) const { return codepoint == o.codepoint && pixelSize == o.pixelSize; }
};

struct GlyphKeyHash
{
    size_t operator()(const GlyphKey& k) const
    {
        return std::hash<uint64_t>{}((static_cast<uint64_t>(k.codepoint) << 32) | static_cast<uint32_t>(k.pixelSize));
    }
};

// Quad emitted for each glyph during text layout
struct GlyphQuad
{
    float x, y, w, h;     // screen position and size (pixels)
    float u0, v0, u1, v1; // atlas UV coordinates (0-1)
    int atlasPage;
};

struct GlyphBounds
{
    float top = 0.0f;
    float bottom = 0.0f;
};

class FontRenderer
{
  public:
    FontRenderer();
    ~FontRenderer();

    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;

    // Load a TTF/OTF font from file. Returns false on failure.
    bool LoadFont(const std::string& path);

    bool LoadFontFromStream(Poseidon::QIStream& stream);

    bool LoadFontFromMemory(const uint8_t* data, size_t size);

    // Apply a permanent 15-degree shear to subsequent glyph rasterizations.
    // Used to synthesize italic from fonts that ship only an upright cut
    // (e.g. AudreysHand).
    void SetSyntheticOblique(bool enable);

    // Apply FT_Outline_Embolden by strengthPx atlas pixels on every glyph
    // rasterized through this renderer.  0 = no thickening.  Caller is
    // expected to scope this to a renderer dedicated to this strength
    // (font.cpp uses #bold:X cache keys) so cached glyphs match the setting.
    void SetSyntheticBold(float strengthPx);

    // Check if a font is loaded
    bool IsLoaded() const { return _face != nullptr; }

    // Returns the bounds of the visible glyphs in the layout.
    GlyphBounds MeasureGlyphBounds(const std::vector<GlyphQuad>& quads) const;

    // Rasterize a single glyph into the atlas. Returns metrics.
    const GlyphMetrics* GetGlyph(uint32_t codepoint, int pixelSize);

    TextMetrics MeasureText(const char* utf8Text, int pixelSize, float widthScale = 1.0f,
                            float letterSpacing = 0.0f) const;

    // Font metrics: ascent above baseline (pixels) and descent below (positive value)
    float GetAscent(int pixelSize) const;
    float GetDescent(int pixelSize) const;
    // Cap height: actual capital letter height (for visual centering, smaller than ascent)
    float GetCapHeight(int pixelSize);

    // Build list of quads for rendering text at given pixel position.
    // Caller uses quads to issue Draw2D calls with atlas textures.
    std::vector<GlyphQuad> LayoutText(const char* utf8Text, float x, float y, int pixelSize, float widthScale = 1.0f, float letterSpacing = 0.0f);

    // Atlas access (for GPU upload)
    int GetAtlasPageCount() const { return static_cast<int>(_pages.size()); }
    const AtlasPage& GetAtlasPage(int index) const { return _pages[static_cast<size_t>(index)]; }
    void ClearAtlasDirtyFlag(int index) { _pages[static_cast<size_t>(index)].dirty = false; }

    // Atlas dimensions
    static constexpr int kAtlasWidth = 1024;
    static constexpr int kAtlasHeight = 1024;
    static constexpr int kGlyphPadding = 4;

    // UTF-8 decoding utility (public for testing)
    static uint32_t DecodeUTF8(const char*& ptr);

  private:
    const GlyphMetrics* RasterizeGlyph(uint32_t codepoint, int pixelSize);
    bool PackGlyph(int glyphW, int glyphH, int& outPage, int& outX, int& outY);

    FT_LibraryRec_* _library = nullptr;
    FT_FaceRec_* _face = nullptr;
    std::vector<uint8_t> _fontData; // kept alive for FreeType memory fonts

    std::vector<AtlasPage> _pages;
    std::unordered_map<GlyphKey, GlyphMetrics, GlyphKeyHash> _glyphCache;

    // Optional synthetic bold (FT_Outline_Embolden strength in atlas px).
    // Snapshotted once per renderer; per-glyph values use a separate
    // cache-keyed renderer (see font.cpp GetOrCreateRenderer).
    float _syntheticBoldPx = 0.0f;
};

} // namespace ui

} // namespace Poseidon
