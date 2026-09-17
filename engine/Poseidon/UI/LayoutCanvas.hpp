#pragma once

namespace Poseidon
{
namespace LayoutCanvas
{
// UI geometry is authored in layout units against a fixed 800x600 canvas and
// mapped onto the real viewport at draw time.
inline constexpr float kWidth = 800.0f;
inline constexpr float kHeight = 600.0f;

inline constexpr float kRatio = kWidth / kHeight;

// AspectSettings normalizes both FOV half-extents against the canvas width, so
// the canvas itself sits at leftFOV 1 and topFOV 600/800.
inline constexpr float kBaseLeftFov = 1.0f;
inline constexpr float kBaseTopFov = kHeight / kWidth;

constexpr float FractionOfWidth(float layoutUnits)
{
    return layoutUnits / kWidth;
}

constexpr float FractionOfHeight(float layoutUnits)
{
    return layoutUnits / kHeight;
}

} // namespace LayoutCanvas
} // namespace Poseidon
