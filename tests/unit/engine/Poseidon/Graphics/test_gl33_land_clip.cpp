#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <PoseidonGL33/EngineGL33.hpp>

using namespace Poseidon;

TEST_CASE("GL33 classifies land-clip vertex modes", "[Graphics][GL33][LandClip]")
{
    CHECK(ClassifyLandClipVertex(ClipNone) == LandClipVertexMode::Rigid);
    CHECK(ClassifyLandClipVertex(ClipLandKeep) == LandClipVertexMode::Keep);
    CHECK(ClassifyLandClipVertex(ClipLandOn) == LandClipVertexMode::On);
}

TEST_CASE("Shape identifies deforming land clip", "[Graphics][GL33][LandClip]")
{
    Shape shape;

    shape.SetHints(ClipNone, ClipNone);
    CHECK_FALSE(shape.HasDeformingLandClip());

    shape.SetHints(ClipLandKeep, ClipNone);
    CHECK(shape.HasDeformingLandClip());

    shape.SetHints(ClipLandOn, ClipLandOn);
    CHECK_FALSE(shape.HasDeformingLandClip());
}

TEST_CASE("LODShape identifies land clip as its only animation", "[Graphics][GL33][LandClip]")
{
    LODShape shape;
    shape.SetHints(ClipLandKeep, ClipLandKeep);
    CHECK_FALSE(shape.IsLandClipOnlyAnim());

    shape.AllowAnimation();
    CHECK(shape.IsLandClipOnlyAnim());

    shape.SetHints(ClipLandKeep | ClipDecalNormal, ClipLandKeep | ClipDecalNormal);
    CHECK_FALSE(shape.IsLandClipOnlyAnim());

    shape.SetHints(ClipLandKeep | ClipLightSky, ClipLandKeep | ClipLightSky);
    CHECK_FALSE(shape.IsLandClipOnlyAnim());
}

TEST_CASE("Objects select their GPU land-clip mode", "[Graphics][GL33][LandClip]")
{
    Ref<LODShapeWithShadow> lod = new LODShapeWithShadow();
    Shape* level = new Shape();
    level->SetHints(ClipNone, ClipNone);
    lod->AddShape(level, 0.0f);

    Ref<ObjectPlain> object = new ObjectPlain(lod, 1);
    CHECK(object->GetLandClipMode(0) == Object::LandClipNone);

    level->SetHints(ClipLandKeep, ClipNone);
    CHECK(object->GetLandClipMode(0) == Object::LandClipVertex);

    Ref<ForestPlain> forest = new ForestPlain(lod, 2);
    CHECK(forest->GetLandClipMode(0) == Object::LandClipPlane);
}
