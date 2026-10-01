#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Rendering/Draw/Font.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/UI/Text/FontRenderer.hpp>
#include <Poseidon/UI/Text/ScreenTextLayout.hpp>
#include <unordered_map>
#include <vector>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

void PrepareTexture(Texture* texture, float z2, int special, float areaOTex);

// Leaked on purpose: static destruction order is undefined across TUs, and
// releasing Ref<Texture> after the engine's TextureBank is gone crashes at
// exit. Same rationale as the stringtable callback registry.
static std::unordered_map<Poseidon::ui::FontRenderer*, std::vector<Ref<Texture>>>& GetAtlasMap()
{
    static auto* s_ftAtlasMap = new std::unordered_map<Poseidon::ui::FontRenderer*, std::vector<Ref<Texture>>>();
    return *s_ftAtlasMap;
}

void ClearFreeTypeAtlasTextures()
{
    GetAtlasMap().clear();
}

// Detect a Texture whose backing GPU surface has been freed (e.g.
// after TextBankGL33::ForceReloadAll dropped every handle in the
// bank for an F5 hot-reload).  The CPU-side atlas pixels in
// FontRenderer's pages are still valid, but our cached Ref<Texture>
// points at a dead GPU handle — UpdateDynamic on that would write to
// it and the next bind samples zero alpha (= invisible text).
// Texture::IsGpuValid() is backend-specific (TextureGL33/TextureMTL
// each know their own handle); this used to reach in via a hardcoded
// static_cast<TextureGL33*>, which is undefined behavior under any
// other backend (e.g. TextureMTL) sharing this code path.
static bool IsTextureGpuValid(Texture* tex)
{
    return tex && tex->IsGpuValid();
}

static void SyncAtlasTextures(Engine* engine, Poseidon::ui::FontRenderer* fr)
{
    auto& textures = GetAtlasMap()[fr];
    int pageCount = fr->GetAtlasPageCount();
    if (static_cast<int>(textures.size()) < pageCount)
        textures.resize(static_cast<size_t>(pageCount));

    for (int i = 0; i < pageCount; i++)
    {
        const auto& page = fr->GetAtlasPage(i);

        // If the cached Ref<Texture> survives but its GPU handle was
        // freed underneath us, drop the cache entry so we recreate.
        // Without this, a paused atlas with no new glyphs since last
        // sync (page.dirty == false) takes the `continue` branch and
        // never re-uploads — so text vanishes after F5 reload.
        if (textures[i] && !IsTextureGpuValid(textures[i]))
            textures[i] = nullptr;

        if (!page.dirty && textures[i])
            continue;

        uint32_t dataSize = static_cast<uint32_t>(page.pixels.size());
        // Mipmap off: per-draw bucketing keeps atlas ~1:1 with screen, so mip 0 bilinear
        // is crisp — and GL's LOD picker on tilted UI surfaces would otherwise blur.
        if (!textures[i])
            textures[i] = engine->TextBank()->CreateDynamic(page.width, page.height, page.pixels.data(), dataSize,
                                                            /*mipmap*/ false);
        else
            engine->TextBank()->UpdateDynamic(textures[i], page.pixels.data(), dataSize);
        fr->ClearAtlasDirtyFlag(i);
    }
}

void Engine::DrawTextFreeType(const Point2DAbs& pos, float sizeEx, const Rect2DAbs& clip, Font* font, PackedColor color,
                              const char* text)
{
    AspectSettings as;
    GetAspectSettings(as);

    Poseidon::ui::FontRenderer* fr = font->FTRenderer();
    if (!fr || font->FTReferencePx() <= 0)
        return;

    Poseidon::ui::ScreenTextBaseScale baseScale;
    if (!Poseidon::ui::ComputeScreenTextBaseScale(Height2D(), Width(), Height(), as.topFOV, as.leftFOV, font->Height(),
                                                  sizeEx, &baseScale))
        return;

    int pixelSize = font->FTPixelSizeForSizeH(baseScale.sizeH);
    Poseidon::ui::ScreenTextScale screenScale;
    if (!Poseidon::ui::FinalizeFreeTypeScreenTextScale(
            baseScale, font->FTReferencePx(), pixelSize, font->FTWidthScale(),
            fr->GetAscent(pixelSize) + font->FTBaselineOffset(), &screenScale, font->FTLetterSpacing()))
        return;

    auto quads = Poseidon::ui::LayoutScreenText(*fr, screenScale, text, pos.x, pos.y);

    SyncAtlasTextures(this, fr);

    Draw2DPars pars;
    pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    pars.SetColor(color);

    auto& textures = GetAtlasMap()[fr];
    for (const auto& q : quads)
    {
        if (q.atlasPage >= static_cast<int>(textures.size()) || !textures[q.atlasPage])
            continue;

        MipInfo mip = TextBank()->UseMipmap(textures[q.atlasPage], 0, 0);
        if (!mip.IsOK())
            continue;

        pars.mip = mip;
        pars.SetU(q.u0, q.u1);
        pars.SetV(q.v0, q.v1);

        Draw2D(pars, Rect2DAbs(q.x, q.y, q.w, q.h), clip);
    }
}

void Engine::DrawTextFreeType3D(Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip, Font* font,
                                PackedColor color, int spec, const char* text, float x1c, float y1c, float x2c,
                                float y2c)
{
    Poseidon::ui::FontRenderer* fr = font->FTRenderer();
    if (!fr || font->FTReferencePx() <= 0)
        return;

    // Camera::Adjust divides camera-space Y by cTop before perspective divide,
    // so pixels_per_world = Height / (2 * cTop * z) — no cNear factor.
    Camera* cam = GScene->GetCamera();
    float dist = (pos - cam->Position()).Size();
    if (dist < 0.01f)
        dist = 0.01f;
    float cTop = cam->Top();
    float cLeft = cam->Left();
    float pixelsPerWorld = (cTop > 1e-6f) ? static_cast<float>(Height()) / (2.0f * dist * cTop) : 1.0f;
    float sizeHEq = (up.Size() * pixelsPerWorld) / static_cast<float>(font->MaxHeight());

    // Snap text start to integer screen pixel — unsnapped world→screen projection
    // otherwise lands consecutive C3DListBox rows at different sub-pixel Y and
    // bilinear sampling turns that into a visible soft band.
    Vector3 posSnapped = pos;
    {
        Matrix4 invT = cam->GetInvTransform();
        Vector3 viewPos = invT.FastTransform(pos);
        if (viewPos.Z() > 1e-3f && cTop > 1e-6f && cLeft > 1e-6f)
        {
            float w = static_cast<float>(Width());
            float h = static_cast<float>(Height());
            float screenX = (viewPos.X() / (cLeft * viewPos.Z()) + 1.0f) * 0.5f * w;
            float screenY = (1.0f - viewPos.Y() / (cTop * viewPos.Z())) * 0.5f * h;
            float dxPx = floorf(screenX + 0.5f) - screenX;
            float dyPx = floorf(screenY + 0.5f) - screenY;
            // Screen Y grows down; camera local +Y is up, hence the sign flip.
            float worldPerPxX = 2.0f * cLeft * viewPos.Z() / w;
            float worldPerPxY = 2.0f * cTop * viewPos.Z() / h;
            Vector3 localDelta(dxPx * worldPerPxX, -dyPx * worldPerPxY, 0);
            posSnapped = pos + cam->Rotate(localDelta);
        }
    }

    int pixelSize = font->FTPixelSizeForSizeH(sizeHEq);
    float renderScale = static_cast<float>(font->FTReferencePx()) / static_cast<float>(pixelSize);

    float ascent = fr->GetAscent(pixelSize) + font->FTBaselineOffset();
    auto quads = fr->LayoutText(text, 0, ascent, pixelSize, font->FTWidthScale(), font->FTLetterSpacing());
    if (quads.empty())
        return;

    SyncAtlasTextures(this, fr);
    auto& textures = GetAtlasMap()[fr];

    // renderScale in invMH keeps world geometry invariant of pixelSize bucketing.
    float invMH = renderScale / static_cast<float>(font->_maxHeight);

    static Ref<ObjectColored> objText3DFT;
    if (!objText3DFT)
    {
        Ref<LODShapeWithShadow> lShape = new LODShapeWithShadow();
        Shape* shape = new Shape;
        lShape->AddShape(shape, 0);

        shape->ReallocTable(4);
        const ClipFlags initClip = ClipAll;
        shape->SetClip(0, initClip);
        shape->SetClip(1, initClip);
        shape->SetClip(2, initClip);
        shape->SetClip(3, initClip);
        shape->SetNorm(0) = VUp;
        shape->SetNorm(1) = VUp;
        shape->SetNorm(2) = VUp;
        shape->SetNorm(3) = VUp;
        shape->CalculateHints();
        lShape->CalculateHints();

        Poly face;
        face.Init();
        face.SetN(4);
        face.Set(0, 0);
        face.Set(1, 1);
        face.Set(2, 3);
        face.Set(3, 2);
        face.SetSpecial(ClampU | ClampV);
        shape->AddFace(face);
        shape->OrSpecial(IsAlpha | IsAlphaFog | NoZWrite);
        PoseidonAssert(shape->NPos() == 4);
        PoseidonAssert(shape->NNorm() == 4);
        PoseidonAssert(shape->NFaces() == 1);

        lShape->OrSpecial(IsColored | IsOnSurface);

        objText3DFT = new ObjectColored(lShape, -1);
        objText3DFT->SetOrientation(M3Identity);
    }

    objText3DFT->SetConstantColor(color);
    objText3DFT->SetSpecial(spec);
    LODShape* lShape = objText3DFT->GetShape();
    Shape* shape = lShape->Level(0);
    for (int i = 0; i < shape->NVertex(); i++)
    {
        shape->SetClip(i, clip | ClipAll);
    }
    shape->CalculateHints();

    float z2 = GScene->GetCamera()->Position().Distance2(pos);

    Vector3 normal = dir.CrossProduct(up).Normalized();

    for (const auto& q : quads)
    {
        if (q.atlasPage >= static_cast<int>(textures.size()) || !textures[q.atlasPage])
            continue;

        // LayoutText origin is top-left, +y grows down — matches the legacy bitmap
        // 3D em convention (1 em = _maxHeight world units along up).
        float emX = q.x * invMH;
        float emY = q.y * invMH;
        float emW = q.w * invMH;
        float emH = q.h * invMH;

        float gx0 = emX;
        float gx1 = emX + emW;
        float gy0 = emY;
        float gy1 = emY + emH;

        if (gx1 <= x1c || gx0 >= x2c || gy1 <= y1c || gy0 >= y2c)
            continue;

        float u0 = q.u0, u1 = q.u1, v0 = q.v0, v1 = q.v1;
        float cx0 = gx0, cx1 = gx1, cy0 = gy0, cy1 = gy1;

        if (cx0 < x1c)
        {
            float t = (x1c - gx0) / emW;
            u0 = q.u0 + t * (q.u1 - q.u0);
            cx0 = x1c;
        }
        if (cx1 > x2c)
        {
            float t = (gx1 - x2c) / emW;
            u1 = q.u1 - t * (q.u1 - q.u0);
            cx1 = x2c;
        }
        if (cy0 < y1c)
        {
            float t = (y1c - gy0) / emH;
            v0 = q.v0 + t * (q.v1 - q.v0);
            cy0 = y1c;
        }
        if (cy1 > y2c)
        {
            float t = (gy1 - y2c) / emH;
            v1 = q.v1 - t * (q.v1 - q.v0);
            cy1 = y2c;
        }

        Poly& face = shape->Face(shape->BeginFaces());

        Texture* texture = textures[q.atlasPage].GetRef();
        if (texture)
        {
            float area = dir.CrossProduct(up).Size();
            float uvArea = (u1 - u0) * (v1 - v0);
            float areaOTex = uvArea > 0 ? area / uvArea : area;
            PrepareTexture(texture, z2, spec, areaOTex);
        }

        face.SetTexture(texture);

        // +dir rightward, -up downward (em-space y grows down).
        shape->SetPos(0) = dir * cx0 - up * cy0;
        shape->SetPos(1) = dir * cx1 - up * cy0;
        shape->SetPos(2) = dir * cx0 - up * cy1;
        shape->SetPos(3) = dir * cx1 - up * cy1;

        shape->SetNorm(0) = normal;
        shape->SetNorm(1) = normal;
        shape->SetNorm(2) = normal;
        shape->SetNorm(3) = normal;

        shape->SetUV(0, u0, v0);
        shape->SetUV(1, u1, v0);
        shape->SetUV(2, u0, v1);
        shape->SetUV(3, u1, v1);
        shape->FindSections();

        lShape->SetAutoCenter(false);
        lShape->CalculateMinMax(true);
        objText3DFT->SetPosition(posSnapped);
        objText3DFT->Draw(0, clip & ClipAll, *objText3DFT);
    }
}

float Engine::GetText3DWidthFreeType(Font* font, const char* text)
{
    Poseidon::ui::FontRenderer* fr = font->FTRenderer();
    if (!fr || font->_maxHeight <= 0 || font->FTReferencePx() <= 0)
        return 0.0f;
    // Measure at refPx; the em-fraction result is pixelSize-independent.
    auto tm = fr->MeasureText(text, font->FTReferencePx(), font->FTWidthScale());
    return tm.width / static_cast<float>(font->_maxHeight);
}
} // namespace Poseidon
