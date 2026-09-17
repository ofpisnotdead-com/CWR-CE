#include <catch2/catch_test_macros.hpp>

#include <PoseidonGL33/GL33PipelineChanges.hpp>
#include <PoseidonGL33/GL33UploadSnapshot.hpp>

#include <array>

namespace
{
void CheckChanges(const Poseidon::GL33Pipeline::Changes& changes, bool depth, bool blend, bool polygonOffset, bool cull,
                  bool frontFace)
{
    CHECK(changes.depth == depth);
    CHECK(changes.blend == blend);
    CHECK(changes.polygonOffset == polygonOffset);
    CHECK(changes.cull == cull);
    CHECK(changes.frontFace == frontFace);
}
} // namespace

TEST_CASE("GL33 upload snapshot matches only valid buffer contents", "[Graphics][GL33][StateCache]")
{
    Poseidon::GL33UploadSnapshot<4> snapshot;
    std::array<unsigned char, 4> data{0, 0, 0, 0};

    REQUIRE_FALSE(snapshot.Matches(7, data.data()));

    snapshot.Record(7, data.data());
    REQUIRE(snapshot.Matches(7, data.data()));

    data[2] = 1;
    REQUIRE_FALSE(snapshot.Matches(7, data.data()));

    snapshot.Record(7, data.data());
    REQUIRE_FALSE(snapshot.Matches(9, data.data()));

    snapshot.Invalidate();
    REQUIRE_FALSE(snapshot.Matches(7, data.data()));
}

TEST_CASE("GL33 pipeline invalidation reapplies every incrementally cached state", "[Graphics][GL33][StateCache]")
{
    const Poseidon::render::RenderPassDescriptor descriptor;

    CheckChanges(Poseidon::GL33Pipeline::Compare(descriptor, descriptor, false), true, true, true, true, true);
    CheckChanges(Poseidon::GL33Pipeline::Compare(descriptor, descriptor, true), false, false, false, false, false);
}

TEST_CASE("GL33 pipeline comparisons isolate descriptor changes", "[Graphics][GL33][StateCache]")
{
    const Poseidon::render::RenderPassDescriptor previous;
    auto next = previous;

    next.depth = Poseidon::render::DepthMode::ReadOnly;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), true, false, false, false, false);

    next = previous;
    next.blend = Poseidon::render::BlendMode::Additive;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), false, true, false, false, false);

    next = previous;
    next.surface = Poseidon::render::SurfaceMode::OnSurface;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), false, false, true, false, false);

    next = previous;
    next.shader = Poseidon::render::ShaderFamily::Shadow;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), false, false, true, false, false);

    next = previous;
    next.cull = Poseidon::render::CullMode::Front;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), false, false, false, true, false);

    next = previous;
    next.frontFace = Poseidon::render::FrontFaceMode::CCW;
    CheckChanges(Poseidon::GL33Pipeline::Compare(previous, next, true), false, false, false, false, true);
}
