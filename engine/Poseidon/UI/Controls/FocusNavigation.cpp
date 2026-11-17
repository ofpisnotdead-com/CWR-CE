#include <Poseidon/UI/Controls/FocusNavigation.hpp>

#include <cmath>

namespace Poseidon
{
namespace FocusNavigation
{
namespace
{
constexpr float kEpsilon = 1e-4f;
// A sideways step counts double, so the control straight ahead wins over one at the
// same distance off to the side, while a near one in the next column still beats a
// far one in this column.
constexpr float kAcrossWeight = 2.0f;

struct Span
{
    float lo = 0.0f;
    float hi = 0.0f;
    float centre = 0.0f;
};

bool IsVertical(Direction dir)
{
    return dir == Direction::Up || dir == Direction::Down;
}

Span Along(Direction dir, const Rect& rect)
{
    const float lo = IsVertical(dir) ? rect.y : rect.x;
    const float size = IsVertical(dir) ? rect.h : rect.w;
    return {lo, lo + size, lo + 0.5f * size};
}

Span Across(Direction dir, const Rect& rect)
{
    const float lo = IsVertical(dir) ? rect.x : rect.y;
    const float size = IsVertical(dir) ? rect.w : rect.h;
    return {lo, lo + size, lo + 0.5f * size};
}

float Gap(const Span& a, const Span& b)
{
    if (b.lo > a.hi)
    {
        return b.lo - a.hi;
    }
    if (a.lo > b.hi)
    {
        return a.lo - b.hi;
    }
    return 0.0f;
}
} // namespace

bool ScoreCandidate(Direction dir, const Rect& from, const Rect& to, Score& score, Reach reach)
{
    const Span fromAlong = Along(dir, from);
    const Span toAlong = Along(dir, to);
    const bool forward = dir == Direction::Right || dir == Direction::Down;
    // The centre has to be past the focused edge, not just past its centre: rows of
    // one column project a fraction of a pixel wider than each other and would
    // otherwise read as standing beside one another.
    const float step = forward ? toAlong.centre - fromAlong.hi : fromAlong.lo - toAlong.centre;
    if (step <= kEpsilon)
    {
        return false;
    }

    const Span fromAcross = Across(dir, from);
    const Span toAcross = Across(dir, to);
    const float across = Gap(fromAcross, toAcross);
    const float along = Gap(fromAlong, toAlong);
    if (across > kEpsilon && reach == Reach::LinedUp)
    {
        return false;
    }
    // A control off to the side has to clear the focused edge entirely: a wide row
    // above or below reaches past the edge without standing in that direction.
    if (across > kEpsilon && along <= kEpsilon)
    {
        return false;
    }
    score.distance = along + kAcrossWeight * across;
    score.lead = std::fabs(toAcross.lo - fromAcross.lo);
    return true;
}

bool IsCloser(const Score& candidate, const Score& best)
{
    if (candidate.distance < best.distance - kEpsilon)
    {
        return true;
    }
    if (best.distance < candidate.distance - kEpsilon)
    {
        return false;
    }
    return candidate.lead < best.lead - kEpsilon;
}

} // namespace FocusNavigation
} // namespace Poseidon
