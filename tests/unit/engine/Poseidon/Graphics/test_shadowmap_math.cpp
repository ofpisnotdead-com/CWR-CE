#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Shadow/ShadowMath.hpp>

#include <cmath>
#include <stddef.h>
#include <array>
#include <initializer_list>
#include <vector>

// Phase A of the shadow-map plan: pure,
// engine-free shadow math proven in isolation before any GL. A4 (texel snap =
// anti-swim) and A7 (the SampleShadow occlusion oracle the GPU shader mirrors)
// are the load-bearing cases — they pin "stable while moving" and "correct
// occlusion" objectively.

using namespace Poseidon::shadow;

namespace
{
constexpr float kPi = 3.14159265358979323846f;

bool ApproxV(const Vec3& a, const Vec3& b, float margin = 1e-4f)
{
    return std::fabs(a.x - b.x) < margin && std::fabs(a.y - b.y) < margin && std::fabs(a.z - b.z) < margin;
}
} // namespace

// ── matrix primitives ────────────────────────────────────────────────────────

TEST_CASE("ShadowMath: identity transforms a point unchanged", "[Graphics][ShadowMath]")
{
    Vec3 p{3.0f, -2.0f, 5.0f};
    REQUIRE(ApproxV(TransformPoint(Identity(), p), p));
}

TEST_CASE("ShadowMath: inverse of a view matrix round-trips a point", "[Graphics][ShadowMath]")
{
    Mat4 v = LookAt({4.0f, 9.0f, -3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    Mat4 vi = Inverse(v);
    Vec3 p{1.5f, -0.5f, 2.25f};
    Vec3 back = TransformPoint(vi, TransformPoint(v, p));
    REQUIRE(ApproxV(back, p, 1e-3f));
}

// ── A1 — cascade splits ──────────────────────────────────────────────────────

TEST_CASE("ShadowMath A1: split endpoints are exact and count is n+1", "[Graphics][ShadowMath]")
{
    auto s = CascadeSplits(2.0f, 200.0f, 3, 0.5f);
    REQUIRE(s.size() == 4);
    REQUIRE(s.front() == Catch::Approx(2.0f));
    REQUIRE(s.back() == Catch::Approx(200.0f));
}

TEST_CASE("ShadowMath A1: splits are monotonically increasing", "[Graphics][ShadowMath]")
{
    auto s = CascadeSplits(1.0f, 500.0f, 4, 0.7f);
    for (size_t i = 1; i < s.size(); i++)
    {
        REQUIRE(s[i] > s[i - 1]);
    }
}

TEST_CASE("ShadowMath A1: lambda=0 is uniform, lambda=1 is logarithmic", "[Graphics][ShadowMath]")
{
    auto uni = CascadeSplits(1.0f, 100.0f, 2, 0.0f);
    REQUIRE(uni[1] == Catch::Approx(50.5f)); // 1 + 99*0.5

    auto log = CascadeSplits(1.0f, 100.0f, 2, 1.0f);
    REQUIRE(log[1] == Catch::Approx(10.0f)); // 1 * (100/1)^0.5

    // The documented blended value: 0.5*10 + 0.5*50.5 = 30.25.
    auto mix = CascadeSplits(1.0f, 100.0f, 2, 0.5f);
    REQUIRE(mix[1] == Catch::Approx(30.25f));
}

// ── A2 — frustum corners ─────────────────────────────────────────────────────

TEST_CASE("ShadowMath A2: identity invViewProj yields the NDC cube corners", "[Graphics][ShadowMath]")
{
    auto c = FrustumCornersWS(Identity());
    REQUIRE(ApproxV(c[0], {-1.0f, -1.0f, 0.0f})); // near plane: zero-to-one NDC z = 0
    REQUIRE(ApproxV(c[2], {1.0f, 1.0f, 0.0f}));
    REQUIRE(ApproxV(c[4], {-1.0f, -1.0f, 1.0f})); // far plane: NDC z = 1
    REQUIRE(ApproxV(c[6], {1.0f, 1.0f, 1.0f}));
}

TEST_CASE("ShadowMath A2: a real camera puts near corners near, far corners far", "[Graphics][ShadowMath]")
{
    Mat4 view = LookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    Mat4 proj = Perspective(kPi / 3.0f, 1.0f, 1.0f, 50.0f); // 60° fovy, near 1, far 50
    Mat4 invVP = Inverse(Mul(proj, view));
    auto c = FrustumCornersWS(invVP);
    // Near plane at z ≈ -1, far plane at z ≈ -50 (looking down -Z).
    REQUIRE(c[0].z == Catch::Approx(-1.0f).margin(0.01f));
    REQUIRE(c[4].z == Catch::Approx(-50.0f).margin(0.1f));
    // Half-width at the near plane = tan(30°) ≈ 0.5774.
    REQUIRE(std::fabs(c[0].x) == Catch::Approx(std::tan(kPi / 6.0f)).margin(0.01f));
}

TEST_CASE("ShadowMath A2: slicing the full edge by t=[0,1] is the identity", "[Graphics][ShadowMath]")
{
    auto c = FrustumCornersWS(Identity());
    auto whole = SliceFrustum(c, 0.0f, 1.0f);
    for (int i = 0; i < 8; i++)
    {
        REQUIRE(ApproxV(whole[i], c[i]));
    }
    // Half slice: the new far face sits at the edge midpoints.
    auto half = SliceFrustum(c, 0.0f, 0.5f);
    REQUIRE(ApproxV(half[4], Lerp(c[0], c[4], 0.5f)));
}

// ── A3 — light view + ortho fit ──────────────────────────────────────────────

TEST_CASE("ShadowMath A3: FitOrtho lands every point inside the NDC cube", "[Graphics][ShadowMath]")
{
    Mat4 lv = LightView({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    Vec3 pts[] = {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f},
                  {-5.0f, 0.0f, 5.0f},  {0.0f, 4.0f, 0.0f},  {2.0f, -3.0f, 1.0f}};
    OrthoFit fit = FitOrtho(lv, pts, 6);
    Mat4 vp = Mul(fit.proj, lv);
    for (const Vec3& p : pts)
    {
        Vec3 q = TransformPoint(vp, p);
        REQUIRE(q.x >= -1.0001f);
        REQUIRE(q.x <= 1.0001f);
        REQUIRE(q.y >= -1.0001f);
        REQUIRE(q.y <= 1.0001f);
        REQUIRE(q.z >= -1.0001f);
        REQUIRE(q.z <= 1.0001f);
    }
}

TEST_CASE("ShadowMath A3: the light-space AABB corners map to the NDC cube edges", "[Graphics][ShadowMath]")
{
    Mat4 lv = LightView({0.3f, -1.0f, 0.2f}, {0.0f, 1.0f, 0.0f});
    Vec3 pts[] = {{-8.0f, 1.0f, -6.0f}, {7.0f, -2.0f, 9.0f}, {1.0f, 5.0f, -3.0f}};
    OrthoFit fit = FitOrtho(lv, pts, 3);

    // proj alone maps the light-space AABB onto the cube: min corner → (-1,-1,+1)
    // (minB.z is the far plane, NDC z=1), max corner → (+1,+1,0) (near, NDC z=0).
    Vec3 lo = TransformPoint(fit.proj, fit.minB);
    Vec3 hi = TransformPoint(fit.proj, fit.maxB);
    REQUIRE(ApproxV(lo, {-1.0f, -1.0f, 1.0f}, 1e-3f));
    REQUIRE(ApproxV(hi, {1.0f, 1.0f, 0.0f}, 1e-3f));
}

// ── A4 — texel snap (anti-swim) ──────────────────────────────────────────────

TEST_CASE("ShadowMath A4: a sub-texel shift leaves the snapped matrix unchanged", "[Graphics][ShadowMath]")
{
    const int res = 8;
    // Two fits whose regions differ by less than one texel and share extent.
    // texel = extent/res = 8/8 = 1 world unit.
    OrthoFit a;
    a.minB = {0.0f, 0.0f, -20.0f};
    a.maxB = {8.0f, 8.0f, -10.0f};
    a.proj = Ortho(a.minB.x, a.maxB.x, a.minB.y, a.maxB.y, -a.maxB.z, -a.minB.z);

    OrthoFit b;
    b.minB = {0.3f, 0.4f, -20.0f}; // < 1 texel shift, same extent
    b.maxB = {8.3f, 8.4f, -10.0f};
    b.proj = Ortho(b.minB.x, b.maxB.x, b.minB.y, b.maxB.y, -b.maxB.z, -b.minB.z);

    OrthoFit sa = SnapToTexelGrid(a, res);
    OrthoFit sb = SnapToTexelGrid(b, res);

    for (int i = 0; i < 16; i++)
    {
        REQUIRE(sa.proj.m[i] == Catch::Approx(sb.proj.m[i]).margin(1e-5f));
    }
}

TEST_CASE("ShadowMath A4: a multi-texel shift DOES move the snapped matrix", "[Graphics][ShadowMath]")
{
    const int res = 8; // texel = 1 world unit
    OrthoFit a;
    a.minB = {0.0f, 0.0f, -20.0f};
    a.maxB = {8.0f, 8.0f, -10.0f};

    OrthoFit b = a;
    b.minB.x += 2.0f; // two whole texels
    b.maxB.x += 2.0f;

    OrthoFit sa = SnapToTexelGrid(a, res);
    OrthoFit sb = SnapToTexelGrid(b, res);

    // At least the X translation column must differ.
    bool differs = false;
    for (int i = 0; i < 16; i++)
    {
        if (std::fabs(sa.proj.m[i] - sb.proj.m[i]) > 1e-4f)
        {
            differs = true;
        }
    }
    REQUIRE(differs);
}

// ── A5 — receiver bias ───────────────────────────────────────────────────────

TEST_CASE("ShadowMath A5: head-on bias is small, grazing bias is larger", "[Graphics][ShadowMath]")
{
    Vec3 down{0.0f, -1.0f, 0.0f};
    Bias headOn = ShadowBias({0.0f, 1.0f, 0.0f}, down, 0.05f);   // normal faces the light
    Bias grazing = ShadowBias({1.0f, 0.15f, 0.0f}, down, 0.05f); // nearly perpendicular

    REQUIRE(headOn.normalOffset == Catch::Approx(0.0f).margin(1e-4f));
    REQUIRE(grazing.depthBias > headOn.depthBias);
    REQUIRE(grazing.normalOffset > headOn.normalOffset);
}

TEST_CASE("ShadowMath A5: depth bias increases monotonically toward grazing, then clamps", "[Graphics][ShadowMath]")
{
    Vec3 down{0.0f, -1.0f, 0.0f};
    float prev = -1.0f;
    for (int i = 0; i <= 8; i++)
    {
        float ang = (kPi / 2.0f) * (static_cast<float>(i) / 9.0f); // 0 → almost 90°
        Vec3 n{std::sin(ang), std::cos(ang), 0.0f};
        Bias bz = ShadowBias(n, down, 0.05f);
        REQUIRE(bz.depthBias >= prev);
        REQUIRE(bz.depthBias <= 0.01f + 1e-6f); // clamped
        prev = bz.depthBias;
    }
}

// ── A6 — world <-> shadow UV ─────────────────────────────────────────────────

TEST_CASE("ShadowMath A6: WorldToShadowUV round-trips through its inverse", "[Graphics][ShadowMath]")
{
    Mat4 lv = LookAt({0.0f, 20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    Mat4 proj = Ortho(-10.0f, 10.0f, -10.0f, 10.0f, 15.0f, 25.0f);
    Mat4 lightVP = Mul(proj, lv);

    Vec3 p{3.0f, 0.0f, -2.0f};
    ShadowUV uv = WorldToShadowUV(lightVP, p);
    REQUIRE(uv.inMap);

    Vec3 back = ShadowUVToWorld(Inverse(lightVP), uv.u, uv.v, uv.depth);
    REQUIRE(ApproxV(back, p, 1e-2f));
}

TEST_CASE("ShadowMath A6: a point outside the light frustum is flagged out-of-map", "[Graphics][ShadowMath]")
{
    Mat4 lv = LookAt({0.0f, 20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    Mat4 proj = Ortho(-10.0f, 10.0f, -10.0f, 10.0f, 15.0f, 25.0f);
    Mat4 lightVP = Mul(proj, lv);

    ShadowUV uv = WorldToShadowUV(lightVP, {100.0f, 0.0f, 0.0f}); // way outside in X
    REQUIRE_FALSE(uv.inMap);
}

// ── A7 + A8 — the occlusion oracle on rasterised geometry ────────────────────
// A roof patch at y=5 floats over a floor at y=0. With the sun straight down,
// the floor directly beneath the roof must be shadowed and the floor away from
// it lit. This is the contract the GPU shadow test must reproduce.

namespace
{
struct RoofScene
{
    Mat4 lightVP;
    DepthMap map;
};

RoofScene BuildRoofScene(int resolution)
{
    // Floor [-10,10]^2 at y=0, roof patch [-3,3]^2 at y=5.
    const Vec3 A{-10.0f, 0.0f, -10.0f}, B{10.0f, 0.0f, -10.0f}, C{10.0f, 0.0f, 10.0f}, D{-10.0f, 0.0f, 10.0f};
    const Vec3 E{-3.0f, 5.0f, -3.0f}, F{3.0f, 5.0f, -3.0f}, G{3.0f, 5.0f, 3.0f}, H{-3.0f, 5.0f, 3.0f};
    const Tri tris[] = {{A, B, C}, {A, C, D}, {E, F, G}, {E, G, H}};
    const Vec3 scenePts[] = {A, B, C, D, E, F, G, H};

    Mat4 lv = LookAt({0.0f, 20.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    OrthoFit fit = FitOrtho(lv, scenePts, 8);
    RoofScene s;
    s.lightVP = Mul(fit.proj, lv);
    s.map = CpuRasterDepth(tris, 4, s.lightVP, resolution);
    return s;
}
} // namespace

TEST_CASE("ShadowMath A8: the raster stores the nearer (roof) depth over the floor", "[Graphics][ShadowMath]")
{
    RoofScene s = BuildRoofScene(64);
    // Under the roof, the stored nearest-to-light depth must be the roof's (~0),
    // not the floor's (~1).
    ShadowUV center = WorldToShadowUV(s.lightVP, {0.0f, 0.0f, 0.0f});
    int cx = static_cast<int>(center.u * s.map.w);
    int cy = static_cast<int>(center.v * s.map.h);
    float stored = s.map.depth[static_cast<size_t>(cy) * s.map.w + cx];
    REQUIRE(stored < 0.25f); // roof is near the light
}

TEST_CASE("ShadowMath A7: floor under the roof is shadowed, floor in the open is lit", "[Graphics][ShadowMath]")
{
    RoofScene s = BuildRoofScene(64);
    const float bias = 0.002f;

    float under = SampleShadow(s.map, s.lightVP, {0.0f, 0.0f, 0.0f}, bias, 0);
    float open = SampleShadow(s.map, s.lightVP, {8.0f, 0.0f, 8.0f}, bias, 0);

    REQUIRE(under == Catch::Approx(0.0f).margin(1e-4f)); // fully shadowed
    REQUIRE(open == Catch::Approx(1.0f).margin(1e-4f));  // fully lit
}

TEST_CASE("ShadowMath A7: a receiver does not self-shadow under correct bias", "[Graphics][ShadowMath]")
{
    RoofScene s = BuildRoofScene(64);
    // An open floor point compares against its own stored depth; bias must keep
    // it lit rather than acne-shadowing itself.
    float v = SampleShadow(s.map, s.lightVP, {-7.0f, 0.0f, 6.0f}, 0.002f, 1);
    REQUIRE(v == Catch::Approx(1.0f).margin(1e-4f));
}

TEST_CASE("ShadowMath A7: PCF softens the shadow boundary", "[Graphics][ShadowMath]")
{
    RoofScene s = BuildRoofScene(128);
    // Sample right at the roof's edge (x≈3). A 3x3 PCF kernel straddles lit and
    // shadowed texels → a fractional, partly-shadowed result.
    float edge = SampleShadow(s.map, s.lightVP, {3.0f, 0.0f, 0.0f}, 0.002f, 1);
    REQUIRE(edge > 0.0f);
    REQUIRE(edge < 1.0f);
}

// ── Cascade composition — the stability guarantees the engine depth pass needs ──
// FrustumBoundingSphere gives a rotation-invariant extent; FitOrthoSphere + texel
// snap pin the matrix to whole-texel steps. Together: no edge crawl under sub-texel
// motion, and no texel-density change as the camera turns.

namespace
{
// Yaw a point about a pivot around world +Y.
Vec3 YawAbout(const Vec3& p, const Vec3& pivot, float ang)
{
    float c = std::cos(ang);
    float s = std::sin(ang);
    Vec3 d = p - pivot;
    return {pivot.x + d.x * c + d.z * s, pivot.y + d.y, pivot.z - d.x * s + d.z * c};
}

Mat4 CameraInvVP(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    Mat4 view = LookAt(eye, center, up);
    Mat4 proj = Perspective(kPi / 3.0f, 1.6f, 1.0f, 80.0f);
    return Inverse(Mul(proj, view));
}
} // namespace

TEST_CASE("ShadowMath cascade: the frustum bounding-sphere radius is rotation-invariant", "[Graphics][ShadowMath]")
{
    Mat4 invVP = CameraInvVP({0.0f, 2.0f, 0.0f}, {0.0f, 2.0f, -10.0f}, {0.0f, 1.0f, 0.0f});
    auto corners = FrustumCornersWS(invVP);
    BoundingSphere a = FrustumBoundingSphere(corners);

    std::array<Vec3, 8> rot;
    for (int i = 0; i < 8; i++)
    {
        rot[i] = YawAbout(corners[i], a.center, 0.9f);
    }
    BoundingSphere b = FrustumBoundingSphere(rot);
    REQUIRE(b.radius == Catch::Approx(a.radius).epsilon(1e-4));
}

TEST_CASE("ShadowMath cascade: every camera frustum corner lands in the light NDC cube", "[Graphics][ShadowMath]")
{
    Mat4 invVP = CameraInvVP({5.0f, 3.0f, 5.0f}, {0.0f, 0.0f, -20.0f}, {0.0f, 1.0f, 0.0f});
    Vec3 light_disc = Normalize({0.4f, -1.0f, 0.3f});
    Mat4 lightVP = ComputeCascadeLightVP(light_disc, {0.0f, 1.0f, 0.0f}, invVP, 0.0f, 1.0f, 2048, 30.0f);

    for (const Vec3& c : FrustumCornersWS(invVP))
    {
        Vec3 q = TransformPoint(lightVP, c);
        REQUIRE(q.x >= -1.0001f);
        REQUIRE(q.x <= 1.0001f);
        REQUIRE(q.y >= -1.0001f);
        REQUIRE(q.y <= 1.0001f);
        REQUIRE(q.z >= -1.0001f);
        REQUIRE(q.z <= 1.0001f);
    }
}

TEST_CASE("ShadowMath cascade: a sub-texel camera shift leaves the matrix unchanged (no crawl)",
          "[Graphics][ShadowMath]")
{
    const int res = 1024;
    Vec3 light_disc = Normalize({0.0f, -1.0f, 0.2f});
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 eye{0.0f, 3.0f, 0.0f};
    Vec3 ctr{0.0f, 0.0f, -25.0f};

    Mat4 invVP0 = CameraInvVP(eye, ctr, up);
    Mat4 vp0 = ComputeCascadeLightVP(light_disc, up, invVP0, 0.0f, 1.0f, res, 30.0f);

    // Shift along the light's aside axis (perpendicular to the sun) by a quarter
    // texel, so the depth range is unchanged and only an in-grid xy nudge remains.
    BoundingSphere bs = FrustumBoundingSphere(FrustumCornersWS(invVP0));
    float texel = (2.0f * bs.radius) / static_cast<float>(res);
    Vec3 d = Normalize(Cross(light_disc, up)) * (0.25f * texel);

    Mat4 vp1 = ComputeCascadeLightVP(light_disc, up, CameraInvVP(eye + d, ctr + d, up), 0.0f, 1.0f, res, 30.0f);
    for (int i = 0; i < 16; i++)
    {
        REQUIRE(vp0.m[i] == Catch::Approx(vp1.m[i]).margin(1e-5f));
    }
}

TEST_CASE("ShadowMath cascade: turning the camera keeps the texel density constant", "[Graphics][ShadowMath]")
{
    const int res = 1024;
    Vec3 light_disc = Normalize({0.3f, -1.0f, 0.2f});
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 eye{0.0f, 3.0f, 0.0f};

    Mat4 vpA =
        ComputeCascadeLightVP(light_disc, up, CameraInvVP(eye, {0.0f, 0.0f, -25.0f}, up), 0.0f, 1.0f, res, 30.0f);
    Vec3 ctrB = YawAbout({0.0f, 0.0f, -25.0f}, eye, 0.6f);
    Mat4 vpB = ComputeCascadeLightVP(light_disc, up, CameraInvVP(eye, ctrB, up), 0.0f, 1.0f, res, 30.0f);

    // The ortho xy scale (lightVP(0,0) and (1,1)) must match: same sphere radius →
    // same extent → same texel density. A tight-AABB fit would fail this; the
    // bounding-sphere fit is exactly why it holds.
    REQUIRE(vpA.m[0] == Catch::Approx(vpB.m[0]).epsilon(1e-4));
    REQUIRE(vpA.m[5] == Catch::Approx(vpB.m[5]).epsilon(1e-4));
}

// ── FP cascaded-shadow kernel (the world-space port pieces) ───────────────────

TEST_CASE("ShadowMath FP: world frustum corners from camera basis", "[Graphics][ShadowMath]")
{
    // Camera at origin, looking down +Z, 45deg-ish (tan=1), near 1, far 10.
    auto c = CameraFrustumCornersWorld({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 1.0f, 1.0f, 1.0f, 10.0f);
    REQUIRE(ApproxV(c[0], {-1, -1, 1}));    // near, (-x,-y)
    REQUIRE(ApproxV(c[2], {1, 1, 1}));      // near, (+x,+y)
    REQUIRE(ApproxV(c[4], {-10, -10, 10})); // far, (-x,-y)
    REQUIRE(ApproxV(c[6], {10, 10, 10}));   // far, (+x,+y)

    // Translating the camera shifts every corner by the same offset.
    Vec3 camPos{100.0f, 5.0f, -200.0f};
    auto c2 = CameraFrustumCornersWorld(camPos, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 1.0f, 1.0f, 1.0f, 10.0f);
    for (int i = 0; i < 8; i++)
    {
        REQUIRE(ApproxV(c2[i], c[i] + camPos));
    }
}

TEST_CASE("ShadowMath FP: world-space cascade fit contains its slice", "[Graphics][ShadowMath]")
{
    // A frustum far out in the world (big coords) — the camera-relative attempt
    // would lose precision here; the world-space fit must still bound the slice.
    Vec3 camPos{1200.0f, 40.0f, -3400.0f};
    auto corners = CameraFrustumCornersWorld(camPos, Normalize({0.2f, -0.1f, 1.0f}), {1, 0, 0}, {0, 1, 0}, 0.6f, 0.45f,
                                             0.5f, 120.0f);
    Vec3 light_disc = Normalize({0.4f, -1.0f, 0.25f});
    Vec3 up{0.0f, 1.0f, 0.0f};
    Mat4 vp = CascadeLightVPWorld(corners, 0.0f, 0.30f, light_disc, up, 2048, 50.0f);

    // Every corner of the [0,0.30] slice must land inside the light NDC cube.
    auto slice = SliceFrustum(corners, 0.0f, 0.30f);
    for (const Vec3& w : slice)
    {
        Vec3 ndc = TransformPoint(vp, w);
        REQUIRE(ndc.x >= -1.0001f);
        REQUIRE(ndc.x <= 1.0001f);
        REQUIRE(ndc.y >= -1.0001f);
        REQUIRE(ndc.y <= 1.0001f);
        REQUIRE(ndc.z >= -0.0001f);
        REQUIRE(ndc.z <= 1.0001f);
    }
}

TEST_CASE("ShadowMath FP: camera-relative bake matches world transform", "[Graphics][ShadowMath]")
{
    // THE reconciliation that fixes the jumping: a world-space lightVP, baked to
    // consume camera-relative positions, must give the identical clip coords.
    Vec3 camPos{800.0f, 25.0f, -1500.0f};
    auto corners = CameraFrustumCornersWorld(camPos, Normalize({0.1f, -0.2f, 1.0f}), {1, 0, 0}, {0, 1, 0}, 0.7f, 0.5f,
                                             0.5f, 90.0f);
    Mat4 worldVP = CascadeLightVPWorld(corners, 0.0f, 0.5f, Normalize({0.3f, -1.0f, 0.1f}), {0, 1, 0}, 1024, 40.0f);
    Mat4 camRelVP = ToCameraRelative(worldVP, camPos);

    for (Vec3 world : {Vec3{802.0f, 26.0f, -1498.0f}, Vec3{780.0f, 24.0f, -1540.0f}, camPos})
    {
        Vec3 camRel = world - camPos; // how the engine stores positions
        Vec3 viaWorld = TransformPoint(worldVP, world);
        Vec3 viaBake = TransformPoint(camRelVP, camRel);
        REQUIRE(ApproxV(viaWorld, viaBake, 1e-3f));
    }
}

TEST_CASE("ShadowMath FP: eye-depth cascade selection", "[Graphics][ShadowMath]")
{
    const float splits[4] = {10.0f, 30.0f, 80.0f, 150.0f};
    REQUIRE(SelectCascade(5.0f, splits, 4) == 0);
    REQUIRE(SelectCascade(10.0f, splits, 4) == 1); // boundary rolls to the next
    REQUIRE(SelectCascade(25.0f, splits, 4) == 1);
    REQUIRE(SelectCascade(30.0f, splits, 4) == 2);
    REQUIRE(SelectCascade(120.0f, splits, 4) == 3);
    REQUIRE(SelectCascade(150.0f, splits, 4) == 4); // beyond range
    REQUIRE(SelectCascade(400.0f, splits, 4) == 4);
}

TEST_CASE("ShadowMath FP: far-edge fade ramps over the last band", "[Graphics][ShadowMath]")
{
    REQUIRE(ShadowFarFade(90.0f, 150.0f, 50.0f) == Catch::Approx(1.0f));  // well inside
    REQUIRE(ShadowFarFade(100.0f, 150.0f, 50.0f) == Catch::Approx(1.0f)); // at fade start
    REQUIRE(ShadowFarFade(125.0f, 150.0f, 50.0f) == Catch::Approx(0.5f)); // mid ramp
    REQUIRE(ShadowFarFade(150.0f, 150.0f, 50.0f) == Catch::Approx(0.0f)); // at edge
    REQUIRE(ShadowFarFade(170.0f, 150.0f, 50.0f) == Catch::Approx(0.0f)); // past edge
}

TEST_CASE("ShadowMath FP: BuildShadowCascades composes a usable cascade set", "[Graphics][ShadowMath]")
{
    CascadeBuildParams p;
    p.camPos = {1500.0f, 30.0f, -2600.0f}; // far from origin: exercises precision
    p.forward = Normalize({0.15f, -0.12f, 1.0f});
    p.right = Normalize(Cross(p.forward, {0.0f, 1.0f, 0.0f}));
    p.up = Normalize(Cross(p.right, p.forward));
    p.tanHalfX = 0.62f;
    p.tanHalfY = 0.46f;
    p.nearD = 0.5f;
    p.farD = 900.0f;
    p.sunDir = Normalize({0.35f, -1.0f, 0.2f});
    p.count = 4;
    p.distanceCoef = 0.12f;
    p.splitCoef = 0.90f;
    p.resolution = 2048;
    p.zPad = 50.0f;

    CascadeSet cs = BuildShadowCascades(p);

    REQUIRE(cs.count == 4);
    // split distances strictly ascending, last == shadowFar = near + coef*(far-near).
    const float shadowFar = p.nearD + p.distanceCoef * (p.farD - p.nearD);
    for (int i = 1; i < cs.count; i++)
    {
        REQUIRE(cs.splitViewDist[i] > cs.splitViewDist[i - 1]);
    }
    REQUIRE(cs.splitViewDist[cs.count - 1] == Catch::Approx(shadowFar).epsilon(1e-3));

    // A caster on the view axis at the middle of cascade 0 must land inside that
    // cascade's depth map when projected through the CAMERA-RELATIVE matrix using
    // the engine's camera-relative position (world - camPos). This is W1 end-to-end.
    float midDist = 0.5f * cs.splitViewDist[0];
    Vec3 casterWorld = p.camPos + p.forward * midDist;
    Vec3 casterCamRel = casterWorld - p.camPos;
    Vec3 ndc = TransformPoint(cs.camRelVP[0], casterCamRel);
    REQUIRE(ndc.x >= -1.0f);
    REQUIRE(ndc.x <= 1.0f);
    REQUIRE(ndc.y >= -1.0f);
    REQUIRE(ndc.y <= 1.0f);
    REQUIRE(ndc.z >= 0.0f);
    REQUIRE(ndc.z <= 1.0f);

    // count clamps.
    p.count = 9;
    REQUIRE(BuildShadowCascades(p).count == 4);
    p.count = 0;
    REQUIRE(BuildShadowCascades(p).count == 1);
}

TEST_CASE("ShadowMath FP: a sub-texel camera move leaves the WORLD cascade unchanged (no jump)",
          "[Graphics][ShadowMath]")
{
    CascadeBuildParams p;
    p.camPos = {5500.0f, 30.0f, -4700.0f}; // Everon-scale coords: where the float bake crawled
    p.forward = {0.0f, 0.0f, 1.0f};
    p.right = {1.0f, 0.0f, 0.0f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.tanHalfX = 0.60f;
    p.tanHalfY = 0.45f;
    p.nearD = 0.5f;
    p.farD = 900.0f;
    p.sunDir = Normalize({0.0f, -1.0f, 0.2f});
    p.count = 4;
    p.distanceCoef = 0.12f;
    p.splitCoef = 0.90f;
    p.resolution = 1024;
    p.zPad = 50.0f;

    CascadeSet a = BuildShadowCascades(p);

    // Cascade-0 texel size (the tightest grid) so the shift is safely sub-texel.
    const float shadowFar = p.nearD + p.distanceCoef * (p.farD - p.nearD);
    auto corners =
        CameraFrustumCornersWorld(p.camPos, p.forward, p.right, p.up, p.tanHalfX, p.tanHalfY, p.nearD, shadowFar);
    std::vector<float> sp = CascadeSplits(p.nearD, shadowFar, p.count, p.splitCoef);
    float t1 = (sp[1] - p.nearD) / (shadowFar - p.nearD);
    BoundingSphere bs0 = FrustumBoundingSphere(SliceFrustum(corners, 0.0f, t1));
    float texel0 = (2.0f * bs0.radius) / static_cast<float>(p.resolution);

    // A FIXED world point inside cascade 0.
    const Vec3 cam0 = p.camPos;
    const Vec3 ground = cam0 + Normalize(p.forward) * (0.5f * a.splitViewDist[0]);

    // Move the camera a fraction of cascade-0's texel along the light aside axis
    // (perpendicular to the sun → an in-grid xy nudge).
    Vec3 aside = Normalize(Cross(Normalize(p.sunDir), Vec3{0.0f, 1.0f, 0.0f}));
    p.camPos = cam0 + aside * (0.2f * texel0);
    CascadeSet b = BuildShadowCascades(p);

    // The fixed world point keeps the SAME camera-relative shadow UV within ~1
    // texel under the sub-texel camera move: the double world-grid snap holds the
    // grid even at large (~thousands) world coordinates, so the shadow does not
    // crawl. The old float worldVP*Translate(camPos) bake lost precision here.
    const Vec3 uvA = TransformPoint(a.camRelVP[0], ground - cam0);
    const Vec3 uvB = TransformPoint(b.camRelVP[0], ground - p.camPos);
    const float oneTexelNdc = 2.0f / static_cast<float>(p.resolution);
    REQUIRE(std::fabs(uvA.x - uvB.x) < 1.5f * oneTexelNdc);
    REQUIRE(std::fabs(uvA.y - uvB.y) < 1.5f * oneTexelNdc);

    // The matrix genuinely differs (proves the camera actually moved).
    bool moved = false;
    for (int k = 0; k < 16; k++)
    {
        if (std::fabs(a.camRelVP[0].m[k] - b.camRelVP[0].m[k]) > 1e-7f)
        {
            moved = true;
        }
    }
    REQUIRE(moved);
}

TEST_CASE("ShadowMath FP: caster-mode classifier (skip / alpha-test / solid)", "[Graphics][ShadowMath]")
{
    // Arbitrary disjoint flag bits standing in for the model's Special flags.
    const int kNoShadow = 1 << 0, kHidden = 1 << 1; // skip
    const int kAlpha = 1 << 2, kTransp = 1 << 3;    // alpha-test
    const int kOnSurface = 1 << 4;                  // irrelevant
    const int skipMask = kNoShadow | kHidden;
    const int alphaMask = kAlpha | kTransp;

    REQUIRE(ClassifyShadowCaster(0, skipMask, alphaMask) == CasterMode::Solid);
    REQUIRE(ClassifyShadowCaster(kOnSurface, skipMask, alphaMask) == CasterMode::Solid);
    REQUIRE(ClassifyShadowCaster(kAlpha, skipMask, alphaMask) == CasterMode::AlphaTest);
    REQUIRE(ClassifyShadowCaster(kTransp | kOnSurface, skipMask, alphaMask) == CasterMode::AlphaTest);
    REQUIRE(ClassifyShadowCaster(kNoShadow, skipMask, alphaMask) == CasterMode::Skip);
    // Skip wins over alpha (a hidden foliage face still must not cast).
    REQUIRE(ClassifyShadowCaster(kHidden | kAlpha, skipMask, alphaMask) == CasterMode::Skip);
}

TEST_CASE("ShadowMath FP: cross-cascade blend weight ramps over the band", "[Graphics][ShadowMath]")
{
    // Cascade far = 50 m, blend band = 10 m → ramp over [40, 50].
    REQUIRE(CascadeBlendWeight(20.0f, 50.0f, 10.0f) == Catch::Approx(0.0f)); // well inside
    REQUIRE(CascadeBlendWeight(40.0f, 50.0f, 10.0f) == Catch::Approx(0.0f)); // band start
    REQUIRE(CascadeBlendWeight(45.0f, 50.0f, 10.0f) == Catch::Approx(0.5f)); // mid band
    REQUIRE(CascadeBlendWeight(50.0f, 50.0f, 10.0f) == Catch::Approx(1.0f)); // far edge
    REQUIRE(CascadeBlendWeight(60.0f, 50.0f, 10.0f) == Catch::Approx(1.0f)); // clamps
    REQUIRE(CascadeBlendWeight(45.0f, 50.0f, 0.0f) == Catch::Approx(0.0f));  // no band
}

namespace
{
// A camera-relative point is inside a cascade when its NDC lands in the unit
// shadow box: xy in (-1,1) (the shader's suv = xy*0.5+0.5 in (0,1)) and z in (0,1).
bool InCascade(const Mat4& camRelVP, const Vec3& camRelPoint)
{
    const Vec3 n = TransformPoint(camRelVP, camRelPoint);
    return n.x > -1.0f && n.x < 1.0f && n.y > -1.0f && n.y < 1.0f && n.z > 0.0f && n.z < 1.0f;
}
} // namespace

TEST_CASE("ShadowMath tiered: omni sphere covers every direction around the camera", "[Graphics][ShadowMath]")
{
    // The omni tier exists so a caster BEHIND or BESIDE the camera (outside the view
    // frustum) still casts a shadow that falls into view. A frustum fit drops those;
    // a camera-centred sphere keeps them. Everon-scale camPos to exercise the snap.
    const Vec3 camPos{5500.0f, 30.0f, -4700.0f};
    const Vec3 light_disc = Normalize(Vec3{0.4f, -0.8f, 0.3f});
    const float R = 100.0f;
    const Mat4 vp = OmniSphereLightVP(camPos, R, light_disc, {0.0f, 1.0f, 0.0f}, 2048, 50.0f);

    // Any point within the radius — in ALL six axis directions, including straight
    // up, straight down, and behind — projects inside the cascade. (A frustum fit
    // would fail for the points behind/below the camera.)
    const float h = 0.5f * R;
    REQUIRE(InCascade(vp, {+h, 0.0f, 0.0f}));
    REQUIRE(InCascade(vp, {-h, 0.0f, 0.0f}));
    REQUIRE(InCascade(vp, {0.0f, +h, 0.0f})); // above
    REQUIRE(InCascade(vp, {0.0f, -h, 0.0f})); // below
    REQUIRE(InCascade(vp, {0.0f, 0.0f, +h})); // behind (−forward) / in front — both
    REQUIRE(InCascade(vp, {0.0f, 0.0f, -h}));
    REQUIRE(InCascade(vp, {0.0f, 0.0f, 0.0f})); // the camera itself

    // Well outside the radius (5R along a horizontal axis) is NOT covered: the omni
    // tier is a bounded near sphere, not the whole world.
    REQUIRE_FALSE(InCascade(vp, {5.0f * R, 0.0f, 0.0f}));
}

TEST_CASE("ShadowMath tiered: 2 omni + 2 frustum layout (radii, splits, behind-cover)", "[Graphics][ShadowMath]")
{
    CascadeBuildParams p;
    p.camPos = {5500.0f, 30.0f, -4700.0f};
    p.forward = {0.0f, 0.0f, -1.0f};
    p.right = {1.0f, 0.0f, 0.0f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.tanHalfX = 0.6f;
    p.tanHalfY = 0.45f;
    p.nearD = 0.1f;
    p.farD = 300.0f; // kept small so the omni texel cap doesn't clamp the fractions
    p.sunDir = Normalize(Vec3{0.4f, -0.8f, 0.3f});
    p.count = 4;
    p.distanceCoef = 1.0f; // full visibility distance
    p.splitCoef = 0.9f;
    p.resolution = 2048;
    p.omniCount = 2;
    p.omniCoef[0] = 0.08f;
    p.omniCoef[1] = 0.20f;

    const CascadeSet cs = BuildShadowCascadesTiered(p);
    const float shadowFar = p.nearD + p.distanceCoef * (p.farD - p.nearD); // = 300

    REQUIRE(cs.count == 4);
    REQUIRE(cs.omniCount == 2);

    // Omni radii are the coefficients times the shadow range (below the cap here),
    // ascending. (24 m and 60 m at shadowFar 300.)
    REQUIRE(cs.omniRadius[0] == Catch::Approx(0.08f * shadowFar).margin(0.5f));
    REQUIRE(cs.omniRadius[1] == Catch::Approx(0.20f * shadowFar).margin(0.5f));
    REQUIRE(cs.omniRadius[0] < cs.omniRadius[1]);

    // Frustum tiers reach the full range, ascending; the last == the shadow range.
    REQUIRE(cs.splitViewDist[2] < cs.splitViewDist[3]);
    REQUIRE(cs.splitViewDist[3] == Catch::Approx(shadowFar).margin(1.0f));
    REQUIRE(cs.splitViewDist[2] > cs.omniRadius[1]); // frustum starts past the omni tiers

    // The crispest omni tier covers a caster BEHIND and BESIDE the camera within its
    // radius (forward is −Z, so +Z is behind): the whole point of the omni tier.
    const float d = 0.5f * cs.omniRadius[0];
    REQUIRE(InCascade(cs.camRelVP[0], {d, 0.0f, d})); // beside + behind, |P| < r0
}

TEST_CASE("ShadowMath tiered: omni radii are capped so near tiers stay crisp", "[Graphics][ShadowMath]")
{
    // With a large shadow distance, an uncapped omni sphere (coef * range) would be
    // huge → coarse texels → "toothy" shadows even up close. The cap keeps the texel
    // size (2*radius/resolution) near a target so the near tiers stay sharp.
    CascadeBuildParams p;
    p.camPos = {5500.0f, 30.0f, -4700.0f};
    p.forward = {0.0f, 0.0f, -1.0f};
    p.right = {1.0f, 0.0f, 0.0f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.tanHalfX = 0.6f;
    p.tanHalfY = 0.45f;
    p.nearD = 0.1f;
    p.farD = 3000.0f; // large shadow distance (the case that went toothy)
    p.sunDir = Normalize(Vec3{0.4f, -0.8f, 0.3f});
    p.count = 4;
    p.distanceCoef = 1.0f;
    p.splitCoef = 0.9f;
    p.resolution = 2048;
    p.omniCount = 2;
    p.omniCoef[0] = 0.08f;
    p.omniCoef[1] = 0.20f;

    const CascadeSet cs = BuildShadowCascadesTiered(p);

    // Uncapped these would be 0.08*3000=240 m and 0.20*3000=600 m — far too coarse.
    // The cap holds them small (≈30 m / ≈61 m at 2048), so the texel stays crisp.
    REQUIRE(cs.omniRadius[0] < 0.08f * 3000.0f);        // genuinely clamped
    REQUIRE(cs.omniRadius[1] < 0.20f * 3000.0f);        // genuinely clamped
    REQUIRE(2.0f * cs.omniRadius[0] / 2048.0f < 0.04f); // tightest tier < 4 cm/texel
    REQUIRE(2.0f * cs.omniRadius[1] / 2048.0f < 0.07f); // looser tier < 7 cm/texel
    REQUIRE(cs.omniRadius[0] < cs.omniRadius[1]);
    // A point within the (capped) radius is still covered — coverage didn't break.
    REQUIRE(InCascade(cs.camRelVP[0], {0.0f, 0.0f, 0.5f * cs.omniRadius[0]}));
}

TEST_CASE("ShadowMath tiered: omniCount 0 is identical to BuildShadowCascades", "[Graphics][ShadowMath]")
{
    CascadeBuildParams p;
    p.camPos = {1200.0f, 25.0f, -800.0f};
    p.forward = {0.0f, -0.1f, -0.99f};
    p.right = {1.0f, 0.0f, 0.0f};
    p.up = {0.0f, 0.99f, -0.1f};
    p.tanHalfX = 0.7f;
    p.tanHalfY = 0.5f;
    p.farD = 900.0f;
    p.sunDir = Normalize(Vec3{0.3f, -0.85f, 0.4f});
    p.count = 4;
    p.distanceCoef = 0.12f;
    p.omniCount = 0; // pure frustum

    const CascadeSet tiered = BuildShadowCascadesTiered(p);
    const CascadeSet base = BuildShadowCascades(p);
    REQUIRE(tiered.count == base.count);
    REQUIRE(tiered.omniCount == 0);
    for (int c = 0; c < base.count; c++)
    {
        REQUIRE(tiered.splitViewDist[c] == Catch::Approx(base.splitViewDist[c]));
        for (int k = 0; k < 16; k++)
        {
            REQUIRE(tiered.camRelVP[c].m[k] == Catch::Approx(base.camRelVP[c].m[k]).margin(1e-4f));
        }
    }
}

TEST_CASE("ShadowMath tiered: SelectShadowTier priority (omni by dist, frustum by eye)", "[Graphics][ShadowMath]")
{
    // 2 omni (radii 72,180) + 2 frustum (far view dists 450,900). splitViewDist[0,1]
    // mirror the omni radii (unused by selection) as the build leaves them.
    const float omniRadius[4] = {72.0f, 180.0f, 0.0f, 0.0f};
    const float splitViewDist[4] = {72.0f, 180.0f, 450.0f, 900.0f};
    const int omniCount = 2, count = 4;

    // Near, in any direction → tightest omni tier (selected by 3D distance).
    REQUIRE(SelectShadowTier(50.0f, 40.0f, omniRadius, omniCount, splitViewDist, count) == 0);
    // Past the first sphere but inside the second → omni tier 1.
    REQUIRE(SelectShadowTier(120.0f, 100.0f, omniRadius, omniCount, splitViewDist, count) == 1);
    // Past both spheres → frustum tiers, picked by eye-depth.
    REQUIRE(SelectShadowTier(300.0f, 200.0f, omniRadius, omniCount, splitViewDist, count) == 2);
    REQUIRE(SelectShadowTier(800.0f, 600.0f, omniRadius, omniCount, splitViewDist, count) == 3);
    // Beyond the shadow range → no tier.
    REQUIRE(SelectShadowTier(800.0f, 950.0f, omniRadius, omniCount, splitViewDist, count) == count);
    // Just outside the omni spheres but small eye-depth (off to the side) → the near
    // frustum tier still catches it (the shader then bounds-fallthrough-refines).
    REQUIRE(SelectShadowTier(200.0f, 30.0f, omniRadius, omniCount, splitViewDist, count) == 2);
}
