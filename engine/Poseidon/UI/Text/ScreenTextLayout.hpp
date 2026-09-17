#pragma once

#include <Poseidon/UI/Text/FontRenderer.hpp>
#include <Poseidon/UI/LayoutCanvas.hpp>


namespace Poseidon
{
namespace ui
{

struct ScreenTextBaseScale
{
    float sizeH = 0.0f;
    float sizeW = 0.0f;
};

struct ScreenTextScale
{
    int pixelSize = 0;
    float renderScale = 1.0f;
    float drawH = 0.0f;
    float drawW = 0.0f;
    float ascent = 0.0f;
    float widthScale = 1.0f;
    float letterSpacing = 0.0f;
};

inline bool ComputeScreenTextBaseScale(float viewportHeight2D, int surfaceWidthPx, int surfaceHeightPx, float topFOV,
                                       float leftFOV, float legacyFontHeight, float sizeEx, ScreenTextBaseScale* out)
{
    if (!out)
        return false;
    *out = {};
    if (viewportHeight2D <= 0.0f || surfaceWidthPx <= 0 || surfaceHeightPx <= 0 || topFOV <= 0.0f || leftFOV <= 0.0f ||
        legacyFontHeight <= 0.0f)
        return false;

    out->sizeH = viewportHeight2D * sizeEx * (1.0f / LayoutCanvas::kHeight) / legacyFontHeight;
    out->sizeW = out->sizeH * static_cast<float>(surfaceWidthPx) * topFOV /
                 (static_cast<float>(surfaceHeightPx) * leftFOV);
    return true;
}

inline bool FinalizeFreeTypeScreenTextScale(const ScreenTextBaseScale& base, int referencePx, int pixelSize,
                                            float widthScale, float ascent, ScreenTextScale* out,
                                            float letterSpacing = 0.0f)
{
    if (!out)
        return false;
    *out = {};
    if (base.sizeH <= 0.0f || base.sizeW <= 0.0f || referencePx <= 0 || pixelSize <= 0)
        return false;

    out->pixelSize = pixelSize;
    out->renderScale = static_cast<float>(referencePx) / static_cast<float>(pixelSize);
    out->drawH = base.sizeH * out->renderScale;
    out->drawW = base.sizeW * out->renderScale;
    out->ascent = ascent;
    out->widthScale = widthScale;
    out->letterSpacing = letterSpacing;
    return true;
}

inline float MeasureScreenTextWidth(const FontRenderer& renderer, const ScreenTextScale& scale, float viewportWidth2D,
                                    const char* text)
{
    if (viewportWidth2D <= 0.0f || !text || !*text)
        return 0.0f;

    TextMetrics metrics = renderer.MeasureText(text, scale.pixelSize, scale.widthScale, scale.letterSpacing);
    return metrics.width * scale.drawW / viewportWidth2D;
}

inline std::vector<GlyphQuad> LayoutScreenText(FontRenderer& renderer, const ScreenTextScale& scale, const char* text,
                                               float originX, float originY)
{
    if (!text || !*text)
        return {};

    std::vector<GlyphQuad> quads = renderer.LayoutText(text, 0.0f, scale.ascent, scale.pixelSize, scale.widthScale,
                                                       scale.letterSpacing);
    for (GlyphQuad& q : quads)
    {
        q.x = originX + q.x * scale.drawW;
        q.y = originY + q.y * scale.drawH;
        q.w *= scale.drawW;
        q.h *= scale.drawH;
    }
    return quads;
}

} // namespace ui

} // namespace Poseidon
