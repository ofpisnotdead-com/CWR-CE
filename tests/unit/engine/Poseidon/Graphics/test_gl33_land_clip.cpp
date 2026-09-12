#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <PoseidonGL33/EngineGL33.hpp>

#include <vector>

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

namespace
{
class GlobalEngineScope
{
  public:
    explicit GlobalEngineScope(Engine* engine) : _previous(GEngine) { GEngine = engine; }
    ~GlobalEngineScope() { GEngine = _previous; }

  private:
    Engine* _previous;
};

// Records what Object::UpdateLandClipParams hands the renderer.
class RecordingEngine : public EngineDummy
{
  public:
    bool LandClipInVS() const override { return _landClipInVS; }

    void SetLandClipParams(float mode, Vector3Par /*boundingCenter*/) override { _modes.push_back(mode); }

    void DisableLandClipInVS() { _landClipInVS = false; }
    const std::vector<float>& Modes() const { return _modes; }

  private:
    bool _landClipInVS = true;
    std::vector<float> _modes;
};

Ref<LODShapeWithShadow> MakeShape(ClipFlags orHints)
{
    Ref<LODShapeWithShadow> lod = new LODShapeWithShadow();
    Shape* level = new Shape();
    level->SetHints(orHints, ClipNone);
    lod->AddShape(level, 0.0f);
    return lod;
}
} // namespace

TEST_CASE("Objects publish their own land clip mode to the renderer", "[Graphics][GL33][LandClip]")
{
    RecordingEngine engine;
    GlobalEngineScope engineScope(&engine);

    Ref<ObjectPlain> rigid = new ObjectPlain(MakeShape(ClipNone), 1);
    rigid->UpdateLandClipParams(0);
    REQUIRE(engine.Modes().size() == 1);
    CHECK(engine.Modes().back() == float(Object::LandClipNone));

    Ref<ObjectPlain> landClipped = new ObjectPlain(MakeShape(ClipLandKeep), 2);
    landClipped->UpdateLandClipParams(0);
    REQUIRE(engine.Modes().size() == 2);
    CHECK(engine.Modes().back() == float(Object::LandClipVertex));

    Ref<ForestPlain> forest = new ForestPlain(MakeShape(ClipNone), 3);
    forest->UpdateLandClipParams(0);
    REQUIRE(engine.Modes().size() == 3);
    CHECK(engine.Modes().back() == float(Object::LandClipPlane));
}

TEST_CASE("Land clip publishing stays off the renderer when it is not GPU side", "[Graphics][GL33][LandClip]")
{
    RecordingEngine engine;
    engine.DisableLandClipInVS();
    GlobalEngineScope engineScope(&engine);

    Ref<ForestPlain> forest = new ForestPlain(MakeShape(ClipNone), 1);
    forest->UpdateLandClipParams(0);
    CHECK(engine.Modes().empty());
}
