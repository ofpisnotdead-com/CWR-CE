
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Map/UIMapCommon.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>

#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <cmath>
#include <mutex>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::UITime;

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using namespace Poseidon;
extern const float CameraZoom;
extern const float InvCameraZoom;

static const float tooltipDelay = 0.3f;

ControlsContainer::ControlsContainer(ControlsContainer* parent)
{
    _parent = parent;
    _enableSimulation = true;
    _enableDisplay = true;
    _enableUI = true;
    _destroyed = false;
    Init();
    SetCursor("Arrow");

    _tooltipTime = Glob.uiTime + tooltipDelay;
}

ControlsContainer::~ControlsContainer()
{
    // virtual function cannot be called from destructor
    // Destroy();
}

void ControlsContainer::Init()
{
    _alwaysShow = false;
    _moving = false;
    _movingEnable = true;
    _idd = -1;
    _exit = -1;
    _controlsBackground.Clear();
    _objects.Clear();
    _controlsForeground.Clear();
    auto& input = InputSubsystem::Instance();
    _lastLButton = input.IsMouseLeftDown();
    _lastRButton = input.IsMouseRightDown();
    _lastX = input.GetCursorX();
    _lastY = input.GetCursorY();
    _dblClkTimeout = UITIME_MIN;

    _indexFocused = ControlId::Null();
    _indexLCaptured = ControlId::Null();
    _indexRCaptured = ControlId::Null();
    _indexDefault = ControlId::Null();
    _indexMouseOver = ControlId::Null();
}

IControl* ControlsContainer::GetCtrl(int idc)
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        IControl* owner = _controlsBackground[i];
        if (!owner)
            continue;
        IControl* ctrl = owner->GetCtrl(idc);
        if (ctrl)
        {
            return ctrl;
        }
    }
    for (int i = 0; i < _objects.Size(); i++)
    {
        IControl* owner = _objects[i];
        if (!owner)
            continue;
        IControl* ctrl = owner->GetCtrl(idc);
        if (ctrl)
        {
            return ctrl;
        }
    }
    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        IControl* owner = _controlsForeground[i];
        if (!owner)
            continue;
        IControl* ctrl = owner->GetCtrl(idc);
        if (ctrl)
        {
            return ctrl;
        }
    }
    return nullptr;
}

IControl* ControlsContainer::GetFocused()
{
    return Ctrl(_indexFocused)->GetFocused();
}

int ControlsContainer::GetFocusedIdc()
{
    IControl* ctrl = Ctrl(_indexFocused);
    if (!ctrl)
    {
        return -1;
    }
    if (auto* container = dynamic_cast<ControlObjectContainer*>(ctrl))
    {
        return container->GetFocusedIdc();
    }
    return ctrl->IDC();
}

void ControlsContainer::RefreshLocalizedText()
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
        if (_controlsBackground[i])
            _controlsBackground[i]->ReloadLocalizedText();
    for (int i = 0; i < _objects.Size(); i++)
        if (_objects[i])
            _objects[i]->ReloadLocalizedText();
    for (int i = 0; i < _controlsForeground.Size(); i++)
        if (_controlsForeground[i])
            _controlsForeground[i]->ReloadLocalizedText();
    if (Child())
        Child()->RefreshLocalizedText();
}

void ControlsContainer::FocusCtrl(int idc)
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        if (_controlsBackground[i]->IDC() == idc)
        {
            ControlId ci = ControlId(CLBackground, i);
            SetFocus(ci);
            return;
        }
    }
    for (int i = 0; i < _objects.Size(); i++)
    {
        if (_objects[i]->IDC() == idc)
        {
            ControlId ci = ControlId(CLObjects, i);
            SetFocus(ci);
            return;
        }
    }
    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        if (_controlsForeground[i]->IDC() == idc)
        {
            ControlId ci = ControlId(CLForeground, i);
            SetFocus(ci);
            return;
        }
    }
    // Descend into 3D object containers (e.g. notebook).  Their
    // sub-controls (rows, buttons rendered on the notebook surface)
    // have their own IDCs and need to be reachable via FocusCtrl too —
    // otherwise a Display could focus its 2D buttons but never an inner
    // notebook row, which is exactly the unified-input model we need.
    //
    // Order matters: outer SetFocus(object) is called first because
    // ControlObjectContainer::OnSetFocus eagerly focuses its first
    // CanBeDefault sub-control on entry; we then override that to the
    // specific IDC the caller actually asked for.
    for (int i = 0; i < _objects.Size(); i++)
    {
        if (auto* container = dynamic_cast<ControlObjectContainer*>(_objects[i].operator->()))
        {
            if (container->GetCtrl(idc))
            {
                ControlId ci = ControlId(CLObjects, i);
                SetFocus(ci);
                container->FocusCtrlByIdc(idc);
                return;
            }
        }
    }
}

IControl* ControlsContainer::GetCtrl(float x, float y)
{
    int i;
    for (i = 0; i < _controlsForeground.Size(); i++)
    {
        IControl* ctrl = _controlsForeground[i];
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->IsInside(x, y))
        {
            return ctrl->GetCtrl(x, y);
        }
    }
    SortObjects();
    for (i = 0; i < _objects.Size(); i++)
    {
        IControl* ctrl = _objects[i];
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->IsInside(x, y))
        {
            return ctrl->GetCtrl(x, y);
        }
    }
    for (i = 0; i < _controlsBackground.Size(); i++)
    {
        IControl* ctrl = _controlsBackground[i];
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->IsInside(x, y))
        {
            return ctrl->GetCtrl(x, y);
        }
    }
    return nullptr;
}

void ControlsContainer::SetCursor(const char* name)
{
    if (name)
    {
        if (stricmp(name, _cursorName) == 0)
        {
            return;
        }
        _cursorName = name;
        const ParamEntry& cls = Pars >> "CfgWrapperUI" >> "Cursors" >> name;
        _cursorTexture = GlobPreloadTexture(GetPictureName(cls >> "texture"));
        _cursorX = cls >> "hotspotX";
        _cursorY = cls >> "hotspotY";
        _cursorW = cls >> "width";
        _cursorH = cls >> "height";
        _cursorColor = GetPackedColor(cls >> "color");
    }
    else
    {
        if (_cursorName.GetLength() == 0)
        {
            return;
        }
        _cursorName = "";
        _cursorTexture = nullptr;
        _cursorX = 0;
        _cursorY = 0;
        _cursorColor = PackedColor(0, 0, 0, 0);
    }
}

void ControlsContainer::LoadControl(const ParamEntry& ctrlCls)
{
    int type = ctrlCls >> "type";
    int idc = ctrlCls >> "idc";
    Control* ctrl = OnCreateCtrl(type, idc, ctrlCls);
    if (ctrl != nullptr)
    {
        int index = _controlsForeground.Add(ctrl);
        if (ctrl->CanBeDefault() && ctrlCls.FindEntry("default") && (ctrlCls >> "default").GetInt())
        {
            PoseidonAssert(_indexDefault.IsNull()); // Only one default button
            _indexDefault = ControlId(CLForeground, index);
            ctrl->SetDefault();
        }
    }
}

void ControlsContainer::LoadObject(const ParamEntry& ctrlCls)
{
    int type = ctrlCls >> "type";
    int idc = ctrlCls >> "idc";
    ControlObject* ctrl = OnCreateObject(type, idc, ctrlCls);
    if (ctrl != nullptr)
    {
        int index = _objects.Add(ctrl);
        if (ctrl->CanBeDefault() && ctrlCls.FindEntry("default") && (ctrlCls >> "default").GetInt())
        {
            PoseidonAssert(_indexDefault.IsNull()); // Only one default button
            _indexDefault = ControlId(CLObjects, index);
            ctrl->SetDefault();
        }
    }
}

void ControlsContainer::LoadControlBackground(const ParamEntry& ctrlCls)
{
    int type = ctrlCls >> "type";
    int idc = ctrlCls >> "idc";
    Control* ctrl = OnCreateCtrl(type, idc, ctrlCls);
    if (ctrl != nullptr)
    {
        int index = _controlsBackground.Add(ctrl);
        if (ctrl->CanBeDefault() && ctrlCls.FindEntry("default") && (ctrlCls >> "default").GetInt())
        {
            PoseidonAssert(_indexDefault.IsNull()); // Only one default button
            _indexDefault = ControlId(CLBackground, index);
            ctrl->SetDefault();
        }
    }
}

void ControlsContainer::Load(const ParamEntry& clsEntry)
{
    _idd = clsEntry >> "idd";
    int moving = clsEntry >> "movingEnable";
    _movingEnable = moving != 0;
    const ParamEntry* cfg = clsEntry.FindEntry("controls");
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
                const ParamEntry& ctrlCls = clsEntry >> ctrlName;
                LoadControl(ctrlCls);
            }
        }
    }
    cfg = clsEntry.FindEntry("objects");
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
                LoadObject(ctrlCls);
            }
        }
        else
        {
            for (int i = 0; i < cfg->GetSize(); i++)
            {
                RString ctrlName = (*cfg)[i];
                const ParamEntry& ctrlCls = clsEntry >> ctrlName;
                LoadObject(ctrlCls);
            }
        }
    }
    cfg = clsEntry.FindEntry("controlsBackground");
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
                LoadControlBackground(ctrlCls);
            }
        }
        else
        {
            for (int i = 0; i < cfg->GetSize(); i++)
            {
                RString ctrlName = (*cfg)[i];
                const ParamEntry& ctrlCls = clsEntry >> ctrlName;
                LoadControlBackground(ctrlCls);
            }
        }
    }
    NextCtrl();

    if (!_indexDefault.IsNull())
    {
        IControl* focused = GetFocused();
        if (focused->CanBeDefault())
        {
            SetFocus(_indexDefault, true);
        }
    }
}

void ControlsContainer::Load(const char* clsName)
{
    const ParamEntry& mainCfg = Res >> clsName;
    Load(mainCfg);
}

void ControlsContainer::Load(int idd) {}

bool ControlsContainer::CanDestroy()
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        if (!_controlsBackground[i]->OnCanDestroy(_exit))
        {
            return false;
        }
    }
    for (int i = 0; i < _objects.Size(); i++)
    {
        if (!_objects[i]->OnCanDestroy(_exit))
        {
            return false;
        }
    }
    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        if (!_controlsForeground[i]->OnCanDestroy(_exit))
        {
            return false;
        }
    }
    return true;
}

void ControlsContainer::Destroy()
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        _controlsBackground[i]->OnDestroy(_exit);
    }
    for (int i = 0; i < _objects.Size(); i++)
    {
        _objects[i]->OnDestroy(_exit);
    }
    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        _controlsForeground[i]->OnDestroy(_exit);
    }
    _destroyed = true;
}

void ControlsContainer::Move(float dx, float dy)
{
    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        _controlsBackground[i]->Move(dx, dy);
    }
    for (int i = 0; i < _objects.Size(); i++)
    {
        _objects[i]->Move(dx, dy);
    }
    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        _controlsForeground[i]->Move(dx, dy);
    }
}

Control* ControlsContainer::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (type)
    {
        case CT_STATIC:
        {
            return new CStatic(this, idc, cls);
        }
        case CT_EDIT:
        {
            return new CEdit(this, idc, cls);
        }
        case CT_BUTTON:
        {
            return new CButton(this, idc, cls);
        }
        case CT_SLIDER:
        {
            return new CSlider(this, idc, cls);
        }
        case CT_COMBO:
        {
            return new CCombo(this, idc, cls);
        }
        case CT_LISTBOX:
        {
            return new CListBox(this, idc, cls);
        }
        case CT_TOOLBOX:
        {
            return new CToolBox(this, idc, cls);
        }
        case CT_CHECKBOXES:
        {
            return new CCheckBoxes(this, idc, cls);
        }
        case CT_PROGRESS:
        {
            return new CProgressBar(this, idc, cls);
        }
        case CT_HTML:
        {
            return new CHTML(this, idc, cls);
        }
        case CT_STATIC_SKEW:
        {
            return new CSkewStatic(this, idc, cls);
        }
        case CT_ACTIVETEXT:
        {
            return new CActiveText(this, idc, cls);
        }
        case CT_TREE:
        {
            return new CTree(this, idc, cls);
        }
        case CT_3DSTATIC:
        {
            return new C3DStatic(this, idc, cls);
        }
        case CT_3DEDIT:
        {
            return new C3DEdit(this, idc, cls);
        }
        case CT_3DACTIVETEXT:
        {
            return new C3DActiveText(this, idc, cls);
        }
        case CT_3DSLIDER:
        {
            return new C3DSlider(this, idc, cls);
        }
        case CT_3DLISTBOX:
        {
            return new C3DListBox(this, idc, cls);
        }
        case CT_3DSCROLLBAR:
        {
            return new C3DScrollBarStandalone(this, idc, cls);
        }
        case CT_3DHTML:
        {
            return new C3DHTML(this, idc, cls);
        }
        case CT_MAP:
        {
            return Poseidon::CreateStaticMap(this, idc, cls);
        }
        case CT_MAP_MAIN:
        {
            return Poseidon::CreateStaticMapMain(this, idc, cls);
        }
    }
    return nullptr;
}

Camera OriginalCamera;

void ControlsContainer::OnDraw(EntityAI* vehicle, float alpha)
{
    if (_exit >= 0)
    {
        return;
    }

    // draw controls
    for (int i = 0; i < _objects.Size(); i++)
    {
        _objects[i]->UpdatePosition();
    }
    SortObjects();

    for (int i = 0; i < _controlsBackground.Size(); i++)
    {
        if (_indexFocused.list == CLBackground && _indexFocused.id == i)
        {
            continue;
        }
        if (_controlsBackground[i]->IsVisible())
        {
            _controlsBackground[i]->OnDraw(alpha);
        }
    }
    if (_indexFocused.list == CLBackground)
    {
        if (_controlsBackground[_indexFocused.id]->IsVisible())
        {
            _controlsBackground[_indexFocused.id]->OnDraw(alpha);
        }
    }

    // temporary override camera
    if (_objects.Size() > 0)
    {
        // clear Z-buffer
        GEngine->Clear(true, false);
        // create light
        LightList work(true);
        work.Resize(1);
        Ref<LightPoint> light = new LightPoint(Color(1, 1, 0.8, 1), Color(0.5, 0.5, 0.4, 1));
        work[0] = light;
        light->SetPosition(Vector3(1, 1, 0));
        light->SetBrightness(8 * Square(CameraZoom));
        GScene->SetActiveLights(work);

        // create camera
        PoseidonAssert(GScene->GetCamera());
        OriginalCamera = *GScene->GetCamera();
        // set object drawing parameters
        float fov = 0.5 * InvCameraZoom;
        Camera cam;
        cam.SetPerspectiveForView(GEngine, 0.1, 100.0, fov);
        cam.Adjust(GEngine);
        GScene->SetCamera(cam);
        GEngine->UpdateFrameCamera();
        for (int i = _objects.Size() - 1; i >= 0; i--)
        {
            if (_objects[i]->IsVisible())
            {
                _objects[i]->OnDraw(alpha);
            }
        }
        // restore camera
        GScene->SetCamera(OriginalCamera);
    }

    for (int i = 0; i < _controlsForeground.Size(); i++)
    {
        if (_indexFocused.list == CLForeground && _indexFocused.id == i)
        {
            continue;
        }
        if (_controlsForeground[i]->IsVisible())
        {
            _controlsForeground[i]->OnDraw(alpha);
        }
    }
    if (_indexFocused.list == CLForeground)
    {
        if (_controlsForeground[_indexFocused.id]->IsVisible())
        {
            _controlsForeground[_indexFocused.id]->OnDraw(alpha);
        }
    }

    if (Glob.uiTime >= _tooltipTime && !_indexMouseOver.IsNull())
    {
        IControl* ctrl = Ctrl(_indexMouseOver);
        if (ctrl)
        {
            float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
            float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;
            ctrl->DrawTooltip(mouseX, mouseY);
        }
    }
}

bool ControlsContainer::OnUnregisteredAddonUsed(RString addon)
{
    return false;
}

void ControlsContainer::OnSimulate(EntityAI* vehicle)
{
    if (_exit < 0)
    {
        SortObjects();
        int i;
        // timer
        for (i = 0; i < _controlsBackground.Size(); i++)
        {
            if (_controlsBackground[i]->GetTimer() <= Glob.uiTime)
            {
                _controlsBackground[i]->OnTimer();
            }
        }
        for (i = 0; i < _objects.Size(); i++)
        {
            if (_objects[i]->GetTimer() <= Glob.uiTime)
            {
                _objects[i]->OnTimer();
            }
        }
        for (i = 0; i < _controlsForeground.Size(); i++)
        {
            if (_controlsForeground[i]->GetTimer() <= Glob.uiTime)
            {
                _controlsForeground[i]->OnTimer();
            }
        }

        // mouse buttons
        auto& input = InputSubsystem::Instance();
        float mouseX = 0.5 + input.GetCursorX() * 0.5;
        float mouseY = 0.5 + input.GetCursorY() * 0.5;
        input.ConsumeCursorDeltaX();
        input.ConsumeCursorDeltaY();

        // found control with mouse over
        ControlId iFound;
        IControl* ctrlFound = nullptr;
        // check focused item first
        if (_indexFocused.list == CLForeground)
        {
            IControl* ctrl = _controlsForeground[_indexFocused.id];
            PoseidonAssert(ctrl);
            if (ctrl->IsVisible() && ctrl->IsInside(mouseX, mouseY))
            {
                if (ctrl->IsEnabled() || ctrl->GetType() == CT_STATIC && (ctrl->GetStyle() & ST_TYPE) == ST_TITLE_BAR)
                {
                    iFound.list = CLForeground;
                    iFound.id = _indexFocused.id;
                    ctrlFound = ctrl;
                    goto CtrlFound;
                }
            }
        }
        for (i = 0; i < _controlsForeground.Size(); i++)
        {
            if (_indexFocused.list == CLForeground && _indexFocused.id == i)
            {
                continue;
            }
            IControl* ctrl = _controlsForeground[i];
            PoseidonAssert(ctrl);
            if (ctrl->IsVisible() && ctrl->IsInside(mouseX, mouseY))
            {
                if (ctrl->IsEnabled() || ctrl->GetType() == CT_STATIC && (ctrl->GetStyle() & ST_TYPE) == ST_TITLE_BAR)
                {
                    iFound.list = CLForeground;
                    iFound.id = i;
                    ctrlFound = ctrl;
                    goto CtrlFound;
                }
            }
        }
        for (i = 0; i < _objects.Size(); i++)
        {
            IControl* ctrl = _objects[i];
            PoseidonAssert(ctrl);
            if (ctrl->IsVisible() && ctrl->IsInside(mouseX, mouseY))
            {
                if (ctrl->IsEnabled())
                {
                    iFound.list = CLObjects;
                    iFound.id = i;
                    ctrlFound = ctrl;
                    goto CtrlFound;
                }
            }
        }
        // check focused item first
        if (_indexFocused.list == CLBackground)
        {
            IControl* ctrl = _controlsBackground[_indexFocused.id];
            PoseidonAssert(ctrl);
            if (ctrl->IsVisible() && ctrl->IsInside(mouseX, mouseY))
            {
                if (ctrl->IsEnabled() || ctrl->GetType() == CT_STATIC && (ctrl->GetStyle() & ST_TYPE) == ST_TITLE_BAR)
                {
                    iFound.list = CLBackground;
                    iFound.id = _indexFocused.id;
                    ctrlFound = ctrl;
                    goto CtrlFound;
                }
            }
        }
        for (i = 0; i < _controlsBackground.Size(); i++)
        {
            if (_indexFocused.list == CLBackground && _indexFocused.id == i)
            {
                continue;
            }
            IControl* ctrl = _controlsBackground[i];
            PoseidonAssert(ctrl);
            if (ctrl->IsVisible() && ctrl->IsInside(mouseX, mouseY))
            {
                if (ctrl->IsEnabled() || ctrl->GetType() == CT_STATIC && (ctrl->GetStyle() & ST_TYPE) == ST_TITLE_BAR)
                {
                    iFound.list = CLBackground;
                    iFound.id = i;
                    ctrlFound = ctrl;
                    goto CtrlFound;
                }
            }
        }
    CtrlFound:
        if (input.IsMouseLeftDown() && !_lastLButton)
        {
            if (!iFound.IsNull())
            {
                if (ctrlFound->IsEnabled())
                {
                    if (SetFocus(iFound))
                    {
                        if (Glob.uiTime < _dblClkTimeout && iFound == _indexLCaptured)
                        {
                            ctrlFound->OnLButtonDblClick(mouseX, mouseY);
                            _dblClkTimeout = UITIME_MIN;
                        }
                        else
                        {
                            ctrlFound->OnLButtonDown(mouseX, mouseY);
                            _dblClkTimeout = Glob.uiTime + 0.3;
                            _dblClkX = mouseX;
                            _dblClkY = mouseY;
                        }
                    }
                }
                else if (ctrlFound->GetType() == CT_STATIC && (ctrlFound->GetStyle() & ST_TYPE) == ST_TITLE_BAR)
                {
                    _moving = true;
                }
            }
            _indexLCaptured = iFound;
        }
        else if (!input.IsMouseLeftDown() && _lastLButton)
        {
            if (!_indexLCaptured.IsNull())
            {
                IControl* ctrl = Ctrl(_indexLCaptured);
                if (ctrl->IsEnabled())
                {
                    ctrl->OnLButtonUp(mouseX, mouseY);
                }
            }
            if (Glob.uiTime >= _dblClkTimeout)
            {
                _indexLCaptured = ControlId::Null();
            }
            _moving = false;
        }

        if (_dblClkTimeout > UITime(-10000) && !_indexLCaptured.IsNull())
        {
            if (Glob.uiTime >= _dblClkTimeout || fabs(mouseX - _dblClkX) > 0.01 || fabs(mouseY - _dblClkY) > 0.01)
            {
                IControl* ctrl = Ctrl(_indexLCaptured);
                if (ctrl->IsEnabled())
                {
                    ctrl->OnLButtonClick(mouseX, mouseY);
                }
                _dblClkTimeout = UITIME_MIN;
                if (!input.IsMouseLeftDown())
                {
                    _indexLCaptured = ControlId::Null();
                }
            }
        }

        if (input.IsMouseRightDown() && !_lastRButton && _indexRCaptured.IsNull())
        {
            if (!iFound.IsNull())
            {
                if (ctrlFound->IsEnabled())
                {
                    ctrlFound->OnRButtonDown(mouseX, mouseY);
                    _indexRCaptured = iFound;
                }
            }
        }
        else if (!input.IsMouseRightDown() && _lastRButton)
        {
            if (!_indexRCaptured.IsNull())
            {
                Ctrl(_indexRCaptured)->OnRButtonUp(mouseX, mouseY);
                _indexRCaptured = ControlId::Null();
            }
        }

        // mouse moving
        ControlId index;
        if (!_indexLCaptured.IsNull())
        {
            index = _indexLCaptured;
        }
        else if (!_indexRCaptured.IsNull())
        {
            index = _indexRCaptured;
        }
        else if (!iFound.IsNull() && ctrlFound->IsEnabled())
        {
            index = iFound;
        }
        // ?? maybe after some distance
        if (_lastX != input.GetCursorX() || _lastY != input.GetCursorY())
        {
            if (_moving)
            {
                Move(0.5 * (input.GetCursorX() - _lastX), 0.5 * (input.GetCursorY() - _lastY));
            }

            if (!_indexMouseOver.IsNull() && _indexMouseOver != index)
            {
                Ctrl(_indexMouseOver)->OnMouseMove(mouseX, mouseY, false);
            }
            if (!index.IsNull())
            {
                Ctrl(index)->OnMouseMove(mouseX, mouseY);
            }

            _tooltipTime = Glob.uiTime + tooltipDelay;
        }
        else
        {
            if (!_indexMouseOver.IsNull() && _indexMouseOver != index)
            {
                Ctrl(_indexMouseOver)->OnMouseMove(mouseX, mouseY, false);
            }
            if (!index.IsNull())
            {
                Ctrl(index)->OnMouseHold(mouseX, mouseY);
            }
        }
        _indexMouseOver = index;
        if (!_indexMouseOver.IsNull())
        {
            Ctrl(_indexMouseOver)->OnMouseZChanged(input.ConsumeCursorScroll());
        }

        _lastLButton = input.IsMouseLeftDown();
        _lastRButton = input.IsMouseRightDown();
        _lastX = input.GetCursorX();
        _lastY = input.GetCursorY();
    }

    if (_exit >= 0 && !_destroyed)
    {
        if (CanDestroy())
        {
            Destroy();
        }
        else
        {
            _exit = -1;
        }
    }
}

bool ControlsContainer::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    bool ok = false;
    if (!_indexFocused.IsNull() && Ctrl(_indexFocused)->OnKeyDown(nChar, nRepCnt, nFlags))
    {
        ok = true;
    }
    else
    {
        switch (nChar)
        {
            case SDLK_TAB:
                if (InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) ||
                    InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
                {
                    PrevCtrl();
                }
                else
                {
                    NextCtrl();
                }
                ok = true;
                break;
            case SDLK_LEFT:
            case SDLK_UP:
            case SDLK_PAGEUP:
                if (!_indexFocused.IsNull() && GetFocused()->CanBeDefault())
                {
                    PrevDefaultCtrl();
                }
                ok = true;
                break;
            case SDLK_RIGHT:
            case SDLK_DOWN:
            case SDLK_PAGEDOWN:
                if (!_indexFocused.IsNull() && GetFocused()->CanBeDefault())
                {
                    NextDefaultCtrl();
                }
                ok = true;
                break;
            case SDLK_ESCAPE:
                OnButtonClicked(IDC_CANCEL);
                ok = true;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (!_indexDefault.IsNull())
                {
                    ok = Ctrl(_indexDefault)->OnKeyDown(nChar, nRepCnt, nFlags);
                }
                break;
        }
    }

    if (_exit >= 0 && !_destroyed)
    {
        if (CanDestroy())
        {
            Destroy();
        }
        else
        {
            _exit = -1;
        }
    }

    return ok;
}

bool ControlsContainer::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    bool ok = false;
    if (!_indexFocused.IsNull() && Ctrl(_indexFocused)->OnKeyUp(nChar, nRepCnt, nFlags))
    {
        ok = true;
    }
    else if (IsUiConfirmKey(nChar))
    {
        if (!_indexDefault.IsNull())
        {
            ok = Ctrl(_indexDefault)->OnKeyUp(nChar, nRepCnt, nFlags);
        }
    }

    if (_exit >= 0 && !_destroyed)
    {
        if (CanDestroy())
        {
            Destroy();
        }
        else
        {
            _exit = -1;
        }
    }

    return ok;
}

bool ControlsContainer::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (!_indexFocused.IsNull())
    {
        return Ctrl(_indexFocused)->OnChar(nChar, nRepCnt, nFlags);
    }
    return false;
}

bool ControlsContainer::OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (!_indexFocused.IsNull())
    {
        return Ctrl(_indexFocused)->OnIMEChar(nChar, nRepCnt, nFlags);
    }
    return false;
}

bool ControlsContainer::OnIMEComposition(unsigned nChar, unsigned nFlags)
{
    if (!_indexFocused.IsNull())
    {
        return Ctrl(_indexFocused)->OnIMEComposition(nChar, nFlags);
    }
    return false;
}

void ControlsContainer::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
            Exit(IDC_OK);
            break;
        case IDC_CANCEL:
            Exit(IDC_CANCEL);
            break;
    }
}

void ControlsContainer::NextCtrl()
{
    int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
    if (n == 0)
    {
        return;
    }

    ControlId id = _indexFocused;
    for (int i = 0; i < n; i++)
    {
        id = Next(id);
        IControl* ctrl = Ctrl(id);
        if (ctrl->IsVisible() && ctrl->IsEnabled())
        {
            SetFocus(id, true);
            return;
        }
    }
    ControlId ci = ControlId::Null();
    SetFocus(ci, true);
}

void ControlsContainer::PrevCtrl()
{
    int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
    if (n == 0)
    {
        return;
    }

    ControlId id = _indexFocused;
    for (int i = 0; i < n; i++)
    {
        id = Prev(id);
        IControl* ctrl = Ctrl(id);
        if (ctrl->IsVisible() && ctrl->IsEnabled())
        {
            SetFocus(id, false);
            return;
        }
    }
    ControlId ci = ControlId::Null();
    SetFocus(ci, false);
}

void ControlsContainer::NextDefaultCtrl()
{
    int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
    if (n == 0)
    {
        return;
    }

    ControlId id = _indexFocused;
    for (int i = 0; i < n; i++)
    {
        id = Next(id);
        IControl* ctrl = Ctrl(id);
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
        {
            SetFocus(id, true, true);
            return;
        }
    }
    ControlId ci = ControlId::Null();
    SetFocus(ci, true, true);
}

void ControlsContainer::PrevDefaultCtrl()
{
    int n = _controlsBackground.Size() + _objects.Size() + _controlsForeground.Size();
    if (n == 0)
    {
        return;
    }

    ControlId id = _indexFocused;
    for (int i = 0; i < n; i++)
    {
        id = Prev(id);
        IControl* ctrl = Ctrl(id);
        if (ctrl->IsVisible() && ctrl->IsEnabled() && ctrl->CanBeDefault())
        {
            SetFocus(id, false, true);
            return;
        }
    }
    ControlId ci = ControlId::Null();
    SetFocus(ci, false, true);
}

ControlObject* ControlsContainer::OnCreateObject(int type, int idc, const ParamEntry& cls)
{
    switch (type)
    {
        case CT_OBJECT:
            return new ControlObject(this, idc, cls);
        case CT_OBJECT_ZOOM:
            return new ControlObjectWithZoom(this, idc, cls);
        case CT_OBJECT_CONTAINER:
            return new ControlObjectContainer(this, idc, cls);
        case CT_OBJECT_CONT_ANIM:
            return new ControlObjectContainerAnim(this, idc, cls);
    }
    return nullptr;
}

int CmpControlObjects(const Ref<ControlObject>* pObj1, const Ref<ControlObject>* pObj2)
{
    ControlObject* obj1 = *pObj1;
    ControlObject* obj2 = *pObj2;
    return sign(obj1->Position().Z() - obj2->Position().Z());
}

void ControlsContainer::SortObjects()
{
    QSort(_objects.Data(), _objects.Size(), CmpControlObjects);
}

ControlId ControlsContainer::Next(ControlId& id)
{
    switch (id.list)
    {
        case CLNone:
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, 0);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, 0);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, 0);
            }
            return ControlId::Null();
        case CLBackground:
            if (id.id + 1 < _controlsBackground.Size())
            {
                return ControlId(CLBackground, id.id + 1);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, 0);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, 0);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, 0);
            }
            return ControlId::Null();
        case CLObjects:
            if (id.id + 1 < _objects.Size())
            {
                return ControlId(CLObjects, id.id + 1);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, 0);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, 0);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, 0);
            }
            return ControlId::Null();
        case CLForeground:
            if (id.id + 1 < _controlsForeground.Size())
            {
                return ControlId(CLForeground, id.id + 1);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, 0);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, 0);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, 0);
            }
            return ControlId::Null();
    }
    return ControlId::Null();
}

ControlId ControlsContainer::Prev(ControlId& id)
{
    switch (id.list)
    {
        case CLNone:
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, _controlsForeground.Size() - 1);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, _objects.Size() - 1);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, _controlsBackground.Size() - 1);
            }
            return ControlId::Null();
        case CLBackground:
            if (id.id > 0)
            {
                return ControlId(CLBackground, id.id - 1);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, _controlsForeground.Size() - 1);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, _objects.Size() - 1);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, _controlsBackground.Size() - 1);
            }
            return ControlId::Null();
        case CLObjects:
            if (id.id > 0)
            {
                return ControlId(CLObjects, id.id - 1);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, _controlsBackground.Size() - 1);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, _controlsForeground.Size() - 1);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, _objects.Size() - 1);
            }
            return ControlId::Null();
        case CLForeground:
            if (id.id > 0)
            {
                return ControlId(CLForeground, id.id - 1);
            }
            if (_objects.Size() > 0)
            {
                return ControlId(CLObjects, _objects.Size() - 1);
            }
            if (_controlsBackground.Size() > 0)
            {
                return ControlId(CLBackground, _controlsBackground.Size() - 1);
            }
            if (_controlsForeground.Size() > 0)
            {
                return ControlId(CLForeground, _controlsForeground.Size() - 1);
            }
            return ControlId::Null();
    }
    return ControlId::Null();
}

IControl* ControlsContainer::Ctrl(ControlId& id)
{
    switch (id.list)
    {
        case CLBackground:
            return _controlsBackground[id.id];
        case CLObjects:
            return _objects[id.id];
        case CLForeground:
            return _controlsForeground[id.id];
        default:
            return nullptr;
    }
}

bool ControlsContainer::SetFocus(ControlId& id, bool up, bool def)
{
    if (id == _indexFocused)
    {
        return true;
    }

    IControl* ctrl = Ctrl(id);
    if (ctrl && (!ctrl->IsVisible() || !ctrl->IsEnabled()))
    {
        return false;
    }

    if (!_indexFocused.IsNull())
    {
        if (!GetFocused()->OnKillFocus())
        {
            return false;
        }
    }
    if (ctrl)
    {
        ctrl->OnSetFocus(up, def);
    }
    _indexFocused = id;
    return true;
}

void ControlsContainer::DrawCursor()
{
    Texture* texture = GetCursorTexture();

    PackedColor color = GetCursorColor();
    float hsX = GetCursorX();
    float hsY = GetCursorY();

    const float mouseScrH = GetCursorH() / 600;
    const float mouseScrW = GetCursorW() / 800;
    float mouseScrX = InputSubsystem::Instance().GetCursorX() * 0.5 + 0.5;
    float mouseScrY = InputSubsystem::Instance().GetCursorY() * 0.5 + 0.5;
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    int mx = toInt((mouseScrX - hsX * mouseScrW) * w);
    int my = toInt((mouseScrY - hsY * mouseScrH) * h);
    int mw = toInt(mouseScrW * w);
    int mh = toInt(mouseScrH * h);

    CursorDrawDebug::Record(this, mx, my, mw, mh);

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(mx, my, mw, mh));
}

namespace CursorDrawDebug
{
namespace
{
struct CursorDrawSnapshot
{
    int idd = -1;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool hasValue = false;
};

std::mutex g_cursorDrawMutex;
CursorDrawSnapshot g_lastDraw;
} // namespace

void Record(ControlsContainer* owner, int x, int y, int w, int h)
{
    std::lock_guard<std::mutex> lock(g_cursorDrawMutex);
    g_lastDraw.idd = owner ? owner->IDD() : -1;
    g_lastDraw.x = x;
    g_lastDraw.y = y;
    g_lastDraw.w = w;
    g_lastDraw.h = h;
    g_lastDraw.hasValue = true;
}

bool Read(int& idd, int& x, int& y, int& w, int& h)
{
    std::lock_guard<std::mutex> lock(g_cursorDrawMutex);
    if (!g_lastDraw.hasValue)
        return false;

    idd = g_lastDraw.idd;
    x = g_lastDraw.x;
    y = g_lastDraw.y;
    w = g_lastDraw.w;
    h = g_lastDraw.h;
    return true;
}
} // namespace CursorDrawDebug

Display::Display(ControlsContainer* parent) : ControlsContainer(parent)
{
    // Every Display auto-refreshes its controls when the language changes; individual
    // controls know how to re-resolve their own $STR_ text via ReloadLocalizedText().
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLocalizedText(); });
}

Display::~Display()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

void Display::DrawHUD(VehicleWithAI* vehicle, float alpha)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        if (ptr->AlwaysShow())
        {
            ptr->OnDraw(vehicle, alpha);
        }
        ptr = ptr->Child();
    }

    ptr->OnDraw(vehicle, alpha);
    if (ptr->GetMsgBox())
    {
        ptr = ptr->GetMsgBox();
        //		ptr->GetMsgBox()->OnDraw(vehicle,alpha);
        ptr->OnDraw(vehicle, alpha);
    }

    // Cursor draw lives in GameCursorOverlay::Draw now.  World walks
    // the dialog stack and asks the topmost active container to
    // paint its cursor exactly once per frame, after every Display
    // has finished its OnDraw pass.  Keeps the legacy stacking order
    // — topmost dialog wins — without each Display painting its own.
}

void Display::SimulateHUD(VehicleWithAI* vehicle)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        ptr->GetMsgBox()->OnSimulate(vehicle);
        if (ptr->GetMsgBox()->GetExitCode() >= 0)
        {
            ptr->OnChildDestroyed(ptr->GetMsgBox()->IDD(), ptr->GetMsgBox()->GetExitCode());
            ptr->DestroyMsgBox();
        }
    }
    else
    {
        ptr->OnSimulate(vehicle);
        if (ptr->GetExitCode() >= 0)
        {
            if (ptr->Parent())
            {
                ptr->Parent()->OnChildDestroyed(ptr->IDD(), ptr->GetExitCode());
            }
            else
            {
                DestroyHUD(ptr->GetExitCode());
            }
        }
    }
}

bool Display::DoKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        return ptr->GetMsgBox()->OnKeyDown(nChar, nRepCnt, nFlags);
    }
    else
    {
        return ptr->OnKeyDown(nChar, nRepCnt, nFlags);
    }
}

bool Display::DoControllerUiAction(Poseidon::ControllerUiAction action)
{
    Poseidon::ControllerMenuUiLayout layout;
    const Poseidon::ControllerUiDispatch dispatch = layout.Map(action);

    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (dispatch.kind == Poseidon::ControllerUiDispatchKind::KeyTap)
    {
        if (ptr->GetMsgBox())
        {
            bool handled = ptr->GetMsgBox()->OnKeyDown(dispatch.key, 1, 0);
            ptr->GetMsgBox()->OnKeyUp(dispatch.key, 1, 0);
            return handled;
        }
        bool handled = ptr->OnKeyDown(dispatch.key, 1, 0);
        ptr->OnKeyUp(dispatch.key, 1, 0);
        return handled;
    }
    return false;
}

Poseidon::ControllerUiScene Display::GetControllerUiScene() const
{
    Poseidon::ControllerUiScene scene = Poseidon::MenuControllerScene();
    switch (IDD())
    {
        case IDD_MAIN:
            scene.sceneCapabilities |= Poseidon::CtrlPause;
            break;
        case IDD_MULTIPLAYER:
        case IDD_SERVER:
        case IDD_CLIENT:
        case IDD_SERVER_SETUP:
        case IDD_CLIENT_SETUP:
        case IDD_MP_SETUP:
        case IDD_JOIN_REQUIREMENTS:
            scene.kind = Poseidon::ControllerSceneKind::Multiplayer;
            scene.activeSection = Poseidon::PagerSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            if (IDD() == IDD_MULTIPLAYER)
                scene.sceneCapabilities |= Poseidon::CtrlPreview | Poseidon::CtrlDelete | Poseidon::CtrlEditorMode;
            break;
        case IDD_FILTER:
        case IDD_PASSWORD:
        case IDD_IP_ADDRESS:
        case IDD_PORT:
        case IDD_MODS_FILTER:
        case IDD_LOGIN:
        case IDD_NEW_USER:
            scene.kind = Poseidon::ControllerSceneKind::TextEntry;
            scene.activeSection = Poseidon::TextFieldSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            break;
        case IDD_REVERT:
            scene.kind = Poseidon::ControllerSceneKind::Modal;
            scene.activeSection = Poseidon::ModalButtonsSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            break;
        case IDD_MODS:
            scene.kind = Poseidon::ControllerSceneKind::ModsCatalog;
            scene.activeSection = Poseidon::PagerSection();
            scene.sceneCapabilities = scene.activeSection.capabilities | Poseidon::CtrlPreview | Poseidon::CtrlDelete |
                                      Poseidon::CtrlEditorMode;
            break;
        case IDD_MODS_DOWNLOAD:
            scene.kind = Poseidon::ControllerSceneKind::DownloadModal;
            scene.activeSection = Poseidon::ModalButtonsSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            break;
        case IDD_MAIN_MAP:
            scene.kind = Poseidon::ControllerSceneKind::Map;
            scene.activeSection = Poseidon::PagerSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            break;
        case IDD_SINGLE_MISSION:
        case IDD_INTEL_GETREADY:
        case IDD_SERVER_GET_READY:
        case IDD_CLIENT_GET_READY:
        case IDD_DEBRIEFING:
            scene.kind = Poseidon::ControllerSceneKind::Notebook;
            scene.activeSection = Poseidon::PagerSection();
            scene.sceneCapabilities = scene.activeSection.capabilities;
            break;
        case IDD_CHAT:
        case IDD_VOICE_CHAT:
            scene.kind = Poseidon::ControllerSceneKind::Chat;
            break;
        default:
            break;
    }
    return scene;
}

bool Display::DoKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        return ptr->GetMsgBox()->OnKeyUp(nChar, nRepCnt, nFlags);
    }
    else
    {
        return ptr->OnKeyUp(nChar, nRepCnt, nFlags);
    }
}

bool Display::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (nChar < 32)
    {
        return false;
    }

    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        return ptr->GetMsgBox()->OnChar(nChar, nRepCnt, nFlags);
    }
    else
    {
        return ptr->OnChar(nChar, nRepCnt, nFlags);
    }
}

bool Display::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        return ptr->GetMsgBox()->OnIMEChar(nChar, nRepCnt, nFlags);
    }
    else
    {
        return ptr->OnIMEChar(nChar, nRepCnt, nFlags);
    }
}

bool Display::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
    ControlsContainer* ptr = this;
    while (ptr->Child())
    {
        ptr = ptr->Child();
    }

    if (ptr->GetMsgBox())
    {
        return ptr->GetMsgBox()->OnIMEComposition(nChar, nFlags);
    }
    else
    {
        return ptr->OnIMEComposition(nChar, nFlags);
    }
}

bool Display::DoUnregisteredAddonUsed(RString addon)
{
    ControlsContainer* ptr = this;
    while (ptr)
    {
        bool done = ptr->OnUnregisteredAddonUsed(addon);
        if (done)
        {
            return true;
        }
        ptr = ptr->Child();
    }
    return false;
}

bool Display::IsSimulationEnabled() const
{
    const ControlsContainer* ptr = this;
    while (ptr->Child() != nullptr)
    {
        ptr = ptr->Child();
    }

    return ptr->SimulationEnabled();
}

bool Display::IsDisplayEnabled() const
{
    const ControlsContainer* ptr = this;
    while (ptr->Child() != nullptr)
    {
        ptr = ptr->Child();
    }

    return ptr->DisplayEnabled();
}

bool Display::IsUIEnabled() const
{
    const ControlsContainer* ptr = this;
    while (ptr->Child() != nullptr)
    {
        ptr = ptr->Child();
    }

    return ptr->UIEnabled();
}

MsgBox::MsgBox(ControlsContainer* parent, int flags, RString text, int idd) : ControlsContainer(parent)
{
    _idd = idd;

    const ParamEntry& cls = Res >> "RscMsgBox";

    Ref<Font> font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "Text" >> "font"));
    const ParamEntry* entry = (cls >> "Text").FindEntry("size");
    float size;
    if (entry)
    {
        size = (float)(*entry) * font->Height();
    }
    else
    {
        size = cls >> "Text" >> "sizeEx";
    }

    const float wBorder = 0.02;
    const float hBorder = 0.02;
    float wText = GLOB_ENGINE->GetTextWidth(size, font, text);
    int nLines = toIntCeil(wText * (1.0 / 0.5));
    saturateMin(wText, 0.6 - 2 * wBorder);
    wText += 2 * wBorder;

    float hText = size * nLines;
    float wGroup = wText + 2 * wBorder;
    float hGroup = hText + 2 * hBorder;
    float wButton = cls >> "Button" >> "w";
    float hButton = cls >> "Button" >> "h";

    int nButtons = 0;
    if (flags & MB_BUTTON_OK)
    {
        nButtons++;
    }
    if (flags & MB_BUTTON_CANCEL)
    {
        nButtons++;
    }

    float w, h, wButtons = 0;
    if (nButtons > 0)
    {
        wButtons = nButtons * wButton + (nButtons - 1) * wBorder;
        saturateMax(wGroup, wButtons);
        wButton = (wGroup - (nButtons - 1) * wBorder) / nButtons;
        w = wGroup + 2 * wBorder;
        h = hGroup + hButton + 2.75 * hBorder;
    }
    else
    {
        w = wGroup + 2 * wBorder;
        h = hGroup + 2 * hBorder;
    }
    float x = 0.5 - 0.5 * w;
    float y = 0.5 - 0.5 * h;

    float top = y;
    _controlsForeground.Add(new CStatic(this, cls >> "Background", x, top, w, h, ""));
    top += hBorder;

    _controlsForeground.Add(new CStatic(this, cls >> "SubBackground", 0.5 - 0.5 * wGroup, top, wGroup, hGroup, ""));
    top += hBorder;

    CStatic* staticText = new CStatic(this, cls >> "Text", 0.5 - 0.5 * wText, top, wText, hText, text);
    _controlsForeground.Add(staticText);
    staticText->EnableCtrl(false);

    top = y + h - hButton - hBorder;
    float left = 0.5 - 0.5 * wGroup;
    if (flags & MB_BUTTON_OK)
    {
        _controlsForeground.Add(new CButton(this, cls >> "ButtonOK", left, top, wButton, hButton));
        left += wButton + wBorder;
    }
    if (flags & MB_BUTTON_CANCEL)
    {
        _controlsForeground.Add(new CButton(this, cls >> "ButtonCancel", left, top, wButton, hButton));
        left += wButton + wBorder;
    }

    NextCtrl();
}

ControlsContainer* CreateWarningMessageBox(RString text)
{
    return new MsgBox(nullptr, MB_BUTTON_OK, text, -1);
}

namespace Poseidon
{
}
