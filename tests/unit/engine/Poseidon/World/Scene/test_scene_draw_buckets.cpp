#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/SceneDrawBuckets.hpp>

namespace
{

Ref<Poseidon::SortObject> MakeSortObject(Poseidon::Object* object, int pass, int lod)
{
    Ref<Poseidon::SortObject> item = new Poseidon::SortObject();
    item->object = object;
    item->passNum = static_cast<signed char>(pass);
    item->drawLOD = static_cast<signed char>(lod);
    return item;
}

} // namespace

TEST_CASE("Scene draw buckets group shape, pass, and LOD", "[scene][draw][instancing]")
{
    Ref<LODShapeWithShadow> shapeA = new LODShapeWithShadow();
    Ref<LODShapeWithShadow> shapeB = new LODShapeWithShadow();
    Ref<Poseidon::ObjectPlain> objectA = new Poseidon::ObjectPlain(shapeA, 1);
    Ref<Poseidon::ObjectPlain> objectB = new Poseidon::ObjectPlain(shapeB, 2);

    Ref<Poseidon::SortObject> a0 = MakeSortObject(objectA, 0, 1);
    Ref<Poseidon::SortObject> b0 = MakeSortObject(objectB, 0, 1);
    Ref<Poseidon::SortObject> a1 = MakeSortObject(objectA, 1, 1);
    Ref<Poseidon::SortObject> a2 = MakeSortObject(objectA, 0, 1);
    Ref<Poseidon::SortObject> b1 = MakeSortObject(objectB, 0, 1);
    Ref<Poseidon::SortObject> aLod = MakeSortObject(objectA, 0, 2);

    Poseidon::SortObjectList objects;
    objects.Add(a0);
    objects.Add(b0);
    objects.Add(a1);
    objects.Add(a2);
    objects.Add(b1);
    objects.Add(aLod);

    Poseidon::SceneDraw::BucketDrawMergersByShape(objects);

    REQUIRE(objects.Size() == 6);
    CHECK(objects[0] == a0);
    CHECK(objects[1] == a2);
    CHECK(objects[2] == b0);
    CHECK(objects[3] == b1);
    CHECK(objects[4] == a1);
    CHECK(objects[5] == aLod);

    Poseidon::SortObjectList nextFrame;
    nextFrame.Add(b1);
    nextFrame.Add(a1);
    nextFrame.Add(a0);
    nextFrame.Add(b0);
    nextFrame.Add(a2);
    nextFrame.Add(aLod);

    Poseidon::SceneDraw::BucketDrawMergersByShape(nextFrame);

    REQUIRE(nextFrame.Size() == 6);
    CHECK(nextFrame[0] == b1);
    CHECK(nextFrame[1] == b0);
    CHECK(nextFrame[2] == a1);
    CHECK(nextFrame[3] == a0);
    CHECK(nextFrame[4] == a2);
    CHECK(nextFrame[5] == aLod);
}
