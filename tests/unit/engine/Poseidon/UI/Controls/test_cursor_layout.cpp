#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Controls/CursorLayout.hpp>
#include <Poseidon/UI/LayoutCanvas.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>

#include <catch2/catch_approx.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

using Poseidon::CursorLayout::ComputeRect;
using Poseidon::CursorLayout::Metrics;
using Poseidon::CursorLayout::ProjectedSize;
using Poseidon::CursorLayout::Rect;
using Poseidon::CursorLayout::RescaleForFOV;
using Poseidon::CursorLayout::Size;

namespace
{
// CfgWrapperUI >> Cursors as shipped in packages/Game: every vanilla cursor is
// square in layout units, and Track's geometry is shared by Move and Scroll.
constexpr Metrics kArrow{16.0f, 16.0f, 0.0f, 0.0f};
constexpr Metrics kTrack{24.0f, 24.0f, 0.5f, 0.5f};

struct Viewport
{
    int w;
    int h;
};

constexpr Viewport kWide[] = {{1600, 900}, {1920, 1080}, {2560, 1080}, {3440, 1440}};

void CheckRect(const Rect& got, int x, int y, int w, int h)
{
    CHECK(got.x == x);
    CHECK(got.y == y);
    CHECK(got.w == w);
    CHECK(got.h == h);
}
} // namespace

TEST_CASE("LayoutCanvas: the canvas keeps its legacy dimensions", "[UI][Cursor][CursorLayout]")
{
    namespace Canvas = Poseidon::LayoutCanvas;
    STATIC_REQUIRE(Canvas::kWidth == 800.0f);
    STATIC_REQUIRE(Canvas::kHeight == 600.0f);
    STATIC_REQUIRE(Canvas::kRatio == 4.0f / 3.0f);
    STATIC_REQUIRE(Canvas::kBaseLeftFov == 1.0f);
    STATIC_REQUIRE(Canvas::kBaseTopFov == 0.75f);
}

TEST_CASE("CursorLayout: square cursors stay square on wide viewports", "[UI][Cursor][CursorLayout]")
{
    for (const Viewport& v : kWide)
    {
        CAPTURE(v.w, v.h);
        const Rect arrow = ComputeRect(kArrow, 0.5f, 0.5f, v.w, v.h);
        CHECK(arrow.w == arrow.h);
        const Rect track = ComputeRect(kTrack, 0.5f, 0.5f, v.w, v.h);
        CHECK(track.w == track.h);
    }

    // 900/600 is 1.5 device pixels per layout unit, so these sizes land exact.
    const Rect arrow16x9 = ComputeRect(kArrow, 0.5f, 0.5f, 1600, 900);
    CHECK(arrow16x9.w == 24);
    CHECK(arrow16x9.h == 24);
    const Rect track16x9 = ComputeRect(kTrack, 0.5f, 0.5f, 1600, 900);
    CHECK(track16x9.w == 36);
    CHECK(track16x9.h == 36);
}

TEST_CASE("CursorLayout: 4:3 viewports keep the legacy geometry", "[UI][Cursor][CursorLayout]")
{
    CheckRect(ComputeRect(kArrow, 0.5f, 0.5f, 800, 600), 400, 300, 16, 16);
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 800, 600), 388, 288, 24, 24);

    CheckRect(ComputeRect(kArrow, 0.5f, 0.5f, 1024, 768), 512, 384, 20, 20);
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 1024, 768), 497, 369, 31, 31);

    CheckRect(ComputeRect(kArrow, 0.5f, 0.5f, 1600, 1200), 800, 600, 32, 32);
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 1600, 1200), 776, 576, 48, 48);
}

TEST_CASE("CursorLayout: cursor size follows viewport height only", "[UI][Cursor][CursorLayout]")
{
    const Rect narrow = ComputeRect(kArrow, 0.0f, 0.0f, 1920, 1080);
    const Rect wide = ComputeRect(kArrow, 0.0f, 0.0f, 2560, 1080);
    CHECK(narrow.w == wide.w);
    CHECK(narrow.h == wide.h);
}

TEST_CASE("CursorLayout: a centre hotspot follows the drawn width", "[UI][Cursor][CursorLayout]")
{
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 1600, 900), 782, 432, 36, 36);
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 1920, 1080), 938, 518, 43, 43);
    CheckRect(ComputeRect(kTrack, 0.5f, 0.5f, 3440, 1440), 1691, 691, 58, 58);
}

TEST_CASE("CursorLayout: a centred cursor stays on the pointer", "[UI][Cursor][CursorLayout]")
{
    for (const Viewport& v : kWide)
    {
        for (float mouseScrX : {0.25f, 0.5f, 0.75f})
        {
            CAPTURE(v.w, v.h, mouseScrX);
            const Rect rect = ComputeRect(kTrack, mouseScrX, 0.5f, v.w, v.h);
            const float centre = rect.x + rect.w * 0.5f;
            CHECK(std::fabs(centre - mouseScrX * v.w) <= 1.0f);
        }
    }
}

TEST_CASE("CursorLayout: a zero hotspot anchors on the pointer", "[UI][Cursor][CursorLayout]")
{
    for (const Viewport& v : kWide)
    {
        CAPTURE(v.w, v.h);
        const Rect rect = ComputeRect(kArrow, 0.25f, 0.75f, v.w, v.h);
        CHECK(rect.x == v.w / 4);
        CHECK(rect.y == v.h * 3 / 4);
    }
}

TEST_CASE("CursorLayout: world cursors stay square across aspect ratios", "[UI][Cursor][CursorLayout]")
{
    for (const Viewport& v : kWide)
    {
        CAPTURE(v.w, v.h);
        const float ratio = static_cast<float>(v.w) / static_cast<float>(v.h);
        const Poseidon::AspectRatio::Settings asp = Poseidon::AspectRatio::BuildSettingsForRatio(ratio);
        const Poseidon::CursorLayout::Size size = ProjectedSize(32.0f, asp.leftFOV, asp.topFOV);
        CHECK(size.w * v.w == Catch::Approx(size.h * v.h).epsilon(0.001));
    }
}

TEST_CASE("CursorLayout: a 4:3 viewport keeps the legacy world cursor fractions", "[UI][Cursor][CursorLayout]")
{
    const Poseidon::AspectRatio::Settings asp = Poseidon::AspectRatio::BuildSettingsForRatio(4.0f / 3.0f);
    const Poseidon::CursorLayout::Size size = ProjectedSize(32.0f, asp.leftFOV, asp.topFOV);
    CHECK(size.w == Catch::Approx(32.0f / 800));
    CHECK(size.h == Catch::Approx(32.0f / 600));
}

TEST_CASE("CursorLayout: rescaling leaves the layout canvas untouched", "[UI][Cursor][CursorLayout]")
{
    const Poseidon::AspectRatio::Settings asp = Poseidon::AspectRatio::BuildSettingsForRatio(4.0f / 3.0f);
    const Size canvasSize{32.0f / 800, 32.0f / 600};
    const Size drawn = RescaleForFOV(canvasSize, asp.leftFOV, asp.topFOV);
    CHECK(drawn.w == Catch::Approx(canvasSize.w));
    CHECK(drawn.h == Catch::Approx(canvasSize.h));
}

TEST_CASE("CursorLayout: a canvas-authored square is drawn square", "[UI][Cursor][CursorLayout]")
{
    for (const Viewport& v : kWide)
    {
        CAPTURE(v.w, v.h);
        const float ratio = static_cast<float>(v.w) / static_cast<float>(v.h);
        const Poseidon::AspectRatio::Settings asp = Poseidon::AspectRatio::BuildSettingsForRatio(ratio);
        const Size drawn = RescaleForFOV({32.0f / 800, 32.0f / 600}, asp.leftFOV, asp.topFOV);
        CHECK(drawn.w * v.w == Catch::Approx(drawn.h * v.h).epsilon(0.001));
    }
}

TEST_CASE("CursorLayout: cursor geometry comes from CursorLayout", "[UI][Cursor][CursorLayout]")
{
    // Every site that turns layout units into device pixels takes its geometry
    // from CursorLayout -- a hand-inlined copy is how the world-projected cursor
    // lost its aspect correction.
    struct Callsite
    {
        const char* path;
        const char* symbol;
        int count;
    };

    const Callsite sites[] = {
        {"UI/Map/UIContainers.cpp", "CursorLayout::ComputeRect(", 1},
        {"UI/InGame/InGameUIDrawCursor.cpp", "CursorLayout::ProjectedSize(", 2},
        {"UI/InGame/InGameUIDrawCursor.cpp", "CursorLayout::RescaleForFOV(", 1},
        {"UI/Map/UIMap.cpp", "CursorLayout::SquareWidthFraction(", 1},
    };

    for (const Callsite& site : sites)
    {
        CAPTURE(site.path, site.symbol);
        const std::filesystem::path source =
            std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" / site.path;
        std::ifstream file(source);
        REQUIRE(file.is_open());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string body = buffer.str();

        int calls = 0;
        for (size_t pos = body.find(site.symbol); pos != std::string::npos; pos = body.find(site.symbol, pos + 1))
        {
            ++calls;
        }
        CHECK(calls == site.count);
    }
}

TEST_CASE("CursorLayout: SquareWidthFraction matches the height in device pixels", "[UI][Cursor][CursorLayout]")
{
    const float heightFraction = 16.0f / 600;
    for (const Viewport& v : kWide)
    {
        CAPTURE(v.w, v.h);
        const float widthFraction = Poseidon::CursorLayout::SquareWidthFraction(heightFraction, v.w, v.h);
        CHECK(widthFraction * v.w == Catch::Approx(heightFraction * v.h));
    }

    // A 4:3 viewport keeps the legacy fraction untouched.
    CHECK(Poseidon::CursorLayout::SquareWidthFraction(heightFraction, 800, 600) == Catch::Approx(16.0f / 800));

    // A collapsed viewport falls back rather than dividing by zero.
    CHECK(Poseidon::CursorLayout::SquareWidthFraction(heightFraction, 0, 600) == Catch::Approx(heightFraction));
}
