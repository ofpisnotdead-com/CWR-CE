#pragma once

namespace Poseidon
{
namespace CursorLayout
{
// Cursor geometry as declared by CfgWrapperUI >> Cursors: size in layout units,
// hotspot as a fraction of the drawn size.
struct Metrics
{
    float width = 0.0f;
    float height = 0.0f;
    float hotspotX = 0.0f;
    float hotspotY = 0.0f;
};

struct Rect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// mouseScrX/mouseScrY are 0..1 across the 2D viewport (Engine::Width2D/Height2D).
Rect ComputeRect(const Metrics& cursor, float mouseScrX, float mouseScrY, int viewportWidth, int viewportHeight);

// A size held as 0..1 fractions of the viewport.
struct Size
{
    float w = 0.0f;
    float h = 0.0f;
};

// Rescale a size authored against the layout canvas onto the canvas the given
// FOV half-extents actually project.
Size RescaleForFOV(const Size& canvasSize, float leftFOV, float topFOV);

// Size of a world-projected cursor of layoutUnits square.
Size ProjectedSize(float layoutUnits, float leftFOV, float topFOV);

// Fraction of the viewport width covering the same number of device pixels as
// heightFraction covers of the viewport height.
float SquareWidthFraction(float heightFraction, int viewportWidth, int viewportHeight);

} // namespace CursorLayout
} // namespace Poseidon
