#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <limits>

using namespace Poseidon;

namespace
{
class CollisionShape : public LODShapeWithShadow
{
  public:
    CollisionShape()
    {
        Shape* geometry = new Shape();
        geometry->Init(4);
        geometry->SetPos(0) = Vector3(-1, -1, -1);
        geometry->SetPos(1) = Vector3(1, -1, -1);
        geometry->SetPos(2) = Vector3(0, 1, -1);
        geometry->SetPos(3) = Vector3(0, 0, 1);
        const VertexIndex faces[][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
        for (const auto& vertices : faces)
        {
            Poly face;
            face.Init();
            face.SetN(3);
            for (int i = 0; i < 3; ++i)
                face.Set(i, vertices[i]);
            geometry->AddFace(face);
        }
        SelInfo points[] = {{0, 255}, {1, 255}, {2, 255}, {3, 255}};
        VertexIndex faceIndices[] = {0, 1, 2, 3};
        geometry->AddNamedSel(NamedSelection("Component01", points, 4, faceIndices, 4));
        geometry->SetMinMax(Vector3(-1, -1, -1), Vector3(1, 1, 1), VZero, 2);
        geometry->StoreOriginalMinMax();
        AddShape(geometry, GEOMETRY_SPEC);
        _boundingSphere = 2;
        _geometrySphere = 2;
        _geomComponents = new ConvexComponents();
        InitConvexComponents(*_geomComponents, geometry);
    }
};
} // namespace

TEST_CASE("Object line intersections reject nonfinite transforms", "[World][Collision][Nonfinite]")
{
    Ref<CollisionShape> shape = new CollisionShape();
    REQUIRE(shape->FindFireGeometryLevel() >= 0);
    REQUIRE(shape->GetConvexComponents(shape->FindFireGeometryLevel())->Size() == 1);
    REQUIRE(shape->GetConvexComponents(shape->FindFireGeometryLevel())->Get(0)->NPlanes() == 4);
    Ref<ObjectPlain> object = new ObjectPlain(shape, 1);
    CollisionBuffer collisions;
    const Vector3 begin(0, 0, -2);
    const Vector3 end(0, 0, 2);
    object->Intersect(collisions, begin, end, 0, ObjIntersectFire);
    REQUIRE(collisions.Size() == 1);
    REQUIRE(collisions[0].pos.IsFinite());
    collisions.Clear();

    const float invalid = GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity());
    SECTION("position")
    {
        object->SetPosition(Vector3(invalid, invalid, invalid));
    }
    SECTION("scale")
    {
        object->SetScale(invalid);
    }
    REQUIRE_FALSE(object->Transform().IsFinite());
    object->Intersect(collisions, begin, end, 0, ObjIntersectFire);
    CHECK(collisions.Size() == 0);
}

TEST_CASE("Object line intersections preserve finite nonzero scales", "[World][Collision][Scale]")
{
    Ref<CollisionShape> shape = new CollisionShape();
    Ref<ObjectPlain> object = new ObjectPlain(shape, 1);
    const float scale = GENERATE(1.0f, -1.0f, 0.0f);
    object->SetScale(scale);
    REQUIRE(object->Transform().IsFinite());
    REQUIRE(object->Scale() == scale);
    CollisionBuffer collisions;
    object->Intersect(collisions, Vector3(0, 0, -2), Vector3(0, 0, 2), 0, ObjIntersectFire);
    CHECK(collisions.Size() == (scale == 0 ? 0 : 1));
    for (int i = 0; i < collisions.Size(); ++i)
        CHECK(collisions[i].pos.IsFinite());
}

TEST_CASE("Landscape visibility rejects nonfinite inputs", "[World][Collision][Nonfinite]")
{
    Landscape landscape(nullptr, nullptr);
    const Vector3 begin(100, 10, 100);
    const Vector3 end(110, 10, 100);
    REQUIRE(landscape.Visible(begin, end, 1, nullptr, nullptr, ObjIntersectIFire) == 1);

    const float invalid = GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity());
    SECTION("origin")
    {
        CHECK(landscape.Visible(Vector3(invalid, invalid, invalid), end, 1, nullptr, nullptr, ObjIntersectIFire) == 0);
    }
    SECTION("target")
    {
        CHECK(landscape.Visible(begin, Vector3(invalid, invalid, invalid), 1, nullptr, nullptr, ObjIntersectIFire) ==
              0);
    }
    SECTION("radius")
    {
        CHECK(landscape.Visible(begin, end, invalid, nullptr, nullptr, ObjIntersectIFire) == 0);
    }
}

TEST_CASE("Landscape surface outside the map has zero slopes", "[World][Collision][Terrain]")
{
    Landscape landscape(nullptr, nullptr);
    const float coordinate = GENERATE(-0.001f, -1000000.0f);
    const float previous = GENERATE(42.0f, std::numeric_limits<float>::quiet_NaN());
    float dx = previous;
    float dz = previous;
    REQUIRE(landscape.SurfaceY(coordinate, coordinate) == YOutsideMap);
    CHECK(landscape.SurfaceY(coordinate, coordinate, &dx, &dz) == YOutsideMap);
    CHECK(dx == 0);
    CHECK(dz == 0);
}
