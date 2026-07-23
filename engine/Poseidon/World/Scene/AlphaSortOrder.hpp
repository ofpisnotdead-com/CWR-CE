#pragma once

namespace Poseidon::AlphaSort
{

// The alpha pass draws cloudlets (dust/smoke) and objects' blend sections together,
// far-to-near by their camera-space depth (zCoord) so nearer entries composite on top.
// Depth must be the planar camera-space Z, not Euclidean distance, which diverges at
// oblique angles and lets a near wheel sort ahead of the dust over it.

// An object owning depth-writing blend sections sorts by its far extent (centre depth +
// radius) so all of its blend sections draw before the dust that interpenetrates it; the
// dust then depth-tests against them and hazes each section. Cloudlets keep centre depth.
inline float AlphaObjectDepth(float centreZ, float radius)
{
    return centreZ + radius;
}

// Returns <0 if a is farther than b (a is drawn first).
inline int CompareAlphaDepth(float zCoordA, float zCoordB)
{
    if (zCoordA > zCoordB)
    {
        return -1;
    }
    if (zCoordA < zCoordB)
    {
        return +1;
    }
    return 0;
}

} // namespace Poseidon::AlphaSort
