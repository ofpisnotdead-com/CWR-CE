#include <Poseidon/World/World.hpp>
#include <Poseidon/Game/TitEffects.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <limits.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using Poseidon::Foundation::EnumName;

using namespace Poseidon;
namespace Poseidon
{
RString FindShape(RString name);
} // namespace Poseidon

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(TitEffectName dummy)
{
    static const EnumName TitEffectNameNames[] = {EnumName(TitPlain, "PLAIN"),
                                                  EnumName(TitPlainDown, "PLAIN DOWN"),
                                                  EnumName(TitBlack, "BLACK"),
                                                  EnumName(TitBlackFaded, "BLACK FADED"),
                                                  EnumName(TitBlackOut, "BLACK OUT"),
                                                  EnumName(TitBlackIn, "BLACK IN"),
                                                  EnumName(TitWhiteOut, "WHITE OUT"),
                                                  EnumName(TitWhiteIn, "WHITE IN"),

                                                  EnumName()};
    return TitEffectNameNames;
}

class TitleEffectTimed : public TitleEffect
{
    typedef TitleEffect base;

  protected:
    float _speed; // all times are scaled relative to this speed

    float _timeToLive;

    float _fadeInTime; // before time to live
    float _fadeInTotal;
    float _fadeOutTime; // after time to live
    float _fadeOutTotal;

    float _alpha;

  public:
    TitleEffectTimed();

    void SetTimes(float fadeIn, float live, float fadeOut);
    void Simulate(float deltaT) override;
    bool IsTerminated() const override;
    void SetSpeed(float speed) { _speed = speed; }

    void Terminate() override;
    void Prolong(float time) override;
};

void TitleEffectTimed::Terminate()
{
    _timeToLive = 0;
}

void TitleEffectTimed::Prolong(float time)
{
    if (_timeToLive > time)
    {
        return; // no need to prolong
    }
    if (_fadeOutTime >= _fadeOutTotal)
    { // fade out not started yet
        _timeToLive = time;
    }
    else if (_fadeOutTime < time)
    {
        // keep alpha unchanged
        _fadeOutTime = time;
        saturateMax(_alpha, 0.2);
        _fadeOutTotal = _fadeOutTime / _alpha;
    }
}

TitleEffectTimed::TitleEffectTimed()
{
    _speed = 1;

    _timeToLive = 0;
    _fadeInTime = 0;
    _fadeOutTime = 0;
    _alpha = 0;
}

void TitleEffectTimed::SetTimes(float fadeIn, float live, float fadeOut)
{
    _fadeInTime = _fadeInTotal = fadeIn;
    _fadeOutTime = _fadeOutTotal = fadeOut;
    _timeToLive = live;
    _alpha = 0;
}

bool TitleEffectTimed::IsTerminated() const
{
    if (_fadeInTime > 0)
    {
        return false;
    }
    if (_timeToLive > 0)
    {
        return false;
    }
    if (_fadeOutTime > 0)
    {
        return false;
    }
    return true;
}

void TitleEffectTimed::Simulate(float deltaT)
{
    PoseidonAssert(deltaT >= 0);

    deltaT /= _speed;
    if (_fadeInTime > 0)
    {
        _fadeInTime -= deltaT;
        _alpha = 1 - _fadeInTime / _fadeInTotal;
    }
    else if (_timeToLive > 0)
    {
        _timeToLive -= deltaT;
        _alpha = 1;
    }
    else
    {
        _fadeOutTime -= deltaT;
        _alpha = _fadeOutTime / _fadeOutTotal;
    }
    saturate(_alpha, 0, 1);
}

class TitleEffectBasic : public TitleEffectTimed
{
    typedef TitleEffectTimed base;

    AutoArray<RString> _texts;
    Ref<Font> _textFont;
    float _textSize;
    float _textYOffset;

    Ref<Object> _object;
    Vector3 _objectCamera;

    Ref<Display> _rsc;
    RefArray<LODShapeWithShadow> _rscOverlayShapes;
    bool _rscOverlayPillarbox = false;
    float _yOffset;

    void DrawText();
    void DrawObject();
    void DrawRsc();

  public:
    // type specific init
    virtual void Init(RString text);
    virtual void Init(const ParamEntry& rsc);
    virtual void Init(LODShapeWithShadow* shape, Vector3Val camera);

    // common init
    virtual void Init(float time);

    void SetTextPos(float yOffset);
    void SetTextFont(Ref<Font> font, float size);
    void SetFade(float in, float out);

    void Draw() override;
};

class DisplayTitle : public Display
{
    typedef Display base;

  public:
    DisplayTitle();
};

DisplayTitle::DisplayTitle() : Display(nullptr)
{
    SetCursor(nullptr);
}

const float DefaultIn = 2;
const float DefaultOut = 1;

void TitleEffectBasic::SetTextPos(float yOffset)
{
    _textYOffset = yOffset;
}
void TitleEffectBasic::SetTextFont(Ref<Font> font, float size)
{
    _textFont = font;
    _textSize = size;
}
void TitleEffectBasic::SetFade(float in, float out)
{
    _fadeInTime = _fadeInTotal = in;
    _fadeOutTime = _fadeOutTotal = out;
}

void TitleEffectBasic::Init(float time)
{
    SetTimes(DefaultIn, time, DefaultOut);
}

void SplitTitleLines(RString text, AutoArray<RString>& out)
{
    out.Clear();
    if (text.GetLength() > 0)
    {
        const char* start = text;
        const char* ptr = strstr(start, "\\n");
        while (ptr)
        {
            out.Add(text.Substring(start - text, ptr - text));
            start = ptr + 2;
            ptr = strstr(start, "\\n");
        }
        out.Add(text.Substring(start - text, INT_MAX));
    }
}

TitleShadowOffset SubtitleShadowOffset(int width2D, int height2D)
{
    // Equal pixel drop on both axes: offsetX*width2D == offsetY*height2D.
    const float offsetY = 0.002f;
    const float offsetX = width2D > 0 ? offsetY * float(height2D) / float(width2D) : offsetY;
    return {offsetX, offsetY};
}

void TitleEffectBasic::Init(RString text)
{
    text = DecodeLegacyTextToRString(text, GLanguage);
    SplitTitleLines(text, _texts);

    const ParamEntry& cls = Res >> "RscTitlesText";
    _textFont = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontBasic"));
    const ParamEntry* entry = cls.FindEntry("sizeBasic");
    if (entry)
    {
        _textSize = (float)(*entry) * _textFont->Height();
    }
    else
    {
        _textSize = cls >> "sizeExBasic";
    }
    _textYOffset = 0;
}

void TitleEffectBasic::Init(const ParamEntry& rsc)
{
    const ParamEntry* controls = rsc.FindEntry("controls");
    const ParamEntry* objects = rsc.FindEntry("objects");
    if (!controls && objects)
    {
        for (int i = 0; i < objects->GetSize(); ++i)
        {
            RString objectName = (*objects)[i];
            const ParamEntry* object = rsc.FindEntry(objectName);
            const ParamEntry* model = object ? object->FindEntry("model") : nullptr;
            const ParamEntry* position = object ? object->FindEntry("position") : nullptr;
            if (!model || !position || position->GetSize() < 3)
                continue;

            const float z = (*position)[2];
            if (z > 0.25f)
                continue;

            Ref<LODShapeWithShadow> shape = Shapes.New(Poseidon::FindShape(*model), true, false);
            if (shape)
            {
                shape->OrSpecial(BestMipmap | NoDropdown);
                _rscOverlayShapes.Add(shape);
            }
        }
        if (_rscOverlayShapes.Size() > 0)
        {
            _rscOverlayPillarbox = true;
            return;
        }
    }

    _rsc = new DisplayTitle;
    _rsc->Load(rsc);
}

void TitleEffectBasic::Init(LODShapeWithShadow* shape, Vector3Val camera)
{
    _object = new Object(shape, -1);
    _objectCamera = camera;
}

void TitleEffectBasic::DrawText()
{
    float top = 0.5 + _textYOffset;
    PackedColor fColor(Color(1, 1, 1, _alpha));
    const TitleShadowOffset shadow = SubtitleShadowOffset(GEngine->Width2D(), GEngine->Height2D());
    for (int i = 0; i < _texts.Size(); i++)
    {
        RString text = _texts[i];
        float width = GEngine->GetTextWidth(_textSize, _textFont, text);
        float x = 0.5 - width * 0.5;
        PackedColor bColor(Color(0, 0, 0, _alpha * 0.3));
        GEngine->DrawText(Point2DFloat(x + shadow.x, top + shadow.y), _textSize, _textFont, bColor, text);
        GEngine->DrawText(Point2DFloat(x, top), _textSize, _textFont, fColor, text);
        top += _textSize;
    }
}

void TitleEffectBasic::DrawObject()
{
    if (!GScene->GetCamera())
    {
        return;
    }
    // temporary override camera
    Camera oldCam = *GScene->GetCamera();
    Camera cam;
    cam.SetPosition(_objectCamera);

    float fov = 0.7;
    float cNear = -_objectCamera.Z() * 0.5;
    saturate(cNear, 0.01, 100);
    cam.SetPerspectiveForView(GEngine, cNear, GScene->GetFogMaxRange(), fov);
    cam.Adjust(GEngine);
    GScene->SetCamera(cam);
    PackedColor color(Color(1, 1, 1, _alpha));
    _object->SetConstantColor(color);
    _object->Draw(0, ClipAll, *_object);
    // restore camera
    GScene->SetCamera(oldCam);
}

void TitleEffectBasic::DrawRsc()
{
    if (_rscOverlayShapes.Size() > 0)
    {
        // 4:3 model overlay — preserve 4:3 + pillarbox while bars are on, else stretch.
        const bool preserve4x3 = AspectRatio::ArePillarboxBarsEnabled();
        for (int i = 0; i < _rscOverlayShapes.Size(); ++i)
            Object::Draw2D(_rscOverlayShapes[i], 0, PackedWhite, /*preserveAspect4x3*/ preserve4x3);
        if (_rscOverlayPillarbox)
            Object::DrawWidescreenPillarbox(/*requireGameplayActive*/ false);
        return;
    }
    _rsc->DrawHUD(nullptr, _alpha);
}

void TitleEffectBasic::Draw()
{
    if (_object)
    {
        DrawObject();
    }
    if (_rsc || _rscOverlayShapes.Size() > 0)
    {
        DrawRsc();
    }
    if (_texts.Size() > 0)
    {
        DrawText();
    }
}

const float DefaultTime = 10;

class TitleEffectPlain : public TitleEffectBasic
{
    typedef TitleEffectBasic base;
};

class TitleEffectPlainDown : public TitleEffectBasic
{
    typedef TitleEffectBasic base;

  public:
    void Init(RString text) override;
};

void TitleEffectPlainDown::Init(RString text)
{
    base::Init(text);

    const ParamEntry& cls = Res >> "RscTitlesText";
    Ref<Font> font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontDown"));
    float size;
    const ParamEntry* entry = cls.FindEntry("sizeDown");
    if (entry)
    {
        size = (float)(*entry) * font->Height();
    }
    else
    {
        size = cls >> "sizeExDown";
    }
    SetTextFont(font, size);
    SetTextPos(0.25);
}

class TitleEffectBlackFaded : public TitleEffectBasic
{
    typedef TitleEffectBasic base;
    PackedColor _color;

  public:
    TitleEffectBlackFaded(PackedColor color = PackedBlack);
    void Draw() override;
    bool IsTransparent() const override { return _alpha >= 0.99; }
};

TitleEffectBlackFaded::TitleEffectBlackFaded(PackedColor color) : _color(color) {}

void TitleEffectBlackFaded::Draw()
{
    Texture* texture = GScene->Preloaded(TextureWhite);
    Draw2DPars pars;
    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    int alphaI = (int)(_alpha * 255);
    saturate(alphaI, 0, 255);
    PackedColor color = PackedColorRGB(_color, alphaI);
    pars.SetColor(color);
    pars.spec = NoZBuf | IsAlpha | NoClamp | IsAlphaFog;
    pars.SetU(0, 1);
    pars.SetV(0, 1);
    Rect2DAbs rect;
    rect.x = 0, rect.y = 0;
    rect.w = GEngine->Width(), rect.h = GEngine->Height();
    GEngine->Draw2D(pars, rect);
    base::Draw();
}

class TitleEffectColorOut : public TitleEffectBlackFaded
{
    typedef TitleEffectBlackFaded base;

  public:
    TitleEffectColorOut(PackedColor color);
    void Init(float time) override { SetTimes(1, 1e20, 1); }
};

TitleEffectColorOut::TitleEffectColorOut(PackedColor color) : base(color)
{
    SetTimes(1, 1e10, 1);
}

class TitleEffectColorIn : public TitleEffectBlackFaded
{
    typedef TitleEffectBlackFaded base;

  public:
    TitleEffectColorIn(PackedColor color);
    void Init(float time) override { SetTimes(0, 0, 1); }
};

TitleEffectColorIn::TitleEffectColorIn(PackedColor color) : base(color)
{
    SetTimes(0, 0, 1);
}

class TitleEffectBlackIn : public TitleEffectColorIn
{
  public:
    TitleEffectBlackIn() : TitleEffectColorIn(PackedBlack) {}
};
class TitleEffectBlackOut : public TitleEffectColorOut
{
  public:
    TitleEffectBlackOut() : TitleEffectColorOut(PackedBlack) {}
};
class TitleEffectWhiteIn : public TitleEffectColorIn
{
  public:
    TitleEffectWhiteIn() : TitleEffectColorIn(PackedWhite) {}
};
class TitleEffectWhiteOut : public TitleEffectColorOut
{
  public:
    TitleEffectWhiteOut() : TitleEffectColorOut(PackedWhite) {}
};

class TitleEffectBlack : public TitleEffectBlackFaded
{
    typedef TitleEffectBlackFaded base;

  public:
    void Init();
};

void TitleEffectBlack::Init()
{
    SetFade(0, 0);
}

static TitleEffectBasic* CreateEffect(TitEffectName name)
{
    switch (name)
    {
        case TitBlack:
        case TitBlackOut:
            return new TitleEffectBlackOut;
        case TitBlackIn:
            return new TitleEffectBlackIn;
        case TitWhiteOut:
            return new TitleEffectWhiteOut;
        case TitWhiteIn:
            return new TitleEffectWhiteIn;
        case TitBlackFaded:
            return new TitleEffectBlack;
        case TitPlain:
            return new TitleEffectPlain;
        case TitPlainDown:
            return new TitleEffectPlainDown;
    }
    return nullptr;
}

static void SetupEffect(TitEffectName name, TitleEffectBasic* effect)
{
    if (!effect)
    {
        return;
    }
    effect->Simulate(0);
}

TitleEffect* CreateTitleEffectObj(TitEffectName name, const ParamEntry& entry, float speed)
{
    RString shapeName = GetShapeName(entry >> "model");
    Ref<LODShapeWithShadow> shape = Shapes.New(shapeName, false, false);
    shape->OrSpecial(IsColored | IsAlphaFog | IsAlpha);
    Vector3 camera;
    const ParamEntry& camEntry = entry >> "camera";
    camera.Init();
    camera[0] = camEntry[0];
    camera[1] = camEntry[1];
    camera[2] = camEntry[2];
    float duration = entry >> "duration";
    TitleEffectBasic* result = CreateEffect(name);
    result->Init(duration);
    result->Init(shape, camera);
    result->SetSpeed(speed);
    SetupEffect(name, result);
    return result;
}

TitleEffect* CreateTitleEffect(TitEffectName name, RString text, float speed, Ref<Font> font, float size)
{
    TitleEffectBasic* result = CreateEffect(name);
    result->Init(DefaultTime);
    result->Init(text);
    if (font)
    {
        result->SetTextFont(font, size);
    }
    result->SetSpeed(speed);
    SetupEffect(name, result);
    return result;
}

TitleEffect* CreateTitleEffectRsc(TitEffectName name, const ParamEntry& entry, float speed)
{
    TitleEffectBasic* result = CreateEffect(name);
    float duration = entry >> "duration";
    float fadeIn = DefaultIn;
    if (entry.FindEntry("fadeIn"))
    {
        fadeIn = entry >> "fadeIn";
    }
    float fadeOut = DefaultOut;
    if (entry.FindEntry("fadeOut"))
    {
        fadeIn = entry >> "fadeOut";
    }

    result->Init(duration);
    result->Init(entry);
    result->SetSpeed(speed);
    result->SetFade(fadeIn, fadeOut);
    SetupEffect(name, result);
    return result;
}
