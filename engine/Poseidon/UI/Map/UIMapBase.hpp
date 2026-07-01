#pragma once

#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/Foundation/Math/Interpol.hpp>

namespace Poseidon
{
struct MapCoord
{
	int x;
	int y;
	MapCoord(int xx, int yy) {x = xx; y = yy;}
	MapCoord() {} // no init
};

enum SignType
{
	signNone,
	signUnit,
	signVehicle,
	signStatic,
	signCheckpoint,
	signSeekAndDestroy,
	signArcadeUnit,
	signArcadeWaypoint,
	signArcadeSensor,
	signArcadeMarker,
};

struct SignInfo
{
	enum SignType _type;
	OLink<AIUnit> _unit;
	TargetId _id;
	Point3 _pos;
	int _indexGroup;
	int _index;
};

class Notepad : public ControlObjectContainer
{
protected:
	AnimationSection _paper;
	Ref<Texture> _paper1;
	Ref<Texture> _paper2;
	Ref<Texture> _paper3;
	Ref<Texture> _paper4;
	Ref<Texture> _paper5;
	Ref<Texture> _paper6;
	Ref<Texture> _paper7;

	CHTML *_briefing;
public:

	Notepad(ControlsContainer *parent, int idc, const ParamEntry &cls);
	void SetPosition(Vector3Par pos) override;
	void Animate(int level) override;
	void Deanimate(int level) override;
	void SetBriefing(CHTML *briefing) {_briefing = briefing;}
};

class Compass : public ControlObjectWithZoom
{
protected:
	AnimationRotation _pointer;
	AnimationRotation _cover;

public:

	Compass(ControlsContainer *parent, int idc, const ParamEntry &cls);
	void Animate(int level) override;
	void Deanimate(int level) override;
	Vector3 Center() const;
};

class Watch : public ControlObjectWithZoom
{
protected:
	AnimationUV _date1;
	AnimationUV _date2;
	AnimationUV _day;

	AnimationRotation _hour;
	AnimationRotation _minute;
	AnimationRotation _second;

public:

	Watch(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void Animate(int level) override;
	void Deanimate(int level) override;
};

struct MapTypeInfo
{
	Ref<Texture> icon;
	PackedColor color;
	float size;

	void Load(const ParamEntry &cls);
};

struct MapAnimationPhase
{
	Poseidon::Foundation::UITime time;
	float scale;
	Vector3 pos;
};

} // namespace Poseidon
#include <Poseidon/Graphics/Core/Engine.hpp>
namespace Poseidon
{

Display* CurrentDisplay();

class CStaticMap : public CStatic
{
public:
	float _mapX;
	float _mapY;

	Vector3 _mouseWorld;

	Vector3 _defaultCenter;

	struct SignInfo _infoClickCandidate;
	struct SignInfo _infoClick;
	struct SignInfo _infoMove;

	float _scaleX;
	float _scaleY;
	float _invScaleX;
	float _invScaleY;
	float _scaleMin;
	float _scaleMax;
	float _scaleDefault;

//	bool _moveOnEdges;

	Point2DFloat _center;

	float _wScreen;
	float _hScreen;
	Rect2DPixel _clipRect;
	int _mipmapLevel;

	Poseidon::Foundation::UITime _animationStart;
	Poseidon::Foundation::UITime _animationLast;
	AutoArray<MapAnimationPhase> _animation;
	APtr<Matrix4> _animMatrices;
	APtr<float> _animTimes;
	SRef<M4Function> _interpolator;

	Poseidon::Foundation::UITime _moveStart;
	Poseidon::Foundation::UITime _moveLast;
	Poseidon::Foundation::UITime _edgeScrollLast;
	unsigned _moveKey;
	unsigned _mouseKey;

	bool _moving;
	bool _dragging;
	bool _selecting;

#if _ENABLE_CHEATS
	bool _show;
	bool _showCost;
#endif
	bool _showIds;
	bool _showScale;

	Ref<Texture> _iconPlayer;
	Ref<Texture> _iconSelect;
	Ref<Texture> _iconCamera;
	Ref<Texture> _iconSensor;
	PackedColor _colorFriendly;
	PackedColor _colorEnemy;
	PackedColor _colorCivilian;
	PackedColor _colorNeutral;
	PackedColor _colorUnknown;
	PackedColor _colorMe;
	PackedColor _colorPlayable;
	PackedColor _colorSelect;
	PackedColor _colorSensor;
	PackedColor _colorDragging;

	PackedColor _colorExposureEnemy;
	PackedColor _colorExposureUnknown;
	PackedColor _colorRoads;
	PackedColor _colorGrid;
	PackedColor _colorGridMap;
	PackedColor _colorCheckpoints;
	PackedColor _colorCamera;
	PackedColor _colorMissions;
	PackedColor _colorActiveMission;
	PackedColor _colorPath;
	PackedColor _colorInfoMove;
	PackedColor _colorGroups;
	PackedColor _colorActiveGroup;
	PackedColor _colorSync;
	PackedColor _colorLabelBackground;

	Vector3 _special;
	Vector3 _lastPos;
//	DrawCoord _dragOffset;

	//                                //
	// NEW PROPERTIES - from resource //
	//                                //
	PackedColor _colorSeaPacked;
	Color _colorSea;
	PackedColor _colorForest;

	PackedColor _colorCountlines;
	PackedColor _colorCountlinesWater;
	PackedColor _colorForestBorder;
	PackedColor _colorNames;

	PackedColor _colorInactive;

	Ref<Font> _fontLabel;
	float _sizeLabel;
	Ref<Font> _fontGrid;
	float _sizeGrid;
	Ref<Font> _fontUnits;
	float _sizeUnits;
	Ref<Font> _fontNames;
	float _sizeNames;

	MapTypeInfo _infoTree;
	MapTypeInfo _infoSmallTree;
	MapTypeInfo _infoBush;
	MapTypeInfo _infoChurch;
	MapTypeInfo _infoChapel;
	MapTypeInfo _infoCross;
	MapTypeInfo _infoRock;
	MapTypeInfo _infoBunker;
	MapTypeInfo _infoFortress;
	MapTypeInfo _infoFountain;
	MapTypeInfo _infoViewTower;
	MapTypeInfo _infoLighthouse;
	MapTypeInfo _infoQuay;
	MapTypeInfo _infoFuelstation;
	MapTypeInfo _infoHospital;
	MapTypeInfo _infoBusStop;

	MapTypeInfo _infoWaypoint;
	MapTypeInfo _infoWaypointCompleted;

public:

	CStaticMap
	(
		ControlsContainer *parent, int idc, const ParamEntry &cls,
		float scaleMin, float scaleMax, float scaleDefault
	);

	void OnRButtonDown(float x, float y) override;
	void OnRButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	virtual void ProcessCheats();

	void OnDraw( float alpha ) override;

	virtual void DrawExt( float alpha ) {}

	void Reset();

	void SetScale(float scale);
	float GetScale() const {return _scaleX;}

	void SetVisibleRect(float x, float y, float w, float h);	// in screen coordinates
	void Center(Vector3Val pt);
	virtual void Center();
	Vector3 GetCenter();

	void ClearAnimation();
	void AddAnimationPhase(float dt, float scale, Vector3Par pos);
	void CreateInterpolator();
	bool HasAnimation() const {return _animation.Size() > 0;}

	Point2DFloat WorldToScreen(Vector3Val pt);
	Point3 ScreenToWorld(Point2DFloat ptMap);

	void ScrollX(float dif);
	void ScrollY(float dif);
	void ScrollOnEdges(float mouseX, float mouseY);

	bool IsShowingIds() const {return _showIds;}
	void ShowIds(bool show = true) {_showIds = show;}

	bool IsShowingScale() const {return _showScale;}
	void ShowScale(bool show = true) {_showScale = show;}

// implementation
protected:
	void Precalculate();
	void SaturateX(float &x);
	void SaturateY(float &y);

	bool DrawSign(Texture *texture, PackedColor color,
		Vector3Val pos, float w, float h, float azimut, RString text = RString());

	bool DrawSign(Texture *texture, PackedColor color,
		DrawCoord posMap, float w, float h, float azimut, RString text = RString());

	bool DrawSign(MapTypeInfo &info, Vector3Val pos);

	virtual void DrawLabel(struct SignInfo &info, PackedColor color) {}
	void DrawBackground();
	void DrawLegend();
	void DrawName(const ParamEntry &cls);
	void DrawMount(Vector3Par pos);

	void DrawField
	(
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);

	void DrawScale
	(
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);

	void DrawSea
	(
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep
	);

	void DrawLines
	(
		DrawCoord *pt, float *height,
		float step, float minLevel, float maxLevel, PackedColor color
	);

	void DrawCountlines
	(
		float x, float y, float xStep, float yStep,
		int i, int j, int iStep, int jStep,
		float step, float maxLevel
	);

	void DrawForests(int i, int j, float x, float y, float w, float h);

	void DrawObjects(int i, int j);

	void DrawForestBorders(int i, int j, float x, float y, float w, float h);

	void DrawRoads(int i, int j);
	void DrawGrid();

	void DrawEllipse(Vector3Val position, float a, float b, float angle, PackedColor color);

	void DrawRectangle(Vector3Val position, float a, float b, float angle, PackedColor color);

	void FillEllipse(Vector3Val position, float a, float b, float angle, PackedColor color, Texture *texture);

	void FillRectangle(Vector3Val position, float a, float b, float angle, PackedColor color, Texture *texture);

	virtual SignInfo FindSign(float x, float y);

	PackedColor InactiveColor(PackedColor color);
};

enum WeatherState
{
	WSUndefined,
	WSClear,
	WSCloudly,
	WSOvercast,
	WSRainy,
	WSStormy
};

WeatherState GetWeatherState(float overcast);

} // namespace Poseidon
