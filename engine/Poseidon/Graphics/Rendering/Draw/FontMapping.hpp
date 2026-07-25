#pragma once

#include <string>

// FreeType TTF mapping for an engine font family.  Each prefix matches a
// family of names like `tahomaB24`, `cwrBodyB36`, etc.; the trailing digits
// are parsed at draw time for pixel size.
//
// The single shipping mapping table lives in font.cpp (the default OFL set under
// fonts/). Per-row size/stretch/spacing is tunable live via the dev font tuner.
//
// Kept in its own header (decoupled from the Font class + reference-counting
// machinery) so unit tests can include it without dragging in the world.

namespace Poseidon
{
struct FreeTypeFontMapping
{
    const char* prefix;
    const char* ttfPath;
    int bitmapMaxHeight;   // from bitmap FXY: max(glyph rect h-1), for sizeH compat
    int renderPx;          // FreeType em px that matches bitmap cap-H
    float widthScale;      // bitmap width / FreeType width at renderPx
    bool syntheticOblique; // apply 15-degree shear for italic-only families
    float baselineOffset;  // atlas-px shift applied to glyph pen Y; +=down, -=up.
                           // Use to compensate when an OFL replacement sits
                           // higher / lower than the legacy face it stands in
                           // for.  0 = no shift.
    float syntheticBold;   // FreeType FT_Outline_Embolden strength, in atlas
                           // pixels.  0 = no thickening.  Use when an OFL
                           // replacement is lighter than the bitmap reference.
    float letterSpacing;   // atlas-px added between glyph advances.
                           // - = tighter, + = looser, 0 = native kerning only.
};

// Active mapping lookup.  Returns nullptr if no prefix matches.
// `lowName` must be already lowercased (`strlwr` applied).
const FreeTypeFontMapping* FindFontMapping(const char* lowName);

// Resolve a mapping's ttfPath (e.g. "Fonts\cwr_body.ttf") to the file to open.
// An enabled mod's copy of that path wins over the base file, "" if neither exists.
std::string ResolveMappedFontPath(const char* ttfPath);

// Refresh every cached Font::_ftRenderer pointer against the mapping table.
// Call after mutating the table (e.g. via SetFontMappingTuning) — old renderers
// stay in the cache (a few MB) until process exit.
void ResetFontRenderers();

// Release all cached FreeType renderer state and any atlas textures that back
// it. Call this during engine shutdown while the TextureBank still exists.
void ClearFreeTypeCaches();

// Live-tune a single mapping row (size / stretch / baseline / bold / spacing).
// Updates the matching prefix's entry and refreshes the cached Fonts so the
// change shows up on the next frame.  Used by the dev font tuner / triFontTune
// for in-engine A/B without a rebuild.  Returns false if the prefix didn't
// match.  Optional ttfPath (nullptr to keep current) swaps the underlying TTF.
bool SetFontMappingTuning(const char* prefix, int renderPx, float widthScale, float baselineOffset, float syntheticBold,
                          float letterSpacing, const char* ttfPath);

// Dump the mapping table as a newline-separated string (prefix renderPx
// widthScale ttf).  Used by triFontDump to capture tuned values for
// transcription back into font.cpp.
const char* DumpFontTable();

} // namespace Poseidon
