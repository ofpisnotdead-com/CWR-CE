#pragma once

namespace Poseidon
{
namespace FocusNavigation
{
enum class Direction
{
    Left,
    Right,
    Up,
    Down
};

// Control bounds in 0..1 UI coordinates: the space 2D controls are authored in
// and the one notebook sub-controls are projected into, so both compare.
struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct Score
{
    float distance = 0.0f;
    float lead = 0.0f;
};

enum class Reach
{
    // Only a control that lines up with the focus in that direction.
    LinedUp,
    // Any control past the focused edge, with a sideways offset counted as extra
    // distance, so the nearest one wins even in the next column.
    Nearest,
};

// False unless `to` lies past `from` in direction `dir` within `reach`. LinedUp
// takes both the controls behind the move and the ones off to the side out of the
// running.
bool ScoreCandidate(Direction dir, const Rect& from, const Rect& to, Score& score, Reach reach = Reach::LinedUp);

// The shorter gap wins, then the smaller leading-edge offset, which lands on the
// first row of a stack a tall control faces.
bool IsCloser(const Score& candidate, const Score& best);

} // namespace FocusNavigation
} // namespace Poseidon
