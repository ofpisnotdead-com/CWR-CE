#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <new>
#include <Poseidon/Graphics/Rendering/Primitives/Vertex.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Edges.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Clip2D.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/ClipVert.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>

using Poseidon::FaceEdges;
using Poseidon::PolyPlain;
using Poseidon::PolyVertices;
using Poseidon::UVPair;
using Poseidon::VertexTable;

TEST_CASE("VertexTable - construction and vertex management", "[rendering][vertex]")
{
    SECTION("default constructor")
    {
        VertexTable vt;
        REQUIRE(vt.NVertex() == 0);
        REQUIRE(vt.BSphereRadius() == 0.0f);
    }

    SECTION("construct with size")
    {
        VertexTable vt(10);
        REQUIRE(vt.NVertex() == 10);
    }

    SECTION("AddVertexFast")
    {
        VertexTable vt;
        vt.Init(0);
        int idx = vt.AddVertexFast(Vector3(1, 2, 3), Vector3(0, 1, 0), 0, 0.5f, 0.5f);
        REQUIRE(idx == 0);
        REQUIRE(vt.NVertex() == 1);
        REQUIRE(vt.U(0) == Catch::Approx(0.5f));
        REQUIRE(vt.V(0) == Catch::Approx(0.5f));
    }

    SECTION("copy constructor")
    {
        VertexTable vt(3);
        VertexTable vt2(vt);
        REQUIRE(vt2.NVertex() == 3);
    }
}

TEST_CASE("VertexTable - min/max cache state does not depend on prior memory", "[rendering][vertex]")
{
    // MinMaxDynamic either returns the cached box or rescans, decided by _minMaxDirty, so
    // constructing over two differently filled buffers must still answer identically.
    auto minMaxOver = [](unsigned char fill, Vector3& min, Vector3& max)
    {
        alignas(VertexTable) unsigned char storage[sizeof(VertexTable)];
        memset(storage, fill, sizeof(storage));
        VertexTable* vt = new (storage) VertexTable(2);
        vt->SetPos(0) = Vector3(-1, -2, -3);
        vt->SetPos(1) = Vector3(4, 5, 6);
        Vector3 minMax[2];
        vt->MinMaxDynamic(minMax);
        min = minMax[0];
        max = minMax[1];
        vt->~VertexTable();
    };

    Vector3 zeroedMin, zeroedMax, poisonedMin, poisonedMax;
    minMaxOver(0x00, zeroedMin, zeroedMax);
    minMaxOver(0xBE, poisonedMin, poisonedMax);

    for (int i = 0; i < 3; i++)
    {
        REQUIRE(poisonedMin[i] == Catch::Approx(zeroedMin[i]));
        REQUIRE(poisonedMax[i] == Catch::Approx(zeroedMax[i]));
    }
}

TEST_CASE("PolyVertices - basic operations", "[rendering][poly]")
{
    SECTION("default state")
    {
        PolyVertices pv;
        pv.Init();
        REQUIRE(pv.N() == 0);
    }

    SECTION("set vertices and check count")
    {
        PolyVertices pv;
        pv.SetN(3);
        pv.Set(0, 0);
        pv.Set(1, 1);
        pv.Set(2, 2);
        REQUIRE(pv.N() == 3);
        REQUIRE(pv.GetVertex(0) == 0);
        REQUIRE(pv.GetVertex(1) == 1);
        REQUIRE(pv.GetVertex(2) == 2);
    }

    SECTION("copy assignment")
    {
        PolyVertices pv;
        pv.SetN(3);
        pv.Set(0, 10);
        pv.Set(1, 20);
        pv.Set(2, 30);
        PolyVertices pv2;
        pv2 = pv;
        REQUIRE(pv2.N() == 3);
        REQUIRE(pv2.GetVertex(0) == 10);
        REQUIRE(pv2.GetVertex(2) == 30);
    }

    SECTION("reverse 3 vertices")
    {
        PolyVertices pv;
        pv.SetN(3);
        pv.Set(0, 0);
        pv.Set(1, 1);
        pv.Set(2, 2);
        pv.Reverse();
        REQUIRE(pv.GetVertex(0) == 1);
        REQUIRE(pv.GetVertex(1) == 0);
        REQUIRE(pv.GetVertex(2) == 2);
    }

    SECTION("reverse 4 vertices")
    {
        PolyVertices pv;
        pv.SetN(4);
        pv.Set(0, 10);
        pv.Set(1, 20);
        pv.Set(2, 30);
        pv.Set(3, 40);
        pv.Reverse();
        REQUIRE(pv.GetVertex(0) == 20);
        REQUIRE(pv.GetVertex(1) == 10);
        REQUIRE(pv.GetVertex(2) == 40);
        REQUIRE(pv.GetVertex(3) == 30);
    }
}

TEST_CASE("Poly - construction", "[rendering][poly]")
{
    SECTION("init clears state")
    {
        Poly p;
        p.Init();
        REQUIRE(p.N() == 0);
        REQUIRE(p.GetTexture() == nullptr);
        REQUIRE(p.Special() == 0);
    }

    SECTION("typical item size")
    {
        REQUIRE(Poly::TypicalItemSize() > 0);
    }
}

TEST_CASE("PolyPlain - construction", "[rendering][poly]")
{
    SECTION("init and set")
    {
        PolyPlain pp;
        pp.Init();
        pp.SetN(3);
        pp.Set(0, 5);
        pp.Set(1, 10);
        pp.Set(2, 15);
        REQUIRE(pp.N() == 3);
        REQUIRE(pp.GetVertex(1) == 10);
    }
}

TEST_CASE("UVPair - basic struct", "[rendering][vertex]")
{
    UVPair uv;
    uv.u = 0.25f;
    uv.v = 0.75f;
    REQUIRE(uv.u == Catch::Approx(0.25f));
    REQUIRE(uv.v == Catch::Approx(0.75f));
}

TEST_CASE("edges.hpp - compile check", "[rendering][edges]")
{
    SECTION("FaceEdges default construction")
    {
        FaceEdges fe;
        REQUIRE(fe.NFaces() == 0);
    }
}

TEST_CASE("clip2d.hpp - compile check", "[rendering][clip2d]")
{
    SUCCEED("clip2d.hpp included successfully");
}

TEST_CASE("clipVert.hpp - compile check", "[rendering][clipVert]")
{
    SUCCEED("clipVert.hpp included successfully");
}
