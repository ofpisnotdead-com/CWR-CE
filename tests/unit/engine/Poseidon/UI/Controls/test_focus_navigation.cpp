#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Controls/FocusNavigation.hpp>

#include <string>

using Poseidon::FocusNavigation::Direction;
using Poseidon::FocusNavigation::IsCloser;
using Poseidon::FocusNavigation::Reach;
using Poseidon::FocusNavigation::Rect;
using Poseidon::FocusNavigation::Score;
using Poseidon::FocusNavigation::ScoreCandidate;

namespace
{
// RscDisplayNewUser's notebook: a name field over the head area on the left, a
// column of lists, the pitch slider and the squad field on the right.
constexpr Rect kName{0.025f, 0.083f, 0.450f, 0.060f};
constexpr Rect kFace{0.525f, 0.083f, 0.450f, 0.180f};
constexpr Rect kGlasses{0.525f, 0.333f, 0.450f, 0.120f};
constexpr Rect kVoice{0.525f, 0.523f, 0.450f, 0.120f};
constexpr Rect kPitch{0.525f, 0.713f, 0.450f, 0.050f};
constexpr Rect kSquad{0.525f, 0.916f, 0.450f, 0.060f};

// The two buttons of the same dialog, side by side and touching.
constexpr Rect kCancel{0.700f, 0.900f, 0.150f, 0.050f};
constexpr Rect kOk{0.850f, 0.900f, 0.150f, 0.050f};

// Picks between two candidates the way a container walk does.
const Rect& Nearer(Direction dir, const Rect& from, const Rect& a, const Rect& b)
{
    Score scoreA;
    Score scoreB;
    const bool okA = ScoreCandidate(dir, from, a, scoreA);
    const bool okB = ScoreCandidate(dir, from, b, scoreB);
    REQUIRE((okA || okB));
    if (!okB)
    {
        return a;
    }
    if (!okA)
    {
        return b;
    }
    return IsCloser(scoreB, scoreA) ? b : a;
}

bool IsSame(const Rect& a, const Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
} // namespace

TEST_CASE("UI - focus navigation - a candidate behind the focus is no candidate")
{
    Score score;
    CHECK_FALSE(ScoreCandidate(Direction::Up, kGlasses, kVoice, score));
    CHECK_FALSE(ScoreCandidate(Direction::Left, kName, kFace, score));
    CHECK_FALSE(ScoreCandidate(Direction::Down, kSquad, kPitch, score));
    CHECK_FALSE(ScoreCandidate(Direction::Right, kFace, kName, score));
}

TEST_CASE("UI - focus navigation - a row of the same height is no candidate either way")
{
    Score score;
    CHECK_FALSE(ScoreCandidate(Direction::Up, kOk, kCancel, score));
    CHECK_FALSE(ScoreCandidate(Direction::Down, kOk, kCancel, score));
    CHECK(ScoreCandidate(Direction::Left, kOk, kCancel, score));
    CHECK(score.distance == 0.0f);
}

TEST_CASE("UI - focus navigation - a row of the same column is no sideways candidate")
{
    // Screen bounds of the voice list and the pitch slider as the tilted notebook
    // projects them: the same column, a fraction of a pixel apart in width.
    constexpr Rect voice{0.536f, 0.514f, 0.306f, 0.082f};
    constexpr Rect pitch{0.537f, 0.644f, 0.307f, 0.034f};

    Score score;
    CHECK_FALSE(ScoreCandidate(Direction::Left, pitch, voice, score));
    CHECK_FALSE(ScoreCandidate(Direction::Right, pitch, voice, score));
    CHECK(ScoreCandidate(Direction::Up, pitch, voice, score));
}

TEST_CASE("UI - focus navigation - the column walks row by row")
{
    CHECK(IsSame(Nearer(Direction::Down, kFace, kGlasses, kVoice), kGlasses));
    CHECK(IsSame(Nearer(Direction::Down, kVoice, kPitch, kSquad), kPitch));
    CHECK(IsSame(Nearer(Direction::Up, kSquad, kPitch, kVoice), kPitch));
}

TEST_CASE("UI - focus navigation - a sideways move keeps to the facing row")
{
    CHECK(IsSame(Nearer(Direction::Right, kName, kFace, kGlasses), kFace));
    CHECK(IsSame(Nearer(Direction::Left, kFace, kName, kGlasses), kName));

    // The rows further down the right column face the head area, not the name.
    Score score;
    CHECK_FALSE(ScoreCandidate(Direction::Left, kVoice, kName, score));
}

TEST_CASE("UI - focus navigation - a tall control faces the top of a stack")
{
    // RscDisplayLogin: the player list spans the left column and the New, Edit
    // and Delete buttons sit in the right one.
    constexpr Rect list{0.025f, 0.100f, 0.450f, 0.867f};
    constexpr Rect buttonNew{0.525f, 0.100f, 0.450f, 0.080f};
    constexpr Rect buttonEdit{0.525f, 0.200f, 0.450f, 0.080f};
    constexpr Rect buttonDelete{0.525f, 0.300f, 0.450f, 0.080f};

    CHECK(IsSame(Nearer(Direction::Right, list, buttonNew, buttonEdit), buttonNew));
    CHECK(IsSame(Nearer(Direction::Right, list, buttonDelete, buttonNew), buttonNew));
}

TEST_CASE("UI - focus navigation - a control off to the side is no candidate")
{
    // The next column over is nearer than the row below, and still the wrong way
    // for a down press. The load-order walk is what reaches it.
    constexpr Rect from{0.100f, 0.500f, 0.200f, 0.050f};
    constexpr Rect below{0.100f, 0.800f, 0.200f, 0.050f};
    constexpr Rect besideAndBelow{0.500f, 0.560f, 0.200f, 0.050f};

    Score score;
    CHECK(ScoreCandidate(Direction::Down, from, below, score));
    CHECK_FALSE(ScoreCandidate(Direction::Down, from, besideAndBelow, score));
}

namespace
{
// Picks between two candidates the way a flat menu does, where a control off to the
// side still counts.
const Rect& NearestOf(Direction dir, const Rect& from, const Rect& a, const Rect& b)
{
    Score scoreA;
    Score scoreB;
    const bool okA = ScoreCandidate(dir, from, a, scoreA, Reach::Nearest);
    const bool okB = ScoreCandidate(dir, from, b, scoreB, Reach::Nearest);
    REQUIRE((okA || okB));
    if (!okB)
        return a;
    if (!okA)
        return b;
    return IsCloser(scoreB, scoreA) ? b : a;
}
} // namespace

TEST_CASE("UI - focus navigation - a control off to the side is a farther candidate")
{
    // The row straight below wins over one the same distance down but off to the side.
    constexpr Rect from{0.100f, 0.500f, 0.200f, 0.050f};
    constexpr Rect below{0.100f, 0.700f, 0.200f, 0.050f};
    constexpr Rect besideAndBelow{0.500f, 0.700f, 0.200f, 0.050f};

    Score score;
    CHECK(ScoreCandidate(Direction::Down, from, besideAndBelow, score, Reach::Nearest));
    CHECK(IsSame(NearestOf(Direction::Down, from, below, besideAndBelow), below));
}

TEST_CASE("UI - focus navigation - a near control in the next column beats a far one in this column")
{
    constexpr Rect from{0.700f, 0.400f, 0.200f, 0.050f};
    constexpr Rect farBelow{0.700f, 0.800f, 0.200f, 0.050f};
    constexpr Rect nearBesideBelow{0.400f, 0.480f, 0.250f, 0.050f};
    CHECK(IsSame(NearestOf(Direction::Down, from, farBelow, nearBesideBelow), nearBesideBelow));
}

namespace
{
// RscDisplayMain: the profile on the left, the editor, options and the injected Mods
// entry in the middle, the game modes and Quit on the right.
struct MenuItem
{
    const char* name;
    Rect rect;
};
constexpr MenuItem kMainMenu[] = {
    {"Player", {0.020f, 0.145f, 0.500f, 0.050f}}, {"Campaign", {0.720f, 0.145f, 0.250f, 0.050f}},
    {"Single", {0.720f, 0.330f, 0.520f, 0.050f}}, {"Multiplayer", {0.720f, 0.390f, 0.520f, 0.050f}},
    {"Editor", {0.380f, 0.480f, 0.300f, 0.050f}}, {"Options", {0.480f, 0.540f, 0.200f, 0.050f}},
    {"Quit", {0.720f, 0.800f, 0.200f, 0.050f}},   {"Mods", {0.480f, 0.600f, 0.200f, 0.050f}},
};
constexpr int kMainMenuCount = sizeof(kMainMenu) / sizeof(kMainMenu[0]);

// One press the way the container resolves it: the nearest control that way, or none.
int Press(Direction dir, int from)
{
    int best = -1;
    Score bestScore;
    for (int i = 0; i < kMainMenuCount; ++i)
    {
        Score score;
        if (i != from && ScoreCandidate(dir, kMainMenu[from].rect, kMainMenu[i].rect, score, Reach::Nearest) &&
            (best < 0 || IsCloser(score, bestScore)))
        {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

// The focus stays put where a press finds nothing, which ends the walk.
std::string Walk(Direction dir, const char* start)
{
    int at = 0;
    while (std::string(kMainMenu[at].name) != start)
        ++at;
    std::string path = kMainMenu[at].name;
    for (int next = Press(dir, at); next >= 0; next = Press(dir, at))
    {
        at = next;
        path += std::string(" ") + kMainMenu[at].name;
    }
    return path;
}

const char* Step(Direction dir, const char* start)
{
    int at = 0;
    while (std::string(kMainMenu[at].name) != start)
        ++at;
    const int next = Press(dir, at);
    return next < 0 ? "-" : kMainMenu[next].name;
}
} // namespace

TEST_CASE("UI - focus navigation - down takes the nearest item below and stops at the bottom")
{
    CHECK(Walk(Direction::Down, "Campaign") == "Campaign Single Multiplayer Editor Options Mods Quit");
    CHECK(Walk(Direction::Down, "Player") == "Player Editor Options Mods Quit");
}

TEST_CASE("UI - focus navigation - up takes the nearest item above and stops at the top")
{
    CHECK(Walk(Direction::Up, "Quit") == "Quit Mods Options Editor Multiplayer Single Campaign");
}

TEST_CASE("UI - focus navigation - the sideways keys jump to the nearest item on that side")
{
    CHECK(std::string(Step(Direction::Right, "Player")) == "Campaign");
    CHECK(std::string(Step(Direction::Left, "Campaign")) == "Player");
    CHECK(std::string(Step(Direction::Left, "Single")) == "Editor");
    CHECK(std::string(Step(Direction::Right, "Editor")) == "Multiplayer");
    CHECK(std::string(Step(Direction::Right, "Quit")) == "-");
}
