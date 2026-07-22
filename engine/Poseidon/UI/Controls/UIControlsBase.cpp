#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Interpol.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <cstring>

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
//
//	 Implementation
//
int ResolveLegacyControlInt(const ParamEntry& entry)
{
    const RStringB raw = entry.GetValueRaw();
    const char* rawText = raw;
    if (rawText && strcmp(rawText, "IDC_STATIC") == 0)
        return -1;
    return entry.GetInt();
}

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

extern const float CameraZoom = 2.0;
extern const float InvCameraZoom = 1.0 / CameraZoom;

// classes IControl, Control

IControl::IControl(ControlsContainer* parent, int idc)
{
    _parent = parent;
    _idc = idc;

    _visible = true;
    _enabled = true;
    _focused = false;
    _default = false;
}

Ref<Font> IControl::_tooltipFont;
float IControl::_tooltipSize = 0.02;

IControl::IControl(ControlsContainer* parent, int idc, const ParamEntry& cls)
{
    _parent = parent;
    _idc = idc;

    _visible = true;
    _enabled = true;
    _focused = false;
    _default = false;

    const ParamEntry* entry = cls.FindEntry("tooltip");
    if (entry)
    {
        _tooltip = *entry;
    }

    if (!_tooltipFont)
    {
        _tooltipFont = GEngine->LoadFont(GetFontID("tahomaB24"));
    }

    _tooltipColorText = PackedWhite;
    entry = cls.FindEntry("tooltipColorText");
    if (entry)
    {
        GetValue(_tooltipColorText, *entry);
    }

    _tooltipColorBox = PackedColor(Color(1, 1, 1, 0.3));
    entry = cls.FindEntry("tooltipColorBox");
    if (entry)
    {
        GetValue(_tooltipColorBox, *entry);
    }

    _tooltipColorShade = PackedColor(Color(0, 0, 0, 0.3));
    entry = cls.FindEntry("tooltipColorShade");
    if (entry)
    {
        GetValue(_tooltipColorShade, *entry);
    }
}

Control::Control(ControlsContainer* parent, int type, int idc, int style, float x, float y, float w, float h)
    : IControl(parent, idc)
{
    _parent = parent;
    _type = type;
    _style = style;
    _x = x;
    _y = y;
    _w = w;
    _h = h;
    _timer = UITIME_MAX;
    _scale = 1.0;
}

Control::Control(ControlsContainer* parent, int type, int idc, const ParamEntry& cls) : IControl(parent, idc, cls)
{
    _parent = parent;
    _type = type;
    _timer = UITIME_MAX;
    _scale = 1.0;

    _style = ResolveLegacyControlInt(cls >> "style");
    _x = cls >> "x";
    _y = cls >> "y";
    _w = cls >> "w";
    _h = cls >> "h";
}

void Control::Move(float dx, float dy)
{
    _x += dx;
    _y += dy;
}

DrawCoord SceneToScreen(Vector3Par pos);

void Control::UpdateInfo(ControlObject* object, ControlInObject& info)
{
    Vector3 posTL = object->PositionModelToWorld(info.posTL);
    Vector3 posBR = object->PositionModelToWorld(info.posBR);
    Vector3Val position = object->Position();
    Vector3Val posBase = object->GetBasePosition();

    float dz = 0.5 * (posTL.Z() + posBR.Z() - 2.0 * position.Z());
    float scale = (posBase.Z() + dz) / (position.Z() + dz);

    DrawCoord pt1 = SceneToScreen(posTL);
    DrawCoord pt2 = SceneToScreen(posBR);
    float w = pt2.x - pt1.x;
    float h = pt2.y - pt1.y;
    info._control->SetPos(pt1.x + info.x * w, pt1.y + info.y * h, info.w * w, info.h * h);
    info._control->SetScale(scale);
}

void IControl::PlaySound(SoundPars& pars)
{
    if (pars.name.GetLength() > 0)
    {
        // float volume = GSoundsys->GetSpeechVolCoef();
        IWave* wave = GSoundScene->OpenAndPlayOnce2D(pars.name, pars.vol, pars.freq, false);
        if (wave)
        {
            wave->SetKind(WaveMusic); // UI sounds considered music???
            wave->SetSticky(true);
            GSoundScene->AddSound(wave);
        }
    }
    GSoundScene->AdvanceAll(0, !GWorld->IsSimulationEnabled());
}

bool IControl::OnSetFocus(bool up, bool def)
{
    _focused = true;
    return true;
}

bool IControl::OnKillFocus()
{
    _focused = false;
    return true;
}

void IControl::OnTimer()
{
    _timer = UITIME_MAX;
}

IControl* IControl::GetCtrl(int idc)
{
    if (idc == _idc)
    {
        return this;
    }
    return nullptr;
}

IControl* IControl::GetCtrl(float x, float y)
{
    return this;
}

bool IControl::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

bool IControl::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

bool IControl::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

bool IControl::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

bool IControl::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
    return false;
}

void IControl::OnLButtonDown(float x, float y) {}

void IControl::OnLButtonUp(float x, float y) {}

void IControl::OnLButtonClick(float x, float y) {}

void IControl::OnLButtonDblClick(float x, float y) {}

void IControl::OnRButtonDown(float x, float y) {}

void IControl::OnRButtonUp(float x, float y) {}

void IControl::OnMouseMove(float x, float y, bool active) {}

void IControl::OnMouseHold(float x, float y, bool active) {}

void IControl::OnMouseZChanged(float dz) {}

bool IControl::OnCanDestroy(int code)
{
    return true;
}

void IControl::OnDestroy(int code) {}

void IControl::DrawTooltip(float x, float y)
{
    if (_tooltip.GetLength() > 0)
    {
        int w = GEngine->Width2D();
        int h = GEngine->Height2D();

        const float border = 0.005;
        float height = _tooltipFont->Height();
        float width = GEngine->GetTextWidth(_tooltipSize, _tooltipFont, _tooltip);

        float bx = x + 0.02;
        //		float by = y - 0.5 * height - border;
        float by = y + 0.02;
        float ex = bx + width + 2.0 * border;
        float ey = by + height + 2.0 * border;

        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        GEngine->Draw2D(mip, _tooltipColorShade, Rect2DPixel(bx * w, by * h, (ex - bx) * w, (ey - by) * h));
        GEngine->DrawText(Point2DFloat(bx + border, by + border), _tooltipSize, _tooltipFont, _tooltipColorText,
                          _tooltip);
        GEngine->DrawLine(Line2DPixel(bx * w, by * h, ex * w, by * h), _tooltipColorBox, _tooltipColorBox);
        GEngine->DrawLine(Line2DPixel(ex * w, by * h, ex * w, ey * h), _tooltipColorBox, _tooltipColorBox);
        GEngine->DrawLine(Line2DPixel(ex * w, ey * h, bx * w, ey * h), _tooltipColorBox, _tooltipColorBox);
        GEngine->DrawLine(Line2DPixel(bx * w, ey * h, bx * w, by * h), _tooltipColorBox, _tooltipColorBox);
    }
}
ControlObject::ControlObject(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : Object(Shapes.New(Poseidon::FindShape(cls >> "model"), false, false), -1), IControl(parent, idc, cls)
{
    if (_shape && _shape->NLevels() > 0)
    {
        _shape->LevelOpaque(0)->MakeCockpit();
        _shape->OrSpecial(BestMipmap | NoDropdown | DisableSun);
    }

    _position.Init();
    _position[0] = (cls >> "position")[0];
    _position[1] = (cls >> "position")[1];
    _position[2] = (cls >> "position")[2];
    _position[2] *= CameraZoom;
    SetPosition(_position);

    Vector3 dir, up;
    dir.Init();
    up.Init();
    dir[0] = (cls >> "direction")[0];
    dir[1] = (cls >> "direction")[1];
    dir[2] = (cls >> "direction")[2];
    up[0] = (cls >> "up")[0];
    up[1] = (cls >> "up")[1];
    up[2] = (cls >> "up")[2];
    SetOrient(dir, up);

    float scale = cls >> "scale";
    SetScale(scale);

    _offset = VZero;
    _moving = false;
}

ControlObject::~ControlObject() = default;

int ControlObject::GetType()
{
    return CT_OBJECT;
}

int ControlObject::GetStyle()
{
    return 0;
}

Vector3 ControlObject::Convert2DTo3D(const Point2DFloat& point, float z)
{
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    float scrX = point.x * (as.uiBottomRightX - as.uiTopLeftX) + as.uiTopLeftX;
    float scrY = point.y * (as.uiBottomRightY - as.uiTopLeftY) + as.uiTopLeftY;
    //
    return Vector3((scrX - 0.5) * InvCameraZoom * as.leftFOV * z, as.topFOV * (0.5 - scrY) * InvCameraZoom * z, z);
}

bool ControlObject::IsInside(float x, float y)
{
    Vector3 begin = VZero;
    Vector3 end = Convert2DTo3D(Point2DFloat(x, y), 2.0 * CameraZoom);
    CollisionBuffer buffer;
    Intersect(buffer, begin, end, 0, ObjIntersectGeom);
    if (buffer.Size() > 0)
    {
        _modelPos = buffer[0].pos;
        return true;
    }
    return false;
}

void ControlObject::Move(float dx, float dy)
{
    // nothing to do
}

void ControlObject::OnLButtonDown(float x, float y)
{
    _offset = PositionModelToWorld(_modelPos) - Position();
    _moving = true;
}

void ControlObject::OnLButtonUp(float x, float y)
{
    _moving = false;
}

void ControlObject::OnRButtonDown(float x, float y)
{
    // Right-drag tumbling is a dev affordance — it was useful when
    // debugging 3D control orientation but lets shipping players
    // re-orient the main-menu notebook / logo by accident.  Gate on
    // `--dev` so production builds are inert on RMB.
    if (!AppConfig::Instance().DevMode())
        return;
    _rotating = true;
    _rotLastX = x;
    _rotLastY = y;
}

void ControlObject::OnRButtonUp(float /*x*/, float /*y*/)
{
    _rotating = false;
}

void ControlObject::OnMouseMove(float x, float y, bool active)
{
    // LMB translation is handled by the _moving path in ControlObjectWithZoom
    // (which knows about inBack / positionBack).  This override exists only to
    // carry right-drag rotation — callers chain here via the Zoom subclass.
    if (_rotating && active)
    {
        const float kSensitivity = H_PI * 2.0f;
        float dx = (x - _rotLastX) * kSensitivity;
        float dy = (y - _rotLastY) * kSensitivity;
        _rotLastX = x;
        _rotLastY = y;
        Matrix3 yaw(MRotationY, dx);
        Matrix3 pitch(MRotationX, dy);
        SetOrientation(yaw * pitch * Orientation());
    }
}

void ControlObject::OnMouseZChanged(float /*dz*/)
{
    // Wheel-zoom on 3D UI objects is intentionally a no-op — vanilla OFP
    // didn't dolly notebook / desk / camera meshes on scroll, and accidental
    // wheel rolls were resizing the briefing notepad mid-mission.
    // ControlObjectWithZoom still offers a discrete near/far toggle via
    // double-click (OnLButtonDblClick → Zoom), which is how the original
    // game scaled the options notebook.
}

void ControlObject::OnDraw(float alpha)
{
    PackedColor color(Color(1, 1, 1, alpha));
    SetConstantColor(color);
    int level = 0;
#if _ENABLE_CHEATS
#endif
    // Force SW T&L path for UI 3D objects — HW-TL shader doesn't support
    // the per-object point light that ControlsContainer::OnDraw creates,
    // and UI objects may gain VBOs during mission OptimizeAll().
    Draw(level, ClipAll | ClipUser0, *this);
}

ControlObjectWithZoom::ControlObjectWithZoom(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : ControlObject(parent, idc, cls)
{
    _positionBack.Init();
    _positionBack[0] = (cls >> "positionBack")[0];
    _positionBack[1] = (cls >> "positionBack")[1];
    _positionBack[2] = (cls >> "positionBack")[2];
    _positionBack[2] *= CameraZoom;
    for (int i = 0; i < 3; i++)
    {
        _zoomMatrices[i].SetIdentity();
    }

    _inBack = cls >> "inBack";
    _enableZoom = cls >> "enableZoom";

    _zoomDuration = cls >> "zoomDuration";

    _zooming = false;

    if (_inBack)
    {
        SetPosition(_positionBack);
    }
}

int ControlObjectWithZoom::GetType()
{
    return CT_OBJECT_ZOOM;
}

void ControlObjectWithZoom::OnLButtonDblClick(float x, float y)
{
    if (_enableZoom)
    {
        Zoom(_inBack);
    }
}

void ControlObjectWithZoom::OnMouseMove(float x, float y, bool active)
{
    if (_zooming)
    {
        return;
    }
    if (_moving)
    {
        Vector3 pos = Convert2DTo3D(Point2DFloat(x, y), Position().Z() + _offset.Z());

        pos -= _offset;
        if (_inBack)
        {
            if (_parent)
            {
                _parent->OnObjectMoved(_idc, pos - _positionBack);
            }
            _positionBack = pos;
        }
        else
        {
            if (_parent)
            {
                _parent->OnObjectMoved(_idc, pos - _position);
            }
            _position = pos;
        }
        SetPosition(pos);
    }
    // Forward to ControlObject so the RMB-rotate path gets a chance to
    // run — otherwise this override shadows the base handler entirely
    // and right-drag rotation never sees any mouse motion.
    ControlObject::OnMouseMove(x, y, active);
}

void ControlObjectWithZoom::UpdatePosition()
{
    if (_zooming)
    {
        float time = Glob.uiTime - _zoomStart;
        if (time >= _zoomTimes[2])
        {
            time = _zoomTimes[2];
            _zooming = false;
            _inBack = !_inBack;
        }
        Vector3 pos = (*_interpolator)(time).Position();
        SetPosition(pos);
        if (!_zooming)
        {
            _interpolator = nullptr;
        }
    }
}

void ControlObjectWithZoom::Zoom(bool zoom)
{
    if (zoom != _inBack)
    {
        return;
    }

    _zooming = true;
    _zoomStart = Glob.uiTime;
    _zoomTimes[0] = 0;
    _zoomTimes[1] = 0.5 * _zoomDuration;
    _zoomTimes[2] = 1.0 * _zoomDuration;
    if (zoom)
    {
        _zoomMatrices[0].SetPosition(_positionBack);
        _zoomMatrices[2].SetPosition(_position);
    }
    else
    {
        _zoomMatrices[0].SetPosition(_position);
        _zoomMatrices[2].SetPosition(_positionBack);
    }
    Vector3 pos;
    pos.Init();
    pos[0] = 0.33 * _position[0] + 0.67 * _positionBack[0];
    pos[1] = 0.33 * _position[1] + 0.67 * _positionBack[1];
    pos[2] = 0.67 * _position[2] + 0.33 * _positionBack[2];
    _zoomMatrices[1].SetPosition(pos);

    _interpolator = new InterpolatorSpline(_zoomMatrices, _zoomTimes, 3);
}

LSError ControlObjectWithZoom::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("inBack", _inBack, 1))
    _position[2] *= InvCameraZoom;
    PARAM_CHECK(ar.Serialize("position", _position, 1))
    _position[2] *= CameraZoom;
    _positionBack[2] *= InvCameraZoom;
    PARAM_CHECK(ar.Serialize("positionBack", _positionBack, 1))
    _positionBack[2] *= CameraZoom;

    if (ar.IsLoading())
    {
        if (_inBack)
        {
            SetPosition(_positionBack);
        }
        else
        {
            SetPosition(_position);
        }
    }
    return LSOK;
}

ControlObjectContainer::ControlObjectContainer(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : base(parent, idc, cls)
{
    _indexFocused = -1;
    _indexL = -1;
    _indexR = -1;
    _indexMove = -1;
    LoadControls(cls);
    SetPosition(Position()); // update controls
}

int ControlObjectContainer::GetType()
{
    return CT_OBJECT_CONTAINER;
}

IControl* ControlObjectContainer::GetCtrl(int idc)
{
    if (idc == _idc)
    {
        return this;
    }
    for (int i = 0; i < _controls.Size(); i++)
    {
        IControl* control = _controls[i]._control;
        if (control)
        {
            IControl* ctrl = control->GetCtrl(idc);
            if (ctrl)
            {
                return ctrl;
            }
        }
    }
    return nullptr;
}

void ControlObjectContainer::ReloadLocalizedText()
{
    for (int i = 0; i < _controls.Size(); i++)
    {
        if (_controls[i]._control)
            _controls[i]._control->ReloadLocalizedText();
    }
}

IControl* ControlObjectContainer::GetCtrl(float x, float y)
{
    int index = FindControl(x, y);
    if (index >= 0)
    {
        return _controls[index]._control;
    }
    else
    {
        return this;
    }
}

bool ControlObjectContainer::SetSubControlPos(int idc, float x, float y, float w, float h)
{
    for (int i = 0; i < _controls.Size(); ++i)
    {
        if (_controls[i]._control && _controls[i]._control->IDC() == idc)
        {
            _controls[i].x = x;
            _controls[i].y = y;
            _controls[i].w = w;
            _controls[i].h = h;
            return true;
        }
    }
    return false;
}

bool ControlObjectContainer::FocusCtrlByIdc(int idc)
{
    for (int i = 0; i < _controls.Size(); ++i)
    {
        Control* ctrl = _controls[i]._control;
        if (ctrl && ctrl->IDC() == idc)
        {
            SetFocus(i);
            return true;
        }
    }
    return false;
}

int ControlObjectContainer::GetHoveredIdc() const
{
    if (_indexMove < 0 || _indexMove >= _controls.Size())
        return -1;
    Control* ctrl = _controls[_indexMove]._control;
    return ctrl ? ctrl->IDC() : -1;
}

int ControlObjectContainer::GetLeftPressedIdc() const
{
    if (!InputSubsystem::Instance().IsMouseLeftDown())
        return -1;
    if (_indexL < 0 || _indexL >= _controls.Size())
        return -1;
    Control* ctrl = _controls[_indexL]._control;
    return ctrl ? ctrl->IDC() : -1;
}

bool ControlObjectContainer::DebugSetLeftPressedIdc(int idc)
{
    if (idc < 0)
    {
        _indexL = -1;
        return true;
    }
    for (int i = 0; i < _controls.Size(); ++i)
    {
        if (_controls[i]._control && _controls[i]._control->IDC() == idc)
        {
            _indexL = i;
            return true;
        }
    }
    return false;
}

IControl* ControlObjectContainer::GetFocused()
{
    if (_indexFocused < 0)
    {
        return this;
    }
    if (_indexFocused >= _controls.Size())
    {
        _indexFocused = -1;
        return this;
    }
    return _controls[_indexFocused]._control;
}

int ControlObjectContainer::GetFocusedIdc()
{
    if (_indexFocused < 0)
    {
        return IDC();
    }
    if (_indexFocused >= _controls.Size())
    {
        _indexFocused = -1;
        return IDC();
    }
    IControl* ctrl = _controls[_indexFocused]._control;
    return ctrl ? ctrl->IDC() : IDC();
}

bool ControlObjectContainer::CanBeDefault() const
{
    int n = _controls.Size();
    for (int i = 0; i < n; i++)
    {
        IControl* ctrl = _controls[i]._control;
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
        {
            return true;
        }
    }
    return false;
}

bool ControlObjectContainer::OnSetFocus(bool up, bool def)
{
    if (!base::OnSetFocus(up))
    {
        return false;
    }
    int n = _controls.Size();
    if (n <= 0)
    {
        return true;
    }
    if (up)
    {
        for (int i = 0; i < n; i++)
        {
            IControl* ctrl = _controls[i]._control;
            if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
            {
                ctrl->OnSetFocus();
                _indexFocused = i;
                return true;
            }
        }
        _indexFocused = -1;
        return true;
    }
    else
    {
        for (int i = n - 1; i >= 0; i--)
        {
            IControl* ctrl = _controls[i]._control;
            if (ctrl->IsVisible() && ctrl->IsEnabled() && (!def || ctrl->CanBeDefault()))
            {
                ctrl->OnSetFocus();
                _indexFocused = i;
                return true;
            }
        }
        _indexFocused = -1;
        return true;
    }
}

Control* ControlObjectContainer::LoadControl(const ParamEntry& ctrlCls)
{
    int type = ResolveLegacyControlInt(ctrlCls >> "type");
    int idc = ResolveLegacyControlInt(ctrlCls >> "idc");
    Control* ctrl = _parent->OnCreateCtrl(type, idc, ctrlCls);
    if (ctrl != nullptr)
    {
        int index = _controls.Add();
        ControlInObject& info = _controls[index];
        info._control = ctrl;
        info.x = ctrl->X();
        info.y = ctrl->Y();
        info.w = ctrl->W();
        info.h = ctrl->H();
        info.selection = ctrlCls >> "selection";
        info.posTL = _shape->MemoryPoint(info.selection + RString(" TL"));
        info.posTR = _shape->MemoryPoint(info.selection + RString(" TR"));
        info.posBL = _shape->MemoryPoint(info.selection + RString(" BL"));
        info.posBR = _shape->MemoryPoint(info.selection + RString(" BR"));
        // Compute screen rect now so a control mounted after the notebook's
        // initial SetPosition is visible immediately.  Without this the
        // per-frame Draw path only refreshes UpdateInfo when isAnimated is
        // true — a static (animPhase=1) notebook would otherwise leave
        // newly-loaded controls at 0,0,0,0 forever.
        ctrl->UpdateInfo(this, info);
    }
    return ctrl;
}

bool ControlObjectContainer::RemoveControl(int idc)
{
    for (int i = 0; i < _controls.Size(); i++)
    {
        if (_controls[i]._control && _controls[i]._control->IDC() == idc)
        {
            // Drop tracking indices that point at (or past) this slot
            // so input dispatch doesn't read a stale pointer or address
            // the wrong neighbour after the array shifts.
            if (_indexFocused == i)
                _indexFocused = -1;
            else if (_indexFocused > i)
                _indexFocused--;
            if (_indexL == i)
                _indexL = -1;
            else if (_indexL > i)
                _indexL--;
            if (_indexR == i)
                _indexR = -1;
            else if (_indexR > i)
                _indexR--;
            if (_indexMove == i)
                _indexMove = -1;
            else if (_indexMove > i)
                _indexMove--;
            _controls.Delete(i);
            return true;
        }
    }
    return false;
}

void ControlObjectContainer::LoadControls(const ParamEntry& cls)
{
    const ParamEntry* cfg = cls.FindEntry("controls");
    if (cfg)
    {
        if (cfg->IsClass())
        {
            for (int i = 0; i < cfg->GetEntryCount(); i++)
            {
                const ParamEntry& ctrlCls = cfg->GetEntry(i);
                if (!ctrlCls.IsClass())
                {
                    continue;
                }
                if (!ctrlCls.FindEntry("type"))
                {
                    continue;
                }
                if (!ctrlCls.FindEntry("idc"))
                {
                    continue;
                }
                LoadControl(ctrlCls);
            }
        }
        else
        {
            for (int i = 0; i < cfg->GetSize(); i++)
            {
                RString ctrlName = (*cfg)[i];
                const ParamEntry& ctrlCls = cls >> ctrlName;
                LoadControl(ctrlCls);
            }
        }
    }
}

void ControlObjectContainer::SetPosition(Vector3Par pos)
{
    base::SetPosition(pos);

    // update position of _controls items
    for (int i = 0; i < _controls.Size(); i++)
    {
        ControlInObject& info = _controls[i];
        info._control->UpdateInfo(this, info);
    }
}

int ControlObjectContainer::FindControl(float x, float y)
{
    for (int i = 0; i < _controls.Size(); i++)
    {
        Control* ctrl = _controls[i]._control;
        PoseidonAssert(ctrl);
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->IsInside(x, y))
        {
            return i;
        }
    }
    return -1;
}

void ControlObjectContainer::OnLButtonDown(float x, float y)
{
    int iFound = FindControl(x, y);
    _indexL = iFound;
    if (iFound >= 0)
    {
        SetFocus(iFound);
        _controls[iFound]._control->OnLButtonDown(x, y);
    }
    else
    {
        SetFocus(-1);
        base::OnLButtonDown(x, y);
    }
}

void ControlObjectContainer::OnLButtonDblClick(float x, float y)
{
    int iFound = FindControl(x, y);
    if (iFound == _indexL)
    {
        if (iFound >= 0)
        {
            _controls[iFound]._control->OnLButtonDblClick(x, y);
        }
        else
        {
            base::OnLButtonDblClick(x, y);
        }
    }
    else
    {
        _indexL = iFound;
        if (iFound >= 0)
        {
            _controls[iFound]._control->OnLButtonDown(x, y);
        }
        else
        {
            base::OnLButtonDown(x, y);
        }
    }
}

void ControlObjectContainer::OnLButtonUp(float x, float y)
{
    if (_indexL >= 0)
    {
        _controls[_indexL]._control->OnLButtonUp(x, y);
    }
    else
    {
        base::OnLButtonUp(x, y);
    }
}

void ControlObjectContainer::OnLButtonClick(float x, float y)
{
    if (_indexL >= 0)
    {
        _controls[_indexL]._control->OnLButtonClick(x, y);
    }
    else
    {
        base::OnLButtonClick(x, y);
    }
}

void ControlObjectContainer::OnRButtonDown(float x, float y)
{
    // RMB is reserved for tumbling the prop itself, so skip the child
    // dispatch and go straight to base (ControlObject::OnRButtonDown) —
    // no Control3D child responds to RMB today, and stealing the event
    // means the user can right-drag anywhere on the prop, even on top
    // of a listbox or button, to rotate.
    _indexR = -1;
    base::OnRButtonDown(x, y);
}

void ControlObjectContainer::OnRButtonUp(float x, float y)
{
    if (_indexR >= 0)
    {
        _controls[_indexR]._control->OnRButtonUp(x, y);
    }
    else
    {
        base::OnRButtonUp(x, y);
    }
    _indexR = -1;
}

void ControlObjectContainer::OnMouseMove(float x, float y, bool active)
{
    if (active)
    {
        int index;
        if (InputSubsystem::Instance().IsMouseLeftDown())
        {
            index = _indexL;
        }
        else if (InputSubsystem::Instance().IsMouseRightDown())
        {
            index = _indexR;
        }
        else
        {
            index = FindControl(x, y);
        }
        if (_indexMove >= 0 && _indexMove != index)
        {
            _controls[_indexMove]._control->OnMouseMove(x, y, false);
        }
        if (index >= 0)
        {
            _controls[index]._control->OnMouseMove(x, y);
        }
        else
        {
            base::OnMouseMove(x, y);
        }
        _indexMove = index;
    }
    else
    {
        if (_indexMove >= 0)
        {
            _controls[_indexMove]._control->OnMouseMove(x, y, false);
        }
        else
        {
            base::OnMouseMove(x, y, false);
        }
        _indexMove = -1;
    }
    base::OnMouseMove(x, y, active);
}

void ControlObjectContainer::OnMouseHold(float x, float y, bool active)
{
    if (active)
    {
        int index;
        if (InputSubsystem::Instance().IsMouseLeftDown())
        {
            index = _indexL;
        }
        else if (InputSubsystem::Instance().IsMouseRightDown())
        {
            index = _indexR;
        }
        else
        {
            index = FindControl(x, y);
        }
        if (_indexMove >= 0 && _indexMove != index)
        {
            _controls[_indexMove]._control->OnMouseMove(x, y, false);
        }
        if (index >= 0)
        {
            _controls[index]._control->OnMouseHold(x, y);
        }
        else
        {
            base::OnMouseHold(x, y);
        }
        _indexMove = index;
    }
    else
    {
        if (_indexMove >= 0)
        {
            _controls[_indexMove]._control->OnMouseHold(x, y, false);
        }
        else
        {
            base::OnMouseHold(x, y, false);
        }
        _indexMove = -1;
    }
    base::OnMouseHold(x, y, active);
}

void ControlObjectContainer::OnMouseZChanged(float dz)
{
    if (_indexMove >= 0)
    {
        _controls[_indexMove]._control->OnMouseZChanged(dz);
    }
    else
    {
        base::OnMouseZChanged(dz);
    }
}

void ControlObjectContainer::SetFocus(int i, bool def)
{
    if (i == _indexFocused)
    {
        return;
    }

    if (_indexFocused >= 0 && _indexFocused < _controls.Size())
    {
        if (!_controls[_indexFocused]._control->OnKillFocus())
        {
            return;
        }
    }

    if (i >= 0)
    {
        _controls[i]._control->OnSetFocus(true, def);
    }
    _indexFocused = i;
}

bool ControlObjectContainer::NextCtrl()
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    for (int i = _indexFocused + 1; i < n; i++)
    {
        IControl* ctrl = _controls[i]._control;
        if (ctrl->IsVisible() && ctrl->IsEnabled())
        {
            SetFocus(i);
            return true;
        }
    }
    SetFocus(-1);
    return false;
}

bool ControlObjectContainer::PrevCtrl()
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    for (int i = _indexFocused - 1; i >= 0; i--)
    {
        IControl* ctrl = _controls[i]._control;
        if (ctrl->IsVisible() && ctrl->IsEnabled())
        {
            SetFocus(i);
            return true;
        }
    }
    SetFocus(-1);
    return false;
}

bool ControlObjectContainer::NextDefaultCtrl()
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    for (int i = _indexFocused + 1; i < n; i++)
    {
        IControl* ctrl = _controls[i]._control;
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
        {
            SetFocus(i, true);
            return true;
        }
    }
    //	SetFocus(-1);
    return false;
}

bool ControlObjectContainer::PrevDefaultCtrl()
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    for (int i = _indexFocused - 1; i >= 0; i--)
    {
        IControl* ctrl = _controls[i]._control;
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
        {
            SetFocus(i, true);
            return true;
        }
    }
    //	SetFocus(-1);
    return false;
}

bool ControlObjectContainer::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    if (_indexFocused >= 0 && _indexFocused < n && _controls[_indexFocused]._control->OnKeyDown(nChar, nRepCnt, nFlags))
    {
        return true;
    }
    else
    {
        switch (nChar)
        {
            case SDLK_TAB:
                if (InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) ||
                    InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
                {
                    return PrevCtrl();
                }
                else
                {
                    return NextCtrl();
                }
            case SDLK_LEFT:
            case SDLK_UP:
            case SDLK_PAGEUP:
                if (_indexFocused >= 0 && _controls[_indexFocused]._control->CanBeDefault())
                {
                    return PrevDefaultCtrl();
                }
                else
                {
                    return true;
                }
            case SDLK_RIGHT:
            case SDLK_DOWN:
            case SDLK_PAGEDOWN:
                if (_indexFocused >= 0 && _controls[_indexFocused]._control->CanBeDefault())
                {
                    return NextDefaultCtrl();
                }
                else
                {
                    return true;
                }
            default:
                return false;
        }
    }
}

bool ControlObjectContainer::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    if (_indexFocused >= 0 && _indexFocused < n && _controls[_indexFocused]._control->OnKeyUp(nChar, nRepCnt, nFlags))
    {
        return true;
    }
    return false;
}

bool ControlObjectContainer::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    if (_indexFocused >= 0 && _indexFocused < n && _controls[_indexFocused]._control->OnChar(nChar, nRepCnt, nFlags))
    {
        return true;
    }
    return false;
}

bool ControlObjectContainer::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    if (_indexFocused >= 0 && _indexFocused < n && _controls[_indexFocused]._control->OnIMEChar(nChar, nRepCnt, nFlags))
    {
        return true;
    }
    return false;
}

bool ControlObjectContainer::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
    int n = _controls.Size();
    if (n == 0)
    {
        return false;
    }
    if (_indexFocused >= 0 && _indexFocused < n && _controls[_indexFocused]._control->OnIMEComposition(nChar, nFlags))
    {
        return true;
    }
    return false;
}

void ControlObjectContainer::OnDraw(float alpha)
{
    ControlObjectWithZoom::OnDraw(alpha);

    bool isAnimated = true;
    if (isAnimated)
    {
        Animate(_shape->FindMemoryLevel());
    }
    for (int i = 0; i < _controls.Size(); i++)
    {
        if (i == _indexFocused)
        {
            continue;
        }
        ControlInObject& info = _controls[i];
        Control* ctrl = info._control;
        if (ctrl->IsVisible())
        {
            if (isAnimated)
            {
                info.posTL = _shape->MemoryPoint(info.selection + RString(" TL"));
                info.posTR = _shape->MemoryPoint(info.selection + RString(" TR"));
                info.posBL = _shape->MemoryPoint(info.selection + RString(" BL"));
                info.posBR = _shape->MemoryPoint(info.selection + RString(" BR"));
                ctrl->UpdateInfo(this, info);
            }
            ctrl->OnDraw(alpha);
        }
    }
    // draw focused control at last
    if (_indexFocused >= 0 && _indexFocused < _controls.Size())
    {
        ControlInObject& info = _controls[_indexFocused];
        Control* ctrl = info._control;
        if (ctrl->IsVisible())
        {
            if (isAnimated)
            {
                info.posTL = _shape->MemoryPoint(info.selection + RString(" TL"));
                info.posTR = _shape->MemoryPoint(info.selection + RString(" TR"));
                info.posBL = _shape->MemoryPoint(info.selection + RString(" BL"));
                info.posBR = _shape->MemoryPoint(info.selection + RString(" BR"));
                ctrl->UpdateInfo(this, info);
            }
            ctrl->OnDraw(alpha);
        }
    }
    if (isAnimated)
    {
        Deanimate(_shape->FindMemoryLevel());
    }
}

void ControlObjectContainer::DrawTooltip(float x, float y)
{
    int iFound = FindControl(x, y);
    if (iFound >= 0)
    {
        _controls[iFound]._control->DrawTooltip(x, y);
    }
    else
    {
        base::DrawTooltip(x, y);
    }
}

ControlObjectContainerAnim::ControlObjectContainerAnim(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : ControlObjectContainer(parent, idc, cls)
{
    _shape->SetAutoCenter(false);
    _shape->CalculateBoundingSphere();
    _shape->AllowAnimation();

    _open = false;
    _close = false;
    _animSpeed = cls >> "animSpeed";
    _invAnimSpeed = 1.0 / _animSpeed;
    _skeleton = new Skeleton();
    AnimationRTName name;
    name.name = GetAnimationName(cls >> "animation");
    name.skeleton = _skeleton;
    _animation = new AnimationRT(name, false);
    _animation->Prepare(_shape, _skeleton, _weights, false);

    int autoZoom = cls >> "autoZoom";
    _autoZoom = autoZoom != 0;
    int autoOpen = cls >> "autoOpen";
    if (autoOpen != 0)
    {
        _phase = 0;
        Open();
    }
    else
    {
        _phase = cls >> "animPhase";
    }
}

void ControlObjectContainerAnim::Open()
{
    if (_open)
    {
        return;
    }
    _open = true;
    _close = false;
    _animStart = Glob.uiTime - _phase * _invAnimSpeed;
    /*
        if (_close)
        {
            _close = false;
            _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
        }
        else
        {
            _animStart = Glob.uiTime;
        }
    */
    if (_autoZoom)
    {
        Zoom(true);
    }
}

void ControlObjectContainerAnim::Close()
{
    if (_close)
    {
        return;
    }
    _close = true;
    _open = false;
    _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
    /*
        if (_open)
        {
            _open = false;
            _animStart = Glob.uiTime - (1 - _phase) * _invAnimSpeed;
        }
        else
        {
            _animStart = Glob.uiTime;
        }
    */
    if (_autoZoom)
    {
        Zoom(false);
    }
}

void ControlObjectContainerAnim::Animate(int level)
{
    if (_open)
    {
        _phase = _animSpeed * (Glob.uiTime - _animStart);
        if (_phase >= 1.0)
        {
            _phase = 1.0;
            _open = false;
        }
    }
    else if (_close)
    {
        _phase = 1.0 - _animSpeed * (Glob.uiTime - _animStart);
        if (_phase <= 0)
        {
            _phase = 0;
            _close = false;
            _parent->OnCtrlClosed(IDC());
        }
    }
    /*
        Shape *shape = _shape->Level(level);
        if (shape && shape->IsAnimated())
        {
            shape->SetPhase(_phase, -1);
        }
    */
    _animation->Apply(_weights, _shape, level, _phase);
}

void ControlObjectContainerAnim::Deanimate(int level)
{
    _animation->Apply(_weights, _shape, level, 0);
    /*
        Shape *shape = _shape->Level(level);
        if (shape && shape->IsAnimated())
            shape->SetPhase(0, 0);
    */
}

const float textBorder = 0.005;
