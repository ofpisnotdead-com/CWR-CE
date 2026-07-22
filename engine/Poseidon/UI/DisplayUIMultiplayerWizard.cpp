#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Filesystem/DirTree.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <Poseidon/UI/OptionsUICommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Core/Global.hpp>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <direct.h>
#endif
#include <sys/stat.h>

namespace Poseidon
{

// Multiplayer wizard displays

RString CreateSingleMissionBank(RString filename);

namespace
{

void AddWizardTemplate(C3DListBox* lbox, const MissionTemplateEntry& templ)
{
    int index = lbox->AddString(GetMissionTemplateSelectorText(templ));
    lbox->SetData(index, templ.name);
    lbox->SetValue(index, templ.bank ? 1 : 0);
}

void PopulateWizardTemplateList(C3DListBox* lbox, bool multiplayer, RString world)
{
    AutoArray<MissionTemplateEntry> templates;
    ListMissionTemplates(templates, multiplayer, world);
    for (int i = 0; i < templates.Size(); ++i)
        AddWizardTemplate(lbox, templates[i]);
}

} // namespace

DisplayWizardTemplate::DisplayWizardTemplate(ControlsContainer* parent, RString world, bool multiplayer)
    : Display(parent)
{
    _enableSimulation = false;
    _world = world;
    _multiplayer = multiplayer;
    Load("RscDisplayWizardTemplate");

    OnTemplateChanged();

    if (_langCbToken >= 0)
        UnregisterLanguageChangedCallback(_langCbToken);
    _langCbToken = RegisterLanguageChangedCallback(
        [this]()
        {
            RefreshLocalizedText();
            RefreshLanguage();
        });
}

Control* DisplayWizardTemplate::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_WIZT_TEMPLATES:
        {
            C3DListBox* lbox = new C3DListBox(this, idc, cls);
            PopulateWizardTemplateList(lbox, _multiplayer, _world);
            lbox->SetCurSel(0);
            return lbox;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayWizardTemplate::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_WIZT_TEMPLATES)
    {
        OnTemplateChanged();
    }
    else
    {
        Display::OnLBSelChanged(idc, curSel);
    }
}

void DisplayWizardTemplate::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_WIZT_TEMPLATES)
    {
        OnButtonClicked(IDC_OK);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplayWizardTemplate::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_WIZT_TEMPLATES));
            if (!lbox)
            {
                break;
            }

            int index = lbox->GetCurSel();
            if (index < 0)
            {
                break;
            }
            RString t = lbox->GetData(index);
            bool bank = lbox->GetValue(index) == 1;

            C3DEdit* edit = dynamic_cast<C3DEdit*>(GetCtrl(IDC_WIZT_NAME));
            if (!edit)
            {
                break;
            }

            RString name = edit->GetText();
            if (name.GetLength() == 0)
            {
                CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MPW_NONAME));
                return;
            }

            // _world, t, bank, name are valid now
            CreateChild(new DisplayWizardMap(this, _world, t, bank, name, _multiplayer));
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayWizardTemplate::OnChildDestroyed(int idd, int exit)
{
    Display::OnChildDestroyed(idd, exit);
    if (idd == IDD_WIZARD_MAP && exit == IDC_OK)
    {
        Exit(IDC_OK);
    }
}

void DisplayWizardTemplate::OnTemplateChanged()
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_WIZT_TEMPLATES));
    if (!lbox)
    {
        return;
    }

    int index = lbox->GetCurSel();
    if (index < 0)
    {
        return;
    }
    RString t = lbox->GetData(index);
    bool bank = lbox->GetValue(index) == 1;

    C3DHTML* html = dynamic_cast<C3DHTML*>(GetCtrl(IDC_WIZT_OVERVIEW));
    if (!html)
    {
        return;
    }
    html->Init();

    RString dir;
    if (bank)
    {
        dir = CreateSingleMissionBank(GetMissionTemplateBasePath(_multiplayer, t, _world));
    }
    else
    {
        dir = GetMissionTemplateBasePath(_multiplayer, t, _world) + RString("\\");
    }

    RString GetOverviewFile(RString dir);
    RString filename = GetOverviewFile(dir);
    LoadLocalizedMissionHtml(html, filename);
}

void DisplayWizardTemplate::RefreshLanguage()
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_WIZT_TEMPLATES));
    if (lbox)
    {
        RString selected;
        int selection = lbox->GetCurSel();
        if (selection >= 0)
            selected = lbox->GetData(selection);

        lbox->ClearStrings();
        PopulateWizardTemplateList(lbox, _multiplayer, _world);
        for (int i = 0; i < lbox->GetSize(); ++i)
        {
            if (lbox->GetData(i) == selected)
            {
                lbox->SetCurSel(i);
                break;
            }
        }
    }
    OnTemplateChanged();
}

CStaticMapWizard::CStaticMapWizard(ControlsContainer* parent, int idc, const ParamEntry& cls, float scaleMin,
                                   float scaleMax, float scaleDefault)
    : CStaticMap(parent, idc, cls, scaleMin, scaleMax, scaleDefault)
{
}

static const char* WizVarPrefix = "WIZVAR_";
static bool HasWizVarPrefix(const char* name)
{
    return strnicmp(name, WizVarPrefix, strlen(WizVarPrefix)) == 0;
}

SignInfo CStaticMapWizard::FindSign(float x, float y)
{
    struct SignInfo info;
    info._type = signNone;
    info._unit = nullptr;
    info._id = nullptr;

    Point3 pt = ScreenToWorld(DrawCoord(x, y));
    const float sizeLand = LandGrid * LandRange;
    float dist, minDist = 0.02 * sizeLand * _scaleX;
    minDist *= minDist;

    // markers
    int n = _template->markers.Size();
    for (int i = 0; i < n; i++)
    {
        ArcadeMarkerInfo& mInfo = _template->markers[i];
        if (!HasWizVarPrefix(mInfo.name))
        {
            continue;
        }
        dist = (pt - mInfo.position).SquareSizeXZ();
        if (dist < minDist)
        {
            minDist = dist;
            info._type = signArcadeMarker;
            info._index = i;
        }
    }

    return info;
}

void CStaticMapWizard::OnLButtonDown(float x, float y)
{
    SignInfo info = FindSign(x, y);
    _infoClickCandidate = info;
    _lastPos = ScreenToWorld(DrawCoord(x, y));
}

void CStaticMapWizard::OnLButtonClick(float x, float y)
{
    auto& input = InputSubsystem::Instance();
    switch (_infoClickCandidate._type)
    {
        case signArcadeMarker:
            _infoClick = _infoClickCandidate;
            _dragging = input.IsMouseLeftDown();
            _selecting = false;
            {
                ArcadeMarkerInfo& mInfo = _template->markers[_infoClickCandidate._index];

                if (!_dragging || !mInfo.selected)
                {
                    // change selection
                    if (!input.IsKeyDown(SDL_SCANCODE_LCTRL) && !input.IsKeyDown(SDL_SCANCODE_RCTRL))
                    {
                        _template->ClearSelection();
                    }
                    mInfo.selected = !mInfo.selected;
                }
            }
            break;
        case signNone:
            if (input.IsMouseLeftDown())
            {
                _dragging = false;
                _selecting = true;
                _special = _lastPos;
            }
            break;
    }
}

void CStaticMapWizard::OnLButtonUp(float x, float y)
{
    if (_dragging)
    {
        _infoClick._type = signNone;
        _infoClickCandidate._type = signNone;
        _infoMove._type = signNone;
        _dragging = false;
    }
    else if (_selecting)
    {
        _selecting = false;
        if (!InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LCTRL) &&
            !InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RCTRL))
        {
            _template->ClearSelection();
        }

        float xMin = floatMin(_special.X(), _lastPos.X());
        float xMax = floatMax(_special.X(), _lastPos.X());
        float zMin = floatMin(_special.Z(), _lastPos.Z());
        float zMax = floatMax(_special.Z(), _lastPos.Z());

        for (int i = 0; i < _template->markers.Size(); i++)
        {
            ArcadeMarkerInfo& info = _template->markers[i];
            if (!HasWizVarPrefix(info.name))
            {
                continue;
            }
            if (info.position.X() >= xMin && info.position.X() < xMax && info.position.Z() >= zMin &&
                info.position.Z() < zMax)
            {
                info.selected = !info.selected;
            }
        }
    }
}

void CStaticMapWizard::OnMouseHold(float x, float y, bool active)
{
    if (_dragging)
    {
        Vector3 curPos = ScreenToWorld(DrawCoord(x, y));

        if (InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT) ||
            InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_RSHIFT))
        {
            // rotate
            Vector3 sum = VZero;
            int count = 0;
            _template->CalculateCenter(sum, count, true);
            if (count > 0)
            {
                Vector3 center = (1.0f / count) * sum;
                Vector3 oldDir = _lastPos - center;
                Vector3 dir = curPos - center;
                float angle = AngleDifference(atan2(dir.X(), dir.Z()), atan2(oldDir.X(), oldDir.Z()));
                _template->Rotate(center, angle, true);
            }
        }
        else
        {
            Vector3 offset = curPos - _lastPos;
            // move all selected items by offset
            for (int i = 0; i < _template->markers.Size(); i++)
            {
                ArcadeMarkerInfo& mInfo = _template->markers[i];
                if (!HasWizVarPrefix(mInfo.name))
                {
                    continue;
                }
                if (mInfo.selected)
                {
                    Vector3 pos = mInfo.position + offset;
                    pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
                    _template->MarkerChangePosition(i, pos);
                }
            }
        }

        _lastPos = curPos;
    }
    else if (_selecting)
    {
        _lastPos = ScreenToWorld(DrawCoord(x, y));
    }
}

void CStaticMapWizard::DrawExt(float alpha)
{
    CStaticMap::DrawExt(alpha);

    DrawMarkers();

    DrawLabel(_infoMove, _colorInfoMove);

    if (_selecting)
    {
        PackedColor color = PackedColor(Color(0, 1, 0, 1));
        DrawCoord pt1 = WorldToScreen(_special);
        DrawCoord pt2 = WorldToScreen(_lastPos);
        GEngine->DrawLine(Line2DPixel(pt1.x * _wScreen, pt1.y * _hScreen, pt2.x * _wScreen, pt1.y * _hScreen), color,
                          color, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt2.x * _wScreen, pt1.y * _hScreen, pt2.x * _wScreen, pt2.y * _hScreen), color,
                          color, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt2.x * _wScreen, pt2.y * _hScreen, pt1.x * _wScreen, pt2.y * _hScreen), color,
                          color, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt1.x * _wScreen, pt2.y * _hScreen, pt1.x * _wScreen, pt1.y * _hScreen), color,
                          color, _clipRect);
    }
}

void CStaticMapWizard::DrawMarkers()
{
    int n = _template->markers.Size();
    for (int i = 0; i < n; i++)
    {
        ArcadeMarkerInfo& mInfo = _template->markers[i];
        if (!HasWizVarPrefix(mInfo.name))
        {
            continue;
        }

        PackedColor color = mInfo.color;
        if (!mInfo.selected)
        {
            color.SetA8(color.A8() / 2);
        }

        switch (mInfo.markerType)
        {
            case MTIcon:
            {
                float size = mInfo.size;
                if (size == 0)
                {
                    size = 32;
                }
                if (!mInfo.icon)
                {
                    break;
                }
                DrawSign(mInfo.icon, color, mInfo.position, size * mInfo.a, size * mInfo.b,
                         mInfo.angle * (H_PI / 180.0), Localize(mInfo.text));
            }
            break;
            case MTRectangle:
                FillRectangle(mInfo.position, mInfo.a, mInfo.b, mInfo.angle, color, mInfo.fill);
                break;
            case MTEllipse:
                FillEllipse(mInfo.position, mInfo.a, mInfo.b, mInfo.angle, color, mInfo.fill);
                break;
        }
    }
}

void CStaticMapWizard::DrawLabel(struct SignInfo& info, PackedColor color)
{
    RString text;
    Point3 pos;
    switch (info._type)
    {
        case signNone:
            break;
        case signArcadeMarker:
        {
            ArcadeMarkerInfo& mInfo = _template->markers[info._index];
            text = Localize(mInfo.text);
            if (text.GetLength() == 0)
            {
                text = LocalizeString(IDS_CFG_MARKERS_MARKER);
            }
            pos = mInfo.position;
            break;
        }
        break;
    }

    if (text.GetLength() > 0)
    {
        DrawCoord posMap = WorldToScreen(pos);

        // place label
        float w = GLOB_ENGINE->GetTextWidth(_sizeLabel, _fontLabel, text);
        float h = _sizeLabel;
        posMap.x -= 0.5 * w;
        posMap.y -= 0.01 + h;

        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        GLOB_ENGINE->Draw2D(mip, _colorLabelBackground,
                            Rect2DPixel(posMap.x * _wScreen, posMap.y * _hScreen, w * _wScreen, h * _hScreen),
                            _clipRect);

        GLOB_ENGINE->DrawText(posMap, _sizeLabel, Rect2DFloat(_x, _y, _w, _h), _fontLabel, color, text);
    }
}

void CStaticMapWizard::Center()
{
    Point3 pt = _defaultCenter;
    int n = _template->markers.Size();
    for (int i = 0; i < n; i++)
    {
        ArcadeMarkerInfo& mInfo = _template->markers[i];
        if (HasWizVarPrefix(mInfo.name))
        {
            pt = mInfo.position;
            break;
        }
    }

    CStaticMap::Center(pt);
}

DisplayWizardMap::DisplayWizardMap(ControlsContainer* parent, RString world, RString t, bool bank, RString name,
                                   bool multiplayer)
    : Display(parent)
{
    _enableSimulation = false;
    _world = world;
    _t = t;
    _bank = bank;
    _name = name;
    _multiplayer = multiplayer;
    _map = nullptr;

    _cursor = CursorArrow;

    CurrentCampaign = "";
    CurrentBattle = "";
    CurrentMission = "";

    SetBaseDirectory("");
    RString dir;
    RString mission;
    if (bank)
    {
        RString filename;
        if (_multiplayer)
        {
            filename = RString("Templates\\") + t + RString(".") + world;
        }
        else
        {
            filename = RString("SPTemplates\\") + t + RString(".") + world;
        }
        CreateSingleMissionBank(filename);
        dir = "missions\\";
        mission = "__cur_sp";
    }
    else
    {
        if (_multiplayer)
        {
            dir = "Templates\\";
        }
        else
        {
            dir = "SPTemplates\\";
        }
        mission = t;
    }

    ::SetMission(world, mission, dir);

    GLOB_WORLD->SwitchLandscape(GetWorldName(world));

    RString filename = dir + mission + RString(".") + world + RString("\\mission.sqm");
    ParamArchiveLoad ar(filename);
    ATSParams params;
    ar.SetParams(&params);
    if (ar.Serialize("Mission", _mission, 1) != TMOK)
    {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
    }

    Load("RscDisplayWizardMap");

    // setting default values
    if (_map)
    {
        _map->SetScale(-1);
        _map->Center();
        _map->Reset();
    }
}

Control* DisplayWizardMap::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MAP:
            _map = new CStaticMapWizard(this, idc, cls, 0.001, 0.5, 0.16);
            _map->SetTemplate(&_mission);
            return _map;
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

struct SaveContext
{
    QFBank* bank;
    RString folder;
};

static void SaveFile(const FileInfoO& fi, const FileBankType* files, void* context)
{
    SaveContext* sc = (SaveContext*)context;
    // save given file
    QIFStreamB file;
    file.open(*sc->bank, fi.name);
    // note: file may be compressed - auto handled by bank

    std::string outFilename = std::string((const char*)sc->folder) + PATH_SEP_STR + std::string(fi.name);
    platformPath(outFilename);
    // note file name may contain subfolder
    char folderName[1024];
    snprintf(folderName, sizeof(folderName), "%s", outFilename.c_str());
    *GetFilenameExt(folderName) = 0;
    if (strlen(folderName) > 0)
    {
        if (folderName[strlen(folderName) - 1] == PATH_SEP)
        {
            folderName[strlen(folderName) - 1] = 0;
        }
        // FIX: create whole path (not only topmost directory)
        CreatePath(folderName);
        ::CreateDirectory(folderName, nullptr);
    }
    //
    FILE* tgt = fopen(outFilename.c_str(), "wb");
    if (!tgt)
    {
        return;
    }

    setvbuf(tgt, nullptr, _IOFBF, 256 * 1024);
    fwrite(file.act(), file.rest(), 1, tgt);

    fclose(tgt);
}

static void ExtractBank(RString dst, RString src)
{
    QFBank bank;
    bank.open(src);

    // enumerate files in bank
    SaveContext saveContext;
    saveContext.bank = &bank;
    saveContext.folder = dst;
    bank.ForEach(SaveFile, &saveContext);
}

bool DisplayWizardMap::CreateMission()
{
    RString src;
    RString dst;
    if (_multiplayer)
    {
        src = RString("Templates\\") + _t + RString(".") + _world;
        dst = GetUserMissionsBase() + RString(GameDirs::MPMissionsPath().c_str()) + _name + RString(".") + _world;
    }
    else
    {
        src = RString("SPTemplates\\") + _t + RString(".") + _world;
        dst = GetUserMissionsBase() + RString("missions/") + _name + RString(".") + _world;
    }
    DeleteDirectoryStructure(dst, false);
    CreatePath(dst + RString("\\"));
    if (_bank)
    {
        ExtractBank(dst, src);
    }
    else
    {
        CopyDirectoryStructure(dst, src);
    }

    ArcadeTemplate mission;
    ArcadeTemplate intro;
    ArcadeTemplate outroWin;
    ArcadeTemplate outroLoose;
    RString filename = dst + RString("\\mission.sqm");
    if (!QIFStream::FileExists(filename))
    {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
        return false;
    }

    for (int i = 0; i < _mission.markers.Size(); i++)
    {
        ArcadeMarkerInfo& marker = _mission.markers[i];
        if (HasWizVarPrefix(marker.name))
        {
            static RString suffixX("_X");
            static RString suffixY("_Y");
            static RString suffixZ("_Z");

            GGameState.VarSet(marker.name + suffixX, GameValue(marker.position[0]));
            GGameState.VarSet(marker.name + suffixY, GameValue(marker.position[1]));
            GGameState.VarSet(marker.name + suffixZ, GameValue(marker.position[2]));
        }
    }

    // load mission
    {
        ParamArchiveLoad ar(filename);
        ATSParams params;
        ar.SetParams(&params);
        if (ar.Serialize("Mission", mission, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("Intro", intro, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("OutroWin", outroWin, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("OutroLoose", outroLoose, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LOAD_TEMPL_FAIL));
            return false;
        }
    }

    // remove markers
    for (int i = 0; i < mission.markers.Size();)
    {
        ArcadeMarkerInfo& mDst = mission.markers[i];
        if (HasWizVarPrefix(mDst.name))
        {
            mission.markers.Delete(i);
        }
        else
        {
            i++;
        }
    }

    // save mission
    {
        chmod(filename, _S_IREAD | _S_IWRITE);
        ParamArchiveSave ar(MissionsVersion);
        ATSParams params;
        ar.SetParams(&params);
        if (ar.Serialize("Mission", mission, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SAVE_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("Intro", intro, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SAVE_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("OutroWin", outroWin, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SAVE_TEMPL_FAIL));
            return false;
        }
        if (ar.Serialize("OutroLoose", outroLoose, 1) != TMOK)
        {
            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SAVE_TEMPL_FAIL));
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
    }

    return true;
}

void DisplayWizardMap::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_OK:
            if (CreateMission())
            {
                // set mission
                RString userDir = GetUserMissionsBase();
                SetBaseDirectory(userDir);
                if (_multiplayer)
                {
                    GetNetworkManager().CreateMission("", "");
                    ::SetMission(_world, _name, GetMPMissionsDir());
                }
                else
                {
                    ::SetMission(_world, _name, GetMissionsDir());
                }

                Exit(IDC_OK);
            }
            else
            {
                Exit(IDC_CANCEL);
            }
            break;
        case IDC_WIZM_EDIT:
            if (CreateMission())
            {
                // set mission
                RString userDir = GetUserMissionsBase();
                SetBaseDirectory(userDir);
                if (_multiplayer)
                {
                    ::SetMission(_world, _name, GetMPMissionsDir());
                }
                else
                {
                    ::SetMission(_world, _name, GetMissionsDir());
                }

                CreateEditor(this, _multiplayer);
            }
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayWizardMap::OnSimulate(EntityAI* vehicle)
{
    Display::OnSimulate(vehicle);
    if (_map && _map->IsVisible())
    {
        _map->ProcessCheats();

        float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
        float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;

        // automatic map movement on edges
        float dif = 0.02 - mouseX;
        if (dif > 0)
        {
            _map->ScrollX(0.003 * exp(dif * 100.0f));
        }
        else
        {
            dif = mouseX - 0.98;
            if (dif > 0)
            {
                _map->ScrollX(-0.003 * exp(dif * 100.0f));
            }
        }

        dif = 0.02 - mouseY;
        if (dif > 0)
        {
            _map->ScrollY(0.003 * exp(dif * 100.0f));
        }
        else
        {
            dif = mouseY - 0.98;
            if (dif > 0)
            {
                _map->ScrollY(-0.003 * exp(dif * 100.0f));
            }
        }

        IControl* ctrl = GetCtrl(mouseX, mouseY);
        if (ctrl && ctrl == _map)
        {
            if (_map->_dragging || _map->_selecting)
            {
                if (_cursor != CursorMove)
                {
                    _cursor = CursorMove;
                    SetCursor("Move");
                }
            }
            else if (_map->_moving)
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
}

void DisplayWizardMap::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_ARCADE_MAP:
            Display::OnChildDestroyed(idd, exit);
            //			if (exit == IDC_OK)
            {
                RString userDir = GetUserMissionsBase();
                SetBaseDirectory(userDir);
                if (_multiplayer)
                {
                    GetNetworkManager().CreateMission("", "");
                    ::SetMission(_world, _name, GetMPMissionsDir());
                }
                else
                {
                    ::SetMission(_world, _name, GetMissionsDir());
                }

                OnButtonClicked(IDC_OK);
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

} // namespace Poseidon
