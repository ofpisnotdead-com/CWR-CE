#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Asset/Probes/ShadowInspect.hpp>
#include "test_fixtures.hpp"

#include <cmath>
#include <stddef.h>
#include <string>
#include <vector>

// InspectShadow loads a P3D, renders its depth from the sun with the pure
// ShadowMath kernel, and samples the shadow factor over a ground grid.

using namespace Poseidon;

TEST_CASE("ShadowInspect: synthetic proxy fixture casts a non-trivial ground shadow", "[Graphics][ShadowInspect]")
{
    const char* path = GET_FIXTURE("p3d/proxy_structure.p3d");
    ShadowInspectOptions opts;
    opts.sunAzDeg = 315.0f;
    opts.sunElDeg = 45.0f;

    ShadowInspectResult r = InspectShadow(path, opts);
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(r.triangleCount > 0);
    REQUIRE(r.vertexCount > 0);
    REQUIRE((r.modelMax[1] - r.modelMin[1]) > 0.0f);

    // Casts SOME shadow but does not blanket the whole 2x-margin ground.
    REQUIRE(r.shadowedFraction > 0.05f);
    REQUIRE(r.shadowedFraction < 0.70f);
}

TEST_CASE("ShadowInspect: an overhead light_disc casts a tighter shadow than a low light_disc",
          "[Graphics][ShadowInspect]")
{
    const char* path = GET_FIXTURE("p3d/proxy_structure.p3d");

    ShadowInspectOptions high;
    high.sunAzDeg = 315.0f;
    high.sunElDeg = 85.0f; // near overhead → footprint-sized shadow
    ShadowInspectResult rHigh = InspectShadow(path, high);

    ShadowInspectOptions low;
    low.sunAzDeg = 315.0f;
    low.sunElDeg = 45.0f; // lower sun → longer, larger shadow
    ShadowInspectResult rLow = InspectShadow(path, low);

    REQUIRE(rHigh.ok);
    REQUIRE(rLow.ok);
    // The light_disc elevation must actually flow through the light matrices and change
    // the occlusion — a constant/broken SampleShadow would give equal fractions.
    REQUIRE(rHigh.shadowedFraction > 0.0f);
    REQUIRE(rHigh.shadowedFraction < rLow.shadowedFraction - 0.05f);
}

TEST_CASE("ShadowInspect: a low-poly tree fixture also rasters a sensible shadow", "[Graphics][ShadowInspect]")
{
    const char* path = GET_FIXTURE("p3d/simple_tree.p3d");
    ShadowInspectOptions opts;
    opts.sunElDeg = 50.0f;

    ShadowInspectResult r = InspectShadow(path, opts);
    REQUIRE(r.ok);
    REQUIRE(r.triangleCount > 0);
    REQUIRE(r.shadowedFraction > 0.02f);
    REQUIRE(r.shadowedFraction < 0.90f);
}

TEST_CASE("ShadowInspect: debug image buffers are filled when requested", "[Graphics][ShadowInspect]")
{
    const char* path = GET_FIXTURE("p3d/proxy_structure.p3d");
    ShadowInspectOptions opts;
    opts.shadowRes = 128;
    opts.groundGrid = 64;
    opts.wantImages = true;

    ShadowInspectResult r = InspectShadow(path, opts);
    REQUIRE(r.ok);
    REQUIRE(r.groundW == 64);
    REQUIRE(r.groundH == 64);
    REQUIRE(r.groundGray.size() == static_cast<size_t>(64) * 64);
    REQUIRE(r.depthW == 128);
    REQUIRE(r.depthGray.size() == static_cast<size_t>(128) * 128);
}

TEST_CASE("ShadowInspect: a missing model reports an error, not a crash", "[Graphics][ShadowInspect]")
{
    ShadowInspectResult r = InspectShadow("/nonexistent/model.p3d", ShadowInspectOptions{});
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
}
