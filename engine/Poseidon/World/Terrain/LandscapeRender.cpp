
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Dev/Diag/FrameProfiler.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Terrain/Occlusion.hpp>
#include <Poseidon/World/Terrain/TerrainProfile.hpp>

#include <Poseidon/World/Terrain/LandscapeShared.hpp>

#include <mutex>
#include <float.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/Array2D.hpp>
#include <Poseidon/Foundation/Containers/SmallArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>

// LandCache::Fill generates segments on multiple TaskPool threads. Each segment
// registers shared landscape textures into its shape, which AddRef's the texture's
// (non-atomic) refcount — concurrent registration would race and corrupt the count,
// surfacing later as a use-after-free / double-free at teardown. Serialize just the
// texture registration; the expensive geometry generation stays parallel.
static std::mutex GSegTextureRegMutex;

namespace Poseidon
{
TerrainProfile GTerrainProfile = {};
} // namespace Poseidon
namespace Poseidon
{

SRef<Occlusion>& GetOcclusions();
extern bool EnableObjOcc;

void Landscape::DrawMesh(Scene& scene, TLVertexTable& table, const Shape& vMesh, Vector3Par offset,
                         const LandBegEnd& rect, bool isWater)
{
    GTerrainProfile.drawMeshCalls++;
    // exact per-segment clip rejection
    Vector3Val bCenter = vMesh.BSphereCenter() + offset;
    float bRadius = vMesh.BSphereRadius();

    // Point3 bCenterView(VFastTransform,GScene->ScaledInvTransform(),bCenter);
    if (scene.GetCamera()->IsClipped(bCenter, bRadius, 1))
    {
        GTerrainProfile.drawMeshClipped++;
        return;
    }
    ClipFlags clip = scene.GetCamera()->MayBeClipped(bCenter, bRadius, 1);
    ClipFlags andClip = ClipNone;
    ClipFlags orClip = table.CheckClipping(*scene.GetCamera(), clip, andClip);
    if (andClip & ClipAll)
    {
        return;
    }

    // select lights on per-segment basis
    LightList work(true);
    const LightList& lights = scene.SelectLights(bCenter, bRadius, work);

    table.DoLightingColorized(lights, _colorizePalette, vMesh, 0);

    // there will be many vertices clipped
    // we want vertex mesh and table to grow fast to avoid many allocations

    if (isWater)
    {
        // animate all vertices
        Texture* tex = _texture[0];
        if (tex)
        {
            for (int i = 0; i < table.NVertex(); i++)
            {
                Vector3Val pos = vMesh.Pos(i);
                float u = tex->UToLogical(table.U(i));
                float v = tex->VToLogical(table.V(i));
                float x = pos.X() * _invLandGrid;
                float z = pos.Z() * _invLandGrid;
                float mw = MoveWater(x, z, 0.3);
                table.SetUV(i, tex->UToPhysical(u + mw * 0.5), tex->VToPhysical(v + mw));
            }
        }
    }

    // prepare surface for drawing
    const FaceArray& sFaces = vMesh.Faces();
    const FaceArray* drawFaces = &sFaces;
    FaceArray clippedFaces;
    // copy all faces and perform per-face clipping

    if (orClip || isWater)
    {
        // this segment is clipped or water
        // avoid copy when not reflected

        Texture* waterTex = _texture[0];
        if (isWater && waterTex)
        {
            float texPhase = fastFmod(Glob.time.toFloat(), 2);
            if (texPhase > 1.0f)
            {
                texPhase = 2 - texPhase;
            }

            int n = waterTex->AnimationLength();
            if (n <= 1)
            {
                return; // no animation
            }
            int i = toIntFloor(texPhase * n);
            saturate(i, 0, n - 1);
            waterTex = waterTex->GetAnimation(i);
        }
        // full clipping required
        drawFaces = &clippedFaces;
        clippedFaces.ReserveFaces(sFaces.Size() * 2, false);
        for (Offset f = sFaces.Begin(), e = sFaces.End(); f < e; sFaces.Next(f))
        {
            const Poly& src = sFaces[f];
            // some faces are fully clipped - have invalid vertices
            // trick to avoid branches
            // if any of three is negative, or is negative as well
            ClipFlags clipOr = ClipNone;
            if (orClip)
            {
                ClipFlags clipAnd = ClipAll;
                for (int i = 0; i < src.N(); i++)
                {
                    int sv = src.GetVertex(i);
                    ClipFlags clip = table.Clip(sv);
                    clipOr |= clip, clipAnd &= clip;
                }
                if (clipAnd)
                {
                    continue;
                }
            }

            Poly* dst = nullptr;
            if (!clipOr)
            {
                dst = clippedFaces.AddNoClip(src, table, scene);
            }
            else
            {
                dst = clippedFaces.AddClipped(src, table, scene, clipOr);
            }
            if (isWater && dst)
            {
                // animate water texture
                dst->SetTexture(waterTex);
            }
        }
    }

    if (drawFaces->Size() <= 0)
    {
        return;
    }

    GEngine->PrepareMesh(render::LegacySpec{});
    _engine->SetBias(0);

    table.DoPerspective(*scene.GetCamera(), orClip);

    if (EnableObjOcc)
    {
        // draw landscape into occlusion buffer
        float invW = +2.0 / GEngine->Width();
        float invH = -2.0 / GEngine->Height();
        // float zScale=1.0/GScene->GetCamera()->ClipNear();
        OcclusionPoly poly;
        for (Offset f = drawFaces->Begin(); f < drawFaces->End(); drawFaces->Next(f))
        {
            const Poly& face = drawFaces->Get(f);
            // construct occlusion poly from landscape poly
            float minZ = 1e10;
            poly.Clear();
            for (int i = face.N(); --i >= 0;)
            {
                int index = face.GetVertex(i);
                // Vector3PVal pos=table.ScreenPos(index);
                //  screen coordinates (0..w,0..h)
                //  must be converted to -1..+1 coordinates
                const TLVertex& tl = table.GetVertex(index);
                Vector3Val sPos = table.TransPos(index);
                Vector3 uPos(tl.pos[0] * invW - 1, tl.pos[1] * invH + 1, sPos.Z());
                // Vector3 uPos(pos[0]*invW-1,pos[1]*invH+1,pos[2]);
                poly.Add(uPos);
                saturateMin(minZ, sPos.Z());
            }
            if (minZ < 250)
            {
                // draw only near landscape polygons
                GetOcclusions()->RenderProjectedPoly(poly);
            }
        }
    }

    // note: BeginMesh may change pos field (may be used for fog etc)
    _engine->BeginMesh(table, render::LegacySpec{});
    float z = scene.GetCamera()->Position().Distance(bCenter) - bRadius;
    float z2 = Square(floatMax(z, 0));
    Texture* lastTexture = nullptr;
    int lastSpec = -1;
    // scan all textures used in this segment and prepare them
    vMesh.PrepareTextures(z2, 0);
    for (Offset f = drawFaces->Begin(); f < drawFaces->End(); drawFaces->Next(f))
    {
        const Poly& face = drawFaces->Get(f);
        Texture* texture = face.GetTexture();
        int spec = face.Special();
        if (texture != lastTexture || spec != lastSpec)
        {
            lastSpec = spec;
            lastTexture = texture;

            face.Prepare(texture, spec);
        }

        GEngine->DrawPolygon(face.GetVertexList(), face.N());
    }
    _engine->EndMesh(table);
    int idleMs = _engine->HowLongIdle();
    if (idleMs >= 0 && GWorld)
    {
        GWorld->PrimaryAllowSwitch(idleMs);
    }
}

static int SortByTextureR(const Poly& p0, const Poly& p1)
{
    return (char*)p0.GetTexture() - (char*)p1.GetTexture();
}

const int MaxVertexCanditates = 6; // vertex may be shared between six faces
class VertexCandidates : public VerySmallArray<int, sizeof(int) * 8>
{
  public:
    void AddUnique(int src)
    {
#if _DEBUG
        for (int i = 0; i < Size(); i++)
        {
            if ((*this)[i] == src)
            {
                Fail("Index not unique");
                return;
            }
        }
#endif
        Add(src);
    }
    int FindOrAdd(int src);
    int Find(int src) const;
};

int VertexCandidates::FindOrAdd(int src)
{
    for (int i = 0; i < Size(); i++)
    {
        if ((*this)[i] == src)
        {
            return i;
        }
    }
    return Add(src);
}

int VertexCandidates::Find(int src) const
{
    for (int i = 0; i < Size(); i++)
    {
        if ((*this)[i] == src)
        {
            return i;
        }
    }
    return -1;
}

extern bool EnableHWTLState;

#define LOG_SHARING 0

// bilinear interpolation, yxz, xf and zf = <0,1>

__forceinline float Bilint(float y00, float y01, float y10, float y11, float xf, float zf)
{
    float y0z = y00 * (1 - zf) + y01 * zf;
    float y1z = y10 * (1 - zf) + y11 * zf;
    return y0z * (1 - xf) + y1z * xf;
}

static Vector3 WaterNormal(0, -1, 0);

// const int WaterFlags = IsWater|SpecularTexture;

const int WaterFlags = SpecularTexture;

#define SEAMS 1

Ref<LandSegment> Landscape::GenerateSegment(const LandBegEnd& rect, bool deferGPU)
{
#if LOG_SEG
    LOG_DEBUG(World, "GenerateSegment {},{}", rect.xBeg, rect.zBeg);
#endif
    LandSegment* seg = new LandSegment;
    GenerateSegmentInto(seg, rect, deferGPU);
    return seg;
}

void Landscape::GenerateSegmentInto(LandSegment* seg, const LandBegEnd& rect, bool deferGPU, int lodLevel)
{
    // we have to reconstruct cache

    // create all vertices
    // const int xCount=rect.xEnd-rect.xBeg+1;
    // int zCount=rect.zEnd-rect.zBeg+1;

    // calculate counts for subdivision of very big landscape squares
    const int subdivisionLevel = _terrainRangeLog - _landRangeLog;
#define maxSubdivisionLevel 4

    const int subdivisionCount = 1 << subdivisionLevel;

    // LOD: reduce effective subdivision for distant segments
    const int lodStride = 1 << (lodLevel < subdivisionLevel ? lodLevel : subdivisionLevel);
    const int effSubdivCount = subdivisionCount / lodStride;
    const float invEffSubdivCount = 1.0f / effSubdivCount;

    LandBegEnd rectSD;
    rectSD.xBeg = rect.xBeg * subdivisionCount;
    rectSD.xEnd = rect.xEnd * subdivisionCount;
    rectSD.zBeg = rect.zBeg * subdivisionCount;
    rectSD.zEnd = rect.zEnd * subdivisionCount;

    const int xCountSD = (rect.xEnd - rect.xBeg) * effSubdivCount + 1;
    const int zCountSD = (rect.zEnd - rect.zBeg) * effSubdivCount + 1;

    const int nVertices = zCountSD * xCountSD;

#define maxSubdivisionCount (1 << maxSubdivisionLevel)
#define maxNVertices ((LandSegmentSize * maxSubdivisionCount + 1) * (LandSegmentSize * maxSubdivisionCount + 1))

    PoseidonAssert(nVertices <= maxNVertices);

    seg->_lodLevel = lodLevel;

    seg->_someWater = false;
    seg->_onlyWater = true;
    seg->_seaLevel = 0; //=_seaLevelWave;

    // Heap-backed arrays — stack arrays would be ~1.6MB (too large for worker threads)
    StaticArrayAuto<Vector3> pos;
    StaticArrayAuto<Vector3> norm;
    StaticArrayAuto<Vector3> wPos;
    pos.Resize(nVertices);
    norm.Resize(nVertices);

    wPos.Resize(nVertices);

    if (this_TerrainInRange(rectSD.zBeg, rectSD.xBeg))
    {
        // first calculate basic vertices (subdivision)
        float lMin = FLT_MAX;
        float lMax = FLT_MIN;

        for (int z = rectSD.zBeg; z <= rectSD.zEnd; z++)
        {
            for (int x = rectSD.xBeg; x <= rectSD.xEnd; x++)
            {
                // const int xz=(z-rectSD.zBeg)*xCountSD+(x-rectSD.xBeg);
                float l = YOutsideMap;
                if (this_TerrainInRange(x, z))
                {
                    l = GetData(x, z);
                }
                // detect possible water (consider max high tide)
                saturateMax(lMax, l);
                saturateMin(lMin, l);
            }
        }

        if (lMin <= maxTide + maxWave)
        {
            seg->_someWater = true;
        }
        if (lMax >= -(maxTide + maxWave))
        {
            seg->_onlyWater = false;
        }
    }
    else
    {
        seg->_someWater = seg->_onlyWater = true;
    }

    bool centered = GEngine->GetTL();
    seg->_offset = VZero;
    int offsetX = 0, offsetZ = 0;
    if (centered)
    {
        seg->_offset =
            Vector3(_landGrid * 0.5f * (rect.xBeg + rect.xEnd), 0, _landGrid * 0.5f * (rect.zBeg + rect.zEnd));
        // make sure offset can be represented by integer
        DoAssert(((rectSD.xBeg + rectSD.xEnd) & 1) == 0);
        DoAssert(((rectSD.zBeg + rectSD.zEnd) & 1) == 0);

        offsetX = (rectSD.xBeg + rectSD.xEnd) >> 1;
        offsetZ = (rectSD.zBeg + rectSD.zEnd) >> 1;
    }

    if (seg->_someWater)
    {
        seg->_wTable.Reserve(nVertices * 4);

        // subdivided water surface at LOD resolution
        for (int zz = 0; zz < zCountSD; zz++)
        {
            for (int xx = 0; xx < xCountSD; xx++)
            {
                const int xz = zz * xCountSD + xx;
                int terrX = rectSD.xBeg + xx * lodStride;
                int terrZ = rectSD.zBeg + zz * lodStride;
                wPos[xz] = Vector3(_terrainGrid * (terrX - offsetX), 0, _terrainGrid * (terrZ - offsetZ));
                const float stretch = +0.001f;
                if (zz == 0)
                {
                    wPos[xz][2] -= stretch;
                }
                if (zz == zCountSD - 1)
                {
                    wPos[xz][2] += stretch;
                }
                if (xx == 0)
                {
                    wPos[xz][0] -= stretch;
                }
                if (xx == xCountSD - 1)
                {
                    wPos[xz][0] += stretch;
                }
            }
        }
    }

    // note: every vertex in mesh is initialized

    StaticArrayAuto<VertexCandidates> wCandidates;
    StaticArrayAuto<VertexCandidates> candidates;

    if (!seg->_onlyWater)
    {
        for (int z = rectSD.zBeg; z <= rectSD.zEnd; z += lodStride)
        {
            for (int x = rectSD.xBeg; x <= rectSD.xEnd; x += lodStride)
            {
                // calculate normals for all basic vertices
                const int xz = ((z - rectSD.zBeg) / lodStride) * xCountSD + ((x - rectSD.xBeg) / lodStride);

                const Coord xDelta = ClippedData(z, x + lodStride) - ClippedData(z, x - lodStride);
                const Coord zDelta = ClippedData(z + lodStride, x) - ClippedData(z - lodStride, x);
                Point3 offX(_terrainGrid * lodStride, xDelta, 0);
                Point3 offZ(0, zDelta, _terrainGrid * lodStride);
                Vector3Val cp = offX.CrossProduct(offZ);
                Vector3Val normal = cp.Normalized();

                PoseidonAssert(normal.IsFinite());
                PoseidonAssert(xz < nVertices);
                norm[xz] = normal;

                // set position data
                float l = ClippedData(z, x);
                pos[xz] = Vector3(_terrainGrid * (x - offsetX), l, _terrainGrid * (z - offsetZ));

                // apply some stretch on border vertices to prevent rounding error tearing
                const float stretch = +0.0001f;
                if (z == rectSD.zBeg)
                {
                    pos[xz][2] -= stretch;
                }
                if (z == rectSD.zEnd)
                {
                    pos[xz][2] += stretch;
                }
                if (x == rectSD.xBeg)
                {
                    pos[xz][0] -= stretch;
                }
                if (x == rectSD.xEnd)
                {
                    pos[xz][0] += stretch;
                }
            }
        }

        candidates.Resize(nVertices);

        if (seg->_someWater)
        {
            wCandidates.Resize(nVertices);
        }

        // we need to implement clamping
        // clamped faces cannot share (u,v) coordinates
        seg->_table.Reserve(nVertices * 4);

        seg->_table.ClearFaces();
        seg->_table.ReserveFaces((xCountSD - 1) * (zCountSD - 1) * 2);
        for (int zs = rect.zBeg; zs < rect.zEnd; zs++)
        {
            for (int xs = rect.xBeg; xs < rect.xEnd; xs++)
            {
                int textureIndex = ClippedTextureIndex(zs, xs);
                if (textureIndex == 0)
                {
                    continue; // no water polygons in this stage
                }

                Texture* texture = _texture[textureIndex];
                int clampFlags = ClampFlags(textureIndex);
                clampFlags |= DetailTexture;

                if (texture)
                {
                    std::lock_guard<std::mutex> texLock(GSegTextureRegMutex);
                    seg->_table.RegisterTexture(texture, _landGrid * _landGrid * 0.5f);
                }

                //      xz     xz
                float tu00 = 0, tv00 = 0;
                float tu01 = 0, tv01 = 0;
                float tu10 = 0, tv10 = 0;
                float tu11 = 0, tv11 = 0;

                // prepare to offset interpolation
                // get u,v offset in all four vertices
                if (this_InRange(xs, zs) && this_InRange(xs + 1, zs + 1))
                {
                    const float scale = 1.0 / 10;
                    tu10 = _random(xs + 1, zs).uOff * scale, tv10 = _random(xs + 1, zs).vOff * scale;
                    tu00 = _random(xs, zs).uOff * scale, tv00 = _random(xs, zs).vOff * scale;
                    tu01 = _random(xs, zs + 1).uOff * scale, tv01 = _random(xs, zs + 1).vOff * scale;
                    tu11 = _random(xs + 1, zs + 1).uOff * scale, tv11 = _random(xs + 1, zs + 1).vOff * scale;
                }

                float u0, v0;
                if (!(clampFlags & ClampU))
                {
                    u0 = xs - rect.xBeg;
                }
                else
                {
                    u0 = 0;
                }
                if (!(clampFlags & ClampV))
                {
                    v0 = zs - rect.zBeg;
                }
                else
                {
                    v0 = 0;
                }

                Poly poly;
                poly.Init();
                poly.SetTexture(texture);
                poly.SetSpecial(clampFlags);

                for (int zzs = 0; zzs < effSubdivCount; zzs++)
                {
                    for (int xxs = 0; xxs < effSubdivCount; xxs++)
                    {
                        const int xx = (xs - rect.xBeg) * effSubdivCount + xxs;
                        const int zz = (zs - rect.zBeg) * effSubdivCount + zzs;

                        // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
                        const int xz = zz * xCountSD + xx;

                        //          xz
                        const int vo10 = xz + 1 + 0;
                        const int vo00 = xz + 0 + 0;
                        const int vo01 = xz + 0 + xCountSD;
                        const int vo11 = xz + 1 + xCountSD;

                        const float xf = xxs * invEffSubdivCount;
                        const float zf = zzs * invEffSubdivCount;

                        const float u = u0 + xf, v = v0 + zf;

                        // now we have polygon to add
                        PoseidonAssert(vo10 < nVertices);
                        PoseidonAssert(vo00 < nVertices);
                        PoseidonAssert(vo01 < nVertices);
                        PoseidonAssert(vo11 < nVertices);
                        float uu0 = 0, uu1 = 0, uu2 = 0, uu3 = 0;
                        float vv0 = 0, vv1 = 0, vv2 = 0, vv3 = 0;
                        if (texture)
                        {
                            // interpolate u,v offset in all four vertices
                            // uuk is interpolated from tuii
                            // vvk is interpolated from tvii
                            const float xf1 = xf + invEffSubdivCount;
                            const float zf1 = zf + invEffSubdivCount;

                            const float xf00 = xf, zf00 = zf;
                            const float xf01 = xf, zf01 = zf1;
                            const float xf10 = xf1, zf10 = zf;
                            const float xf11 = xf1, zf11 = zf1;

#define UV(xy)                                                            \
    const float tuu##xy = Bilint(tu00, tu01, tu10, tu11, xf##xy, zf##xy); \
    const float tvv##xy = Bilint(tv00, tv01, tv10, tv11, xf##xy, zf##xy)
                            UV(00);
                            UV(01);
                            UV(10);
                            UV(11);

                            // 00-> 1
                            // 01-> 2
                            // 10-> 0
                            // 11-> 3

                            // 10-> 0
                            // 00-> 1
                            // 01-> 2
                            // 11-> 3

                            uu0 = texture->UToPhysical(u + invEffSubdivCount + tuu10); // v10
                            vv0 = texture->VToPhysical(v + tvv10);
                            uu1 = texture->UToPhysical(u + tuu00); // v00
                            vv1 = texture->VToPhysical(v + tvv00);
                            uu2 = texture->UToPhysical(u + tuu01); // v01
                            vv2 = texture->VToPhysical(v + invEffSubdivCount + tvv01);
                            uu3 = texture->UToPhysical(u + invEffSubdivCount + tuu11); // v11
                            vv3 = texture->VToPhysical(v + invEffSubdivCount + tvv11);
                        }
#undef UV

                        // store index as candidate for vertices created from same data
                        bool reused;
                        int v10 = seg->_table.AddVertex(pos[vo10], norm[vo10], ClipAll, uu0, vv0,
                                                        candidates[vo10].Data(), candidates[vo10].Size(), reused);
                        if (!reused)
                        {
                            candidates[vo10].AddUnique(v10);
                        }
                        int v00 = seg->_table.AddVertex(pos[vo00], norm[vo00], ClipAll, uu1, vv1,
                                                        candidates[vo00].Data(), candidates[vo00].Size(), reused);
                        if (!reused)
                        {
                            candidates[vo00].AddUnique(v00);
                        }
                        int v01 = seg->_table.AddVertex(pos[vo01], norm[vo01], ClipAll, uu2, vv2,
                                                        candidates[vo01].Data(), candidates[vo01].Size(), reused);
                        if (!reused)
                        {
                            candidates[vo01].AddUnique(v01);
                        }
                        int v11 = seg->_table.AddVertex(pos[vo11], norm[vo11], ClipAll, uu3, vv3,
                                                        candidates[vo11].Data(), candidates[vo11].Size(), reused);
                        if (!reused)
                        {
                            candidates[vo11].AddUnique(v11);
                        }
                        // create a face
                        poly.Set(0, v10); // shared vertices
                        poly.Set(1, v00);
                        poly.Set(2, v01);
                        poly.SetN(3);

                        seg->_table.AddFace(poly);

                        // reuse as much as possible
                        poly.Set(1, v01);
                        poly.Set(2, v11);

                        seg->_table.AddFace(poly);

#if SEAMS
                        const float ySeam = -0.50f;
                        if (xxs == 0 && xs == rect.xBeg)
                        {
                            // border: create a seam
                            // use vertices v00, v01

                            int v00Seam =
                                seg->_table.AddVertex(pos[vo00] + Vector3(0, ySeam, 0), norm[vo00], ClipAll, uu1, vv1);
                            int v01Seam =
                                seg->_table.AddVertex(pos[vo01] + Vector3(0, ySeam, 0), norm[vo01], ClipAll, uu2, vv2);

                            poly.SetN(4);
                            poly.Set(0, v01);
                            poly.Set(1, v00);
                            poly.Set(2, v00Seam);
                            poly.Set(3, v01Seam);
                            seg->_table.AddFace(poly);
                        }
                        if (xxs == effSubdivCount - 1 && xs == rect.xEnd - 1)
                        {
                            // border: create a seam
                            // use vertices v10, v11

                            int v10Seam =
                                seg->_table.AddVertex(pos[vo10] + Vector3(0, ySeam, 0), norm[vo10], ClipAll, uu0, vv0);
                            int v11Seam =
                                seg->_table.AddVertex(pos[vo11] + Vector3(0, ySeam, 0), norm[vo11], ClipAll, uu3, vv3);

                            poly.SetN(4);
                            poly.Set(0, v10);
                            poly.Set(1, v11);
                            poly.Set(2, v11Seam);
                            poly.Set(3, v10Seam);
                            seg->_table.AddFace(poly);
                        }

                        if (zzs == 0 && zs == rect.zBeg)
                        {
                            // border: create a seam
                            // use vertices v00, v10

                            int v00Seam =
                                seg->_table.AddVertex(pos[vo00] + Vector3(0, ySeam, 0), norm[vo00], ClipAll, uu1, vv1);
                            int v10Seam =
                                seg->_table.AddVertex(pos[vo10] + Vector3(0, ySeam, 0), norm[vo10], ClipAll, uu0, vv0);
                            poly.SetN(4);
                            poly.Set(0, v00);
                            poly.Set(1, v10);
                            poly.Set(2, v10Seam);
                            poly.Set(3, v00Seam);
                            seg->_table.AddFace(poly);
                        }
                        if (zzs == effSubdivCount - 1 && zs == rect.zEnd - 1)
                        {
                            // border: create a seam
                            // use vertices v11,v01

                            int v01Seam =
                                seg->_table.AddVertex(pos[vo01] + Vector3(0, ySeam, 0), norm[vo01], ClipAll, uu2, vv2);
                            int v11Seam =
                                seg->_table.AddVertex(pos[vo11] + Vector3(0, ySeam, 0), norm[vo11], ClipAll, uu3, vv3);
                            poly.SetN(4);

                            poly.Set(0, v11);
                            poly.Set(1, v01);
                            poly.Set(2, v01Seam);
                            poly.Set(3, v11Seam);
                            seg->_table.AddFace(poly);
                        }
#endif
                    }
                }
            }
        }
// for( int zs=rect.zBeg; zs<rect.zEnd; zs++ )
// for( int xs=rect.xBeg; xs<rect.xEnd; xs++ )

// Log("Alloc %d<%d",seg->_mesh.NVertex(),nVertices*3);
#if LOG_SHARING
        int maxV = seg->_table.NFaces() * 3;
        int actV = seg->_table.NVertex();
        LOG_DEBUG(World, "Ground sharing ratio is {}", actV * 1.0f / maxV);
#endif

        seg->_table.Compact();
        seg->_table.CalculateMinMax();

        if (seg->_someWater)
        {
            // calculate how much faces will be in water
            int wFaces = 0;
            int zn = (rect.zEnd - rect.zBeg) * effSubdivCount;
            int xn = (rect.xEnd - rect.xBeg) * effSubdivCount;

            int wVertices = zn + 1 + xn + 1;
            for (int zz = 0; zz < zn; zz++)
            {
                for (int xx = 0; xx < xn; xx++)
                {
                    // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
                    int xz = xx + zz * xCountSD;
                    float miny = pos[xz + 0 + 1].Y();
                    saturateMin(miny, pos[xz + 0 + 0].Y());
                    saturateMin(miny, pos[xz + 0 + xCountSD].Y());
                    saturateMin(miny, pos[xz + 1 + xCountSD].Y());
                    miny += seg->_offset.Y();
                    if (miny <= maxTide + maxWave)
                    {
                        wFaces += 2, wVertices += 2;
                    }
                }
            }

            seg->_wTable.Clear();
            seg->_wTable.ReserveFaces(wFaces);

            seg->_wTable.Reserve(wVertices * 4);

            // add water faces
            Poly water;
            water.Init();
            water.SetTexture(_texture[0]);
            water.OrSpecial(WaterFlags | NoClamp);
            water.SetN(3);
            for (int zz = 0; zz < zn; zz++)
            {
                for (int xx = 0; xx < xn; xx++)
                {
                    // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
                    int xz = xx + zz * xCountSD;
                    float miny = pos[xz + 0 + 1].Y();
                    saturateMin(miny, pos[xz + 0 + 0].Y());
                    saturateMin(miny, pos[xz + 0 + xCountSD].Y());
                    saturateMin(miny, pos[xz + 1 + xCountSD].Y());
                    if (miny <= maxTide + maxWave)
                    {
                        // water polygon
                        const float zR = zz * invEffSubdivCount;
                        const float xR = xx * invEffSubdivCount;

                        const int vo10 = xz + 0 + 0;
                        const int vo00 = xz + 0 + 1;
                        const int vo01 = xz + xCountSD + 0;
                        const int vo11 = xz + xCountSD + 1;

                        Texture* t0 = _texture[0];
                        float zu0 = 0, zu1 = 0, xv0 = 0, xv1 = 0;
                        if (t0)
                        {
                            zu0 = t0->UToPhysical(zR);
                            zu1 = t0->UToPhysical(zR + invEffSubdivCount);
                            xv0 = t0->VToPhysical(xR);
                            xv1 = t0->VToPhysical(xR + invEffSubdivCount);
                        }
                        bool reused;
                        int v10 = seg->_wTable.AddVertex(wPos[vo10], WaterNormal, ClipAll, zu0, xv0,
                                                         wCandidates[vo10].Data(), wCandidates[vo10].Size(), reused);
                        if (!reused)
                        {
                            wCandidates[vo10].AddUnique(v10);
                        }
                        int v00 = seg->_wTable.AddVertex(wPos[vo00], WaterNormal, ClipAll, zu0, xv1,
                                                         wCandidates[vo00].Data(), wCandidates[vo00].Size(), reused);
                        if (!reused)
                        {
                            wCandidates[vo00].AddUnique(v00);
                        }
                        int v01 = seg->_wTable.AddVertex(wPos[vo01], WaterNormal, ClipAll, zu1, xv0,
                                                         wCandidates[vo01].Data(), wCandidates[vo01].Size(), reused);
                        if (!reused)
                        {
                            wCandidates[vo01].AddUnique(v01);
                        }
                        int v11 = seg->_wTable.AddVertex(wPos[vo11], WaterNormal, ClipAll, zu1, xv1,
                                                         wCandidates[vo11].Data(), wCandidates[vo11].Size(), reused);
                        if (!reused)
                        {
                            wCandidates[vo11].AddUnique(v11);
                        }

                        water.Set(0, v00), water.Set(1, v10), water.Set(2, v01);
                        seg->_wTable.AddFace(water);

                        water.Set(0, v00), water.Set(1, v01), water.Set(2, v11);
                        seg->_wTable.AddFace(water);
                    }
                }
            }
            // vertices should be shared .. check if it is true
            // verify vertex and face estimation
            PoseidonAssert(seg->_wTable.NFaces() <= wFaces);
            if (seg->_wTable.NFaces() > 0)
            {
                std::lock_guard<std::mutex> texLock(GSegTextureRegMutex);
                seg->_wTable.RegisterTexture(_texture[0], _landGrid * _landGrid * 0.5f);
            }
            seg->_wTable.Compact();
            seg->_wTable.CalculateMinMax();
            seg->_wTable.FindSections(true);
            if (!deferGPU)
                seg->_wTable.ConvertToVBuffer(VBBigDiscardable);

#if LOG_SHARING
            int maxV = seg->_wTable.NFaces() * 3;
            int actV = seg->_wTable.NVertex();
            LOG_DEBUG(World, "Water sharing ratio is {}", actV * 1.0f / maxV);
#endif
        }
        // sort faces by texture - it will help to avoid state changes
        seg->_table.Optimize();
        seg->_table.FindSections(true);
        if (!deferGPU)
            seg->_table.ConvertToVBuffer(VBBigDiscardable);
    }
    else
    {
        // only water - no landscape
        seg->_table.Clear();

        PoseidonAssert(seg->_someWater);
        // calculate how much faces will be in water

        int zn = (rect.zEnd - rect.zBeg) * effSubdivCount;
        int xn = (rect.xEnd - rect.xBeg) * effSubdivCount;

        int wFaces = zn * xn * 2;

        seg->_wTable.Clear();
        seg->_wTable.ReserveFaces(wFaces);

        // add water faces
        Poly water;
        water.Init();
        water.SetTexture(_texture[0]);
        water.OrSpecial(WaterFlags | NoClamp);
        water.SetN(3);

        wCandidates.Resize(nVertices);

        for (int zz = 0; zz < zn; zz++)
        {
            for (int xx = 0; xx < xn; xx++)
            {
                // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
                const int xz = xx + zz * xCountSD;
                // water polygon
                const float zR = zz * invEffSubdivCount;
                const float xR = xx * invEffSubdivCount;

                const int vo10 = xz + 0 + 0;
                const int vo00 = xz + 0 + 1;
                const int vo01 = xz + xCountSD + 0;
                const int vo11 = xz + xCountSD + 1;

                Texture* t0 = _texture[0];
                float zu0 = 0, zu1 = 0, xv0 = 0, xv1 = 0;
                if (t0)
                {
                    zu0 = t0->UToPhysical(zR);
                    zu1 = t0->UToPhysical(zR + invEffSubdivCount);
                    xv0 = t0->VToPhysical(xR);
                    xv1 = t0->VToPhysical(xR + invEffSubdivCount);
                }

                bool reused;
                int v10 = seg->_wTable.AddVertex(wPos[vo10], WaterNormal, ClipAll, zu0, xv0, wCandidates[vo10].Data(),
                                                 wCandidates[vo10].Size(), reused);
                if (!reused)
                {
                    wCandidates[vo10].AddUnique(v10);
                }
                int v00 = seg->_wTable.AddVertex(wPos[vo00], WaterNormal, ClipAll, zu0, xv1, wCandidates[vo00].Data(),
                                                 wCandidates[vo00].Size(), reused);
                if (!reused)
                {
                    wCandidates[vo00].AddUnique(v00);
                }
                int v01 = seg->_wTable.AddVertex(wPos[vo01], WaterNormal, ClipAll, zu1, xv0, wCandidates[vo01].Data(),
                                                 wCandidates[vo01].Size(), reused);
                if (!reused)
                {
                    wCandidates[vo01].AddUnique(v01);
                }
                int v11 = seg->_wTable.AddVertex(wPos[vo11], WaterNormal, ClipAll, zu1, xv1, wCandidates[vo11].Data(),
                                                 wCandidates[vo11].Size(), reused);
                if (!reused)
                {
                    wCandidates[vo11].AddUnique(v11);
                }

                water.Set(0, v00), water.Set(1, v10), water.Set(2, v01);
                seg->_wTable.AddFace(water);

                water.Set(0, v00), water.Set(1, v01), water.Set(2, v11);
                seg->_wTable.AddFace(water);
            }
        }
        // vertices should be shared .. check if it is true
        // PoseidonAssert( seg->_wMesh.NVertex()<=wVertices );
        // PoseidonAssert( seg->_wFaces.Size()<=wFaces );
        // seg->_wFaces.Compact();
        seg->_wTable.Compact();
        PoseidonAssert(seg->_wTable.NFaces() <= wFaces);
        seg->_wTable.CalculateMinMax();
        {
            std::lock_guard<std::mutex> texLock(GSegTextureRegMutex);
            seg->_wTable.RegisterTexture(_texture[0], _landGrid * _landGrid * 0.5f);
        }
        seg->_wTable.FindSections(true);
        if (!deferGPU)
            seg->_wTable.ConvertToVBuffer(VBBigDiscardable);

#if LOG_SHARING
        int maxV = seg->_wTable.NFaces() * 3;
        int actV = seg->_wTable.NVertex();
        LOG_DEBUG(World, "Water (deep) sharing ratio is {}", actV * 1.0f / maxV);
#endif
    }

    if (!deferGPU)
    {
        if (seg->_someWater)
        {
            Ref<TexMaterial> mat = GTexMaterialBank.New("#Water");
            for (int i = 0; i < seg->_wTable.NSections(); i++)
                seg->_wTable.GetSection(i).surfMat = mat;
        }
        {
            Ref<TexMaterial> mat = GTexMaterialBank.New("#Terrain");
            for (int i = 0; i < seg->_table.NSections(); i++)
                seg->_table.GetSection(i).surfMat = mat;
        }
    }

    // verify seg _table vertices
    seg->_valid = true;
    seg->_rect = rect;
}

void Landscape::FinalizeSegmentGPU(LandSegment* seg)
{
    seg->_table.ConvertToVBuffer(VBBigDiscardable);
    seg->_wTable.ConvertToVBuffer(VBBigDiscardable);

    if (seg->_someWater)
    {
        Ref<TexMaterial> mat = GTexMaterialBank.New("#Water");
        for (int i = 0; i < seg->_wTable.NSections(); i++)
            seg->_wTable.GetSection(i).surfMat = mat;
    }
    {
        Ref<TexMaterial> mat = GTexMaterialBank.New("#Terrain");
        for (int i = 0; i < seg->_table.NSections(); i++)
            seg->_table.GetSection(i).surfMat = mat;
    }
}

#define LANDDRAW 1

class AnimatorDefault : public IAnimator
{
    Color _color;

  public:
    AnimatorDefault(Color color = Color(1, 1, 1));

    // both Transform and Light should include
    // any animation required on position and normals
    void DoTransform(TLVertexTable& dst, const Shape& src, const Matrix4& posView, int from, int to) const override;
    // when Light is called TLVertexTable already contains
    void DoLight(TLVertexTable& dst, const Shape& src, const Matrix4& worldToModel, const LightList& lights, int spec,
                 int material, int from, int to) const override;
    // get material with given index
    void GetMaterial(TLMaterial& mat, int index) const override;
    bool GetAnimated(const Shape& src) const override { return false; }
};

AnimatorDefault::AnimatorDefault(Color color) : _color(color) {}

void AnimatorDefault::DoTransform(TLVertexTable& dst, const Shape& src, const Matrix4& posView, int from, int to) const
{
    dst.DoTransformPoints(src, posView, from, to);
}
// when Light is called TLVertexTable already contains
void AnimatorDefault::DoLight(TLVertexTable& dst, const Shape& src, const Matrix4& worldToModel,
                              const LightList& lights, int spec, int material, int from, int to) const
{
    TLMaterial mat;
    GetMaterial(mat, material);
    mat.specFlags = spec;
    dst.DoMaterialLightingP(mat, worldToModel, lights, src, from, to);
}

#define TL_LANDSCAPE 1

// get material with given index
void AnimatorDefault::GetMaterial(TLMaterial& mat, int index) const
{
    ColorVal accom = GEngine->GetAccomodateEye();
    // distrubute by predefined materials
    CreateMaterial(mat, accom * _color, index);
}

class AnimatorAlpha : public AnimatorDefault
{
    float _alpha;

  public:
    void SetAlpha(float a) { _alpha = a; }
    void GetMaterial(TLMaterial& mat, int index) const override;
};

void AnimatorAlpha::GetMaterial(TLMaterial& mat, int index) const
{
    ColorVal accom = GEngine->GetAccomodateEye();
    // distrubute by predefined materials
    CreateMaterial(mat, accom, index);
    mat.ambient.SetA(_alpha);
    mat.diffuse.SetA(_alpha);
}

class AnimatorBrightness : public AnimatorDefault
{
    float _bright;

  public:
    void SetBrigth(float b) { _bright = b; }
    void GetMaterial(TLMaterial& mat, int index) const override;
};

void AnimatorBrightness::GetMaterial(TLMaterial& mat, int index) const
{
    ColorVal accom = GEngine->GetAccomodateEye() * _bright;
    // distrubute by predefined materials
    CreateMaterial(mat, accom, index);
}

static AnimatorDefault GAnimatorDefault;
static AnimatorDefault GAnimatorWater(Color(0.8, 0.8, 0.8));

void Landscape::DrawWater(const LandBegEnd& bigRect, Scene& scene)
{
    for (int x = bigRect.xBeg; x < bigRect.xEnd; x += LandSegmentSize)
    {
        for (int z = bigRect.zBeg; z < bigRect.zEnd; z += LandSegmentSize)
        {
            LandBegEnd sRect;
            sRect.xBeg = x, sRect.zBeg = z;
            sRect.xEnd = x + LandSegmentSize, sRect.zEnd = z + LandSegmentSize;
            // Ref<LandSegment> seg=GenerateSegment(sRect);
            Ref<LandSegment> seg = _segCache.Segment(this, sRect, Glob.time.toFloat());

            if (seg->_needsGPU)
            {
                FinalizeSegmentGPU(seg);
                seg->_needsGPU = false;
            }

            // draw all water segments
            if (seg->_someWater)
            {
                // after drawing landscape draw water

                Shape* shape = &seg->_wTable;
#if TL_LANDSCAPE
                if (ENGINE_CONFIG.enableHWTL && EnableHWTLState && GEngine->GetTL())
                {
                    Vector3Val bCenter = shape->BSphereCenter() + seg->Offset();
                    float bRadius = shape->BSphereRadius();

                    bool isClipped = scene.GetCamera()->IsClipped(bCenter, bRadius, 1);
                    if (!isClipped)
                    {
                        Vector3 offset = seg->Offset() + Vector3(0, _seaLevelWave, 0);
                        Matrix4 trans(MTranslation, offset);
                        Matrix4 iTrans(MTranslation, -offset);
                        // calculate z2

                        float z = scene.GetCamera()->Position().Distance(bCenter) - bRadius;
                        float z2 = Square(floatMax(z, 0));

                        // animate water texture
                        // water shape should have only one section
                        PoseidonAssert(shape->NSections() == 1);
                        ShapeSection& sec = shape->GetSection(0);
                        float texPhase = fastFmod(Glob.time.toFloat(), 2);
                        if (texPhase > 1.0f)
                        {
                            texPhase = 2 - texPhase;
                        }
                        sec.properties.AnimateTexture(texPhase);

                        shape->PrepareTextures(z2, 0);
                        LightList work(true);
                        const LightList& lights = scene.SelectLights(bCenter, bRadius, work);
                        shape->Draw(&GAnimatorWater, lights, ClipAll, 0, trans, iTrans);
                    }
                }
                else
                {
#endif

                    TLVertexTable tlTable;
                    Matrix4Val pointView = scene.ScaledInvTransform();
                    tlTable.DoTransformPoints(&GAnimatorWater, seg->_wTable, pointView);
                    DrawMesh(scene, tlTable, *shape, seg->Offset(), seg->_rect, true);
#if TL_LANDSCAPE
                }
#endif
            }
        }
    }
}

struct GroundLayerInfo
{
    Vector3 offset;
    float alpha1, alpha2, bright;
    bool isAlpha;

    GroundLayerInfo() : offset(VZero), alpha1(0), alpha2(1), isAlpha(false) {}
    GroundLayerInfo(float y, float a1, float b = 1, float a2 = 1)
        : offset(0, y, 0), bright(b), alpha1(a1 * 0.5 + 0.5), alpha2(a2 * 0.5), isAlpha(true)
    {
    }
};
const static GroundLayerInfo GroundLayersMax[] = {
#define MAX_A 1.5f
#define MAX_S 1.5f
    GroundLayerInfo(0.01f * MAX_S, -0.10f, 0.5f, MAX_A), GroundLayerInfo(0.02f * MAX_S, -0.20f, 0.6f, MAX_A),
    GroundLayerInfo(0.03f * MAX_S, -0.30f, 0.6f, MAX_A), GroundLayerInfo(0.04f * MAX_S, -0.40f, 0.6f, MAX_A),
    GroundLayerInfo(0.05f * MAX_S, -0.50f, 0.7f, MAX_A), GroundLayerInfo(0.06f * MAX_S, -0.60f, 0.8f, MAX_A),
    GroundLayerInfo(0.07f * MAX_S, -0.70f, 0.9f, MAX_A), GroundLayerInfo(0.08f * MAX_S, -0.80f, 0.9f, MAX_A),
    GroundLayerInfo(0.09f * MAX_S, -0.90f, 1.0f, MAX_A),
};
const static GroundLayerInfo GroundLayersMid[] = {
#define MID_A 2.0f
    GroundLayerInfo(0.01f, -0.10f, 0.50f, MID_A), GroundLayerInfo(0.03f, -0.30f, 0.55f, MID_A),
    GroundLayerInfo(0.05f, -0.50f, 0.70f, MID_A), GroundLayerInfo(0.07f, -0.70f, 0.90f, MID_A),
    GroundLayerInfo(0.09f, -0.90f, 1.00f, MID_A),
};
const static GroundLayerInfo GroundLayersMin[] = {
#define MID_A 2.0f
    GroundLayerInfo(0.01f, -0.10f, 0.60f, MID_A),
    GroundLayerInfo(0.05f, -0.50f, 0.75f, MID_A),
    GroundLayerInfo(0.09f, -0.90f, 1.00f, MID_A),
};

struct GrassMode
{
    const GroundLayerInfo* layers;
    int nLayers;
    const char* name;
};

GrassMode GrassModes[] = {
#define G_MODE(a) {GroundLayers##a, sizeof(GroundLayers##a) / sizeof(*GroundLayers##a), #a}
    G_MODE(Min), G_MODE(Mid), G_MODE(Max)
#undef G_MODE
};

const int NGrassModes = sizeof(GrassModes) / sizeof(*GrassModes);

void Landscape::DrawGround(const LandBegEnd& bigRect, Scene& scene, const GroundLayerInfo& layer)
{
    auto t0 = TerrainProfile::Now();
    for (int x = bigRect.xBeg; x < bigRect.xEnd; x += LandSegmentSize)
    {
        for (int z = bigRect.zBeg; z < bigRect.zEnd; z += LandSegmentSize)
        {
            LandBegEnd sRect;
            sRect.xBeg = x, sRect.zBeg = z;
            sRect.xEnd = x + LandSegmentSize, sRect.zEnd = z + LandSegmentSize;
            // Ref<LandSegment> seg=GenerateSegment(sRect);
            Ref<LandSegment> seg = _segCache.Segment(this, sRect, Glob.time.toFloat());
            if (seg->_needsGPU)
            {
                FinalizeSegmentGPU(seg);
                seg->_needsGPU = false;
            }

            {
                if (seg->_someWater && seg->_seaLevel != _seaLevelWave)
                {
                    // change level of all water points
                    // for TL: adjust matrix

                    seg->_seaLevel = _seaLevelWave;
                    VertexTable& table = seg->_wTable;
                    for (int i = 0; i < table.NPos(); i++)
                    {
                        table.SetPos(i)[1] = _seaLevelWave;
                    }
                }
            }

            // if( !_cache || !_cache->ValidFor(rect) )

            if (!seg->_onlyWater)
            {
                Shape* shape = &seg->_table;

#if TL_LANDSCAPE
                if (ENGINE_CONFIG.enableHWTL && EnableHWTLState)
                {
                    Vector3Val bCenter = shape->BSphereCenter() + seg->Offset();
                    float bRadius = shape->BSphereRadius();

                    if (!scene.GetCamera()->IsClipped(bCenter, bRadius, 1))
                    {
                        // select only most important light
                        LightList work(true);
                        const LightList& lights = scene.SelectLights(bCenter, bRadius, work);
                        float z = scene.GetCamera()->Position().Distance(bCenter) - bRadius;
                        float z2 = Square(floatMax(z, 0));
                        shape->PrepareTextures(z2, 0);

                        // draw several layers, each with lesser alpha
                        // AnimatorAlpha alpha;
                        // alpha.SetAlpha(layer.alpha);
                        Vector3 offset = seg->Offset() + layer.offset;

                        Matrix4 trans(MTranslation, offset);
                        Matrix4 iTrans(MTranslation, -offset);
                        const int alphaSpec = ::IsAlpha | ::NoZWrite | ::GrassTexture;
                        IAnimator* anim = &GAnimatorDefault;
                        AnimatorBrightness animLand;
                        if (layer.isAlpha)
                        {
                            GEngine->SetGrassParams(layer.alpha1, layer.alpha2,
                                                    scene.GetCamera()->GetAdditionalClippingFar());
                            shape->OrSpecial(alphaSpec);
                            anim = &animLand;
                            animLand.SetBrigth(layer.bright);
                        }
#if 1
                        shape->Draw(anim, lights, ClipAll, 0, trans, iTrans);
#endif
                        if (layer.isAlpha)
                        {
                            shape->AndSpecial(~alphaSpec);
                        }
                    }
                }
                else
                {
#endif
                    TLVertexTable tlTable;

                    Matrix4Val pointView = scene.ScaledInvTransform();
                    tlTable.DoTransformPoints(&GAnimatorDefault, seg->_table, pointView);
                    // landscape always uses world space coordinates

                    DrawMesh(scene, tlTable, *shape, seg->Offset(), sRect, false);
#if TL_LANDSCAPE
                }
#endif
            }
        }
    }
    GTerrainProfile.drawGroundCycles += (TerrainProfile::Now() - t0);
}

void Landscape::DrawHorizont(Scene& scene)
{
    // Optional, like the other sky slots (see DrawSky's null guards above) --
    // LOG_WARN("...has no model configured", Landscape.cpp:64) means a
    // landscape config can omit "horizont", in which case _horizontObject is
    // never constructed. Every line below depends on it, so a single early
    // return covers the whole function instead of guarding each use.
    if (!_horizontObject)
        return;

    GEngine->EnableReorderQueues(false);
    GEngine->FlushQueues();

    // draw landscape background polygon
    LODShape* horShape = _horizontObject->GetShape();
    Shape* horShape0 = horShape->Level(0);
    // 8 points, point layout:
    // 4    5
    // 23  67
    // 0    1
    PoseidonAssert(horShape0->NVertex() == 8);
    // we want to adjust points 0 and 1

    Vector3 direction = scene.GetCamera()->Direction();
    Vector3 horPos = scene.GetCamera()->Position(); //+direction*backZ;

    direction[1] = 0;
    _horizontObject->SetOrient(direction, VUp);

    Vector3Val bc = horShape->BoundingCenter();
    horPos += _horizontObject->DirectionModelToWorld(bc);
    _horizontObject->SetPosition(horPos);

    float extendDown = floatMax(-direction.Y(), 0) + 0.02;

    const Camera* camera = scene.GetCamera();

    float cameraAbove = horPos.Y() - GetSeaLevel();
    float backZ = scene.GetCamera()->ClipFar() * 0.99f;
    float posX = backZ * scene.GetCamera()->Left() * 2.0f;
    float negX = -posX;
    float topY = backZ * 0.06f;
    float midY = backZ * 0.03f;
    float botY = -cameraAbove - extendDown * backZ;

    // float botSize2 = Square(negX)+Square(botY)+Square(backZ);
    // float botCoef = InvSqrt(botSize2)*backZ;

    // calculate (estimate) inverse projection
    // of camera corners to far clipping frame

    // far clipping plane world space equation is
    Plane farPlane(camera->Direction(), camera->Position() + camera->Direction() * backZ);

    Matrix4 invTrans = _horizontObject->GetInvTransform();
    farPlane.Transform(*_horizontObject, invTrans);

    // we need to convert this to model space
    // (offset (0,0,0), orientation change)

    horShape0->SetPos(0) = Vector3(negX, botY, backZ); //*botCoef;
    horShape0->SetPos(1) = Vector3(posX, botY, backZ); //*botCoef;

    horShape0->SetPos(2) = Vector3(negX, midY, backZ);
    horShape0->SetPos(3) = Vector3(negX, midY, backZ);

    horShape0->SetPos(4) = Vector3(negX, topY, backZ);
    horShape0->SetPos(5) = Vector3(posX, topY, backZ);

    horShape0->SetPos(6) = Vector3(posX, midY, backZ);
    horShape0->SetPos(7) = Vector3(posX, midY, backZ);

    // note: pos 2..7 must keep y coordinate even after projection to far plane

    // camera (0,0,0) converted to model coordinates is
    Vector3 camZero = invTrans.FastTransform(camera->Position());
    float camZeroDistance = farPlane.Distance(camZero);
    for (int i = 0; i < 2; i++)
    {
        Vector3 pos = horShape0->Pos(i);
        // we need to calculate intersection of line camZero..pos
        // with far plane
        // we have far plane converted to model coordinates
        float posDistance = farPlane.Distance(pos);
        if (fabs(posDistance - camZeroDistance) < 1e-6)
        {
            // no intersection - we have no way to calculate it
            Log("No horizont intersection %d", i);
            continue;
        }
        float t = posDistance / (posDistance - camZeroDistance);
        Vector3 isect = pos * (1 - t) + camZero * t;
        // verify - project to camera space
        horShape0->SetPos(i) = isect;
    }

    horShape->SetAutoCenter(false);
    horShape->CalculateMinMax(true);

    //_horizontObject->SetScale(scale);
    // no LOD for horizont

    float horizBorder = backZ * 0.025f;
    scene.GetCamera()->SetUserClipPars(VUp, -GetSeaLevel() + horizBorder);
    //_horizontObject->Draw(0,ClipAll&~ClipBack|ClipUser0,*_horizontObject);
    _horizontObject->Draw(0, ClipAll & ~ClipBack | ClipUser0, *_horizontObject);
    scene.GetCamera()->CancelUserClip();

    GEngine->FlushQueues();
}

#if _ENABLE_CHEATS
} // namespace Poseidon
#include <Poseidon/Input/InputSubsystem.hpp>
namespace Poseidon
{

#endif

void Landscape::DrawRect(Scene& scene, const LandBegEnd& bigRect)
{
    Camera& camera = *scene.GetCamera();
    (void)camera; // May be used in debug builds or future code

    // Matrix4 normalView(MIdentity);
    if (bigRect.xEnd <= bigRect.xBeg)
    {
        return;
    }
    if (bigRect.zEnd <= bigRect.zBeg)
    {
        return;
    }
// if cache was reconstructed too often, do not try to use it
// it would degrade performance

// some loop invariant expression are precalculated here

// draw all reflected and non-reflected segments
// split large rectangle into small segments
#if LANDDRAW
#if _ENABLE_CHEATS
    if (!CHECK_DIAG(DETransparent) || !CHECK_DIAG(DEForce))
#endif
    {
        GEngine->EnableReorderQueues(true);

        float nightEye = ((1 - scene.MainLight()->Diffuse().R()) * scene.MainLight()->NightEffect());

        GEngine->EnableNightEye(nightEye);

        GroundLayerInfo opaqueLayer;
        DrawGround(bigRect, scene, opaqueLayer);
    }
#endif

    // camera.SetUserClipPars(1,VUp,3.0);
    for (int i = 0; i < _arrows.Size(); i++)
    {
        scene.ObjectForDrawing(_arrows[i]);
    }
    // scene.GetCamera()->SetUserClip(1);

#if LANDDRAW
#if _ENABLE_CHEATS
    if (!CHECK_DIAG(DETransparent) || !CHECK_DIAG(DEForce))
#endif
        if (!ENGINE_CONFIG.noLandscape)
        {
            GEngine->EnableReorderQueues(true);
            DrawWater(bigRect, scene);

            DrawHorizont(scene);
        }
#endif

    GEngine->FlushQueues();
    Dev::GFrameProfiler().Mark(Dev::FrameProfiler::PhaseDrawLandGround);
    GEngine->EnableReorderQueues(true);
    // draw non-alpha objects
    scene.DrawObjectsAndShadowsPass1();
    Dev::GFrameProfiler().Mark(Dev::FrameProfiler::PhaseDrawLandObjects);

#if LANDDRAW
#if _ENABLE_CHEATS
    static int grassMode = 2;
    if (GEngine->CanGrass() && InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_MINUS))
    {
        grassMode++;
        if (grassMode > NGrassModes)
            grassMode = 0;
        if (grassMode > 0)
        {
            GlobalShowMessage(500, "Grass mode %s", GrassModes[grassMode - 1].name);
        }
        else
        {
            GlobalShowMessage(500, "Grass off", grassMode);
        }
    }
    if (grassMode > 0 && GEngine->CanGrass())
    {
// calculate pixel size of grass
#if 2
        // no joy with clipping - there is no effective way to clip on GeForce3
        float grassLength = 0.16f;
        Matrix4Val project = camera.Projection();
        float minGrassPixelSize = 1;
        float maxDist = -project(1, 1) * grassLength * camera.InvTop() / minGrassPixelSize;

        camera.SetAdditionalClipping(0, maxDist);
#endif

        const GrassMode& mode = GrassModes[grassMode - 1];
        for (int i = 0; i < mode.nLayers; i++)
        {
            DrawGround(bigRect, scene, mode.layers[i]);
        }
        // reset additional clipping to infinity
        camera.SetAdditionalClipping(0, 100000);
    }
#endif
#endif
    GEngine->FlushQueues();

    // draw alpha objects and shadows
    scene.DrawObjectsAndShadowsPass2();

    GEngine->EnableReorderQueues(false);
    // clear any outstanding arrows (Buldozer ONLY)
    _arrows.Clear();
}

void Landscape::CalculBoundingRect(LandBegEnd& res, const Camera& camera, float dist, float grid)
{
    Point3 corner[8];
    float cLeftFar = camera.Left() * dist;
    float cTopFar = camera.Top() * dist;
    float cFar = dist;

    Matrix4Val invView = camera.Transform();

    // float invSize=1.0;
    //  middle point
    corner[0] = camera.Position();
    corner[1].SetFastTransform(invView, Point3(0, 0, cFar));
    corner[2].SetFastTransform(invView, Vector3(-cLeftFar, -cTopFar, cFar));
    corner[3].SetFastTransform(invView, Vector3(+cLeftFar, -cTopFar, cFar));
    corner[4].SetFastTransform(invView, Vector3(-cLeftFar, +cTopFar, cFar));
    corner[5].SetFastTransform(invView, Vector3(+cLeftFar, +cTopFar, cFar));

    Coord xMin, xMax, zMin, zMax;
    // always include camera position
    xMin = xMax = corner[0].X();
    zMin = zMax = corner[0].Z();
    for (int i = 1; i < 6; i++)
    {
        Coord x = corner[i].X(), z = corner[i].Z();
        saturateMin(xMin, x);
        saturateMin(zMin, z);
        saturateMax(xMax, x);
        saturateMax(zMax, z);
    }
    float iGrid = 1.0f / grid;
    res.xBeg = toIntFloor(xMin * iGrid) - 1; // leave some reserve
    res.zBeg = toIntFloor(zMin * iGrid) - 1;
    res.xEnd = toIntCeil(xMax * iGrid) + 1;
    res.zEnd = toIntCeil(zMax * iGrid) + 1;
}

// draw sky using pre-builded cover
// load cover definition

void Landscape::DrawSky(Scene& scene)
{
    // if( !ENGINE_CONFIG.background ) return;

    Camera& camera = *scene.GetCamera();

    // calculate sun position
    LightSun* sun = scene.MainLight();
    Vector3Val skyPosition = camera.Position();

    // Each of these is independently optional: LOG_WARN("...has no model
    // configured", Landscape.cpp:64) already tells us a landscape config can
    // omit any sky slot, in which case the corresponding object is never
    // constructed (stays null) -- DrawClouds already guards _cloudObj[i] the
    // same way (LandscapeRender.cpp:1795), this just extends the same
    // defensive pattern to sky/stars/sun/moon instead of assuming all four
    // are always present.
    if (_skyObject)
        _skyObject->SetPosition(skyPosition + _skyObject->GetShape()->BoundingCenter());
    if (_starsObject)
    {
        _starsObject->SetPosition(skyPosition + _starsObject->GetShape()->BoundingCenter());
        _starsObject->SetOrientation(sun->StarsOrientation());
    }
    const float sunScale = 120.0 / 12000;
    if (_sunObject)
    {
        Vector3 relPos = sun->SunDirection() * 12000 * sunScale;
        Vector3 sunPosition = camera.Position() - relPos;
        _sunObject->SetScale(sunScale);
        _sunObject->SetPosition(sunPosition);

        // LOG_DEBUG(World, "Sun rel pos {:.2f},{:.2f},{:.2f}",relPos[0],relPos[1],relPos[2]);
    }
    if (_moonObject)
    {
        Point3 moonPosition = camera.Position() - sun->MoonDirection() * 12000 * sunScale;
        _moonObject->SetPosition(moonPosition);
        Matrix3 moonOrient;
        moonOrient.SetDirectionAndUp(-sun->MoonDirection(), sun->MoonDirectionUp());
        _moonObject->SetOrientation(moonOrient);
        _moonObject->SetScale(sunScale);
        Shape* shape = _moonObject->GetShape()->LevelOpaque(0);
        if (shape->NFaces() >= 2)
        {
            shape->FaceIndexed(1).AnimateTexture(sun->MoonPhase());
        }
        if (shape->NSections() >= 2)
        {
            shape->GetSection(1).properties.AnimateTexture(sun->MoonPhase());
        }
    }

    float clipLevel = skyPosition.Y();
    scene.GetCamera()->SetUserClipPars(VUp, -clipLevel);

    if (_skyObject)
        _skyObject->Draw(0, ClipAll & ~ClipBack | ClipUser0, *_skyObject);
    if (_starsObject)
    {
        float starsVisibility = (
            // see TLVertexMesh::DoStarLighting
            // overcast limitation
            (1.5 * SkyThrough() - 0.5) *
            // daytime limitation
            sun->StarsVisibility());
        if (starsVisibility >= 0.1)
        {
            _starsObject->DrawPoints(0, ClipAll & ~ClipBack | ClipUser0, *_starsObject);
        }
    }
    scene.GetCamera()->CancelUserClip();

    if (_sunObject)
        _sunObject->Draw(0, ClipAll & ~ClipBack, *_sunObject);
    if (_moonObject)
        _moonObject->Draw(0, ClipAll & ~ClipBack, *_moonObject);
}

// there are three separate clouds levels
#define SKY_LEVELS 3

#define SKY_Z (20000)

#define SKY_GRID (6000.0f)

struct CloudInfo
{
    float posX, posZ, azimut;
};

void Landscape::DrawClouds(Scene& scene)
{
    const Camera& camera = *scene.GetCamera();

    // Matrix4Val invView=camera.Transform();

    // Vector3Val pos=camera.Position();
    LandBegEnd skyBegEnd;
    CalculBoundingRect(skyBegEnd, camera, SKY_Z, SKY_GRID);

    // precalculate variables for fast clipping

    // try to clip bounding sphere
    // Matrix4Val centerView=scene.ScaledInvTransform();

    int skyLevel;
    // Ref<Object> cloudObj[N_CLOUDS];
    int i;
    for (i = 0; i < N_CLOUDS; i++)
    {
        if (!_cloudObj[i])
        {
            _cloudObj[i] = new ObjectPlain(GScene->Preloaded(CloudShapes[i]), -1);
        }
    }
    // scene.GetCamera()->SetUserClip(1);
    AUTO_STATIC_ARRAY(CloudInfo, clouds0, 1024);
    AUTO_STATIC_ARRAY(CloudInfo, clouds1, 1024);
    AUTO_STATIC_ARRAY(CloudInfo, clouds2, 1024);
    AUTO_STATIC_ARRAY(CloudInfo, clouds3, 1024);
    typedef StaticArrayAuto<CloudInfo> CloudArray;
    CloudArray* clouds[N_CLOUDS] = {&clouds0, &clouds1, &clouds2, &clouds3};
    for (skyLevel = 0; skyLevel < SKY_LEVELS; skyLevel++)
    {
        clouds0.Resize(0);
        clouds1.Resize(0);
        clouds2.Resize(0);
        clouds3.Resize(0);
        const static float skyY[SKY_LEVELS] = {6000, 4000, 2000};
#define SP (2.0f)
        const static float skyXSpeed[SKY_LEVELS] = {+0.020 * SP, +0.012 * SP, +0.010 * SP};
        const static float skyZSpeed[SKY_LEVELS] = {+0.004 * SP, -0.002 * SP, -0.004 * SP};
        int xx, zz;

        float cPos = CloudsPosition();
        float xMove = cPos * skyXSpeed[skyLevel];
        float zMove = cPos * skyZSpeed[skyLevel];

        float height = CloudsBrightness();
        saturate(height, 0.6, 1);

        int xMoveInt = toIntFloor(xMove);
        int zMoveInt = toIntFloor(zMove);
        xMove -= xMoveInt;
        zMove -= zMoveInt;

        xMove *= SKY_GRID;
        zMove *= SKY_GRID;

        float posY = skyY[skyLevel] * height;

        for (zz = skyBegEnd.zBeg; zz <= skyBegEnd.zEnd; zz++)
        {
            for (xx = skyBegEnd.xBeg; xx <= skyBegEnd.xEnd; xx++)
            {
                // preloaded objects _cloud[]
                int seedXZ = _randGen.GetSeed(xx - xMoveInt, zz - zMoveInt, skyLevel);
                float isHereF = _randGen.RandomValue(seedXZ);
                // RandBegin(xx-xMoveInt,zz-zMoveInt,skyLevel);
                int isHere = toIntFloor(isHereF * 8);
                if (isHere >= N_CLOUDS)
                {
                    continue;
                }
                CloudArray& array = *clouds[isHere];
                CloudInfo& info = array.Append();

                float xOffset = xMove + _randGen.RandomValue(seedXZ + 1) * (SKY_GRID * 0.5);
                float zOffset = zMove + _randGen.RandomValue(seedXZ + 2) * (SKY_GRID * 0.5);

                info.posX = xx * SKY_GRID + xOffset;
                info.posZ = zz * SKY_GRID + zOffset;
                info.azimut = _randGen.RandomValue(seedXZ + 3) * (2 * H_PI / 2);
                // Point3 pos=Vector3(xx*SKY_GRID+xOffset,skyY[skyLevel]*height,zz*SKY_GRID+zOffset);
            }
        }
        for (int isHere = 0; isHere < N_CLOUDS; isHere++)
        {
            Vector3Val camPos = camera.Position();
            Object* object = _cloudObj[isHere];
            if (!object)
            {
                continue;
            }

            CloudArray& cloudsType = *clouds[isHere];
            for (int i = 0; i < cloudsType.Size(); i++)
            {
                const CloudInfo& info = cloudsType[i];

                Vector3 pos(info.posX, posY, info.posZ);
                Matrix3 orient(MRotationY, info.azimut);
                orient *= CloudScale;

                // pretend clouds are much further
                // by adding part of camera position

                // pos = (pos-camPos)*CloudScale+camPos;
                pos = pos * CloudScale + (1 - CloudScale) * camPos;

                object->SetOrient(orient);
                object->SetPosition(pos);

                // note: radius is constant for all clouds of given type
                float radius = object->GetRadius();
                if ((camera.IsClipped(pos, radius, 1) & ~ClipBack) == ClipNone)
                {
                    // no LOD for clouds
                    object->Draw(0, ClipAll & ~ClipBack, *object);
                }
            }
        }
    }
}

void Landscape::Draw(Scene& scene)
{
    {
        Camera& camera = *scene.GetCamera();

        float oldNear = camera.ClipNear();
        float oldFar = camera.ClipFar();
        // camera.SetClipRange(10,1500);
        camera.SetClipRange(10, 4000);

        DrawSky(scene);
        DrawClouds(scene);
        GEngine->FlushQueues();

        camera.SetClipRange(oldNear, oldFar);
    }

    // project points limiting viewpoint
    LandBegEnd begEnd;
    CalculBoundingRect(begEnd, *scene.GetCamera(), scene.GetFogMaxRange(), _landGrid);

    // use only rough rectangles - no generalization
    begEnd.xBeg &= ~(LandSegmentSize - 1);
    begEnd.zBeg &= ~(LandSegmentSize - 1);
    begEnd.xEnd = (begEnd.xEnd + (LandSegmentSize - 1)) & ~(LandSegmentSize - 1);
    begEnd.zEnd = (begEnd.zEnd + (LandSegmentSize - 1)) & ~(LandSegmentSize - 1);
    // detect overflow
    // limits correspond to 13000 km
    // any larger coordinates must mean some kind of error

    const int maxCoord = 0x40000;
    const int minCoord = -0x40000;
    if (begEnd.xBeg > maxCoord || begEnd.xBeg < minCoord || begEnd.zBeg > maxCoord || begEnd.zBeg < minCoord)
    {
        Fail("Ground drawing out of valid range");
        RptF("  Rect %d,%d..%d,%d", begEnd.xBeg, begEnd.zBeg, begEnd.xEnd, begEnd.zEnd);
        return;
    }
    if (begEnd.xEnd - begEnd.xBeg > 0x1000 || begEnd.zEnd - begEnd.zBeg > 0x1000)
    {
        Fail("Ground drawing segment too big");
        RptF("  Rect %d,%d..%d,%d", begEnd.xBeg, begEnd.zBeg, begEnd.xEnd, begEnd.zEnd);
        RptF("  Fog max range %.1f", scene.GetFogMaxRange());
        return;
    }

    int requiredCacheSize =
        (begEnd.xEnd - begEnd.xBeg) * (begEnd.zEnd - begEnd.zBeg) / (LandSegmentSize * LandSegmentSize);
    int cacheSize = CalculateCacheSize(ENGINE_CONFIG.horizontZ, GetInvLandGrid());
    saturateMax(cacheSize, requiredCacheSize);
    _segCache._cache.SetMaxN(cacheSize);

    DrawRect(scene, begEnd);
}

Object* Landscape::GetObject(int id) const
{
    if (_objectIds.Size() <= 0)
    {
        LOG_ERROR(World, "No Object ID cache - performing slow search");
        return FindObjectNC(id);
    }
    if (id < 0 || id >= _objectIds.Size())
    {
        return nullptr;
    }
    return _objectIds[id];
}

Texture* Landscape::GetTexture(int id) const
{
    if (id < 0 || id >= _texture.Size())
    {
        return nullptr;
    }
    return _texture[id];
}
} // namespace Poseidon
