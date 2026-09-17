#pragma once

#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>

#include <Poseidon/Foundation/Containers/RStringArray.hpp>

class Display;

enum TargetSide;

namespace Poseidon
{
struct UNIT_INFO;
struct SENSOR_INFO;



struct ArcadeUnitInfo
{
	float presence;
	RString presenceCondition;
	Point3 position;
	float placement;
	float azimut;
	ArcadeUnitSpecial special;
	ArcadeUnitAge age;
	int id;
	TargetSide side;
	RString vehicle;
	InitPtr<VehicleType> type; // used to override type (in viewer)
	Ref<Texture> icon;
	float size;
	ArcadeUnitPlayer player;
	bool leader;
	LockState lock;
	bool selected;
	Rank rank;
	float skill;
	float health;
	float fuel;
	float ammo;
	RString name;
	RString init;
	AutoArray<RString> markers;

	ArcadeUnitInfo();
	ArcadeUnitInfo(const ArcadeUnitInfo &src);

	void Init();

	void FromNet(const UNIT_INFO &msg);
	void ToNet(UNIT_INFO &msg) const;

	LSError Serialize(ParamArchive &ar);
	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);

	void RequiredAddons(FindArrayRStringCI &addOns);
};

struct ArcadeSensorInfo
{
	Point3 position;
	float a;
	float b;
	float angle;

	ArcadeSensorActivation activationBy;
	ArcadeSensorActivationType activationType;
	float timeoutMin;
	float timeoutMid;
	float timeoutMax;
	bool repeating;
	bool interruptable;
	bool rectangular;
	bool selected;
	ArcadeSensorType type;
	RString object;
	ArcadeUnitAge age;

	int idStatic;
	int idVehicle;

	RString text;
	RString name;

	RString expCond;
	RString expActiv;
	RString expDesactiv;

	ArcadeEffects effects;
	AutoArray<int> synchronizations;

	ArcadeSensorInfo();
	ArcadeSensorInfo(const ArcadeSensorInfo &src);

	void Init();

	void FromNet(const SENSOR_INFO &msg);
	void ToNet(SENSOR_INFO &msg) const;

	LSError Serialize(ParamArchive &ar);
	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);
};

struct ArcadeGroupInfo
{
	TargetSide side;
	AutoArray<ArcadeUnitInfo> units;
	AutoArray<ArcadeWaypointInfo> waypoints;
	AutoArray<ArcadeSensorInfo> sensors;

	LSError Serialize(ParamArchive &ar);
	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);
	void Select(bool select = true);

	void RequiredAddons(FindArrayRStringCI &addOns);
};

struct ArcadeIntel : public SerializeClass
{
	float friends[3][3];
	float weather;
	float fog;
	float weatherForecast;
	float fogForecast;
	int year;
	int month;
	int day;
	int hour;
	int minute;

	RString briefingName;
	RString briefingDescription;

	ArcadeIntel();

	void Init();

	LSError Serialize(ParamArchive &ar) override;
};

struct ATSParams
{
	int nextSyncId;
	int nextVehId;
	ATSParams() {nextSyncId = nextVehId = 0;}
};


struct ArcadeTemplate : public SerializeClass
{
	AutoArray<ArcadeGroupInfo> groups;
	AutoArray<ArcadeUnitInfo> emptyVehicles;
	AutoArray<ArcadeSensorInfo> sensors;
	AutoArray<ArcadeMarkerInfo> markers;
	ArcadeIntel intel;
	int randomSeed;

	bool showHUD;
	bool showMap;
	bool showWatch;
	bool showCompass;
	bool showNotepad;
	bool showGPS;
	
	int nextSyncId;
	int nextVehId;

	//! mission designer can manually add into this section
	FindArrayRStringCI addOns;
	//! list of addons added into addOns by mission editor
	//! items in this list may be also automatically removed
	FindArrayRStringCI addOnsAuto;

	//! list of addons that were detected as missing
	FindArrayRStringCI missingAddOns;

	ArcadeTemplate();

	void ScanRequiredAddons();

	LSError Serialize(ParamArchive &ar) override;

	void CheckSynchro();
	ArcadeUnitInfo *FindUnit(int id, int &idGroup, int &idUnit);
	ArcadeUnitInfo *FindPlayer();
	ArcadeGroupInfo *FindPlayerGroup();
	ArcadeMarkerInfo *FindMarker(const char *name);
	ArcadeUnitInfo *FindVehicle(const char *name);
	ArcadeSensorInfo *FindSensor(const char *name);

	void Clear();
	void GroupDelete(int ig);
	void UnitDelete(int ig, int iu);
	void WaypointDelete(int ig, int iw);
	bool UnitChangeGroup(int ig, int iu, int ignew);
	void WaypointChangeSynchro(int ig, int iw, int ig1, int iw1);
	bool UnitChangePosition(int ig, int iu, Vector3Val pos);
	bool GroupChangePosition(int ig, int iu, Vector3Val pos);
	bool WaypointChangePosition(int ig, int iw, Vector3Val pos);
	void UnitUpdate
	(
		int &ig, int &iu,
		ArcadeUnitInfo &uInfo
	);
	void WaypointUpdate
	(
		int ig, int iw, int &iwnew,
		ArcadeWaypointInfo &waypoint
	);
	void SensorUpdate
	(
		int ig, int index,
		ArcadeSensorInfo &sInfo
	);
	void SensorDelete(int ig, int index);
	bool SensorChangePosition(int ig, int index, Vector3Val pos);
	bool SensorChangeGroup(int ig, int index, int ignew);
	void SensorChangeSynchro(int ig, int index, int ig1, int iw1);
	void SensorChangeVehicle(int ig, int index, int id);
	void SensorChangeStatic(int ig, int index, int id);

	void MarkerUpdate(int index, ArcadeMarkerInfo &mInfo);
	void MarkerDelete(int index);
	bool MarkerChangePosition(int index, Vector3Val pos);
	void UnitAddMarker(int ig, int index, int indexMarker);
	void RemoveMarker(int indexMarker);

	void AddGroup(const ParamEntry &cls, Vector3Par position);

	void SendLoadVehicle(int ig, int iu, ArcadeUnitInfo &uInfo);
	void SendLoadObjective(int ig, int iw, ArcadeWaypointInfo &wInfo);
	void SendLoadSensor(int ig, int index, ArcadeSensorInfo &sInfo);

	bool IsConsistent(Display *disp, bool multiplayer);

	void Compact();
	void Merge(ArcadeTemplate &t, Vector3Par offset = VZero);
	void AddOffset(Vector3Par offset);
	void Rotate(Vector3Par center, float angle, bool sel);
	void CalculateCenter(Vector3 &sum, int &count, bool sel);

	void ClearSelection();

	void RequiredAddons(FindArrayRStringCI &addOns);
};

void FindMissingAddons(const FindArrayRStringCI &addOns, FindArrayRStringCI &missing);
void ReadMissionAddons(const ParamEntry &mission, FindArrayRStringCI &addOns);
// One sentence listing every missing addon, or only the first maxListed followed by "..."
// when maxListed > 0.
RString MissingAddonMessage(const FindArrayRStringCI &missing, int maxListed = 0);

}  // namespace Poseidon
