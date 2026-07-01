#pragma once
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>

#include <Poseidon/AI/Path/PathPlanner.hpp>	// AI::Rank, AI::Formation, AI::Semaphore
#include <Poseidon/World/World.hpp>        // CamEffectPosition, TitEffectName


namespace Poseidon
{
struct EFFECTS_INFO;
struct WAYPOINT_INFO;
struct MARKER_INFO;

enum TitleType
{
	TitleNone,
	TitleObject,
	TitleResource,
	TitleText
};

struct ArcadeEffects : public SerializeClass
{
	RString condition;

	RString cameraEffect;
	CamEffectPosition cameraPosition;

	RString sound;
	RString voice;
	RString soundEnv;
	RString soundDet;
	RString track;

	TitleType titleType;
	TitEffectName titleEffect;
	RString title;

	ArcadeEffects();
	ArcadeEffects(const ArcadeEffects &src);

	void Init();

	void FromNet(const EFFECTS_INFO &msg);
	void ToNet(EFFECTS_INFO &msg) const;

	LSError Serialize(ParamArchive &ar) override;
	LSError WorldSerialize(ParamArchive &ar);

	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx);
};

enum ArcadeWaypointType
{
	ACUNDEFINED,
	ACMOVE,
	ACDESTROY,
	ACGETIN,
	ACSEEKANDDESTROY,
	ACJOIN,
	ACLEADER,
	ACGETOUT,
	ACCYCLE,
	ACLOAD,
	ACUNLOAD,
	ACTRANSPORTUNLOAD,
	ACHOLD,
	ACSENTRY,
	ACGUARD,
	ACTALK,
	ACSCRIPTED,
	ACSUPPORT,
	ACN,
	ACLOGIC = ACN,
	ACAND = ACLOGIC,
	ACOR,
	ACLOGICN
};

enum SpeedMode
{
	SpeedUnchanged,
	SpeedLimited,
	SpeedNormal,
	SpeedFull
};

enum CombatMode
{
	CMUnchanged,
	CMCareless,
	CMSafe,
	CMAware,
	CMCombat,
	CMStealth
};

enum AWPShow
{
	ShowNever,
	ShowEasy,
	ShowAlways
};

struct ArcadeWaypointInfo
{
	Point3 position;
	float placement;
	int id;					// target vehicle's id
	int idStatic;		// target building's id
	int housePos;		// position in house
	ArcadeWaypointType type;
	AI::Semaphore combatMode;
	AI::Formation formation;
	SpeedMode speed;
	CombatMode combat;
	float timeoutMin;
	float timeoutMid;
	float timeoutMax;
	RString description;
	RString expCond;
	RString expActiv;
	RString script;
	AWPShow showWP;
	bool selected;
	AutoArray<int> synchronizations;
	ArcadeEffects effects;

	ArcadeWaypointInfo();
	ArcadeWaypointInfo(const ArcadeWaypointInfo &src);

	void Init();

	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);

	bool HasEffect() const;

	LSError Serialize(ParamArchive &ar);

	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx);
};

struct WaypointInfo : public ArcadeWaypointInfo
{
	// World serialization
	LSError Serialize(ParamArchive &ar);
};

enum ArcadeUnitSpecial
{
	ASpNone,
	ASpCargo,
	ASpFlying,
	ASpForm
};

enum ArcadeUnitAge
{
	AAActual,
	AA5Min,
	AA10Min,
	AA15Min,
	AA30Min,
	AA60Min,
	AA120Min,
	AAUnknown,
	AAN
};

enum ArcadeUnitPlayer
{
	APNonplayable,
	APPlayerCommander,
	APPlayerDriver,
	APPlayerGunner,
	APPlayableC,
	APPlayableD,
	APPlayableG,
	APPlayableCD,
	APPlayableCG,
	APPlayableDG,
	APPlayableCDG,
};

DEFINE_ENUM_BEG(LockState)
	LSUnlocked,
	LSDefault,
	LSLocked
DEFINE_ENUM_END(LockState)

enum ArcadeSensorActivation
{
	ASANone,
	ASAEast,
	ASAWest,
	ASAGuerrila,
	ASACivilian,
	ASALogic,
	ASAAnybody,
	ASAAlpha,
	ASABravo,
	ASACharlie,
	ASADelta,
	ASAEcho,
	ASAFoxtrot,
	ASAGolf,
	ASAHotel,
	ASAIndia,
	ASAJuliet,
	ASAStatic = 253,
	ASAVehicle,
	ASAGroup,
	ASALeader,
	ASAMember,
};

enum ArcadeSensorActivationType
{
	ASATPresent,
	ASATNotPresent,
	ASATWestDetected,
	ASATEastDetected,
	ASATGuerrilaDetected,
	ASATCiviliansDetected,
};

enum ArcadeSensorType
{
	ASTNone,
	ASTEastGuarded,
	ASTWestGuarded,
	ASTGuerrilaGuarded,
	ASTSwitch,
	ASTEnd1,
	ASTEnd2,
	ASTEnd3,
	ASTEnd4,
	ASTEnd5,
	ASTEnd6,
	ASTLoose,
	ASTN
};

enum MarkerType
{
	MTIcon,
	MTRectangle,
	MTEllipse
};

struct ArcadeMarkerInfo
{
	Point3 position;
	RString name;
	RString text;
	MarkerType markerType;
	RString type;
	RString colorName;
	PackedColor color;
	Ref<Texture> icon;
	RString fillName;
	Ref<Texture> fill;
	float size;
	float a;
	float b;
	float angle;

	bool selected;	

	ArcadeMarkerInfo();
	ArcadeMarkerInfo(const ArcadeMarkerInfo &src);

	void Init();

	LSError Serialize(ParamArchive &ar);

	NetworkMessageType GetNMType(NetworkMessageClass cls) const;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx);

	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);

	void OnColorChanged();
	void OnFillChanged();
	void OnTypeChanged();
};

}  // namespace Poseidon
