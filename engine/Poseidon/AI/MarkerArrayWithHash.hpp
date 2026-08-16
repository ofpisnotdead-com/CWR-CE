#pragma once


#include <unordered_map>

#include <Poseidon/Foundation/Containers/ArrayWithHash.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>

namespace Poseidon
{

struct RStrIHasher
{
	unsigned int operator()(const RString& str) const
	{
		return CalculateStringHashValueCI(str);
	}
};
struct RStrIEqual
{
	bool operator()(const RString& lhs, const RString& rhs) const
	{
		return stricmp(lhs.Data(), rhs.Data()) == 0;
	}
};

struct MarkerNameExtractor
{
	const RString& operator()(const ArcadeMarkerInfo& marker) const
	{
		return marker.name;
	}
};

class GlobalMarkerArrayWithHash : public Foundation::ArrayWithHashDuplicate<ArcadeMarkerInfo, RString, MarkerNameExtractor, RStrIHasher, RStrIEqual>
{
public:
	// Custom Delete method to handle duplicate marker names properly.
	void Delete(int index);
};

}  // namespace Poseidon
