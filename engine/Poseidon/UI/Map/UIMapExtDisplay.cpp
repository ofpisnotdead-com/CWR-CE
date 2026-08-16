#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>

#include <Poseidon/IO/PackFiles.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#if defined _WIN32
#endif

#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <filesystem>

#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/Core/Version.hpp>

namespace Poseidon
{
RString GetUserParams();
}

namespace Poseidon
{
static void DeleteROFile(RString name)
{
    std::error_code ec;
    std::filesystem::permissions(std::string(name), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ec);
    std::filesystem::remove(std::string(name), ec);
}

} // namespace Poseidon
namespace Poseidon
{
bool DefaultAdvancedEditorMode()
{
    return true;
}

bool LoadAdvancedEditorModeFromUserParams(const char* userParamsPath)
{
    ParamArchiveLoad ar(userParamsPath);
    bool advanced = Poseidon::DefaultAdvancedEditorMode();
    if (ar.Serialize("advancedEditor", advanced, 1, advanced) != LSOK)
    {
        Poseidon::Foundation::WarningMessage("Cannot load user paremeters.");
    }
    return advanced;
}
} // namespace Poseidon
namespace Poseidon
{

void DisplayArcadeMap::LoadParams()
{
    _advanced = Poseidon::LoadAdvancedEditorModeFromUserParams(Poseidon::GetUserParams());
}

void DisplayArcadeMap::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(Poseidon::GetUserParams());
    if (ar.Serialize("advancedEditor", _advanced, 1) != LSOK)
    {
    }
    if (ar.Save(Poseidon::GetUserParams()) != LSOK)
    {
    }
}

LSError DisplayArcadeMap::SerializeAll(ParamArchive& ar, bool merge)
{
    ATSParams params;
    ar.SetParams(&params);
    if (merge)
    {
        ArcadeTemplate t;
        PARAM_CHECK(ar.Serialize("Mission", t, 1))
        _templateMission.Merge(t);
        PARAM_CHECK(ar.Serialize("Intro", t, 1))
        _templateIntro.Merge(t);
        PARAM_CHECK(ar.Serialize("OutroWin", t, 1))
        _templateOutroWin.Merge(t);
        PARAM_CHECK(ar.Serialize("OutroLoose", t, 1))
        _templateOutroLoose.Merge(t);
        // do not merge cutscenes
    }
    else
    {
        PARAM_CHECK(ar.Serialize("Mission", _templateMission, 1))
        PARAM_CHECK(ar.Serialize("Intro", _templateIntro, 1))
        PARAM_CHECK(ar.Serialize("OutroWin", _templateOutroWin, 1))
        PARAM_CHECK(ar.Serialize("OutroLoose", _templateOutroLoose, 1))
        if (ar.IsLoading() && ar.GetArVersion() < 7)
        {
            ArcadeIntel intel;
            PARAM_CHECK(ar.Serialize("Intel", intel, 1))
            _templateMission.intel = intel;
            _templateIntro.intel = intel;
            _templateOutroWin.intel = intel;
            _templateOutroLoose.intel = intel;
        }
    }
    return LSOK;
}

bool DisplayArcadeMap::LoadTemplates(const char* filename)
{
    QIFStreamB test;
    test.AutoOpen(filename);
    if (test.get() == 0)
    {
        // binary file
        return false;
    }

    ParamArchiveLoad ar(filename);
    LSError result = SerializeAll(ar);
    if (result != LSOK)
    {
        if (result == LSNoAddOn)
        {
            RString message = LocalizeString(IDS_MSG_ADDON_MISSING);
            bool first = true;
            for (int i = 0; i < _templateMission.missingAddOns.Size(); i++)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    message = message + RString(", ");
                }
                message = message + _templateMission.missingAddOns[i];
            }
            for (int i = 0; i < _templateIntro.missingAddOns.Size(); i++)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    message = message + RString(", ");
                }
                message = message + _templateIntro.missingAddOns[i];
            }
            for (int i = 0; i < _templateOutroWin.missingAddOns.Size(); i++)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    message = message + RString(", ");
                }
                message = message + _templateOutroWin.missingAddOns[i];
            }
            for (int i = 0; i < _templateOutroLoose.missingAddOns.Size(); i++)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    message = message + RString(", ");
                }
                message = message + _templateOutroLoose.missingAddOns[i];
            }
            CreateMsgBox(MB_BUTTON_OK, message);
        }
        else
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
        }
        _templateMission.Clear();
        _templateIntro.Clear();
        _templateOutroWin.Clear();
        _templateOutroLoose.Clear();
        return false;
    }

    _currentTemplate = &_templateMission;
    CCombo* combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCMAP_SECTION));
    if (combo)
    {
        combo->SetCurSel(0);
    }
    _map->SetTemplate(_currentTemplate);

    // loaded

    // warning for rad only files
    if (QIFStream::FileReadOnly(filename))
    {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_TEMPL_READONLY));
    }

    return true;
}

bool DisplayArcadeMap::MergeTemplates(const char* filename)
{
    QIFStreamB test;
    test.AutoOpen(filename);
    if (test.get() == 0)
    {
        // binary file
        return false;
    }

    ParamArchiveLoad ar(filename);
    LSError result = SerializeAll(ar, true);
    if (result == LSNoAddOn)
    {
        RString message = LocalizeString(IDS_MSG_ADDON_MISSING);
        bool first = true;
        for (int i = 0; i < _templateMission.missingAddOns.Size(); i++)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                message = message + RString(", ");
            }
            message = message + _templateMission.missingAddOns[i];
        }
        for (int i = 0; i < _templateIntro.missingAddOns.Size(); i++)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                message = message + RString(", ");
            }
            message = message + _templateIntro.missingAddOns[i];
        }
        for (int i = 0; i < _templateOutroWin.missingAddOns.Size(); i++)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                message = message + RString(", ");
            }
            message = message + _templateOutroWin.missingAddOns[i];
        }
        for (int i = 0; i < _templateOutroLoose.missingAddOns.Size(); i++)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                message = message + RString(", ");
            }
            message = message + _templateOutroLoose.missingAddOns[i];
        }
        CreateMsgBox(MB_BUTTON_OK, message);
        return false;
    }
    else if (result != LSOK)
    {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
        return false;
    }

    //	_currentTemplate = &_templateMission;

    return true;
}

bool DisplayArcadeMap::SaveTemplates(const char* filename)
{
    ParamArchiveSave ar(MissionsVersion);
    if (SerializeAll(ar) != LSOK)
    {
        return false;
    }
    LSError err = ar.Save(filename);
    if (err != LSOK)
    {
        if (err == LSAccessDenied)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_TEMPL_ACCESSDENIED));
        }
        else
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SAVE_TEMPL_FAIL));
        }
        return false;
    }
    return true;
}

WeatherState DisplayArcadeMap::GetWeather()
{
    return GetWeatherState(_currentTemplate->intel.weather);
}

// static const int daysInMonth[]={31,28,31,30,31,30,31,31,30,31,30,31};
} // namespace Poseidon
namespace Poseidon
{
int GetDaysInMonth(int year, int month);
}
namespace Poseidon
{

Clock DisplayArcadeMap::GetTime()
{
    int day = _currentTemplate->intel.day - 1;
    int year = _currentTemplate->intel.year;
    for (int m = 0; m < _currentTemplate->intel.month - 1; m++)
    {
        day += Poseidon::GetDaysInMonth(year, m);
    }

    Clock clock;
    clock.SetTimeInYear(OneDay * day + OneHour * _currentTemplate->intel.hour +
                        OneMinute * _currentTemplate->intel.minute + 0.5 * OneSecond);
    clock.SetTimeOfDay(_currentTemplate->intel.hour, _currentTemplate->intel.minute);
    clock.SetYear(year);
    return clock;
}

bool DisplayArcadeMap::GetPosition(Point3& pos)
{
    ArcadeUnitInfo* me = _currentTemplate->FindPlayer();
    if (me)
    {
        pos = me->position;
        return true;
    }
    else
    {
        return false;
    }
}

bool BreakIntro(unsigned nChar)
{
    if (nChar == 0)
    {
        // called from OnSimulate
        auto& input = InputSubsystem::Instance();
        if (input.IsMouseLeftPressed())
        {
            return true;
        }
        if (input.IsMouseRightPressed())
        {
            return true;
        }
        return false;
    }
    else
    {
        // called from OnKeyDown
        if (nChar == SDLK_ESCAPE)
        {
            return true;
        }
        if (nChar == SDLK_SPACE)
        {
            return true;
        }
        return false;
    }
}

void DisplayArcadeMap::ShowButtons()
{
    IControl* ctrl = GetCtrl(IDC_ARCMAP_PREVIEW);
    if (ctrl)
    {
        bool show = _currentTemplate->IsConsistent(nullptr, _multiplayer);
        if (show && _multiplayer)
        {
            show = Glob.header.filename[0] != 0;
        }
        ctrl->ShowCtrl(show);
    }
    ctrl = GetCtrl(IDC_ARCMAP_CONTINUE);
    if (ctrl)
    {
        ctrl->ShowCtrl(!_multiplayer && (GWorld->GetMode() == GModeArcade));
    }
}

void DisplayArcadeMap::SetAdvancedMode(bool advanced)
{
    _advanced = advanced;
    CActiveText* txt = static_cast<CActiveText*>(GetCtrl(IDC_ARCMAP_DIFF));
    if (txt)
    {
        if (advanced)
        {
            txt->SetText(LocalizeString(IDS_EDITOR_ADVANCED));
        }
        else
        {
            txt->SetText(LocalizeString(IDS_EDITOR_EASY));
        }
    }

    IControl* ctrl = GetCtrl(IDC_ARCMAP_MERGE);
    if (ctrl)
    {
        ctrl->ShowCtrl(_advanced);
    }
    ctrl = GetCtrl(IDC_ARCMAP_SECTION);
    if (ctrl)
    {
        ctrl->ShowCtrl(_advanced);
    }
    ctrl = GetCtrl(IDC_ARCMAP_IDS);
    if (ctrl)
    {
        ctrl->ShowCtrl(_advanced);
    }
    if (!_advanced)
    {
        _map->ShowIds(false);
        UpdateIdsButton();
    }

    if (!_advanced && _currentTemplate != &_templateMission)
    {
        CCombo* combo = static_cast<CCombo*>(GetCtrl(IDC_ARCMAP_SECTION));
        if (combo)
        {
            combo->SetCurSel(0);
        }
    }
}

// Arcade Map display

Control* DisplayArcadeMap::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MAP:
        {
            _map = new CStaticMapArcade(this, idc, cls, ERFull, 0.001, 1.0, 0.1);
            _map->SetTemplate(&_templateMission);
            return _map;
        }
        case IDC_ARCMAP_SECTION:
        {
            CCombo* combo = new CCombo(this, idc, cls);
            combo->AddString(LocalizeString(IDS_SECTION_MISSION));
            combo->AddString(LocalizeString(IDS_SECTION_INTRO));
            combo->AddString(LocalizeString(IDS_SECTION_OUTRO_WIN));
            combo->AddString(LocalizeString(IDS_SECTION_OUTRO_LOOSE));
            combo->SetCurSel(0);
            return combo;
        }
        default:
            return DisplayMapEditor::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayArcadeMap::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_ARCMAP_DIFF:
            SetAdvancedMode(!_advanced);
            SaveParams();
            return;
        case IDC_ARCMAP_LOAD:
            CreateChild(new DisplayTemplateLoad(this));
            return;
        case IDC_ARCMAP_MERGE:
            CreateChild(new DisplayTemplateLoad(this, true));
            return;
        case IDC_ARCMAP_SAVE:
            CreateChild(new DisplayTemplateSave(this));
            return;
        case IDC_ARCMAP_INTEL:
            CreateChild(new DisplayIntel(this, _currentTemplate->intel, _advanced));
            return;
        case IDC_ARCMAP_CLEAR:
            CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE), IDD_MSG_CLEARTEMPLATE);
            return;
        case IDC_CANCEL:
            CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE), IDD_MSG_EXITTEMPLATE);
            return;
        case IDC_ARCMAP_PREVIEW:
            if (_running)
            {
                return;
            }
            _running = true;
            if (_currentTemplate->IsConsistent(this, _multiplayer))
            {
                _currentTemplate->ScanRequiredAddons();
                if (_multiplayer)
                {
                    // save current version
                    RString directory;
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)GetMissionsDirectory());
                    CreateDirectoryUtf8(buffer);
                    strncat(buffer, Glob.header.filename, sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, ".", sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, Glob.header.worldname, sizeof(buffer) - strlen(buffer) - 1);
                    directory = buffer;
                    CreateDirectoryUtf8(directory);
                    strncat(buffer, "\\mission.sqm", sizeof(buffer) - strlen(buffer) - 1);
                    SaveTemplates(buffer);

                    Exit(IDC_OK);
                    return;
                }

                CurrentTemplate = *_currentTemplate;
                _alwaysShow = false;

                RString weapons = GetSaveDirectory() + RString("weapons.cfg");
                ::DeleteFile(weapons);

                GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));
                GWorld->ActivateAddons(CurrentTemplate.addOns);
                GWorld->InitGeneral(CurrentTemplate.intel);
                if (_currentTemplate == &_templateMission)
                {
                    GStats.ClearAll();
                    bool ok = GWorld->InitVehicles(GModeArcade, CurrentTemplate);

                    if (ok)
                    {
                        RString weapons = GetSaveDirectory() + RString("weapons.cfg");
                        ::DeleteFile(weapons);

                        // remove continue.fps
                        RString dir = GetSaveDirectory();
                        RString save = dir + RString("continue.fps");
                        ::DeleteFile(save);
                        save = dir + RString("autosave.fps");
                        ::DeleteFile(save);
                        save = dir + RString("save.fps");
                        ::DeleteFile(save);

                        if ((InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) ||
                             InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT)) &&
                            GetBriefingFile().GetLength() > 0)
                        {
                            CreateChild(new DisplayGetReady(this));
                            _running = false;
                            return;
                        }
                        CreateChild(new DisplayMission(this, true));
                    }
                    else
                    {
                        _alwaysShow = true;
                    }
                }
                else
                {
                    bool ok = GWorld->InitVehicles(GModeIntro, CurrentTemplate);
                    if (ok)
                    {
                        CreateChild(new DisplayIntro(this, DisplayIntro::noInit));
                    }
                    else
                    {
                        _alwaysShow = true;
                    }
                }
            }
            _running = false;
            return;
        case IDC_ARCMAP_CONTINUE:
            if (GWorld->GetMode() == GModeArcade)
            {
                _alwaysShow = false;
                //				GWorld->EnableDisplay(true);
                CreateChild(new DisplayMission(this));
            }
            return;
        case IDC_ARCMAP_IDS:
            if (_map)
            {
                _map->ShowIds(!_map->IsShowingIds());
                UpdateIdsButton();
            }
            break;
        case IDC_ARCMAP_TEXTURES:
            if (_map)
            {
                _map->ShowScale(!_map->IsShowingScale());
                UpdateTexturesButton();
            }
            break;
        default:
            DisplayMapEditor::OnButtonClicked(idc);
            break;
    }
}

void DisplayArcadeMap::UpdateIdsButton()
{
    CButton* button = dynamic_cast<CButton*>(GetCtrl(IDC_ARCMAP_IDS));
    if (button && _map)
    {
        if (_map->IsShowingIds())
        {
            button->SetText(LocalizeString(IDS_ARCMAP_HIDE_IDS));
        }
        else
        {
            button->SetText(LocalizeString(IDS_ARCMAP_SHOW_IDS));
        }
    }
}

void DisplayArcadeMap::UpdateTexturesButton()
{
    CButton* button = dynamic_cast<CButton*>(GetCtrl(IDC_ARCMAP_TEXTURES));
    if (button && _map)
    {
        if (_map->IsShowingScale())
        {
            button->SetText(LocalizeString(IDS_ARCMAP_SHOW_TEXTURES));
        }
        else
        {
            button->SetText(LocalizeString(IDS_ARCMAP_HIDE_TEXTURES));
        }
    }
}

void DisplayArcadeMap::OnComboSelChanged(int idc, int curSel)
{
    switch (idc)
    {
        case IDC_ARCMAP_SECTION:
            if (curSel == 0)
            {
                _currentTemplate = &_templateMission;
            }
            else if (curSel == 1)
            {
                _currentTemplate = &_templateIntro;
            }
            else if (curSel == 2)
            {
                _currentTemplate = &_templateOutroWin;
            }
            else if (curSel == 3)
            {
                _currentTemplate = &_templateOutroLoose;
            }
            _map->_infoClick._type = signNone;
            _map->_infoClickCandidate._type = signNone;
            _map->_infoMove._type = signNone;
            _map->SetTemplate(_currentTemplate);
            // Intel does not change
            ArcadeUnitInfo* uInfo = _currentTemplate->FindPlayer();
            if (uInfo)
            {
                Glob.header.playerSide = (TargetSide)uInfo->side;
            }
            ShowButtons();
            break;
    }
    Display::OnComboSelChanged(idc, curSel);
}

void DisplayArcadeMap::OnToolBoxSelChanged(int idc, int curSel)
{
    if (idc == IDC_ARCMAP_MODE)
    {
        _mode = (InsertMode)curSel;
    }
}

void DisplayArcadeMap::CycleControllerMode(int delta)
{
    int mode = static_cast<int>(_mode) + delta;
    if (mode < 0)
        mode = IMN - 1;
    else if (mode >= IMN)
        mode = 0;

    _mode = static_cast<InsertMode>(mode);
    CToolBox* box = dynamic_cast<CToolBox*>(GetCtrl(IDC_ARCMAP_MODE));
    if (box)
        box->SetCurSel(_mode);
}

bool DisplayArcadeMap::DoControllerUiAction(ControllerUiAction action)
{
    ControllerEditorUiLayout layout;
    const ControllerUiDispatch dispatch = layout.Map(action);

    switch (dispatch.kind)
    {
        case ControllerUiDispatchKind::PreviousTab:
            CycleControllerMode(-1);
            return true;
        case ControllerUiDispatchKind::NextTab:
            CycleControllerMode(1);
            return true;
        case ControllerUiDispatchKind::Preview:
        {
            IControl* preview = GetCtrl(IDC_ARCMAP_PREVIEW);
            if (preview && preview->IsVisible() && preview->IsEnabled())
            {
                OnButtonClicked(IDC_ARCMAP_PREVIEW);
                return true;
            }
            return false;
        }
        default:
            break;
    }
    if (dispatch.kind == ControllerUiDispatchKind::KeyTap)
    {
        DoKeyDown(dispatch.key, 1, 0);
        DoKeyUp(dispatch.key, 1, 0);
        return true;
    }
    return false;
}

ControllerUiScene DisplayArcadeMap::GetControllerUiScene() const
{
    return EditorMapControllerScene(51);
}

#if _ENABLE_CHEATS
// hpp {
struct Argument
{
    RString name;
    RString value;
};

template <class Type>
inline RString CreateArgument(const Type& value)
{
    return FindEnumName(value);
}

template <>
inline RString CreateArgument(const RString& value)
{
    return value;
}

template <>
inline RString CreateArgument(const float& value)
{
    return Format("%g", value);
}

template <>
inline RString CreateArgument(const int& value)
{
    return Format("%d", value);
}

template <>
inline RString CreateArgument(const bool& value)
{
    if (value)
        return "true";
    else
        return "false";
}

template <>
inline RString CreateArgument(const Vector3& value)
{
    return Format("[%g, %g]", value.X(), value.Z());
}

template <>
inline RString CreateArgument(const TargetSide& value)
{
    switch (value)
    {
        case TWest:
            return "west";
        case TEast:
            return "east";
        case TCivilian:
            return "civilian";
        case TGuerrila:
            return "resistance";
        case TLogic:
            return "sideLogic";
        default:
            Fail("Side");
            return "ERROR";
    }
}

class Arguments : public AutoArray<Argument>
{
  private:
    typedef AutoArray<Argument> base;

  public:
    RString Get(RString name) const
    {
        for (int i = 0; i < Size(); i++)
            if (base::Get(i).name == name)
                return base::Get(i).value;
        return RString();
    }
    template <class Type>
    int Set(RString name, const Type& value)
    {
        return SetRaw(name, CreateArgument(value));
    };

  protected:
    int SetRaw(RString name, RString value)
    {
        int index = Add();
        base::Set(index).name = name;
        base::Set(index).value = value;
        return index;
    };
};

enum EditorParamSource
{
    EPSDialog,
    EPSPosition,
    EPSLink,
    EPSId
};

struct EditorParam
{
    EditorParamSource source;
    RString name;
    RString type;
    RString subtype;
    bool hasDefValue;
    RString defValue;

    EditorParam(EditorParamSource src, RString n, RString t, RString subt, bool hasDefVal, RString defVal)
    {
        source = src;
        name = n;
        type = t;
        subtype = subt;
        hasDefValue = hasDefVal;
        defValue = defVal;
    }
};

class EditorParams : public AutoArray<EditorParam>
{
  public:
    void Load(const ParamEntry& cfg, RString name);
    const EditorParam* Find(RString name) const
    {
        for (int i = 0; i < Size(); i++)
            if (Get(i).name == name)
                return &Get(i);
        return nullptr;
    }
};

class EditorObjectType
{
  protected:
    RString _name;
    EditorParams _params;
    AutoArray<RString> _create;
    int _nextID;

  public:
    EditorObjectType(RString name);

    void WriteToScript(QOStream& out, const Arguments& args) const;
    RString NextVarName() { return Format("_%s_%d", (const char*)_name, _nextID++); }
};

// }

// cpp {

void EditorParams::Load(const ParamEntry& cfg, RString name)
{
    EditorParam defaults[] = {EditorParam(EPSId, "VARIABLE_NAME", name, "", false, "")};
    int nDefaults = sizeof(defaults) / sizeof(EditorParam);

    int n = cfg.GetEntryCount();
    Realloc(n + nDefaults);
    Resize(n + nDefaults);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& entry = cfg.GetEntry(i);

        const ParamEntry* check = entry.FindEntry("source");
        if (check)
        {
            RString source = *check;
            if (stricmp(source, "link") == 0)
                Set(i).source = EPSLink;
            else if (stricmp(source, "position") == 0)
                Set(i).source = EPSPosition;
            else
            {
                DoAssert(stricmp(source, "dialog") == 0);
                Set(i).source = EPSDialog;
            }
        }
        else
            Set(i).source = EPSDialog;

        Set(i).name = entry.GetName();
        Set(i).type = entry >> "type";

        check = entry.FindEntry("subtype");
        if (check)
            Set(i).subtype = *check;

        check = entry.FindEntry("default");
        if (check)
        {
            Set(i).hasDefValue = true;
            Set(i).defValue = *check;
        }
        else
            Set(i).hasDefValue = false;
    }

    // default parameters
    for (int i = 0; i < nDefaults; i++)
        Set(n + i) = defaults[i];
}

EditorObjectType::EditorObjectType(RString name)
{
    _name = name;
    _nextID = 0;

    ParamFile file;
    file.Parse(Format("Editor\\%s.hpp", (const char*)name));
    _params.Load(file >> "Params", _name);

    const ParamEntry& array = file >> "create";
    int n = array.GetSize();
    _create.Realloc(n);
    _create.Resize(n);
    for (int i = 0; i < n; i++)
        _create[i] = array[i];
}

inline void WriteLine(QOStream& out, RString line)
{
    out.write(line, strlen(line));
    out.write("\r\n", 2);
}

static RString NormalizeString(const char* src)
{
    static const int len = 1024;
    char dst[len];
    int i = 0;
    for (const char* ptr = src; *ptr; ptr++)
    {
        if (i >= len - 1)
        {
            Fail("Buffer overflow");
            break;
        }
        dst[i++] = *ptr;
        if (*ptr == '"')
        {
            if (i >= len - 1)
            {
                Fail("Buffer overflow");
                break;
            }
            dst[i++] = *ptr;
        }
    }
    dst[i] = 0;
    return dst;
}

static RString ExtractValue(RString name, const EditorParams& params, const Arguments& args, bool& required)
{
    const EditorParam* param = params.Find(name);
    if (!param)
    {
        LOG_ERROR(UI, "Undeclared parameter: {}", (const char*)name);
        return "ERROR";
    }

    switch (param->source)
    {
        case EPSLink:
        {
            if (stricmp(param->subtype, "single") == 0)
            {
                // no default value
                required = true;
                return args.Get(name);
            }
            else
            {
                RString value = "[";
                bool first = true;
                for (int i = 0; i < args.Size(); i++)
                {
                    if (args[i].name != name)
                        continue;
                    if (first)
                        first = false;
                    else
                        value = value + ", ";
                    value = value + args[i].value;
                }
                if (!first)
                    required = true;
                return value + "]";
            }
        }
        default:
        {
            RString value = args.Get(name);

            if (!param->hasDefValue || value != param->defValue)
                required = true;

            if (stricmp(param->type, "text") == 0)
                return RString("\"") + NormalizeString(value) + RString("\"");
            else if (stricmp(param->type, "enum") == 0)
                return RString("\"") + value + RString("\"");
            else if (stricmp(param->type, "config") == 0)
                return RString("\"") + value + RString("\"");
            return value;
        }
    }
}

#define PARSING_COMMENT "."

typedef void (*OnRequiredFunction)(void* context);
typedef RString (*OnValueFunction)(RString name, const EditorParams& params, void* context);
typedef RString (*OnExpressionFunction)(RString name, const EditorParams& params, void* context);

static RString ParseLine(RString format, const EditorParams& params, OnRequiredFunction onRequired,
                         OnValueFunction onValue, OnExpressionFunction onExpression, void* context)
{
    static const int len = 1024;
    char dst[len];
    int i = 0;
    for (const char* ptr = format; *ptr; ptr++)
    {
        if (*ptr == '%')
        {
            ptr++;
            if (!*ptr)
            {
                Fail("Syntax error");
                break;
            }
            else if (*ptr == '%')
            {
                dst[i++] = *ptr;
                if (i >= len - 1)
                {
                    Fail("Buffer overflow");
                    break;
                }
            }
            else if (*ptr == '!')
            {
                onRequired(context);
            }
            else if (*ptr == '(')
            {
                ptr++;
                const char* begin = ptr;
                while (*ptr && *ptr != ')')
                    ptr++;
                RString name(begin, ptr - begin);
                if (!*ptr)
                    ptr--;

                // expression
                RString value = onExpression(name, params, context);
                int n = value.GetLength();
                if (i + n >= len - 1)
                {
                    Fail("Buffer overflow");
                    break;
                }
                strcpy(dst + i, value);
                i += n;
            }
            else
            {
                const char* begin = ptr;
                while (*ptr && __iscsym(*ptr))
                    ptr++;
                RString name(begin, ptr - begin);
                ptr--;

                // value
                RString value = onValue(name, params, context);
                int n = value.GetLength();
                if (i + n >= len - 1)
                {
                    Fail("Buffer overflow");
                    break;
                }
                strcpy(dst + i, value);
                i += n;
            }
        }
        else
        {
            dst[i++] = *ptr;
            if (i >= len - 1)
            {
                Fail("Buffer overflow");
                break;
            }
        }
    }
    dst[i] = 0;
    return dst;
}

struct CreateContext
{
    const Arguments* args;
    bool some;
    bool required;
};

void CreateOnRequired(void* context)
{
    CreateContext* ctx = (CreateContext*)context;
    ctx->required = true;
}

RString CreateOnValue(RString name, const EditorParams& params, void* context)
{
    CreateContext* ctx = (CreateContext*)context;
    ctx->some = true;
    return RString("/*" PARSING_COMMENT) + name + RString("*/") +
           ExtractValue(name, params, *ctx->args, ctx->required) + RString("/*" PARSING_COMMENT "*/");
}

RString CreateOnExpression(RString name, const EditorParams& params, void* context)
{
    CreateContext* ctx = (CreateContext*)context;
    ctx->some = true;
    return ExtractValue(name, params, *ctx->args, ctx->required);
}

void EditorObjectType::WriteToScript(QOStream& out, const Arguments& args) const
{
    CreateContext ctx;
    ctx.args = &args;

    RString varName = args.Get("VARIABLE_NAME");
    WriteLine(out, Format("/*" PARSING_COMMENT "_OBJECT %s %s {*/", (const char*)_name, (const char*)varName));
    for (int i = 0; i < _create.Size(); i++)
    {
        ctx.some = false;
        ctx.required = false;
        RString line = ParseLine(_create[i], _params, CreateOnRequired, CreateOnValue, CreateOnExpression, &ctx);
        if (ctx.required || !ctx.some)
            WriteLine(out, line);
    }
    WriteLine(out, "/*" PARSING_COMMENT "}*/");
}

// }

static void WriteLine(QOStream& out, const char* format, ...)
{
    va_list arglist;
    va_start(arglist, format);

    BString<1024> line;
    vsprintf(line, format, arglist);
    out.write(line, strlen(line));
    out.write("\r\n", 2);

    va_end(arglist);
}

struct CenterInfo
{
    TargetSide side;
    RString variable;
};

struct MarkerInfo
{
    RString name;
    RString variable;
};

struct VehicleInfo
{
    int id;
    RString variable;
};

struct SyncInfo
{
    int sync;
    RString variable1;
    RString variable2;
};

} // namespace Poseidon
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Vehicles/InvisibleVeh.hpp>

namespace Poseidon
{

static void WriteEffects(Arguments& args, const ArcadeEffects& effects)
{
    args.Set("EFFECT_CONDITION", effects.condition);
    args.Set("CAMERA_EFFECT", effects.cameraEffect);
    args.Set("CAMERA_EFFECT_POSITION", effects.cameraPosition);
    args.Set("SOUND_EFFECT", effects.sound);
    args.Set("VOICE_EFFECT", effects.voice);
    args.Set("SOUND_ENV_EFFECT", effects.soundEnv);
    args.Set("SOUND_DET_EFFECT", effects.soundDet);
    args.Set("MUSIC_EFFECT", effects.track);
    args.Set("TITLE_EFFECT_TYPE", effects.titleType);
    args.Set("TITLE_EFFECT_EFFECT", effects.titleEffect);
    args.Set("TITLE_EFFECT_TITLE", effects.title);
}

static void WriteTrigger(QOStream& out, const ArcadeSensorInfo& sensor, EditorObjectType& typeTrigger,
                         EditorObjectType& typeGuardedPoint, AutoArray<SyncInfo>& waypointMap,
                         AutoArray<VehicleInfo>& vehicleMap)
{
    TargetSide side = TSideUnknown;
    switch (sensor.type) // special cases - guarded by ...
    {
        case ASTEastGuarded:
            side = TEast;
            break;
        case ASTWestGuarded:
            side = TWest;
            break;
        case ASTGuerrilaGuarded:
            side = TGuerrila;
            break;
    }
    if (side == TSideUnknown)
    {
        // add real trigger
        Arguments args;

        args.Set("POSITION", sensor.position);
        args.Set("OBJECT", sensor.object);
        args.Set("A", sensor.a);
        args.Set("B", sensor.b);
        args.Set("ANGLE", sensor.angle);
        args.Set("RECTANGULAR", sensor.rectangular);
        args.Set("ACTIVATION_BY", sensor.activationBy);
        args.Set("ACTIVATION_TYPE", sensor.activationType);
        args.Set("REPEATING", sensor.repeating);
        args.Set("TYPE", sensor.type);
        args.Set("TIMEOUT_MIN", sensor.timeoutMin);
        args.Set("TIMEOUT_MID", sensor.timeoutMid);
        args.Set("TIMEOUT_MAX", sensor.timeoutMax);
        args.Set("INTERRUPTABLE", sensor.interruptable);
        args.Set("ID_STATIC", sensor.idStatic);
        if (sensor.idVehicle >= 0)
        {
            bool found = false;
            for (int i = 0; i < vehicleMap.Size(); i++)
            {
                if (sensor.idVehicle == vehicleMap[i].id)
                {
                    args.Set("VEHICLE", vehicleMap[i].variable);
                    found = true;
                    break;
                }
            }
            DoAssert(found);
        }
        args.Set("TEXT", sensor.text);
        args.Set("EXP_COND", sensor.expCond);
        args.Set("EXP_ACTIV", sensor.expActiv);
        args.Set("EXP_DESACTIV", sensor.expDesactiv);
        args.Set("AGE", sensor.age);
        args.Set("NAME", sensor.name);
        WriteEffects(args, sensor.effects);

        RString varName = typeTrigger.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        // synchronizations
        for (int s = 0; s < sensor.synchronizations.Size(); s++)
        {
            int sync = sensor.synchronizations[s];
            RString name;
            for (int k = 0; k < waypointMap.Size(); k++)
            {
                if (sync == waypointMap[k].sync)
                {
                    DoAssert(waypointMap[k].variable2.GetLength() == 0);
                    name = waypointMap[k].variable1;
                    waypointMap[k].variable2 = varName;
                    break;
                }
            }
            DoAssert(name.GetLength() > 0);
            args.Set("SYNC", name);
        }

        typeTrigger.WriteToScript(out, args);
    }
    else
    {
        // add guarded point
        Arguments args;

        args.Set("POSITION", sensor.position);
        args.Set("SIDE", side);
        args.Set("ID_STATIC", sensor.idStatic);
        if (sensor.idVehicle >= 0)
        {
            bool found = false;
            for (int i = 0; i < vehicleMap.Size(); i++)
            {
                if (sensor.idVehicle == vehicleMap[i].id)
                {
                    args.Set("VEHICLE", vehicleMap[i].variable);
                    found = true;
                    break;
                }
            }
            DoAssert(found);
        }

        RString varName = typeGuardedPoint.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeGuardedPoint.WriteToScript(out, args);
    }
}

RString CreateCondition(RString presenceCondition, float presence)
{
    if (presence < 1)
    {
        if (strcmp(presenceCondition, "true") == 0)
            return Format("%g > random 1", presence);
        return Format("(%s) and (%g > random 1)", (const char*)presenceCondition, presence);
    }
    else
        return presenceCondition;
}

static RString WriteVehicle(QOStream& out, const ArcadeUnitInfo& unit, EditorObjectType& typeSoundSource,
                            EditorObjectType& typeMine, EditorObjectType& typeVehicle, AutoArray<MarkerInfo>& markerMap,
                            AutoArray<VehicleInfo>& vehicleMap)
{
    RString vehClass = Pars >> "CfgVehicles" >> unit.vehicle >> "vehicleClass";

    if (stricmp(vehClass, "Sounds") == 0)
    {
        // sound source
        Arguments args;

        RString condition = CreateCondition(unit.presenceCondition, unit.presence);
        args.Set("PRESENCE_CONDITION", condition);
        args.Set("POSITION", unit.position);
        args.Set("TYPE", unit.vehicle);
        for (int i = 0; i < unit.markers.Size(); i++)
            for (int j = 0; j < markerMap.Size(); j++)
                if (markerMap[j].name == unit.markers[i])
                {
                    args.Set("MARKER", markerMap[j].variable);
                    break;
                }
        args.Set("PLACEMENT", unit.placement);

        RString varName = typeSoundSource.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeSoundSource.WriteToScript(out, args);

        return RString();
    }
    else if (stricmp(vehClass, "Mines") == 0)
    {
        // mine
        Arguments args;

        RString condition = CreateCondition(unit.presenceCondition, unit.presence);
        args.Set("PRESENCE_CONDITION", condition);
        args.Set("POSITION", unit.position);
        args.Set("TYPE", unit.vehicle);
        for (int i = 0; i < unit.markers.Size(); i++)
            for (int j = 0; j < markerMap.Size(); j++)
                if (markerMap[j].name == unit.markers[i])
                {
                    args.Set("MARKER", markerMap[j].variable);
                    break;
                }
        args.Set("PLACEMENT", unit.placement);

        RString varName = typeMine.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeMine.WriteToScript(out, args);

        return RString();
    }
    else
    {
        // regular vehicle
        Arguments args;

        RString condition = CreateCondition(unit.presenceCondition, unit.presence);
        args.Set("PRESENCE_CONDITION", condition);
        args.Set("POSITION", unit.position);
        args.Set("TYPE", unit.vehicle);
        for (int i = 0; i < unit.markers.Size(); i++)
            for (int j = 0; j < markerMap.Size(); j++)
                if (markerMap[j].name == unit.markers[i])
                {
                    args.Set("MARKER", markerMap[j].variable);
                    break;
                }
        args.Set("PLACEMENT", unit.placement);
        args.Set("SPECIAL", unit.special);
        args.Set("AZIMUT", unit.azimut);
        args.Set("NAME", unit.name);
        args.Set("HEALTH", unit.health);
        args.Set("FUEL", unit.fuel);
        args.Set("AMMO", unit.ammo);
        args.Set("ID", unit.id);
        args.Set("AGE", unit.age);
        args.Set("LOCK", unit.lock);
        args.Set("INIT", unit.init);

        RString varName = typeVehicle.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeVehicle.WriteToScript(out, args);

        if (unit.id >= 0)
        {
            int index = vehicleMap.Add();
            vehicleMap[index].id = unit.id;
            vehicleMap[index].variable = varName;
        }

        return varName;
    }
}

static void WriteUnit(QOStream& out, RString grpName, const ArcadeUnitInfo& unit, EditorObjectType& typeUnit,
                      AutoArray<MarkerInfo>& markerMap, AutoArray<VehicleInfo>& vehicleMap)
{
    // free soldier
    Arguments args;

    RString condition = CreateCondition(unit.presenceCondition, unit.presence);
    args.Set("PRESENCE_CONDITION", condition);
    args.Set("POSITION", unit.position);
    args.Set("GROUP", grpName);
    args.Set("TYPE", unit.vehicle);
    for (int i = 0; i < unit.markers.Size(); i++)
        for (int j = 0; j < markerMap.Size(); j++)
            if (markerMap[j].name == unit.markers[i])
            {
                args.Set("MARKER", markerMap[j].variable);
                break;
            }
    args.Set("PLACEMENT", unit.placement);
    args.Set("SPECIAL", unit.special);
    args.Set("AZIMUT", unit.azimut);
    args.Set("NAME", unit.name);
    args.Set("HEALTH", unit.health);
    args.Set("AMMO", unit.ammo);
    args.Set("ID", unit.id);
    args.Set("AGE", unit.age);
    args.Set("INIT", unit.init);
    args.Set("RANK", unit.rank);
    args.Set("SKILL", unit.skill);

    RString varName = typeUnit.NextVarName();
    args.Set("VARIABLE_NAME", varName);

    if (unit.player == APPlayerCommander)
        args.Set("PLAYER", varName);
    if (unit.leader)
        args.Set("LEADER", grpName);

    typeUnit.WriteToScript(out, args);

    if (unit.id >= 0)
    {
        int index = vehicleMap.Add();
        vehicleMap[index].id = unit.id;
        vehicleMap[index].variable = varName;
    }
}

static void WriteUnit(QOStream& out, EditorObjectType& typeUnit, RString type, RString grpName, Vector3Par position,
                      RString name, Rank rank, float skill, RString vehicle, GetInPosition pos, bool player,
                      bool leader)
{
    // soldier in vehicle
    Arguments args;

    args.Set("PRESENCE_CONDITION", Format("not isNull %s", (const char*)vehicle));
    args.Set("POSITION", position);
    args.Set("GROUP", grpName);
    args.Set("TYPE", type);
    args.Set("PLACEMENT", 0);
    args.Set("SPECIAL", RString("NONE"));
    args.Set("AZIMUT", 0);
    args.Set("NAME", name);
    args.Set("HEALTH", 1);
    args.Set("AMMO", 1);
    args.Set("ID", -1);
    args.Set("AGE", RString("UNKNOWN"));
    args.Set("INIT", RString());
    args.Set("RANK", rank);
    args.Set("SKILL", skill);
    switch (pos)
    {
        case GIPCommander:
            args.Set("COMMANDER", vehicle);
            break;
        case GIPDriver:
            args.Set("DRIVER", vehicle);
            break;
        case GIPGunner:
            args.Set("GUNNER", vehicle);
            break;
        case GIPCargo:
            args.Set("CARGO", vehicle);
            break;
    }

    RString varName = typeUnit.NextVarName();
    args.Set("VARIABLE_NAME", varName);

    if (player)
        args.Set("PLAYER", varName);
    if (leader)
        args.Set("LEADER", grpName);

    typeUnit.WriteToScript(out, args);
}

static void ExportMission(ArcadeTemplate& t, RString filename)
{
    // Create templates
    EditorObjectType typeIntel("intel");
    EditorObjectType typeCenter("center");
    EditorObjectType typeMarker("marker");
    EditorObjectType typeTrigger("trigger");
    EditorObjectType typeGuardedPoint("guardedPoint");
    EditorObjectType typeSoundSource("soundSource");
    EditorObjectType typeMine("mine");
    EditorObjectType typeVehicle("vehicle");
    EditorObjectType typeUnit("unit");
    EditorObjectType typeGroup("group");
    EditorObjectType typeWaypoint("waypoint");

    // Translation
    AutoArray<CenterInfo> centerMap;
    AutoArray<MarkerInfo> markerMap;
    AutoArray<VehicleInfo> vehicleMap;
    AutoArray<SyncInfo> waypointMap;
    AutoArray<RString> groupMap;

    QOFStream out(filename);
    WriteLine(out, "/*" PARSING_COMMENT "_MISSION_EDITOR {*/");
    WriteLine(out, "/* DO NOT MANUALLY EDIT THIS CODE! */");
    WriteLine(out, "");

    // general init
    RString line = "activateAddons [";
    bool first = true;
    for (int i = 0; i < t.addOns.Size(); i++)
    {
        if (first)
            first = false;
        else
            line = line + RString(", ");
        line = line + RString("\"") + t.addOns[i] + RString("\"");
    }
    line = line + "];";
    if (!first)
        WriteLine(out, line);

    // intel
    {
        Arguments args;
        args.Set("OVERCAST", t.intel.weather);
        args.Set("OVERCAST_WANTED", t.intel.weatherForecast);
        args.Set("FOG", t.intel.fog);
        args.Set("FOG_WANTED", t.intel.fogForecast);
        args.Set("YEAR", t.intel.year);
        args.Set("MONTH", t.intel.month);
        args.Set("DAY", t.intel.day);
        args.Set("HOUR", t.intel.hour);
        args.Set("MINUTE", t.intel.minute);

        RString varName = typeIntel.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeIntel.WriteToScript(out, args);
    }
    WriteLine(out, "");

    // centers
    for (int i = 0; i < TSideUnknown; i++)
    {
        bool found = false;
        for (int j = 0; j < t.groups.Size(); j++)
        {
            if (t.groups[j].side == i)
            {
                found = true;
                break;
            }
        }
        if (!found)
            continue;

        Arguments args;
        args.Set("SIDE", (TargetSide)i);
        if (i == TCivilian)
        {
            args.Set("FRIEND_EAST", t.intel.friends[TGuerrila][TEast]);
            args.Set("FRIEND_WEST", t.intel.friends[TGuerrila][TWest]);
            args.Set("FRIEND_RESISTANCE", t.intel.friends[TGuerrila][TGuerrila]);
        }
        else
        {
            args.Set("FRIEND_EAST", t.intel.friends[i][TEast]);
            args.Set("FRIEND_WEST", t.intel.friends[i][TWest]);
            args.Set("FRIEND_RESISTANCE", t.intel.friends[i][TGuerrila]);
        }
        args.Set("FRIEND_CIVILIAN", 1.0f);

        RString varName = typeCenter.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeCenter.WriteToScript(out, args);

        int index = centerMap.Add();
        centerMap[index].side = (TargetSide)i;
        centerMap[index].variable = varName;
    }
    {
        // game logic
        bool found = false;
        for (int j = 0; j < t.groups.Size(); j++)
        {
            if (t.groups[j].side == TLogic)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            Arguments args;
            args.Set("SIDE", TLogic);
            args.Set("FRIEND_EAST", 1.0f);
            args.Set("FRIEND_WEST", 1.0f);
            args.Set("FRIEND_RESISTANCE", 1.0f);
            args.Set("FRIEND_CIVILIAN", 1.0f);

            RString varName = typeCenter.NextVarName();
            args.Set("VARIABLE_NAME", varName);

            typeCenter.WriteToScript(out, args);

            int index = centerMap.Add();
            centerMap[index].side = TLogic;
            centerMap[index].variable = varName;
        }
    }
    WriteLine(out, "");

    // markers
    for (int j = 0; j < t.markers.Size(); j++)
    {
        const ArcadeMarkerInfo& marker = t.markers[j];

        Arguments args;
        args.Set("POSITION", marker.position);
        args.Set("NAME", marker.name);
        args.Set("TEXT", marker.text);
        args.Set("MARKER_TYPE", marker.markerType);
        args.Set("TYPE", marker.type);
        args.Set("COLOR", marker.colorName);
        args.Set("FILL", marker.fillName);
        args.Set("A", marker.a);
        args.Set("B", marker.b);
        args.Set("ANGLE", marker.angle);

        RString varName = typeMarker.NextVarName();
        args.Set("VARIABLE_NAME", varName);

        typeMarker.WriteToScript(out, args);

        int index = markerMap.Add();
        markerMap[index].name = marker.name;
        markerMap[index].variable = varName;
    }
    WriteLine(out, "");

    // group units
    groupMap.Realloc(t.groups.Size());
    groupMap.Resize(t.groups.Size());
    RString player = "objNull";
    for (int i = 0; i < t.groups.Size(); i++)
    {
        const ArcadeGroupInfo& group = t.groups[i];

        Arguments args;
        for (int j = 0; j < centerMap.Size(); j++)
            if (centerMap[j].side == group.side)
            {
                args.Set("CENTER", centerMap[j].variable);
                break;
            }
        RString grpName = typeGroup.NextVarName();
        args.Set("VARIABLE_NAME", grpName);
        groupMap[i] = grpName;
        typeGroup.WriteToScript(out, args);

        // units
        for (int j = 0; j < group.units.Size(); j++)
        {
            const ArcadeUnitInfo& unit = group.units[j];
            if (unit.special == ASpCargo)
                continue;

            Ref<EntityType> type = VehicleTypes.New(unit.vehicle);
            TransportType* transportType = dynamic_cast<TransportType*>(type.GetRef());

            if (transportType)
            {
                RString vehName =
                    WriteVehicle(out, unit, typeSoundSource, typeMine, typeVehicle, markerMap, vehicleMap);

                // crew
                RString crewType = transportType->GetCrew();
                int commanderOffset = 1;
                int gunnerOffset = -1;
                // for vehicle where gunner is commander gunner rank
                // should be higher that driver
                bool driverIsCommander = transportType->DriverIsCommander();
                if (!driverIsCommander)
                {
                    commanderOffset = 2;
                    gunnerOffset = 1;
                }

                bool hasCommander = transportType->HasCommander();
                bool hasDriver = transportType->HasDriver();
                bool hasGunner = transportType->HasGunner();

                bool leaderCommander = false;
                bool leaderDriver = false;
                bool leaderGunner = false;
                if (unit.leader)
                {
                    if (hasCommander)
                        leaderCommander = true;
                    else if (driverIsCommander)
                    {
                        if (hasDriver)
                            leaderDriver = true;
                        else
                            leaderGunner = true;
                    }
                    else
                    {
                        if (hasGunner)
                            leaderGunner = true;
                        else
                            leaderDriver = true;
                    }
                }

                if (hasDriver)
                {
                    RString name;
                    if (unit.name.GetLength() > 0)
                        name = unit.name + RString("d");
                    WriteUnit(out, typeUnit, crewType, grpName, unit.position, name, unit.rank, unit.skill, vehName,
                              GIPDriver, unit.player == APPlayerDriver, leaderDriver);
                }

                if (hasCommander)
                {
                    int commanderRank = unit.rank + commanderOffset;
                    saturate(commanderRank, 0, NRanks - 1);
                    RString name;
                    if (unit.name.GetLength() > 0)
                        name = unit.name + RString("c");
                    WriteUnit(out, typeUnit, crewType, grpName, unit.position, name, (Rank)commanderRank, unit.skill,
                              vehName, GIPCommander, unit.player == APPlayerCommander, leaderCommander);
                }

                if (hasGunner)
                {
                    int gunnerRank = unit.rank + gunnerOffset;
                    saturate(gunnerRank, 0, NRanks - 1);
                    RString name;
                    if (unit.name.GetLength() > 0)
                        name = unit.name + RString("g");
                    WriteUnit(out, typeUnit, crewType, grpName, unit.position, name, (Rank)gunnerRank, unit.skill,
                              vehName, GIPGunner, unit.player == APPlayerGunner, leaderGunner);
                }
            }
            else if (dynamic_cast<ManType*>(type.GetRef()) || dynamic_cast<InvisibleVehicleType*>(type.GetRef()))
            {
                WriteUnit(out, grpName, unit, typeUnit, markerMap, vehicleMap);
            }
            else
            {
                LOG_ERROR(UI, "Unit in group is not Man nor Transport");
                WriteVehicle(out, unit, typeSoundSource, typeMine, typeVehicle, markerMap, vehicleMap);
            }
        }
        // units
        for (int j = 0; j < group.units.Size(); j++)
        {
            const ArcadeUnitInfo& unit = group.units[j];
            if (unit.special != ASpCargo)
                continue;

            Ref<EntityType> type = VehicleTypes.New(unit.vehicle);
            DoAssert(dynamic_cast<ManType*>(type.GetRef()));

            WriteUnit(out, grpName, unit, typeUnit, markerMap, vehicleMap);
        }
        WriteLine(out, "");
    }

    // empty vehicles
    for (int j = 0; j < t.emptyVehicles.Size(); j++)
        WriteVehicle(out, t.emptyVehicles[j], typeSoundSource, typeMine, typeVehicle, markerMap, vehicleMap);
    WriteLine(out, "");

    // group waypoints
    for (int i = 0; i < t.groups.Size(); i++)
    {
        const ArcadeGroupInfo& group = t.groups[i];
        RString grpName = groupMap[i];

        for (int j = 0; j < group.waypoints.Size(); j++)
        {
            const ArcadeWaypointInfo& waypoint = group.waypoints[j];
            Arguments args;
            args.Set("POSITION", waypoint.position);
            args.Set("PLACEMENT", waypoint.placement);
            args.Set("GROUP", grpName);
            args.Set("TYPE", waypoint.type);
            if (waypoint.id >= 0)
            {
                bool found = false;
                for (int k = 0; k < vehicleMap.Size(); k++)
                    if (waypoint.id == vehicleMap[k].id)
                    {
                        args.Set("VEHICLE", vehicleMap[k].variable);
                        found = true;
                        break;
                    }
                DoAssert(found);
            }
            args.Set("ID_STATIC", waypoint.idStatic);
            args.Set("HOUSE_POS", waypoint.housePos);
            args.Set("COMBAT_MODE", waypoint.combatMode);
            args.Set("FORMATION", waypoint.formation);
            args.Set("SPEED", waypoint.speed);
            args.Set("COMBAT", waypoint.combat);
            args.Set("DESCRIPTION", waypoint.description);
            args.Set("EXP_COND", waypoint.expCond);
            args.Set("EXP_ACTIV", waypoint.expActiv);
            args.Set("SCRIPT", waypoint.script);
            args.Set("TIMEOUT_MIN", waypoint.timeoutMin);
            args.Set("TIMEOUT_MID", waypoint.timeoutMid);
            args.Set("TIMEOUT_MAX", waypoint.timeoutMax);
            args.Set("SHOW", waypoint.showWP);
            WriteEffects(args, waypoint.effects);

            RString wpName = typeWaypoint.NextVarName();
            args.Set("VARIABLE_NAME", wpName);

            // synchronizations
            for (int s = 0; s < waypoint.synchronizations.Size(); s++)
            {
                int sync = waypoint.synchronizations[s];
                RString name;
                for (int k = 0; k < waypointMap.Size(); k++)
                {
                    if (sync == waypointMap[k].sync)
                    {
                        DoAssert(waypointMap[k].variable2.GetLength() == 0);
                        name = waypointMap[k].variable1;
                        waypointMap[k].variable2 = wpName;
                        break;
                    }
                }
                if (name.GetLength() == 0)
                {
                    int index = waypointMap.Add();
                    waypointMap[index].sync = sync;
                    waypointMap[index].variable1 = wpName;
                }
                else
                {
                    args.Set("SYNC", name);
                }
            }

            typeWaypoint.WriteToScript(out, args);
        }
    }

    // sensors
    for (int j = 0; j < t.sensors.Size(); j++)
        WriteTrigger(out, t.sensors[j], typeTrigger, typeGuardedPoint, waypointMap, vehicleMap);
    WriteLine(out, "");

    WriteLine(out, "/*" PARSING_COMMENT "} _MISSION_EDITOR*/");
    out.close();
}
#endif

void DisplayArcadeMap::OnSimulate(EntityAI* vehicle)
{
    DisplayMapEditor::OnSimulate(vehicle);
    _map->ProcessCheats();
#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_Z) && _currentTemplate)
        ExportMission(*_currentTemplate, GetMissionDirectory() + RString("mission.sqf"));
#endif
}

bool DisplayArcadeMap::OnUnregisteredAddonUsed(RString addon)
{
    _currentTemplate->addOns.AddUnique(addon);
    return true;
}

void EnableDesktopCursor(bool enable);

// "Send mission by e-mail" was a 2001-era map-editor convenience built on
// MAPI. No modern Windows user has a working MAPI client set up, and the
// feature isn't reachable from the UI in normal play. The menu entry is
// kept wired (see OnChildDestroyed case 3 below) but the send is now a
// no-op that only logs a warning.
static void SendViaEmail(const char* /*fileName*/)
{
    Poseidon::Foundation::WarningMessage("Mission send-by-email is no longer supported");
}

void DisplayArcadeMap::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_INTEL_GETREADY:
        {
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            if (exit != IDC_CANCEL)
            {
                CreateChild(new DisplayMission(this));
            }
        }
        break;
        case IDD_MISSION:
        {
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            ParamEntry* entry = ExtParsMission.FindEntry("debriefing");
            if (entry && !(bool)(*entry))
            {
                GStats.Update();
                goto StartOutro;
            }
            else
            {
                CreateChild(new DisplayDebriefing(this, false));
            }
        }
        break;
        case IDD_DEBRIEFING:
        case IDD_INTRO:
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            GWorld->DestroyMap(IDC_OK);
        StartOutro:
            _alwaysShow = true;
            ShowButtons();
            break;
        case IDD_ARCADE_UNIT:
            if (exit == IDC_OK)
            {
                DisplayArcadeUnit* display = dynamic_cast<DisplayArcadeUnit*>((ControlsContainer*)_child);
                PoseidonAssert(display);

                int ig = display->_indexGroup;
                int iu = display->_index;

                _lastUnit = display->_unit;

                _currentTemplate->ClearSelection();
                display->_unit.selected = true;
                _currentTemplate->UnitUpdate(ig, iu, display->_unit);

                _currentTemplate->IsConsistent(this, _multiplayer);
                ShowButtons();

                _map->_infoClick._type = signArcadeUnit;
                _map->_infoClick._indexGroup = ig;
                _map->_infoClick._index = iu;
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_ARCADE_GROUP:
            if (exit == IDC_OK)
            {
                DisplayArcadeGroup* display = dynamic_cast<DisplayArcadeGroup*>((ControlsContainer*)_child);
                PoseidonAssert(display);

                Vector3Val position = display->_position;

                float azimut = 0;
                CEdit* edit = dynamic_cast<CEdit*>(display->GetCtrl(IDC_ARCGRP_AZIMUT));
                if (edit)
                {
                    azimut = edit->GetText() ? atof(edit->GetText()) : 0;
                }

                CCombo* combo = dynamic_cast<CCombo*>(display->GetCtrl(IDC_ARCGRP_SIDE));
                if (combo && combo->GetCurSel() >= 0)
                {
                    RString side = combo->GetData(combo->GetCurSel());

                    combo = dynamic_cast<CCombo*>(display->GetCtrl(IDC_ARCGRP_TYPE));
                    if (combo && combo->GetCurSel() >= 0)
                    {
                        RString type = combo->GetData(combo->GetCurSel());

                        combo = dynamic_cast<CCombo*>(display->GetCtrl(IDC_ARCGRP_NAME));
                        if (combo && combo->GetCurSel() >= 0)
                        {
                            RString name = combo->GetData(combo->GetCurSel());

                            _currentTemplate->ClearSelection();
                            _currentTemplate->AddGroup(Pars >> "CfgGroups" >> side >> type >> name, position);
                            int index = _currentTemplate->groups.Size() - 1;
                            _currentTemplate->groups[index].Rotate(position, (H_PI / 180.0f) * azimut, false);

                            _map->SetLastGroup(side, type, name);

                            _currentTemplate->IsConsistent(this, _multiplayer);
                            ShowButtons();

                            _map->_infoClick._type = signNone;
                            _map->_infoClickCandidate._type = signNone;
                        }
                    }
                }
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_ARCADE_SENSOR:
            if (exit == IDC_OK)
            {
                DisplayArcadeSensor* display = dynamic_cast<DisplayArcadeSensor*>((ControlsContainer*)_child);
                PoseidonAssert(display);

                _currentTemplate->ClearSelection();
                display->_sensor.selected = true;
                _currentTemplate->SensorUpdate(display->_ig, display->_index, display->_sensor);

                _map->_infoClick._type = signArcadeSensor;
                _map->_infoClick._indexGroup = display->_ig;
                _map->_infoClick._index = display->_index;
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_ARCADE_MARKER:
            if (exit == IDC_OK)
            {
                DisplayArcadeMarker* display = dynamic_cast<DisplayArcadeMarker*>((ControlsContainer*)_child);
                PoseidonAssert(display);

                _currentTemplate->ClearSelection();
                display->_marker.selected = true;
                _currentTemplate->MarkerUpdate(display->_index, display->_marker);

                _map->_infoClick._type = signArcadeMarker;
                _map->_infoClick._index = display->_index;
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_ARCADE_WAYPOINT:
            if (exit == IDC_OK)
            {
                DisplayArcadeWaypoint* display = dynamic_cast<DisplayArcadeWaypoint*>((ControlsContainer*)_child);
                PoseidonAssert(display);

                int ig = display->_indexGroup;
                int iw = display->_index;

                int iwnew = iw < 0 ? _currentTemplate->groups[ig].waypoints.Size() : iw;
                CCombo* combo = dynamic_cast<CCombo*>(display->GetCtrl(IDC_ARCWP_SEQ));
                if (combo)
                {
                    iwnew = combo->GetCurSel();
                    if (iw < 0 && iwnew < 0)
                    {
                        iwnew = combo->GetSize() - 1;
                    }
                }

                _currentTemplate->ClearSelection();
                display->_waypoint.selected = true;
                _currentTemplate->WaypointUpdate(ig, iw, iwnew, display->_waypoint);

                _map->_infoClick._type = signArcadeWaypoint;
                _map->_infoClick._indexGroup = ig;
                _map->_infoClick._index = iwnew;
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_TEMPLATE_SAVE:
            if (exit == IDC_OK)
            {
                CEdit* edit = dynamic_cast<CEdit*>(_child->GetCtrl(IDC_TEMPL_NAME));
                PoseidonAssert(edit);
                RString text = edit->GetText();
                if (text.GetLength() > 0)
                {
                    if (_multiplayer)
                    {
                        SetMission(Glob.header.worldname, text, GetMPMissionsDir());
                    }
                    else
                    {
                        SetMission(Glob.header.worldname, text, GetMissionsDir());
                    }

                    RString directory;
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)GetMissionsDirectory());
                    CreateDirectoryUtf8(buffer);
                    strncat(buffer, text, sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, ".", sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, Glob.header.worldname, sizeof(buffer) - strlen(buffer) - 1);
                    directory = buffer;
                    CreateDirectoryUtf8(directory);
                    strncat(buffer, "\\mission.sqm", sizeof(buffer) - strlen(buffer) - 1);
                    SaveTemplates(buffer);

                    CCombo* combo = dynamic_cast<CCombo*>(_child->GetCtrl(IDC_TEMPL_MODE));
                    PoseidonAssert(combo);
                    switch (combo->GetCurSel())
                    {
                        case 0:
                            // user mission
                            break;
                        case 1:
                            // single mission
                            {
                                RString fileName = RString("Missions\\") + text + RString(".") +
                                                   RString(Glob.header.worldname) + RString(".pbo");
                                DeleteROFile(fileName);
                                FileBankManager mgr;
                                mgr.Create(fileName, directory, true);
                            }
                            break;
                        case 2:
                            // multiplayer mission
                            {
                                RString fileName = RString(GameDirs::MPMissionsPath().c_str()) + text + RString(".") +
                                                   RString(Glob.header.worldname) + RString(".pbo");
                                DeleteROFile(fileName);
                                FileBankManager mgr;
                                mgr.Create(fileName, directory, true);
                            }
                            break;
                        case 3:
                            // send by e-mail
                            {
                                // create file
                                RString fileName = directory + RString(".pbo");
                                DeleteROFile(fileName);
                                FileBankManager mgr;
                                mgr.Create(fileName, directory, true);

                                SendViaEmail(fileName);
                                DeleteROFile(fileName);
                            }
                            break;
                    }
                    ShowButtons();
                }
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_TEMPLATE_LOAD:
            if (exit == IDC_OK)
            {
                CCombo* combo = dynamic_cast<CCombo*>(_child->GetCtrl(IDC_TEMPL_ISLAND));
                PoseidonAssert(combo);
                int index = combo->GetCurSel();
                PoseidonAssert(index >= 0);
                RString island = combo->GetData(index);
                combo = dynamic_cast<CCombo*>(_child->GetCtrl(IDC_TEMPL_NAME));
                PoseidonAssert(combo);
                int i = combo->GetCurSel();
                if (i >= 0)
                {
                    RString name = combo->GetText(i);
                    RString filename = GetMissionsDirectory() + name + RString(".") + island + RString("\\mission.sqm");
                    DisplayTemplateLoad* display = dynamic_cast<DisplayTemplateLoad*>((ControlsContainer*)_child);
                    PoseidonAssert(display);
                    if (display->_merge)
                    {
                        MergeTemplates(filename);
                    }
                    else
                    {
                        if (stricmp(island, Glob.header.worldname) != 0)
                        {
                            GWorld->SwitchLandscape(GetWorldName(island));
                            _map->CreateObjectList();
                        }

                        if (_multiplayer)
                        {
                            SetMission(island, name, GetMPMissionsDir());
                        }
                        else
                        {
                            SetMission(island, name, GetMissionsDir());
                        }
                        LoadTemplates(filename);
                        ArcadeUnitInfo* uInfo = _currentTemplate->FindPlayer();
                        if (uInfo)
                        {
                            Glob.header.playerSide = (TargetSide)uInfo->side;
                        }
                    }
                    if (_map)
                    {
                        _map->SetScale(-1); // default
                        _map->Center();
                        _map->Reset();
                    }
                }
                ShowButtons();
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_INTEL:
            if (exit == IDC_OK)
            {
                DisplayIntel* display = dynamic_cast<DisplayIntel*>((ControlsContainer*)_child);
                PoseidonAssert(display);
                _currentTemplate->intel = display->_intel;
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_MSG_CLEARTEMPLATE:
            if (exit == IDC_OK)
            {
                _currentTemplate->Clear();
                if (_multiplayer)
                {
                    SetMission(Glob.header.worldname, "", GetMPMissionsDir());
                }
                else
                {
                    SetMission(Glob.header.worldname, "", GetMissionsDir());
                }
                if (_map)
                {
                    _map->_infoClick._type = signNone;
                    _map->_infoClickCandidate._type = signNone;
                    _map->_infoMove._type = signNone;
                }
                ShowButtons();
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        case IDD_MSG_EXITTEMPLATE:
            if (exit == IDC_OK)
            {
                Exit(IDC_CANCEL);
            }
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
        default:
            DisplayMapEditor::OnChildDestroyed(idd, exit);
            break;
    }
}

bool DisplayArcadeMap::CanDestroy()
{
    if (!DisplayMapEditor::CanDestroy())
    {
        return false;
    }

    if (_exit == IDC_OK)
    {
        return _templateMission.IsConsistent(this, _multiplayer);
    }
    else
    {
        return true;
    }
}

void DisplayArcadeMap::Destroy()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
    DisplayMapEditor::Destroy();
    //	GWorld->EnableDisplay(true);
}

void DisplayArcadeMap::RefreshLanguage()
{
    // State-driven labels: re-derive text from current mode.
    SetAdvancedMode(_advanced);
    UpdateIdsButton();
    UpdateTexturesButton();

    // Walk every child control and call ReloadLocalizedText. Controls that
    // cache `text="$STR_..."` (CStatic, CButton, CActiveText, C3DStatic) and
    // the per-cell `strings[]` of a CToolBox re-read from their stored
    // ParamEntry, which transparently re-resolves $STR_* against the new
    // language column. Covers the top mode bar (ToolboxMode) and every
    // resource-defined button without per-control wiring.
    RefreshLocalizedText();

    // Mission / Intro / Outro selector: filled programmatically in
    // OnCreateCtrl (no ParamEntry-backed text to re-resolve). Clear and
    // re-add so the labels follow the new language. The combo carries no
    // per-row data — selection is by index only.
    if (CCombo* combo = dynamic_cast<CCombo*>(GetCtrl(IDC_ARCMAP_SECTION)))
    {
        const int sel = combo->GetCurSel();
        combo->ClearStrings();
        combo->AddString(LocalizeString(IDS_SECTION_MISSION));
        combo->AddString(LocalizeString(IDS_SECTION_INTRO));
        combo->AddString(LocalizeString(IDS_SECTION_OUTRO_WIN));
        combo->AddString(LocalizeString(IDS_SECTION_OUTRO_LOOSE));
        combo->SetCurSel(sel >= 0 ? sel : 0);
    }
}
} // namespace Poseidon
