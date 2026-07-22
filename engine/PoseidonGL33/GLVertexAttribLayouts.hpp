#pragma once

#include <glad/gl.h>

#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <PoseidonGL33/EngineGL33.hpp> // SVertex

#include <cstddef>

// Vertex attribute layout helpers for the two vertex types the
// GL33 backend uses:
//
//   TLVertex — screen-space, fully pre-transformed (pos, rhw, color,
//              specular, uv0, uv1).  Read by VSScreen.
//
//   SVertex  — 3D mesh (pos, norm, uv).  Read by VSTransform from
//              the SAME underlying byte buffer the TLVertex layout
//              uses for VSScreen — different layouts share the
//              physical memory; the GPU reinterprets bytes based
//              on the active VAO's attribute table.  When VSScreen-
//              shaped data lives in the buffer, the VSTransform
//              read produces "garbage" for the normal slot (it
//              reads the rhw+color+specular bytes) which is the
//              expected behaviour matching the D3D11 backend.
//
// Centralising both layouts here means:
//   - `offsetof(...)` is used everywhere (no hardcoded byte
//     offsets that drift if a field is added),
//   - the two VAO setups in `CreateVB` and any per-mesh VAO
//     setup share a single source of truth,
//   - adding a new vertex attribute means editing two structs
//     (the C++ struct + the shader's `in` declarations) and
//     this header — not also patching every CreateVB-style site.

namespace Poseidon::render::vao
{

// VSScreen reads TLVertex with 6 attributes.  Caller must have a
// non-zero VAO + the corresponding VBO bound to GL_ARRAY_BUFFER.
inline void SetupTLVertexLayout()
{
    const GLsizei stride = sizeof(TLVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, rhw)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TLVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TLVertex, specular)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, t0)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TLVertex, t1)));
}

// VSTransform reads SVertex with 3 attributes.  Caller must have a
// non-zero VAO + the corresponding VBO bound to GL_ARRAY_BUFFER.
inline void SetupSVertexLayout()
{
    const GLsizei stride = sizeof(SVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, norm)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SVertex, t0)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(SVertex, landClip)));
}

} // namespace Poseidon::render::vao
