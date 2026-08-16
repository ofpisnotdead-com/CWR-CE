#pragma once

#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>


namespace Poseidon
{
DECL_ENUM(SimulationImportance)

struct RandomVector3Type
{
	Vector3 rng;
	float minT, maxT;

	void Load(const ParamEntry &cfg);
};

class RandomVector3	
{
	Vector3 _cur;
	Vector3 _spd;
	float _timeToWanted;

	public:
	RandomVector3();

	operator const Vector3 &() const {return _cur;}
	__forceinline float X() const {return _cur.X();}
	__forceinline float Y() const {return _cur.Y();}
	__forceinline float Z() const {return _cur.Z();}

	void SetWanted(Vector3Par wanted, float time);
	bool Simulate(float deltaT);
	void SetRandomTgt(Vector3Par rng, float minT, float maxT);

	bool SimulateAndSetRandomTgt
	(
		float deltaT,
		Vector3Par rng, float minT, float maxT
	);
	bool SimulateAndSetRandomTgt
	(
		float deltaT, const RandomVector3Type &type
	);
};

class HeadType
{
public:

	AnimationSection _personality;
	AnimationSection _glasses;

	Animation _lBrow, _mBrow, _rBrow;
	Animation _lMouth, _mMouth, _rMouth;

	Animation _eyelid;
	Animation _lip;

	RandomVector3Type _lBrowRandom, _mBrowRandom, _rBrowRandom;
	RandomVector3Type _lMouthRandom, _mMouthRandom, _rMouthRandom;

	Ref<Texture> _textureOrig;

	HeadType();
	void Load(const ParamEntry &cfg);
	void InitShape(const ParamEntry &cfg, LODShape *shape);
};

struct ManLipInfoItem
{
	float time;
	int phase;
};

class ManLipInfo
{
protected:
	AutoArray<ManLipInfoItem> _items;
	int _current;
	float _freq;
	Foundation::Time _start;
	float _frame;
	float _invFrame;

public:
	ManLipInfo() {_frame = 0.11; _invFrame = 1.0 / _frame;}
	bool AttachWave(IWave *wave, float freq = 1.0f);
	bool GetPhase(int &phase);
	float GetPhase();
	int PhonemeCount() const { return _items.Size(); }
	float ElapsedOffset() const;
	int CurrentCursor() const { return _current; }
};

class Head
{
public:
	Ref<Texture> _glasses;

	float _winkPhase;
	int _forceWinkPhase;
	Foundation::Time _nextWink;

	Ref<Texture> _texture;
	Ref<Texture> _textureWounded;

	Vector3 _lBrow, _mBrow, _rBrow;
	Vector3 _lMouth, _mMouth, _rMouth;
	Vector3 _lBrowOld, _mBrowOld, _rBrowOld;
	Vector3 _lMouthOld, _mMouthOld, _rMouthOld;

	RandomVector3 _lBrowRandom, _mBrowRandom,	_rBrowRandom;
	RandomVector3 _lMouthRandom, _mMouthRandom, _rMouthRandom;

	RString _forceMimic;
	float _mimicPhase;
	const ParamEntry *_mimicMode;
	float _nextMimicTime;

	SRef<ManLipInfo> _lipInfo;
	
	bool _randomLip;
	float _actualRandomLip;
	float _wantedRandomLip;
	Foundation::Time _nextChangeRandomLip;
	float _speedRandomLip;

	Head(const HeadType &type, LODShape *lShape);
	void Animate
	(
		const HeadType &type, LODShape *lShape, int level, bool isDead, Matrix3Par trans, bool hiddenHead
	);
	void Deanimate
	(
		const HeadType &type, LODShape *lShape, int level, bool isDead, Matrix3Par trans, bool hiddenHead
	);

	void Simulate( const HeadType &type, float deltaT, SimulationImportance prec, bool dead );

	int GetFaceAnimation() const {return _forceWinkPhase;}
	void SetFaceAnimation(int phase) {_forceWinkPhase = phase;}

	void SetFace(const HeadType &type, bool women, LODShape *lShape, RString name, RString player = "");
	RString GetFaceTextureName() const;
	void SetGlasses(const HeadType &type, LODShape *lShape, RString name);
	void SetForceMimic(RStringB name);
	void SetMimic(RStringB name);
	void SetMimicMode(RStringB modeName);
	RStringB GetMimicMode() const;

	void AttachWave(IWave *wave, float freq = 1.0f);
	void SetRandomLip(bool set = true); 

protected:
	void NextRandomLip();
};

}  // namespace Poseidon
