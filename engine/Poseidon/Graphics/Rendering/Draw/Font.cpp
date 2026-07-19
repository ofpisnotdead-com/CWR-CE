#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Draw/Font.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontData.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <stdarg.h>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/UI/Text/FontRenderer.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontSystem.hpp>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

// The single shipping font set. The cwr* rows are the engine fonts; the trailing
// rows are legacy RESOURCE.BIN font-name aliases kept so old config references
// still resolve to the current faces. Hand = Caveat, a single face covering
// both Latin and Cyrillic, so Latin and Russian handwriting share it
// (ru_audreyshand is just an alias for it).
//
// Per-row tuning (renderPx / widthScale / baselineOffset / syntheticBold /
// letterSpacing) is editable live via the F8 ImGui tuner; commit values from
// triFontDump back into this table when satisfied.
static FreeTypeFontMapping s_fontTable[] = {
    {"cwrtitle", "fonts\\cwr_title.ttf", 59, 46, 0.628f, false, -6.0f, -1.5f, 0.0f},
    {"cwrbody", "fonts\\cwr_body.ttf", 12, 10, 1.000f, false, 0.0f, 0.0f, 0.0f},
    {"cwrmono", "fonts\\cwr_mono.ttf", 29, 28, 0.800f, false, 0.0f, -0.6f, 0.0f},
    {"cwrserif", "fonts\\cwr_serif.ttf", 29, 24, 0.935f, false, 0.0f, 0.0f, 0.0f},
    {"cwrhand", "fonts\\cwr_hand.ttf", 27, 21, 1.050f, false, -1.60f, -0.90f, 0.0f},
    {"ru_audreyshand", "fonts\\cwr_hand.ttf", 27, 21, 1.050f, false, -1.60f, -0.90f, 0.0f},
    {"steelfishb", "fonts\\cwr_title.ttf", 59, 46, 0.628f, false, -6.0f, -1.5f, 0.0f},
    {"impact", "fonts\\cwr_title.ttf", 59, 46, 0.628f, false, -6.0f, -1.5f, 0.0f},
    {"tahomab", "fonts\\cwr_body.ttf", 12, 10, 1.000f, false, 0.0f, 0.0f, 0.0f},
    {"fontmaincz", "fonts\\cwr_body.ttf", 12, 10, 1.000f, false, 0.0f, 0.0f, 0.0f},
    {"couriernewb", "fonts\\cwr_mono.ttf", 29, 28, 0.800f, false, 0.0f, -0.6f, 0.0f},
    {"garamond", "fonts\\cwr_serif.ttf", 29, 24, 0.935f, false, 0.0f, 0.0f, 0.0f},
    {"audreyshand", "fonts\\cwr_hand.ttf", 27, 21, 1.050f, false, -1.60f, -0.90f, 0.0f},
};
static const size_t s_fontTableCount = sizeof(s_fontTable) / sizeof(s_fontTable[0]);

static const char* const s_langPrefixes[] = {"cz_", "ru_", "pl_"};

const FreeTypeFontMapping* FindFontMapping(const char* lowName)
{
    auto findIndex = [&](const char* name) -> int
    {
        for (size_t i = 0; i < s_fontTableCount; i++)
            if (strncmp(name, s_fontTable[i].prefix, strlen(s_fontTable[i].prefix)) == 0)
                return static_cast<int>(i);
        return -1;
    };

    int idx = findIndex(lowName);
    if (idx < 0)
    {
        // Retry with cz_/ru_/pl_ stripped — keeps Czech / Russian font names
        // routing to the correct face.
        const char* stripped = lowName;
        for (const auto* p : s_langPrefixes)
        {
            if (strncmp(stripped, p, strlen(p)) == 0)
            {
                stripped += strlen(p);
                break;
            }
        }
        if (stripped == lowName)
            return nullptr;
        idx = findIndex(stripped);
        if (idx < 0)
            return nullptr;
    }

    return &s_fontTable[idx];
}

bool SetFontMappingTuning(const char* prefix, int renderPx, float widthScale, float baselineOffset, float syntheticBold,
                          float letterSpacing, const char* ttfPath)
{
    if (!prefix)
        return false;
    FreeTypeFontMapping* table = s_fontTable;
    const size_t n = s_fontTableCount;
    for (size_t i = 0; i < n; i++)
    {
        if (strcmp(table[i].prefix, prefix) == 0)
        {
            table[i].renderPx = renderPx;
            table[i].widthScale = widthScale;
            table[i].baselineOffset = baselineOffset;
            table[i].syntheticBold = syntheticBold;
            table[i].letterSpacing = letterSpacing;
            if (ttfPath && *ttfPath)
            {
                // Buffer the path so the row's ttfPath stays valid after this
                // function returns.  Per-row static slot is fine since each row
                // is mutated independently.
                static char s_pathBuf[32][256];
                size_t row = i;
                if (row >= 32)
                    row = 31;
                strncpy(s_pathBuf[row], ttfPath, sizeof(s_pathBuf[row]) - 1);
                s_pathBuf[row][sizeof(s_pathBuf[row]) - 1] = 0;
                table[i].ttfPath = s_pathBuf[row];
            }
            ResetFontRenderers();
            return true;
        }
    }
    return false;
}

const char* DumpFontTable()
{
    static char s_buf[8192];
    int pos = 0;
    pos += snprintf(s_buf + pos, sizeof(s_buf) - pos, "font table:\n");
    const FreeTypeFontMapping* table = s_fontTable;
    const size_t n = s_fontTableCount;
    for (size_t i = 0; i < n && pos < (int)sizeof(s_buf) - 256; i++)
    {
        pos +=
            snprintf(s_buf + pos, sizeof(s_buf) - pos, "  {\"%s\", \"%s\", %d, %d, %.3ff, %s, %.2ff, %.2ff, %.2ff},\n",
                     table[i].prefix, table[i].ttfPath, table[i].bitmapMaxHeight, table[i].renderPx,
                     table[i].widthScale, table[i].syntheticOblique ? "true" : "false", table[i].baselineOffset,
                     table[i].syntheticBold, table[i].letterSpacing);
    }
    return s_buf;
}

bool HasFreeTypeFontMapping(const char* lowName)
{
    return lowName && FindFontMapping(lowName) != nullptr;
}

void ClearFreeTypeAtlasTextures();

static int ParseTrailingSize(const char* name, const char* prefix)
{
    const char* digits = name + strlen(prefix);
    int size = atoi(digits);
    return size > 0 ? size : 24;
}

Font::Font()
{
    _nChars = 0;
    _langID = English;
}

Font::~Font()
{
    if (GLOB_ENGINE)
        GLOB_ENGINE->FontDestroyed(this);
    DoDestruct();
}

void Font::DoDestruct() {}

static std::unordered_map<std::string, std::unique_ptr<ui::FontRenderer>>& GetFTRenderers()
{
    static std::unordered_map<std::string, std::unique_ptr<ui::FontRenderer>> renderers;
    return renderers;
}

static std::string ResolveMappedFontPath(const char* ttfPath)
{
    if (!ttfPath || !*ttfPath)
        return {};

    QIFStreamB stream;
    stream.AutoOpen(ttfPath);
    if (stream.rest() > 0)
        return ttfPath;

    const char* baseName = strrchr(ttfPath, '\\');
    if (!baseName)
        baseName = strrchr(ttfPath, '/');
    baseName = baseName ? baseName + 1 : ttfPath;

    char flatPath[256];
    snprintf(flatPath, sizeof(flatPath), "fonts\\%s", baseName);
    stream.AutoOpen(flatPath);
    if (stream.rest() > 0)
        return flatPath;

    stream.AutoOpen(baseName);
    if (stream.rest() > 0)
        return baseName;

    return {};
}

void ResetFontRenderers()
{
    // Don't drop the renderer cache — every cached Font holds a raw _ftRenderer
    // pointer into it and would dangle on the next text draw.  Instead, refresh
    // each Font so its _ftRenderer is re-resolved against the active mapping
    // table.  Renderers from the previously-active table stay alive in the
    // cache (a few MB) until process exit — fine for a dev-only toggle.
    if (GLOB_ENGINE)
        GLOB_ENGINE->RefreshAllFonts();
}

void ClearFreeTypeCaches()
{
    ClearFreeTypeAtlasTextures();
    // Cached Font objects hold raw pointers into this renderer map, and some UI
    // objects keep Font refs across world remounts.  Dropping the renderers here
    // leaves those refs with dangling FreeType faces on the next draw.  Keep the
    // renderers process-lifetime; ResetFontRenderers refreshes mappings without
    // invalidating existing Font objects.
}

static ui::FontRenderer* GetOrCreateRenderer(const char* ttfPath, bool syntheticOblique, float syntheticBold)
{
    std::string resolvedPath = ResolveMappedFontPath(ttfPath);
    if (resolvedPath.empty())
        return nullptr;

    auto& renderers = GetFTRenderers();
    // Cache key includes oblique flag AND bold strength so an upright + a
    // synthesized-italic face on the same TTF keep separate atlases, and
    // multiple bold variants of the same face also stay separate.  Bold is
    // rounded to 1 decimal so dragging the slider doesn't churn renderers.
    char boldKey[32] = "";
    if (std::fabs(syntheticBold) > 0.05f)
        snprintf(boldKey, sizeof(boldKey), "#bold:%.1f", syntheticBold);
    std::string key = resolvedPath + (syntheticOblique ? "#oblique" : "") + boldKey;
    auto it = renderers.find(key);
    if (it != renderers.end())
        return it->second.get();

    QIFStreamB stream;
    stream.AutoOpen(resolvedPath.c_str());
    if (stream.rest() <= 0)
        return nullptr;

    auto renderer = std::make_unique<ui::FontRenderer>();
    if (!renderer->LoadFontFromStream(stream))
        return nullptr;
    renderer->SetSyntheticOblique(syntheticOblique);
    renderer->SetSyntheticBold(syntheticBold);

    auto* ptr = renderer.get();
    renderers[key] = std::move(renderer);
    return ptr;
}

void Font::Load(const char* name)
{
    strcpy(_name, name);
    strlwr(_name);

    // strip directory prefix for mapping lookup
    const char* baseName = strrchr(_name, '\\');
    baseName = baseName ? baseName + 1 : _name;

    // FreeType eligibility depends only on whether a slot mapping +
    // TTF exists on disk — never on init-time order.  Gating on
    // FontSystem::IsAvailable() here meant fonts created before
    // FontSystem::Initialize() ran (e.g. ProgressSystem's font during
    // InitializeWorld, before InitializeSubsystems) were permanently
    // routed through the .fxy bitmap atlas, which walks the input
    // string byte-by-byte (fontDraw.cpp:86) — UTF-8 multibyte
    // sequences then drew two glyphs per codepoint.  Now Font::Load
    // always tries FreeType first; the gate only controls log noise
    // for apps that opted out of text rendering.
    if (!AppConfig::Instance().NoFreeType())
    {
        auto* mapping = FindFontMapping(baseName);
        if (mapping)
        {
            auto* renderer = GetOrCreateRenderer(mapping->ttfPath, mapping->syntheticOblique, mapping->syntheticBold);
            if (renderer)
            {
                _isFreeType = true;
                _ftRenderer = renderer;
                _ftReferencePx = mapping->renderPx;
                _maxHeight = mapping->bitmapMaxHeight;
                _ftWidthScale = mapping->widthScale;
                _ftBaselineOffset = mapping->baselineOffset;
                _ftLetterSpacing = mapping->letterSpacing;
                _nChars = 0;
                return;
            }
        }
    }

    char fontName[256];
    QIFStreamB f;
    snprintf(fontName, sizeof(fontName), "%s.fxy", _name);
    f.AutoOpen(fontName);
    if (f.rest() > 0)
    {
        FXYData data = ParseFXY(f, _name);

        _isFreeType = false;
        _ftRenderer = nullptr;
        _ftReferencePx = 0;
        _ftWidthScale = 1.0f;
        _nChars = data.nChars;
        _infos.Realloc(_nChars);
        _infos.Resize(_nChars);
        _maxHeight = data.maxHeight;

        for (int i = 0; i < _nChars && i < static_cast<int>(data.glyphs.size()); i++)
        {
            const FXYGlyph& glyph = data.glyphs[static_cast<size_t>(i)];
            CharInfo& info = _infos[i];
            info.nameTex = glyph.textureName.c_str();
            info.xTex = glyph.x;
            info.yTex = glyph.y;
            info.width = glyph.w;
            info.height = glyph.h;
            info.wTex = glyph.wTex;
            info.hTex = glyph.hTex;
        }
        return;
    }

    // Only loud when FontSystem is up — apps that opted out of text
    // rendering deliberately get empty Fonts without log noise.
    if (FontSystem::Instance().IsAvailable())
        LOG_ERROR(Graphics, "No modern font mapping or .fxy fallback for '{}'", _name);
    _isFreeType = false;
    _ftRenderer = nullptr;
    _ftReferencePx = 0;
    _ftWidthScale = 1.0f;
    _maxHeight = 0;
    _nChars = 0;
    _infos.Clear();
}

float Font::Height() const
{
    // normalized height (fraction of screen height)
    return _maxHeight * (1.0 / 600);
}

int BucketFTPixelSize(float idealPx)
{
    int bucketed = static_cast<int>((idealPx + 2.0f) / 4.0f) * 4;
    if (bucketed < 8)
        bucketed = 8;
    if (bucketed > 160)
        bucketed = 160;
    return bucketed;
}

int Font::FTPixelSizeForSizeH(float sizeH) const
{
    if (!_isFreeType || _ftReferencePx <= 0)
        return _ftReferencePx;
    return BucketFTPixelSize(sizeH * static_cast<float>(_ftReferencePx));
}

Font* FontCache::Load(FontID id)
{
    char lowName[256];
    snprintf(lowName, sizeof(lowName), "%s", (const char*)id.name), strlwr(lowName);
    int i;
    for (i = 0; i < _fonts.Size(); i++)
    {
        Font* font = _fonts[i];
        if (font && !strcmp(font->Name(), lowName))
        {
            DoAssert(font->GetLangID() == id.langID);
            return font;
        }
    }
    // add new font
    Font* font = new Font;
    PoseidonAssert(font);
    font->Load(lowName);
    font->SetLangID(id.langID);
    _fonts.Add(font);
    return font;
}

Font* Engine::LoadFont(FontID id)
{
    // search for loaded fonts
    return _fonts.Load(id);
}

Texture* FontCache::Load(Font* font, RStringB texName)
{
    // check font info
    // search cache
    for (int i = 0; i < _lastChars.Size(); i++)
    {
        CachedChar& item = _lastChars[i];
        PoseidonAssert(item._font);
        if (item._font != font)
        {
            continue;
        }
        if (item._c != texName)
        {
            continue;
        }
        Texture* texture = item._texture;
        // move item forward
        CachedChar copy = item;
        _lastChars.Delete(i);
        _lastChars.Add(copy);
        return texture;
    }

    // check if letter exists
    Ref<Texture> texture;
    if (QIFStreamB::FileExist(texName))
    {
        texture = GLOB_ENGINE->TextBank()->Load(texName);
        if (texture)
        {
            texture->SetMipmapRange(0, 0);
        }
    }
    else
    {
        LOG_DEBUG(Graphics, "Missing file {}", (const char*)texName);
        texture = GetDefaultTexture();
    }
    //  not in cache - add
    CachedChar item;
    item._font = font;
    item._texture = texture;
    item._c = texName;
    const int maxChars = 256;
    if (_lastChars.Size() > maxChars)
    {
        _lastChars.Delete(0);
    }
    _lastChars.Add(item);
    return texture;
}

void FontCache::RemoveFont(Font* font) {}

void FontCache::Clear()
{
    _fonts.Clear();
    PoseidonAssert(_fonts.Size() == 0);
    _lastChars.Clear();
}

void FontCache::RefreshAllFonts()
{
    // Re-run Font::Load on every cached Font so _ftRenderer / _ftReferencePx /
    // _ftWidthScale pick up the active mapping table.  Char texture cache
    // (_lastChars) keyed by Font* + texName stays valid — pointers don't move.
    for (int i = 0; i < _fonts.Size(); i++)
    {
        Font* font = _fonts[i];
        if (font)
            font->Load(font->Name());
    }
}
} // namespace Poseidon
