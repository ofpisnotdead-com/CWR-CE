#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>

using namespace Poseidon;

namespace
{
class GlobalLandscapeScope
{
  public:
    explicit GlobalLandscapeScope(Landscape* landscape) : _previous(GLandscape) { GLandscape = landscape; }
    ~GlobalLandscapeScope() { GLandscape = _previous; }

  private:
    Landscape* _previous;
};

class CountingObject : public ObjectPlain
{
  public:
    CountingObject(LODShapeWithShadow* shape, int id) : ObjectPlain(shape, id) {}

    void Animate(int level) override
    {
        ++_animateCalls;
        ObjectPlain::Animate(level);
    }

    int AnimateCalls() const { return _animateCalls; }

    void MarkDestroyed()
    {
        _isDestroyed = true;
        _destroyPhase = 255;
    }

  private:
    int _animateCalls = 0;
};

void SetBounds(Shape& shape, Vector3Val min, Vector3Val max)
{
    shape.SetMinMax(min, max, (min + max) * 0.5f, (max - min).Size() * 0.5f);
    shape.StoreOriginalMinMax();
}
} // namespace

TEST_CASE("Object caches undamaged land-clipped bounds", "[World][Object][Bounds]")
{
    Landscape landscape(nullptr, nullptr);
    GlobalLandscapeScope globalLandscape(&landscape);

    Ref<LODShapeWithShadow> lod = new LODShapeWithShadow();
    Shape* level = new Shape();
    level->SetHints(ClipLandOn, ClipLandOn);
    SetBounds(*level, Vector3(1, 2, 3), Vector3(4, 5, 6));
    lod->AddShape(level, 0.0f);

    Ref<CountingObject> object = new CountingObject(lod, 1);
    Vector3 bounds[2];
    object->AnimatedMinMax(0, bounds);
    REQUIRE(bounds[0] == Vector3(1, 2, 3));
    REQUIRE(bounds[1] == Vector3(4, 5, 6));
    REQUIRE(object->AnimateCalls() == 1);

    object->AnimatedMinMax(0, bounds);
    REQUIRE(object->AnimateCalls() == 1);

    object->SetDammage(0.5f);
    object->AnimatedMinMax(0, bounds);
    REQUIRE(object->AnimateCalls() == 2);
}

TEST_CASE("Destroyed objects report shared shape deformation", "[World][Object][Instancing]")
{
    Ref<LODShapeWithShadow> lod = new LODShapeWithShadow();
    Shape* level = new Shape();
    lod->AddShape(level, 0.0f);

    Ref<CountingObject> object = new CountingObject(lod, 1);
    CHECK_FALSE(object->DeformsSharedShape(0));

    Poly face;
    face.Init();
    face.SetN(0);
    level->AddFace(face);
    CHECK_FALSE(object->DeformsSharedShape(0));

    object->SetDestructType(DestructBuilding);
    object->MarkDestroyed();
    CHECK(object->DeformsSharedShape(0));

    object->SetDestructType(DestructTree);
    CHECK_FALSE(object->DeformsSharedShape(0));
}
