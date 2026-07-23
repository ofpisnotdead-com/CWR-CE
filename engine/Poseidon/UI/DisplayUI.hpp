#pragma once

#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/World/Entities/Infantry/Head.hpp>
#include <Poseidon/UI/GameModule.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Core/DownloadWorker.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Network/NetTransport.hpp>

// MasterServerBrowser is the global (non-namespaced) C API backing the
// internet server browser; forward-declare it here so DisplayMultiplayer can
// hold a pointer without pulling in the heavy template header. The mod-catalog
// entry is likewise global; forward-declare it for the workshop fetch.
namespace Poseidon
{
struct MasterServerBrowser;
struct MasterServerServiceModCatalogEntry;
class CKeys : public C3DListBox
{
  protected:
    AutoArray<int> _keys[UAN];
    AutoArray<bool> _collisions[UAN];

    int _mode;
    int _ignoredKey;

  public:
    CKeys(ControlsContainer* parent, int idc, const ParamEntry& cls);

    int GetMode() const { return _mode; }
    void CheckIgnoredKey();

    const AutoArray<int>& GetKeys(int action) const { return _keys[action]; }
    void SetKeys(int action, const AutoArray<int>& keys);
    void RemoveKey(int action);
    void AddKey(int action, int key);

    bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override { return true; }
    bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override { return true; }
    void OnLButtonDown(float x, float y) override;

    void DrawItem(Vector3Par position, Vector3Par down, int i, float alpha) override;

  protected:
    void CheckCollisions(int key);
};

class DisplayConfigure : public Display
{
  protected:
    CKeys* _keys;
    bool _oldJoystickEnabled;
    bool _oldRevMouse;
    bool _oldMouseButtonsReversed;
    float _oldMouseSensitivityX;
    float _oldMouseSensitivityY;

    int _langCbToken = -1;

  public:
    DisplayConfigure(ControlsContainer* parent, bool enableSimulation);
    ~DisplayConfigure() override;

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnSimulate(EntityAI* vehicle) override;
    void OnButtonClicked(int idc) override;
    void OnSliderPosChanged(int idc, float pos) override;

    void RefreshLanguage();

    void Destroy() override;
    /*
        void InitParams();	// set defaults
        void LoadParams();
        void SaveParams();
    */
};

class DisplaySelectIsland : public Display
{
  protected:
    int _exitWhenClose;

  public:
    DisplaySelectIsland(ControlsContainer* parent) : Display(parent)
    {
        Load("RscDisplaySelectIsland");
        _exitWhenClose = -1;
        if (!GameModuleRegistry::IsRegistered(GameModuleId::Editor))
            GetCtrl(IDC_SELECT_ISLAND_WIZARD)->ShowCtrl(false);
    }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    void OnCtrlClosed(int idc) override;
};

class DisplayCustomArcade : public Display
{
  public:
    DisplayCustomArcade(ControlsContainer* parent) : Display(parent)
    {
        Load("RscDisplayCustomArcade");
        InsertGames();
        ShowButtons();
    }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnTreeSelChanged(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;

    bool CanDestroy() override;

  protected:
    void InsertGames();
    void ShowButtons();
};

// Top-level MODS screen (v1: an empty notebook — header + Close only).
// DisplayMultiplayer: animate the notebook shut, then Exit when it finishes
// (OnCtrlClosed). Registered as GameModuleId::Mods via ModsModule::Register().
// Background workshop-catalog fetch state (defined in OptionsUIApp.cpp): a
// detached worker fills it, DisplayMods::OnSimulate merges it in when ready.
struct ModsWorkshopFetch;

class DisplayMods : public Display
{
  protected:
    int _exitWhenClose;
    std::shared_ptr<ModsWorkshopFetch> _workshopFetch;
    bool _workshopStarted = false;
    bool _workshopMerged = false;
    int _sortColumn = 0; // active sort (ModsSortColumn); header click toggles dir
    bool _sortAscending = true;

  public:
    DisplayMods(ControlsContainer* parent);

    // Builds the catalog list as a CModsList (5-column DrawItem) and seeds it.
    // Out-of-line in OptionsUIApp.cpp where CModsList is fully visible.
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;

    // Test-only (triSeedMods): replace the catalog rows with count fake mods.
    void SeedTestMods(int count);

    // Default Name-ascending sort + caret, applied once after Load.
    void ApplyInitialSort();
    // Reflect _sortColumn/_sortAscending on the four header carets (shared
    // UpdateSortCaret helper), mirroring the MP browser's UpdateIcons.
    void UpdateSortCarets();
    // Set the Source cycling-button label for the given mode (0=All/1=Workshop/2=Local).
    void UpdateSourceButton(int mode);
    // Set the Filter button label to reflect the active name filter ("Filter" /
    // "Filter: <text>"). Reads the current filter from the list.
    void UpdateFilterButton();
    // When the name-filter dialog (IDD_MODS_FILTER) closes on OK, read its edit
    // and apply it as the list name filter. Out-of-line (needs CModsList).
    void OnChildDestroyed(int idd, int exit) override;

    // Workshop (remote) catalog: OnSimulate kicks a one-shot background fetch from
    // the master server (papa-bear.cz / --master-server) and merges the result into
    // the list as Workshop rows (shown only; never activated/downloaded for now).
    // Skipped under autotest. MergeWorkshopMods is reused by triSeedWorkshopMods.
    void OnSimulate(EntityAI* vehicle) override;
    void StartWorkshopFetch();
    void MergeWorkshopMods(const std::vector<MasterServerServiceModCatalogEntry>& catalog);

    // Cancel closes the notebook; column-header clicks sort the catalog list.
    // Out-of-line in OptionsUIApp.cpp (needs the full CModsList type).
    void OnButtonClicked(int idc) override;
    bool DoControllerUiAction(ControllerUiAction action) override;

    void OnCtrlClosed(int idc) override
    {
        if (idc == IDC_MODS_NOTEBOOK)
            Exit(_exitWhenClose);
        else
            Display::OnCtrlClosed(idc);
    }
};

// Name-filter dialog for the MODS catalog (RscDisplayModsFilter). One edit
// field seeded with the current filter; the parent (DisplayMods) reads it back
// on OK via OnChildDestroyed, like the MP browser's DisplayPort. OnCreateCtrl is
// out-of-line in OptionsUIApp.cpp (where C3DEdit is in scope).
class DisplayModsFilter : public Display
{
  protected:
    RString _filterName;

  public:
    DisplayModsFilter(ControlsContainer* parent, RString current) : Display(parent)
    {
        _enableSimulation = false;
        _filterName = current;
        Load("RscDisplayModsFilter");
    }
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
};

// Download-progress dialog (RscDisplayModDownload). Agnostic: given a list of
// DownloadTasks and a transport, it runs a background DownloadWorker and shows
// two bars — the current file on top, the whole job below — plus a speed/ETA
// status line. Opened by DisplayMods from Apply when the ticked set includes
// not-yet-downloaded mods; the same dialog serves the MP single-mission
// transfer (one task, no post-step). On success it Exits IDC_OK so the opener
// can proceed; Cancel and an unrecovered failure Exit IDC_CANCEL.
// Out-of-line in OptionsUIApp.cpp (needs the download/control types).
class DisplayModDownload : public Display
{
  protected:
    std::vector<DownloadTask> _tasks;
    DownloadWorker _worker;
    RString _unitNoun;
    RString _prompt;
    enum Phase
    {
        PhasePrompt,
        PhaseRunning,
        PhaseDone,
        PhaseFailed
    };
    Phase _phase;
    bool _started;

    // Set a 3D notebook static/active-text by idc (no-op if absent/mismatched).
    void SetNotebookText(int idc, RString text);
    // (Re)start the worker over _tasks and switch to the running phase.
    void StartDownload();
    // Push the current DownloadWorker snapshot into the bars + text lines.
    void ApplyView();

  public:
    DisplayModDownload(ControlsContainer* parent, std::vector<DownloadTask> tasks, DownloadFileFn transport,
                       const char* unitNoun = "addon", std::function<double()> now = std::function<double()>());

    void OnButtonClicked(int idc) override;
    void OnSimulate(EntityAI* vehicle) override;
};

enum SortColumn
{
    SCServer,
    SCMission,
    SCState,
    SCPlayers,
    SCPing
};

class CSessions : public C3DListBox
{
    friend class DisplayMultiplayer;

  protected:
    AutoArray<SessionInfo> _sessions;
    Ref<Texture> _password;
    Ref<Texture> _version;
    Ref<Texture> _none;

  public:
    CSessions(ControlsContainer* parent, int idc, const ParamEntry& cls);

    int GetSize() override { return VisibleCount(); }
    int FilterRowCount() const override { return _sessions.Size(); }
    RString GetData(int i) override { return _sessions[VisibleRow(i)].guid; }
    RString GetText(int i) override { return _sessions[VisibleRow(i)].name; }
    void DrawItem(Vector3Par position, Vector3Par down, int i, float alpha) override;

    void Sort(SortColumn column, bool ascending);
};

// Local install/selection state of a catalog mod.
// Only this drives the coloured State column; everything else is green-CRT.
// Where a catalog mod comes from (storage.md sec 4): Workshop = from the PAPA
// BEAR catalog into the managed mods root; Local = a user-dropped / dev mod
// found outside it. Drives the Source column + the Source filter.
enum class ModRowSource
{
    Workshop,
    Local,
};

enum class ModRowState
{
    Missing,    // not on disk
    Downloaded, // installed, not in the active set
    Active,     // installed + in the active set
};

struct ModRow
{
    bool checked = false; // selection tick -> drives the active set on Apply
    RString modId;
    RString folderName;
    RString name;
    RString version;
    int64_t sizeBytes = 0;
    ModRowState state = ModRowState::Missing;
    ModRowSource source = ModRowSource::Workshop;
    RString downloadUrl; // Workshop rows: PBO URL from the catalog (empty for local)
};

// Sortable columns for the MODS catalog table (CModsList::Sort).
enum ModsSortColumn
{
    MSCName,
    MSCVersion,
    MSCSize,
    MSCState,
    MSCSource, // appended last: Name/Version/Size/State keep indices 0-3
};

// Catalog table - a 3D notebook listbox whose DrawItem renders the columns
// (checkbox / Name / Version / Size / State), mirroring CSessions on the MP
// browser. Implemented in DisplayUIMultiplayer.cpp (same 3D draw stack).
class CModsList : public C3DListBox
{
  protected:
    AutoArray<ModRow> _modRows;
    int _sourceMode = 0; // Source filter: 0=All, 1=Workshop, 2=Local
    RString _nameFilter; // name search (Filter dialog); empty = no name filter

  public:
    CModsList(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DListBox(parent, idc, cls) {}

    int GetSize() override { return VisibleCount(); }
    int FilterRowCount() const override { return _modRows.Size(); }
    // Row passes when it matches the Source filter AND its name contains the
    // (case-insensitive) name filter. Both empty/All -> every row visible.
    bool FilterRowVisible(int actualRow) const override
    {
        const ModRow& m = _modRows[actualRow];
        if (_sourceMode == 1 && m.source != ModRowSource::Workshop)
            return false;
        if (_sourceMode == 2 && m.source != ModRowSource::Local)
            return false;
        if (_nameFilter.GetLength() > 0 && !ContainsNoCase(m.name, _nameFilter))
            return false;
        return true;
    }
    // Case-insensitive substring test (empty needle matches anything).
    static bool ContainsNoCase(const char* hay, const char* needle);
    RString GetData(int i) override { return _modRows[VisibleRow(i)].modId; }
    RString GetText(int i) override { return _modRows[VisibleRow(i)].name; }
    void Sort(ModsSortColumn column, bool ascending);
    void DrawItem(Vector3Par position, Vector3Par down, int i, float alpha) override;

    // Selection -> mod set (drives Apply). CheckedModIds joins the checked rows'
    // @modId by ',' ("" if none, harness-readable); BuildModPath prefixes each with
    // its source's root (local vs workshop) and joins by ';' for the re-mount.
    RString CheckedModIds() const;
    RString BuildModPath(const char* localRoot, const char* workshopRoot) const;

    // Source filter (0=All, 1=Workshop, 2=Local). Re-derives the visible rows.
    void SetSourceMode(int mode)
    {
        _sourceMode = mode;
        ApplyFilters();
    }
    int GetSourceMode() const { return _sourceMode; }
    // Name search (Filter dialog). Re-derives the visible rows.
    void SetNameFilter(RString f)
    {
        _nameFilter = f;
        ApplyFilters();
    }
    RString GetNameFilter() const { return _nameFilter; }
    // Recompute the visible set after rows or the filter change; reset the cursor.
    void ApplyFilters()
    {
        SetFilterActive(_sourceMode != 0 || _nameFilter.GetLength() > 0);
        SetCurSel(VisibleCount() > 0 ? 0 : -1, false);
    }
    const AutoArray<ModRow>& GetRows() const { return _modRows; }
    void SetRows(const AutoArray<ModRow>& rows)
    {
        _modRows = rows;
        ApplyFilters();
    }
    // The checkbox is the leftmost column; its width (fraction of the list) is
    // shared with DrawItem so the click target lines up with what is drawn.
    static constexpr float kCheckboxColumnWidth = 0.07f;
    void ToggleChecked(int displayIdx)
    {
        int a = VisibleRow(displayIdx);
        if (a >= 0 && a < _modRows.Size())
            _modRows[a].checked = !_modRows[a].checked;
    }
    // Toggle the tick ONLY when the click lands in the small checkbox column of a
    // REAL row. u,v are the click position within the list (Control3D _u/_v,
    // 0..1): u gates the checkbox column, v picks the row under the cursor. The
    // row comes from v, never from GetCurSel() — at button-down the selection
    // still holds the PREVIOUS row, so toggling it un-ticked a different line when
    // you clicked between rows. A click below the last row toggles nothing.
    void HandleRowClick(float u, float v)
    {
        if (u < 0.0f || u >= kCheckboxColumnWidth)
            return;
        if (v < 0.0f || v > 1.0f || _rows <= 0.0f)
            return;
        int slot = static_cast<int>(v * _rows);
        if (slot < 0 || slot >= static_cast<int>(_rows))
            return;
        int displayRow = _topString + slot;
        if (displayRow < 0 || displayRow >= GetSize())
            return;
        ToggleChecked(displayRow);
    }
    // Test hook: a checkbox click on display row `row` at horizontal fraction `u`,
    // with v synthesised from the row's slot so it drives the same HandleRowClick
    // path a real mouse click does (which derives the row back from v).
    void ClickRowCheckbox(int row, float u)
    {
        if (_rows <= 0.0f)
            return;
        float v = (static_cast<float>(row - _topString) + 0.5f) / _rows;
        HandleRowClick(u, v);
    }
    void OnLButtonDown(float x, float y) override
    {
        C3DListBox::OnLButtonDown(x, y); // sets _u/_v via IsInside(x,y)
        HandleRowClick(_u, _v);
    }
};

#define BROWSING_SOURCE_ENUM(type, prefix, XX) \
    XX(type, prefix, LAN)                      \
    XX(type, prefix, Internet)                 \
    XX(type, prefix, Remote)

DECLARE_ENUM(BrowsingSource, BS, BROWSING_SOURCE_ENUM)

struct SessionFilter : SerializeClass
{
    RString serverName;
    RString missionName;
    int maxPing;
    int minPlayers;
    int maxPlayers;
    bool fullServers;
    bool passwordedServers;

    SessionFilter()
    {
        maxPing = 0;
        minPlayers = 0;
        maxPlayers = 0;
        fullServers = true;
        passwordedServers = true;
    }
    LSError Serialize(ParamArchive& ar) override;
};

class DisplayMultiplayer : public Display
{
  protected:
    RString _ipAddress;
    int _portLocal;
    int _portRemote;

    RString _password;
    int _exitWhenClose;
    CSessions* _sessions;

    // Internet (BSInternet) server-list browser, backed by the MasterServer
    // service. Created lazily on the first refresh, destroyed with the display.
    MasterServerBrowser* _serverList = nullptr;

    BrowsingSource _source;

    SortColumn _sort;
    bool _ascending;
    int _langCbToken = -1;

    RString _sortUp;
    RString _sortDown;

    SessionFilter _filter;

    bool _refresh;

    bool _refreshing;
    bool _internetPingProbe = false;
    int _internetPingProbeFrames = 0;

    // Test-only (triSeedSessions): once set, UpdateServerList is a no-op so a
    // LAN/master refresh can't wipe the seeded rows.
    bool _testSeeded = false;

    // Pending modded-server join: the target server + its required mod set, carried
    // from the Join click across the requirements + download dialogs to the apply step.
    RString _joinGuid;
    RString _joinTargetIp;
    int _joinTargetPort = 0;
    RString _joinServerMod;
    bool _joinServerEqualMod = false;
    std::vector<DownloadTask> _joinTasks; // missing mods to fetch before the join (may be empty)

  public:
    DisplayMultiplayer(ControlsContainer* parent);
    ~DisplayMultiplayer() override;

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnSimulate(EntityAI* vehicle) override;
    void OnButtonClicked(int idc) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnCtrlClosed(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    bool DoControllerUiAction(ControllerUiAction action) override;

    void SetProgress(float progress);

    // Test-only (triSeedSessions): replace the browser list with count fake
    // sessions so the table's rows/sort can be characterized in the harness.
    void SeedTestSessions(int count);
    int GetVisibleSessionPingForTest(int row) const;

  protected:
    // Join a server that requires mods: resolve the required set against the catalog,
    // then download (if missing) / re-mount (to switch) / connect directly. The
    // download dialog and re-mount finish asynchronously via OnChildDestroyed + the
    // PendingConnect hook. `BeginModdedJoin` returns false when there's nothing modded
    // to do (the caller falls back to the plain JoinSession path).
    bool BeginModdedJoin(const SessionInfo& info);
    void ApplyModdedJoin();
    void UpdateAddress(RString ipAddress, int port);
    void UpdatePassword(RString password);
    void UpdateSessions();
    void UpdateServerList();
    void UpdateInternetSessionPings();
    int GetPort();
    void SetPort(int port);
    void SetSource(BrowsingSource source);
    void UpdateIcons();
    void UpdateFilter();

    void LoadParams();
    void SaveParams();
    void RefreshLanguage();
};

class DisplayPassword : public Display
{
  protected:
    RString _password;

  public:
    DisplayPassword(ControlsContainer* parent, RString password) : Display(parent)
    {
        _enableSimulation = false;
        _password = password;
        Load("RscDisplayPassword");
    }
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
};

// One-screen "join a modded server" approval: shows the server name, the mod diff
// (already formatted by the caller from a ServerModResolution), and a password field.
// IDC_OK exits; the parent reads the password back and proceeds.
class DisplayJoinRequirements : public Display
{
  protected:
    RString _title;
    RString _diff;
    RString _password;
    RString _okText;

  public:
    DisplayJoinRequirements(ControlsContainer* parent, RString title, RString diff, RString password,
                            RString okText = RString())
        : Display(parent), _title(title), _diff(diff), _password(password), _okText(okText)
    {
        _enableSimulation = false;
        Load("RscDisplayJoinRequirements");
    }
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
};

class DisplayPort : public Display
{
  protected:
    int _port;

  public:
    DisplayPort(ControlsContainer* parent, int port) : Display(parent)
    {
        _enableSimulation = false;
        _port = port;
        Load("RscDisplayPort");
    }
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
};

class DisplayServer : public Display
{
  public:
    bool _cadetMode;

    DisplayServer(ControlsContainer* parent) : Display(parent)
    {
        _enableSimulation = false;
        Load("RscDisplayServer");
        UpdateMissions();

        _cadetMode = true;
        LoadParams();
        OnChangeDifficulty();
        if (!GameModuleRegistry::IsRegistered(GameModuleId::Editor))
            GetCtrl(IDC_SERVER_EDITOR)->ShowCtrl(false);
    }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnChildDestroyed(int idd, int exit) override;

  protected:
    void UpdateMissions(RString filename = "");
    void OnMissionChanged(int sel);

    bool SetMission(bool editor);

    void OnChangeDifficulty();

    void LoadParams();
    void SaveParams();
};

class DisplayRemoteMissions : public Display
{
  public:
    bool _cadetMode;

    DisplayRemoteMissions(ControlsContainer* parent) : Display(parent)
    {
        _enableSimulation = false;
        Load("RscDisplayRemoteMissions");
        UpdateMissions();

        _cadetMode = false;
        OnChangeDifficulty();
        GetCtrl(IDC_SERVER_EDITOR)->ShowCtrl(false);
    }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnSimulate(EntityAI* vehicle) override;

  protected:
    void UpdateMissions(RString filename = "");
    void OnMissionChanged(int sel);

    void OnChangeDifficulty();
};

class DisplayIPAddress : public Display
{
  public:
    DisplayIPAddress(ControlsContainer* parent) : Display(parent)
    {
        _enableSimulation = false;
        Load("RscDisplayIPAddress");
    }
    bool CanDestroy() override;
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
};

class DisplayWizardTemplate : public Display
{
  public:
    RString _world;
    bool _multiplayer;

    DisplayWizardTemplate(ControlsContainer* parent, RString world, bool multiplayer);

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;

  protected:
    void OnTemplateChanged();
    void RefreshLanguage();
};

class DisplayClient : public Display
{
    bool _wasConnected = false;

  public:
    DisplayClient(ControlsContainer* parent) : Display(parent)
    {
        _enableSimulation = false;
        Load("RscDisplayClient");
        SetCursor(nullptr);
    }

    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    void OnSimulate(EntityAI* vehicle) override;
};

class DisplayMultiplayerSetup : public Display
{
  protected:
    int _player;
    RString _message;
    Ref<Font> _messageFont;
    float _messageSize;

    TargetSide _side;

    bool _init;
    bool _transferMission;
    bool _transferOverlayVisible;
    unsigned _transferOverlayShows;
    bool _loadIsland;
    bool _play;

    bool _sessionLocked;

    bool _allDisabled;

    bool _autoAssigned;

    bool _dragging;
    int _dragPlayer;
    RString _dragName;
    Ref<Font> _dragFont;
    float _dragSize;
    PackedColor _dragColor;

    Ref<Texture> _none;
    Ref<Texture> _westUnlocked;
    Ref<Texture> _westLocked;
    Ref<Texture> _eastUnlocked;
    Ref<Texture> _eastLocked;
    Ref<Texture> _guerUnlocked;
    Ref<Texture> _guerLocked;
    Ref<Texture> _civlUnlocked;
    Ref<Texture> _civlLocked;

  public:
    DisplayMultiplayerSetup(ControlsContainer* parent);
    RString GetMessageForTest() const { return _message; }
    unsigned GetTransferOverlayShowsForTest() const { return _transferOverlayShows; }
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;

    void OnButtonClicked(int idc) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnChildDestroyed(int idd, int exit) override;
    void OnSimulate(EntityAI* vehicle) override;
    void OnDraw(EntityAI* vehicle, float alpha) override;

    void OnLBDrag(int idc, int curSel) override;
    void OnLBDragging(float x, float y) override;
    void OnLBDrop(float x, float y) override;

  protected:
    void Preinit();
    void Init();
    void Update();
    bool CanDrag(int player);
};

// Server and client briefing

class DisplayServerGetReady : public DisplayGetReady
{
    typedef DisplayGetReady base;

  public:
    DisplayServerGetReady(ControlsContainer* parent);
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    void Destroy() override;
    void OnSimulate(EntityAI* vehicle) override;
};

class DisplayClientGetReady : public DisplayGetReady
{
    typedef DisplayGetReady base;
    int _autoReadyFrames = -1; // frames until auto-ready (-1=disabled)
    bool _autoReadySent = false;
    bool _launched = false; // true after user clicks "I'm Ready"
    void Launch();          // mark briefing as read, hide OK
  public:
    DisplayClientGetReady(ControlsContainer* parent);
    void Destroy() override;
    void OnButtonClicked(int idc) override;
    void OnDraw(EntityAI* vehicle, float alpha) override;
    void OnSimulate(EntityAI* vehicle) override;
};

class DisplayMPPlayers : public Display
{
  protected:
    PackedColor _color;
    PackedColor _colorWest;
    PackedColor _colorEast;
    PackedColor _colorCiv;
    PackedColor _colorRes;

  public:
    DisplayMPPlayers(ControlsContainer* parent, bool init = true) : Display(parent)
    {
        if (init)
        {
            _enableSimulation = true;

            // Remaster variant adds the client-local Mute/Ignore buttons next
            // to the admin-only Kick/Ban (resources/menu/mpPlayers.hpp).
            Load("RscDisplayMPPlayersRemaster");
            UpdatePlayers();
            UpdatePlayerInfo();
        }

        const ParamEntry& cls = Pars >> "CfgInGameUI" >> "MPTable";
        _color = GetPackedColor(cls >> "color");
        _colorWest = GetPackedColor(cls >> "colorWest");
        _colorEast = GetPackedColor(cls >> "colorEast");
        _colorCiv = GetPackedColor(cls >> "colorCiv");
        _colorRes = GetPackedColor(cls >> "colorRes");

        InputSubsystem::Instance().ChangeGameFocus(+1);
    }
    ~DisplayMPPlayers() override { InputSubsystem::Instance().ChangeGameFocus(-1); }

    void OnButtonClicked(int idc) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnSimulate(EntityAI* vehicle) override;

  protected:
    void UpdatePlayers();
    void UpdatePlayerInfo();
};

class DisplayClientWait : public DisplayMPPlayers
{
  public:
    DisplayClientWait(ControlsContainer* parent) : DisplayMPPlayers(parent, false)
    {
        _enableSimulation = false;

        Load("RscDisplayClientWait");
        UpdatePlayers();
        UpdatePlayerInfo();
    }

    void OnButtonClicked(int idc) override;
    void OnSimulate(EntityAI* vehicle) override;
};

class DisplayLogin : public Display
{
  protected:
    int _exitWhenClose;

  public:
    DisplayLogin(ControlsContainer* parent) : Display(parent) { Load("RscDisplayLogin"); }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnCtrlClosed(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;

    bool CanDestroy() override;
};

class CHead : public ControlObject
{
  protected:
    Ref<LODShapeWithShadow> _manShape;
    Ref<LODShapeWithShadow> _womanShape;

    SRef<HeadType> _manHeadType;
    SRef<HeadType> _womanHeadType;

    SRef<Head> _manHead;
    SRef<Head> _womanHead;

    Poseidon::Foundation::UITime _lastSimulation;

    bool _woman;

  public:
    CHead(ControlsContainer* parent, int idc, const ParamEntry& cls);
    void Animate(int level) override;
    void Deanimate(int level) override;
    void OnDraw(float alpha) override;
    void Simulate();
    void SetFace(RString name);
    void SetGlasses(RString name);
    void AttachWave(IWave* wave, float freq = 1) override;
    Head* GetHead() { return _woman ? _womanHead : _manHead; }
    HeadType* GetHeadType() { return _woman ? _womanHeadType : _manHeadType; }
    LODShapeWithShadow* GetHeadShape() { return _woman ? _womanShape.GetRef() : _manShape.GetRef(); }
};

class DisplayNewUser : public Display
{
  public:
    RString _name;
    bool _edit;

  protected:
    int _langCbToken = -1;
    bool _doPreview;

    int _exitWhenClose;

    RString _face;
    RString _glasses;
    RString _speaker;
    RString _squad;
    float _pitch;

    DWORD _previewTime;
    RString _previewSpeaker;
    float _previewPitch;
    Ref<IWave> _previewSpeech;

    Ref<CHead> _head;

  public:
    DisplayNewUser(ControlsContainer* parent, RString name, bool edit);
    ~DisplayNewUser() override;

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    ControlObject* OnCreateObject(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnSliderPosChanged(int idc, float pos) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnObjectMoved(int idc, Vector3Par offset) override;
    void OnCtrlClosed(int idc) override;
    void OnSimulate(EntityAI* vehicle) override;
    bool CanDestroy() override;

  protected:
    void Preview(RString speaker, float pitch);
    void RefreshLanguage();
};

class DisplayMissionEnd : public Display
{
  public:
    int _quotation;

  public:
    DisplayMissionEnd(ControlsContainer* parent /*, bool editor = false*/);
    ~DisplayMissionEnd() override;
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;

    void OnButtonClicked(int idc) override;
};

// Mission, intro, outro, campaign intro displays

class DisplayOutro : public DisplayCutscene
{
  public:
    EndMode _mode;

  public:
    DisplayOutro(ControlsContainer* parent, EndMode mode);
};

class DisplayAward : public DisplayCutscene
{
  public:
    DisplayAward(ControlsContainer* parent, RString cutscene);
};

class DisplayCampaignIntro : public DisplayCutscene
{
  public:
    DisplayCampaignIntro(ControlsContainer* parent, RString cutscene);
};

class DisplayInterrupt : public Display
{
  public:
    DisplayInterrupt(ControlsContainer* parent);
    ~DisplayInterrupt() override;

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    ControllerUiScene GetControllerUiScene() const override;
    bool DoControllerUiAction(ControllerUiAction action) override;

  protected:
    void RefreshLanguage();
};

class DisplayMain : public Display
{
  protected:
    EndMode _end;
    RString _version;
    int _langCbToken = -1;

  public:
    DisplayMain(ControlsContainer* parent);
    ~DisplayMain() override;

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    void OnDraw(EntityAI* vehicle, float alpha) override;

    bool DoKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    bool DoControllerUiAction(ControllerUiAction action) override;

    void DestroyHUD(int exit) override;

  protected:
    void RefreshLanguage();
    //	void LoadHeader();
};

#if _ENABLE_CHEATS
class DisplayDebug : public Display
{
  protected:
    int _current;
    GameState* _gs;

  public:
    DisplayDebug(ControlsContainer* parent, GameState* gs, const char* cls = "RscDisplayDebug");
    ~DisplayDebug();
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls);
    void OnButtonClicked(int idc);
    bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
    void OnDraw(EntityAI* vehicle, float alpha);

    void DestroyHUD(int exit);

  protected:
    void UpdateLog(CListBox* lbox);
};
#endif

RString FindPicture(RString name);
RString GetCampaignDirectory(RString campaign);
RString GetUserDirectory();
RString GetUserMissionsBase();
RString GetMissionDirectory();
RString GetSaveDirectory();

RString GetBaseDirectory();
RString GetBaseSubdirectory();
void SetBaseDirectory(RString dir);
void SetMission(RString world, RString mission);

bool ProcessTemplateName(RString name);
bool ProcessTemplateName(RString name, RString dir);
bool ParseMission(bool multiplayer);
bool ParseIntro();
bool ParseCutscene(RString name, bool multiplayer);

void RunInitScript();

RString GetKeyName(int dikCode);

extern bool ContinueSaved;

} // namespace Poseidon

using ::Poseidon::FindPicture;
using ::Poseidon::GetBaseDirectory;
using ::Poseidon::GetBaseSubdirectory;
using ::Poseidon::GetKeyName;
using ::Poseidon::GetMissionDirectory;
using ::Poseidon::GetUserDirectory;
using ::Poseidon::GetUserMissionsBase;
using ::Poseidon::ParseMission;
using ::Poseidon::RunInitScript;
using ::Poseidon::SetBaseDirectory;
using ::Poseidon::SetMission;
