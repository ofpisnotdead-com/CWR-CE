#include <PoseidonMTL/VertexBufferMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>

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
    if (_vbHandle == 0)
        _vbHandle = _bootstrap.CreateMeshBuffer(verts.data(), byteSize, _dynamic);
    else
        _bootstrap.UpdateMeshBuffer(_vbHandle, verts.data(), byteSize);
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

    // Fan-triangulate each face (N-gon -> N-2 triangles), same as GL33.
    std::vector<uint16_t> indices;
    int totalIndices = 0;
    for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
    {
        const Poly& poly = src.Face(o);
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
