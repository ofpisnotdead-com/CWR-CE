#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/World/Scene/Scene.hpp>

using namespace Poseidon;

// Scene::DrawFlares() ground-occludes point-light halos by ray-casting from the
// camera towards the light and checking whether terrain is hit first (issue #87:
// point-light halos shone through terrain at dawn/dusk). The pre-fix code reused
// the "light -> camera" vector as-is for that ray (firing it away from the light)
// and tested only the first 1.1 world units regardless of how far the light
// actually was, so terrain almost never occluded anything. ComputeLightOcclusionRay
// pins the corrected geometry: the ray must point from camera TO the light, and the
// tested distance must scale with the real distance to the light.
TEST_CASE("ComputeLightOcclusionRay: ray points from camera towards the light", "[scene][light]")
{
    Vector3 camPos(0, 0, 0);
    Vector3 lightPos(10, 0, 0);

    LightOcclusionRay ray = ComputeLightOcclusionRay(camPos, lightPos);

    // dir must point towards the light (positive X here), not away from it.
    REQUIRE(ray.dir.X() > 0);
    REQUIRE(ray.dir.X() == Catch::Approx(10.0f));
}

TEST_CASE("ComputeLightOcclusionRay: max distance scales with the real light distance", "[scene][light]")
{
    Vector3 camPos(0, 0, 0);

    // A light 10 units away must not use the same test distance as one 1000 units away
    // (the pre-fix bug hardcoded 1.1 regardless of actual distance).
    LightOcclusionRay near = ComputeLightOcclusionRay(camPos, Vector3(10, 0, 0));
    LightOcclusionRay far = ComputeLightOcclusionRay(camPos, Vector3(1000, 0, 0));

    REQUIRE(near.dist == Catch::Approx(10.0f));
    REQUIRE(near.maxDist == Catch::Approx(11.0f)); // 10 * 1.1

    REQUIRE(far.dist == Catch::Approx(1000.0f));
    REQUIRE(far.maxDist == Catch::Approx(1100.0f)); // 1000 * 1.1

    REQUIRE(far.maxDist > near.maxDist);
}
