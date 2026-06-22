#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/UI/Settings/PresentationRects.hpp>

using namespace Poseidon;

namespace
{
void CheckRect(Presentation::Rect rect, float x, float y, float w, float h)
{
    CHECK(rect.x == Catch::Approx(x).epsilon(0.001f));
    CHECK(rect.y == Catch::Approx(y).epsilon(0.001f));
    CHECK(rect.w == Catch::Approx(w).epsilon(0.001f));
    CHECK(rect.h == Catch::Approx(h).epsilon(0.001f));
}
} // namespace

TEST_CASE("PresentationRects physical viewport is full screen", "[Settings][PresentationRects]")
{
    AspectSettings aspect{};

    CheckRect(Presentation::ResolveRect(Presentation::RectKind::PhysicalViewport, 3840, 1080, aspect), 0.0f, 0.0f, 1.0f,
              1.0f);
}

TEST_CASE("PresentationRects logical 2D viewport follows AspectSettings", "[Settings][PresentationRects]")
{
    AspectSettings aspect{};
    aspect.uiTopLeftX = 0.3125f;
    aspect.uiTopLeftY = 0.125f;
    aspect.uiBottomRightX = 0.6875f;
    aspect.uiBottomRightY = 0.875f;

    CheckRect(Presentation::ResolveRect(Presentation::RectKind::Logical2DViewport, 3840, 1080, aspect), 0.3125f, 0.125f,
              0.375f, 0.75f);
}

TEST_CASE("PresentationRects common safe rects at 4:3", "[Settings][PresentationRects]")
{
    AspectSettings aspect{};

    CheckRect(Presentation::ResolveRect(Presentation::RectKind::Safe4x3, 1024, 768, aspect), 0.0f, 0.0f, 1.0f, 1.0f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe16x9, 1024, 768, aspect), 0.0f, 0.125f, 1.0f,
              0.75f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe21x9, 1024, 768, aspect), 0.0f, 0.2142857f, 1.0f,
              0.5714286f);
}

TEST_CASE("PresentationRects common safe rects at Full HD", "[Settings][PresentationRects]")
{
    AspectSettings aspect{};

    CheckRect(Presentation::ResolveRect(Presentation::RectKind::Safe4x3, 1920, 1080, aspect), 0.125f, 0.0f, 0.75f,
              1.0f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe16x9, 1920, 1080, aspect), 0.0f, 0.0f, 1.0f,
              1.0f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe21x9, 1920, 1080, aspect), 0.0f, 0.1190476f,
              1.0f, 0.7619048f);
}

TEST_CASE("PresentationRects common safe rects at 32:9", "[Settings][PresentationRects]")
{
    AspectSettings aspect{};

    CheckRect(Presentation::ResolveRect(Presentation::RectKind::Safe4x3, 3840, 1080, aspect), 0.3125f, 0.0f, 0.375f,
              1.0f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe16x9, 3840, 1080, aspect), 0.25f, 0.0f, 0.5f,
              1.0f);
    CheckRect(Presentation::ResolveRect(Presentation::RectKind::HudSafe21x9, 3840, 1080, aspect), 0.171875f, 0.0f,
              0.65625f, 1.0f);
}

TEST_CASE("PresentationRects names parse round trip", "[Settings][PresentationRects]")
{
    Presentation::RectKind kind = Presentation::RectKind::PhysicalViewport;
    REQUIRE(Presentation::ParseRectKind("Logical2DViewport", kind));
    CHECK(kind == Presentation::RectKind::Logical2DViewport);
    CHECK(std::string(Presentation::RectKindName(kind)) == "Logical2DViewport");
    CHECK_FALSE(Presentation::ParseRectKind("NoSuchRect", kind));
}
