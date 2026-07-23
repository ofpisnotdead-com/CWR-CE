#include <Poseidon/UI/Map/UIMap.hpp>
using namespace Poseidon;
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Strings/LocaleCollate.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/MasterServerBrowser.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <Poseidon/UI/OptionsUICommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>
#include <Poseidon/Core/ModId.hpp>
#include <Poseidon/Core/ServerModResolve.hpp>
#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/PendingConnect.hpp>
#include <Poseidon/Core/Application.hpp>
#include "ModDownloadSupport.hpp"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <sys/stat.h>

// Defined at global scope in Network/NetworkMisc.cpp.
void CheckMPVersion(SessionInfo& info);

namespace Poseidon
{
using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::QSort;

// Multiplayer display

void __cdecl CreateServer();

void __cdecl CreateServer()
{
    int port = GetNetworkPort();
    RString password = GetNetworkPassword();
    GetNetworkManager().Init("", port, false);
    GetNetworkManager().CreateSession(port, password);

    // Dedicated server: no display available, just create the network session
    if (ENGINE_CONFIG.doCreateDedicatedServer)
        return;

    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
        return;

    if (GetNetworkManager().IsServer())
    {
        options->CreateChild(new DisplayServer(options));
    }
}

void __cdecl CreateClient(RString ip, int port, RString password);

void __cdecl CreateClient(RString ip, int port, RString password)
{
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!options)
    {
        LOG_DEBUG(Network, "[CreateClient] GWorld->Options() is null, cannot create client display");
        return;
    }

    LOG_DEBUG(Network, "[CreateClient] Connecting to {}:{}", ip.Data(), port);
    GetNetworkManager().Init(ip, port);
    if (!GetNetworkManager().WaitForSession())
    {
        GetNetworkManager().Done();
        return;
    }
    RString guid = GetNetworkManager().IPToGUID(ip, port);
    ConnectResult result = GetNetworkManager().JoinSession(guid, password);
    switch (result)
    {
        case CROK:
            options->CreateChild(new DisplayClient(options));
            break;
        case CRPassword:
            options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
            break;
        case CRVersion:
            options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
            break;
        case CRError:
            options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
            break;
        case CRSessionFull:
            options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
            break;
    }
}

// Surface a failed in-process mod re-mount on the rebuilt menu. The re-mount already
// rolled back to the previous mod set; AppIdle calls this once the menu (GModeIntro +
// Options) is live again, mirroring the deferred MP-join below.
void __cdecl ReportRemountFailure();
void __cdecl ReportRemountFailure()
{
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (options != nullptr)
    {
        options->CreateMsgBox(MB_BUTTON_OK,
                              RString("Could not load the selected mod set. Reverted to the previous one."));
    }
}

bool __cdecl CreateClientDeferred(RString ip, int port, RString password);

// Non-blocking connect to a known host, driven once per frame from AppIdle. The
// enumeration is pumped by the menu loop, so WaitForSession's blocking while(true)
// starves it and hangs. Instead this kicks off enumeration once, polls while the
// menu loop pumps it, then joins the explicit address even if the host never
// appeared in the session list.
// Returns true when finished (connected or failed); false while waiting.
bool __cdecl CreateClientDeferred(RString ip, int port, RString password)
{
    static bool s_enumerating = false;
    static int s_ticks = 0;

    if (!s_enumerating)
    {
        LOG_INFO(Network, "[CreateClientDeferred] Enumerating {}:{}", ip.Data(), port);
        GetNetworkManager().Init(ip, port, /*startEnum*/ true);
        s_enumerating = true;
        s_ticks = 0;
        return false; // keep ticking; the loop pumps the enum between frames
    }

    ++s_ticks;
    AutoArray<SessionInfo> sessions;
    GetNetworkManager().GetSessions(sessions);
    const bool found = sessions.Size() > 0;
    if (!found && s_ticks < 600) // poll for several seconds of enumeration
    {
        return false;
    }

    s_enumerating = false;
    Display* options = dynamic_cast<Display*>(GWorld->Options());
    if (!found)
    {
        LOG_INFO(Network, "[CreateClientDeferred] no enumerated session for {}:{} after {} ticks; trying direct join",
                 ip.Data(), port, s_ticks);
    }

    RString guid = GetNetworkManager().IPToGUID(ip, port);
    ConnectResult result = GetNetworkManager().JoinSession(guid, password);
    LOG_INFO(Network, "[CreateClientDeferred] JoinSession result={}", static_cast<int>(result));
    if (options)
    {
        switch (result)
        {
            case CROK:
                options->CreateChild(new DisplayClient(options));
                break;
            case CRPassword:
                options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
                break;
            case CRVersion:
                options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
                break;
            case CRError:
                options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
                break;
            case CRSessionFull:
                options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
                break;
        }
    }
    return true;
}

class DisplaySessionFilter : public Display
{
    typedef Display base;

  protected:
    SessionFilter _filter;

  public:
    DisplaySessionFilter(ControlsContainer* parent, SessionFilter& filter) : Display(parent)
    {
        _enableSimulation = false;
        _filter = filter;
        Load("RscDisplayFilter");
        UpdateValues();
        UpdateButtons();
    }
    void OnButtonClicked(int idc) override;

    bool CanDestroy() override;
    void Destroy() override;

    const SessionFilter& GetFilter() const { return _filter; }

  protected:
    void UpdateButtons();

    void UpdateValues();
};

void DisplaySessionFilter::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_FILTER_FULL:
            _filter.fullServers = !_filter.fullServers;
            UpdateButtons();
            break;
        case IDC_FILTER_PASSWORDED:
            _filter.passwordedServers = !_filter.passwordedServers;
            UpdateButtons();
            break;
        case IDC_FILTER_DEFAULT:
            _filter = SessionFilter();
            UpdateValues();
            UpdateButtons();
            break;
        default:
            base::OnButtonClicked(idc);
            break;
    }
}

bool DisplaySessionFilter::CanDestroy()
{
    return base::CanDestroy();
}

void DisplaySessionFilter::Destroy()
{
    C3DEdit* ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_SERVER));
    if (ctrl)
    {
        _filter.serverName = ctrl->GetText();
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MISSION));
    if (ctrl)
    {
        _filter.missionName = ctrl->GetText();
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MAXPING));
    if (ctrl)
    {
        _filter.maxPing = atoi(ctrl->GetText());
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MINPLAYERS));
    if (ctrl)
    {
        _filter.minPlayers = atoi(ctrl->GetText());
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MAXPLAYERS));
    if (ctrl)
    {
        _filter.maxPlayers = atoi(ctrl->GetText());
    }

    base::Destroy();
}

void DisplaySessionFilter::UpdateValues()
{
    C3DEdit* ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_SERVER));
    if (ctrl)
    {
        ctrl->SetText(_filter.serverName);
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MISSION));
    if (ctrl)
    {
        ctrl->SetText(_filter.missionName);
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MAXPING));
    if (ctrl)
    {
        ctrl->SetText(Format("%d", _filter.maxPing));
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MINPLAYERS));
    if (ctrl)
    {
        ctrl->SetText(Format("%d", _filter.minPlayers));
    }

    ctrl = dynamic_cast<C3DEdit*>(GetCtrl(IDC_FILTER_MAXPLAYERS));
    if (ctrl)
    {
        ctrl->SetText(Format("%d", _filter.maxPlayers));
    }
}

void DisplaySessionFilter::UpdateButtons()
{
    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_FILTER_FULL));
    if (button)
    {
        button->SetText(
            Format(LocalizeString(IDS_FILTER_FULL),
                   (const char*)(_filter.fullServers ? LocalizeString(IDS_DISP_SHOW) : LocalizeString(IDS_DISP_HIDE))));
    }

    button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_FILTER_PASSWORDED));
    if (button)
    {
        button->SetText(Format(
            LocalizeString(IDS_FILTER_PASSWORDED),
            (const char*)(_filter.passwordedServers ? LocalizeString(IDS_DISP_SHOW) : LocalizeString(IDS_DISP_HIDE))));
    }
}

CSessions::CSessions(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DListBox(parent, idc, cls)
{
    _password = GlobLoadTexture("data\\zamecek.paa");
    _version = GlobLoadTexture("data\\mission_uncomplete.paa");
    _none = GlobLoadTexture("data\\clear_empty.paa");

    _sb3DWidth = 0.05;
}

void CSessions::DrawItem(Vector3Par position, Vector3Par down, int i, float alpha)
{
    // i is the display row (BeginRow position + selection); a is the actual session
    // it maps to through the shared filter layer (identity when no filter is active).
    const int a = VisibleRow(i);
    DWORD t = _sessions[a].lastTime;
    float age = t == 0xffffffff ? 0 : 0.001 * (DWORD)((DWORD)GetTickCount() - t);
    if (age > 10)
    {
        alpha *= (15.0 - age) * (1.0 / 5.0);
    }

    C3DTableRow row = BeginRow(position, down, i, alpha, 0.05, 0.4, true);
    PackedColor color = row.color;

    // icon (sits inside the left of the name column)
    Texture* texture = nullptr;
    PackedColor col = color;
    if (_sessions[a].badActualVersion || _sessions[a].badRequiredVersion || _sessions[a].badMod || _sessions[a].badTag)
    {
        texture = _version;
        col = PackedColor(Color(1, 0, 0, alpha));
    }
    else if (_sessions[a].password)
    {
        texture = _password;
    }
    else
    {
        texture = _none;
    }
    float iconSize = 0;
    if (texture)
    {
        Vector3 iconRight = (float)texture->AWidth() / (float)texture->AHeight() * row.up.Size() * row.dir;
        float rightSize = iconRight.Size();
        float x2c = 1;
        if (rightSize > row.rightSBSize)
        {
            x2c = row.rightSBSize / rightSize;
        }
        GEngine->Draw3D(row.pos + row.border * row.dir, -row.up, iconRight, ClipAll, col, DisableSun, texture, 0,
                        row.y1ct, x2c, row.y2ct);
        iconSize = rightSize + row.border;
    }

    // session name — drawn past the icon, then advance + the column divider
    {
        float column = 0.38 * row.rightSBSize;
        float x2c = (column - iconSize - 2.0 * row.border) * row.invRightSize;
        GEngine->DrawText3D(row.pos + (iconSize + row.border) * row.dir, row.up, row.right, ClipAll, row.font, color,
                            DisableSun, _sessions[a].name, 0, row.y1ct, x2c, row.y2ct);
        row.pos += column * row.dir;
        GEngine->DrawLine3D(row.pos + (row.y1c - row.top) * row.down, row.pos + (row.y2c - row.top) * row.down, color,
                            DisableSun);
    }

    // mission
    row.DrawColumn(0.3, _sessions[a].mission, color, true);

    // state
    RString state;
    col = color;
    switch (_sessions[a].gameState)
    {
        case NGSCreating:
        case NGSCreate:
        case NGSLogin:
            state = LocalizeString(IDS_SESSION_CREATE);
            break;
        case NGSEdit:
            state = LocalizeString(IDS_SESSION_EDIT);
            break;
        case NGSPrepareSide:
            state = LocalizeString(IDS_SESSION_WAIT);
            break;
        case NGSPrepareRole:
        case NGSPrepareOK:
        case NGSTransferMission:
        case NGSLoadIsland:
            state = LocalizeString(IDS_SESSION_SETUP);
            col = PackedColor(Color(1, 0, 0, alpha));
            break;
        case NGSDebriefing:
        case NGSDebriefingOK:
            state = LocalizeString(IDS_SESSION_DEBRIEFING);
            col = PackedColor(Color(1, 0, 0, alpha));
            break;
        case NGSBriefing:
            state = LocalizeString(IDS_SESSION_BRIEFING);
            col = PackedColor(Color(1, 0, 0, alpha));
            break;
        case NGSPlay:
            state = LocalizeString(IDS_SESSION_PLAY);
            col = PackedColor(Color(1, 0, 0, alpha));
            break;
    }
    row.DrawColumn(0.14, state, col, true);

    // players
    RString text;
    if (_sessions[a].maxPlayers)
    {
        text = Format("%d/%d", _sessions[a].numPlayers, _sessions[a].maxPlayers - 2);
    }
    else
    {
        text = Format("%d", _sessions[a].numPlayers);
    }
    row.DrawColumn(0.10, text, color, true);

    // ping (last column on line 1 — no trailing divider)
    row.DrawColumn(0.08, Format("%d", _sessions[a].ping), color);

    // second line (no selection redraw)
    C3DTableRow row2 = BeginRow(position, down, i, alpha, 0.5, 0.4, false);

    // versions (actual + required, second offset by the first text width)
    {
        float column = 0.38 * row2.rightSBSize;
        float x2c = (column - 2.0 * row2.border) * row2.invRightSize;
        char buffer[256];
        float ver = 0.01f * _sessions[a].actualVersion;
        saturateMax(ver, 1.0f);
        const char* remoteTag = _sessions[a].versionTag.GetLength() > 0 ? (const char*)_sessions[a].versionTag : "";
        snprintf(buffer, sizeof(buffer), "%.1f%s%s", ver, remoteTag[0] != 0 ? "-" : "", remoteTag);
        col = (_sessions[a].badActualVersion || _sessions[a].badTag) ? PackedColor(Color(1, 0, 0, alpha)) : color;
        GEngine->DrawText3D(row2.pos + row2.border * row2.dir, row2.up, row2.right, ClipAll, row2.font, col, DisableSun,
                            buffer, 0, row2.y1ct, x2c, row2.y2ct);
        Vector3 width = GEngine->GetText3DWidth(row2.right, row2.font, buffer);
        x2c -= width.Size() * row2.invRightSize;
        ver = 0.01f * _sessions[a].requiredVersion;
        saturateMax(ver, 1.0f);
        ::sprintf(buffer, LocalizeString(IDS_SESSION_REQUIRED), ver);
        col = _sessions[a].badRequiredVersion ? PackedColor(Color(1, 0, 0, alpha)) : color;
        GEngine->DrawText3D(row2.pos + width + row2.border * row2.dir, row2.up, row2.right, ClipAll, row2.font, col,
                            DisableSun, buffer, 0, row2.y1ct, x2c, row2.y2ct);
        row2.pos += column * row2.dir;
    }

    // mods
    col = _sessions[a].badMod ? PackedColor(Color(1, 0, 0, alpha)) : color;
    row2.DrawColumn(0.3, _sessions[a].mod, col);

    // time left
    if (_sessions[a].mission.GetLength() > 0 && _sessions[a].timeleft > 0)
    {
        row2.DrawColumn(0.14, Format(LocalizeString(IDS_TIME_LEFT), _sessions[a].timeleft), color);
    }
}

// Compact human-readable size for the catalog Size column.
static RString FormatModSize(int64_t bytes)
{
    if (bytes <= 0)
        return RString("--");
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return Format("%.1f GB", mb / 1024.0);
    return Format("%.1f MB", mb);
}

// One catalog row across the notebook surface via the shared C3DTableRow
// painter. Columns sum to 1.0 of the list width (checkbox .07, Name .31,
// Source .14, Version .10, Size .13, State .25). Only State takes a colour; the
// rest use the row base colour (green-CRT _ftColor, or _selColor when selected).
void CModsList::DrawItem(Vector3Par position, Vector3Par down, int i, float alpha)
{
    C3DTableRow row = BeginRow(position, down, i, alpha, 0.05, 0.4, true);
    const ModRow& mod = _modRows[VisibleRow(i)]; // i = display row; map to the actual row

    // "[X]" / "[ ]" — both three glyphs so the box is the same width either way
    // (a two-space "[  ]" made the empty box wider). Width shared with the click
    // target (HandleRowClick) so what is drawn lines up with what toggles.
    row.DrawColumn(kCheckboxColumnWidth, mod.checked ? "[X]" : "[ ]");
    row.DrawColumn(0.31, mod.name);
    row.DrawColumn(0.14, mod.source == ModRowSource::Local ? LocalizeString("STR_DISP_MODS_SOURCE_LOCAL_VALUE")
                                                           : LocalizeString("STR_DISP_MODS_SOURCE_WORKSHOP_VALUE"));
    row.DrawColumn(0.10, mod.version);
    row.DrawColumn(0.13, FormatModSize(mod.sizeBytes));

    PackedColor stateColor;
    RString stateText;
    switch (mod.state)
    {
        case ModRowState::Active:
            stateText = LocalizeString("STR_DISP_MODS_STATE_ACTIVE");
            stateColor = PackedColor(Color(0.55, 1.0, 0.55, alpha));
            break;
        case ModRowState::Downloaded:
            stateText = LocalizeString("STR_DISP_MODS_STATE_DOWNLOADED");
            stateColor = PackedColor(Color(0.55, 0.80, 1.0, alpha));
            break;
        case ModRowState::Missing:
        default:
            // "Available" — known on the workshop catalog but not yet downloaded.
            stateText = LocalizeString("STR_DISP_MODS_STATE_AVAILABLE");
            stateColor = PackedColor(Color(1.0, 0.80, 0.45, alpha));
            break;
    }
    row.DrawColumn(0.25, stateText, stateColor);
}

struct CmpModsContext
{
    ModsSortColumn column;
    bool ascending;
};

int CmpMods(const ModRow* a, const ModRow* b, CmpModsContext ctx)
{
    int value = 0;
    switch (ctx.column)
    {
        case MSCName:
            value = Poseidon::Foundation::CollateUtf8(a->name, b->name);
            break;
        case MSCVersion:
            value = stricmp(a->version, b->version);
            break;
        case MSCSize:
            value = a->sizeBytes < b->sizeBytes ? -1 : (a->sizeBytes > b->sizeBytes ? 1 : 0);
            break;
        case MSCState:
            value = static_cast<int>(a->state) - static_cast<int>(b->state);
            break;
        case MSCSource:
            value = static_cast<int>(a->source) - static_cast<int>(b->source);
            break;
    }
    return ctx.ascending ? value : -value;
}

void CModsList::Sort(ModsSortColumn column, bool ascending)
{
    SortPreservingSelection(
        [&]
        {
            CmpModsContext ctx;
            ctx.column = column;
            ctx.ascending = ascending;
            QSort(_modRows.Data(), _modRows.Size(), ctx, CmpMods);
            // _visible holds actual row indices into the just-reordered _modRows.
            if (IsFilterActive())
                RebuildVisibleRows();
        });
}

// The checked rows' @modId joined by ',' (selection order), "" if none. This is
// the human/harness-readable view of the active set (triAssertModsActiveSet); the
// comma — not the path separator ';' — keeps it embeddable in a tri .sqf string,
// whose runner splits statements on ';'. BuildModPath produces the real ';'-joined
// path the re-mount consumes.
RString CModsList::CheckedModIds() const
{
    std::string out;
    for (int i = 0; i < _modRows.Size(); i++)
    {
        if (!_modRows[i].checked)
            continue;
        if (!out.empty())
            out += ',';
        out += (const char*)_modRows[i].modId;
    }
    return RString(out.c_str());
}

// Same checked set as CheckedModIds, but each @modId prefixed with its source's
// root to form the absolute, semicolon-separated mod path RequestRemountWithMods
// wants ("" = base game). Local mods mount from localRoot, downloaded (workshop)
// mods from workshopRoot — so a mod's source is preserved by its on-disk location.
RString CModsList::BuildModPath(const char* localRoot, const char* workshopRoot) const
{
    const std::string local = localRoot ? localRoot : "";
    const std::string workshop = workshopRoot ? workshopRoot : "";
    std::string out;
    for (int i = 0; i < _modRows.Size(); i++)
    {
        // A ticked mod mounts once it is on disk. Workshop (remote) mods not yet
        // downloaded are still Missing and are skipped here (Apply opens the
        // download dialog for them first); once downloaded + unpacked their state
        // flips off Missing and they mount from the workshop root.
        if (!_modRows[i].checked || _modRows[i].state == ModRowState::Missing)
            continue;
        const std::string& root = (_modRows[i].source == ModRowSource::Workshop) ? workshop : local;
        if (!out.empty())
            out += ';';
        out += root;
        // Insert a separator if the root didn't already end with one (GamePaths dirs
        // end with '/', but an absolute --mods-dir / --workshop-dir does not).
        if (!root.empty() && root.back() != '/' && root.back() != '\\')
            out += '/';
        const char* folder = _modRows[i].folderName.GetLength() > 0 ? (const char*)_modRows[i].folderName
                                                                    : (const char*)_modRows[i].modId;
        out += folder;
    }
    return RString(out.c_str());
}

// Case-insensitive substring test for the name filter; empty needle matches anything.
bool CModsList::ContainsNoCase(const char* hay, const char* needle)
{
    if (needle == nullptr || needle[0] == 0)
        return true;
    if (hay == nullptr)
        return false;
    for (const char* h = hay; *h != 0; ++h)
    {
        int k = 0;
        while (needle[k] != 0 && h[k] != 0 && tolower((unsigned char)h[k]) == tolower((unsigned char)needle[k]))
            ++k;
        if (needle[k] == 0)
            return true;
    }
    return false;
}

struct CmpSessionsContext
{
    SortColumn column;
    bool ascending;
};

int CmpSessions(const SessionInfo* info1, const SessionInfo* info2, CmpSessionsContext ctx)
{
    bool bad1 = info1->badActualVersion || info1->badRequiredVersion || info1->badMod || info1->badTag;
    bool bad2 = info2->badActualVersion || info2->badRequiredVersion || info2->badMod || info2->badTag;
    int diff = bad1 - bad2;
    if (diff != 0)
    {
        return diff;
    }

    int value = 0;
    switch (ctx.column)
    {
        case SCServer:
            value = stricmp(info1->name, info2->name);
            break;
        case SCMission:
            value = stricmp(info1->mission, info2->mission);
            break;
        case SCState:
            value = info1->gameState - info2->gameState;
            break;
        case SCPlayers:
            value = info1->numPlayers - info2->numPlayers;
            break;
        case SCPing:
            value = info1->ping - info2->ping;
            break;
    }
    if (ctx.ascending)
    {
        return value;
    }
    else
    {
        return -value;
    }
}

void CSessions::Sort(SortColumn column, bool ascending)
{
    SortPreservingSelection(
        [&]
        {
            CmpSessionsContext ctx;
            ctx.column = column;
            ctx.ascending = ascending;
            QSort(_sessions.Data(), _sessions.Size(), ctx, CmpSessions);
        });
}

DisplayMultiplayer::DisplayMultiplayer(ControlsContainer* parent) : Display(parent)
{
    _enableSimulation = false;
    _sessions = nullptr;
    _portRemote = _portLocal = GetNetworkPort();

    _source = BSLAN;

    Load("RscDisplayMultiplayer");
    LoadParams();

    _refresh = true;
    _refreshing = false;

    _serverList = nullptr;

    _sort = SCPing;
    _ascending = true;
    _sortUp = "\\misc\\sipkau.paa";
    _sortDown = "\\misc\\sipkad.paa";

    BrowsingSource source = _source;
    if (source == BSRemote)
    {
        source = BSLAN;
    }

    UpdateAddress("", GetNetworkPort());
    UpdatePassword("");
    UpdateSessions();

    if (IControl* masterServerLogo = GetCtrl(IDC_MULTI_MASTER_SERVER_LOGO))
    {
        masterServerLogo->ShowCtrl(false);
    }

    // Always sockets (DirectPlay removed) - set implementation label
    C3DActiveText* dplayButton = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MULTI_DPLAY));
    if (dplayButton)
    {
        dplayButton->SetText(FormatNetworkMasterServerAttribution(GetNetworkMasterServer()));
    }

    SetSource(source);
    SetProgress(0);

    UpdateIcons();
    UpdateFilter();
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplayMultiplayer::~DisplayMultiplayer()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
    if (_serverList)
    {
        DestroyMasterServerBrowser(_serverList);
        _serverList = nullptr;
    }
}

Control* DisplayMultiplayer::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_MULTI_SESSIONS:
            _sessions = new CSessions(this, idc, cls);
            return _sessions;
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayMultiplayer::OnSimulate(EntityAI* vehicle)
{
    if (_source == BSInternet)
    {
        GetNetworkManager().OnSimulate();
        UpdateServerList();
        UpdateInternetSessionPings();
    }
    else
    {
        GetNetworkManager().OnSimulate();
        UpdateSessions();
    }
    Display::OnSimulate(vehicle);
}

void __cdecl CreateDisplayIPAddress(ControlsContainer* parent)
{
    parent->CreateChild(new DisplayIPAddress(parent));
}

void DisplayMultiplayer::SetSource(BrowsingSource source)
{
    _source = source;
    SaveParams();

    bool internet = source == BSInternet;
    if (GetCtrl(IDC_MULTI_INTERNET))
    {
        GetCtrl(IDC_MULTI_INTERNET)->ShowCtrl(true);
    }
    if (GetCtrl(IDC_MULTI_PROGRESS))
    {
        GetCtrl(IDC_MULTI_PROGRESS)->ShowCtrl(internet);
    }
    if (GetCtrl(IDC_MULTI_REFRESH))
    {
        GetCtrl(IDC_MULTI_REFRESH)->ShowCtrl(internet);
    }
    if (GetCtrl(IDC_MULTI_FILTER))
    {
        GetCtrl(IDC_MULTI_FILTER)->ShowCtrl(internet);
    }
    if (GetCtrl(IDC_MULTI_PORT))
    {
        GetCtrl(IDC_MULTI_PORT)->ShowCtrl(!internet);
    }

    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MULTI_INTERNET));
    if (button)
    {
        if (internet)
        {
            button->SetText(
                Format(LocalizeString(IDS_SESSIONS_SOURCE), (const char*)LocalizeString(IDS_SESSIONS_INTERNET)));
        }
        else if (source == BSLAN)
        {
            button->SetText(Format(LocalizeString(IDS_SESSIONS_SOURCE), (const char*)LocalizeString(IDS_MULTI_LAN)));
        }
        else
        {
            button->SetText(Format(LocalizeString(IDS_SESSIONS_SOURCE), (const char*)_ipAddress));
        }
    }

    if (internet)
    {
        CStatic* title = dynamic_cast<CStatic*>(GetCtrl(IDC_MULTI_TITLE));
        if (title)
        {
            title->SetText(LocalizeString(IDS_MULTI_TITLE_INTERNET));
        }
        _refresh = true;
    }
}

void DisplayMultiplayer::UpdateIcons()
{
    // One sort-direction caret per column, via the shared header-caret helper:
    // show the arrow on the active sort column (in the sort direction), hide the rest.
    UpdateSortCaret(GetCtrl(IDC_MULTI_SERVER_ICON), _sort == SCServer, _ascending, _sortUp, _sortDown);
    UpdateSortCaret(GetCtrl(IDC_MULTI_MISSION_ICON), _sort == SCMission, _ascending, _sortUp, _sortDown);
    UpdateSortCaret(GetCtrl(IDC_MULTI_STATE_ICON), _sort == SCState, _ascending, _sortUp, _sortDown);
    UpdateSortCaret(GetCtrl(IDC_MULTI_PLAYERS_ICON), _sort == SCPlayers, _ascending, _sortUp, _sortDown);
    UpdateSortCaret(GetCtrl(IDC_MULTI_PING_ICON), _sort == SCPing, _ascending, _sortUp, _sortDown);
}

void DisplayMultiplayer::UpdateFilter()
{
    C3DStatic* text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MULTI_SERVER_FILTER));
    if (text)
    {
        if (_filter.serverName.GetLength() > 0)
        {
            text->SetText(Format("(%s)", (const char*)_filter.serverName));
        }
        else
        {
            text->SetText(RString());
        }
    }

    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MULTI_MISSION_FILTER));
    if (text)
    {
        if (_filter.missionName.GetLength() > 0)
        {
            text->SetText(Format("(%s)", (const char*)_filter.missionName));
        }
        else
        {
            text->SetText(RString());
        }
    }

    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MULTI_PLAYERS_FILTER));
    if (text)
    {
        if (_filter.minPlayers > 0 || _filter.maxPlayers > 0)
        {
            text->SetText(Format("%d..%d", _filter.minPlayers, _filter.maxPlayers));
        }
        else
        {
            text->SetText(RString());
        }
    }

    text = dynamic_cast<C3DStatic*>(GetCtrl(IDC_MULTI_PING_FILTER));
    if (text)
    {
        if (_filter.maxPing > 0)
        {
            text->SetText(Format("<%d", _filter.maxPing));
        }
        else
        {
            text->SetText(RString());
        }
    }
}

void DisplayMultiplayer::SetProgress(float progress)
{
    CProgressBar* ctrl = dynamic_cast<CProgressBar*>(GetCtrl(IDC_MULTI_PROGRESS));
    if (ctrl)
    {
        ctrl->SetPos(progress);
    }
}

// Resolve a modded server's required set against the catalog + what's on disk, then
// route to the right action: download missing mods (DisplayModDownload), re-mount to
// switch sets (ApplyModdedJoin), or fall back to a plain connect when nothing's needed.
// Returns true if this took over the join; false to let the caller JoinSession plainly.
bool DisplayMultiplayer::BeginModdedJoin(const SessionInfo& info)
{
    // Stash the target — the guid is "address:port".
    _joinGuid = info.guid;
    _joinServerMod = info.mod;
    _joinServerEqualMod = info.equalModRequired;
    std::string guid = (const char*)info.guid;
    _joinTargetIp = guid.c_str();
    _joinTargetPort = GetNetworkPort();
    const std::size_t colon = guid.rfind(':');
    if (colon != std::string::npos)
    {
        _joinTargetIp = guid.substr(0, colon).c_str();
        _joinTargetPort = atoi(guid.c_str() + colon + 1);
    }

    // Catalog (best-effort — absent/unreachable just means missing mods can't be
    // auto-downloaded, which the resolver reports as Blocked).
    Poseidon::ModCatalog catalog;
    const std::string host = (const char*)GetNetworkMasterServer();
    if (!host.empty())
    {
        const std::string proxy = (const char*)GetNetworkProxy();
        std::vector<MasterServerServiceModCatalogEntry> mods;
        if (FetchMasterServerServiceModList(host.c_str(), nullptr, proxy.empty() ? nullptr : proxy.c_str(), mods))
        {
            for (const auto& m : mods)
                catalog.Add(Poseidon::ModCatalogEntry(Poseidon::ModId(m.modId), m.name, m.downloadUrl, m.sizeBytes,
                                                      m.folderName));
        }
    }

    // Installed (both roots) + active (current mount path).
    std::vector<Poseidon::ModId> installed;
    for (const auto& m : Poseidon::ScanModsRoot(Poseidon::ModsLocalRoot(), Poseidon::ModSource::Local))
        installed.emplace_back(m.catalogId.empty() ? m.id : m.catalogId);
    for (const auto& m : Poseidon::ScanModsRoot(Poseidon::ModsWorkshopRoot(), Poseidon::ModSource::Workshop))
        installed.emplace_back(m.catalogId.empty() ? m.id : m.catalogId);
    std::vector<Poseidon::ModId> active;
    for (const auto& m : Poseidon::ActiveModsFromMountPath((const char*)Poseidon::ModSystem::GetModList()).All())
        active.emplace_back(m.catalogId.empty() ? m.id : m.catalogId);

    const Poseidon::ServerModList required((const char*)info.mod, info.equalModRequired);
    const Poseidon::ServerModResolution res = Poseidon::ServerModResolver(installed, active).Resolve(required, catalog);

    // Already-mounted check: does the engine's current mount path already equal the
    // server's required set resolved to disk?
    Poseidon::ModLoader loader;
    loader.AddRoot(Poseidon::ModsLocalRoot(), Poseidon::ModSource::Local);
    loader.AddRoot(Poseidon::ModsWorkshopRoot(), Poseidon::ModSource::Workshop);
    const std::string requiredPath = loader.Load().MountPathForIds(required.Required());
    const bool alreadyMounted = (requiredPath == (const char*)Poseidon::ModSystem::GetModList());

    const Poseidon::MpJoinAction action = Poseidon::DecideMpJoinAction(res, alreadyMounted);
    if (action == Poseidon::MpJoinAction::ConnectDirect)
    {
        return false; // nothing to switch — let the caller JoinSession plainly
    }
    if (action == Poseidon::MpJoinAction::Blocked)
    {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
        return true;
    }

    // Download / ApplyThenConnect: stash the work and show the one-screen requirements
    // dialog (diff + password). Approving it chains to the download (if any) + apply.
    _joinTasks.clear();
    const std::string root = Poseidon::ModsWorkshopRoot();
    for (const Poseidon::ModDownload& d : res.ToDownload())
        _joinTasks.push_back(
            Poseidon::MakeModDownloadTask(d.id.Value(), d.name, d.downloadUrl, d.sizeBytes, root, d.folderName));

    std::string diff = (const char*)LocalizeString("STR_DISP_MODS_JOIN_REQUIRES");
    diff += "\n";
    for (const Poseidon::ModId& id : res.Satisfied())
        diff += "  [ok] @" + id.Value() + "   " + (const char*)LocalizeString("STR_DISP_MODS_JOIN_INSTALLED") + "\n";
    for (const Poseidon::ModDownload& d : res.ToDownload())
    {
        const std::string mb = std::to_string(d.sizeBytes / (1024 * 1024));
        diff += "  [dl] " + (d.name.empty() ? ("@" + d.id.Value()) : d.name) + "   " +
                (const char*)LocalizeString("STR_DISP_MODS_JOIN_DOWNLOAD") + " " + mb + " MB\n";
    }
    if (!res.ToDisable().empty())
    {
        diff += (const char*)LocalizeString("STR_DISP_MODS_JOIN_DISABLED");
        diff += "\n";
        for (const Poseidon::ModId& id : res.ToDisable())
            diff += "  [x] @" + id.Value() + "\n";
    }

    std::string title = (const char*)Format(LocalizeString("STR_DISP_MODS_JOIN_TITLE"), (const char*)info.name);
    const RString okText = LocalizeString(action == Poseidon::MpJoinAction::Download ? "STR_DISP_MODS_DOWNLOAD_JOIN"
                                                                                     : "STR_DISP_MODS_SETUP_JOIN");
    CreateChild(new DisplayJoinRequirements(this, RString(title.c_str()), RString(diff.c_str()), _password, okText));
    return true;
}

// Switch the engine to the pending join's required mod set and arm the deferred
// connect; the AppIdle PendingConnect hook finishes the join once the menu is back.
void DisplayMultiplayer::ApplyModdedJoin()
{
    if (GApp == nullptr || GWorld == nullptr || GWorld->GetMode() != GModeIntro)
        return;
    Poseidon::ModLoader loader;
    loader.AddRoot(Poseidon::ModsLocalRoot(), Poseidon::ModSource::Local);
    loader.AddRoot(Poseidon::ModsWorkshopRoot(), Poseidon::ModSource::Workshop);
    const Poseidon::ServerModList required((const char*)_joinServerMod, _joinServerEqualMod);
    const std::string path = loader.Load().MountPathForIds(required.Required());
    Poseidon::GPendingConnect().Arm((const char*)_joinTargetIp, _joinTargetPort, (const char*)_password);
    GApp->RequestRemountWithMods(path.c_str());
}

void DisplayMultiplayer::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_MULTI_REMOTE:
            // Macrovision CD Protection
            CreateDisplayIPAddress(this);
            break;
        case IDC_MULTI_INTERNET:
            if (_source == BSInternet)
            {
                UpdateAddress("", _portLocal);
            }
            else
            {
                SetSource(BSInternet);
            }
            break;
        case IDC_MULTI_PASSWORD:
            CreateChild(new DisplayPassword(this, _password));
            break;
        case IDC_MULTI_PORT:
            CreateChild(new DisplayPort(this, GetPort()));
            break;
        case IDC_MULTI_FILTER:
            CreateChild(new DisplaySessionFilter(this, _filter));
            break;
        case IDC_MULTI_NEW:
            // create session
            GetNetworkManager().CreateSession(GetPort(), _password);
            if (GetNetworkManager().IsServer())
            {
                CreateChild(new DisplayServer(this));
            }
            break;
        case IDC_MULTI_JOIN:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_MULTI_SESSIONS));
            if (lbox)
            {
                int sel = lbox->GetCurSel();
                if (sel >= 0 && _sessions != nullptr && sel < _sessions->GetSize())
                {
                    // A server that requires mods routes through the resolve/download/
                    // apply path (BeginModdedJoin); it returns false when there's no mod
                    // work to do, falling through to the plain JoinSession below.
                    const SessionInfo& info = _sessions->_sessions[_sessions->VisibleRow(sel)];
                    if (info.mod.GetLength() > 0 && BeginModdedJoin(info))
                    {
                        break;
                    }
                    // join session
                    ConnectResult result = GetNetworkManager().JoinSession(lbox->GetData(sel), _password);
                    switch (result)
                    {
                        case CROK:
                            CreateChild(new DisplayClient(this));
                            break;
                        case CRPassword:
                            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
                            break;
                        case CRVersion:
                            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
                            break;
                        case CRError:
                            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
                            break;
                        case CRSessionFull:
                            CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
                            break;
                    }
                }
            }
        }
        break;
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_MULTI_NOTEBOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_MULTI_SERVER_COLUMN:
            if (_sort == SCServer)
            {
                _ascending = !_ascending;
            }
            else
            {
                _sort = SCServer;
                _ascending = true;
            }
            _sessions->Sort(_sort, _ascending);
            UpdateIcons();
            break;
        case IDC_MULTI_MISSION_COLUMN:
            if (_sort == SCMission)
            {
                _ascending = !_ascending;
            }
            else
            {
                _sort = SCMission;
                _ascending = true;
            }
            _sessions->Sort(_sort, _ascending);
            UpdateIcons();
            break;
        case IDC_MULTI_STATE_COLUMN:
            if (_sort == SCState)
            {
                _ascending = !_ascending;
            }
            else
            {
                _sort = SCState;
                _ascending = true;
            }
            _sessions->Sort(_sort, _ascending);
            UpdateIcons();
            break;
        case IDC_MULTI_PLAYERS_COLUMN:
            if (_sort == SCPlayers)
            {
                _ascending = !_ascending;
            }
            else
            {
                _sort = SCPlayers;
                _ascending = true;
            }
            _sessions->Sort(_sort, _ascending);
            UpdateIcons();
            break;
        case IDC_MULTI_PING_COLUMN:
            if (_sort == SCPing)
            {
                _ascending = !_ascending;
            }
            else
            {
                _sort = SCPing;
                _ascending = true;
            }
            _sessions->Sort(_sort, _ascending);
            UpdateIcons();
            break;
        case IDC_MULTI_REFRESH:
            _refresh = true;
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

bool DisplayMultiplayer::DoControllerUiAction(ControllerUiAction action)
{
    switch (action)
    {
        case ControllerUiAction::Preview:
            OnButtonClicked(IDC_MULTI_REFRESH);
            return true;
        case ControllerUiAction::Delete:
            OnButtonClicked(IDC_MULTI_FILTER);
            return true;
        case ControllerUiAction::PreviousTab:
        case ControllerUiAction::NextTab:
            SetSource(_source == BSInternet ? BSLAN : BSInternet);
            return true;
        default:
            break;
    }
    return Display::DoControllerUiAction(action);
}

void DisplayMultiplayer::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_MULTI_SESSIONS && curSel >= 0)
    {
        OnButtonClicked(IDC_MULTI_JOIN);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplayMultiplayer::OnCtrlClosed(int idc)
{
    if (idc == IDC_MULTI_NOTEBOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}
void DisplayMultiplayer::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_PASSWORD:
            if (exit == IDC_OK)
            {
                C3DEdit* edit = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_PASSWORD));
                if (edit)
                {
                    UpdatePassword(edit->GetText());
                }
            }
            Display::OnChildDestroyed(idd, exit);
            break;
        case IDD_IP_ADDRESS:
            if (exit == IDC_OK)
            {
                C3DEdit* editIP = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_IP_ADDRESS));
                C3DEdit* editPort = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_IP_PORT));
                if (editIP && editPort)
                {
                    RString ip = editIP->GetText();

                    if (ip.GetLength() > 0)
                    {
                        int port = atoi(editPort->GetText());
                        if (port <= 0)
                        {
                            port = GetNetworkPort();
                        }
                        UpdateAddress(ip, port);

                        RString filename = GetUserParams();
                        if (QIFStream::FileExists(filename))
                        {
                            ParamFile cfg;
                            cfg.Parse(filename);
                            cfg.Add("remoteIPAddress", ip);
                            cfg.Add("remotePort", port);
                            cfg.Save(filename);
                        }
                    }
                    else
                    {
                        UpdateAddress("", _portLocal);
                    }
                }
            }
            Display::OnChildDestroyed(idd, exit);
            break;
        case IDD_PORT:
            if (exit == IDC_OK)
            {
                C3DEdit* edit = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_PORT_PORT));
                if (edit)
                {
                    int port = atoi(edit->GetText());
                    if (port <= 0)
                    {
                        port = GetNetworkPort();
                    }
                    UpdateAddress(_ipAddress, port);
                }
            }
            Display::OnChildDestroyed(idd, exit);
            break;
        case IDD_FILTER:
            if (exit == IDC_OK)
            {
                DisplaySessionFilter* display = dynamic_cast<DisplaySessionFilter*>(_child.GetRef());
                if (display)
                {
                    _filter = display->GetFilter();
                    SaveParams();
                    UpdateFilter();
                    _refresh = true;
                }
            }
            Display::OnChildDestroyed(idd, exit);
            break;
        case IDD_SERVER:
        case IDD_CLIENT:
            GetNetworkManager().Close();
            Display::OnChildDestroyed(idd, exit);
            StartRandomCutscene(Glob.header.worldname);
            break;
        case IDD_JOIN_REQUIREMENTS:
        {
            // The player approved the modded join: keep the password they entered, then
            // download the missing mods (if any) before applying — else apply straight
            // away. Read the edit BEFORE the base clears _child.
            const bool ok = (exit == IDC_OK);
            if (ok)
            {
                C3DEdit* pw = dynamic_cast<C3DEdit*>(_child->GetCtrl(IDC_JOINREQ_PASSWORD));
                if (pw)
                {
                    UpdatePassword(pw->GetText());
                }
            }
            Display::OnChildDestroyed(idd, exit);
            if (ok)
            {
                if (!_joinTasks.empty())
                {
                    const std::string proxy = (const char*)GetNetworkProxy();
                    CreateChild(new DisplayModDownload(this, std::move(_joinTasks),
                                                       Poseidon::MakeModDownloadTransport(proxy), "mod"));
                    _joinTasks.clear();
                }
                else
                {
                    ApplyModdedJoin();
                }
            }
            break;
        }
        case IDD_MODS_DOWNLOAD:
            // A modded-join download finished: on success, switch to the required mod
            // set + arm the deferred connect (the PendingConnect hook joins after the
            // re-mount). On cancel, just return to the browser.
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_OK)
            {
                ApplyModdedJoin();
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

void DisplayMultiplayer::UpdateAddress(RString ipAddress, int port)
{
    _ipAddress = ipAddress;
    SetPort(port);
    bool lan = _ipAddress.GetLength() == 0;
    SetSource(lan ? BSLAN : BSRemote);

    CStatic* title = dynamic_cast<CStatic*>(GetCtrl(IDC_MULTI_TITLE));
    if (title)
    {
        char buffer[256];
        if (lan)
        {
            ::sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_LAN), port);
        }
        else
        {
            ::sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_REMOTE), (const char*)_ipAddress, port);
        }
        title->SetText(buffer);
    }

    if (!GetNetworkManager().Init(ipAddress, port))
    {
        Fail("Network");
    }
}

void DisplayMultiplayer::UpdatePassword(RString password)
{
    _password = password;

    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MULTI_PASSWORD));
    if (button)
    {
        if (password.GetLength() > 0)
        {
            BString<512> buffer;
            sprintf(buffer, LocalizeString(IDS_PASSWORD), (const char*)password);
            button->SetText((const char*)buffer);
        }
        else
        {
            BString<512> buffer;
            sprintf(buffer, LocalizeString(IDS_PASSWORD), (const char*)LocalizeString(IDS_NO_PASSWORD));
            button->SetText((const char*)buffer);
        }
    }
}

void DisplayMultiplayer::UpdateSessions()
{
    if (!_sessions)
    {
        return;
    }

    RString selGUID;
    int sel = _sessions->GetCurSel();
    if (sel >= 0 && sel < _sessions->GetSize())
    {
        selGUID = _sessions->GetData(sel);
    }

    _sessions->_sessions.Resize(0);
    GetNetworkManager().GetSessions(_sessions->_sessions);

    sel = 0;
    for (int i = 0; i < _sessions->GetSize(); i++)
    {
        if (_sessions->GetData(i) == selGUID)
        {
            sel = i;
            break;
        }
    }
    _sessions->SetCurSel(sel);
}

static const RString& GetAnd()
{
    static const RString And(" and ");
    return And;
}

inline void AddCondition(RString& filter, RString condition)
{
    if (filter.GetLength() > 0)
    {
        filter = filter + GetAnd();
    }
    filter = filter + condition;
}

RString CreateServerFilter(const SessionFilter& filter)
{
    RString value;
    if (filter.serverName.GetLength() > 0)
    {
        AddCondition(value, Format("hostname like '%%%s%%'", (const char*)filter.serverName));
    }
    if (filter.missionName.GetLength() > 0)
    {
        AddCondition(value, Format("gametype like '%%%s%%'", (const char*)filter.missionName));
    }

    if (filter.minPlayers > 0)
    {
        AddCondition(value, Format("numplayers >= %d", filter.minPlayers));
    }
    if (filter.maxPlayers > 0)
    {
        AddCondition(value, Format("numplayers <= %d", filter.maxPlayers));
    }

    if (!filter.fullServers)
    {
        AddCondition(value, "numplayers < maxplayers");
    }

    return value;
}

void DisplayMultiplayer::UpdateServerList()
{
    if (!_sessions)
    {
        return;
    }

    // Test seed (triSeedSessions) freezes the list — skip the refresh so the
    // injected rows aren't cleared by the (empty) LAN/master fetch.
    if (_testSeeded)
    {
        return;
    }

    // (Re)fetch only on an explicit refresh — kick the query asynchronously so
    // the menu keeps rendering while the HTTP round-trip is in flight; the
    // results land on a later frame below via ThinkMasterServerBrowser().
    if (_refresh)
    {
        _refresh = false;

        RString host = GetNetworkMasterServer();
        if (host.GetLength() <= 0)
        {
            _sessions->_sessions.Resize(0);
            _sessions->SetCurSel(0);
            _refreshing = false;
            SetProgress(100);
            return;
        }

        if (!_serverList)
        {
            _serverList = CreateMasterServerBrowser(host, this, nullptr);
        }
        if (!_serverList)
        {
            _refreshing = false;
            SetProgress(100);
            return;
        }

        MasterServerBrowserFilter filter{};
        if (_filter.serverName.GetLength() > 0)
        {
            filter.serverName = _filter.serverName.Data();
        }
        if (_filter.missionName.GetLength() > 0)
        {
            filter.missionName = _filter.missionName.Data();
        }
        filter.minPlayers = _filter.minPlayers;
        filter.maxPlayers = _filter.maxPlayers;
        filter.includeFullServers = _filter.fullServers;

        SetProgress(0);
        _internetPingProbe = false;
        _internetPingProbeFrames = 0;
        UpdateMasterServerBrowser(_serverList, filter); // spawns the fetch worker
        _refreshing = true;
        return;
    }

    if (!_refreshing || !_serverList)
    {
        return;
    }

    ThinkMasterServerBrowser(_serverList);
    if (GetMasterServerBrowserState(_serverList) != MasterServerBrowserState::Idle)
    {
        return; // still fetching — keep rendering
    }

    // Fetch complete — rebuild the session list from the browser results.
    RString selGUID;
    int sel = _sessions->GetCurSel();
    if (sel >= 0 && sel < _sessions->GetSize())
    {
        selGUID = _sessions->GetData(sel);
    }

    _sessions->_sessions.Resize(0);
    AutoArray<RemoteHostAddress> pingHosts;
    const int count = GetMasterServerBrowserCount(_serverList);
    for (int i = 0; i < count; i++)
    {
        MasterServerSessionInfo info;
        if (!TryGetMasterServerBrowserSession(_serverList, i, info))
        {
            continue;
        }
        SessionInfo& dst = _sessions->_sessions[_sessions->_sessions.Add()];
        // guid is the join target — same "address:port" form NetSessionEnum::IPToGUID
        // produces, so the IDC_MULTI_JOIN handler's JoinSession(GetData(sel)) connects
        // straight to the host the master server advertised.
        char guid[256];
        snprintf(guid, sizeof(guid), "%s:%d", info.address ? info.address : "", info.hostPort);
        dst.guid = guid;
        dst.name = info.hostName;
        dst.lastTime = 0xFFFFFFFF;
        dst.actualVersion = info.actualVersion;
        dst.requiredVersion = info.requiredVersion;
        dst.password = info.password;
        dst.badActualVersion = false;
        dst.badRequiredVersion = false;
        dst.badMod = false;
        dst.versionTag = info.versionTag;
        // The master reports the host's build tag, so flag a mismatch against ours (same
        // unconditional, case-insensitive rule the join validator applies).
        dst.badTag = stricmp(dst.versionTag, GetVersionTag()) != 0;
        dst.mission = info.mission;
        dst.gameState = info.gameState;
        dst.ping = info.ping;
        dst.numPlayers = info.numPlayers;
        dst.maxPlayers = info.maxPlayers;
        dst.timeleft = info.timeLeft;
        dst.mod = info.mod;
        dst.equalModRequired = info.equalModRequired;

        if (info.address != nullptr && info.address[0] != 0 && info.hostPort > 0)
        {
            RemoteHostAddress& host = pingHosts[pingHosts.Add()];
            host.name = info.hostName;
            host.ip = info.address;
            host.port = info.hostPort;
        }
    }

    sel = 0;
    for (int i = 0; i < _sessions->GetSize(); i++)
    {
        if (_sessions->GetData(i) == selGUID)
        {
            sel = i;
            break;
        }
    }
    _sessions->SetCurSel(sel);
    _sessions->Sort(_sort, _ascending);

    _refreshing = false;
    _internetPingProbe = pingHosts.Size() > 0 && GetNetworkManager().ProbeRemoteHosts(pingHosts, GetNetworkPort());
    _internetPingProbeFrames = _internetPingProbe ? 0 : 0;
    SetProgress(100);
}

void DisplayMultiplayer::UpdateInternetSessionPings()
{
    if (!_internetPingProbe || !_sessions)
    {
        return;
    }

    AutoArray<SessionInfo> measured;
    GetNetworkManager().GetSessions(measured);
    bool changed = false;
    for (int i = 0; i < measured.Size(); ++i)
    {
        const SessionInfo& src = measured[i];
        if (src.ping <= 0)
        {
            continue;
        }
        for (int j = 0; j < _sessions->_sessions.Size(); ++j)
        {
            SessionInfo& dst = _sessions->_sessions[j];
            if (stricmp(dst.guid, src.guid) == 0)
            {
                if (dst.ping != src.ping)
                {
                    dst.ping = src.ping;
                    changed = true;
                }
                break;
            }
        }
    }

    if (changed)
    {
        _sessions->Sort(_sort, _ascending);
    }

    _internetPingProbeFrames++;
    if (_internetPingProbeFrames > 180)
    {
        _internetPingProbe = false;
    }
}

void DisplayMultiplayer::SeedTestSessions(int count)
{
    if (!_sessions)
        return;
    _testSeeded = true; // freeze the list so the LAN/master refresh won't wipe it
    _refresh = false;
    _refreshing = false;
    _sessions->_sessions.Resize(0);
    for (int i = 0; i < count; i++)
    {
        SessionInfo& dst = _sessions->_sessions[_sessions->_sessions.Add()];
        char buf[64];
        snprintf(buf, sizeof(buf), "127.0.0.1:%d", 2302 + i);
        dst.guid = buf;
        snprintf(buf, sizeof(buf), "Test Server %d", i + 1);
        dst.name = buf;
        dst.lastTime = GetTickCount();
        dst.actualVersion = 199;
        dst.requiredVersion = 199;
        dst.password = (i % 3 == 0);
        dst.badActualVersion = false;
        dst.badRequiredVersion = false;
        dst.badMod = false;
        dst.versionTag = (i == 1) ? "rc2" : "rc1";
        dst.badTag = (i == 1);
        snprintf(buf, sizeof(buf), "Test Mission %d", i + 1);
        dst.mission = buf;
        dst.gameState = NGSCreate;
        dst.ping = 20 + i * 13;
        dst.numPlayers = i % 8;
        dst.maxPlayers = 10; // 8 shown (the UI prints maxPlayers - 2)
        dst.timeleft = 0;
        dst.mod = "";
        dst.equalModRequired = false;
    }
    _sessions->SetCurSel(_sessions->GetSize() > 0 ? 0 : -1);
    _sessions->Sort(_sort, _ascending);
}

int DisplayMultiplayer::GetVisibleSessionPingForTest(int row) const
{
    if (!_sessions || row < 0 || row >= _sessions->GetSize())
    {
        return -1;
    }
    const int actualRow = _sessions->VisibleRow(row);
    if (actualRow < 0 || actualRow >= _sessions->_sessions.Size())
    {
        return -1;
    }
    return _sessions->_sessions[actualRow].ping;
}

int DisplayMultiplayer::GetPort()
{
    if (_ipAddress.GetLength() == 0)
    {
        return _portLocal;
    }
    else
    {
        return _portRemote;
    }
}

void DisplayMultiplayer::SetPort(int port)
{
    if (_ipAddress.GetLength() == 0)
    {
        _portLocal = port;
    }
    else
    {
        _portRemote = port;
    }

    C3DActiveText* button = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MULTI_PORT));
    if (button)
    {
        BString<512> buffer;
        sprintf(buffer, LocalizeString(IDS_DISP_PORT_BUTTON), port);
        button->SetText((const char*)buffer);
    }
}

void DisplayMultiplayer::RefreshLanguage()
{
    if (auto* dplayButton = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_MULTI_DPLAY)))
    {
        dplayButton->SetText(FormatNetworkMasterServerAttribution(GetNetworkMasterServer()));
    }

    SetSource(_source);

    if (_source != BSInternet)
    {
        if (auto* title = dynamic_cast<CStatic*>(GetCtrl(IDC_MULTI_TITLE)))
        {
            char buffer[256];
            if (_ipAddress.GetLength() == 0)
            {
                ::sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_LAN), GetPort());
            }
            else
            {
                ::sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_REMOTE), (const char*)_ipAddress, GetPort());
            }
            title->SetText(buffer);
        }
    }

    UpdatePassword(_password);
    SetPort(GetPort());
    UpdateFilter();
    _refresh = true;
}

LSError SessionFilter::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("serverName", serverName, 1))
    PARAM_CHECK(ar.Serialize("missionName", missionName, 1))
    PARAM_CHECK(ar.Serialize("maxPing", maxPing, 1))
    PARAM_CHECK(ar.Serialize("minPlayers", minPlayers, 1))
    PARAM_CHECK(ar.Serialize("maxPlayers", maxPlayers, 1))
    PARAM_CHECK(ar.Serialize("fullServers", fullServers, 1))
    PARAM_CHECK(ar.Serialize("passwordedServers", passwordedServers, 1))
    return LSOK;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(BrowsingSource dummy)
{
    static const EnumName BrowsingSourceNames[] = {BROWSING_SOURCE_ENUM(BrowsingSource, BS, ENUM_NAME) EnumName()};
    return BrowsingSourceNames;
}

template <>
BrowsingSource Poseidon::Foundation::GetEnumCount(BrowsingSource value)
{
    return NBrowsingSource;
}

void DisplayMultiplayer::LoadParams()
{
    ParamArchiveLoad ar(GetUserParams());
    ar.SerializeEnum("browsingSource", _source, 1, BSLAN);
    ParamArchive arSubcls;
    if (ar.IsSubclass("Filter"))
    {
        ar.Serialize("Filter", _filter, 1);
    }
}

void DisplayMultiplayer::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(GetUserParams());
    ar.SerializeEnum("browsingSource", _source, 1);
    ar.Serialize("Filter", _filter, 1);

    if (ar.Save(GetUserParams()) != LSOK)
    {
    }
}

Control* DisplayPassword::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_PASSWORD:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            edit->SetText(_password);
            return edit;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

Control* DisplayJoinRequirements::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (idc == IDC_JOINREQ_PASSWORD)
    {
        C3DEdit* edit = new C3DEdit(this, idc, cls);
        edit->SetText(_password);
        return edit;
    }
    Control* c = Display::OnCreateCtrl(type, idc, cls);
    if (idc == IDC_JOINREQ_DIFF)
    {
        if (C3DStatic* s = dynamic_cast<C3DStatic*>(c))
            s->SetText(_diff);
    }
    else if (idc == IDC_JOINREQ_TITLE)
    {
        if (CStatic* s = dynamic_cast<CStatic*>(c))
            s->SetText(_title);
    }
    else if (idc == IDC_OK && _okText.GetLength() > 0)
    {
        if (CActiveText* a = dynamic_cast<CActiveText*>(c))
            a->SetText(_okText);
        else if (C3DActiveText* a = dynamic_cast<C3DActiveText*>(c))
            a->SetText(_okText);
        else if (CButton* b = dynamic_cast<CButton*>(c))
            b->SetText(_okText);
    }
    return c;
}

Control* DisplayPort::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_PORT_PORT:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%d", _port);
            edit->SetText(buffer);
            return edit;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

Control* DisplayIPAddress::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_IP_ADDRESS:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            RString address;
            RString filename = GetUserParams();
            if (QIFStream::FileExists(filename))
            {
                ParamFile cfg;
                cfg.Parse(filename);
                const ParamEntry* entry = cfg.FindEntry("remoteIPAddress");
                if (entry)
                {
                    address = *entry;
                }
            }
            edit->SetText(address);
            return edit;
        }
        case IDC_IP_PORT:
        {
            C3DEdit* edit = new C3DEdit(this, idc, cls);
            int port = GetNetworkPort();
            RString filename = GetUserParams();
            if (QIFStream::FileExists(filename))
            {
                ParamFile cfg;
                cfg.Parse(filename);
                const ParamEntry* entry = cfg.FindEntry("remotePort");
                if (entry)
                {
                    port = *entry;
                }
            }
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%d", port);
            edit->SetText(buffer);
            return edit;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

bool DisplayIPAddress::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }

    if (_exit == IDC_OK)
    {
        C3DEdit* edit = dynamic_cast<C3DEdit*>(GetCtrl(IDC_IP_ADDRESS));
        PoseidonAssert(edit);
        RString address = edit->GetText();
        int n = address.GetLength() - 1;
        for (; n >= 0; n--)
        {
            if (!isspace(address[n]))
            {
                break;
            }
        }
        if (n < 0 || address[n] != '.')
        {
            return true;
        }
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_BAD_ADDRESS));
        return false;
    }

    return true;
}

class CMPMissionsList : public C3DListBox
{
    typedef C3DListBox base;

  public:
    CMPMissionsList(ControlsContainer* parent, int idc, const ParamEntry& cls);
    void DrawTooltip(float x, float y) override;
};

CMPMissionsList::CMPMissionsList(ControlsContainer* parent, int idc, const ParamEntry& cls)
    : C3DListBox(parent, idc, cls)
{
}

void CMPMissionsList::DrawTooltip(float x, float y)
{
    _tooltip = RString();

    if (_scrollbar.IsLocked())
    {
        return;
    }
    if (!IsReadOnly() && IsInside(x, y))
    {
        if (_scrollbar.IsEnabled() && _u > (1.0 - _sb3DWidth))
        {
            return;
        }

        float index = _v * _rows;
        if (index >= 0 && index < _rows)
        {
            int sel = toIntFloor(_topString + index);
            if (sel >= 0 && sel < GetSize())
            {
                _tooltip = GetText(sel);
            }
            base::DrawTooltip(x, y);
        }
    }
}

// Server display
Control* DisplayServer::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_SERVER_ISLAND:
        {
            C3DListBox* lbox = new C3DListBox(this, idc, cls);
            int sel = 0;
            // int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
            int m = (Pars >> "CfgWorldList").GetEntryCount();
            for (int j = 0; j < m; j++)
            {
                const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(j);
                if (!entry.IsClass())
                {
                    continue;
                }
                RString name = entry.GetName();

                // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

                // ADDED - check if wrp file exists
                RString fullname = GetWorldName(name);
                if (!QIFStreamB::FileExist(fullname))
                {
                    continue;
                }

                int index = lbox->AddString(Pars >> "CfgWorlds" >> name >> "description");
                lbox->SetData(index, name);
                if (stricmp(name, Glob.header.worldname) == 0)
                {
                    sel = index;
                }
                RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
                RString fullName = FindPicture(textureName);
                if (fullName.GetLength() > 0)
                {
                    fullName.Lower();
                }
                Ref<Texture> texture = GlobLoadTexture(fullName);
                lbox->SetTexture(index, texture);
            }
            lbox->SetCurSel(sel);
            return lbox;
        }
        case IDC_SERVER_MISSION:
            return new CMPMissionsList(this, idc, cls);
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayServer::OnChangeDifficulty()
{
    CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_SERVER_DIFF));
    if (!ctrl)
    {
        return;
    }
    RString text;
    if (_cadetMode)
    {
        text = LocalizeString(IDS_DIFF_CADET);
    }
    else
    {
        text = LocalizeString(IDS_DIFF_VETERAN);
    }
    ctrl->SetText(text);
}

RString CreateMPMissionBank(RString filename, RString island);
void RemoveBank(const char* prefix);

bool DisplayServer::SetMission(bool editor)
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_ISLAND));
    if (!lbox)
    {
        return false;
    }

    CurrentCampaign = "";
    CurrentBattle = "";
    CurrentMission = "";
    USER_CONFIG.easyMode = false;

    int index = lbox->GetCurSel();
    if (index < 0)
    {
        return false;
    }
    RString world = lbox->GetData(index);
    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_MISSION));
    if (!lbox)
    {
        return false;
    }
    index = lbox->GetCurSel();
    if (index < 0)
    {
        if (editor)
        {
        OnEditor:
            RString userDir = GetUserMissionsBase();
            SetBaseDirectory(userDir);
            ::CreateDirectory(GetBaseDirectory() + RString(GameDirs::MPMissions), nullptr);
            ::SetMission(world, "", GetMPMissionsDir());
            return true;
        }
        else
        {
            return false;
        }
    }
    RString mission = lbox->GetData(index);
    int value = lbox->GetValue(index);

    switch (value)
    {
        case 0:
            if (editor)
            {
                goto OnEditor;
            }
            else
            {
                SetBaseDirectory("");
                GetNetworkManager().CreateMission(mission, world);
                ::SetMission(world, "__cur_mp", GetMPMissionsDir());
                Glob.header.filenameReal = mission;
            }
            break;
        case 1:
            if (editor)
            {
                goto OnEditor;
            }
            else
            {
                SetBaseDirectory("");
                GetNetworkManager().CreateMission("", "");
                ::SetMission(world, mission, GetMPMissionsDir());
            }
            break;
        case 2:
        {
            RString userDir = GetUserMissionsBase();
            SetBaseDirectory(userDir);
            ::CreateDirectory(GetBaseDirectory() + RString(GameDirs::MPMissions), nullptr);
            if (!editor)
            {
                GetNetworkManager().CreateMission("", "");
            }
            ::SetMission(world, mission, GetMPMissionsDir());
        }
        break;
        default:
            if (editor)
            {
                goto OnEditor;
            }
            break;
    }
    return true;
}

void __cdecl CreateEditor(ControlsContainer* parent, bool multiplayer)
{
    parent->CreateChild(new DisplayArcadeMap(parent, multiplayer));
}

void DisplayServer::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_SERVER_EDITOR:
        {
            if (!SetMission(true))
            {
                break;
            }

            GetNetworkManager().SetGameState(NGSEdit);

            GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));
            CreateEditor(this, true);
            // CreateChild(new DisplayArcadeMap(this, true));
        }
        break;
        case IDC_OK:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_MISSION));
            if (lbox)
            {
                int index = lbox->GetCurSel();
                if (index >= 0)
                {
                    int value = lbox->GetValue(index);
                    if (value == -2)
                    {
                        if (!SetMission(true))
                        {
                            break;
                        }

                        GetNetworkManager().SetGameState(NGSEdit);

                        GLOB_WORLD->SwitchLandscape(GetWorldName(Glob.header.worldname));

                        // Macrovision CD Protection
                        CreateEditor(this, true);
                        break;
                    }
                    else if (value == -1)
                    {
                        C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_ISLAND));
                        if (!lbox)
                        {
                            break;
                        }

                        int index = lbox->GetCurSel();
                        if (index < 0)
                        {
                            break;
                        }
                        RString world = lbox->GetData(index);

                        GetNetworkManager().SetGameState(NGSEdit);

                        CreateChild(new DisplayWizardTemplate(this, world, true));
                        break;
                    }
                }
            }

            if (!SetMission(false))
            {
                break;
            }

            // load template
            CurrentTemplate.Clear();
            if (GWorld->UI())
            {
                GWorld->UI()->Init();
            }
            if (!ParseMission(true))
            {
                break;
            }

            USER_CONFIG.easyMode = _cadetMode;
            GetNetworkManager().InitMission(_cadetMode);
            GetNetworkManager().SetGameState(NGSPrepareSide);
            CreateChild(new DisplayMultiplayerSetup(this));
        }
        break;
        case IDC_SERVER_DIFF:
            _cadetMode = !_cadetMode;
            SaveParams();
            OnChangeDifficulty();
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayServer::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_SERVER_ISLAND)
    {
        UpdateMissions();
    }
    else if (idc == IDC_SERVER_MISSION)
    {
        OnMissionChanged(curSel);
    }
    Display::OnComboSelChanged(idc, curSel);
}

void DisplayServer::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_SERVER_MISSION && curSel >= 0)
    {
        OnButtonClicked(IDC_OK);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplayServer::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_ARCADE_MAP:
            Display::OnChildDestroyed(idd, exit);
            GetNetworkManager().SetGameState(NGSCreate);

            UpdateMissions(Glob.header.filename);
            if (exit == IDC_OK)
            {
                OnButtonClicked(IDC_OK);
            }
            break;
            //		case IDD_SERVER_SETUP:
        case IDD_SERVER_SIDE:
            Display::OnChildDestroyed(idd, exit);
            GetNetworkManager().SetGameState(NGSCreate);
            break;
        case IDD_WIZARD_TEMPLATE:
            Display::OnChildDestroyed(idd, exit);
            GetNetworkManager().SetGameState(NGSCreate);
            UpdateMissions(Glob.header.filename);
            if (exit == IDC_OK)
            {
                // load template
                CurrentTemplate.Clear();
                if (GWorld->UI())
                {
                    GWorld->UI()->Init();
                }
                if (!ParseMission(true))
                {
                    break;
                }

                USER_CONFIG.easyMode = _cadetMode;
                GetNetworkManager().InitMission(_cadetMode);
                GetNetworkManager().SetGameState(NGSPrepareSide);
                CreateChild(new DisplayMultiplayerSetup(this));
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

void DisplayServer::LoadParams()
{
    ParamArchiveLoad ar(GetUserParams());
    if (ar.Serialize("cadetModeMP", _cadetMode, 1, true) != LSOK)
    {
        WarningMessage("Cannot load user paremeters.");
    }
}

void DisplayServer::SaveParams()
{
    ParamArchiveSave ar(UserInfoVersion);
    ar.Parse(GetUserParams());
    if (ar.Serialize("cadetModeMP", _cadetMode, 1) != LSOK)
    {
    }
    if (ar.Save(GetUserParams()) != LSOK)
    {
    }
}

namespace
{
void AddMPMissionBankRow(C3DListBox* lbox, const char* filename)
{
    char name[256];
    snprintf(name, sizeof(name), "%s", filename);
    char* ext = strrchr(name, '.'); // extension .pbo
    if (!ext)
    {
        return;
    }
    *ext = 0;
    ext = strrchr(name, '.'); // world name
    if (!ext)
    {
        return;
    }
    *ext = 0;

    const int index = lbox->AddString(name);
    lbox->SetData(index, name);
    lbox->SetValue(index, 0); // public/mod bank
}

bool MPMissionBankMatchesIsland(const char* filename, RString island)
{
    char name[256];
    snprintf(name, sizeof(name), "%s", filename);
    char* ext = strrchr(name, '.'); // extension .pbo
    if (!ext || stricmp(ext + 1, "pbo") != 0)
    {
        return false;
    }
    *ext = 0;
    ext = strrchr(name, '.'); // world name
    if (!ext)
    {
        return false;
    }
    return stricmp(ext + 1, island) == 0;
}

struct AddModMPMissionsContext
{
    C3DListBox* lbox;
    RString island;
};

void AddModMPMissionFilesInDir(AddModMPMissionsContext* ctx, const char* modDir, const char* missionsDir)
{
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s%c%s%c*.pbo", modDir, PATH_SEP, missionsDir, PATH_SEP);

    _finddata_t info;
    intptr_t h = _findfirst(pattern, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) == 0 && MPMissionBankMatchesIsland(info.name, ctx->island))
            {
                AddMPMissionBankRow(ctx->lbox, info.name);
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
}

bool AddModMPMissionFiles(RStringB dir, void* context)
{
    if (dir.GetLength() == 0)
    {
        return false;
    }

    auto* ctx = static_cast<AddModMPMissionsContext*>(context);
    AddModMPMissionFilesInDir(ctx, (const char*)dir, GameDirs::MPMissions);
    AddModMPMissionFilesInDir(ctx, (const char*)dir, "mpmissions");
    return false;
}
} // namespace

void DisplayServer::UpdateMissions(RString filename)
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_ISLAND));
    if (!lbox)
    {
        return;
    }
    int index = lbox->GetCurSel();
    RString island = index < 0 ? "" : lbox->GetData(index);

    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_MISSION));
    if (!lbox)
    {
        return;
    }

    RString mission = filename;
    int value = 2;
    if (mission.GetLength() == 0)
    {
        index = lbox->GetCurSel();
        if (index >= 0)
        {
            mission = lbox->GetData(index);
            value = lbox->GetValue(index);
        }
    }

    lbox->ClearStrings();
    if (island.GetLength() == 0)
    {
        return;
    }

    if (GameModuleRegistry::IsRegistered(GameModuleId::Editor))
    {
        index = lbox->AddString(LocalizeString(IDS_MPW_NEW_EDIT));
        lbox->SetValue(index, -2);
        lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 0.25)));
        lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 0.5)));

        index = lbox->AddString(LocalizeString(IDS_MPW_NEW_WIZ));
        lbox->SetValue(index, -1);
        lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 0.25)));
        lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 0.5)));
    }

    char buffer[256];
    ::sprintf(buffer, "%s%c*.%s.pbo", GameDirs::MPMissions, PATH_SEP, (const char*)island);

    _finddata_t info;
    intptr_t h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) == 0)
            {
                AddMPMissionBankRow(lbox, info.name);
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }

    AddModMPMissionsContext modMissions;
    modMissions.lbox = lbox;
    modMissions.island = island;
    ModSystem::EnumDirectories(AddModMPMissionFiles, &modMissions);

    ::sprintf(buffer, "%s%c*.%s", GameDirs::MPMissions, PATH_SEP, (const char*)island);

    h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                char name[256];
                snprintf(name, sizeof(name), "%s", (const char*)info.name);
                char* ext = strrchr(name, '.');
                if (ext)
                {
                    *ext = 0;
                }
                int index = lbox->AddString(name);
                lbox->SetData(index, name);
                lbox->SetValue(index, 1); // public directory
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }

    ::sprintf(buffer, "%s%s%c*.%s", (const char*)GetUserMissionsBase(), GameDirs::MPMissions, PATH_SEP,
              (const char*)island);

    h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                char name[256];
                snprintf(name, sizeof(name), "%s", (const char*)info.name);
                char* ext = strrchr(name, '.');
                if (ext)
                {
                    *ext = 0;
                }
                int index = lbox->AddString(name);
                lbox->SetData(index, name);
                lbox->SetValue(index, 2); // private directory
                lbox->SetFtColor(index, PackedColor(Color(1, 1, 0, 0.5)));
                lbox->SetSelColor(index, PackedColor(Color(1, 1, 0, 1)));
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    lbox->SortItemsByValue();

    int sel = 0;
    for (int i = 0; i < lbox->GetSize(); i++)
    {
        if (stricmp(lbox->GetData(i), mission) == 0 && lbox->GetValue(i) == value)
        {
            sel = i;
        }
    }
    lbox->SetCurSel(sel);

    OnMissionChanged(lbox->GetCurSel());
}

void DisplayServer::OnMissionChanged(int sel)
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_MISSION));
    if (!lbox)
    {
        return;
    }

    /*
        RString tooltip;
        if (sel >= 0) tooltip = lbox->GetText(sel);
        lbox->SetTooltip(tooltip);
    */

    if (GameModuleRegistry::IsRegistered(GameModuleId::Editor) && sel >= 0)
    {
        int value = lbox->GetValue(sel);

        IControl* ctrl = GetCtrl(IDC_SERVER_EDITOR);
        if (ctrl)
        {
            ctrl->ShowCtrl(value == 2);
        }
    }
}

// Gamemaster select mission display
Control* DisplayRemoteMissions::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_SERVER_ISLAND:
        {
            C3DListBox* lbox = new C3DListBox(this, idc, cls);
            int sel = 0;
            // int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
            int m = (Pars >> "CfgWorldList").GetEntryCount();
            for (int j = 0; j < m; j++)
            {
                const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(j);
                if (!entry.IsClass())
                {
                    continue;
                }
                RString name = entry.GetName();

                // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

                // ADDED - check if wrp file exists
                RString fullname = GetWorldName(name);
                if (!QIFStreamB::FileExist(fullname))
                {
                    continue;
                }

                int index = lbox->AddString(Pars >> "CfgWorlds" >> name >> "description");
                lbox->SetData(index, name);
                if (stricmp(name, Glob.header.worldname) == 0)
                {
                    sel = index;
                }
                RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
                RString fullName = FindPicture(textureName);
                if (fullName.GetLength() > 0)
                {
                    fullName.Lower();
                }
                Ref<Texture> texture = GlobLoadTexture(fullName);
                lbox->SetTexture(index, texture);
            }
            lbox->SetCurSel(sel);
            return lbox;
        }
        case IDC_SERVER_MISSION:
            return new CMPMissionsList(this, idc, cls);
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayRemoteMissions::OnChangeDifficulty()
{
    CActiveText* ctrl = dynamic_cast<CActiveText*>(GetCtrl(IDC_SERVER_DIFF));
    if (!ctrl)
    {
        return;
    }
    RString text;
    if (_cadetMode)
    {
        text = LocalizeString(IDS_DIFF_CADET);
    }
    else
    {
        text = LocalizeString(IDS_DIFF_VETERAN);
    }
    ctrl->SetText(text);
}

void DisplayRemoteMissions::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_SERVER_DIFF:
            _cadetMode = !_cadetMode;
            OnChangeDifficulty();
            break;
        case IDC_AUTOCANCEL:
            Exit(idc);
            break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayRemoteMissions::OnLBSelChanged(int idc, int curSel)
{
    if (idc == IDC_SERVER_ISLAND)
    {
        UpdateMissions();
    }
    else if (idc == IDC_SERVER_MISSION)
    {
        OnMissionChanged(curSel);
    }
    Display::OnComboSelChanged(idc, curSel);
}

void DisplayRemoteMissions::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_SERVER_MISSION && curSel >= 0)
    {
        OnButtonClicked(IDC_OK);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplayRemoteMissions::OnSimulate(EntityAI* vehicle)
{
    // update player list
    CListBox* lbox = dynamic_cast<CListBox*>(GetCtrl(IDC_SERVER_PLAYERS));
    if (lbox)
    {
        lbox->SetReadOnly();
        lbox->ShowSelected(false);
        lbox->ClearStrings();

        const AutoArray<PlayerIdentity>& identities = *GetNetworkManager().GetIdentities();
        for (int i = 0; i < identities.Size(); i++)
        {
            const PlayerIdentity& identity = identities[i];
            int index = lbox->AddString(GetIdentityText(identity));
            switch (identity.state)
            {
                case NGSNone:
                case NGSCreating:
                case NGSCreate:
                case NGSLogin:
                case NGSEdit:
                    lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
                    lbox->SetValue(index, 0);
                    break;
                case NGSMissionVoted:
                case NGSPrepareSide:
                case NGSPrepareRole:
                case NGSPrepareOK:
                case NGSDebriefing:
                case NGSDebriefingOK:
                case NGSTransferMission:
                case NGSLoadIsland:
                case NGSBriefing:
                case NGSPlay:
                    lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 1)));
                    lbox->SetValue(index, 2);
                    break;
                default:
                    break;
            }
        }
        lbox->SortItemsByValue();
    }

    if (!GetNetworkManager().CanSelectMission() && !GetNetworkManager().CanVoteMission())
    {
        OnButtonClicked(IDC_AUTOCANCEL);
        return;
    }

    if (GetNetworkManager().GetGameState() == NGSNone)
    {
        OnButtonClicked(IDC_CANCEL);
        return;
    }

    if (GetNetworkManager().GetServerState() < NGSCreate)
    {
        OnButtonClicked(IDC_CANCEL);
        return;
    }
    else if (GetNetworkManager().GetServerState() > NGSCreate)
    {
        OnButtonClicked(IDC_AUTOCANCEL);
        return;
    }

    Display::OnSimulate(vehicle);
}

void DisplayRemoteMissions::UpdateMissions(RString filename)
{
    C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_ISLAND));
    if (!lbox)
    {
        return;
    }
    int index = lbox->GetCurSel();
    RString island = index < 0 ? "" : lbox->GetData(index);

    lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SERVER_MISSION));
    if (!lbox)
    {
        return;
    }

    RString mission = filename;
    if (mission.GetLength() == 0)
    {
        index = lbox->GetCurSel();
        if (index >= 0)
        {
            mission = lbox->GetText(index);
        }
    }

    lbox->ClearStrings();
    if (island.GetLength() == 0)
    {
        return;
    }

    const AutoArray<RString>& names = GetNetworkManager().GetServerMissions();
    for (int i = 0; i < names.Size(); i++)
    {
        RString name = names[i];
        const char* beg = name;
        const char* end = strrchr(beg, '.');
        if (!end || stricmp(end + 1, island) != 0)
        {
            continue;
        }
        int index = lbox->AddString(name.Substring(0, end - beg));
        lbox->SetData(index, name);
    }

    lbox->SortItems();

    int sel = 0;
    for (int i = 0; i < lbox->GetSize(); i++)
    {
        if (stricmp(lbox->GetText(i), mission) == 0)
        {
            sel = i;
        }
    }
    lbox->SetCurSel(sel);

    OnMissionChanged(lbox->GetCurSel());
}

void DisplayRemoteMissions::OnMissionChanged(int sel)
{
    /*
        C3DListBox *lbox = dynamic_cast<C3DListBox *>(GetCtrl(IDC_SERVER_MISSION));
        if (!lbox) return;

        RString tooltip;
        if (sel >= 0) tooltip = lbox->GetText(sel);
        lbox->SetTooltip(tooltip);
    */
}

} // namespace Poseidon

// Multiplayer wizard displays
