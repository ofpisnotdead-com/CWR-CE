#pragma once

#include <Poseidon/UI/Map/UIMapBase.hpp>

namespace Poseidon
{

class DisplayNotebook : public Display
{
protected:

	WeatherState _lastWeather;

public:

	DisplayNotebook(ControlsContainer *parent);

	virtual WeatherState GetWeather() = 0;
	virtual Clock GetTime() = 0;
	virtual bool GetPosition(Point3 &pos) = 0;

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnDraw(EntityAI *vehicle, float alpha) override;
};

enum CursorType
{
	CursorArrow,
	CursorTrack,
	CursorMove,
	CursorScroll
};

class DisplayMapEditor : public DisplayNotebook
{
protected:
	CursorType _cursor;
public:

	DisplayMapEditor(ControlsContainer *parent);

	virtual CStaticMap *GetMap() = 0;
	void OnSimulate(EntityAI *vehicle) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnDraw(EntityAI *vehicle, float alpha) override;
};

// Main map (ingame)

struct CommandInfo
{
	OLink<AIGroup> grp;
	int id;
	CommandState state;
	OLink<AISubgroup> subgrp;
	Vector3 position;

	LSError Serialize(ParamArchive &ar);
};

class DisplayMap;

struct MapUnitInfo
{
	OLink<AIUnit> unit;
	float time;	// report status time
	int x;
	int z;

	LSError Serialize(ParamArchive &ar);
};

struct MapEnemyInfo
{
	TargetId id;
	const VehicleType *type;
	float time;	// report status time
	int x;
	int z;

	LSError Serialize(ParamArchive &ar);
};

struct MainMapInfo : public SerializeClass
{
	AutoArray<MapUnitInfo> unitInfo;
	AutoArray<MapEnemyInfo> enemyInfo;
	AutoArray<CommandInfo> commands;

	LSError Serialize(ParamArchive &ar) override;
	void Clear();
};

class CStaticMapMain : public CStaticMap
{
protected:
#if _ENABLE_CHEATS
	bool _showUnits;
	bool _showTargets;
	bool _showSensors;
	bool _showVariables;
#endif

	int _activeMarker;

	MapTypeInfo _infoCommand;

	PackedColor _colorActiveMarker;
	float _sizeActiveMarker;

	Ref<Texture> _iconPosition;

public:

	CStaticMapMain(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonClick(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void ProcessCheats() override;
	void DrawExt(float alpha) override;

	DisplayMap *GetParent();
	void Center() override;

	void SetActiveMarker(int marker) {_activeMarker = marker;}

	void AddUnitInfo(AIUnit *unit);
	void AddEnemyInfo
	(
		TargetType *id, const VehicleType *type, int x, int z
	);

	LSError Serialize(ParamArchive &ar);

protected:
	AIUnit *GetMyUnit();
	AICenter *GetMyCenter();

	SignInfo FindSign(float x, float y) override;

	void DrawExposure();
	void DrawCost();
	void DrawUnits(AICenter *center);

	void IssueMove(float x, float y);
	void IssueWatch(float x, float y);
	void IssueAttack(TargetType *target);
	void IssueGetIn(TargetType *target);

	void DrawCommands();
	void DrawInfo();
	void DrawMarkers();
	void DrawSensors();
	void DrawWaypoints(AICenter *center, AIGroup *myGroup);
	DrawCoord GetWaypointPosition(const ArcadeWaypointInfo &wInfo);

	void DrawLabel(struct SignInfo &info, PackedColor color) override;


	void DrawExposureEnemy
	(
		AICenter *center,
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);

	void DrawExposureUnknown
	(
		AICenter *center,
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);
	void DrawCost

	(
		AISubgroup *subgroup, float invCost,
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);

	void DrawInfo
	(
		float x, float y, float xStep, float yStep, int i, int j
	);


	void FindUnit(AICenter *center, Vector3Par pt, SignInfo &info, float &minDist);

	void FindWaypoint
	(
		AICenter *myCenter, AIGroup *myGroup,
		Vector3Par pt, SignInfo &info, float &minDist
	);
};

class CUnitsSelector : public CCheckBoxes
{
public:
	CUnitsSelector(ControlsContainer *parent, int idc, const ParamEntry &cls)
		: CCheckBoxes(parent, idc, cls) {}

	PackedBoolArray GetArray() const override;
};

class CWarrant : public Control
{
protected:
	Ref<Font> _font;
	float _size;
	PackedColor _color;

public:
	CWarrant(ControlsContainer *parent, int idc, const ParamEntry &cls);
	void OnDraw(float alpha) override;
};

class DisplayInsertMarker : public Display
{
public:
	RString _text;
	int _picture;
	int _color;
	float _x, _y;
	float _cx, _cy, _cw, _ch;

	// direction of marker in DEG
	int m_dir;
protected:
	int _exitKey;
	int _exitVK;

public:

	DisplayInsertMarker(ControlsContainer *parent, float x, float y, float cx, float cy, float cw, float ch, bool enableSimulation);
	~DisplayInsertMarker() override;

	// Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls);
	void OnButtonClicked(int idc) override;
	void OnSimulate(EntityAI *vehicle) override;
	void Destroy() override;

protected:
	void UpdatePicture();
	void PrevPicture();
	void NextPicture();
	void PrevColor();
	void NextColor();

	// change direction
	void ChangeDir(int delta);
};

#define WEAPON_SLOTS			5
#define MAGAZINE_SLOTS		14
#define TOTAL_SLOTS				WEAPON_SLOTS + MAGAZINE_SLOTS

struct UnitWeaponsInfo : public SerializeClass
{
	OLink<AIUnit> unit;
	RString name;
	int weaponSlots;
	Ref<WeaponType> weapons[WEAPON_SLOTS];
	Ref<Magazine> magazines[MAGAZINE_SLOTS];

	UnitWeaponsInfo() {weaponSlots = 0;}

	UnitWeaponsInfo(AIUnit *u);

	void RemoveWeapon(const WeaponType *weapon)
	{
		for (int i=0; i<WEAPON_SLOTS; i++)
			if (weapons[i] == weapon) weapons[i] = nullptr;
	}
	void RemoveMagazine(const Magazine *magazine)
	{
		for (int i=0; i<MAGAZINE_SLOTS; i++)
			if (magazines[i] == magazine) magazines[i] = nullptr;
	}

	bool IsMagazineUsable(const MagazineType *type);

	LSError Serialize(ParamArchive &ar) override;
};

struct WeaponsInfo : public SerializeClass
{
	AutoArray<UnitWeaponsInfo> _weapons;
	RefArray<WeaponType> _weaponsPool;
	RefArray<Magazine> _magazinesPool;

	bool Load(RString filename);
	bool Save(RString filename);
	bool Import(const ParamEntry &superclass);
	void Apply();
	Ref<Magazine> RemovePoolMagazine(const MagazineType *type);

	LSError Serialize(ParamArchive &ar) override;
};

enum ObjectiveStatus
{
	OSActive,
	OSDone,
	OSFailed,
	OSHidden
};

class DisplayMap : public Display
{
protected:
	CHTML *_briefing;
	CStatic *_name;
	CActiveText *_bookmark1;
	CActiveText *_bookmark2;
	CActiveText *_bookmark3;
	CActiveText *_bookmark4;
	CActiveText *_radioAlpha;
	CActiveText *_radioBravo;
	CActiveText *_radioCharlie;
	CActiveText *_radioDelta;
	CActiveText *_radioEcho;
	CActiveText *_radioFoxtrot;
	CActiveText *_radioGolf;
	CActiveText *_radioHotel;
	CActiveText *_radioIndia;
	CActiveText *_radioJuliet;
	CStatic *_gpsCtrl;
	CStaticMapMain *_map;

	Ref<Compass> _compass;
	Ref<Watch> _watch;
	Ref<ControlObjectContainer> _walkieTalkie;
	Ref<Notepad> _notepad;
	Ref<ControlObjectContainer> _warrant;
	Ref<ControlObjectContainer> _gps;

//	bool _debriefing;
	bool _selectWeapons;
	bool _needsHUDRefresh;

	int _currentUnit;
	int _currentSlot;
	WeaponsInfo _weaponsInfo;

	CursorType _cursor;
public:

	DisplayMap(ControlsContainer *parent, RString resource);
	~DisplayMap() override;

	CStaticMapMain *GetMap() {return _map;}
	RString GetRadioTexts();

	bool IsShownMap() const;
	bool IsShownCompass() const;
	bool IsShownWatch() const;
	bool IsShownWalkieTalkie() const;
	bool IsShownNotepad() const;
	bool IsShownWarrant() const;
	bool IsShownGPS() const;
	void ShowMap(bool show = true);
	// Test hook: the briefing/notes HTML control (holds the note in-page links).
	CHTML *GetBriefingControl() { return _briefing; }
	void ShowCompass(bool show = true);
	void ShowWatch(bool show = true);
	void ShowWalkieTalkie(bool show = true);
	void ShowNotepad(bool show = true);
	void ShowWarrant(bool show = true);
	void ShowGPS(bool show = true);

	virtual bool IsInGame() const {return false;}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	ControlObject *OnCreateObject(int type, int idc, const ParamEntry &cls) override;

	void OnChildDestroyed(int idd, int exit) override;

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnDraw(EntityAI *vehicle, float alpha) override;
	void OnSimulate(EntityAI *vehicle) override;

	void OnButtonClicked(int idc) override;
	void OnHTMLLink(int idc, RString link) override;

	void ResetHUD() override;

	void AdjustMapVisibleRect();

	void UpdatePlan();

	virtual void SwitchBriefingSection(RString section);

	void LoadWeapons(RString filename) {_weaponsInfo.Load(filename);}
	void SaveWeapons(RString filename) {_weaponsInfo.Save(filename);}

	void UpdateWeaponsInBriefing();

	LSError Serialize(ParamArchive &ar) override;

protected:
    void RefreshLanguage();
    void ReloadBriefingContent(RString activeSection = RString());
    void UpdateMissionName();
    void LoadParams();
	void SaveParams();
	LSError SerializeParams(ParamArchive &ar);

	void CreateWeaponsPage();
	void CreateWeaponsPoolPage(int slot);
	void UpdateUnitsInBriefing();
//	void UpdateDebriefing();

	void RemoveUnusableMagazines(UnitWeaponsInfo &info);
	void AddUsableMagazines(UnitWeaponsInfo &info, const WeaponType *weapon, int from, int to);

	void AddUsableHandGunMagazines(UnitWeaponsInfo &info, const WeaponType *weapon, int from, int to);

	void SetRadioText();
	void Init();

//	void SwitchDebriefing(bool debriefing = true);

friend void SetCommandState(int id, CommandState state, AISubgroup *subgrp);
};

class DisplayMainMap : public DisplayMap
{
public:

	DisplayMainMap(ControlsContainer *parent);
	void DestroyHUD(int exit) override;
	bool IsInGame() const override {return true;}
};

class DisplayGetReady : public DisplayMap
{
protected:
	bool _soundPlanPlayed = false;
	bool _soundNotesPlayed = false;
	bool _soundGearPlayed = false;
	bool _soundGroupPlayed = false;

	Ref<IWave> _sound;

public:

	DisplayGetReady(ControlsContainer *parent);

	DisplayGetReady(ControlsContainer *parent, RString resource);
	~DisplayGetReady() override;
	void OnButtonClicked(int idc) override;
	void Destroy() override;
	void SwitchBriefingSection(RString section) override;

protected:

	void PlaySound(RString name);
};


class DisplayDebriefing : public Display
{
protected:
	CHTMLContainer *_left;
	CHTMLContainer *_right;
	CHTMLContainer *_stats;
	AIStats _oldStats;
	bool _animation;
	bool _server;
	bool _client;

public:

	DisplayDebriefing(ControlsContainer *parent, bool animation);
	void Destroy() override;

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnButtonClicked(int idc) override;
	void OnHTMLLink(int idc, RString link) override;
	void OnSimulate(EntityAI *vehicle) override;

protected:
    void RefreshLanguage();
    void CreateDebriefing();
};

class DisplayClientDebriefing : public DisplayDebriefing
{
	typedef DisplayDebriefing base;

public:

	DisplayClientDebriefing(ControlsContainer *parent, bool animation);
	void OnSimulate(EntityAI *vehicle) override;
};

inline DisplayMap *CStaticMapMain::GetParent()
{
	PoseidonAssert(dynamic_cast<DisplayMap *>(_parent));
	return static_cast<DisplayMap *>(_parent);
}

class CStaticMapWizard : public CStaticMap
{
protected:
	ArcadeTemplate *_template;

public:

	CStaticMapWizard
	(
		ControlsContainer *parent, int idc, const ParamEntry &cls,
		float scaleMin, float scaleMax, float scaleDefault
	);

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonClick(float x, float y) override;
	void OnMouseHold(float x, float y, bool active = true) override;

	void DrawExt(float alpha) override;

	void Center() override;

	void SetTemplate(ArcadeTemplate *templ) {_template = templ;}
	const ArcadeTemplate *GetTemplate() const {return _template;}
protected:
	SignInfo FindSign(float x, float y) override;

	void DrawMarkers();

	void DrawLabel(struct SignInfo &info, PackedColor color) override;
};

class DisplayWizardMap : public Display
{
protected:
	CursorType _cursor;

public:
	RString _world;
	RString _t;
	bool _bank;
	RString _name;
	CStaticMapWizard *_map;
	ArcadeTemplate _mission;
	bool _multiplayer;


	DisplayWizardMap(ControlsContainer *parent, RString world, RString t, bool bank, RString name, bool multiplayer);
	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnButtonClicked(int idc) override;
	void OnSimulate(EntityAI *vehicle) override;
	void OnChildDestroyed(int idd, int exit) override;

protected:
	bool CreateMission();
};

// Arcade map (mission editor)

enum InsertMode
{
	IMUnits,
	IMGroups,
	IMSensors,
	IMWaypoints,
	IMSynchronize,
	IMMarkers,
	IMN
};

class CStaticMapArcadeViewer : public CStaticMap
{
protected:
	ArcadeTemplate *_template;
	OLinkArray<EntityAI> _objects;

	float _sizeEmptyMarker;
public:

	CStaticMapArcadeViewer
	(
		ControlsContainer *parent, int idc, const ParamEntry &cls,
		float scaleMin, float scaleMax, float scaleDefault
	);

	void SetTemplate(ArcadeTemplate *templ) {_template = templ;}
	const ArcadeTemplate *GetTemplate() const {return _template;}

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;

	void Center() override;
	void DrawExt(float alpha) override;

	void CreateObjectList();

protected:
	AIUnit *GetMyUnit();
	AICenter *GetMyCenter();

	virtual InsertMode GetMode();

	void DrawObjects();
	EntityAI *FindObject(int id);

	void DrawLabel(struct SignInfo &info, PackedColor color) override;
	SignInfo FindSign(float x, float y) override;

	virtual bool HasFullRights() {return false;}

	bool IsSelected(const SignInfo &info) const;
	void InvertSelection(const SignInfo &info);
};

enum EditRights
{
	ERNone,
	ERGroupWP,
	ERSideWP,
	ERFull
};

class CStaticMapArcade : public CStaticMapArcadeViewer
{
protected:
	EditRights _editRights;

	RString _lastGroupSide;
	RString _lastGroupType;
	RString _lastGroupName;

public:

	CStaticMapArcade
	(
		ControlsContainer *parent, int idc, const ParamEntry &cls, EditRights rights,
		float scaleMin, float scaleMax, float scaleDefault
	)
	: CStaticMapArcadeViewer(parent, idc, cls, scaleMin, scaleMax, scaleDefault)
	{_editRights = rights;}

	void SetLastGroup(RString side, RString type, RString name)
	{
		_lastGroupSide = side; _lastGroupType = type; _lastGroupName = name;
	}

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonClick(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnMouseHold(float x, float y, bool active = true) override;

	void DrawExt(float alpha) override;

	void ProcessCheats() override;

protected:
	void ClipboardDelete();
	void ClipboardCopy();
	void ClipboardCut();
	void ClipboardPaste();
	void ClipboardPasteAbsolute();

	InsertMode GetMode() override;
	bool IsAdvanced();
	ArcadeUnitInfo *GetLastUnit();

	bool HasFullRights() override {return _editRights == ERFull;}
	bool HasRight(int ig);
};

class DisplayArcadeWaypoint : public Display
{
public:
	int _indexGroup;
	int _index;
	ArcadeWaypointInfo _waypoint;

	ArcadeTemplate *_template;

	bool _advanced;

public:

	DisplayArcadeWaypoint
	(
		ControlsContainer *parent, ArcadeTemplate *templ,
		int indexGroup, int index, ArcadeWaypointInfo &waypoint,
		bool advanced
	);
	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;

	void OnButtonClicked(int idc) override;
	void OnChildDestroyed(int idd, int exit) override;

	bool CanDestroy() override;
	void Destroy() override;
};

class DisplayArcadeMap : public DisplayMapEditor
{
public:
	int _langCbToken = -1;

	CStaticMapArcade *_map;

	ArcadeTemplate _templateMission;
	ArcadeTemplate _templateIntro;
	ArcadeTemplate _templateOutroWin;
	ArcadeTemplate _templateOutroLoose;

	InitPtr<ArcadeTemplate> _currentTemplate;

	InsertMode _mode;
	ArcadeUnitInfo _lastUnit;

	bool _multiplayer;
	bool _running;
	bool _advanced;

public:

	DisplayArcadeMap(ControlsContainer *parent, bool multiplayer = false);

	WeatherState GetWeather() override;
	Clock GetTime() override;
	bool GetPosition(Point3 &pos) override;
	CStaticMap *GetMap() override {return _map;}

	void OnChildDestroyed(int idd, int exit) override;

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnButtonClicked(int idc) override;
	void OnComboSelChanged(int idc, int curSel) override;
	void OnToolBoxSelChanged(int idc, int curSel) override;
	ControllerUiScene GetControllerUiScene() const override;
	bool DoControllerUiAction(ControllerUiAction action) override;
	bool OnUnregisteredAddonUsed( RString addon ) override;
	void OnSimulate(EntityAI *vehicle) override;

	bool CanDestroy() override;
	void Destroy() override;

	void ShowButtons();
	void CycleControllerMode(int delta);

protected:
	LSError SerializeAll(ParamArchive &ar, bool merge = false);
	bool LoadTemplates(const char *filename);
	bool MergeTemplates(const char *filename);
	bool SaveTemplates(const char *filename);
	void SetAdvancedMode(bool advanced);
	void LoadParams();
	void SaveParams();

	void UpdateIdsButton();
	void UpdateTexturesButton();
	void RefreshLanguage();
};

// Mission, intro, outro, campaign intro displays

class DisplayMission : public Display
{
protected:
	bool _editor;

	Ref<Compass> _compass;
	Ref<Watch> _watch;
	bool _missionSmokeReported = false;

public:

	DisplayMission(ControlsContainer *parent, bool editor = false, bool erase = true, bool load = false);
	~DisplayMission() override;

	ControlObject *OnCreateObject(int type, int idc, const ParamEntry &cls) override;
	void OnSimulate(EntityAI *vehicle) override;
	void OnChildDestroyed(int idd, int exit) override;
	void OnButtonClicked(int idc) override {}
	ControllerUiScene GetControllerUiScene() const override;
	bool DoControllerUiAction(ControllerUiAction action) override;


	void ShowHint(RString hint);

protected:
	void InitUI();
	void OpenInterrupt();
	bool RetryMission();
//	bool RestartMission();
};

class DisplayCutscene : public Display
{
public:

	DisplayCutscene(ControlsContainer *parent);
	~DisplayCutscene() override;

	void OnSimulate(EntityAI *vehicle) override;
	void OnButtonClicked(int idc) override {}
};

class DisplayIntro : public DisplayCutscene
{
public:
	enum NoInit {noInit};
	bool _missionSmokeReported = false;

	DisplayIntro(ControlsContainer *parent, NoInit);

	DisplayIntro(ControlsContainer *parent);

	DisplayIntro(ControlsContainer *parent, RString cutscene);
	void OnSimulate(EntityAI *vehicle) override;
	void Init();
};

RString& GetMPMissionsDir();
RString& GetAnimsDir();
RString& GetMissionsDir();

RString GetSaveDirectory();
void SetBaseDirectory(RString dir);
void SetMission(RString world, RString mission, RString subdir);
void SetMission(RString world, RString mission);

RString GetMissionsDirectory();
RString GetMissionDirectory();
RString GetBriefingFile();

// Mission editor subdisplays

class CStaticAzimut : public CStatic
{
protected:
	int _idcEdit;

public:

	CStaticAzimut(ControlsContainer *parent, int idc, const ParamEntry &cls, int idcEdit)
		:	CStatic(parent, idc, cls) {_enabled = true; _idcEdit = idcEdit;}

	void OnDraw(float alpha) override;
	void OnLButtonClick(float x, float y) override;
};

class DisplayArcadeUnit : public Display
{
public:
	int _indexGroup;
	int _index;

	ArcadeUnitInfo _unit;
	ArcadeTemplate *_template;

public:

	DisplayArcadeUnit
	(
		ControlsContainer *parent,
		int indexGroup, int index,
		ArcadeUnitInfo &unit,
		ArcadeTemplate *t,
		bool advanced
	)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;

		_indexGroup = indexGroup;
		_index = index;
		_unit = unit;
		_template = t;
		if (_unit.side == TEmpty)
		{
			_unit.player = APNonplayable;
		}
		if (advanced)
			Load("RscDisplayArcadeUnit");
		else
			Load("RscDisplayArcadeUnitSimple");
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnComboSelChanged(int idc, int curSel) override;
	bool CanDestroy() override;
	void Destroy() override;


	void UpdateClasses(CCombo *combo, const char *vehicle);

	void UpdateVehicles(CCombo *combo, const char *vehicle);

	void UpdatePlayer(CCombo *combo, ArcadeUnitPlayer player);
};

class DisplayArcadeGroup : public Display
{
public:
	Vector3 _position;
	float _azimut;

public:

	DisplayArcadeGroup
	(
		ControlsContainer *parent, Vector3Par position,
		RString side, RString type, RString name, float azimut
	);

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnComboSelChanged(int idc, int curSel) override;

	void UpdateTypes();
	void UpdateNames();
};

class DisplayArcadeMarker : public Display
{
public:
	int _index;
	ArcadeMarkerInfo _marker;

	ArcadeTemplate *_template;

protected:
	int _oldType;

public:

	DisplayArcadeMarker
	(
		ControlsContainer *parent,
		int index,
		ArcadeMarkerInfo &marker,
		ArcadeTemplate *templ,
		bool advanced
	)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;

		_index = index;
		_marker = marker;
		_template = templ;

		_oldType = marker.markerType;

		if (advanced)
			Load("RscDisplayArcadeMarker");
		else
			Load("RscDisplayArcadeMarkerSimple");

		ChangeType(marker.markerType);
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnToolBoxSelChanged(int idc, int curSel) override;

	bool CanDestroy() override;
	void Destroy() override;

protected:
	void ChangeType(int curSel);
};

class DisplayArcadeSensor : public Display
{
public:
	int _ig;
	int _index;
	ArcadeSensorInfo _sensor;
	ArcadeTemplate *_t;
	bool _advanced;

public:

	DisplayArcadeSensor
	(
		ControlsContainer *parent,
		int ig, int index,
		ArcadeSensorInfo &sensor,
		ArcadeTemplate *t,
		bool advanced
	)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;
		_ig = ig;
		_index = index;
		_sensor = sensor;
		_t = t;
		_advanced = advanced;

		if (advanced)
			Load("RscDisplayArcadeSensor");
		else
			Load("RscDisplayArcadeSensorSimple");
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;

	void OnButtonClicked(int idc) override;
	void OnChildDestroyed(int idd, int exit) override;

	bool CanDestroy() override;
	void Destroy() override;
};

class DisplayArcadeEffects : public Display
{
public:
	ArcadeEffects _effects;

public:

	DisplayArcadeEffects
	(
		ControlsContainer *parent,
		ArcadeEffects effects,
		bool advanced
	);
	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnComboSelChanged(int idc, int curSel) override;

	void Destroy() override;

protected:
	void ChangeTitleType(int type);
};

class DisplayTemplateSave : public Display
{
public:

	DisplayTemplateSave(ControlsContainer *parent)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;
		Load("RscDisplayTemplateSave");
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
};

class DisplayTemplateLoad : public Display
{
public:
	bool _merge;

public:

	DisplayTemplateLoad(ControlsContainer *parent, bool merge = false)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;
		_merge = merge;
		Load("RscDisplayTemplateLoad");
		OnIslandChanged();
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void OnComboSelChanged(int idc, int curSel) override;

protected:
	void OnIslandChanged();
};

class DisplayIntel : public Display
{
public:
	ArcadeIntel _intel;

public:

	DisplayIntel(ControlsContainer *parent, ArcadeIntel &intel, bool advanced)
		: Display(parent)
	{
		_enableSimulation = false;
		_enableDisplay = false;

		_intel = intel;
		if (advanced)
			Load("RscDisplayIntel");
		else
			Load("RscDisplayIntelSimple");
	}

	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	void Destroy() override;

	void OnComboSelChanged(int idc, int curSel) override;


	void UpdateDays(CCombo *combo, int day = -1);
};

} // namespace Poseidon

using ::Poseidon::GetMPMissionsDir;
