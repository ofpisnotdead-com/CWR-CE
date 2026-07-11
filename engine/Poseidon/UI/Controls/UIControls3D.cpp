#include <algorithm>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/UI/Locale/AutoComplete.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Poseidon::Foundation::QSort;

} // namespace Poseidon
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

namespace Poseidon
{
RString FindPicture(RString name);
}
extern DrawCoord SceneToScreen(Vector3Par pos);

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

static RString Decode3DControlText(RString text)
{
    return Poseidon::DecodeLegacyTextToRString(text, GLanguage);
}

inline PackedColor ModAlpha(PackedColor color, float alpha)
{
    int a = toInt(alpha * color.A8());
    saturate(a, 0, 255);
    return PackedColorRGB(color, a);
}

inline bool PointInRect(float x, float y, float left, float top, float width, float height)
{
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

Control3D::Control3D(ControlsContainer* parent, int type, int idc, const ParamEntry& cls)
    : Control(parent, type, idc, cls)
{
    _position = VZero;
    _right = VZero;
    _down = VZero;

    float angle = cls >> "angle";
    _angle = (H_PI / 180.0) * angle;

    _u = _v = 0;
}

void Control3D::UpdateInfo(ControlObject* object, ControlInObject& info)
{
    Control::UpdateInfo(object, info);
    SetScale(1);

    Vector3 right = info.posTR - info.posTL;
    Vector3 down = info.posBL - info.posTL;

    Matrix3 matrix;
    matrix.SetUpAndAside(-down, right);
    Matrix3 invMatrix(MInverseRotation, matrix);
    Matrix3 rotZ(MRotationZ, _angle);
    Matrix3 rotation = matrix * rotZ * invMatrix;

    Vector3 rightS = info.w * right;
    Vector3 downS = info.h * down;

    _position = object->PositionModelToWorld(info.posTL + info.x * right + info.y * down);
    _right = object->DirectionModelToWorld(rightS * rotation);
    _down = object->DirectionModelToWorld(downS * rotation);

    // Override the 2D bounds (set by Control::UpdateInfo via screen-
    // space linear interpolation between the container's TL/BR) with
    // the actual screen-projected bounding rect of the 3D quad.  The
    // linear-interp version assumes the container projects to a
    // screen-space rectangle, which is wrong under perspective for
    // sub-controls that aren't centred — for chevrons at info.x ≈
    // 0.85 the linear-interp center can fall several pixels outside
    // the 3D quad's true projected footprint, so a click computed
    // from `cx = _x + _w/2` then ray-cast against the 3D quad in
    // IsInside misses entirely.
    //
    // Project the four corners of the actual 3D quad and take their
    // axis-aligned bounding rect on screen.  The center of that rect
    // is guaranteed to land inside the projected quad.
    DrawCoord c0 = SceneToScreen(_position);
    DrawCoord c1 = SceneToScreen(_position + _right);
    DrawCoord c2 = SceneToScreen(_position + _down);
    DrawCoord c3 = SceneToScreen(_position + _right + _down);
    float minX = floatMin(floatMin(c0.x, c1.x), floatMin(c2.x, c3.x));
    float minY = floatMin(floatMin(c0.y, c1.y), floatMin(c2.y, c3.y));
    float maxX = floatMax(floatMax(c0.x, c1.x), floatMax(c2.x, c3.x));
    float maxY = floatMax(floatMax(c0.y, c1.y), floatMax(c2.y, c3.y));
    info._control->SetPos(minX, minY, maxX - minX, maxY - minY);
}

bool Control3D::IsInside(float x, float y)
{
    const float EPSILON = 1e-9;
    _u = 0;
    _v = 0;
    if (_right.SquareSize() < EPSILON)
    {
        return false;
    }
    if (_down.SquareSize() < EPSILON)
    {
        return false;
    }

    Vector3 b = ControlObject::Convert2DTo3D(Point2DFloat(x, y), 1);
    /*
    AspectSettings as;
    GEngine->GetAspectSettings(as);

    Vector3 b = Vector3
    (
        (x - 0.5) * InvCameraZoom * as.leftFOV, as.topFOV * (0.5 - y) * InvCameraZoom, 1.0
    );
    */
    float pxy = _position.Y() * b.X() - _position.X() * b.Y();
    float rxy = _right.Y() * b.X() - _right.X() * b.Y();
    float dxy = _down.Y() * b.X() - _down.X() * b.Y();
    float pxz = _position.Z() * b.X() - _position.X() * b.Z();
    float rxz = _right.Z() * b.X() - _right.X() * b.Z();
    float dxz = _down.Z() * b.X() - _down.X() * b.Z();

    float dr = dxy * rxz - dxz * rxy;
    float pr = pxy * rxz - pxz * rxy;

    _v = -pr / dr;
    _u = -(pxy + dxy * _v) / rxy;
    return _u >= 0 && _u <= 1 && _v >= 0 && _v <= 1;
}

C3DStatic::C3DStatic(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DSTATIC, idc, cls)
{
    _enabled = false;
    _texture = nullptr;
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    _color = GetPackedColor(cls >> "color");
    if ((_style & ST_TYPE) == ST_BACKGROUND)
    {
        _bgColor = GetPackedColor(cls >> "colorBackground");
    }
    else
    {
        _bgColor = PackedBlack;
    }
    _hCoef = 1;
    if (cls.FindEntry("h2"))
    {
        float h = cls >> "h";
        float h2 = cls >> "h2";
        if (h > 0)
        {
            _hCoef = h2 / h;
        }
    }
    _zBias = cls.FindEntry("zBias") ? (float)(cls >> "zBias") : 0.0f;

    if ((_style & ST_TYPE) == ST_MULTI)
    {
        _maxLines = cls >> "lines";
    }
    else
    {
        _maxLines = 1;
    }

    RString text = cls >> "text";
    SetText(text);
    _textCls = &cls; // set after SetText since SetText clears it
}

void C3DStatic::SetText(RString text)
{
    const int type = _style & ST_TYPE;
    const RString visibleText = type == ST_PICTURE ? text : Decode3DControlText(text);
    if (!strcmp(_text, visibleText))
    {
        return;
    }

    _textCls = nullptr; // diverged from resource template
    _text = visibleText;
    if (type == ST_PICTURE)
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
    }
}

void C3DStatic::FormatText()
{
    if ((_style & ST_TYPE) != ST_MULTI)
    {
        return;
    }

    _lines.Clear();
    _lines.Add(0);

    Vector3 down = (1.0 / _maxLines) * _down;
    Vector3 up = -down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();

    float lineWidth = _right.Size();

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
            if (c == '\n')
            {
                // Explicit line break — start the next line after the newline,
                // regardless of width (width-wrap still applies within a line).
                _lines.Add(p - (const char*)_text);
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
            width += GEngine->GetText3DWidth(right, _font, temp).Size();

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

void C3DStatic::OnDraw(float alpha)
{
    PackedColor color = ModAlpha(_color, alpha);
    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 position = _position - 0.002 * normal;

    switch (_style & ST_TYPE)
    {
        case ST_PICTURE:
            GEngine->Draw3D(position, _down, _right, ClipAll, color, DisableSun, _texture);
            break;
        case ST_BACKGROUND:
        {
            PackedColor bgColor = ModAlpha(_bgColor, alpha);
            // Apply optional camera-forward Z bias so this fill wins
            // depth ties against neighbouring coplanar ST_BACKGROUND
            // fills (e.g. slider tracks layered over a focus highlight).
            Vector3 fillPos = (_zBias != 0.0f) ? (_position - _zBias * normal) : _position;
            GEngine->Draw3D(fillPos, _down, _right, ClipAll, bgColor, DisableSun, nullptr);

            GEngine->DrawLine3D(position, position + _right, color, DisableSun);
            GEngine->DrawLine3D(position + _right, position + _right + _down, color, DisableSun);
            GEngine->DrawLine3D(position + _right + _down, position + _down, color, DisableSun);
            GEngine->DrawLine3D(position + _down, position, color, DisableSun);

            if (_text.GetLength() > 0)
            {
                DrawText(_text, position, _down, color);
            }
        }
        break;
        case ST_FRAME:
            if (_text.GetLength() > 0)
            {
                Vector3 up = -_hCoef * _down;
                Vector3 right = 0.75 * up.Size() * _right.Normalized();

                Vector3 width = GEngine->GetText3DWidth(right, _font, _text);
                Vector3 borderH = -up;
                Vector3 borderW = up.Size() * right.Normalized();
                if ((2 * borderW + width + 2 * borderW).Size() > _right.Size())
                {
                    goto NoText;
                }
                GEngine->DrawText3D(position + 2 * borderW, up, right, ClipAll, _font, color, DisableSun, _text);
                Vector3 top = position + 0.5 * borderW + 0.5 * borderH;
                Vector3 down = _down - borderH;
                right = _right - borderW;
                GEngine->DrawLine3D(top, position + 2 * borderW + 0.5 * borderH, color, DisableSun);
                GEngine->DrawLine3D(position + 2 * borderW + 0.5 * borderH + width, top + right, color, DisableSun);
                GEngine->DrawLine3D(top + right, top + right + down, color, DisableSun);
                GEngine->DrawLine3D(top + right + down, top + down, color, DisableSun);
                GEngine->DrawLine3D(top + down, top, color, DisableSun);
                /*
                            float width = GLOB_ENGINE->GetTextWidth(size, _font, _text);
                            float heigth = size;
                            const float border = _scale * 0.01;
                            if (border + width + border <= _w)
                            {
                                GLOB_ENGINE->DrawText
                                (
                                    _x + border, _y, size,
                                    _x, _y, _w, _h,
                                    _font, ftColor, _text
                                );
                                float top = yy + 0.5 * heigth * h;
                                GLOB_ENGINE->DrawLine
                                (
                                    xx, top, xx + border * w, top, ftColor, ftColor
                                );
                                GLOB_ENGINE->DrawLine
                                (
                                    xx + (border + width) * w, top, xx + ww - 1, top, ftColor, ftColor
                                );
                                GLOB_ENGINE->DrawLine
                                (
                                    xx, top, xx, yy + hh - 1, ftColor, ftColor
                                );
                                GLOB_ENGINE->DrawLine
                                (
                                    xx + ww - 1, top, xx + ww - 1, yy + hh - 1, ftColor, ftColor
                                );
                                DrawBottom(0, ftColor);
                                return;
                            }
                */
            }
            else
            {
            NoText:
                GEngine->DrawLine3D(position, position + _right, color, DisableSun);
                GEngine->DrawLine3D(position + _right, position + _right + _down, color, DisableSun);
                GEngine->DrawLine3D(position + _right + _down, position + _down, color, DisableSun);
                GEngine->DrawLine3D(position + _down, position, color, DisableSun);
            }
            break;
        case ST_MULTI:
        {
            Vector3 top = position;
            Vector3 down = (1.0 / _maxLines) * _down;
            for (int i = 0; i < _lines.Size() && i < _maxLines; i++)
            {
                DrawText(GetLine(i), top, down, color);
                top += down;
            }
        }
        break;
        case ST_WITH_RECT:
        {
            GEngine->DrawLine3D(position, position + _right, color, DisableSun);
            GEngine->DrawLine3D(position + _right, position + _right + _down, color, DisableSun);
            GEngine->DrawLine3D(position + _right + _down, position + _down, color, DisableSun);
            GEngine->DrawLine3D(position + _down, position, color, DisableSun);
            // continue - draw text
        }
        default:
            DrawText(_text, position, _down, color);
            break;
    }
}

RString C3DStatic::GetLine(int i) const
{
    if (i < 0)
    {
        return "";
    }
    PoseidonAssert(i < _lines.Size());

    int from = _lines[i];
    int to = i + 1 < _lines.Size() ? _lines[i + 1] : _text.GetLength();
    return _text.Substring(from, to);
}

void C3DStatic::DrawText(const char* text, Vector3Par top, Vector3Par down, PackedColor color)
{
    // formatting
    Vector3 pos = top;
    Vector3 up = -down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();

    Vector3 offset = VZero;
    Vector3 width = GEngine->GetText3DWidth(right, _font, _text);
    switch (_style & ST_HPOS)
    {
        case ST_RIGHT:
            offset = _right - width;
            break;
        case ST_CENTER:
            offset = 0.5 * (_right - width);
            break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) break;
    }
    pos += offset;

    float invRSize = 1.0 / right.Size();
    float x1c = 0, x2c = _right.Size() * invRSize;
    if (width.SquareSize() > _right.SquareSize())
    {
        float offsetSize = offset.Size() * invRSize;
        x1c += offsetSize;
        x2c += offsetSize;
    }

    GEngine->DrawText3D(pos, up, right, ClipAll, _font, color, DisableSun, text, x1c, 0, x2c, 1);
}

C3DEdit::C3DEdit(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DEDIT, idc, cls), CEditContainer(cls)
{
    _size = cls >> "size";

    RString text = cls >> "text";
    SetText(Decode3DControlText(text));
    // note: SetText does EnsureVisible

    if ((_style & ST_TYPE) == ST_MULTI)
    {
        _maxLines = cls >> "lines";
    }
    else
    {
        _maxLines = 1;
    }
}

bool C3DEdit::IsMulti() const
{
    return (_style & ST_TYPE) == ST_MULTI;
}

void C3DEdit::OnDraw(float alpha)
{
    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 position = _position - 0.002 * normal;

    // frame
    GEngine->DrawLine3D(position, position + _right, ftColor, DisableSun);
    GEngine->DrawLine3D(position + _right, position + _right + _down, ftColor, DisableSun);
    GEngine->DrawLine3D(position + _right + _down, position + _down, ftColor, DisableSun);
    GEngine->DrawLine3D(position + _down, position, ftColor, DisableSun);

    if (_text.GetLength() > 0)
    {
        if ((_style & ST_TYPE) == ST_MULTI)
        {
            Vector3 top = position;
            Vector3 down = (1.0 / _maxLines) * _down;
            for (int i = FirstLine(), j = 0; i < _lines.Size() && j < _maxLines; i++, j++)
            {
                DrawText(GetLine(i), _lines[i], top, down, alpha);
                top += down;
            }
        }
        else
        {
            DrawText(_text.Substring(_firstVisible, _text.GetLength()), _firstVisible, position, _down, alpha);
        }
    }

    // draw caret
    if (!IsFocused())
    {
        return;
    }
    bool state = ((Glob.uiTime.toFloat() - toIntFloor(Glob.uiTime.toFloat())) < 0.5);
    if (!state)
    {
        return;
    }

    Vector3 top;
    Vector3 down;
    Vector3 border = 0.02 * _right;
    float rightSize = (_right - border).Size();
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        down = (1.0 / _maxLines) * _down;
        int cur = CurLine();
        int first = FirstLine();
        if (cur - first < 0 || cur - first >= _maxLines)
        {
            return;
        }
        top = position + (cur - first) * down;
        Vector3 dir = PosToDir(GetLine(cur), _blockEnd - (cur < 0 ? 0 : _lines[cur]));
        if (dir.Size() > rightSize)
        {
            return;
        }
        top += dir;
    }
    else
    {
        down = _down;
        top = position;
        Vector3 dir = PosToDir(_text.Substring(_firstVisible, _text.GetLength()), _blockEnd - _firstVisible);
        if (dir.Size() > rightSize)
        {
            return;
        }
        top += dir;
    }
    top += 0.5 * (1.0 - _size) * down;
    down *= _size;

    GEngine->DrawLine3D(top, top + down, ftColor, DisableSun);
}

bool C3DEdit::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoKeyDown(nChar, nRepCnt, nFlags);
}

bool C3DEdit::OnSetFocus(bool up, bool def)
{
    if (!Control3D::OnSetFocus(up, def))
    {
        return false;
    }
    BeginPlatformTextInput();
    return true;
}

bool C3DEdit::OnKillFocus()
{
    EndPlatformTextInput();
    return Control3D::OnKillFocus();
}

bool C3DEdit::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoChar(nChar, nRepCnt, nFlags);
}

bool C3DEdit::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return CEditContainer::DoIMEChar(nChar, nRepCnt, nFlags);
}

bool C3DEdit::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
    return CEditContainer::DoIMEComposition(nChar, nFlags);
}

void C3DEdit::OnLButtonDown(float x, float y)
{
    BeginPlatformTextInput();

    IsInside(x, y);
    int pos = FindPos(_u, _v);
    if (!InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) &&
        !InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
    {
        _blockBegin = pos;
    }
    _blockEnd = pos;
    EnsureVisible(_blockEnd);
}

void C3DEdit::OnLButtonUp(float x, float y) {}

void C3DEdit::OnMouseMove(float x, float y, bool active)
{
    if (InputSubsystem::Instance().IsMouseLeftDown())
    {
        IsInside(x, y);
        _blockEnd = FindPos(_u, _v);
        EnsureVisible(_blockEnd);
    }
}

int C3DEdit::FindPos(float x, float y)
{
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        if (_text.GetLength() == 0)
        {
            return 0;
        }
        else
        {
            int line = FirstLine() + toIntFloor(y / _maxLines);
            saturate(line, 0, _lines.Size() - 1);
            return _lines[line] + XToPos(GetLine(line), x);
        }
    }
    else
    {
        return _firstVisible + XToPos(_text.Substring(_firstVisible, _text.GetLength()), x);
    }
}

Vector3 C3DEdit::PosToDir(RString text, int pos) const
{
    Vector3 down;
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        down = (1.0 / _maxLines) * _down;
    }
    else
    {
        down = _down;
    }
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    Vector3 border = 0.02 * _right;

    Vector3 dir;
    int style = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        style = _style & ST_HPOS;
    }
    switch (style)
    {
        case ST_RIGHT:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            dir = _right - width - border;
        }
        break;
        case ST_CENTER:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            dir = 0.5 * (_right - width);
        }
        break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) dir = border;
            break;
    }

    RString str = text.Substring(0, pos);
    return dir + GEngine->GetText3DWidth(right, _font, str);
}

float C3DEdit::PosToX(RString text, int pos) const
{
    Vector3 dir = PosToDir(text, pos);
    return dir.Size() / _right.Size();
}

int C3DEdit::XToPos(RString text, float x) const
{
    Vector3 down;
    if ((_style & ST_TYPE) == ST_MULTI)
    {
        down = (1.0 / _maxLines) * _down;
    }
    else
    {
        down = _down;
    }
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    Vector3 border = 0.02 * _right;

    Vector3 dir;
    int style = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        style = _style & ST_HPOS;
    }
    switch (style)
    {
        case ST_RIGHT:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            dir = _right - width - border;
        }
        break;
        case ST_CENTER:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            dir = 0.5 * (_right - width);
        }
        break;
        default:
            PoseidonAssert((_style & ST_HPOS) == ST_LEFT) dir = border;
            break;
    }

    x *= _right.Size();
    float dirSize = dir.Size();
    if (x < dirSize)
    {
        return 0;
    }

    int n = strlen(text);
    for (int i = 0; i < n;)
    {
        int j = i;
        char temp[8];
        Vector3 cw;
        i = CopyTextElement(text, i, _font->GetLangID(), temp, sizeof(temp));
        cw = GEngine->GetText3DWidth(right, _font, temp);

        float cwSize = cw.Size();
        if (dirSize + cwSize > x)
        {
            if (x - dirSize < dirSize + cwSize - x)
            {
                return j;
            }
            else
            {
                return i;
            }
        }
        dirSize += cwSize;
    }
    return n;
}

void C3DEdit::EnsureVisible(int pos)
{
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

        int lines = _lines.Size() - first;
        if (lines <= _maxLines)
        {
            first += lines - _maxLines;
            saturateMax(first, 0);
        }
        else if (first + _maxLines <= cur)
        {
            first = cur - _maxLines + 1;
        }
        _firstVisible = _lines[first];
    }
    else
    {
        Vector3 up = -_size * _down;
        Vector3 right = 0.75 * up.Size() * _right.Normalized();
        Vector3 border = 0.02 * _right;

        saturateMin(_firstVisible, _blockEnd);
        float wlimit = (_right - 2.0 * border).Size();
        float w = GEngine->GetText3DWidth(right, _font, _text.Substring(_firstVisible, _text.GetLength())).Size();
        if (w <= wlimit)
        {
            while (_firstVisible > 0)
            {
                int prev = PrevPos(_firstVisible);
                w += GEngine->GetText3DWidth(right, _font, _text.Substring(prev, _firstVisible)).Size();
                if (w > wlimit)
                {
                    break;
                }
                _firstVisible = prev;
            }
        }
        else
        {
            w = GEngine->GetText3DWidth(right, _font, _text.Substring(_firstVisible, _blockEnd)).Size();
            while (w > wlimit)
            {
                int prev = _firstVisible;
                _firstVisible = NextPos(_firstVisible);
                w -= GEngine->GetText3DWidth(right, _font, _text.Substring(prev, _firstVisible)).Size();
            }
        }
    }
}

void C3DEdit::DrawText(const char* text, int offset, Vector3Par top, Vector3Par down, float alpha)
{
    // formatting
    Vector3 position = top + 0.5 * (1.0 - _size) * down;
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    Vector3 border = 0.02 * _right;
    float rightSize = (_right - 2.0 * border).Size();
    float x2c = rightSize / right.Size();

    int pos = ST_LEFT;
    if ((_style & ST_TYPE) == ST_MULTI || _firstVisible == 0)
    {
        pos = _style & ST_HPOS;
    }
    switch (pos)
    {
        case ST_RIGHT:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            position += _right - width - border;
        }
        break;
        case ST_CENTER:
        {
            Vector3 width = GEngine->GetText3DWidth(right, _font, text);
            position += 0.5 * (_right - width);
        }
        break;
        default:
            PoseidonAssert(pos == ST_LEFT) position += border;
            break;
    }

    // block
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
            Vector3 x1 = VZero;
            char buffer[1024];
            if (from > static_cast<int>(offset)) // from is int, offset could be unsigned
            {
                snprintf(buffer, sizeof(buffer), "%s", (const char*)text);
                buffer[from - offset] = 0;
                x1 = GEngine->GetText3DWidth(right, _font, buffer);
            }
            snprintf(buffer, sizeof(buffer), "%s", (const char*)text);
            if (to < static_cast<int>(offset + strlen(text)))
            { // strlen returns size_t, to is int
                buffer[to - offset] = 0;
            }
            Vector3 x2 = GEngine->GetText3DWidth(right, _font, buffer);
            if (x2.Size() > rightSize)
            {
                x2 = _right - 2.0 * border;
            }
            GEngine->Draw3D(position + x1, -up, x2 - x1, ClipAll, selColor, DisableSun, nullptr, 0, 0, 1, 1);
        }
    }

    // text
    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    GEngine->DrawText3D(position, up, right, ClipAll, _font, ftColor, DisableSun, text, 0, 0, x2c, 1);
}

void C3DEdit::FormatText()
{
    if ((_style & ST_TYPE) != ST_MULTI)
    {
        return;
    }

    _lines.Clear();
    _lines.Add(0);

    Vector3 down = (1.0 / _maxLines) * _down;
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    Vector3 border = 0.02 * _right;

    float lineWidth = (_right - 2.0 * border).Size();

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
            if (c == '\n')
            {
                // Explicit line break — start the next line after the newline,
                // regardless of width (width-wrap still applies within a line).
                _lines.Add(p - (const char*)_text);
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
            width += GEngine->GetText3DWidth(right, _font, temp).Size();

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

C3DActiveText::C3DActiveText(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DACTIVETEXT, idc, cls)
{
    _texture = nullptr;
    RString text = cls >> "text";
    SetText(text);
    _textCls = &cls; // set after SetText since SetText clears it
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    _color = GetPackedColor(cls >> "color");
    _colorActive = GetPackedColor(cls >> "colorActive");
    // Optional focus-highlight background, see CActiveText ctor.
    _colorFocusBg = PackedColor(0, 0, 0, 0);
    if (const ParamEntry* fbg = cls.FindEntry("colorFocusBg"))
    {
        _colorFocusBg = GetPackedColor(*fbg);
    }

    GetValue(_enterSound, cls >> "soundEnter");
    GetValue(_pushSound, cls >> "soundPush");
    GetValue(_clickSound, cls >> "soundClick");
    GetValue(_escapeSound, cls >> "soundEscape");

    _active = false;

    if (const ParamEntry* dfl = cls.FindEntry("drawFocusLine"))
    {
        _drawFocusLine = (int)*dfl != 0;
    }

    const ParamEntry* entry = cls.FindEntry("action");
    if (entry)
    {
        _action = *entry;
    }
}

void C3DActiveText::SetText(RString text)
{
    const int type = _style & ST_TYPE;
    const RString visibleText = type == ST_PICTURE ? text : Decode3DControlText(text);
    if (!strcmp(_text, visibleText))
    {
        return;
    }

    _textCls = nullptr; // diverged from resource template
    _text = visibleText;
    if (type == ST_PICTURE)
    {
        text = Poseidon::FindPicture(text);
        text.Lower();
        _texture = GlobLoadTexture(text);
        if (_texture)
        {
            _texture->SetMaxSize(1024); // no limits
        }
    }
}

bool C3DActiveText::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (IsUiConfirmKey(nChar))
    {
        _returnDown = true;
        PlaySound(_pushSound);
        return true;
    }
    return false;
}

bool C3DActiveText::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (IsUiConfirmKey(nChar))
    {
        // Orphan KEY_UP — KEY_DOWN went to a different control (typically
        // a since-destroyed display) and only the release reached us.  Do
        // NOT synthesize a click; that's how a single Enter tap on a
        // closing screen would re-open the parent's selected entry.
        if (!_returnDown)
            return false;
        _returnDown = false;
        PlaySound(_clickSound);
        if (_action.GetLength() > 0)
        {
            GameState* gstate = GWorld->GetGameState();
            gstate->Execute(_action);
            GWorld->SimulateScripts();
        }
        _parent->OnButtonClicked(IDC());
        return true;
    }
    return false;
}

void C3DActiveText::OnMouseMove(float x, float y, bool active)
{
    OnMouseHold(x, y, active);
}

void C3DActiveText::OnMouseHold(float x, float y, bool active)
{
    bool newActive = active && IsInside(x, y);
    if (newActive && !_active)
    {
        PlaySound(_enterSound);
    }
    _active = newActive;
}

void C3DActiveText::OnLButtonDown(float x, float y)
{
    _pressX = _x;
    _pressY = _y;
    _pressW = _w;
    _pressH = _h;
    PlaySound(_pushSound);
}

void C3DActiveText::OnLButtonUp(float x, float y)
{
    if (IsInside(x, y) || PointInRect(x, y, _pressX, _pressY, _pressW, _pressH))
    {
        PlaySound(_clickSound);
        if (_action.GetLength() > 0)
        {
            GameState* gstate = GWorld->GetGameState();
            gstate->Execute(_action);
            GWorld->SimulateScripts();
        }
        _parent->OnButtonClicked(IDC());
    }
    else
    {
        PlaySound(_escapeSound);
    }
}

void C3DActiveText::OnDraw(float alpha)
{
    PackedColor color = ModAlpha(_active ? _colorActive : _color, alpha);
    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 position = _position - 0.002 * normal;

    // Optional focus highlight — filled quad on the host's surface
    // drawn from colorFocusBg.  Pulled slightly camera-forward of the
    // surface (0.001) so it wins the depth tie against the model
    // texture.  The text itself sits at -0.002 (already further
    // camera-forward), so it lands on top of this fill.  The border
    // outline reuses the text colour (OPT_C_CRT_GREEN-style attribute)
    // so a single colorFocusBg attribute on the resource produces both
    // fill and border without needing a paired colorFocusBorder.
    if (IsFocused() && _colorFocusBg.A8() > 0)
    {
        PackedColor bg = ModAlpha(_colorFocusBg, alpha);
        Vector3 fillPos = _position - 0.001 * normal;
        GEngine->Draw3D(fillPos, _down, _right, ClipAll, bg, DisableSun, nullptr);
        PackedColor border = ModAlpha(_color, alpha);
        GEngine->DrawLine3D(fillPos, fillPos + _right, border, DisableSun);
        GEngine->DrawLine3D(fillPos + _right, fillPos + _right + _down, border, DisableSun);
        GEngine->DrawLine3D(fillPos + _right + _down, fillPos + _down, border, DisableSun);
        GEngine->DrawLine3D(fillPos + _down, fillPos, border, DisableSun);
    }

    switch (_style & ST_TYPE)
    {
        case ST_PICTURE:
            GEngine->Draw3D(position, _down, _right, ClipAll, color, DisableSun, _texture);
            break;
        default:
        {
            Vector3 up = -_down;
            Vector3 right = 0.75 * up.Size() * _right.Normalized();

            Vector3 offset = VZero;
            Vector3 width = GEngine->GetText3DWidth(right, _font, _text);
            switch (_style & ST_HPOS)
            {
                case ST_RIGHT:
                    offset = _right - width;
                    break;
                case ST_CENTER:
                    offset = 0.5 * (_right - width);
                    break;
                default:
                    PoseidonAssert((_style & ST_HPOS) == ST_LEFT) break;
            }
            position += offset;

            float invRSize = 1.0 / right.Size();
            float x1c = 0, x2c = _right.Size() * invRSize;
            bool clip = width.SquareSize() > _right.SquareSize();
            if (clip)
            {
                float offsetSize = offset.Size() * invRSize;
                x1c += offsetSize;
                x2c += offsetSize;
            }

            bool focused = IsFocused();
            bool selected = IsDefault() && !_parent->GetFocused()->CanBeDefault();
            if (_drawFocusLine && (focused || selected))
            {
                PackedColor col;
                if (focused)
                {
                    col = color;
                }
                else
                {
                    col = ModAlpha(color, 0.5);
                }

                if (clip)
                {
                    GEngine->DrawLine3D(position - offset + _down, position - offset + _down + _right, col, DisableSun);
                }
                else
                {
                    GEngine->DrawLine3D(position + _down, position + _down + width, col, DisableSun);
                }
            }

            GEngine->DrawText3D(position, up, right, ClipAll, _font, color, DisableSun, _text, x1c, 0, x2c, 1);
        }
        break;
    }
}

C3DSlider::C3DSlider(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DSLIDER, idc, cls), CSliderContainer(cls)
{
}

void C3DSlider::OnLButtonDown(float x, float y)
{
    IsInside(x, y); // calculate _u, _v

    if ((_style & SL_DIR) == SL_VERT)
    {
        /*
                float spinHeight = (1.33 * 0.6) * _w;
                const float thumbHeight = 0.02;
                float top = _y + spinHeight + 0.5 * thumbHeight;
                float fieldHeight = _h - 2 * spinHeight - thumbHeight;

                float coef = 0;
                if (_maxPos > _minPos)
                    coef = 1.0 / (_maxPos - _minPos);
                float thumbPos = top + fieldHeight * (_curPos - _minPos) * coef;

                if (y - _y < spinHeight)
                {
                    SetThumbPos(_curPos - _lineStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (y - _y > _h - spinHeight)
                {
                    SetThumbPos(_curPos + _lineStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (y < thumbPos - 0.5 * thumbHeight)
                {
                    SetThumbPos(_curPos - _pageStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (y > thumbPos + 0.5 * thumbHeight)
                {
                    SetThumbPos(_curPos + _pageStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else
                {
                    _thumbLocked = true;
                    _thumbOffset = y - thumbPos;
                }
        */
    }
    else
    {
        float ratio = _down.Size() / _right.Size();
        float spinWidth = 0.6 * ratio;
        float thumbWidth = 1.0 * ratio;
        float left = spinWidth + 0.5 * thumbWidth;
        float fieldWidth = 1.0 - 2 * spinWidth - thumbWidth;

        float coef = 0;
        if (_maxPos > _minPos)
        {
            coef = 1.0 / (_maxPos - _minPos);
        }
        float thumbPos = left + fieldWidth * (_curPos - _minPos) * coef;

        if (_u < spinWidth)
        {
            SetThumbPos(_curPos - _lineStep);
            if (_parent)
            {
                _parent->OnSliderPosChanged(IDC(), _curPos);
            }
        }
        else if (_u > 1.0 - spinWidth)
        {
            SetThumbPos(_curPos + _lineStep);
            if (_parent)
            {
                _parent->OnSliderPosChanged(IDC(), _curPos);
            }
        }
        else if (_u < thumbPos - 0.5 * thumbWidth)
        {
            SetThumbPos(_curPos - _pageStep);
            if (_parent)
            {
                _parent->OnSliderPosChanged(IDC(), _curPos);
            }
        }
        else if (_u > thumbPos + 0.5 * thumbWidth)
        {
            SetThumbPos(_curPos + _pageStep);
            if (_parent)
            {
                _parent->OnSliderPosChanged(IDC(), _curPos);
            }
        }
        else
        {
            _thumbLocked = true;
            _thumbOffset = _u - thumbPos;
        }

        /*
                float spinWidth = (0.75 * 0.6) * _h;
                const float thumbWidth = 0.015;
                float left = _x + spinWidth + 0.5 * thumbWidth;
                float fieldWidth = _w - 2 * spinWidth - thumbWidth;

                float coef = 0;
                if (_maxPos > _minPos)
                    coef = 1.0 / (_maxPos - _minPos);
                float thumbPos = left + fieldWidth * (_curPos - _minPos) * coef;

                if (x - _x < spinWidth)
                {
                    SetThumbPos(_curPos - _lineStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (x - _x > _w - spinWidth)
                {
                    SetThumbPos(_curPos + _lineStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (x < thumbPos - 0.5 * thumbWidth)
                {
                    SetThumbPos(_curPos - _pageStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else if (x > thumbPos + 0.5 * thumbWidth)
                {
                    SetThumbPos(_curPos + _pageStep);
                    if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                }
                else
                {
                    _thumbLocked = true;
                    _thumbOffset = x - thumbPos;
                }
        */
    }
}

void C3DSlider::OnLButtonUp(float x, float y)
{
    _thumbLocked = false;
}

void C3DSlider::OnMouseMove(float x, float y, bool active)
{
    IsInside(x, y); // calculate _u, _v

    if (_thumbLocked)
    {
        if ((_style & SL_DIR) == SL_VERT)
        {
            /*
                        float spinHeight = (1.33 * 0.7) * _w;
                        const float thumbHeight = 0.02;
                        float top = _y + spinHeight + 0.5 * thumbHeight;
                        float fieldHeight = _h - 2 * spinHeight - thumbHeight;

                        float coef = 0;
                        if (_maxPos > _minPos)
                            coef = _maxPos - _minPos;

                        if (y - _thumbOffset < top)
                        {
                            SetThumbPos(_minPos);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
                        else if (y - _thumbOffset > top + fieldHeight)
                        {
                            SetThumbPos(_maxPos);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
                        else
                        {
                            SetThumbPos(_minPos + (y - _thumbOffset - top) * coef / fieldHeight);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
            */
        }
        else
        {
            float ratio = _down.Size() / _right.Size();
            float spinWidth = 0.6 * ratio;
            float thumbWidth = 1.0 * ratio;
            float left = spinWidth + 0.5 * thumbWidth;
            float fieldWidth = 1.0 - 2 * spinWidth - thumbWidth;

            float coef = 0;
            if (_maxPos > _minPos)
            {
                coef = _maxPos - _minPos;
            }

            float thumbPos = _u - _thumbOffset;
            if (thumbPos < left)
            {
                SetThumbPos(_minPos);
            }
            else if (thumbPos > left + fieldWidth)
            {
                SetThumbPos(_maxPos);
            }
            else
            {
                SetThumbPos(_minPos + (thumbPos - left) * coef / fieldWidth);
            }
            if (_parent)
            {
                _parent->OnSliderPosChanged(IDC(), _curPos);
            }

            /*
                        float spinWidth = (0.75 * 0.7) * _h;
                        const float thumbWidth = 0.015;
                        float left = _x + spinWidth + 0.5 * thumbWidth;
                        float fieldWidth = _w - 2 * spinWidth - thumbWidth;

                        float coef = 0;
                        if (_maxPos > _minPos)
                            coef = _maxPos - _minPos;

                        if (x - _thumbOffset < left)
                        {
                            SetThumbPos(_minPos);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
                        else if (x - _thumbOffset > left + fieldWidth)
                        {
                            SetThumbPos(_maxPos);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
                        else
                        {
                            SetThumbPos(_minPos + (x - _thumbOffset - left) * coef / fieldWidth);
                            if (_parent) _parent->OnSliderPosChanged(IDC(), _curPos);
                        }
            */
        }
    }
}

void C3DSlider::OnDraw(float alpha)
{
    PackedColor color = ModAlpha(_color, alpha);
    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 position = _position - 0.002 * normal;

    if ((_style & SL_DIR) == SL_VERT)
    {
        Vector3 bigLine = 0.6 * _right;
        Vector3 smallLine = 0.3 * _right;
        Vector3 space = 0.15 * _right;
        Vector3 thumbWidth = _right - bigLine - space;

        float w = _right.Size();
        Vector3 downNorm = _down.Normalized();
        Vector3 spinHeight = 0.6 * w * downNorm;
        Vector3 thumbHeight = w * downNorm;
        Vector3 fieldHeight = _down - 2.0 * spinHeight - thumbHeight;

        Vector3 top = position + spinHeight + 0.5 * thumbHeight;
        Vector3 bottom = top + fieldHeight;

        float coef = 0;
        if (_maxPos > _minPos)
        {
            coef = 1.0 / (_maxPos - _minPos);
        }
        Vector3 thumbPos = top + fieldHeight * (_curPos - _minPos) * coef + bigLine + space;

        // draw left spin
        Vector3 center = position + 0.5 * bigLine;
        GEngine->DrawLine3D(center, position + spinHeight, color, DisableSun);
        GEngine->DrawLine3D(position + spinHeight, position + spinHeight + bigLine, color, DisableSun);
        GEngine->DrawLine3D(position + spinHeight + bigLine, center, color, DisableSun);

        // draw right spin
        center += _down;
        GEngine->DrawLine3D(center, position + _down - spinHeight, color, DisableSun);
        GEngine->DrawLine3D(position + _down - spinHeight, position + _down - spinHeight + bigLine, color, DisableSun);
        GEngine->DrawLine3D(position + _down - spinHeight + bigLine, center, color, DisableSun);

        // draw background
        GEngine->DrawLine3D(top + bigLine, bottom + bigLine, color, DisableSun);
        int lines = 1;
        float lineDist = fieldHeight.Size();
        while (lineDist > w)
        {
            lines *= 2;
            lineDist *= 0.5;
        }
        Vector3 pos = top + bigLine;
        Vector3 diff = lineDist * downNorm;
        for (int i = 0; i <= lines; i++)
        {
            Vector3 right = (i % 2 == 0 || lines == 1) ? bigLine : smallLine;
            GEngine->DrawLine3D(pos, pos - right, color, DisableSun);
            pos += diff;
        }

        // draw thumb
        GEngine->DrawLine3D(thumbPos, thumbPos + thumbWidth - 0.5 * thumbHeight, color, DisableSun);
        GEngine->DrawLine3D(thumbPos + thumbWidth - 0.5 * thumbHeight, thumbPos + thumbWidth + 0.5 * thumbHeight, color,
                            DisableSun);
        GEngine->DrawLine3D(thumbPos + thumbWidth + 0.5 * thumbHeight, thumbPos, color, DisableSun);
    }
    else
    {
        Vector3 bigLine = 0.6 * _down;
        Vector3 smallLine = 0.3 * _down;
        Vector3 space = 0.15 * _down;
        Vector3 thumbHeight = _down - bigLine - space;

        float h = _down.Size();
        Vector3 rightNorm = _right.Normalized();
        Vector3 spinWidth = 0.6 * h * rightNorm;
        Vector3 thumbWidth = h * rightNorm;
        Vector3 fieldWidth = _right - 2.0 * spinWidth - thumbWidth;

        Vector3 left = position + spinWidth + 0.5 * thumbWidth;
        Vector3 right = left + fieldWidth;

        float coef = 0;
        if (_maxPos > _minPos)
        {
            coef = 1.0 / (_maxPos - _minPos);
        }
        Vector3 thumbPos = left + fieldWidth * (_curPos - _minPos) * coef + bigLine + space;

        // draw left spin
        Vector3 center = position + 0.5 * bigLine;
        GEngine->DrawLine3D(center, position + spinWidth, color, DisableSun);
        GEngine->DrawLine3D(position + spinWidth, position + spinWidth + bigLine, color, DisableSun);
        GEngine->DrawLine3D(position + spinWidth + bigLine, center, color, DisableSun);

        // draw right spin
        center += _right;
        GEngine->DrawLine3D(center, position + _right - spinWidth, color, DisableSun);
        GEngine->DrawLine3D(position + _right - spinWidth, position + _right - spinWidth + bigLine, color, DisableSun);
        GEngine->DrawLine3D(position + _right - spinWidth + bigLine, center, color, DisableSun);

        // draw background
        GEngine->DrawLine3D(left + bigLine, right + bigLine, color, DisableSun);
        int lines = 1;
        float lineDist = fieldWidth.Size();
        while (lineDist > h)
        {
            lines *= 2;
            lineDist *= 0.5;
        }
        Vector3 pos = left + bigLine;
        Vector3 diff = lineDist * rightNorm;
        for (int i = 0; i <= lines; i++)
        {
            Vector3 down = (i % 2 == 0 || lines == 1) ? bigLine : smallLine;
            GEngine->DrawLine3D(pos, pos - down, color, DisableSun);
            pos += diff;
        }

        // draw thumb
        GEngine->DrawLine3D(thumbPos, thumbPos + thumbHeight - 0.5 * thumbWidth, color, DisableSun);
        GEngine->DrawLine3D(thumbPos + thumbHeight - 0.5 * thumbWidth, thumbPos + thumbHeight + 0.5 * thumbWidth, color,
                            DisableSun);
        GEngine->DrawLine3D(thumbPos + thumbHeight + 0.5 * thumbWidth, thumbPos, color, DisableSun);
    }
}

C3DScrollBar::C3DScrollBar()
{
    _thumbLocked = false;
}

void C3DScrollBar::OnLButtonDown(float v)
{
    float down = _down.Size();

    // spins
    float spinSize = 0.8 * _right.Size();
    if (2.0 * spinSize >= down)
    {
        return; // too width scrollbar
    }
    if (_maxPos <= _minPos)
    {
        return;
    }

    float y = down * v;

    float invRange = (down - 2.0f * spinSize) / (_maxPos - _minPos);
    float thumbSize = _page * invRange;
    float thumbPos = spinSize + _curPos * invRange;

    if (y <= spinSize)
    {
        // lineUp
        _curPos -= _lineStep;
    }
    else if (y <= thumbPos)
    {
        // pageUp
        _curPos -= _pageStep;
    }
    else if (y <= thumbPos + thumbSize)
    {
        // thumb
        _thumbLocked = true;
        _thumbOffset = y - thumbPos;
    }
    else if (y <= down - spinSize)
    {
        // pageDown
        _curPos += _pageStep;
    }
    else
    {
        // lineDown
        _curPos += _lineStep;
    }
    saturate(_curPos, _minPos, _maxPos - _page);
}

void C3DScrollBar::OnLButtonUp()
{
    _thumbLocked = false;
}

void C3DScrollBar::OnMouseHold(float v)
{
    if (_thumbLocked)
    {
        float down = _down.Size();

        // spins
        float spinSize = 0.8 * _right.Size();
        if (2.0 * spinSize >= down)
        {
            return; // too width scrollbar
        }
        if (_maxPos <= _minPos)
        {
            return;
        }

        float y = down * v;
        float range = (_maxPos - _minPos) / (down - 2.0f * spinSize);
        float thumbPos = y - _thumbOffset;
        _curPos = (thumbPos - spinSize) * range;
        saturate(_curPos, _minPos, _maxPos - _page);
    }
}

void C3DScrollBar::OnDraw(float alpha)
{
    PackedColor color = ModAlpha(_color, alpha);

    // frame
    Vector3 posTL = _position;
    Vector3 posTR = posTL + _right;
    Vector3 posBL = posTL + _down;
    Vector3 posBR = posTR + _down;
    GEngine->DrawLine3D(posTL, posTR, color, DisableSun);
    GEngine->DrawLine3D(posTR, posBR, color, DisableSun);
    GEngine->DrawLine3D(posBR, posBL, color, DisableSun);
    GEngine->DrawLine3D(posBL, posTL, color, DisableSun);

    // spins
    float spinSize = 0.8 * _right.Size();
    if (2.0 * spinSize >= _down.Size())
    {
        return; // too width scrollbar
    }

    Vector3 borderX = 0.25 * _right;
    Vector3 centerX = 0.5 * _right;
    Vector3 borderY = 0.25 * spinSize * _down.Normalized();
    Vector3 top = _position + spinSize * _down.Normalized();
    GEngine->DrawLine3D(top, top + _right, color, DisableSun);
    GEngine->DrawLine3D(top + borderX - borderY, _position + centerX + borderY, color, DisableSun);
    GEngine->DrawLine3D(_position + centerX + borderY, top + _right - borderX - borderY, color, DisableSun);
    Vector3 bottom = _position + _down - spinSize * _down.Normalized();
    GEngine->DrawLine3D(bottom, bottom + _right, color, DisableSun);
    GEngine->DrawLine3D(bottom + borderX + borderY, _position + _down + centerX - borderY, color, DisableSun);
    GEngine->DrawLine3D(_position + _down + centerX - borderY, bottom + _right - borderX + borderY, color, DisableSun);

    // thumb
    if (_maxPos <= _minPos)
    {
        return;
    }
    Vector3 invRange = (bottom - top) * (1 / (_maxPos - _minPos));
    Vector3 thumbSize = _page * invRange;
    Vector3 thumbPos = top + _curPos * invRange;

    posTL = thumbPos + 0.2 * borderX + 0.2 * borderY;
    posTR = posTL + (_right - 0.4 * borderX);
    posBL = posTL + (thumbSize - 0.4 * borderY);
    posBR = posTR + (thumbSize - 0.4 * borderY);
    GEngine->DrawLine3D(posTL, posTR, color, DisableSun);
    GEngine->DrawLine3D(posTR, posBR, color, DisableSun);
    GEngine->DrawLine3D(posBR, posBL, color, DisableSun);
    GEngine->DrawLine3D(posBL, posTL, color, DisableSun);
}

// CT_3DSCROLLBAR.  Wraps the C3DScrollBar
// helper as a top-level Control so resource templates can declare a single
// scrollbar widget without the host class having to embed and manage it.
//
// Geometry: the scrollbar fills the control's full extent; carets sit at
// the top and bottom 0.8 * width, the thumb between.  Mouse events arrive
// in (x,y) → IsInside writes _u/_v ∈ [0,1] in control-local coords; we
// forward _v (the vertical fraction) to C3DScrollBar which knows how to
// map it onto carets / thumb / page zones.
//
// Position is refreshed in OnDraw because Control3D::UpdateInfo recomputes
// _position / _right / _down each frame from the host transform and the
// scrollbar geometry has to follow.

C3DScrollBarStandalone::C3DScrollBarStandalone(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DSCROLLBAR, idc, cls)
{
    // Foreground colour (lines + carets + thumb border).  Matches the
    // resource convention: `color[]` is the visible foreground.
    _scrollbar.SetColor(GetPackedColor(cls >> "color"));
    // Default: empty range, disabled until the host pushes a real range.
    _scrollbar.SetRange(0, 0, 1);
    _scrollbar.SetPos(0);
    _scrollbar.SetSpeed(1, 1);
    // Always enabled — host suppresses by sizing range so page covers
    // the whole space, which auto-disables the underlying helper.
    _scrollbar.Enable(true);
}

void C3DScrollBarStandalone::OnLButtonDown(float x, float y)
{
    // C3DScrollBar's hit math depends on its _down/_right vectors, which
    // we mirror from Control3D::_position/_right/_down.  Refresh them
    // before forwarding so a press in the very first frame after
    // construction (no OnDraw yet) still computes correctly.
    _scrollbar.SetPosition(_position, _right, _down);
    IsInside(x, y);
    // Use GetV() so synthetic harness drags (DebugSetUV) override the
    // engine-computed _v.  IsInside still has to run to set _u/_v for
    // real mouse events that don't pre-populate the debug fields.
    _scrollbar.OnLButtonDown(GetV());
}

void C3DScrollBarStandalone::OnLButtonUp(float /*x*/, float /*y*/)
{
    _scrollbar.OnLButtonUp();
}

void C3DScrollBarStandalone::OnMouseHold(float x, float y, bool /*active*/)
{
    if (!_scrollbar.IsLocked())
        return;
    _scrollbar.SetPosition(_position, _right, _down);
    IsInside(x, y);
    _scrollbar.OnMouseHold(GetV());
}

void C3DScrollBarStandalone::OnDraw(float alpha)
{
    _scrollbar.SetPosition(_position, _right, _down);
    _scrollbar.OnDraw(alpha);
}

C3DListBox::C3DListBox(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Control3D(parent, CT_3DLISTBOX, idc, cls), CListBoxContainer(cls)
{
    _selBgColor = GetPackedColor(cls >> "colorSelectBackground");

    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    _size = cls >> "size";
    _rows = cls >> "rows";

    _scrollUp = false;
    _scrollDown = false;
    _dragging = false;

    _sb3DWidth = 0.10;

    _scrollbar.SetPosition(_position + (1.0 - _sb3DWidth) * _right, _sb3DWidth * _right, _down);
    _scrollbar.SetColor(_ftColor);
    _scrollbar.SetRange(0, 0, _rows);
    _scrollbar.SetPos(0);
    _scrollbar.SetSpeed(1, floatMax(_rows - 1, 1));

    _colorPicture = false;
}

void C3DListBox::SetCurSel(int sel, bool sendUpdate)
{
    if (!sendUpdate && sel == _selString)
    {
        saturateMin(_topString, GetSize() - _rows);
        saturateMax(_topString, 0);
        return;
    }

    if (GetSize() == 0)
    {
        _selString = -1;
    }
    else if (sel < 0)
    {
        _selString = 0;
    }
    else if (sel >= GetSize())
    {
        _selString = GetSize() - 1;
    }
    else
    {
        _selString = sel;
    }

    if (_selString < _topString)
    {
        _topString = _selString;
        saturateMin(_topString, GetSize() - _rows);
        saturateMax(_topString, 0);
    }
    else if (_selString > _topString + _rows - 1)
    {
        _topString = _selString - _rows + 1;
    }

    if (sendUpdate)
    {
        OnSelChanged(_selString);
        _parent->OnLBSelChanged(IDC(), _selString);
    }

    Check();
}

bool C3DListBox::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (IsReadOnly())
    {
        return false;
    }

    switch (nChar)
    {
        case SDLK_UP:
            SetCurSel(GetCurSel() - 1);
            return true;
        case SDLK_DOWN:
            SetCurSel(GetCurSel() + 1);
            return true;
    }
    return false;
}

void C3DListBox::OnLButtonDown(float x, float y)
{
    // if (IsReadOnly()) return;

    IsInside(x, y);
    if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
    {
        _scrollbar.SetPos(_topString);
        _scrollbar.OnLButtonDown(_v);
        _topString = _scrollbar.GetPos();
    }
    else if (_parent)
    {
        float index = _v * _rows;
        // FIX
        if (index >= 0 && index < _rows && _topString + index < GetSize())
        {
            _parent->OnLBDrag(IDC(), toIntFloor(_topString + index));
            _dragging = true;
        }
    }
}

void C3DListBox::OnLButtonUp(float x, float y)
{
    if (_scrollbar.IsLocked())
    {
        _scrollbar.OnLButtonUp();
    }
    else if (!IsReadOnly() && IsInside(x, y))
    {
        if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
        {
        }
        else
        {
            float index = _v * _rows;
            if (index >= 0 && index < _rows)
            {
                SetCurSel(toIntFloor(_topString + index));
            }
        }
    }

    if (_dragging)
    {
        if (_parent)
        {
            _parent->OnLBDrop(x, y);
        }
        _dragging = false;
    }
}

void C3DListBox::OnMouseZChanged(float dz)
{
    // if (IsReadOnly()) return;

    CListBoxContainer::OnMouseZChanged(dz);
}

void C3DListBox::OnMouseMove(float x, float y, bool active)
{
    // if (IsReadOnly()) return;

    if (!InputSubsystem::Instance().IsMouseLeftDown())
    {
        return;
    }

    OnMouseHold(x, y, active);
}

void C3DListBox::OnMouseHold(float x, float y, bool active)
{
    if (!InputSubsystem::Instance().IsMouseLeftDown())
    {
        return;
    }

    IsInside(x, y);

    if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
    {
        _scrollbar.SetPos(_topString);
        _scrollbar.OnMouseHold(_v);
        _topString = _scrollbar.GetPos();
    }
    else if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
    {
    }
    /*
        // avoid multiple SetCurSel
        else if (!IsReadOnly())
        {
            float index = _v * _rows;
            if (index >= 0 && index < _rows)
            {
                SetCurSel(toIntFloor(_topString + index));
            }
        }
    */

    if (_dragging)
    {
        if (active && _parent)
        {
            _parent->OnLBDragging(x, y);
        }
    }
}

void C3DListBox::OnLButtonDblClick(float x, float y)
{
    if (IsReadOnly())
    {
        return;
    }

    if (_selString < 0)
    {
        return;
    }

    IsInside(x, y);
    if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
    {
        return;
    }

    _parent->OnLBDblClick(IDC(), _selString);
}

void C3DListBox::SetCurSel(float x, float y, bool sendUpdate)
{
    if (!IsInside(x, y))
    {
        return;
    }

    float index = _v * _rows;
    if (index >= 0 && index < _rows)
    {
        SetCurSel(toIntFloor(_topString + index), sendUpdate);
    }
}

void C3DListBox::OnDraw(float alpha)
{
    Vector3 normal = _down.CrossProduct(_right).Normalized();

    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    Vector3 posTL = _position - 0.002 * normal;
    Vector3 posTR = posTL + _right;
    Vector3 posBL = posTL + _down;
    Vector3 posBR = posTR + _down;

    GEngine->DrawLine3D(posTL, posTR, ftColor, DisableSun);
    GEngine->DrawLine3D(posTR, posBR, ftColor, DisableSun);
    GEngine->DrawLine3D(posBR, posBL, ftColor, DisableSun);
    GEngine->DrawLine3D(posBL, posTL, ftColor, DisableSun);

    if (GetSize() > _rows)
    {
        _scrollbar.Enable(true);
        _scrollbar.SetPosition(posTL + (1.0 - _sb3DWidth) * _right, _sb3DWidth * _right, _down);
        _scrollbar.SetRange(0, GetSize(), _rows);
        _scrollbar.SetPos(_topString);
        _scrollbar.OnDraw(alpha);
    }
    else
    {
        _scrollbar.Enable(false);
    }

    float invItems = 1.0 / _rows;
    Vector3 down = invItems * _down;

    const float eps = 1e-6;
    int i = toIntFloor(_topString + eps);
    int iMax = toIntCeil(floatMin(_topString + _rows, GetSize()) - eps);
    Vector3 position = _position - (_topString - i) * down;

    saturateMin(iMax, GetSize());

    while (i < iMax)
    {
        DrawItem(position, down, i, alpha);
        position += down;
        i++;
    }
}

C3DTableRow C3DListBox::BeginRow(Vector3Par position, Vector3Par down, int i, float alpha, float top, float size,
                                 bool drawSelection)
{
    float y1c = 0;
    float y2c = 1;
    if (i < _topString)
        y1c = _topString - i;
    if (i > _topString + _rows - 1)
        y2c = _topString + _rows - i;

    Vector3 normal = _down.CrossProduct(_right).Normalized();
    Vector3 dir = _right.Normalized();
    float rightSBSize = (1.0 - _sb3DWidth) * _right.Size();
    float border = 0.01 * rightSBSize;
    Vector3 curPos = position - 0.002 * normal;

    bool selected = i == GetCurSel() && IsEnabled();
    PackedColor color;
    if (selected && _showSelected)
    {
        if (drawSelection)
        {
            PackedColor selBgColor = ModAlpha(_selBgColor, alpha);
            Vector3 rightSB = _right;
            if (GetSize() > _rows)
                rightSB = (1.0 - _sb3DWidth) * _right;
            GEngine->Draw3D(position, down, rightSB, ClipAll, selBgColor, DisableSun, nullptr, 0, y1c, 1, y2c);
        }
        color = ModAlpha(_selColor, alpha);
    }
    else
    {
        color = ModAlpha(_ftColor, alpha);
    }

    float y1ct = 0;
    float y2ct = 1;
    if (y1c > top)
        y1ct = (y1c - top) / size;
    if (y2c < top + size)
        y2ct = (y2c - top) / size;

    C3DTableRow row;
    row.pos = curPos + top * down;
    row.up = -size * down;
    row.dir = dir;
    row.right = 0.75 * row.up.Size() * dir;
    row.invRightSize = 1.0 / row.right.Size();
    row.rightSBSize = rightSBSize;
    row.border = border;
    row.y1ct = y1ct;
    row.y2ct = y2ct;
    row.y1c = y1c;
    row.y2c = y2c;
    row.top = top;
    row.down = down;
    row.font = _font;
    row.color = color;
    return row;
}

void C3DTableRow::DrawColumn(float widthFraction, RString text, PackedColor textColor, bool divider)
{
    float column = widthFraction * rightSBSize;
    float x2c = (column - 2.0 * border) * invRightSize;
    GEngine->DrawText3D(pos + border * dir, up, right, ClipAll, font, textColor, DisableSun, text, 0, y1ct, x2c, y2ct);
    pos += column * dir;
    // Full-row vertical divider at the column boundary (matches CSessions' old
    // COLUMN_LINE), always in the row's base colour.
    if (divider)
        GEngine->DrawLine3D(pos + (y1c - top) * down, pos + (y2c - top) * down, color, DisableSun);
}

void UpdateSortCaret(IControl* icon, bool active, bool ascending, RString upTex, RString downTex)
{
    C3DStatic* pic = dynamic_cast<C3DStatic*>(icon);
    if (pic == nullptr)
        return;
    if (active)
    {
        pic->ShowCtrl(true);
        pic->SetText(ascending ? upTex : downTex);
    }
    else
    {
        pic->ShowCtrl(false);
    }
}

void C3DListBox::RebuildVisibleRows()
{
    _visible.Clear();
    const int n = FilterRowCount();
    for (int a = 0; a < n; a++)
    {
        if (FilterRowVisible(a))
            _visible.Add(a);
    }
}

void C3DListBox::SetFilterActive(bool active)
{
    _filterActive = active;
    if (active)
        RebuildVisibleRows();
}

int C3DListBox::VisibleRow(int displayIdx) const
{
    if (!_filterActive)
        return displayIdx;
    if (displayIdx >= 0 && displayIdx < _visible.Size())
        return _visible[displayIdx];
    return displayIdx;
}

void C3DListBox::DrawItem(Vector3Par position, Vector3Par down, int i, float alpha)
{
    float y1c = 0;
    float y2c = 1;
    if (i < _topString)
    {
        y1c = _topString - i;
    }
    if (i > _topString + _rows - 1)
    {
        y2c = _topString + _rows - i;
    }

    Vector3 normal = _down.CrossProduct(_right).Normalized();

    Vector3 rightSB = _right;
    if (GetSize() > _rows)
    {
        rightSB = (1.0 - _sb3DWidth) * _right;
    }
    float rightSBSize = rightSB.Size();
    Vector3 border = 0.02 * _right;

    Vector3 curPos = position - 0.002 * normal;

    bool selected = i == GetCurSel() && IsEnabled();
    PackedColor color;
    if (selected && _showSelected)
    {
        PackedColor selBgColor = ModAlpha(_selBgColor, alpha);
        GEngine->Draw3D(curPos, down, rightSB, ClipAll, selBgColor, DisableSun, nullptr, 0, y1c, 1, y2c);
        color = ModAlpha(GetSelColor(i), alpha);
    }
    else
    {
        color = ModAlpha(GetFtColor(i), alpha);
    }

    curPos = position - 0.003 * normal;

    Texture* texture = GetTexture(i);
    if (texture)
    {
        PackedColor picColor = color;
        if (_colorPicture)
        {
            picColor = ModAlpha(PackedWhite, alpha);
        }

        Vector3 right = (float)texture->AWidth() / (float)texture->AHeight() * down.Size() * _right.Normalized();
        float rightSize = right.Size();
        float x2c = 1;
        if (rightSize > rightSBSize)
        {
            x2c = rightSBSize / rightSize;
        }
        GEngine->Draw3D(curPos, down, right, ClipAll, picColor, // PackedColor(Color(1, 1, 1, alpha)),
                        DisableSun, texture, 0, y1c, x2c, y2c);
        curPos += right;
        rightSBSize -= rightSize;
        if (rightSBSize <= 0)
        {
            return;
        }
    }

    RString text = GetText(i);
    float top = 0.5 * (1.0 - _size);
    Vector3 pos = curPos + top * down + border;
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    float x2c = (rightSBSize - 2.0 * border.Size()) / right.Size();
    float y1ct = 0;
    float y2ct = 1;
    if (y1c > top)
    {
        y1ct = (y1c - top) / _size;
    }
    if (y2c < top + _size)
    {
        y2ct = (y2c - top) / _size;
    }
    GEngine->DrawText3D(pos, up, right, ClipAll, _font, color, DisableSun, text, 0, y1ct, x2c, y2ct);
}

void C3DListBox::UpdateInfo(ControlObject* object, ControlInObject& info)
{
    Control3D::UpdateInfo(object, info);
}
