#include <PoseidonMTL/VertexBufferMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

#include <cstdio>

namespace Poseidon
{

VertexBufferMTL::VertexBufferMTL(EngineMTLBootstrap& bootstrap) : _bootstrap(bootstrap) {}

VertexBufferMTL::~VertexBufferMTL()
{
    if (_vbHandle != 0)
        _bootstrap.DestroyMeshBuffer(_vbHandle);
    if (_ibHandle != 0)
        _bootstrap.DestroyMeshBuffer(_ibHandle);
}

void VertexBufferMTL::CopyVertices(const Shape& src)
{
    if (_vertexCount <= 0)
        return;

    std::vector<VertexMeshMTL> verts(static_cast<size_t>(_vertexCount));
    for (int i = 0; i < _vertexCount; i++)
    {
        Vector3Val pos = src.Pos(i);
        Vector3Val norm = src.Norm(i);
        const UVPair& uv = src.UV(i);
        VertexMeshMTL& v = verts[static_cast<size_t>(i)];
        v.px = static_cast<float>(pos.X());
        v.py = static_cast<float>(pos.Y());
        v.pz = static_cast<float>(pos.Z());
        // Negated to match D3D convention -- same as GL33's SVertex upload
        // (EngineGL33_VertexBuffer.cpp:100).
        v.nx = static_cast<float>(-norm.X());
        v.ny = static_cast<float>(-norm.Y());
        v.nz = static_cast<float>(-norm.Z());
        v.u = uv.u;
        v.v = uv.v;
    }

    const size_t byteSize = verts.size() * sizeof(VertexMeshMTL);
    char label[64];
    std::snprintf(label, sizeof(label), "VB %s nv=%d ns=%d", _dynamic ? "dyn" : "static", _vertexCount,
                  src.NSections());
    if (_vbHandle == 0)
    {
        _vbHandle = _bootstrap.CreateMeshBuffer(verts.data(), byteSize, _dynamic, label);
        return;
    }

    // One VertexBufferMTL is shared across every object instance that uses
    // the same cached Shape resource (e.g. two soldiers wearing the same
    // model) -- confirmed empirically (logging showed the same handle
    // Update()'d twice in one frame). Metal only *records* draw calls
    // during the frame; nothing executes until commit(), so overwriting
    // the buffer in place here would corrupt an earlier instance's
    // already-recorded-but-not-yet-GPU-executed draw from earlier this
    // same frame -- by the time the GPU actually ran, both draws would
    // read whichever instance's data was written last. Allocate fresh
    // each repeat call instead and defer-destroy the old one (mirrors
    // GL33's MapDynamicWriteInvalidate orphan-and-get-new-storage pattern,
    // which exists in EngineGL33_VertexBuffer.cpp for exactly this hazard).
    int newHandle = _bootstrap.CreateMeshBuffer(verts.data(), byteSize, _dynamic, label);
    if (newHandle == 0)
        return; // allocation failed -- keep drawing with the stale buffer rather than nothing
    _bootstrap.DestroyMeshBufferDeferred(_vbHandle);
    _vbHandle = newHandle;
}

bool VertexBufferMTL::Init(const Shape& src, VBType type)
{
    if (src.NVertex() <= 0)
        return false;

    _dynamic = (type == VBDynamic || type == VBSmallDiscardable);
    _vertexCount = src.NVertex();
    CopyVertices(src);
    if (_vbHandle == 0)
        return false;

    // Fan-triangulate each face (N-gon -> N-2 triangles), same as GL33
    // (which asserts poly.N() >= 3 -- a degenerate face would make this
    // negative, corrupting totalIndices and every section's index range
    // computed below it). Checked empirically against real game models
    // (logged for one investigation, zero hits) -- skip defensively rather
    // than assert, since release builds have no assert to catch it anyway.
    std::vector<uint16_t> indices;
    int totalIndices = 0;
    for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
    {
        const Poly& poly = src.Face(o);
        if (poly.N() < 3)
            continue;
        totalIndices += (poly.N() - 2) * 3;
    }
    if (totalIndices <= 0)
        return true; // no faces -- vertex-only buffer is still usable

    indices.reserve(static_cast<size_t>(totalIndices));
    for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
    {
        const Poly& poly = src.Face(o);
        for (int i = 2; i < poly.N(); i++)
        {
            indices.push_back(static_cast<uint16_t>(poly.GetVertex(0)));
            indices.push_back(static_cast<uint16_t>(poly.GetVertex(i - 1)));
            indices.push_back(static_cast<uint16_t>(poly.GetVertex(i)));
        }
    }

    _ibHandle = _bootstrap.CreateMeshBuffer(indices.data(), indices.size() * sizeof(uint16_t), false);
    if (_ibHandle == 0)
        return false;

    // Per-section index ranges, same scan as VertexBufferGL33::Init.
    _sections.resize(static_cast<size_t>(src.NSections()));
    int start = 0;
    for (int i = 0; i < src.NSections(); i++)
    {
        const ShapeSection& sec = src.GetSection(i);
        int size = 0;
        for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
        {
            const Poly& face = src.Face(o);
            if (face.N() < 3)
                continue; // see the matching guard above -- keeps this section's range consistent with it
            size += (face.N() - 2) * 3;
        }
        _sections[static_cast<size_t>(i)].beg = start;
        _sections[static_cast<size_t>(i)].end = start + size;
        start += size;
    }

    return true;
}

void VertexBufferMTL::Update(const Shape& src, bool dynamic)
{
    if (_dynamic || dynamic || bufferDirty)
    {
        CopyVertices(src);
        bufferDirty = false;
    }
}

bool VertexBufferMTL::ResolveRange(int beg, int end, int& outFirstIndex, int& outIndexCount) const
{
    if (_sections.empty() || beg < 0 || end <= beg || end > static_cast<int>(_sections.size()))
        return false;

    const SectionInfo& siBeg = _sections[static_cast<size_t>(beg)];
    const SectionInfo& siEnd = _sections[static_cast<size_t>(end - 1)];
    const int indexCount = siEnd.end - siBeg.beg;
    if (indexCount <= 0)
        return false;

    outFirstIndex = siBeg.beg;
    outIndexCount = indexCount;
    return true;
}

} // namespace Poseidon
