#pragma once
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/UI/InGame/InGameUI.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/Input/InputContext.hpp>

namespace Poseidon { class Landscape; }
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/IO/ParamFile/ParamFileCtx.hpp>
#include <Poseidon/Graphics/Cursor/ICursorOverlay.hpp>

class GameState;
struct MissionHeader;
class ControlsContainer;


namespace Poseidon
{
class Script;
struct InputContextResolution;

DEFINE_ENUM_BEG(CameraType)
	CamInternal,CamGunner,CamExternal,CamGroup,
	MaxCameraType
DEFINE_ENUM_END(CameraType)

typedef void (Entity::*VehicleSimulation)( float deltaT, SimulationImportance prec );

enum StartVehicle {StartSoldier,StartHelicopter,StartAPC,StartSeaGull};

extern StartVehicle PlayStart;

class HeliPilotWaypoints;
class SeaGullPilotCommander;
class TankPilotFollow;
class CarPilotFollow;
class SoldierPilotFollow;

class BMPPlayer;
class BMPWithAI;

class VehicleList: public RefArray<Entity>, public SerializeClass
{
	public:
	void Insert( Entity *object ) {RefArray<Entity>::Add(object);}
	void Add( Entity *object );

	LSError Serialize(ParamArchive &ar) override;
};

class Transport;
class SensorList;

enum PreloadedVType
{
	VTypeStatic,
	VTypeBuilding, // all static but fortreses
	VTypeStrategic, VTypeNonStrategic,
	VTypeObjective,VTypePrimaryObjective,VTypeSecondaryObjective,
	VTypeTarget,
	VTypeAllVehicles,
	VTypeAir,VTypePlane,
	VTypeShip,VTypeBigShip,
	VTypeAPC,VTypeTank,VTypeCar,
	VTypeMan,
	NPreloadedVTypes,
};

enum GameMode
{
	GModeNetware,
	GModeArcade,
	GModeIntro,
};

struct AnimationDescriptor;

class CameraEffect: public RefCountWithLinks
{
	protected:
	OLink<Object> _object; // effect base
	Matrix4 _transform;

	public:
	CameraEffect( Object *object ); // each camera effect is assinged to something
	~CameraEffect() override;
	
	Object *GetObject() const {return _object;}
	void SetObject( Object *obj ) {_object=obj;}

	virtual void Simulate( float deltaT ) {}
	virtual Matrix4 GetTransform() const {return _transform;} // get camera position
	virtual float GetFOV() const {return -1;} // set to default
	virtual bool IsInside() const {return false;} // set to default
	virtual void Draw() const; // draw anything
	virtual bool IsTerminated() const {return true;}

	virtual int GetType() const = 0;
	LSError Serialize(ParamArchive &ar);
	static CameraEffect *CreateObject(ParamArchive &ar);
};

DECL_ENUM(CamEffectPosition)
DECL_ENUM(CamEffectName)

class TitleEffect: public RefCountWithLinks
{
	public:
	virtual void Draw() {}
	virtual void Simulate( float deltaT ) {}
	virtual void Terminate() {}
	virtual void Prolong( float time ) {}

	virtual bool IsTerminated() const {return true;}
	virtual bool IsTransparent() const {return true;}
};

} // namespace Poseidon
// TitEffectName's enum is defined at global scope (TitEffects.hpp); forward-declare it
// globally here too — a Poseidon-scoped forward-decl is ambiguous with ::TitEffectName.
DECL_ENUM(TitEffectName)
namespace Poseidon
{
DECL_ENUM(EndMode)

class VehiclesDistributed : public SerializeClass
{
	friend class World;

	VehicleList _visibleNear;
	VehicleList _visibleFar;
	VehicleList _invisibleNear;
	VehicleList _invisibleFar;

	public:
	void Clear();

	void Add( Entity *vehicle ); // add to list and landscape
	void Insert( Entity *vehicle ); // add to list only

	void Delete( Entity *vehicle ); // delete from list and landscape
	void Remove( Entity *vehicle ); // delete from list only
	int Find( Entity *vehicle ) const;

	int Size() const;

	Entity *Get( int index ) const;

	LSError Serialize(ParamArchive &ar) override;
};



DEFINE_ENUM_BEG(VehicleListType)
	VLTVehicle,
	VLTAnimal,
	VLTBuilding,
	VLTCloudlet,
	VLTFast,
	VLTOut,
DEFINE_ENUM_END(VehicleListType)

struct GridInfo
{
	float zoomMax;
	RString format;
	RString formatX;
	RString formatY;
	float stepX;
	float stepY;
	float invStepX;
	float invStepY;

	GridInfo(float z, RString f, RString fX, RString fY, float sX, float sY)
	{
		zoomMax = z; format = f; formatX = fX; formatY = fY;
		stepX = sX; invStepX = sX == 0 ? 0 : 1.0f / sX; stepY = sY; invStepY = sY == 0 ? 0 : 1.0f / sY;
	}
	GridInfo()
	{
		zoomMax = 0;
		stepX = 0; invStepX = 0; stepY = 0; invStepY = 0;
	}
};


DECL_ENUM(AICenterMode)

class World
{
	protected:
	Engine *_engine;
	SRef<AbstractUI> _ui;
	SRef<AbstractOptionsUI> _userDlg;
	SRef<AbstractOptionsUI> _options;
	SRef<AbstractOptionsUI> _channel;
	SRef<AbstractOptionsUI> _chat;
	SRef<AbstractOptionsUI> _voiceChat;
	SRef<ControlsContainer> _warningMessage;
	// Polymorphic cursor strategy — viewer mode installs a
	// ViewerCursorOverlay; in-game / menu modes leave this null and
	// rely on the existing ControlsContainer / DrawHUDNonAI cursor
	// machinery.  Set once at world init; null-checked at the draw
	// site, so there are no IsViewerMode() gates scattered around.
	SRef<::Poseidon::ICursorOverlay> _cursorOverlay;
	Scene _scene;
	SRef<AbstractOptionsUI> _map;
	SRef<SensorList> _sensorList;
	Ref<CameraEffect> _cameraEffect;
	Ref<TitleEffect> _titleEffect;
	Ref<TitleEffect> _cutEffect;
	RefArray<Script> _scripts;
	LLink<Script> _cameraScript;

	Ref<VehicleType> _preloadedVType[NPreloadedVTypes];
	ParamOwnerList _activeAddons;

	GameMode _mode;
	
	float _acceleratedTime;
	bool _firstFrame;

	Foundation::UITime _channelChanged;

	RefArray<Entity> _cloudlets;

	VehicleList _fastVehicles;
	
	VehiclesDistributed _vehicles;
	VehiclesDistributed _animals;
	VehiclesDistributed _buildings;

	RefArray<Entity> _outVehicles; // soldiers in vehicles, or in transit between lists

	Foundation::Time _nearImportanceDistributionTime;
	Foundation::Time _farImportanceDistributionTime;
	void DistributeImportances
	(
		VehiclesDistributed &target, VehicleList &list,
		SimulationImportance prec, const Vector3 *viewerPos, int nViewers
	);

	void DistributeNearImportances( VehiclesDistributed &list );
	void DistributeFarImportances( VehiclesDistributed &list );

	void DistributeNearImportances();
	void DistributeFarImportances();

	void GetViewerList(StaticArrayAuto<Vector3> &viewers);

	FindArray< InitPtr<AttachedOnVehicle> > _attached;

	Ref<AICenter> _eastCenter,_westCenter,_guerrilaCenter,_civilianCenter,_logicCenter;

	OLink<Object> _cameraOn;
	OLink<Person> _playerOn;
	OLink<Person> _realPlayer;
	OLinkArray<Person> _players;

	float _actualOvercast; // use for weather changes simulation
	float _wantedOvercast;
	float _speedOvercast; // speed of change

	float _actualFog;
	float _wantedFog;
	float _speedFog; // speed of change

	// geography latitude - positive is south
	float _latitude;
	// geography longitude - positive is east
	float _longitude;

	float _weatherTime;
	float _nextWeatherChange; // time when _wantedOvercast should be reached

	float _timeToSkip;

	bool _playerManual;
	bool _playerSuspended;

	bool _editor = false; // world can be create in world editing or game playing mode
	bool _showMap = false;
	bool _showCompass = false;
	bool _showWatch = false;
	bool _noDisplay = false;
	bool _enableSimulation = false;
	bool _simulationFocus = false; // true when application should run and render
	bool _enableRadio = false;

	bool _cameraExternal = false;

	bool _endDialogEnabled = false;
	bool _endForced = false;

	bool _forceMap = false;

	int _userInputDisabled;

	float _camHeading[MaxCameraType];
	float _camHeadingWanted[MaxCameraType];
	float _camDive[MaxCameraType];
	float _camDiveWanted[MaxCameraType];
	float _camFOV[MaxCameraType];
	float _camFOVWanted[MaxCameraType];
	float _camNear[MaxCameraType]; // zoom by going near
	float _camMaxDist[MaxCameraType]; // smooth camera distance

	CameraType _camTypeMain;
	CameraType _camType;

	Link <RadioChannel> _channelVehicle;
	Link <RadioChannel> _channelGroup;
	Link <RadioChannel> _channelSide;
	SRef <RadioChannel> _radio;
	SRef<Speaker> _speaker;

	Foundation::UITime _missionEndExpired;
	EndMode _endMission;

	int _nextMagazineID;

	Vector3 _ilsPosEast; // east airports
	Vector3 _ilsDirEast;
	Vector3 _ilsPosWest; // west airports
	Vector3 _ilsDirWest;
	Vector3 _ilsPosCiv; // civilian airports
	Vector3 _ilsDirCiv;

	AutoArray<Vector3> _taxiInPathsEast;
	AutoArray<Vector3> _taxiOffPathsEast;
	AutoArray<Vector3> _taxiInPathsWest;
	AutoArray<Vector3> _taxiOffPathsWest;
	AutoArray<Vector3> _taxiInPathsCiv;
	AutoArray<Vector3> _taxiOffPathsCiv;

	float _gridOffsetX;
	float _gridOffsetY;
	AutoArray<GridInfo> _gridInfo;

	protected:
	
	void BrowseCamera( int dir );
	void InitCameraPars();

	public:
	World( Engine *engine, bool editor=false );
	virtual ~World();

	void SetActiveChannels();

	private: // no copy possible
	World(); // no default constructor
	World( const World &src );
	void operator =( const World &src );

	public:
	void InsertSeaGulls();

	void InitGeneral(); // at the beginning of InitXX
	void InitGeneral( const ParamEntry &cfg );
	void InitGeneral( ArcadeIntel &intel );
	void InitClient();	// InitVehicles for multiplayer client
	void AdjustSubdivision(GameMode mode);
	void AdjustSubdivisionGrid(float grid);

	void InitCenter(AICenterMode mode, AICenter *cnt);
	bool InitVehicles( GameMode mode, ArcadeTemplate &t );
	void InitLandscape( Landscape *landscape );
	void InitEditor( Landscape *landscape, Entity *cursor );
	void InitFinish();

	int GetMagazineID() {return _nextMagazineID++;}

	void SetViewerPhase( float time );
	float GetViewerPhase() const;

	void ReloadViewer( void *buf, int len, const char *classDesc );
	void ReloadViewer( const char *filename, const char *classDesc );
	void ReloadViewerCore( LODShapeWithShadow *shape, const char *classDesc );

	void StartViewer( const char *modelPath, const char *animPath = "" );

	void SetViewerAnimation( const char *animPath );
	void ReloadViewerTextures();
	void ResetViewer();
	void DrawViewerSceneAddons();
	void TickViewerControls(float deltaT);
	Object* GetViewerObject() const;

	const ICursorOverlay* GetCursorOverlay() const { return _cursorOverlay; }
	ICursorOverlay*       GetCursorOverlay()       { return _cursorOverlay; }

	AbstractOptionsUI* GetOptions()   const { return _options; }
	AbstractOptionsUI* GetUserDlg()   const { return _userDlg; }
	AbstractOptionsUI* GetChannel()   const { return _channel; }
	AbstractOptionsUI* GetChat()      const { return _chat; }
	AbstractOptionsUI* GetVoiceChat() const { return _voiceChat; }
	ControlsContainer* GetWarningMessage() const { return _warningMessage; }

	AbstractUI* GetUI() const { return _ui; }

	bool GetILS( Vector3 &pos, Vector3 &dir, TargetSide side );

	const AutoArray<Vector3> GetTaxiInPath( TargetSide side );
	const AutoArray<Vector3> GetTaxiOffPath( TargetSide side );

	float GetGridOffsetX() const {return _gridOffsetX;}
	float GetGridOffsetY() const {return _gridOffsetY;}
	const GridInfo *GetGridInfo(float zoom) const;
	void ParseCfgWorld();

	void CleanUpDeinit();
	void CleanUpInit();
	void CleanUp();
	void Clear();
	void Reset();
	
	bool MouseControlEnabled() const; // check if mouse can be used to control
	bool LookAroundEnabled() const;

	bool IsUserInputEnabled() const {return _userInputDisabled <= 0;}
	void DisableUserInput(bool disable = true);

	void SetCameraEffect( CameraEffect *effect );
	CameraEffect *GetCameraEffect() const {return _cameraEffect;}

	void SetCameraScript( Script *script );
	Script *GetCameraScript() const;

	void StartCameraScript( Script *script );
	void TerminateCameraScript();
	
	void SetTitleEffect( TitleEffect *effect );
	TitleEffect *GetTitleEffect() const {return _titleEffect;}

	void FreelookChange(bool active);

	bool IsEndDialogEnabled() const {return _endDialogEnabled;}
	void EnableEndDialog(bool enable = true) {_endDialogEnabled = enable;}

	bool IsEndForced() const {return _endForced;}
	void ForceEnd(bool force = true) {_endForced = force;}

	void SetCutEffect( TitleEffect *effect );
	TitleEffect *GetCutEffect() const {return _cutEffect;}

	void AddScript(Script *script);
	void TerminateScript(Script *script);

	void AddAttachment( AttachedOnVehicle *attach );
	void RemoveAttachment( AttachedOnVehicle *attach );

	void DeleteAnyVehicle( Entity *vehicle );
	void RemoveAnyVehicle( Entity *vehicle );

	Entity *GetVehicle( int i ) const {return _vehicles.Get(i);}
	int NVehicles() const {return _vehicles.Size();}
	void AddVehicle( Entity *vehicle );
	void RemoveVehicle( Entity *vehicle );
	void InsertVehicle( Entity *vehicle );
	void DeleteVehicle( Entity *vehicle );

	Entity *GetAnimal( int i ) const {return _animals.Get(i);}
	int NAnimals() const {return _animals.Size();}
	void AddAnimal( Entity *vehicle ) {_animals.Add(vehicle);}
	void RemoveAnimal( Entity *vehicle ){_animals.Remove(vehicle);}
	void InsertAnimal( Entity *vehicle ){_animals.Insert(vehicle);}
	void DeleteAnimal( Entity *vehicle ){_animals.Delete(vehicle);}

	Entity *GetBuilding( int i ) const {return _buildings.Get(i);}
	int NBuildings() const {return _buildings.Size();}
	void AddBuilding( Entity *vehicle ) {_buildings.Add(vehicle);}
	void RemoveBuilding( Entity *vehicle ){_buildings.Remove(vehicle);}
	void InsertBuilding( Entity *vehicle ){_buildings.Insert(vehicle);}
	void DeleteBuilding( Entity *vehicle ){_buildings.Delete(vehicle);}

	Entity *GetFastVehicle( int i ) const {return _fastVehicles[i];}
	int NFastVehicles() const {return _fastVehicles.Size();}
	void AddFastVehicle( Entity *object ){_fastVehicles.Add(object);}
	void RemoveFastVehicle( Entity *object ) {_fastVehicles.Delete(object);}

	Entity *GetOutVehicle( int i ) const {return _outVehicles[i];}
	int NOutVehicles() const {return _outVehicles.Size();}
	void AddOutVehicle( Entity *object );
	void RemoveOutVehicle( Entity *object );

	bool ValidateOutVehicle(Entity *veh, bool complex=true) const;
	bool ValidateOutVehicles(bool complex=true) const;
	
	Entity *GetCloudlet( int i ) const {return _cloudlets[i];}
	int NCloudlets() const {return _cloudlets.Size();}
	void AddCloudlet( Entity *object ){_cloudlets.Add(object);}
	void RemoveCloudlet( Entity *object ){_cloudlets.Delete(object);}
	
	void PerformSound( VehiclesDistributed &list, Entity *inside, float deltaT );
	void PerformSound( VehicleList &list, Entity *inside, float deltaT );
	void PerformSound( Entity *inside, float deltaT );

	void UnloadSounds( VehiclesDistributed &list );
	void UnloadSounds( VehicleList &list );
	void UnloadSounds();
	
	void BackgroundSimulate
	(
		float deltaT, float noAccDeltaT, Entity *cameraVehicle, bool insideVehicle
	);
	InputContextResolution ResolveInputContextResolution() const;
	InputContext ResolveInputContext() const;
	void UpdateInputContext();
	void Simulate( float deltaT, bool &enableDraw );
	void PerformAI( float deltaT, float noAccDeltaT );
	void ProcessNetwork();
	void SimulateAllVehicles
	(
		float deltaT, float noAccDeltaT, Entity *cameraVehicle
	);
	void SimulateLandscape( float deltaT );
	void SimulateScripts();
	void MoveOutAndDelete( VehicleList &vehicles, float deltaT, bool applyMove=false );
	void MoveOutAndDelete( VehiclesDistributed &vehicles, float deltaT, bool applyMove=false );
	void SimulateOnly
	(
		VehicleList &vehicles,
		float deltaT, VehicleSimulation simul, Entity *insideVehicle,
		SimulationImportance prec
	);
	void SimulateOnly
	(
		VehiclesDistributed &vehicles,
		float deltaT, VehicleSimulation simul, Entity *insideVehicle,
		SimulationImportance maxPrec
	);
	void SimulateVehicles
	(
		float deltaT, VehicleSimulation simul, Entity *insideVehicle
	);
	void SimulateBuildings( float deltaT, VehicleSimulation simul );

	void SimulateFastVehicles( float deltaT, VehicleSimulation simul );
	void SimulateCloudlets( float deltaT );

	void PrimaryAllowSwitch( int ms );
	void SecondaryAllowSwitch();
	void SecondaryIsFinished();

	Transport *FindFreeVehicle( Person *driver ) const;

	void VehicleSwitched( Object *from, Object *to );

	void SwitchCameraTo( Object *vehicle, CameraType cam );
	void BrowseCamera( Object *vehicle );

	Object* CameraOn() const {return _cameraOn;}
	Person* PlayerOn() const;
	Person* GetRealPlayer() const;
	void SetRealPlayer(Person *veh);

	bool PlayerManual() const {return _playerManual;}
	void SetPlayerManual( bool manual ) {_playerManual=manual;}

	bool GetPlayerSuspended() const {return _playerSuspended;}
	void SetPlayerSuspended( bool susp ) {_playerSuspended=susp;}

	AIUnit *FocusOn() const;

	GameMode GetMode() const {return _mode;}
	EndMode GetEndMode() const {return _endMission;}
	void SetEndMode(EndMode m) {_endMission = m;}

	RadioChannel &GetRadio();
	const RadioChannel &GetRadio() const;
	Speaker *GetSpeaker() const {return _speaker;}
	void SetSpeaker(RString speaker, float pitch);

	const VehicleType *Preloaded( PreloadedVType type ) const
	{
		PoseidonAssert( type<=NPreloadedVTypes );
		return _preloadedVType[type];
	}

	void SwitchPlayerTo(Person *veh);

	float GetAcceleratedTime() const {return _acceleratedTime;}
	void SetAcceleratedTime( float time ) {_acceleratedTime=time;}
	
	void SkipTime(float time);
	
	AbstractUI *UI() const {return _ui;}
	AbstractOptionsUI *Options() const {return _options;}
	AbstractOptionsUI *UserDialog() const {return _userDlg;}
	AbstractOptionsUI *Map() const {return _map;}
	AbstractOptionsUI *ChatChannel() const {return _channel;}
	AbstractOptionsUI *Chat() const {return _chat;}
	AbstractOptionsUI *VoiceChat() const {return _voiceChat;}

	void OnChannelChanged();

	bool IsSimulationEnabled() const;
	void SetSimulationEnabled(bool val) { _enableSimulation = val; }
	bool IsDisplayEnabled() const;
	bool IsUIEnabled() const;
	bool HasOptions() const;
	bool HasMap() const {return (_showMap || _forceMap) && _map != nullptr;}
	bool HasCompass() const;
	bool HasWatch() const;
	bool CanShowMap() const;
	bool CanShowCompass() const;
	bool CanShowWatch() const;
	void ForceMap(bool force = true) {_forceMap = force;}
	void DestroyOptions(int exitCode);
	void DestroyMap(int exitCode);
	void DestroyChat(int exitCode);
	void DestroyVoiceChat(int exitCode);
	void CreateMainOptions();
	void CreateEndOptions(int mode);
	void CreateChat();
	void CreateVoiceChat();
	void CreateMainMap();
	void CreateWarningMessage(RString text);
	bool SetUserDialog(AbstractOptionsUI *dlg)
	{
		if (_userDlg) return false;
		else
		{
			_userDlg = dlg;
			return true;
		}
	}
	void DestroyUserDialog() {if (_userDlg) _userDlg.Free();}

	void EnableRadio( bool val=true ) {_enableRadio = val;}
	bool IsRadioEnabled() const {return _enableRadio;}
	void SetSimulationFocus( bool val ) {_simulationFocus=val;}
	bool GetSimulationFocus() const {return _simulationFocus;}

	bool GetRenderingDisabled() const {return _noDisplay;}

	void StartIntro();
	void StopIntro();

	void StartLogo();
	void StopLogo();

	void DoKeyDown( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
	void DoKeyUp( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
	Poseidon::ControllerUiScene GetControllerUiScene() const;
	bool DoControllerUiAction(Poseidon::ControllerUiAction action);
	bool IsEditorControllerUiActive();
	void DoChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
	void DoIMEChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags );
	void DoIMEComposition( unsigned nChar, unsigned nFlags );

	CameraType GetCameraType() const {return _camType;}
	CameraType GetCameraTypeWanted() const {return _camTypeMain;}
	
	Scene *GetScene() {return &_scene;}
	Engine *GetEngine() const {return _engine;}
	void AddSensor( Person *vehicle );
	SensorList *GetSensorList() const {return _sensorList;}
	void RemoveSensor( Entity *vehicle );
	void RemoveTarget( Entity *vehicle );

	GameState *GetGameState() const {return &GGameState;}

	float Visibility( AIUnit *from, Object *to ) const;
	Foundation::Time VisibilityTime( AIUnit *from, Object *to ) const;

	AICenter *GetWestCenter() const {return _westCenter;}
	AICenter *GetEastCenter() const {return _eastCenter;}
	AICenter *GetGuerrilaCenter() const {return _guerrilaCenter;}
	AICenter *GetCivilianCenter() const {return _civilianCenter;}
	AICenter *GetLogicCenter() const {return _logicCenter;}
	AICenter *GetCenter(TargetSide side);
	AICenter *CreateCenter(TargetSide side);
	void DeleteCenter(TargetSide side);
	void AddCenter(AICenter *center);	// used in multiplayer
	void RemoveCenter(AICenter *center);	// used in multiplayer

	void ScanPlayers(StaticArrayAuto< OLink<Person> > &players); // used in multiplayers

	void SetWeather(float overcast, float fog, float time);
	void SetDate(int year, int month, int day, int hour, int minute);
	float GetLatitude() const {return _latitude;}
	float GetLongitude() const {return _longitude;}

	private:
	// load/save support
	LSError Serialize(ParamArchive &ar, int message);
	LSError SerializeVehicles(ParamArchive &ar);

	public:
	void SwitchLandscape( const char *name );
	void ActivateAddons(const FindArrayRStringCI &addons);
	bool CheckAddon(const ParamEntry &entry);

	LSError Load(const char *name, int message);
	LSError Save(const char *name, int message) const;

	bool LoadBin(const char *name, int message);
	bool SaveBin(const char *name, int message) const;

	void SaveCrash() const;

	void ResetIDs() const;
	void RemoveIDs() const;

	bool CheckVehicleStructure() const;
};

extern World *GWorld;

#define GLOB_WORLD ( GWorld )

// create AI vehicle
EntityAI *NewVehicle( VehicleType *type, RString shapeName=nullptr, bool fullCreate=true);

EntityAI *NewVehicle( RString typeName, RString shapeName=nullptr, bool fullCreate=true);

Entity *NewNonAIVehicle( RString typeName, RString shapeName=nullptr, bool fullCreate=true);

Object *NewObject( RString typeName );

}  // namespace Poseidon
