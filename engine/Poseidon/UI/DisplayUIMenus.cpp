#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/WorldChatInput.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <filesystem>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string>
#include <vector>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <sys/stat.h>

// Defined at global scope: GetPublicKey in Network/MultiplayerAuth.cpp,
// ProcessMouse in Input/InputDispatch.cpp.
RString GetPublicKey();
void ProcessMouse(DWORD timeDelta = 1);

namespace Poseidon
{
RString FindShape(RString name);
void PositionToAA11(Vector3Val pos, char* buffer);
} // namespace Poseidon

namespace Poseidon
{
using Poseidon::Foundation::UITime;

namespace
{
const char* kNewUserDefaultNameKey = "STR_DISP_NEW_USER_DEFAULT_NAME";
}

// Login display

Control* DisplayLogin::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_LOGIN_USER)
    {
        //		CCombo *combo = new CCombo(this, idc, cls);
        C3DListBox* ctrl = new C3DListBox(this, idc, cls);

        auto profiles = ProfileManager::EnumerateProfiles(GamePaths::Instance().UserDir());
        for (const auto& profile : profiles)
        {
            ctrl->AddString(profile.name.c_str());
        }
        ctrl->SortItems();
        ctrl->SetCurSel(0);
        for (int i = 0; i < ctrl->GetSize(); i++)
        {
            if (Glob.header.playerName == ctrl->GetText(i))
            {
                ctrl->SetCurSel(i);
            }
        }
        return ctrl;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayLogin::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_LOGIN_NEW:
        case IDC_LOGIN_EDIT:
            Exit(idc);
            break;
        case IDC_OK:
        case IDC_CANCEL:
        {
            _exit = idc;
            bool canDestroy = CanDestroy();
            _exit = -1;
            if (!canDestroy)
            {
                break;
            }
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_LOGIN_NOTEBOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_LOGIN_DELETE:
            CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE), IDD_MSG_DELETEPLAYER);
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayLogin::OnCtrlClosed(int idc)
{
    if (idc == IDC_LOGIN_NOTEBOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

void DisplayLogin::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_MSG_DELETEPLAYER:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_OK)
            {
                C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_LOGIN_USER));
                int index = ctrl->GetCurSel();
                if (index >= 0)
                {
                    const char* name = ctrl->GetText(index);
                    ProfileManager::DeleteProfile(GamePaths::Instance().UserDir(), name);
                    ctrl->DeleteString(index);
                    ctrl->SetCurSel(0);
                }
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

bool DisplayLogin::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }

    if (_exit == IDC_OK)
    {
        C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_LOGIN_USER));
        if (ctrl->GetSize() == 0 || ctrl->GetCurSel() < 0)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_EMPTY));
            return false;
        }
    }

    return true;
}

// New user display

CHead::CHead(ControlsContainer* parent, int idc, const ParamEntry& cls) : ControlObject(parent, idc, cls)
{
    _lastSimulation = Glob.uiTime;
    _woman = false;

    _manShape = _shape;
    _manHeadType = new HeadType();
    _manHeadType->Load(Pars >> "CfgMimics" >> "HeadPreview");
    _manHeadType->InitShape(cls, _shape);
    _manHead = new Head(*_manHeadType, _shape);
    _womanShape = Shapes.New(Poseidon::FindShape(cls >> "modelWoman"), false, false);
    _womanHeadType = new HeadType();
    _womanHeadType->Load(Pars >> "CfgMimics" >> "HeadPreview");
    _womanHeadType->InitShape(cls, _womanShape);
    _womanHead = new Head(*_womanHeadType, _womanShape);
}

void CHead::OnDraw(float alpha)
{
    LODShapeWithShadow* oldShape = GetShape();
    SetShape(GetHeadShape());
    ControlObject::OnDraw(alpha);
    SetShape(oldShape);
}

void CHead::Animate(int level)
{
    GetHead()->Animate(*GetHeadType(), GetHeadShape(), level, false, M3Identity, false);
}

void CHead::Deanimate(int level)
{
    GetHead()->Deanimate(*GetHeadType(), GetHeadShape(), level, false, M3Identity, false);
}

void CHead::Simulate()
{
    UITime time = Glob.uiTime;
    const float t0 = 4.0;
    float oldT = fastFmod(_lastSimulation.toFloat(), t0);
    float newT = fastFmod(time.toFloat(), t0);
    float angle = (H_PI * 2.0 / t0) * newT;
    Matrix3 orient(MRotationY, angle);
    float scale = Scale();
    SetOrientation(orient);
    SetScale(scale);

    const float tChange = 0.5 * t0;
    if (oldT < tChange && newT >= tChange)
    {
        int size = (Pars >> "CfgMimics" >> "States").GetEntryCount();
        int i = toIntFloor(size * GRandGen.RandomValue());
        GetHead()->SetMimic((Pars >> "CfgMimics" >> "States").GetEntry(i).GetName());
    }

    GetHead()->Simulate(*GetHeadType(), time - _lastSimulation, SimulateVisibleNear, false);
    _lastSimulation = time;
}

void CHead::SetFace(RString name)
{
    const ParamEntry* cls = (Pars >> "CfgFaces").FindEntry(name);
    if (!cls)
    {
        cls = (Pars >> "CfgFaces").FindEntry("Default");
        //		WarningMessage("Unknown face: %s", (const char *)name);
        if (!cls)
        {
            return;
        }
    }

    _woman = false;
    if (cls->FindEntry("woman"))
    {
        _woman = *cls >> "woman";
    }

    GetHead()->SetFace(*GetHeadType(), _woman, GetHeadShape(), name);
}

void CHead::SetGlasses(RString name)
{
    GetHead()->SetGlasses(*GetHeadType(), GetHeadShape(), name);
}

void CHead::AttachWave(IWave* wave, float freq)
{
    GetHead()->AttachWave(wave, freq);
}

DisplayNewUser::DisplayNewUser(ControlsContainer* parent, RString name, bool edit) : Display(parent)
{
    _edit = edit;
    if (edit)
    {
        _name = name;
        _face = Glob.header.playerFace;
        _glasses = Glob.header.playerGlasses;
        _speaker = Glob.header.playerSpeaker;
        _pitch = Glob.header.playerPitch;

        RString filename =
            ProfileManager::GetProfileCfgPath(GamePaths::Instance().UserDir(), std::string(name)).c_str();
        if (QIFStream::FileExists(filename))
        {
            ParamFile cfg;
            cfg.Parse(filename);
            const ParamEntry* identity = cfg.FindEntry("Identity");
            if (identity)
            {
                _face = (*identity) >> "face";
                if (identity->FindEntry("glasses"))
                {
                    _glasses = (*identity) >> "glasses";
                }
                _speaker = (*identity) >> "speaker";
                _pitch = (*identity) >> "pitch";
                if (identity->FindEntry("squad"))
                {
                    _squad = (*identity) >> "squad";
                }
            }
        }
    }
    else
    {
        _name = LocalizeString(kNewUserDefaultNameKey);
        _face = "Default";
        _glasses = "None";
        _speaker = (Pars >> "CfgVoice" >> "voices")[0];
        _pitch = 1.0;
    }

    _doPreview = false;

    Load("RscDisplayNewUser");
    _head->SetFace(_face);
    _head->SetGlasses(_glasses);

    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplayNewUser::~DisplayNewUser()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

Control* DisplayNewUser::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_NEW_USER_TITLE:
        {
            CStatic* text = new CStatic(this, idc, cls);
            if (_edit)
            {
                text->SetText(LocalizeString(IDS_NEWUSER_TITLE2));
            }
            else
            {
                text->SetText(LocalizeString(IDS_NEWUSER_TITLE1));
            }
            return text;
        }
        case IDC_NEW_USER_ID:
        {
            C3DStatic* text = new C3DStatic(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_PLAYER_ID), (const char*)::GetPublicKey());
            text->SetText(buffer);
            return text;
        }
        case IDC_NEW_USER_NAME:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            edit->SetMaxChars(24);
            edit->SetText(_name);
            //			edit->EnableCtrl(!_edit);
            return edit;
        }
        case IDC_NEW_USER_FACE:
        {
            C3DListBox* ctrl = new C3DListBox(this, idc, cls);
            int sel = 0;
            const ParamEntry& list = Pars >> "CfgFaces";
            for (int i = 0; i < list.GetEntryCount(); i++)
            {
                const ParamEntry& entry = list.GetEntry(i);
                if (entry.FindEntry("disabled"))
                {
                    continue;
                }
                /*
                                bool woman = false;
                                if (entry.FindEntry("woman")) woman = entry >> "woman";
                                if (woman) continue;
                */
                RString name = entry.GetName();
                int index = ctrl->AddString(entry >> "name");
                ctrl->SetData(index, name);
                if (name == _face)
                {
                    sel = index;
                }
            }
            ctrl->SetCurSel(sel);
            return ctrl;
        }
        case IDC_NEW_USER_GLASSES:
        {
            C3DListBox* ctrl = new C3DListBox(this, idc, cls);
            int sel = 0;
            const ParamEntry& list = Pars >> "CfgGlasses";
            for (int i = 0; i < list.GetEntryCount(); i++)
            {
                const ParamEntry& entry = list.GetEntry(i);
                RString name = entry.GetName();
                int index = ctrl->AddString(entry >> "name");
                ctrl->SetData(index, name);
                if (name == _glasses)
                {
                    sel = index;
                }
            }
            ctrl->SetCurSel(sel);
            return ctrl;
        }
        case IDC_NEW_USER_SPEAKER:
        {
            C3DListBox* ctrl = new C3DListBox(this, idc, cls);
            int sel = 0;
            const ParamEntry& list = Pars >> "CfgVoice";
            const ParamEntry& array = list >> "voices";
            for (int i = 0; i < array.GetSize(); i++)
            {
                RString name = array[i];
                const ParamEntry& entry = list >> name;
                int index = ctrl->AddString(entry >> "name");
                ctrl->SetData(index, name);
                if (name == _speaker)
                {
                    sel = index;
                }
            }
            ctrl->SetCurSel(sel);
            return ctrl;
        }
        case IDC_NEW_USER_PITCH:
        {
            C3DSlider* ctrl = new C3DSlider(this, idc, cls);
            ctrl->SetRange(0.8, 1.2);
            ctrl->SetThumbPos(_pitch);
            ctrl->SetSpeed(0.02, 0.1);
            return ctrl;
        }
        case IDC_NEW_USER_SQUAD:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            edit->SetText(_squad);
            return edit;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

ControlObject* DisplayNewUser::OnCreateObject(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_NEW_USER_HEAD)
    {
        _head = new CHead(this, idc, cls);
        return _head;
    }
    else
    {
        return Display::OnCreateObject(type, idc, cls);
    }
}

void DisplayNewUser::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        case IDC_CANCEL:
        {
            _exit = idc;
            bool canDestroy = CanDestroy();
            _exit = -1;
            if (!canDestroy)
            {
                break;
            }
            _exitWhenClose = idc;
            _head->ShowCtrl(false);
            ControlObjectContainerAnim* ctrl =
                dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_NEW_USER_NOTEBOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayNewUser::OnSliderPosChanged(int idc, float pos)
{
    if (idc == IDC_NEW_USER_PITCH)
    {
        C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_NEW_USER_SPEAKER));
        if (ctrl)
        {
            int sel = ctrl->GetCurSel();
            if (sel >= 0)
            {
                _previewSpeaker = ctrl->GetData(sel);
                _previewPitch = pos;
                _doPreview = true;
                _previewTime = GlobalTickCount() + 200;
            }
        }
    }
    else
    {
        Display::OnSliderPosChanged(idc, pos);
    }
}

void DisplayNewUser::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_NEW_USER_SPEAKER)
    {
        if (curSel >= 0)
        {
            C3DSlider* slider = dynamic_cast<C3DSlider*>(GetCtrl(IDC_NEW_USER_PITCH));
            C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_NEW_USER_SPEAKER));
            if (slider && ctrl)
            {
                _previewSpeaker = ctrl->GetData(curSel);
                _previewPitch = slider->GetThumbPos();
                _doPreview = true;
                _previewTime = GlobalTickCount() + 200;
            }
        }
    }
    else if (idc == IDC_NEW_USER_FACE)
    {
        C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_NEW_USER_FACE));
        if (ctrl && _head && curSel >= 0)
        {
            _head->SetFace(ctrl->GetData(curSel));
        }
    }
    else if (idc == IDC_NEW_USER_GLASSES)
    {
        C3DListBox* ctrl = dynamic_cast<C3DListBox*>(GetCtrl(IDC_NEW_USER_GLASSES));
        if (ctrl && _head && curSel >= 0)
        {
            _head->SetGlasses(ctrl->GetData(curSel));
        }
    }
    else
    {
        Display::OnSliderPosChanged(idc, curSel);
    }
}

void DisplayNewUser::OnObjectMoved(int idc, Vector3Par offset)
{
    if (idc == IDC_NEW_USER_NOTEBOOK)
    {
        PoseidonAssert(dynamic_cast<Control3D*>(GetCtrl(IDC_NEW_USER_HEAD_AREA)));
        Control3D* area = static_cast<Control3D*>(GetCtrl(IDC_NEW_USER_HEAD_AREA));
        Vector3 pos = area->GetCenter();
        float coef = _head->Position().Z() / pos.Z();
        _head->SetPosition(_head->Position() + coef * offset);
    }
    Display::OnObjectMoved(idc, offset);
}

void DisplayNewUser::OnCtrlClosed(int idc)
{
    if (idc == IDC_NEW_USER_NOTEBOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

void DisplayNewUser::OnSimulate(EntityAI* vehicle)
{
    if (_head)
    {
        _head->Simulate();
    }

    Display::OnSimulate(vehicle);

    if (_previewSpeech && _previewSpeech->IsTerminated())
    {
        _previewSpeech = nullptr;
    }
    if (!_previewSpeech && _doPreview && GlobalTickCount() >= _previewTime)
    {
        Preview(_previewSpeaker, _previewPitch);
        _doPreview = false;
    }
}

void DisplayNewUser::Preview(RString speaker, float pitch)
{
    int index = ENGINE_CONFIG.singleVoice ? 1 : 0;
    RString dir = (Pars >> "CfgVoice" >> speaker >> "directories")[index];
    RString word = Pars >> "CfgVoice" >> "preview";
    RString name = RString("voice\\") + dir + word + RString(".wss");

    _previewSpeech = GSoundScene->OpenAndPlayOnce2D(name, 1, pitch, false);
    if (_previewSpeech)
    {
        _previewSpeech->SetKind(WaveSpeech);
        _head->AttachWave(_previewSpeech, pitch);
    }
}

bool DisplayNewUser::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }

    if (_exit == IDC_OK)
    {
        C3DEdit* edit = dynamic_cast<C3DEdit*>(GetCtrl(IDC_NEW_USER_NAME));
        const char* name = edit->GetText();
        if (!name || !*name)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_EMPTY));
            return false;
        }
        if (strcspn(name, "\\/:*?\"<>|") < strlen(name))
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_INVALID));
            return false;
        }

        if (!_edit || stricmp(name, _name) != 0)
        {
            std::string profileDir = ProfileManager::GetProfileDirPath(GamePaths::Instance().UserDir(), name);
            if (std::filesystem::is_directory(profileDir))
            {
                // player already exist
                CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYER_EXIST));
                return false;
            }
        }
    }

    return true;
}

void DisplayNewUser::RefreshLanguage()
{
    if (auto* title = dynamic_cast<CStatic*>(GetCtrl(IDC_NEW_USER_TITLE)))
    {
        title->SetText(LocalizeString(_edit ? IDS_NEWUSER_TITLE2 : IDS_NEWUSER_TITLE1));
    }

    if (auto* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_NEW_USER_ID)))
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_PLAYER_ID), (const char*)::GetPublicKey());
        text->SetText(buffer);
    }

    if (auto* edit = dynamic_cast<C3DEdit*>(GetCtrl(IDC_NEW_USER_NAME)))
    {
        edit->SetMaxChars(24);
    }
}

// Mission, intro, outro displays

bool BreakIntro()
{
    auto& input = InputSubsystem::Instance();
    if (input.IsKeyPressed(SDL_SCANCODE_ESCAPE))
    {
        input.ConsumeKeyPress(SDL_SCANCODE_ESCAPE);
        return true;
    }
    if (input.IsKeyPressed(SDL_SCANCODE_SPACE))
    {
        input.ConsumeKeyPress(SDL_SCANCODE_SPACE);
        return true;
    }
    return false;
}

class DisplayHintC : public Display
{
  protected:
    CStatic* _background;
    CStatic* _hint;
    CActiveText* _button;

  public:
    DisplayHintC(ControlsContainer* parent, RString hint);
    void OnSimulate(EntityAI* vehicle) override;
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    RString GetHint() { return _hint->GetText(); }
};

DisplayHintC::DisplayHintC(ControlsContainer* parent, RString hint) : Display(parent)
{
    _enableSimulation = false;
    Load(Res >> "RscDisplayHintC");

    _hint->SetText(hint);
    float h = _hint->GetTextHeight();
    float offset = _button->Y() - _background->Y() - _background->H();
    float dh = _background->H() - _hint->H();
    _hint->SetPos(_hint->X(), _hint->Y(), _hint->W(), h);
    _background->SetPos(_background->X(), _background->Y(), _background->W(), h + dh);
    _button->SetPos(_button->X(), _background->Y() + _background->H() + offset, _button->W(), _button->H());
}

Control* DisplayHintC::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_HINTC_BG:
            _background = new CStatic(this, idc, cls);
            return _background;
        case IDC_HINTC_HINT:
            _hint = new CStatic(this, idc, cls);
            _hint->EnableCtrl(false);
            return _hint;
        case IDC_CANCEL:
            _button = new CActiveText(this, idc, cls);
            return _button;
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayHintC::OnSimulate(EntityAI* vehicle)
{
    if (InputSubsystem::Instance().IsKeyPressed(SDL_SCANCODE_SPACE))
    {
        InputSubsystem::Instance().IsKeyPressed(SDL_SCANCODE_SPACE);
        Exit(IDC_CANCEL);
    }

    Display::OnSimulate(vehicle);
}

DisplayMission::DisplayMission(ControlsContainer* parent, bool editor, bool erase, bool load) : Display(parent)
{
    _editor = editor;
    Load("RscDisplayMission");
    _compass->ShowCtrl(false);
    _watch->ShowCtrl(false);

    SetCursor(nullptr);

    // Flush stale mouse input accumulated during menus/loading
    ::ProcessMouse();
    auto& input = InputSubsystem::Instance();
    input.FlushAndResetMouse();

    InitUI();
    GStats.OnMissionStart();

    if (erase)
    {
        RString dir = GetSaveDirectory();

        RString name = dir + RString("autosave.fps");
        unlink(name);

        name = dir + RString("save.fps");
        unlink(name);
    }

    if (IsCampaign() && !load)
    {
        RString displayName = Localize(CurrentTemplate.intel.briefingName);
        if (displayName.GetLength() == 0)
        {
            displayName = RString(Glob.header.filename) + RString(".") + RString(Glob.header.worldname);
        }
        void AddMission(RString campaign, RString battle, RString mission, RString displayName);
        AddMission(CurrentCampaign, CurrentBattle, CurrentMission, displayName);
    }

    ContinueSaved = editor || GWorld->GetMode() == GModeNetware;

    GWorld->ForceEnd(false);

    ShowCinemaBorder(true);
}

ControlObject* DisplayMission::OnCreateObject(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MAP_COMPASS:
            _compass = new Compass(this, idc, cls);
            return _compass;
        case IDC_MAP_WATCH:
            _watch = new Watch(this, idc, cls);
            return _watch;
        default:
            return Display::OnCreateObject(type, idc, cls);
    }
}

void DisplayMission::ShowHint(RString hint)
{
    CreateChild(new DisplayHintC(this, hint));
}

void DisplayMission::InitUI()
{
    // main map
    GWorld->CreateMainMap();
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        const ParamEntry* cls = ExtParsMission.FindEntry("showMap");
        map->ShowMap(!cls ? true : (*cls));
        cls = ExtParsMission.FindEntry("showWatch");
        map->ShowWatch(!cls ? true : (*cls));
        cls = ExtParsMission.FindEntry("showCompass");
        map->ShowCompass(!cls ? true : (*cls));
        cls = ExtParsMission.FindEntry("showNotepad");
        map->ShowNotepad(!cls ? true : (*cls));
        cls = ExtParsMission.FindEntry("showGPS");
        map->ShowGPS(!cls ? false : (*cls));
    }

    // in game UI
    AbstractUI* ui = GWorld->UI();
    if (ui)
    {
        const ParamEntry* cls = ExtParsMission.FindEntry("showHUD");
        bool show = !cls ? true : (*cls);
        ui->ShowAll(show);
    }

    // chat
    // Do NOT clear here: a JIP client may have received in-mission chat while
    // loading, and those messages should be visible when the mission starts.
    // The destructor clears on exit, so a fresh mission always begins empty.
    GChatList.SetRect(0.05, 0.68, 0.7, 0.02);
    GChatList.SetRows(6);
}

static void SaveContinue()
{
    if (!ContinueSaved && GWorld->GetRealPlayer() && !GWorld->GetRealPlayer()->IsDammageDestroyed())
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%scontinue.fps", (const char*)GetSaveDirectory());
        GWorld->SaveBin(buffer, IDS_AUTOSAVE_GAME);
    }
    ContinueSaved = true;
}

DisplayMission::~DisplayMission()
{
    GChatList.Clear();
    GChatList.SetRect(0.05, 0.02, 0.9, 0.02);
    GChatList.SetRows(4);
    // SaveContinue();
    if (GWorld)
    {
        GWorld->UnloadSounds();
        // FIX
        GWorld->DestroyUserDialog();
    }
}

void DisplayMission::OnSimulate(EntityAI* vehicle)
{
#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_C))
    {
        CreateChild(new DisplayDebug(this, GWorld->GetGameState()));
        return;
    }
    /*
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_V))
    {
        CreateChild(new DisplayDebug(this,GWorld->GetGameState(),"RscDisplayDiag"));
        return;
    }
    */
#endif

    _compass->ShowCtrl(GWorld->HasCompass());
    _watch->ShowCtrl(GWorld->HasWatch());

    if (AppConfig::Instance().IsMissionSmokeCheck() && !_missionSmokeReported)
    {
        std::string missionName =
            std::string((const char*)Glob.header.filename) + "." + (const char*)Glob.header.worldname;
        LOG_INFO(Core, "Mission smoke check: mission runtime entered ({})", missionName);
        LOG_INFO(Core, "AUTO-TEST SUCCESS");
        GApp->m_exitCode = 0;
        GApp->m_closeRequest = true;
        _missionSmokeReported = true;
        return;
    }

    if (GWorld->GetMode() == GModeNetware)
    {
        NetworkGameState gameState = GetNetworkManager().GetGameState();
        if (gameState < NGSPlay)
        {
            LOG_WARN(Network, "[MP] DisplayMission: gameState={} < NGSPlay, exiting! serverState={}", (int)gameState,
                     (int)GetNetworkManager().GetServerState());
            // end mission
            //			GStats.Update();
            GWorld->DestroyMap(IDC_OK);
            //			GWorld->EnableSimulation(false);
            Exit(IDC_CANCEL);
            return;
        }

        if (Poseidon::ShouldHandleMultiplayerChatShortcut(GWorld->GetChat() != nullptr) &&
            InputSubsystem::Instance().GetActionToDo(UANetworkPlayers))
        {
            CreateChild(new DisplayMPPlayers(this));
        }
    }
    else
    {
        if (GWorld->GetEndMode() == EMKilled && GWorld->CameraOn())
        {
            if (GWorld->IsEndDialogEnabled())
            {
                if (AutoTest)
                {
                    GWorld->DestroyMap(IDC_OK);
                    Exit(IDC_MAIN_QUIT);
                }
                else
                {
                    CreateChild(new DisplayMissionEnd(this));
                }
            }
            return;
        }
    }

    if (GWorld->GetEndMode() != EMContinue && GWorld->GetEndMode() != EMKilled &&
        ((!GWorld->GetCameraEffect() && !GWorld->GetTitleEffect()) || GWorld->IsEndForced()))
    {
        RString exitScript = GetMissionDirectory() + RString("exit.sqs");
        if (QIFStreamB::FileExist(exitScript))
        {
            float end = GWorld->GetEndMode() - EMLoser;
            Script* script = new Script("exit.sqs", GameValue(end), INT_MAX);
            GWorld->AddScript(script);
            GWorld->SimulateScripts();
        }

        // end mission
        //		GStats.Update();
        GWorld->DestroyMap(IDC_OK);
        if (AutoTest)
        {
            Exit(IDC_MAIN_QUIT);
        }
        else
        {
            Exit(IDC_CANCEL);
        }
    }
    else if (InputSubsystem::Instance().GetKeyToDo(SDL_SCANCODE_ESCAPE))
    {
        OpenInterrupt();
    }
}

void DisplayMission::OpenInterrupt()
{
    if (!Child() && !GetMsgBox())
        CreateChild(new DisplayInterrupt(this));
}

bool DisplayMission::DoControllerUiAction(ControllerUiAction action)
{
    ControllerInGameUiLayout layout;
    const ControllerUiDispatch dispatch = layout.Map(action);
    if (dispatch.kind == ControllerUiDispatchKind::Pause)
    {
        OpenInterrupt();
        return true;
    }
    return Display::DoControllerUiAction(action);
}

ControllerUiScene DisplayMission::GetControllerUiScene() const
{
    return Child() || _msgBox ? PauseMenuControllerScene() : GameplayControllerScene();
}

bool DisplayMission::RetryMission()
{
    if (GStats._mission._lives == 0)
    {
        return false;
    }

    // retry mission
    RString name = GetSaveDirectory() + RString("autosave.fps");
    if (QIFStream::FileExists(name))
    {
        GWorld->LoadBin(name, IDS_LOAD_GAME);
        if (GStats._mission._lives > 0)
        {
            GStats._mission._lives--;
        }
    }
    else
    {
        GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));
        GStats.ClearMission();
        // if (IsCampaign())
        {
            // load variables
            GameState* gstate = GWorld->GetGameState();
            AutoArray<GameVariable>& variables = GStats._campaign._variables;
            for (int i = 0; i < variables.Size(); i++)
            {
                GameVariable& var = variables[i];
                gstate->VarSet(var._name, var._value, var._readOnly);
            }
        }
        GWorld->ActivateAddons(CurrentTemplate.addOns);
        GWorld->InitGeneral(CurrentTemplate.intel);
        GWorld->InitVehicles(GModeArcade, CurrentTemplate);
        int lives = GStats._mission._lives;
        if (GStats._mission._lives > 0)
        {
            GStats._mission._lives = lives - 1;
        }

        RString weapons = GetSaveDirectory() + RString("weapons.cfg");
        if (QIFStream::FileExists(weapons))
        {
            WeaponsInfo info;
            info.Load(weapons);
            info.Apply();
        }
    }

    GWorld->DestroyMap(IDC_OK);
    InitUI();
    GStats.OnMissionStart();

    // FIX
    GWorld->DestroyUserDialog();

    return true;
}

void DisplayMission::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_MISSION_END:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_ME_RETRY)
            {
                if (!RetryMission())
                {
                    // end mission
                    //				GStats.Update();
                    GWorld->DestroyMap(IDC_OK);
                    Exit(IDC_CANCEL);
                }
            }
            else if (exit == IDC_ME_LOAD)
            {
                RString name = GetSaveDirectory() + RString("save.fps");
                GWorld->LoadBin(name, IDS_LOAD_GAME);
                GStats.OnMissionStart();
            }
            else if (exit == IDC_CANCEL)
            {
                // end mission
                GWorld->DestroyMap(IDC_OK);
                Exit(IDC_MAIN_QUIT);
            }
            break;
        case IDD_INTERRUPT:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_INT_ABORT)
            {
                // abort mission
                SaveContinue();
                GWorld->DestroyMap(IDC_OK);
                Exit(IDC_MAIN_QUIT);
            }
            else if (exit == IDC_INT_RETRY)
            {
                if (!RetryMission())
                {
                    // abort mission
                    SaveContinue();
                    GWorld->DestroyMap(IDC_OK);
                    Exit(IDC_MAIN_QUIT);
                }
            }
            else if (exit == IDC_INT_SAVE)
            {
                RString name = GetSaveDirectory() + RString("save.fps");
                GWorld->SaveBin(name, IDS_SAVE_GAME);
            }
            else if (exit == IDC_INT_LOAD)
            {
                // FIX
                GWorld->DestroyUserDialog();

                RString name = GetSaveDirectory() + RString("save.fps");
                GWorld->LoadBin(name, IDS_LOAD_GAME);
                GStats.OnMissionStart();
            }
            break;
        case IDD_HINTC:
        {
            DisplayHintC* child = static_cast<DisplayHintC*>(_child.GetRef());
            GWorld->UI()->ShowHint(child->GetHint());
        }
            Display::OnChildDestroyed(idd, exit);
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

DisplayCutscene::DisplayCutscene(ControlsContainer* parent) : Display(parent)
{
    SetCursor(nullptr);
    //	GWorld->EnableSimulation(true);
    ShowCinemaBorder(true);
}

DisplayCutscene::~DisplayCutscene()
{
    GWorld->UnloadSounds();
}

void DisplayCutscene::OnSimulate(EntityAI* vehicle)
{
#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_C))
    {
        CreateChild(new DisplayDebug(this, GWorld->GetGameState()));
        return;
    }
    /*
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_V))
    {
        CreateChild(new DisplayDebug(this,GWorld->GetGameState(),"RscDisplayDiag"));
        return;
    }
    */
#endif

    if (BreakIntro() || GWorld->GetEndMode() != EMContinue /* || !GLOB_WORLD->GetCameraEffect() */)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
    }
}

void DisplayIntro::OnSimulate(EntityAI* vehicle)
{
    DisplayCutscene::OnSimulate(vehicle);

    if (AppConfig::Instance().IsMissionSmokeCheck() && !_missionSmokeReported)
    {
        _missionSmokeReported = true;
        LOG_INFO(Core, "Mission smoke check: intro runtime entered ({})",
                 Glob.header.filenameReal.GetLength() > 0 ? (const char*)Glob.header.filenameReal
                                                          : Glob.header.filename);
        LOG_INFO(Core, "AUTO-TEST SUCCESS");
        GApp->m_exitCode = 0;
        GApp->m_closeRequest = true;
    }
}

DisplayIntro::DisplayIntro(ControlsContainer* parent, DisplayIntro::NoInit) : DisplayCutscene(parent)
{
    Load("RscDisplayIntro");

    Init();
}

DisplayIntro::DisplayIntro(ControlsContainer* parent) : DisplayCutscene(parent)
{
    Load("RscDisplayIntro");

    ParseIntro();

    if (CurrentTemplate.groups.Size() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
    if (!GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate))
    {
        Exit(IDC_CANCEL);
        return;
    }

    Init();
}

DisplayIntro::DisplayIntro(ControlsContainer* parent, RString cutscene) : DisplayCutscene(parent)
{
    Load("RscDisplayIntro");

    if (cutscene.GetLength() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    if (!ProcessTemplateName(cutscene))
    {
        Exit(IDC_CANCEL);
        return;
    }
    ParseIntro();

    if (CurrentTemplate.groups.Size() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
    if (!GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate))
    {
        Exit(IDC_CANCEL);
        return;
    }

    Init();
}

void DisplayIntro::Init()
{
    // main map
    GWorld->CreateMainMap();
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowMap(true);
        map->ShowWatch(false);
        map->ShowCompass(false);
        map->ShowNotepad(false);
        map->ShowGPS(false);

        map->SetCursor(nullptr);
    }

    // if (IsCampaign())
    {
        // load variables
        GameState* gstate = GWorld->GetGameState();
        AutoArray<GameVariable>& variables = GStats._campaign._variables;
        for (int i = 0; i < variables.Size(); i++)
        {
            GameVariable& var = variables[i];
            gstate->VarSet(var._name, var._value, var._readOnly);
        }
    }

    RString initScript = GetMissionDirectory() + RString("initintro.sqs");
    if (QIFStreamB::FileExist(initScript))
    {
        Script* script = new Script("initintro.sqs", GameValue(), INT_MAX);
        GWorld->AddScript(script);
        GWorld->SimulateScripts();
    }
}

DisplayOutro::DisplayOutro(ControlsContainer* parent, EndMode mode) : DisplayCutscene(parent)
{
    Load("RscDisplayOutro");

    if (mode == EMContinue || mode == EMKilled)
    {
        _mode = EMLoser;
    }
    else
    {
        _mode = mode;
    }

    switch (_mode)
    {
        case EMLoser:
            ParseCutscene("OutroLoose", false);
            break;
        case EMEnd1:
        case EMEnd2:
        case EMEnd3:
        case EMEnd4:
        case EMEnd5:
        case EMEnd6:
            ParseCutscene("OutroWin", false);
            break;
    }

    if (CurrentTemplate.groups.Size() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
    if (!GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate))
    {
        Exit(IDC_CANCEL);
        return;
    }
}

DisplayAward::DisplayAward(ControlsContainer* parent, RString cutscene) : DisplayCutscene(parent)
{
    Load("RscDisplayAward");

    if (cutscene.GetLength() == 0)
    {
        Exit(IDC_CANCEL);
        return;
    }

    if (!ProcessTemplateName(cutscene))
    {
        Exit(IDC_CANCEL);
        return;
    }
    ParseIntro();

    if (CurrentTemplate.groups.Size() == 0)
    {
        Exit(IDC_CANCEL);
        return;
    }

    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
    if (!GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate))
    {
        Exit(IDC_CANCEL);
        return;
    }
}

DisplayCampaignIntro::DisplayCampaignIntro(ControlsContainer* parent, RString cutscene) : DisplayCutscene(parent)
{
    Load("RscDisplayCampaign");

    if (cutscene.GetLength() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    if (!ProcessTemplateName(cutscene))
    {
        Exit(IDC_CANCEL);
        return;
    }
    ParseIntro();

    if (CurrentTemplate.groups.Size() == 0)
    {
        //		GWorld->EnableSimulation(false);
        Exit(IDC_CANCEL);
        return;
    }

    GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
    if (!GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate))
    {
        Exit(IDC_CANCEL);
        return;
    }

    RString initScript = GetMissionDirectory() + RString("initintro.sqs");
    if (QIFStreamB::FileExist(initScript))
    {
        Script* script = new Script("initintro.sqs", GameValue(), INT_MAX);
        GWorld->AddScript(script);
        GWorld->SimulateScripts();
    }
}

inline bool OnKilled()
{
    /*
        int lives = GStats._mission._lives;
        PoseidonAssert(lives != 0);
        if (lives > 0)
        {
            lives--;
            if (lives > 0)
            {
                GStats.ClearMission();
                GStats._mission._lives = lives;
                return false;
            }
            else
            {
                GStats._mission._lives = lives;
                return true;
            }
        }
        else if (lives < 0)
        {
            GStats.ClearMission();
        }
        return false;
    */
    PoseidonAssert(GStats._mission._lives != 0);
    if (GStats._mission._lives > 0)
    {
        GStats._mission._lives--;
        return GStats._mission._lives == 0;
    }
    else
    {
        return false;
    }
}

DisplayMissionEnd::DisplayMissionEnd(ControlsContainer* parent /*, bool editor*/) : Display(parent)
{
    int quotations = (IDS_QUOTE_LAST - IDS_QUOTE_1) / 2 + 1;
    _quotation = toIntFloor(quotations * GRandGen.RandomValue());
    Load("RscDisplayMissionEnd");

    GetCtrl(IDC_ME_LOAD)->ShowCtrl(false);
    GetCtrl(IDC_ME_RETRY)->ShowCtrl(false);
    if (GWorld->GetMode() != GModeNetware)
    {
        RString name = GetSaveDirectory() + RString("save.fps");
        if (QIFStream::FileExists(name))
        {
            GetCtrl(IDC_ME_LOAD)->ShowCtrl(true);
        }
        GetCtrl(IDC_ME_RETRY)->ShowCtrl(true);
    }
    InputSubsystem::Instance().ChangeGameFocus(+1);
}

DisplayMissionEnd::~DisplayMissionEnd()
{
    InputSubsystem::Instance().ChangeGameFocus(-1);
}

Control* DisplayMissionEnd::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_ME_SUBTITLE:
        {
            CStatic* text = new CStatic(this, idc, cls);
            int lives = GStats._mission._lives;
            PoseidonAssert(lives != 0);
            if (lives <= 0)
            {
                text->SetText("");
            }
            else if (lives <= 1)
            {
                text->SetText(LocalizeString(IDS_LIVES_1));
            }
            else if (lives <= 3)
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_LIVES_3), lives);
                text->SetText(buffer);
            }
            else
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_LIVES_MORE), lives);
                text->SetText(buffer);
            }
            return text;
        }
        case IDC_ME_QUOTATION:
        {
            CStatic* text = new CStatic(this, idc, cls);
            text->SetText(RString("\"") + LocalizeString(IDS_QUOTE_1 + 2 * _quotation) + RString("\""));
            text->EnableCtrl(false);
            return text;
        }
        case IDC_ME_AUTHOR:
        {
            CStatic* text = new CStatic(this, idc, cls);
            text->SetText(LocalizeString(IDS_AUTHOR_1 + 2 * _quotation));
            return text;
        }
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayMissionEnd::OnButtonClicked(int idc)
{
    Exit(idc);
}

#if _ENABLE_CHEATS
DisplayDebug::DisplayDebug(ControlsContainer* parent, GameState* gs, const char* cls) : Display(parent)
{
    _current = -1;
    _gs = gs;

    Load(cls);

    _enableSimulation = false;
    _enableDisplay = true;

    InputSubsystem::Instance().ChangeGameFocus(+1);
}

DisplayDebug::~DisplayDebug()
{
    InputSubsystem::Instance().ChangeGameFocus(-1);
}

const int DebugConsoleExpRows = 4;
struct DebugConsoleInfo
{
    AutoArray<RString> history;
    RString exp[4];
};

AutoArray<RString> DebugConsoleLog;
DebugConsoleInfo GDebugConsoleInfo;

void DisplayDebug::UpdateLog(CListBox* lbox)
{
    lbox->ClearStrings();
    int n = DebugConsoleLog.Size();
    for (int i = 0; i < n; i++)
    {
        lbox->AddString(DebugConsoleLog[i]);
    }
    lbox->SetCurSel(n - 1);
}

Control* DisplayDebug::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_DEBUG_LOG:
        {
            CListBox* lbox = new CListBox(this, idc, cls);
            UpdateLog(lbox);
            return lbox;
        }
        case IDC_DEBUG_EXP:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            _current = GDebugConsoleInfo.history.Size() - 1;
            edit->SetText(_current >= 0 ? GDebugConsoleInfo.history[_current] : "");
            return edit;
        }
        case IDC_DEBUG_EXP1:
        case IDC_DEBUG_EXP2:
        case IDC_DEBUG_EXP3:
        case IDC_DEBUG_EXP4:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->SetText(GDebugConsoleInfo.exp[idc - IDC_DEBUG_EXP1]);
            return edit;
        }
        case IDC_DEBUG_RES1:
        case IDC_DEBUG_RES2:
        case IDC_DEBUG_RES3:
        case IDC_DEBUG_RES4:
        {
            CEdit* edit = new CEdit(this, idc, cls);
            edit->EnableCtrl(false);
            return edit;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayDebug::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_DEBUG_APPLY:
        case IDC_OK:
            // execute expression
            {
                CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP));
                if (edit)
                {
                    RString exp = edit->GetText();
                    GameState* gstate = GWorld->GetGameState();
                    if (!gstate->CheckExecute(edit->GetText()))
                    {
                        FocusCtrl(IDC_DEBUG_EXP);
                        CreateMsgBox(MB_BUTTON_OK, gstate->GetLastErrorText());
                        edit->SetCaretPos(gstate->GetLastErrorPos());
                        return;
                    }
                    gstate->Execute(exp);

                    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_DEBUG_LOG));
                    if (lbox)
                        UpdateLog(lbox);
                }
            }
            // continue
        case IDC_CANCEL:
        {
            CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP));
            PoseidonAssert(edit);
            RString text = edit->GetText();
            int last = GDebugConsoleInfo.history.Size() - 1;
            if (last < 0 || GDebugConsoleInfo.history[last] != text)
                GDebugConsoleInfo.history.Add(text);
        }
            for (int i = 0; i < DebugConsoleExpRows; i++)
            {
                CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP1 + i));
                PoseidonAssert(edit);
                GDebugConsoleInfo.exp[i] = edit->GetText();
            }
            break;
    }

    Display::OnButtonClicked(idc);
}

bool DisplayDebug::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (this->GetFocused()->IDC() == IDC_DEBUG_EXP)
    {
        switch (nChar)
        {
            case SDLK_UP:
                if (_current > 0)
                {
                    _current--;
                    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP));
                    PoseidonAssert(edit);
                    edit->SetText(GDebugConsoleInfo.history[_current]);
                }
                return true;
            case SDLK_DOWN:
                if (_current < GDebugConsoleInfo.history.Size() - 1)
                {
                    _current++;
                    CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP));
                    PoseidonAssert(edit);
                    edit->SetText(GDebugConsoleInfo.history[_current]);
                }
                return true;
        }
    }

    return Display::OnKeyDown(nChar, nRepCnt, nFlags);
}

void DisplayDebug::OnDraw(EntityAI* vehicle, float alpha)
{
    for (int i = 0; i < DebugConsoleExpRows; i++)
    {
        char var[16];
        snprintf(var, sizeof(var), "debug%d", i + 1);
        GameState* gstate = GWorld->GetGameState();
        gstate->VarDelete(var);
    }
    for (int i = 0; i < DebugConsoleExpRows; i++)
    {
        char var[16];
        snprintf(var, sizeof(var), "debug%d", i + 1);
        GameState* gstate = GWorld->GetGameState();
        CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_EXP1 + i));
        PoseidonAssert(edit);
        RString exp = edit->GetText();
        RString result;
        if (exp.GetLength() > 0)
        {
            if (!gstate->CheckEvaluate(exp))
            {
                result = "Error";
            }
            else
            {
                GameValue val = gstate->Evaluate(exp);
                gstate->VarSet(var, val);
                result = val.GetText();
            }
        }
        edit = dynamic_cast<CEdit*>(GetCtrl(IDC_DEBUG_RES1 + i));
        PoseidonAssert(edit);
        edit->SetText(result);
    }

    Display::OnDraw(vehicle, alpha);
}

AbstractOptionsUI* CreateDebugConsole()
{
    return new DisplayDebug(nullptr, GWorld->GetGameState());
}

void DisplayDebug::DestroyHUD(int exit)
{
    GLOB_WORLD->DestroyOptions(exit);
}

#endif

// display with dirty glasses

DisplayNotebook::DisplayNotebook(ControlsContainer* parent) : Display(parent)
{
    /*
        _glassA = GlobLoadTexture("data\\notasskloa.paa");
        _glassB = GlobLoadTexture("data\\notassklob.paa");
        _glassC = GlobLoadTexture("data\\notasskloc.paa");
        _glassD = GlobLoadTexture("data\\notassklod.paa");
        _glassE = GlobLoadTexture("data\\notasskloe.paa");
    */
    _lastWeather = WSUndefined;
}
Control* DisplayNotebook::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_TIME:
            return new CStaticTime(this, idc, cls, false);
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayNotebook::OnDraw(EntityAI* vehicle, float alpha)
{
    // set values of info items
    WeatherState cur = GetWeather();
    if (cur != _lastWeather)
    {
        PoseidonAssert(dynamic_cast<CStatic*>(GetCtrl(IDC_WEATHER)));
        CStatic* picture = static_cast<CStatic*>(GetCtrl(IDC_WEATHER));
        switch (cur)
        {
            case WSClear:
                picture->SetText("jasnosl.paa");
                break;
            case WSCloudly:
                picture->SetText("polojasno.paa");
                break;
            case WSOvercast:
                picture->SetText("zatazenosl.paa");
                break;
            case WSRainy:
                picture->SetText("destivo.paa");
                break;
            case WSStormy:
                picture->SetText("bourka.paa");
                break;
        }
        _lastWeather = cur;
    }

    char buffer[256];
    PoseidonAssert(dynamic_cast<CStaticTime*>(GetCtrl(IDC_TIME)));
    CStaticTime* time = static_cast<CStaticTime*>(GetCtrl(IDC_TIME));
    Clock clock = GetTime();
    time->SetTime(clock);

    PoseidonAssert(dynamic_cast<CStatic*>(GetCtrl(IDC_DATE)));
    CStatic* text = static_cast<CStatic*>(GetCtrl(IDC_DATE));
    clock.FormatDate(LocalizeString(IDS_DATE_FORMAT), buffer);
    text->SetText(buffer);

    PoseidonAssert(dynamic_cast<CStatic*>(GetCtrl(IDC_POSITION)));
    text = static_cast<CStatic*>(GetCtrl(IDC_POSITION));
    Point3 pos;
    if (GetPosition(pos))
    {
        Poseidon::PositionToAA11(pos, buffer);
    }
    else
    {
        buffer[0] = 0;
    }
    text->SetText(buffer);

    // draw controls
    Display::OnDraw(vehicle, alpha);

    /*
        // draw glasses
        PackedColor color(Color(1,1,1,1));
        const int w = GLOB_ENGINE->Width();
        const int h = GLOB_ENGINE->Height();

        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassA, 0, 0);
        GLOB_ENGINE->Draw2D(mip, color, 0, 0, 0.4 * w, 0.5 * h);
        mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassB, 0, 0);
        GLOB_ENGINE->Draw2D(mip, color, 0.4 * w, 0, 0.4 * w, 0.5 * h);
        mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassC, 0, 0);
        GLOB_ENGINE->Draw2D(mip, color, 0, 0.5 * h, 0.4 * w, 0.5 * h);
        mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassD, 0, 0);
        GLOB_ENGINE->Draw2D(mip, color, 0.4 * w, 0.5 * h, 0.4 * w, 0.5 * h);
        mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassE, 0, 0);
        GLOB_ENGINE->Draw2D(mip, color, 0.8 * w, 0, 0.2 * w, 0.5 * h);
    */
}

DisplayMapEditor::DisplayMapEditor(ControlsContainer* parent) : DisplayNotebook(parent)
{
    _cursor = CursorArrow;
}

void DisplayMapEditor::OnSimulate(EntityAI* vehicle)
{
    CStaticMap* map = GetMap();
    if (map && GetCtrl(IDC_MAP)->IsVisible())
    {
        float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
        float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;
        IControl* ctrl = GetCtrl(mouseX, mouseY);
        if (ctrl && ctrl->IDC() == IDC_MAP)
        {
            if (map->_dragging || map->_selecting)
            {
                if (_cursor != CursorMove)
                {
                    _cursor = CursorMove;
                    SetCursor("Move");
                }
            }
            else if (map->_moving)
            {
                if (_cursor != CursorScroll)
                {
                    _cursor = CursorScroll;
                    SetCursor("Scroll");
                }
            }
            else
            {
                if (_cursor != CursorTrack)
                {
                    _cursor = CursorTrack;
                    SetCursor("Track");
                }
            }
        }
        else
        {
            if (_cursor != CursorArrow)
            {
                _cursor = CursorArrow;
                SetCursor("Arrow");
            }
        }
    }

    DisplayNotebook::OnSimulate(vehicle);
}

bool DisplayMapEditor::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    CStaticMap* map = GetMap();
    if (map && GetCtrl(IDC_MAP)->IsVisible())
    {
        if (map->OnKeyDown(nChar, nRepCnt, nFlags))
        {
            return true;
        }
    }
    return DisplayNotebook::OnKeyDown(nChar, nRepCnt, nFlags);
}

bool DisplayMapEditor::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    CStaticMap* map = GetMap();
    if (map && GetCtrl(IDC_MAP)->IsVisible())
    {
        if (map->OnKeyUp(nChar, nRepCnt, nFlags))
        {
            return true;
        }
    }
    return DisplayNotebook::OnKeyUp(nChar, nRepCnt, nFlags);
}

void DisplayMapEditor::OnDraw(EntityAI* vehicle, float alpha)
{
    DisplayNotebook::OnDraw(vehicle, alpha);
}

// creation of main display

AbstractOptionsUI* CreateMainOptionsUI()
{
    return new DisplayMain(nullptr);
}

AbstractOptionsUI* CreateEndOptionsUI(int mode)
{
    Fail("Obsolete");
    return nullptr;
}

void DisplayMain::DestroyHUD(int exit)
{
    GLOB_WORLD->DestroyOptions(exit);
}

void OpenEditor()
{
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
    {
        return;
    }
    GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));
    // Macrovision CD Protection
    CreateEditor(options);
    // options->CreateChild(new DisplayArcadeMap(options));
}

void StartIntro()
{
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
    {
        return;
    }
    options->CreateChild(new DisplayIntro(options));
}

void StartMission()
{
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
    {
        return;
    }
    options->CreateChild(new DisplayMission(options));
}

bool StartAutoTest()
{
    CurrentTemplate.Clear();
    if (GWorld->UI())
    {
        GWorld->UI()->Init();
    }

    bool introOnly = false;
    if (!ParseMission(false) || CurrentTemplate.groups.Size() == 0)
    {
        CurrentTemplate.Clear();
        if (!ParseIntro() || CurrentTemplate.groups.Size() == 0)
        {
            return false;
        }
        introOnly = true;
    }

    GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));
    GWorld->ActivateAddons(CurrentTemplate.addOns);
    GWorld->InitGeneral(CurrentTemplate.intel);
    if (!GWorld->InitVehicles(introOnly ? GModeIntro : GModeArcade, CurrentTemplate))
    {
        return false;
    }

    GStats.ClearAll();

    // remove temporary files
    RString dir = GetSaveDirectory();
    RString weapons = dir + RString("weapons.cfg");
    ::DeleteFile(weapons);
    RString save = dir + RString("continue.fps");
    ::DeleteFile(save);
    save = dir + RString("autosave.fps");
    ::DeleteFile(save);
    save = dir + RString("save.fps");
    ::DeleteFile(save);

    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
    {
        return false;
    }
    if (introOnly)
    {
        options->CreateChild(new DisplayIntro(options, DisplayIntro::noInit));
    }
    else
    {
        options->CreateChild(new DisplayMission(options));
    }
    return true;
}

} // namespace Poseidon
