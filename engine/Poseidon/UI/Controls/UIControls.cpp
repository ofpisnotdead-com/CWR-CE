#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/UI/Controls/IosKeyboardAccessory.hpp>
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>

using namespace Poseidon;
namespace Poseidon
{

} // namespace Poseidon
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/UI/Locale/AutoComplete.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <SDL3/SDL.h>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

namespace Poseidon
{
RString FindPicture(RString name);
RString FindShape(RString name);
} // namespace Poseidon
void DrawFrame(Texture* corner, PackedColor color, const Rect2DPixel& frame);
// for listbox and combobox
#define SCROLL_SPEED 100.0
#define SCROLL_MIN 2.0
#define SCROLL_MAX 10.0

#define ISSPACE(c) ((c) >= 0 && (c) <= 32)

#define CX(x) (toInt((x) * w) + 0.5)
#define CY(y) (toInt((y) * h) + 0.5)

#define DrawBottom(i, color) \
    GLOB_ENGINE->DrawLine(Line2DPixel(xx + i, yy + hh - 1 - i, xx + ww - i, yy + hh - 1 - i), color, color);
#define DrawRight(i, color) \
    GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 1 - i, yy + hh - 1 - i, xx + ww - 1 - i, yy + 0 + i), color, color);
#define DrawLeft(i, color) GLOB_ENGINE->DrawLine(Line2DPixel(xx + i, yy + hh - 1 - i, xx + i, yy + i), color, color);
#define DrawTop(i, color) GLOB_ENGINE->DrawLine(Line2DPixel(xx + i, yy + i, xx + ww - 1 - i, yy + i), color, color);

static RString DecodeControlText(RString text)
{
    return Poseidon::DecodeLegacyTextToRString(text, GLanguage);
}

const float textBorder = 0.005;

CStatic::CStatic(ControlsContainer* parent, const ParamEntry& cls, float x, float y, float w, float h, RString text)
    : Control(parent, CT_STATIC, cls >> "idc", cls >> "style", x, y, w, h)
{
    _enabled = ((_style & ST_TYPE) == ST_MULTI);
    _texture = nullptr;
    _bgColor = GetPackedColor(cls >> "colorBackground");
    _ftColor = GetPackedColor(cls >> "colorText");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }
    _firstLine = 0;
    _lineSpacing = 1;
    InitTextShadow(cls);
    SetText(text);

    InitColors();
}

CStatic::CStatic(ControlsContainer* parent, int idc, const ParamEntry& cls) : Control(parent, CT_STATIC, idc, cls)
{
    _texture = nullptr;
    _bgColor = GetPackedColor(cls >> "colorBackground");
    _ftColor = GetPackedColor(cls >> "colorText");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }
    _firstLine = 0;
    _enabled = false;
    _lineSpacing = 1;
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        _enabled = true;
        _lineSpacing = cls >> "lineSpacing";
    }
    InitTextShadow(cls);

    RString text = cls >> "text";
    SetText(text);
    _textCls = &cls; // set after SetText (which clears it)

    InitColors();
}

void CStatic::InitColors()
{
    const ParamEntry& clsColors = Pars >> "CfgWrapperUI" >> "Colors";
    _color1 = GetPackedColor(clsColors >> "color1");
    _color2 = GetPackedColor(clsColors >> "color2");
    _color3 = GetPackedColor(clsColors >> "color3");
    _color4 = GetPackedColor(clsColors >> "color4");
    _color5 = GetPackedColor(clsColors >> "color5");
    switch (_style & ST_TYPE)
    {
        case ST_BACKGROUND:
            _alpha = Pars >> "CfgWrapperUI" >> "Background" >> "alpha";
            break;
        case ST_GROUP_BOX:
            _alpha = Pars >> "CfgWrapperUI" >> "GroupBox" >> "alpha";
            break;
        case ST_GROUP_BOX2:
            _alpha = Pars >> "CfgWrapperUI" >> "GroupBox2" >> "alpha";
            break;
        case ST_TITLE_BAR:
            _alpha = Pars >> "CfgWrapperUI" >> "TitleBar" >> "alpha";
            break;
        default:
            _alpha = 1;
            break;
    }
}

bool CStatic::SetText(RString text)
{
    const int type = _style & ST_TYPE;
    const RString visibleText = (type == ST_PICTURE || type == ST_TILE_PICTURE) ? text : DecodeControlText(text);
    if (_text && !strcmp(_text, visibleText))
    {
        return false;
    }

    // Explicit SetText diverges the control from its resource text; stop
    // auto-refreshing from _textCls on subsequent language changes.
    _textCls = nullptr;

    _text = visibleText;
    if (type == ST_PICTURE || type == ST_TILE_PICTURE)
    {
        text = Poseidon::FindPicture(text);
        text.Lower();
        _texture = GlobLoadTexture(text);
        if (_texture)
        {
            _texture->SetMaxSize(1024); // no limits
        }
    }
    else if (type == ST_MULTI)
    {
        FormatText();
        float height = _scale * _size;
        float maxLine = NLines() - _h / height;
        saturateMax(maxLine, 0);
        saturate(_firstLine, 0, toIntCeil(maxLine));
    }
    return true;
}

bool CStatic::SetText(const char* text)
{
    // avoid reallocation if possible
    if (GetText() && !strcmp(GetText(), text))
    {
        return false;
    }
    return SetText(RString(text));
}

static float AspectCorrectTextShadowOffsetX(float offsetY)
{
    const int width = GLOB_ENGINE->Width2D();
    const int height = GLOB_ENGINE->Height2D();
    if (width <= 0 || height <= 0)
    {
        // Legacy 4:3 authored ratio: 0.075 / 0.1.
        return offsetY * 0.75f;
    }
    return offsetY * float(height) / float(width);
}

void CStatic::InitTextShadow(const ParamEntry& cls)
{
    _shadowOffsetYRatio = 0.1f;
    _shadowOffsetXRatio = 0.075f;
    _shadowOffsetXAspectCorrect = false;

    const ParamEntry* shadowOffsetY = cls.FindEntry("shadowOffsetY");
    const ParamEntry* shadowOffsetX = cls.FindEntry("shadowOffsetX");
    if (shadowOffsetY)
    {
        _shadowOffsetYRatio = *shadowOffsetY;
        if (shadowOffsetX)
        {
            _shadowOffsetXRatio = *shadowOffsetX;
        }
        else
        {
            _shadowOffsetXAspectCorrect = true;
        }
    }
    else if (shadowOffsetX)
    {
        _shadowOffsetXRatio = *shadowOffsetX;
    }
}

int CStatic::NLines() const
{
    return (_style & ST_TYPE) == ST_MULTI ? _lines.Size() : 1;
}

float CStatic::GetTextHeight() const
{
    return NLines() * _scale * _size;
}

bool CStatic::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (NLines() <= 1)
    {
        return false;
    }

    switch (nChar)
    {
        case SDLK_UP:
            _firstLine--;
            saturateMax(_firstLine, 0);
            return true;
        case SDLK_DOWN:
            _firstLine++;
            float height = _scale * _size;
            float maxLine = NLines() - _h / height;
            saturateMin(_firstLine, toIntCeil(maxLine));
            return true;
    }
    return false;
}

void CStatic::FormatText()
{
    _lines.Clear();

    float lineWidth = _w - 2 * _scale * textBorder;
    float size = _scale * _size;

    const char* p = _text;
    while (*p != 0)
    {
        // begin of the line
        _lines.Add(p - (const char*)_text);
        const char* word = p;
        int n = 0;
        float width = 0;
        while (true)
        {
            const char* q = p;
            char c = *p++;
            if (c == 0)
            {
                return;
            }
            if (c == '\\' && *p == 'n')
            {
                p++;
                break;
            }
            if (ISSPACE(c))
            {
                n++;
                word = p;
            }

            char temp[8];
            p = _text + CopyTextElement(_text, static_cast<int>(q - (const char*)_text), _font->GetLangID(), temp,
                                        sizeof(temp));
            width += GLOB_ENGINE->GetTextWidth(size, _font, temp);

            if (width > lineWidth)
            {
                if (n > 0)
                {
                    p = word;
                    break;
                }
                else
                {
                    p = q;
                    break;
                }
            }
        }
    }
}

inline PackedColor ModAlpha(PackedColor color, float alpha)
{
    int a = toInt(alpha * color.A8());
    saturate(a, 0, 255);
    return PackedColorRGB(color, a);
}

RString CStatic::GetLine(int i) const
{
    if ((_style & ST_TYPE) != ST_MULTI)
    {
        return "";
    }
    if (i < 0)
    {
        return "";
    }
    if (i >= NLines())
    {
        return "";
    }

    int from = _lines[i];
    int to = i + 1 < NLines() ? _lines[i + 1] : strlen(_text);
    return _text.Substring(from, to);
}

void CStatic::OnDraw(float alpha)
{
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float xx = toInt(_x * w);
    float yy = toInt(_y * h);
    float ww = toInt((_w + _x) * w) - xx;
    float hh = toInt((_h + _y) * h) - yy;

    xx += 0.5;
    yy += 0.5;

    int type = _style & ST_TYPE;

    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    PackedColor bgColor = ModAlpha(_bgColor, alpha);

    float size = _scale * _size;

    switch (type)
    {
        case ST_HUD_BACKGROUND:
        {
            Texture* corner = GLOB_SCENE->Preloaded(Corner);
            DrawFrame(corner, bgColor, Rect2DPixel(xx, yy, ww, hh));
            break;
        }
        case ST_BACKGROUND:
        {
            // background
            Texture* background = GLOB_SCENE->Preloaded(DialogBackground);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
            float invH = 1, invW = 1;
            if (background)
            {
                invH = 1.0 / background->AHeight();
                invW = 1.0 / background->AWidth();
            }
            Rect2DPixel rect;
            rect.w = ww;
            rect.h = hh;
            rect.x = xx;
            rect.y = yy;
            Draw2DPars pars;
            pars.mip = mip;
            pars.SetColor(bgColor);
            pars.spec = NoZBuf | IsAlpha | NoClamp | IsAlphaFog;
            pars.SetU(0, ww * invW);
            pars.SetV(0, hh * invH);
            GLOB_ENGINE->Draw2D(pars, rect);
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();

            // frame
            PackedColor bgColor1 = ModAlpha(_color1, alpha * _alpha);
            PackedColor bgColor2 = ModAlpha(_color2, alpha * _alpha);
            PackedColor bgColor3 = ModAlpha(_color3, alpha * _alpha);
            PackedColor bgColor4 = ModAlpha(_color4, alpha * _alpha);
            PackedColor bgColor5 = ModAlpha(_color5, alpha * _alpha);

            DrawLeft(0, bgColor5);
            DrawTop(0, bgColor5);
            DrawLeft(1, bgColor4);
            DrawTop(1, bgColor4);
            DrawLeft(2, bgColor3);
            DrawTop(2, bgColor3);
            DrawLeft(3, bgColor3);
            DrawTop(3, bgColor3);
            DrawLeft(4, bgColor2);
            DrawTop(4, bgColor2);
            DrawLeft(5, bgColor1);
            DrawTop(5, bgColor1);

            DrawBottom(0, bgColor1);
            DrawRight(0, bgColor1);
            DrawBottom(1, bgColor2);
            DrawRight(1, bgColor2);
            DrawBottom(2, bgColor3);
            DrawRight(2, bgColor3);
            DrawBottom(3, bgColor3);
            DrawRight(3, bgColor3);
            DrawBottom(4, bgColor4);
            DrawRight(4, bgColor4);
            DrawBottom(5, bgColor5);
            DrawRight(5, bgColor5);
            break;
        }
        case ST_FRAME:
        {
            if (_text.GetLength() > 0)
            {
                float width = GLOB_ENGINE->GetTextWidth(size, _font, _text);
                float heigth = size;
                const float border = _scale * 0.01;
                if (border + width + border <= _w)
                {
                    GLOB_ENGINE->DrawText(Point2DFloat(_x + border, _y), size, Rect2DFloat(_x, _y, _w, _h), _font,
                                          ftColor, _text);
                    float top = yy + 0.5 * heigth * h;
                    GLOB_ENGINE->DrawLine(Line2DPixel(xx, top, xx + border * w, top), ftColor, ftColor);
                    GLOB_ENGINE->DrawLine(Line2DPixel(xx + (border + width) * w, top, xx + ww - 1, top), ftColor,
                                          ftColor);
                    GLOB_ENGINE->DrawLine(Line2DPixel(xx, top, xx, yy + hh - 1), ftColor, ftColor);
                    GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 1, top, xx + ww - 1, yy + hh - 1), ftColor, ftColor);
                    DrawBottom(0, ftColor);
                    return;
                }
            }

            DrawLeft(0, ftColor);
            DrawTop(0, ftColor);
            DrawBottom(0, ftColor);
            DrawRight(0, ftColor);
            return;
        }
        case ST_WITH_RECT:
        {
            DrawLeft(0, ftColor);
            DrawTop(0, ftColor);
            DrawBottom(0, ftColor);
            DrawRight(0, ftColor);
            break;
        }
        case ST_LINE:
        {
            GLOB_ENGINE->DrawLine(Line2DPixel(xx, yy, xx + ww, yy + hh), ftColor, ftColor);
            break;
        }
        case ST_GROUP_BOX:
        {
            PackedColor grpColor2 = ModAlpha(_color2, alpha * _alpha);
            PackedColor grpColor3 = ModAlpha(_color3, alpha * _alpha);
            PackedColor grpColor4 = ModAlpha(_color4, alpha * _alpha);

            Texture* background = GLOB_SCENE->Preloaded(TextureWhite);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
            GLOB_ENGINE->Draw2D(mip, grpColor3, Rect2DPixel(xx + 1, yy + 1, ww - 2, hh - 2));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();

            DrawLeft(0, grpColor2);
            DrawTop(0, grpColor2);
            DrawBottom(0, grpColor4);
            DrawRight(0, grpColor4);
            break;
        }
        case ST_GROUP_BOX2:
        {
            PackedColor grpColor2 = ModAlpha(_color2, alpha * _alpha);
            PackedColor grpColor3 = ModAlpha(_color3, alpha * _alpha);
            PackedColor grpColor4 = ModAlpha(_color4, alpha * _alpha);

            Texture* background = GLOB_SCENE->Preloaded(DialogGroup);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
            GLOB_ENGINE->Draw2D(mip, bgColor, Rect2DPixel(xx + 1, yy + 1, ww - 2, hh - 2));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();

            DrawLeft(0, grpColor2);
            DrawTop(0, grpColor2);
            DrawBottom(0, grpColor4);
            DrawRight(0, grpColor4);
            break;
        }
        case ST_PICTURE:
        case ST_TILE_PICTURE:
        {
            if (_texture)
            {
                MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(_texture, 0, 0);
                GLOB_ENGINE->Draw2D(mip, ftColor, Rect2DPixel(xx, yy, ww, hh));
                // GLOB_ENGINE->TextBank()->ReleaseMipmap();
            }
            return; // do not draw any text (text field content texture name)
        }
        case ST_TITLE_BAR:
        {
            PackedColor barColor2 = ModAlpha(_color2, alpha * _alpha);
            PackedColor barColor3 = ModAlpha(_color3, alpha * _alpha);
            PackedColor barColor4 = ModAlpha(_color4, alpha * _alpha);

            Texture* background = GLOB_SCENE->Preloaded(TextureWhite);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
            GLOB_ENGINE->Draw2D(mip, barColor3, Rect2DPixel(xx + 5, yy + 1, ww - 10, hh - 2));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + 5, yy, xx + ww - 5, yy), barColor4, barColor4);
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 8, yy + 5, xx + 7, yy + 5), barColor2, barColor2);
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + 7, yy + 5, xx + 7, yy + hh - 6), barColor2, barColor2);
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + 7, yy + hh - 6, xx + ww - 7, yy + hh - 6), barColor4, barColor4);
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 8, yy + hh - 6, xx + ww - 8, yy + 5), barColor4, barColor4);
            GLOB_ENGINE->DrawLine(Line2DPixel(xx + 5, yy + hh - 1, xx + ww - 5, yy + hh - 1), barColor2, barColor2);
            break;
        }
        default:
        {
            if (bgColor.A8() > 0)
            {
                Texture* texture = GLOB_SCENE->Preloaded(TextureWhite);
                MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
                GLOB_ENGINE->Draw2D(mip, bgColor, Rect2DPixel(xx, yy, ww, hh));
            }
            break;
        }
    }

    if (!IsText())
    {
        return;
    }

    float height = size * _lineSpacing;
    if (type == ST_MULTI)
    {
        if (IsFocused() && (_style & ST_NO_RECT) == 0)
        {
            DrawLeft(0, ftColor);
            DrawTop(0, ftColor);
            DrawBottom(0, ftColor);
            DrawRight(0, ftColor);
        }

        int fl = toIntFloor(_firstLine);
        float top = _y - (_firstLine - fl) * height;
        for (int i = fl; i < _lines.Size(); i++)
        {
            RString text = GetLine(i);
            char* p = text.MutableData();
            while ((p = strchr(p, '\\')) != nullptr)
            {
                if (*(++p) == 'n')
                {
                    *(p - 1) = 0;
                    break;
                }
            }
            DrawText(text, top, alpha);
            top += height;
            if (top >= _y + _h)
            {
                break;
            }
        }
    }
    else
    {
        float top = _y + 0.5 * (_h - height);
        DrawText(_text, top, alpha);
    }
}

bool CStatic::IsText()
{
    return _text.GetLength() > 0;
}

void CStatic::DrawText(const char* text, float top, float alpha)
{
    int pos = _style & ST_HPOS;
    float size = _scale * _size;
    float left;
    bool vertical = false;
    const float height = size;
    const float offsetX =
        (_shadowOffsetXAspectCorrect ? AspectCorrectTextShadowOffsetX(_shadowOffsetYRatio) : _shadowOffsetXRatio) *
        height;
    const float offsetY = _shadowOffsetYRatio * height;

    switch (pos)
    {
        case ST_UP:
            left = _x + 0.5 * (_w - height);
            top = _y + _scale * textBorder;
            vertical = true;
            break;
        case ST_VCENTER:
            left = _x + 0.5 * (_w - height);
            top = _y + 0.5 * (_h - GLOB_ENGINE->GetTextWidth(size, _font, text));
            vertical = true;
            break;
        case ST_DOWN:
            left = _x + 0.5 * (_w - height);
            top = _y + _h - GLOB_ENGINE->GetTextWidth(size, _font, text) - _scale * textBorder;
            vertical = true;
            break;
        case ST_RIGHT:
            left = _x + _w - GLOB_ENGINE->GetTextWidth(size, _font, text) - _scale * textBorder;
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - GLOB_ENGINE->GetTextWidth(size, _font, text));
            break;
        default:
            PoseidonAssert(pos == ST_LEFT) left = _x + _scale * textBorder;
            break;
    }

    bool shadow = (_style & ST_SHADOW) != 0;

    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    PackedColor shadowColor = PackedColor(Color(0, 0, 0, alpha));

    if (vertical)
    {
        if (shadow)
        {
            GLOB_ENGINE->DrawTextVertical(Point2DFloat(left + offsetX, top + offsetY), size,
                                          Rect2DFloat(_x, _y, _w, _h), _font, shadowColor, text);
        }
        GLOB_ENGINE->DrawTextVertical(Point2DFloat(left, top), size, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, text);
    }
    else
    {
        if (shadow)
        {
            GLOB_ENGINE->DrawText(Point2DFloat(left + offsetX, top + offsetY), size, Rect2DFloat(_x, _y, _w, _h), _font,
                                  shadowColor, text);
        }
        GLOB_ENGINE->DrawText(Point2DFloat(left, top), size, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, text);
    }
}

bool CStaticTime::IsText()
{
    return (_style & ST_TYPE) != ST_MULTI;
}

void CStaticTime::DrawText(const char* text, float top, float alpha)
{
    float time = _time.GetTimeOfDay();
    if (time > 1.0)
    {
        time--;
    }
    time *= 24;
    int hour = toIntFloor(time);
    time -= hour;
    time *= 60;
    int min = toIntFloor(time);
    time -= min;
    time *= 60;
    int sec = toIntFloor(time);
    bool colon = !_blink || (sec % 2 == 0);

    char bufHour[4], bufMin[4], bufSec[4];
    snprintf(bufHour, sizeof(bufHour), "% 2d", hour);
    snprintf(bufMin, sizeof(bufMin), "%02d", min);
    snprintf(bufSec, sizeof(bufSec), "%02d", sec);

    const float coefH = 0.8;
    const float coefW = 0.5;
    float size = _scale * _size;
    float size2 = size * 0.75;

    float diffH = coefH * (size - size2);
    float diffW = coefW * GLOB_ENGINE->GetTextWidth(size2, _font, " ");

    float width = GLOB_ENGINE->GetTextWidth(size, _font, bufHour) + GLOB_ENGINE->GetTextWidth(size, _font, ":") +
                  GLOB_ENGINE->GetTextWidth(size, _font, bufMin) + diffW +
                  GLOB_ENGINE->GetTextWidth(size2, _font, bufSec);

    int pos = _style & ST_HPOS;
    float left;
    switch (pos)
    {
        case ST_RIGHT:
            left = _x + _w - width - _scale * textBorder;
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - width);
            break;
        default:
            PoseidonAssert(pos == ST_LEFT) left = _x + _scale * textBorder;
            break;
    }

    const float offsetX =
        (_shadowOffsetXAspectCorrect ? AspectCorrectTextShadowOffsetX(_shadowOffsetYRatio) : _shadowOffsetXRatio) *
        size;
    const float offsetY = _shadowOffsetYRatio * size;

    bool shadow = (_style & ST_SHADOW) != 0;

    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    PackedColor shadowColor = PackedColor(Color(0, 0, 0, alpha));

    if (shadow)
    {
        float left2 = left;
        GLOB_ENGINE->DrawText(Point2DFloat(left2 + offsetX, top + offsetY), size, Rect2DFloat(_x, _y, _w, _h), _font,
                              shadowColor, bufHour);
        left2 += GLOB_ENGINE->GetTextWidth(size, _font, bufHour);
        if (colon)
        {
            GLOB_ENGINE->DrawText(Point2DFloat(left2 + offsetX, top + offsetY), size, Rect2DFloat(_x, _y, _w, _h),
                                  _font, shadowColor, ":");
        }
        left2 += GLOB_ENGINE->GetTextWidth(size, _font, ":");
        GLOB_ENGINE->DrawText(Point2DFloat(left2 + offsetX, top + offsetY), size, Rect2DFloat(_x, _y, _w, _h), _font,
                              shadowColor, bufMin);
        left2 += GLOB_ENGINE->GetTextWidth(size, _font, bufMin) + diffW;
        GLOB_ENGINE->DrawText(Point2DFloat(left2 + offsetX, top + diffH + offsetY), size2, Rect2DFloat(_x, _y, _w, _h),
                              _font, shadowColor, bufSec);
    }
    GLOB_ENGINE->DrawText(Point2DFloat(left, top), size, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, bufHour);
    left += GLOB_ENGINE->GetTextWidth(size, _font, bufHour);
    if (colon)
    {
        GLOB_ENGINE->DrawText(Point2DFloat(left, top), size, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, ":");
    }
    left += GLOB_ENGINE->GetTextWidth(size, _font, ":");
    GLOB_ENGINE->DrawText(Point2DFloat(left, top), size, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, bufMin);
    left += GLOB_ENGINE->GetTextWidth(size, _font, bufMin) + diffW;
    GLOB_ENGINE->DrawText(Point2DFloat(left, top + diffH), size2, Rect2DFloat(_x, _y, _w, _h), _font, ftColor, bufSec);
}

CSkewStatic::CSkewStatic(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control(parent, CT_STATIC_SKEW, idc, cls)
{
    _enabled = false;
    _color = GetPackedColor(cls >> "color");
    _xTL = cls >> "TL" >> "x";
    _yTL = cls >> "TL" >> "y";
    _alphaTL = cls >> "TL" >> "alpha";
    _xTR = cls >> "TR" >> "x";
    _yTR = cls >> "TR" >> "y";
    _alphaTR = cls >> "TR" >> "alpha";
    _xBL = cls >> "BL" >> "x";
    _yBL = cls >> "BL" >> "y";
    _alphaBL = cls >> "BL" >> "alpha";
    _xBR = cls >> "BR" >> "x";
    _yBR = cls >> "BR" >> "y";
    _alphaBR = cls >> "BR" >> "alpha";

    const ParamEntry& clsColors = Pars >> "CfgWrapperUI" >> "Colors";
    _lineColors[0] = GetPackedColor(clsColors >> "color1");
    _lineColors[1] = GetPackedColor(clsColors >> "color2");
    _lineColors[2] = GetPackedColor(clsColors >> "color3");
    _lineColors[3] = GetPackedColor(clsColors >> "color4");
    _lineColors[4] = GetPackedColor(clsColors >> "color5");

    _x = floatMin(_xTL, _xBL);
    _w = floatMax(_xTR, _xBR) - _x;
    _y = floatMin(_yTL, _yTR);
    _h = floatMax(_yBL, _yBR) - _y;
}

void CSkewStatic::Move(float dx, float dy)
{
    Control::Move(dx, dy);
    _xTL += dx;
    _xTR += dx;
    _xBL += dx;
    _xBR += dx;
    _yTL += dy;
    _yTR += dy;
    _yBL += dy;
    _yBR += dy;
}

void DrawLine(float xs, float ys, float as, float xe, float ye, float ae, PackedColor color)
{
    if (as <= 0)
    {
        if (ae <= 0)
        {
            return;
        }
        float coef = ae / (ae - as);
        xs = xe - coef * (xe - xs);
        ys = ye - coef * (ye - ys);
        // as = 0 == ae - coef * (ae - as)
        as = 0;
    }
    else if (ae <= 0)
    {
        float coef = as / (as - ae);
        xe = xs - coef * (xs - xe);
        ye = ys - coef * (ys - ye);
        // ae = 0 == as - coef * (as - ae)
        ae = 0;
    }
    GEngine->DrawLine(Line2DPixel(xs, ys, xe, ye), ModAlpha(color, as), ModAlpha(color, ae));
}

/*
#ifdef _MSC_VER
void DrawPoly
(
    MipInfo &mip,
    Vertex2D *vertices, float *alphas, int n,
    Rect2D &clipRect = Rect2DClip()
)
#else
static Rect2DClip DefaultClipRect;

void DrawPoly
(
    MipInfo &mip,
    Vertex2D *vertices, float *alphas, int n,
    Rect2D &clipRect = DefaultClipRect
)
#endif
{
    // simplified implementation
    for (int i=0; i<n-1; i++)
    {
        int iplus = i;
        int iminus = i;
        if (alphas[i] < 0)
        {
            if (alphas[i + 1] <= 0) continue;
            iplus++;
        }
        else if (alphas[i + 1] < 0)
        {
            if (alphas[i] == 0) continue;
            iminus++;
        }
        else continue;

        float coef = alphas[iplus] / (alphas[iplus] - alphas[iminus]);
        Vertex2D &vplus = vertices[iplus];
        Vertex2D &vminus = vertices[iminus];
        vminus.x = vplus.x - coef * (vplus.x - vminus.x);
        vminus.y = vplus.y - coef * (vplus.y - vminus.y);
        vminus.u = vplus.u - coef * (vplus.u - vminus.u);
        vminus.v = vplus.v - coef * (vplus.v - vminus.v);
        alphas[iminus] = 0;
    }

    for (int i=0; i<n; i++)
    {
        vertices[i].color = ModAlpha(vertices[i].color, alphas[i]);
    }

    GEngine->DrawPoly(mip, vertices, n, clipRect);
}
*/

void CSkewStatic::OnDraw(float alpha)
{
    Fail("Obsolete");
    /*
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    switch (_style & ST_TYPE)
    {
        case ST_BACKGROUND:
        {
            if (_yTL >= _yBL || _yTR >= _yBR) break;

            // background
            Texture *background = GLOB_SCENE->Preloaded(DialogBackground);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
            float invH=1, invW=1;
            if (background)
            {
                invH = 1.0 / background->AHeight();
                invW = 1.0 / background->AWidth();
            }
            const int n = 4;
            Vertex2D vs[n];
            float as[n];
            // 0
            vs[0].x = _xTL * w;
            vs[0].y = _yTL * h;
            vs[0].u = (_xTL - _x) * w * invW;
            vs[0].v = 0;
            vs[0].color = _color;
            as[0] = alpha * _alphaTL;
            // 1
            vs[1].x = _xTR * w;
            vs[1].y = _yTR * h;
            vs[1].u = (_xTR - _x) * w * invW;
            vs[1].v = 0;
            vs[1].color = _color;
            as[1] = alpha * _alphaTR;
            // 2
            vs[2].x = _xBR * w;
            vs[2].y = _yBR * h;
            vs[2].u = (_xBR - _x) * w * invW;
            vs[2].v = _h * h * invH;
            vs[2].color = _color;
            as[2] = alpha * _alphaBR;
            // 3
            vs[3].x = _xBL * w;
            vs[3].y = _yBL * h;
            vs[3].u = (_xBL - _x) * w * invW;
            vs[3].v = _h * h * invH;
            vs[3].color = _color;
            as[3] = alpha * _alphaBL;

            DrawPoly(mip, vs, as, n);

            float xs, ys, xe, ye;
            float alphas, alphae;
            float dleft = (_xBL - _xTL) / (_yBL - _yTL);
            float dright = (_xBR - _xTR) / (_yBR - _yTR);

            // left border
            xs = toInt(_xBL * w) + 0.5;
            ys = toInt(_yBL * h) + 0.5;
            alphas = _alphaBL * alpha;
            xe = toInt(_xTL * w) + 0.5;
            ye = toInt(_yTL * h) + 0.5;
            alphae = _alphaTL * alpha;
            for (int i=0; i<5; i++)
            {
                DrawLine
                (
                    xs + i, ys, alphas,
                    xe + i, ye, alphae,
                    _lineColors[4 - i]
                );
            }
            // right border
            xs = toInt(_xBR * w) + 0.5;
            ys = toInt(_yBR * h) + 0.5;
            alphas = _alphaBR * alpha;
            xe = toInt(_xTR * w) + 0.5;
            ye = toInt(_yTR * h) + 0.5;
            alphae = _alphaTR * alpha;
            for (int i=0; i<5; i++)
            {
                DrawLine
                (
                    xs - i, ys, alphas,
                    xe - i, ye, alphae,
                    _lineColors[i]
                );
            }
            // top border
            xs = toInt(_xTL * w) + 0.5;
            ys = toInt(_yTL * h) + 0.5;
            alphas = _alphaTL * alpha;
            xe = toInt(_xTR * w) + 0.5;
            ye = toInt(_yTR * h) + 0.5;
            alphae = _alphaTR * alpha;
            for (int i=0; i<5; i++)
            {
                DrawLine
                (
                    xs + i + toInt(i * dleft), ys + i, alphas,
                    xe - i + toInt(i * dright), ye + i, alphae,
                    _lineColors[4 - i]
                );
            }
            // bottom border
            xs = toInt(_xBL * w) + 0.5;
            ys = toInt(_yBL * h) + 0.5;
            alphas = _alphaBL * alpha;
            xe = toInt(_xBR * w) + 0.5;
            ye = toInt(_yBR * h) + 0.5;
            alphae = _alphaBR * alpha;
            for (int i=0; i<5; i++)
            {
                DrawLine
                (
                    xs + i - toInt((4 - i) * dleft), ys - i, alphas,
                    xe - i - toInt((4 - i) * dright), ye - i, alphae,
                    _lineColors[i]
                );
            }
            break;
        }
        case ST_GROUP_BOX:
        {
            const int n = 4;
            Vertex2D vs[n];
            float as[n];
            // 0
            vs[0].x = _xTL * w;
            vs[0].y = _yTL * h;
            vs[0].u = 0;
            vs[0].v = 0;
            vs[0].color = _lineColors[2];
            as[0] = alpha * _alphaTL;
            // 1
            vs[1].x = _xTR * w;
            vs[1].y = _yTR * h;
            vs[1].u = 0;
            vs[1].v = 0;
            vs[1].color = _lineColors[2];
            as[1] = alpha * _alphaTR;
            // 2
            vs[2].x = _xBR * w;
            vs[2].y = _yBR * h;
            vs[2].u = 0;
            vs[2].v = 0;
            vs[2].color = _lineColors[2];
            as[2] = alpha * _alphaBR;
            // 3
            vs[3].x = _xBL * w;
            vs[3].y = _yBL * h;
            vs[3].u = 0;
            vs[3].v = 0;
            vs[3].color = _lineColors[2];
            as[3] = alpha * _alphaBL;

            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
            DrawPoly(mip, vs, as, n);

            DrawLine
            (
                _xTL * w, _yTL * h, _alphaTL * alpha,
                _xBL * w, _yBL * h, _alphaBL * alpha,
                _lineColors[3]
            );
            DrawLine
            (
                _xTL * w, _yTL * h, _alphaTL * alpha,
                _xTR * w, _yTR * h, _alphaTR * alpha,
                _lineColors[1]
            );
            DrawLine
            (
                _xTR * w, _yTR * h, _alphaTR * alpha,
                _xBR * w, _yBR * h, _alphaBR * alpha,
                _lineColors[1]
            );
            DrawLine
            (
                _xBL * w, _yBL * h, _alphaBL * alpha,
                _xBR * w, _yBR * h, _alphaBR * alpha,
                _lineColors[3]
            );
            break;
        }
        default:
        {
            const int n = 4;
            Vertex2D vs[n];
            float as[n];
            // 0
            vs[0].x = _xTL * w;
            vs[0].y = _yTL * h;
            vs[0].u = 0;
            vs[0].v = 0;
            vs[0].color = _color;
            as[0] = alpha * _alphaTL;
            // 1
            vs[1].x = _xTR * w;
            vs[1].y = _yTR * h;
            vs[1].u = 0;
            vs[1].v = 0;
            vs[1].color = _color;
            as[1] = alpha * _alphaTR;
            // 2
            vs[2].x = _xBR * w;
            vs[2].y = _yBR * h;
            vs[2].u = 0;
            vs[2].v = 0;
            vs[2].color = _color;
            as[2] = alpha * _alphaBR;
            // 3
            vs[3].x = _xBL * w;
            vs[3].y = _yBL * h;
            vs[3].u = 0;
            vs[3].v = 0;
            vs[3].color = _color;
            as[3] = alpha * _alphaBL;

            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
            DrawPoly(mip, vs, as, n);
            break;
        }
    }
    */
}

CEditContainer::CEditContainer(const ParamEntry& cls)
{
    _ftColor = GetPackedColor(cls >> "colorText");
    _selColor = GetPackedColor(cls >> "colorSelection");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));

    _enableToolip = false;
    _firstVisible = 0;
    _blockBegin = 0;
    _blockEnd = 0;

    _maxChars = INT_MAX;

    _size = 0;

    RString autoComplete = cls >> "autocomplete";
    SetAutoComplete(CreateAutoComplete(autoComplete));
}

void CEditContainer::SetAutoComplete(IAutoComplete* ac)
{
    _autoComplete = ac;
}

void CEditContainer::SetText(RString text)
{
    text = DecodeControlText(text);
    const int langID = _font ? _font->GetLangID() : Poseidon::Foundation::GetLangID();
    if (CountTextElements(text, langID) <= _maxChars)
    {
        _text = text;
    }
    else
    {
        _text = text.Substring(0, ByteOffsetForTextElements(text, _maxChars, langID));
    }

    FormatText();

    int n = strlen(text);
    saturateMin(_blockBegin, n);
    saturateMin(_blockEnd, n);
    saturateMin(_firstVisible, n);
    EnsureVisible(_blockEnd);
}

void CEditContainer::SetCaretPos(int pos)
{
    int n = strlen(GetText());
    saturate(pos, 0, n);
    _blockBegin = pos;
    _blockEnd = pos;
    EnsureVisible(pos);
}

int CEditContainer::NextPos(int pos)
{
    return NextTextElementPos(GetText(), pos, _font ? _font->GetLangID() : Poseidon::Foundation::GetLangID());
}

int CEditContainer::PrevPos(int pos)
{
    return PrevTextElementPos(GetText(), pos, _font ? _font->GetLangID() : Poseidon::Foundation::GetLangID());
}

inline bool IsKey(int nVirtKey)
{
    SDL_Keymod mod = SDL_GetModState();
    if (nVirtKey == SDLK_LSHIFT || nVirtKey == SDLK_RSHIFT)
        return (mod & SDL_KMOD_SHIFT) != 0;
    if (nVirtKey == SDLK_LCTRL || nVirtKey == SDLK_RCTRL)
        return (mod & SDL_KMOD_CTRL) != 0;
    return false;
}

#define DIAG_KOREAN 0

bool CEditContainer::DoKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = strlen(GetText());
    PoseidonAssert(_blockBegin >= 0);
    PoseidonAssert(_blockBegin <= n);
    PoseidonAssert(_blockEnd >= 0);
    PoseidonAssert(_blockEnd <= n);
    switch (nChar)
    {
        case SDLK_LEFT:
            if (_blockEnd > 0)
            {
                _blockEnd = PrevPos(_blockEnd);
                if (_blockEnd > 0 && (IsKey(SDLK_LCTRL)))
                {
                    const char* text = GetText();
                    while (true)
                    {
                        if (ISSPACE(text[_blockEnd - 1]) && !ISSPACE(text[_blockEnd]))
                        {
                            break;
                        }
                        _blockEnd = PrevPos(_blockEnd);
                        if (_blockEnd <= 0)
                        {
                            break;
                        }
                    }
                }
                EnsureVisible(_blockEnd);
                _enableToolip = false;
            }
            if (!IsKey(SDLK_LSHIFT))
            {
                _blockBegin = _blockEnd;
            }
            return true;
        case SDLK_RIGHT:
            if (_blockEnd < n)
            {
                _blockEnd = NextPos(_blockEnd);
                if (_blockEnd < n && (IsKey(SDLK_LCTRL)))
                {
                    const char* text = GetText();
                    while (true)
                    {
                        if (ISSPACE(text[_blockEnd - 1]) && !ISSPACE(text[_blockEnd]))
                        {
                            break;
                        }
                        _blockEnd = NextPos(_blockEnd);
                        if (_blockEnd >= n)
                        {
                            break;
                        }
                    }
                }
                EnsureVisible(_blockEnd);
                _enableToolip = false;
            }
            if (!IsKey(SDLK_LSHIFT))
            {
                _blockBegin = _blockEnd;
            }
            return true;
        case SDLK_UP:
            if (IsMulti())
            {
                int line = CurLine();
                if (line > 0)
                {
                    RString text = GetLine(line);
                    int pos = _blockEnd - _lines[line];
                    float x = PosToX(text, pos);
                    line--;
                    text = GetLine(line);
                    pos = XToPos(text, x);
                    _blockEnd = _lines[line] + pos;
                    EnsureVisible(_blockEnd);
                    _enableToolip = false;
                    if (!IsKey(SDLK_LSHIFT))
                    {
                        _blockBegin = _blockEnd;
                    }
                }
                return true;
            }
            else
            {
                return false;
            }
        case SDLK_DOWN:
            if (IsMulti())
            {
                int line = CurLine();
                if (line < _lines.Size() - 1)
                {
                    RString text = GetLine(line);
                    int pos = _blockEnd - _lines[line];
                    float x = PosToX(text, pos);
                    line++;
                    text = GetLine(line);
                    pos = XToPos(text, x);
                    _blockEnd = _lines[line] + pos;
                    EnsureVisible(_blockEnd);
                    _enableToolip = false;
                    if (!IsKey(SDLK_LSHIFT))
                    {
                        _blockBegin = _blockEnd;
                    }
                }
                return true;
            }
            else
            {
                return false;
            }
        case SDLK_HOME:
            _blockEnd = 0;
            _enableToolip = false;
            EnsureVisible(_blockEnd);
            if (!IsKey(SDLK_LSHIFT))
            {
                _blockBegin = _blockEnd;
            }
            return true;
        case SDLK_END:
            _blockEnd = n;
            _enableToolip = true;
            EnsureVisible(_blockEnd);
            if (!IsKey(SDLK_LSHIFT))
            {
                _blockBegin = _blockEnd;
            }
            return true;
        case SDLK_BACKSPACE:
            if (_blockBegin != _blockEnd)
            {
                BlockDelete();
                return true;
            }
            if (_blockEnd <= 0)
            {
                return true;
            }
            _blockEnd = PrevPos(_blockEnd);
            _blockBegin = _blockEnd;
            _enableToolip = true;
            goto DeleteChar;
        case SDLK_DELETE:
            if (_blockBegin != _blockEnd)
            {
                if (IsKey(SDLK_LSHIFT))
                {
                    BlockCut();
                }
                else
                {
                    BlockDelete();
                }
                return true;
            }
            if (_blockEnd >= n)
            {
                return true;
            }
            _enableToolip = true;
        DeleteChar:
        {
            SetText(_text.Substring(0, _blockEnd) + _text.Substring(NextPos(_blockEnd), _text.GetLength()));
            // EnsureVisible is called from SetText
        }

#if DIAG_KOREAN
            RptF("KEY DOWN: BACKSPACE / DELETE");
            char buffer[1024];
            buffer[0] = 0;
            for (int i = 0; i < GetText().GetLength(); i++)
            {
                sprintf(buffer + strlen(buffer), "%02x ", (int)GetText()[i] & 0xff);
            }
            RptF("Whole text: %s, cursor %d - %d", buffer, _blockBegin, _blockEnd);
#endif

            return true;
        case SDLK_INSERT:
            if (IsKey(SDLK_LSHIFT))
            {
                if (_blockBegin != _blockEnd)
                {
                    BlockDelete();
                }
                BlockPaste();
            }
            else if (IsKey(SDLK_LCTRL))
            {
                BlockCopy();
            }
            return true;
        case SDLK_TAB:
        {
            if (_blockBegin != _blockEnd || !_autoComplete || !_enableToolip)
            {
                return false;
            }
            bool certain;
            RString beg;
            RString tip = _autoComplete->Guess(_text, _blockEnd, certain, beg);
            if (tip.GetLength() <= 0)
            {
                return false;
            }

            if (tip.GetLength() + _text.GetLength() <= _maxChars)
            {
                int start = _blockEnd - beg.GetLength();
                saturateMax(start, 0);
                RString text = _text.Substring(0, start) + tip + _text.Substring(_blockEnd, _text.GetLength());
                SetText(text);
                _blockBegin = _blockEnd = start + tip.GetLength();
                EnsureVisible(_blockEnd);
            }

            return true;
        }

        case 'x':
            if (IsKey(SDLK_LCTRL))
            {
                BlockCut();
                return true;
            }
            break;
        case 'c':
            if (IsKey(SDLK_LCTRL))
            {
                BlockCopy();
                return true;
            }
            break;
        case 'v':
            if (IsKey(SDLK_LCTRL))
            {
                if (_blockBegin != _blockEnd)
                {
                    BlockDelete();
                }
                BlockPaste();
                return true;
            }
            break;
    }

    return false;
}

bool CEditContainer::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = strlen(GetText());
    const int langID = _font ? _font->GetLangID() : Poseidon::Foundation::GetLangID();
    if (CountTextElements(GetText(), langID) >= _maxChars)
    {
        return true;
    }

    PoseidonAssert(_blockBegin >= 0);
    PoseidonAssert(_blockBegin <= n);
    PoseidonAssert(_blockEnd >= 0);
    PoseidonAssert(_blockEnd <= n);
    char newChar[8];
    int charLen = EncodeUtf8Codepoint(nChar, newChar, sizeof(newChar));
    if (charLen <= 0)
        return true;

    if (_blockBegin != _blockEnd)
    {
        BlockDelete();
    }

    RString data = (GetText().Substring(0, _blockEnd) + RString(newChar) + GetText().Substring(_blockEnd, n));

    SetText(data);
    _blockEnd += charLen;
    _blockBegin = _blockEnd;
    if (_autoComplete)
    {
        _autoComplete->AfterChar(_text, _blockEnd);
    }
    _enableToolip = true;
    EnsureVisible(_blockEnd);

#if DIAG_KOREAN
    RptF("CHAR: %02x", nChar);
    char buffer[1024];
    buffer[0] = 0;
    for (int i = 0; i < GetText().GetLength(); i++)
    {
        sprintf(buffer + strlen(buffer), "%02x ", (int)GetText()[i] & 0xff);
    }
    RptF("Whole text: %s, cursor %d - %d", buffer, _blockBegin, _blockEnd);
#endif

    return true;
}

bool CEditContainer::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = strlen(GetText());
    if (n + 2 > _maxChars)
    {
        return true;
    }

    PoseidonAssert(_blockBegin >= 0);
    PoseidonAssert(_blockBegin <= n);
    PoseidonAssert(_blockEnd >= 0);
    PoseidonAssert(_blockEnd <= n);
    char newChar[3];
    newChar[0] = (nChar >> 8) & 0xff;
    newChar[1] = nChar & 0xff;
    newChar[2] = 0;

    if (_blockBegin != _blockEnd)
    {
        BlockDelete();
    }

    RString data = (GetText().Substring(0, _blockEnd) + RString(newChar) + GetText().Substring(_blockEnd, n));

    SetText(data);
    _blockEnd += 2;
    _blockBegin = _blockEnd;
    if (_autoComplete)
    {
        _autoComplete->AfterChar(_text, _blockEnd);
    }
    _enableToolip = true;
    EnsureVisible(_blockEnd);

#if DIAG_KOREAN
    RptF("IME CHAR: %02x %02x", (int)newChar[0] & 0xff, (int)newChar[1] & 0xff);
    char buffer[1024];
    buffer[0] = 0;
    for (int i = 0; i < GetText().GetLength(); i++)
    {
        sprintf(buffer + strlen(buffer), "%02x ", (int)GetText()[i] & 0xff);
    }
    RptF("Whole text: %s, cursor %d - %d", buffer, _blockBegin, _blockEnd);
#endif

    return true;
}

bool CEditContainer::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
    int n = strlen(GetText());
    if (nChar != 0 && n + 2 > _maxChars)
    {
        return true;
    }

    PoseidonAssert(_blockBegin >= 0);
    PoseidonAssert(_blockBegin <= n);
    PoseidonAssert(_blockEnd >= 0);
    PoseidonAssert(_blockEnd <= n);

    if (_blockBegin != _blockEnd)
    {
        BlockDelete();
    }
    if (nChar == 0)
    {
#if DIAG_KOREAN
        RptF("IME COMPOSITION: 00 00");
        char buffer[1024];
        buffer[0] = 0;
        for (int i = 0; i < GetText().GetLength(); i++)
        {
            sprintf(buffer + strlen(buffer), "%02x ", (int)GetText()[i] & 0xff);
        }
        RptF("Whole text: %s, cursor %d - %d", buffer, _blockBegin, _blockEnd);
#endif

        return true;
    }

    char newChar[3];
    newChar[0] = (nChar >> 8) & 0xff;
    newChar[1] = nChar & 0xff;
    newChar[2] = 0;

    RString data = (GetText().Substring(0, _blockEnd) + RString(newChar) + GetText().Substring(_blockEnd, n));

    SetText(data);
    _blockBegin = _blockEnd;
    _blockEnd += 2;
    if (_autoComplete)
    {
        _autoComplete->AfterChar(_text, _blockEnd);
    }
    _enableToolip = true;
    EnsureVisible(_blockEnd);

#if DIAG_KOREAN
    RptF("IME COMPOSITION: %02x %02x", (int)newChar[0] & 0xff, (int)newChar[1] & 0xff);
    char buffer[1024];
    buffer[0] = 0;
    for (int i = 0; i < GetText().GetLength(); i++)
    {
        sprintf(buffer + strlen(buffer), "%02x ", (int)GetText()[i] & 0xff);
    }
    RptF("Whole text: %s, cursor %d - %d", buffer, _blockBegin, _blockEnd);
#endif

    return true;
}

void CEditContainer::BeginPlatformTextInput()
{
    if (!SDL_HasScreenKeyboardSupport())
    {
        return;
    }

    SDL_Window* window = SDL_GetKeyboardFocus();
    if (window == nullptr)
    {
        return;
    }

    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME, "0");

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props != 0)
    {
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
        SDL_StartTextInputWithProperties(window, props);
        SDL_DestroyProperties(props);
        Poseidon::InstallIosKeyboardHideAccessory();
        return;
    }

    SDL_StartTextInput(window);
    Poseidon::InstallIosKeyboardHideAccessory();
}

void CEditContainer::EndPlatformTextInput()
{
    if (!SDL_HasScreenKeyboardSupport())
    {
        return;
    }

    SDL_Window* window = SDL_GetKeyboardFocus();
    if (window != nullptr)
    {
        SDL_StopTextInput(window);
    }
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
}

int CEditContainer::CurLine() const
{
    int n = _lines.Size();
    for (int i = 1; i < n; i++)
    {
        if (_blockEnd < _lines[i])
        {
            return i - 1;
        }
    }
    return n - 1;
}

int CEditContainer::FirstLine() const
{
    int n = _lines.Size();
    for (int i = 1; i < n; i++)
    {
        if (_firstVisible < _lines[i])
        {
            return i - 1;
        }
    }
    return n - 1;
}

RString CEditContainer::GetLine(int i) const
{
    if (i < 0)
    {
        return "";
    }
    // PoseidonAssert((_style & ST_TYPE) == ST_MULTI);
    PoseidonAssert(i < _lines.Size());

    int from = _lines[i];
    int to = i + 1 < _lines.Size() ? _lines[i + 1] : _text.GetLength();
    return _text.Substring(from, to);
}

void CEditContainer::BlockDelete()
{
    if (_blockBegin == _blockEnd)
    {
        return;
    }

    int from, to;
    if (_blockBegin < _blockEnd)
    {
        from = _blockBegin;
        to = _blockEnd;
    }
    else
    {
        from = _blockEnd;
        to = _blockBegin;
    }
    RString text = _text.Substring(0, from) + _text.Substring(to, _text.GetLength());
    _blockBegin = _blockEnd = from;
    SetText(text);
}

void CEditContainer::BlockCopy()
{
    if (_blockBegin == _blockEnd)
    {
        return;
    }

    int from, to;
    if (_blockBegin < _blockEnd)
    {
        from = _blockBegin;
        to = _blockEnd;
    }
    else
    {
        from = _blockEnd;
        to = _blockBegin;
    }
    RString text = _text.Substring(from, to);
    SDL_SetClipboardText(text);
}

void CEditContainer::BlockCut()
{
    BlockCopy();
    BlockDelete();
}

void CEditContainer::BlockPaste()
{
    char* clipboard = SDL_GetClipboardText();
    if (clipboard && *clipboard)
    {
        for (char* p = clipboard; *p; ++p)
        {
            if (*p == '\n' || *p == '\r' || *p == '\t')
                *p = ' ';
        }
        int n = strlen(clipboard);
        if (n + _text.GetLength() <= _maxChars)
        {
            RString text =
                _text.Substring(0, _blockEnd) + RString(clipboard) + _text.Substring(_blockEnd, _text.GetLength());
            SetText(text);
            _blockBegin = _blockEnd = _blockEnd + n;
            EnsureVisible(_blockEnd);
        }
    }
    SDL_free(clipboard);
}

CEdit::CEdit(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control(parent, CT_EDIT, idc, cls), CEditContainer(cls)
{
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }

    RString text = cls >> "text";
    SetText(text);
    // note: SetText does EnsureVisible
}

bool CEdit::IsMulti() const
{
    return (_style & ST_TYPE) == ST_MULTI;
}

float CEdit::PosToX(RString text, int pos) const
{
    float left;
    int style = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        style = _style & ST_HPOS;
    }
    float size = _scale * _size;
    switch (style)
    {
        case ST_RIGHT:
            left = _x + _w - GLOB_ENGINE->GetTextWidth(size, _font, text) - _scale * textBorder;
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - GLOB_ENGINE->GetTextWidth(size, _font, text));
            break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) left = _x + _scale * textBorder;
            break;
    }

    RString str = text.Substring(0, pos);
    return left + GLOB_ENGINE->GetTextWidth(size, _font, str);
}

int CEdit::XToPos(RString text, float x) const
{
    float left;
    int style = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        style = _style & ST_HPOS;
    }
    float size = _scale * _size;
    switch (style)
    {
        case ST_RIGHT:
            left = _x + _w - GLOB_ENGINE->GetTextWidth(size, _font, text) - _scale * textBorder;
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - GLOB_ENGINE->GetTextWidth(size, _font, text));
            break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) left = _x + _scale * textBorder;
            break;
    }

    if (x < left)
    {
        return 0;
    }

    // Iterate the passed-in `text` (a single line / scrolled substring), NOT the
    // whole `_text`: callers pass GetLine() or a Substring shorter than _text, so
    // bounding the loop by strlen(_text) walks CopyTextElement past the buffer —
    // an out-of-bounds read (crash) or a stall at the NUL (freeze).
    int n = strlen(text);
    for (int i = 0; i < n;)
    {
        int j = i;
        char temp[8];
        float cw;
        i = CopyTextElement(text, i, _font->GetLangID(), temp, sizeof(temp));
        cw = GLOB_ENGINE->GetTextWidth(size, _font, temp);
        if (left + cw > x)
        {
            if (x - left < left + cw - x)
            {
                return j;
            }
            else
            {
                return i;
            }
        }
        left += cw;
    }
    return n;
}

void CEdit::DrawText(const char* text, int offset, float top, float alpha)
{
    int pos = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        pos = _style & ST_HPOS;
    }
    float size = _scale * _size;
    float left;
    switch (pos)
    {
        case ST_RIGHT:
            left = _x + _w - GLOB_ENGINE->GetTextWidth(size, _font, text) - _scale * textBorder;
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - GLOB_ENGINE->GetTextWidth(size, _font, text));
            break;
        default:
            PoseidonAssert(pos == ST_LEFT) left = _x + _scale * textBorder;
            break;
    }

    if (_blockBegin != _blockEnd)
    {
        int from, to;
        if (_blockBegin < _blockEnd)
        {
            from = _blockBegin;
            to = _blockEnd;
        }
        else
        {
            from = _blockEnd;
            to = _blockBegin;
        }
        if (from < static_cast<int>(offset + strlen(text)) && to > offset) // strlen returns size_t, from/to are int
        {
            PackedColor selColor = ModAlpha(_selColor, alpha);
            float x1 = 0;
            //			char buffer[1024];
            if (from > static_cast<int>(offset)) // from is int, offset could be unsigned
            {
                RString prefix(text, from - offset);
                x1 = GLOB_ENGINE->GetTextWidth(size, _font, prefix);
            }
            float x2 = 0;
            if (to < static_cast<int>(offset + strlen(text))) // strlen returns size_t, to is int
            {
                RString prefix(text, to - offset);
                x2 = GLOB_ENGINE->GetTextWidth(size, _font, prefix);
            }
            else
            {
                x2 = GLOB_ENGINE->GetTextWidth(size, _font, text);
            }

            const int w = GLOB_ENGINE->Width2D();
            const int h = GLOB_ENGINE->Height2D();

            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
            GLOB_ENGINE->Draw2D(
                mip, selColor, Rect2DPixel((left + x1) * w, top * h, (x2 - x1) * w, size * h),
                Rect2DPixel((_x + _scale * textBorder) * w, _y * h, (_w - 2.0 * _scale * textBorder) * w, _h * h));
        }
    }

    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    GLOB_ENGINE->DrawText(Point2DFloat(left, top), size,
                          Rect2DFloat(_x + _scale * textBorder, _y, _w - 2.0 * _scale * textBorder, _h), _font, ftColor,
                          text);
}

void CEdit::OnDraw(float alpha)
{
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float xx = toInt(_x * w) + 0.5;
    float yy = toInt(_y * h) + 0.5;
    float ww = toInt(_w * w);
    float hh = toInt(_h * h);

    PackedColor ftColor = ModAlpha(_ftColor, alpha);

    DrawLeft(0, ftColor);
    DrawTop(0, ftColor);
    DrawBottom(0, ftColor);
    DrawRight(0, ftColor);

    float size = _scale * _size;
    float height = size;

    if (_text.GetLength() > 0)
    {
        if ((_style & ST_TYPE) == ST_MULTI)
        {
            float top = _y;
            for (int i = FirstLine(); i < _lines.Size(); i++)
            {
                DrawText(GetLine(i), _lines[i], top, alpha);
                top += height;
                if (top >= _y + _h)
                {
                    break;
                }
            }
        }
        else
        {
            float top = _y + 0.5 * (_h - height);
            DrawText(_text.Substring(_firstVisible, _text.GetLength()), _firstVisible, top, alpha);
        }
    }

    // draw caret
    if (!IsFocused())
    {
        return;
    }
    float top, left;
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        int cur = CurLine();
        int first = FirstLine();
        top = _y + (cur - first) * height;
        left = CX(PosToX(GetLine(cur), _blockEnd - (cur < 0 ? 0 : _lines[cur])));
    }
    else
    {
        top = _y + 0.5 * (_h - height);
        left = CX(PosToX(_text.Substring(_firstVisible, _text.GetLength()), _blockEnd - _firstVisible));
    }
    bool state = ((Glob.uiTime.toFloat() - toIntFloor(Glob.uiTime.toFloat())) < 0.5);
    if (state)
    {
        GEngine->DrawLine(Line2DPixel(left, CY(top), left, CY(top + height)), ftColor, ftColor);
    }
    // draw autocomplete
    if (_blockBegin == _blockEnd && _autoComplete && _enableToolip)
    {
        bool certain = false;
        RString beg;
        RString tip = _autoComplete->Guess(_text, _blockEnd, certain, beg);
        if (tip.GetLength() > 0)
        {
            int w = GEngine->Width2D();
            int h = GEngine->Height2D();

            float x = left / w;
            float y = top;
            const float border = 0.005;
            float height = _tooltipFont->Height();
            float width = GEngine->GetTextWidth(_tooltipSize, _tooltipFont, tip);
            float begW = GEngine->GetTextWidth(_tooltipSize, _tooltipFont, beg);

            PackedColor shade(Color(0, 0, 0, 0.3));
            PackedColor colorText = PackedWhite;
            PackedColor color(Color(1, 1, 1, 0.3));
            if (!certain)
            {
                colorText = PackedBlack;
                shade = PackedColor(Color(0.95, 0.95, 0.95, 0.3));
            }

            float bx = x - begW;
            //		float by = y - 0.5 * height - border;
            float by = y - height - 2.0 * border;
            float ex = bx + width + 2.0 * border;
            float ey = by + height + 2.0 * border;

            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
            GEngine->Draw2D(mip, shade, Rect2DPixel(bx * w, by * h, (ex - bx) * w, (ey - by) * h));
            GEngine->DrawText(Point2DFloat(bx + border, by + border), _tooltipSize, _tooltipFont, colorText, tip);
            GEngine->DrawLine(Line2DPixel(bx * w, by * h, ex * w, by * h), color, color);
            GEngine->DrawLine(Line2DPixel(ex * w, by * h, ex * w, ey * h), color, color);
            GEngine->DrawLine(Line2DPixel(ex * w, ey * h, bx * w, ey * h), color, color);
            GEngine->DrawLine(Line2DPixel(bx * w, ey * h, bx * w, by * h), color, color);
        }
    }
}

bool CEdit::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoKeyDown(nChar, nRepCnt, nFlags);
}

bool CEdit::OnSetFocus(bool up, bool def)
{
    if (!Control::OnSetFocus(up, def))
    {
        return false;
    }
    BeginPlatformTextInput();
    return true;
}

bool CEdit::OnKillFocus()
{
    EndPlatformTextInput();
    return Control::OnKillFocus();
}

bool CEdit::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoChar(nChar, nRepCnt, nFlags);
}

bool CEdit::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoIMEChar(nChar, nRepCnt, nFlags);
}

bool CEdit::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
    return CEditContainer::DoIMEComposition(nChar, nFlags);
}

int CEdit::FindPos(float x, float y)
{
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        if (_text.GetLength() == 0)
        {
            return 0;
        }
        else
        {
            float size = _scale * _size;
            float height = size;
            int line = FirstLine() + toIntFloor((y - _y) / height);
            saturate(line, 0, _lines.Size() - 1);
            return _lines[line] + XToPos(GetLine(line), x);
        }
    }
    else
    {
        return _firstVisible + XToPos(_text.Substring(_firstVisible, _text.GetLength()), x);
    }
}

void CEdit::OnLButtonDown(float x, float y)
{
    BeginPlatformTextInput();

    int pos = FindPos(x, y);
    if (!InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) &&
        !InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
    {
        _blockBegin = pos;
    }
    _blockEnd = pos;
    EnsureVisible(_blockEnd);
}

void CEdit::OnLButtonUp(float x, float y)
{
    /*
        _blockEnd = FindPos(x, y);
        EnsureVisible(_blockEnd);
    */
}

void CEdit::OnMouseMove(float x, float y, bool active)
{
    if (InputSubsystem::Instance().IsMouseLeftDown())
    {
        _blockEnd = FindPos(x, y);
        EnsureVisible(_blockEnd);
    }
}

void CEdit::EnsureVisible(int pos)
{
    float size = _scale * _size;

    if ((_style & ST_TYPE) == ST_MULTI)
    {
        if (_text.GetLength() == 0)
        {
            _firstVisible = 0;
            return;
        }

        int cur = CurLine();
        int first = FirstLine();
        saturateMin(first, cur);

        float height = size;
        int maxlines = toIntFloor(_h / height);

        int lines = _lines.Size() - first;
        if (lines <= maxlines)
        {
            first += lines - maxlines;
            saturateMax(first, 0);
        }
        else if (first + maxlines <= cur)
        {
            first = cur - maxlines + 1;
        }
        _firstVisible = _lines[first];
    }
    else
    {
        saturateMin(_firstVisible, _blockEnd);
        float wlimit = _w - 2.0 * _scale * textBorder;
        float w = GLOB_ENGINE->GetTextWidth(size, _font, _text.Substring(_firstVisible, _text.GetLength()));
        if (w <= wlimit)
        {
            while (_firstVisible > 0)
            {
                int prev = PrevPos(_firstVisible);
                w += GLOB_ENGINE->GetTextWidth(size, _font, _text.Substring(prev, _firstVisible));
                if (w > wlimit)
                {
                    break;
                }
                _firstVisible = prev;
            }
        }
        else
        {
            w = GLOB_ENGINE->GetTextWidth(size, _font, _text.Substring(_firstVisible, _blockEnd));
            while (w > wlimit)
            {
                int prev = _firstVisible;
                _firstVisible = NextPos(_firstVisible);
                w -= GLOB_ENGINE->GetTextWidth(size, _font, _text.Substring(prev, _firstVisible));
            }
        }
    }
}

void CEdit::FormatText()
{
    if ((_style & ST_TYPE) != ST_MULTI)
    {
        return;
    }

    _lines.Clear();

    float lineWidth = _w - 2 * _scale * textBorder;
    float size = _scale * _size;

    _lines.Add(0);

    const char* p = _text;
    while (*p != 0)
    {
        // begin of the line
        const char* word = p;
        int n = 0;
        float width = 0;
        while (true)
        {
            const char* q = p;
            char c = *p++;
            if (c == 0)
            {
                return;
            }
            if (ISSPACE(c))
            {
                n++;
                word = p;
            }

            char temp[8];
            p = _text + CopyTextElement(_text, static_cast<int>(q - (const char*)_text), _font->GetLangID(), temp,
                                        sizeof(temp));
            width += GLOB_ENGINE->GetTextWidth(size, _font, temp);

            if (width > lineWidth)
            {
                if (n > 0)
                {
                    p = word;
                }
                else
                {
                    p = q;
                }
                _lines.Add(p - (const char*)_text);
                break;
            }
        }
    }
}

CButton::CButton(ControlsContainer* parent, const ParamEntry& cls, float x, float y, float w, float h)
    : Control(parent, CT_BUTTON, cls >> "idc", cls >> "style", x, y, w, h)
{
    _text = DecodeControlText(cls >> "text");
    _ftColor = GetPackedColor(cls >> "colorText");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }
    _state = false;

    GetValue(_pushSound, cls >> "soundPush");
    GetValue(_clickSound, cls >> "soundClick");
    GetValue(_escapeSound, cls >> "soundEscape");

    InitColors();

    entry = cls.FindEntry("action");
    if (entry)
    {
        _action = *entry;
    }
}

CButton::CButton(ControlsContainer* parent, int idc, const ParamEntry& cls) : Control(parent, CT_BUTTON, idc, cls)
{
    _textCls = &cls;
    _text = DecodeControlText(cls >> "text");
    _ftColor = GetPackedColor(cls >> "colorText");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }
    _state = false;

    GetValue(_pushSound, cls >> "soundPush");
    GetValue(_clickSound, cls >> "soundClick");
    GetValue(_escapeSound, cls >> "soundEscape");

    InitColors();

    entry = cls.FindEntry("action");
    if (entry)
    {
        _action = *entry;
    }
}

void CButton::InitColors()
{
    const ParamEntry& clsButton = Pars >> "CfgWrapperUI" >> "Button";
    _color1 = GetPackedColor(clsButton >> "color1");
    _color2 = GetPackedColor(clsButton >> "color2");
    _color3 = GetPackedColor(clsButton >> "color3");
    _color4 = GetPackedColor(clsButton >> "color4");
    _color5 = GetPackedColor(clsButton >> "color5");
}

bool CButton::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (IsUiConfirmKey(nChar))
    {
        if (!_state)
        {
            _state = true;
            PlaySound(_pushSound);
        }
        return true;
    }
    return false;
}

bool CButton::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (IsUiConfirmKey(nChar))
    {
        _state = false;
        PlaySound(_clickSound);
        if (_action.GetLength() > 0)
        {
            GameState* gstate = GWorld->GetGameState();
            gstate->Execute(_action);
            GWorld->SimulateScripts();
        }
        OnClicked();
        _parent->OnButtonClicked(IDC());
        return true;
    }
    return false;
}

void CButton::OnLButtonDown(float x, float y)
{
    _state = true;
    PlaySound(_pushSound);
}

void CButton::OnLButtonUp(float x, float y)
{
    _state = false;
    if (IsInside(x, y))
    {
        PlaySound(_clickSound);
        if (_action.GetLength() > 0)
        {
            GameState* gstate = GWorld->GetGameState();
            gstate->Execute(_action);
            GWorld->SimulateScripts();
        }
        OnClicked();
        _parent->OnButtonClicked(IDC());
    }
    else
    {
        PlaySound(_escapeSound);
    }
}

void CButton::OnDraw(float alpha)
{
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float xx = toInt(_x * w) + 0.5;
    float yy = toInt(_y * h) + 0.5;
    float ww = toInt(_w * w);
    float hh = toInt(_h * h);

    int pos = _style & ST_HPOS;
    bool selected = IsFocused() || IsDefault() && !_parent->GetFocused()->CanBeDefault();
    bool pushed = IsEnabled() && _state;

    float offsetX = pushed ? 0.003 : 0;
    float offsetY = pushed ? 0.004 : 0;

    PackedColor color1 = ModAlpha(_color1, alpha);
    PackedColor color2 = ModAlpha(_color2, alpha);
    PackedColor color3 = ModAlpha(_color3, alpha);
    PackedColor color4 = ModAlpha(_color4, alpha);
    PackedColor color5 = ModAlpha(_color5, alpha);
    PackedColor ftColor = ModAlpha(_ftColor, alpha);

    if (pushed)
    {
        DrawLeft(0, color1);
        DrawTop(0, color1);
        DrawLeft(1, color2);
        DrawTop(1, color2);
        DrawBottom(1, color4);
        DrawRight(1, color4);
        DrawBottom(0, color5);
        DrawRight(0, color5);
    }
    else
    {
        DrawLeft(0, color5);
        DrawTop(0, color5);
        DrawLeft(1, color4);
        DrawTop(1, color4);
        DrawBottom(1, color2);
        DrawRight(1, color2);
        DrawBottom(0, color1);
        DrawRight(0, color1);
    }

    Texture* background = GLOB_SCENE->Preloaded(TextureWhite);
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(background, 0, 0);
    GLOB_ENGINE->Draw2D(mip, color3, Rect2DPixel(xx + 2, yy + 2, ww - 4, hh - 4));
    // GLOB_ENGINE->TextBank()->ReleaseMipmap();

    if (selected)
    {
        float ox = toInt(offsetX * w);
        float oy = toInt(offsetY * h);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + 6 + ox, yy + 6 + oy, xx + ww - 7 + ox, yy + 6 + oy), ftColor, ftColor);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 7 + ox, yy + 6 + oy, xx + ww - 7 + ox, yy + hh - 7 + oy), ftColor,
                              ftColor);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww - 7 + ox, yy + hh - 7 + oy, xx + 6 + ox, yy + hh - 7 + oy), ftColor,
                              ftColor);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + 6 + ox, yy + hh - 7 + oy, xx + 6 + ox, yy + 6 + oy), ftColor, ftColor);
    }

    float left, top;
    switch (pos)
    {
        case ST_RIGHT:
            left = _x + _w - GLOB_ENGINE->GetTextWidth(_size, _font, _text);
            break;
        case ST_CENTER:
            left = _x + 0.5 * (_w - GLOB_ENGINE->GetTextWidth(_size, _font, _text));
            break;
        default:
            PoseidonAssert(pos == ST_LEFT) left = _x;
            break;
    }
    top = _y + 0.5 * (_h - _size);
    GLOB_ENGINE->DrawText(Point2DFloat(left + offsetX, top + offsetY), _size, Rect2DFloat(_x, _y, _w, _h), _font,
                          ftColor, _text);
}

void CButton::OnClicked() {}
