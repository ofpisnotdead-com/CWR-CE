#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
namespace Poseidon { class ParamEntry; }


namespace Poseidon
{
struct RecoilItem
{
	float _time;
	Matrix4 _mat;
};

struct RecoilFFRamp
{
	float _startTime,_duration;
	float _begAmplitude,_endAmplitude;
};

class RecoilFunction: public RefCount
{
	AutoArray<RecoilItem> _queue;
	RStringB _name;

	public:
	RecoilFunction();
	RecoilFunction( RStringB name );
	~RecoilFunction() override;

	const RStringB &GetName() const;

	void AddItem(const RecoilItem &item);
	void AddItem(float time, float offset, float angle);

	float GetRecoilRotX( float time) const;
	void ApplyRecoil(float time, Matrix4 &mat, float factor=1) const;
	bool GetTerminated( float time ) const;

	void GetFFEdge(int index, float &time, float &amplitude) const;
	bool GetFFRamp(int index, RecoilFFRamp &ramp) const;
};

}  // namespace Poseidon
