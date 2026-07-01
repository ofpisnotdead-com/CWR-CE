#pragma once
#include <Poseidon/UI/InGame/InGameUI.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>

#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Game/UiActions.hpp>

namespace Poseidon
{
enum UIMode
{
	UIFire, // weapon aiming
	UIFirePosLock,	// possible lock
	UIStrategy, // no command
	UIStrategySelect, // select subordinate unit
	UIStrategyMove, // command unit: move to target
	UIStrategyAttack, // command unit: attack target
	UIStrategyFire, // command unit: fire
	UIStrategyGetIn, // command unit: get in vehicle
	UIStrategyWatch, // command unit: watch position or target
	// define command groups
	UIFireMin=UIFire,UIFireMax=UIFirePosLock,
	UIStrategyMin=UIStrategy,UIStrategyMax=UIStrategyGetIn,
};

#define ModeIsStrategy(x) ( (x)>=UIStrategyMin && (x)<=UIStrategyMax )

class Menu;

class MenuItem : public RefCount
{
public:
	RString _baseText;
	RString _text;
	int _key;
	RString _char;
	int _cmd;
	LLink<EntityAI> _target;
	UIActionType _action;
	int _param;
	int _param2;
	RString _param3;
	Ref<Menu> _submenu; 
	bool _enable;
	bool _visible;
	bool _check;
	
public:
	MenuItem
	(
		RString text, int key, RString ch, int cmd,
		UIActionType action = ATNone, EntityAI *target = nullptr,
		int param = 0, int param2 = 0, RString param3 = ""
	);
	MenuItem(RString text, int key, RString ch, Menu *submenu, int cmd);
};

class Menu : public RefCount
{
public:
	RString _text;
	RefArray<MenuItem> _items;
	Menu *_parent;
	bool _enable;
	bool _visible;
	bool _atomic; // menu may be enabled/disabled only as whole
	int _minCmd,_maxCmd; // for faster rejection of EnableCommand/ShowCommand

public:
	Menu();
	Menu(const char *text, Menu *parent);

	void Load(const ParamEntry *cls);
	void AddItem(MenuItem *item);

	void NotifySubmenuCommandAdded(int cmd);
	void NotifySubmenuCommandRemoved(int cmd);
	void RescanMinMax();
	void RescanParents();
	void RescanChildren();
	//void RescanThisAndParents();
	//void RescanThisAndChildren();

	bool CanBeInMenu(int cmd) const;
	bool EnableCommand(int cmd, bool enable = true);
	bool ShowCommand(int cmd, bool show = true);
	bool ShowAndEnableCommand(int cmd, bool show = true, bool enable = true);

	bool CheckCommand(int cmd, bool check);
	bool SetText(int cmd, RString text);
	bool ResetText(int cmd);

	Menu *FindMenu(int cmd, bool alsoInAtomic=false);
	MenuItem *Find(int cmd, bool alsoInAtomic=false);
};

#define N_SHOW_COMMANDS 10

} // namespace Poseidon
#include <Poseidon/AI/AIRadio.hpp>
namespace Poseidon
{

struct UnitDescription
{
public:
	enum Status
	{
		none,
		wait,
		away,
		command,
		cargo,
		commander,
		gunner
	};

	bool valid;
	bool selected;
	bool problems;
	bool player;
	bool playerVehicle;
	OLink<Transport> vehicle;
	OLink<Person> person;
	AI::Semaphore semaphore;
	Status status;
	Command::Message cmd;
	int vehCommander;
	int leader;
	Team team;

	UnitDescription()
	{
		valid = false;
	}
};

struct CursorText
{
	RString text;
	float upDown;
};

class CursorTexts : public AutoArray<CursorText>
{
typedef AutoArray<CursorText> base;

public:
	void Add(RString text, float upDown)
	{
		int index = base::Add();
		Set(index).text = text;
		Set(index).upDown = upDown;
	}
};

class DisplayUnitInfo : public Display
{
public:
	CStaticTime *time;
	CStatic *date;
	CStatic *name;
	CStatic *unit;
	CStatic *vehicle;
	CProgressBar *valueExp;
	CStatic *formation;
	CStatic *combatMode;
	CStatic *speed;
	CStatic *alt;
	CProgressBar *valueHealth;
	CProgressBar *valueArmor;
	CProgressBar *valueFuel;
	CStatic *cargoMan;
	CStatic *cargoFuel;
	CStatic *cargoRepair;
	CStatic *cargoAmmo;
	CStatic *weapon;
	CStatic *ammo;

	CStatic *background;
public:
	
	DisplayUnitInfo(ControlsContainer *parent);
	void InitControls();
	void Reload(const ParamEntry &clsEntry);
	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
};

class DisplayHint : public Display
{
protected:
	// fast access to control (null until OnCreateCtrl runs — see SetHint guard)
	CStatic *_background = nullptr;
	CStatic *_hint = nullptr;

public:
	
	DisplayHint(ControlsContainer *parent);
	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls) override;
	RString GetHint() {return _hint->GetText();}
	void SetHint(RString hint);
	void SetPosition(float top);	// height is calculate
};

DECL_ENUM(VCommand)

enum MenuType
{
	MTNone,
	MTMain,
//	MTRadio,
//	MTTank,
};

class InGameUI: public AbstractUI
{
	protected:

	UIMode _mode;
	UIMode _modeAuto;
	Point3 _groundPoint;
	float _groundPointDistance;

	LinkTarget _target; // active target (under cursor)
	LinkTarget _lockTarget; // active target (under cursor)
	int _housePos;
	bool _wantLock; // users wants to lock enemy target
	Poseidon::Foundation::UITime _timeSendTarget;
//	Poseidon::Foundation::UITime _timeSendLoad;
	Poseidon::Foundation::UITime _timeToPlay; // time of protection message

	Ref<Font> _font24;
	Ref<Font> _font36;

	const TargetList *VisibleList() const
	{
		AIUnit *unit = GLOB_WORLD->FocusOn();
		if (!unit) return nullptr;
		AISubgroup *subgroup = unit->GetSubgroup();
		if (!subgroup) return nullptr;
		AIGroup *group = subgroup->GetGroup();
		if (!group) return nullptr;
		return &group->GetTargetList();
	}

//	int _curWeapon;

	LLinkArray<Target> _visibleListTemp;
//	StaticStorage<Target> _visibleListS;
	AutoArray<RString> _customRadio;

#if _ENABLE_CHEATS
	bool _showAll;
#endif

	bool _blinkState;
	bool _groundPointValid;
	Poseidon::Foundation::UITime _worldCursorTime; // when was cursor set (old cursor may be moved)

	Poseidon::Foundation::UITime _blinkStateChange;

	Vector3 _modelCursor; // cursor direction relative to vehicle
	Vector3 _worldCursor; // world cursor direction

	bool _cursorWorld; // true -> _worldCursor valid, else _modelCursor valid

	bool _leftPressed;
	bool _rightPressed;

	Vector3 _lockAim; // explicit gun direction
	Poseidon::Foundation::UITime _lockAimValidUntil;

	UnitDescription _groupInfo[MAX_UNITS_PER_GROUP];

	Poseidon::Foundation::UITime _lastGroupInfoTime;
	Poseidon::Foundation::UITime _lastUnitInfoTime;
	Poseidon::Foundation::UITime _lastMeTime;
	Poseidon::Foundation::UITime _lastCmdTime;
	Poseidon::Foundation::UITime _lastTargetTime;
	Poseidon::Foundation::UITime _lastGroupDirTime;
	Poseidon::Foundation::UITime _lastFormTime;
	Poseidon::Foundation::UITime _lastSelTime[MAX_UNITS_PER_GROUP];
	Poseidon::Foundation::UITime _lastMenuTime;

	int _lastCmdId;

	Ref<Menu> _menuMain;
	//Ref<Menu> _menuRadio;
	//Ref<Menu> _menuTank;
	MenuType _menuType;
	Menu *_menuCurrent;
	
	UnitInfoType _lastUnitInfoType;
	SRef<DisplayUnitInfo> _unitInfo;

	SRef<DisplayHint> _hint;
	Poseidon::Foundation::UITime _hintTime;
	float _hintTop;
	SoundPars _hintSound;

	float _tmPos;
	Poseidon::Foundation::UITime _tmTime;
	float _tankPos;
	Poseidon::Foundation::UITime _tankTime;
	bool _tmIn;
	bool _tmOut;
	bool _tankIn;
	bool _tankOut;

	bool _fireEnabled = false;

	bool _dragging;
	bool _mouseDown;
	Poseidon::Foundation::UITime _mouseDownTime;
	Vector3 _startSelection;
	Vector3 _endSelection;

	UIActions _actions;

	public:
	InGameUI();
	~InGameUI() override;

	RString GetActionMenuTexts() const override;
	RString GetCommandMenuTexts() const override;

	void DrawHUD
	(
		const Camera &camera, EntityAI *vehicle, CameraType cam
	) override; // draw overlay - called before FinishDraw
	void SimulateHUD
	(
		const Camera &camera, EntityAI *vehicle, CameraType cam, float deltaT
	) override;

	// simplified version - used for non-AI vehicles
	void DrawHUDNonAI
	(
		const Camera &camera, Entity *vehicle, CameraType cam
	) override; // draw overlay - called before FinishDraw
	void SimulateHUDNonAI
	(
		const Camera &camera, Entity *vehicle, CameraType cam, float deltaT
	) override;

	void ResetHUD() override;
	void ResetVehicle( EntityAI *vehicle ) override;
	void OnWeaponRemoved(int slot) override;

	void SetCursorMode( bool world ) override; // mouse used - switch to world mode
	bool GetCursorMode() const override; // mouse used - switch to world mode

	void ShowMessage( int channel, const char *text);
	void ShowMe() override {_lastMeTime = Glob.uiTime;}
	void ShowFormPosition() override {_lastFormTime = Glob.uiTime;}
	void ShowTarget() override {_lastTargetTime = Glob.uiTime;}
	void ShowGroupDir() override {_lastGroupDirTime = Glob.uiTime;}

	void ShowHint(RString hint) override;
	void ShowLastHint() {_hintTime = Glob.uiTime;}

	void ShowMenu() {_lastMenuTime = Glob.uiTime;}
	bool IsCommandMenuOpen() const override { return _menuType != MTNone; }

	// used to handle mouse movements
	Vector3 GetCursorDirection() const override; // get world cursor direction
	void SetCursorDirection( Vector3Par dir ) override; // set world cursor direction

	Vector3 GetWorldCursor() const override;
	void SetWorldCursor( Vector3Par dir ) override;
	Vector3 GetModelCursor() const override;
	void SetModelCursor( Vector3Par dir ) override;

	float GetCursorAge() const override {return Glob.uiTime-_worldCursorTime;}

	void SwitchToStrategy( EntityAI *vehicle ) override;
	void SwitchToFire( EntityAI *vehicle ) override;
	void SelectWeapon(AIUnit *unit, int weapon);

	TargetSide RadarTargetSide( AIUnit *unit, Target &tar );
	
	void FindTarget( EntityAI *me, bool prev );
	void NextTarget( EntityAI *me ) {FindTarget(me,false);}
	void PrevTarget( EntityAI *me ) {FindTarget(me,true);}

	void RevealTarget( Target *tgt, float spot );

	void Init() override;

	const AutoArray<RString> &GetCustomRadio() const override {return _customRadio;}

	// Wire RscMainMenu's movement command sub-menu. Static + public so the null path
	// (a broken RscMainMenu, where FindMenu returns null) is unit-testable without a
	// full InGameUI. See InGameUIMenuSim.cpp.
	static void WireMovementCommandMenu(Menu* menuMain);

private:
	void InitMenu();

	void SetMode( UIMode mode );
	void ToggleSelection(AIGroup *grp, int id);

	Target *CheckCursorTarget
	(
		Vector3 &itPos, Vector3Par cursorDir,
		const Camera &camera, CameraType cam, bool knownOnly
	);

	void IssueVCommand(EntityAI *vehicle, VCommand cmd);
	void IssueCommand(EntityAI *vehicle, Command::Message cmd = Command::NoCommand, bool follow = false);
	void IssueWatchAround(AIGroup *grp); // watch around
	void IssueEngage(AIGroup *grp); // engage current target
	void IssueFire(AIGroup *grp); // fire at current target
	void IssueWatchAuto(AIGroup *grp); // watch around
	void IssueWatch(AIGroup *grp,int what); // direction
	void IssueWatchTarget(AIGroup *grp,int what); // target from the list
	void IssueVMove(Transport *vehicle, int where);
	void IssueMove(AIGroup *grp, int where);
	void IssueAttack(AIGroup *grp, int tgt);
	void IssueGetIn(AIGroup *grp, int index);
	void IssueAction(AIGroup *grp, MenuItem &item);
	void SetSemaphore(AIUnit *unit, AI::Semaphore status);
	void SetBehaviour(CombatMode mode);
	void SetUnitPosition(AIUnit *unit, UnitPosition status);
	void SetFormationPos(AIUnit *unit, AI::FormationPos status);

	void SendFireReady(AIUnit *unit, bool ready);
	void SendAnswer(AIUnit *unit, AI::Answer answer);
	void SendConfirm(AIUnit *unit);
	void SendRepeat(AIUnit *unit);

	void SendKilled(AIUnit *unit, PackedBoolArray list); // report unit is killed
	void SendResourceState(AIUnit *unit,AI::Answer answer); // report some state
	void SendObjectDestroyed(AIUnit *unit,AIGroup *grp);

	bool CheckJoin(AIGroup *grp);

	void CreateAttackList(AIGroup *group, Menu *submenu, int cmdBase);

	void ProcessMenu(const Camera &camera, EntityAI *vehicle);
	bool ShouldShowGameplayHUD() const;
	void ProcessActions(AIUnit *unit);
	void RefreshActionsMenu();
	void CollectActions(UIActions &actions);
	void DrawMenu();
	void DrawTankDirection(const Camera &camera);
	PackedColor ColorFromHit(float hit);
	void DrawTacticalDisplay
	(
		const Camera &camera, AIUnit *unit, const TargetList &list
	);
	void DrawCompass(EntityAI *vehicle);
	void DrawUnitInfo(EntityAI *vehicle);
	void DrawGroupDir(const Camera &camera, AIGroup *grp);
	void DrawHint();
	void DrawGroupUnit(AIUnit *u, float xScreen, float yScreen, float alpha, int align);
	void DrawGroupInfo(EntityAI *vehicle);
	bool DrawMouseCursor(const Camera &camera, AIUnit *unit, bool td);
	bool DrawTargetInfo
	(
		const Camera &camera, AIUnit *unit, Vector3Par dir,
		Texture *cursor, Texture *cursor2,
		PackedColor color, float cursorA, float cursor2A,
		const Target *target, int housePos,
		bool info, bool extended, bool td
	);
	void DrawCursor
	(
		const Camera &camera, EntityAI *vehicle,
		Vector3Val dir, float size,
		Texture *texture, float width, float height,
		PackedColor color, bool drawInTD,
		CursorTexts texts = CursorTexts()
	);
	void DrawCommand(const Command &cmd, AIUnit *unit, const Camera &camera, bool td, float alpha = 1.0);
	void BackupTargets();

// cfg parameters
private: 
	float tmX;
	float tmY;
	float tmW;
	float tmH;
	float tankX;
	float tankY;
	float tankW;
	float tankH;
	float tdX;
	float tdY;
	float tdW;
	float tdH;
	float coX;
	float coY;
	float coW;
	float coH;
	float giX;
	float giY;
	float giW;
	float giH;
	float piX;
	float piY;
	float piW;
	float piH;
	float uiX;
	float uiY;
	float uiW;
	float uiH;
	float gdX;
	float gdY;
	float gdW;
	float gdH;
	float ppicW;
	float ppicH;
	float piSignH;
	float piSignSW;
	float piSignGW;
	float piSignUW;
	float piSideH;
	float piSideW;
	float barH;
	float hbarW;
	float abarW;
	float fbarW;
	float ebarW;
	float tdCurW;
	float tdCurH;
	float curSignH;
	float curSignSW;
	float curSignGW;
	float curSignUW;
	float semW;
	float semH;
	float actW;
	float actH;
	float actMin;
	float actMax;
	RString tdName;
	RString giName;
	RString piName;
	RString uiName;
	PackedColor bgColor;
	PackedColor bgColorCmd;
	PackedColor bgColorHelp;
	PackedColor ftColor;
	PackedColor menuCheckedColor;
	PackedColor menuEnabledColor;
	PackedColor menuDisabledColor;
	PackedColor friendlyColor;
	PackedColor enemyColor;
	PackedColor neutralColor;
	PackedColor civilianColor;
	PackedColor unknownColor;
	PackedColor cameraColor;
	PackedColor barBgColor;
	PackedColor barGreenColor;
	PackedColor barYellowColor;
	PackedColor barRedColor;
	PackedColor barBlinkOnColor;
	PackedColor barBlinkOffColor;
	PackedColor ebarColor;
	PackedColor uiColorNone;
	PackedColor uiColorNormal;
	PackedColor uiColorSelected;
	PackedColor uiColorPlayer;
	PackedColor pictureColor;
	PackedColor pictureProblemsColor;
	PackedColor cursorColor;
	PackedColor cursorBgColor;
	PackedColor cursorLockColor;
	PackedColor compassColor;
	PackedColor compassDirColor;
	PackedColor compassTurretDirColor;
	PackedColor tdCursorColor;
	PackedColor holdFireColor;
	PackedColor capBgColor;
	PackedColor capFtColor;
	PackedColor capLnColor;
	PackedColor enemyActColor;
	PackedColor timeColor;
	PackedColor meColor;
	PackedColor selectColor;
	PackedColor leaderColor;
	PackedColor missionColor;

	PackedColor msg1Color;
	PackedColor msg2Color;
	PackedColor msg3Color;

	PackedColor tankColor;
	PackedColor tankColorHalfDammage;
	PackedColor tankColorFullDammage;

	PackedColor teamColors[NTeams];

	PackedColor dragColor;

	float cursorDim;
	float messagesDim;
	float groupInfoDim;
	float meDim;
	float meDimStartTime;
	float meDimEndTime;
	float cmdDimStartTime;
	float cmdDimEndTime;
	float targetDimStartTime;
	float targetDimEndTime;
	float groupDirDimStartTime;
	float groupDirDimEndTime;
	float formDimStartTime;
	float formDimEndTime;
	float piDimStartTime;
	float piDimEndTime;
	float hintDimStartTime;
	float hintDimEndTime;
	float menuHideTime;

	Ref<Texture> _iconMe;
	Ref<Texture> _iconSelect;
	Ref<Texture> _iconLeader;
	Ref<Texture> _iconMission;

	Ref<Texture> _imageSemaphore;
	Ref<Texture> _imageBar;

	// animated with main turret
	Ref<Texture> _imageTurret;
	Ref<Texture> _imageGun;

	// animated with observer turret
	Ref<Texture> _imageObsTurret;

	// animated with hull
	Ref<Texture> _imageHull;
	Ref<Texture> _imageEngine;
	Ref<Texture> _imageLTrack;
	Ref<Texture> _imageRTrack;

	// group direction
	Ref<Texture> _imageGroupDir;

	// pictures in group info
	Ref<Texture> _imageDefaultWeapons;
	Ref<Texture> _imageNoWeapons;

	// Connection Lost message
	float _clX;
	float _clY;
	float _clW;
	float _clH;
	Ref <Font> _clFont;
	float _clSize;
	PackedColor _clColor;
};

inline void InGameUI::SetMode( UIMode mode )
{
	_mode = mode;
}

// Selected-units and team accessors (defined in InGameUIActions.cpp).
AIUnit* GetSelectedUnit(int i);
void SetSelectedUnit(int i, AIUnit* unit);
void ClearSelectedUnits();
bool IsEmptySelectedUnits();
Foundation::PackedBoolArray ListSelectedUnits();
Team GetTeam(int i);

} // namespace Poseidon
