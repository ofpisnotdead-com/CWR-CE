#include <catch2/catch_test_macros.hpp>
#include "test_fixtures.hpp"
#include <Poseidon/Graphics/Textures/ModelRenderer.hpp>
#include <catch2/catch_message.hpp>
#include <string>
#include <vector>

using namespace Poseidon;

TEST_CASE("ModelRenderer: RenderP3DFile with ODOL quad view", "[Graphics][ModelRenderer]")
{
    auto result = RenderP3DFile(GET_FIXTURE("p3d/simple_proxy.p3d"), 400, 400, "quad");
    REQUIRE(result.valid());
    REQUIRE(result.width == 400);
    REQUIRE(result.height == 400);
    REQUIRE(result.rgb.size() == 400 * 400 * 3);
}

TEST_CASE("ModelRenderer: RenderP3DFile with front view", "[Graphics][ModelRenderer]")
{
    auto result = RenderP3DFile(GET_FIXTURE("p3d/simple_proxy.p3d"), 256, 256, "front");
    REQUIRE(result.valid());
    REQUIRE(result.width == 256);
    REQUIRE(result.height == 256);
}

TEST_CASE("ModelRenderer: RenderP3DFile with 3d view", "[Graphics][ModelRenderer]")
{
    auto result = RenderP3DFile(GET_FIXTURE("p3d/complex_vehicle.p3d"), 512, 512, "3d");
    REQUIRE(result.valid());
    REQUIRE(result.width == 512);
    REQUIRE(result.height == 512);
}

TEST_CASE("ModelRenderer: RenderP3DFile all view directions", "[Graphics][ModelRenderer]")
{
    const char* views[] = {"front", "back", "left", "right", "top", "bottom", "3d", "quad"};
    for (auto view : views)
    {
        INFO("view: " << view);
        auto result = RenderP3DFile(GET_FIXTURE("p3d/flat_quad.p3d"), 200, 200, view);
        REQUIRE(result.valid());
    }
}

TEST_CASE("ModelRenderer: RenderP3DFile returns invalid for nonexistent file", "[Graphics][ModelRenderer]")
{
    auto result = RenderP3DFile("nonexistent.p3d", 256, 256);
    REQUIRE_FALSE(result.valid());
}

TEST_CASE("ModelRenderer: Rendered quad view has grid dividers", "[Graphics][ModelRenderer]")
{
    auto result = RenderP3DFile(GET_FIXTURE("p3d/simple_proxy.p3d"), 400, 400, "quad", 0, 255, 255, 255);
    REQUIRE(result.valid());
    // Center horizontal divider at y=200 should be gray (64,64,64)
    int midY = 200, midX = 100;
    int idx = (midY * 400 + midX) * 3;
    REQUIRE(result.rgb[idx] == 64);
    REQUIRE(result.rgb[idx + 1] == 64);
    REQUIRE(result.rgb[idx + 2] == 64);
}

TEST_CASE("ModelRenderer: Multiple ODOL fixtures render successfully", "[Graphics][ModelRenderer]")
{
    const char* fixtures[] = {"p3d/simple_tree.p3d", "p3d/proxy_structure.p3d", "p3d/multi_lod_vehicle.p3d",
                              "p3d/sky_plane.p3d", "p3d/crew_proxy.p3d"};
    for (auto f : fixtures)
    {
        INFO("fixture: " << f);
        auto result = RenderP3DFile(GET_FIXTURE(f), 300, 300, "quad");
        REQUIRE(result.valid());
    }
}
