#pragma once

#include <Poseidon/Graphics/Rendering/Primitives/Vertex.hpp>
#include <vector>

namespace Poseidon
{

class EngineMTLBootstrap;
class Shape;

// Mirrors VertexBufferGL33 (engine/PoseidonGL33/EngineGL33_VertexBuffer.cpp):
// a self-contained vertex+index GPU buffer pair per Shape, built once and
// (for animated meshes) refreshed in place. Holds opaque mesh-buffer handles
// from EngineMTLBootstrap rather than MTL::Buffer* directly -- this header is
// included from EngineMTL.cpp/.hpp, which pulls in Poseidon's core headers
// and therefore cannot also include metal-cpp (see EngineMTLBootstrap.hpp).
class VertexBufferMTL : public VertexBuffer
{
  public:
    explicit VertexBufferMTL(EngineMTLBootstrap& bootstrap);
    ~VertexBufferMTL() override;

    bool Init(const Shape& src, VBType type);
    void Update(const Shape& src, bool dynamic) override;

    // Resolves the contiguous index range spanning sections [beg, end) --
    // same lookup as VertexBufferGL33::DrawSectionTL's caller does on its
    // own _sections array.
    bool ResolveRange(int beg, int end, int& outFirstIndex, int& outIndexCount) const;

    int VertexBufferHandle() const { return _vbHandle; }
    int IndexBufferHandle() const { return _ibHandle; }

  private:
    struct SectionInfo
    {
        int beg, end; // index-element range, like GL33's VBSectionInfo
    };

    EngineMTLBootstrap& _bootstrap;
    int _vbHandle = 0;
    int _ibHandle = 0;
    bool _dynamic = false;
    int _vertexCount = 0;
    std::vector<SectionInfo> _sections;

    void CopyVertices(const Shape& src);
};

} // namespace Poseidon
