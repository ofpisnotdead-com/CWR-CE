#include <Poseidon/UI/Controls/CursorLayout.hpp>

#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/UI/LayoutCanvas.hpp>

namespace Poseidon
{
namespace CursorLayout
{
Rect ComputeRect(const Metrics& cursor, float mouseScrX, float mouseScrY, int viewportWidth, int viewportHeight)
{
    // A cursor keeps its declared shape only when both axes scale off the same
    // viewport edge.
    const float cursorPxW = LayoutCanvas::FractionOfHeight(cursor.width) * viewportHeight;
    const float cursorPxH = LayoutCanvas::FractionOfHeight(cursor.height) * viewportHeight;

    Rect rect;
    rect.x = toInt(mouseScrX * viewportWidth - cursor.hotspotX * cursorPxW);
    rect.y = toInt(mouseScrY * viewportHeight - cursor.hotspotY * cursorPxH);
    rect.w = toInt(cursorPxW);
    rect.h = toInt(cursorPxH);
    return rect;
}

Size RescaleForFOV(const Size& canvasSize, float leftFOV, float topFOV)
{
    Size size;
    size.w = canvasSize.w * (LayoutCanvas::kBaseLeftFov / leftFOV);
    size.h = canvasSize.h * (LayoutCanvas::kBaseTopFov / topFOV);
    return size;
}

Size ProjectedSize(float layoutUnits, float leftFOV, float topFOV)
{
    const Size canvasSize{LayoutCanvas::FractionOfWidth(layoutUnits), LayoutCanvas::FractionOfHeight(layoutUnits)};
    return RescaleForFOV(canvasSize, leftFOV, topFOV);
}

float SquareWidthFraction(float heightFraction, int viewportWidth, int viewportHeight)
{
    if (viewportWidth <= 0)
        return heightFraction;
    return heightFraction * viewportHeight / viewportWidth;
}

} // namespace CursorLayout
} // namespace Poseidon
