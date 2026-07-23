#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

TEST_CASE("Shape: LODShape default construction", "[Shape]")
{
    LODShape shape;
    REQUIRE(shape.NLevels() == 0);
}

TEST_CASE("clipShape.hpp compiles", "[Shape]")
{
    REQUIRE(true);
}

TEST_CASE("LODShape: FindSpecLevel skips dropped null LOD slots", "[Shape][LOD]")
{
    LODShape lod;
    lod.AddShape(new Shape(), 0.0f);
    lod.AddShape(new Shape(), VIEW_PILOT);

    REQUIRE(lod.FindSpecLevel(VIEW_PILOT) == 1);

    lod.ChangeShape(1, nullptr);

    REQUIRE(lod.FindSpecLevel(VIEW_PILOT) == -1);
    REQUIRE(lod.FindLevel(10.0f) == 0);
}

TEST_CASE("NamedSelection skips invalid face indices when building face offsets", "[Shape][Selection]")
{
    Shape shape;
    Poly face;
    face.Init();
    face.SetN(0);
    shape.AddFace(face);

    VertexIndex selectedFaces[] = {0, 99};
    Poseidon::NamedSelection selection("bad_faces", nullptr, 0, selectedFaces, 2);

    const Poseidon::FaceSelection& offsets = selection.FaceOffsets(&shape);

    REQUIRE(offsets.Size() == 1);
    REQUIRE(offsets[0] == shape.BeginFaces());
}

TEST_CASE("NamedSelection array compaction preserves copied selections", "[Shape][Selection]")
{
    Poseidon::SelInfo selectedPoints[] = {Poseidon::SelInfo(0, 255)};
    VertexIndex selectedFaces[] = {0};

    Poseidon::Foundation::AutoArray<Poseidon::NamedSelection> selections;
    selections.Add(Poseidon::NamedSelection("door", selectedPoints, 1, selectedFaces, 1));
    selections.Add(Poseidon::NamedSelection("window", selectedPoints, 1, selectedFaces, 1));

    selections.Compact();

    REQUIRE(selections.Size() == 2);
    REQUIRE(std::strcmp(selections[0].Name(), "door") == 0);
    REQUIRE(std::strcmp(selections[1].Name(), "window") == 0);
    REQUIRE(selections[0].Faces().Size() == 1);
    REQUIRE(selections[1].Faces().Size() == 1);
}
