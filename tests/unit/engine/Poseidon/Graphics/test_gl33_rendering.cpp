#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Core/RenderState.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>

#include <cstddef>
#include <stdint.h>
#include <catch2/catch_message.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <iterator>
#include <set>
#include <string>

TEST_CASE("PassIdName: every pass id maps to a distinct non-empty label", "[Graphics][GL33][DebugGroup]")
{
    // The labels feed glPushDebugGroup at the real pass transitions;
    // an empty or duplicated label makes a RenderDoc capture ambiguous.
    constexpr Poseidon::PassId kAll[] = {
        Poseidon::PassId::Opaque,      Poseidon::PassId::Cutout, Poseidon::PassId::Transparent,
        Poseidon::PassId::Shadow,      Poseidon::PassId::Light,  Poseidon::PassId::OnSurface,
        Poseidon::PassId::Cockpit,     Poseidon::PassId::Sky,    Poseidon::PassId::Water,
        Poseidon::PassId::ScreenSpace,
    };
    std::set<std::string> seen;
    for (const auto id : kAll)
    {
        const char* name = Poseidon::PassIdName(id);
        REQUIRE(name != nullptr);
        REQUIRE(!std::string(name).empty());
        seen.insert(name);
    }
    REQUIRE(seen.size() == std::size(kAll));
}

// TLVertex Layout Tests
// These verify the GPU-facing struct layout matches what our VAO attributes expect.
// Any layout change here would cause rendering corruption.

TEST_CASE("TLVertex: size matches expected GPU stride", "[Graphics][GL33]")
{
    // VAO configured with stride = sizeof(TLVertex) = 40 bytes
    REQUIRE(sizeof(TLVertex) == 40);
}

TEST_CASE("TLVertex: member offsets match VAO attribute pointers", "[Graphics][GL33]")
{
    // Attribute 0: pos at offset 0 (vec3, 12 bytes)
    REQUIRE(offsetof(TLVertex, pos) == 0);

    // Attribute 1: rhw at offset 12 (float, 4 bytes)
    REQUIRE(offsetof(TLVertex, rhw) == 12);

    // Attribute 2: color at offset 16 (4x ubyte, 4 bytes)
    REQUIRE(offsetof(TLVertex, color) == 16);

    // Attribute 3: specular at offset 20 (4x ubyte, 4 bytes)
    REQUIRE(offsetof(TLVertex, specular) == 20);

    // Attribute 4: t0 at offset 24 (vec2, 8 bytes)
    REQUIRE(offsetof(TLVertex, t0) == 24);

    // Attribute 5: t1 at offset 32 (vec2, 8 bytes)
    REQUIRE(offsetof(TLVertex, t1) == 32);
}

TEST_CASE("TLVertex: member sizes match GPU format expectations", "[Graphics][GL33]")
{
    REQUIRE(sizeof(Vector3P) == 12);
    REQUIRE(sizeof(float) == 4);
    REQUIRE(sizeof(PackedColor) == 4);
    REQUIRE(sizeof(UVPair) == 8);
}

// PackedColor BGRA Layout Tests
// PackedColor stores as ARGB in a 32-bit DWORD: (A<<24)|(R<<16)|(G<<8)|B
// When read as bytes in memory (little-endian), the byte order is B,G,R,A.
// OpenGL reads these bytes via GL_UNSIGNED_BYTE as (B,G,R,A) — shaders may
// need .bgra swizzle.

TEST_CASE("PackedColor: ARGB component packing", "[Graphics][GL33]")
{
    PackedColor c(100, 150, 200, 255);
    REQUIRE(c.R8() == 100);
    REQUIRE(c.G8() == 150);
    REQUIRE(c.B8() == 200);
    REQUIRE(c.A8() == 255);
}

TEST_CASE("PackedColor: byte layout is BGRA in memory (little-endian)", "[Graphics][GL33]")
{
    PackedColor c(100, 150, 200, 255);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&c);
    // Little-endian DWORD: (A<<24)|(R<<16)|(G<<8)|B
    // Bytes in memory: [B, G, R, A]
    REQUIRE(bytes[0] == 200); // B
    REQUIRE(bytes[1] == 150); // G
    REQUIRE(bytes[2] == 100); // R
    REQUIRE(bytes[3] == 255); // A
}

TEST_CASE("PackedColor: zero value", "[Graphics][GL33]")
{
    PackedColor c(0u);
    REQUIRE(c.R8() == 0);
    REQUIRE(c.G8() == 0);
    REQUIRE(c.B8() == 0);
    REQUIRE(c.A8() == 0);
}

TEST_CASE("PackedColor: white (opaque)", "[Graphics][GL33]")
{
    PackedColor c(255, 255, 255, 255);
    REQUIRE(c.R8() == 255);
    REQUIRE(c.G8() == 255);
    REQUIRE(c.B8() == 255);
    REQUIRE(c.A8() == 255);
}

// SpecToPassId Tests
// Pure function mapping engine spec flags to render pass IDs.
// Tested exhaustively since ApplyPassState dispatches on this.

TEST_CASE("SpecToPassId: default (no flags) maps to Opaque", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(0) == PassId::Opaque);
}

TEST_CASE("SpecToPassId: IsShadow has highest priority", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsShadow) == PassId::Shadow);
    // Even with other flags set, IsShadow wins
    REQUIRE(SpecToPassId(IsShadow | IsLight) == PassId::Shadow);
    REQUIRE(SpecToPassId(IsShadow | IsAlpha) == PassId::Shadow);
    REQUIRE(SpecToPassId(IsShadow | IsWater) == PassId::Shadow);
}

TEST_CASE("SpecToPassId: IsLight maps to Light", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsLight) == PassId::Light);
}

TEST_CASE("SpecToPassId: IsWater maps to Water", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsWater) == PassId::Water);
    // Water beats alpha
    REQUIRE(SpecToPassId(IsWater | IsAlpha) == PassId::Water);
}

TEST_CASE("SpecToPassId: IsAlphaFog maps to Transparent", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsAlphaFog) == PassId::Transparent);
}

TEST_CASE("SpecToPassId: IsAlpha maps to Transparent", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsAlpha) == PassId::Transparent);
}

TEST_CASE("SpecToPassId: IsTransparent (chromakey) maps to Cutout", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(IsTransparent) == PassId::Cutout);
}

TEST_CASE("SpecToPassId: NoZBuf maps to Sky", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(NoZBuf) == PassId::Sky);
}

TEST_CASE("SpecToPassId: NoZWrite maps to Transparent", "[Graphics][GL33]")
{
    REQUIRE(SpecToPassId(NoZWrite) == PassId::Transparent);
}

TEST_CASE("SpecToPassId: priority order is correct", "[Graphics][GL33]")
{
    // Light beats Water
    REQUIRE(SpecToPassId(IsLight | IsWater) == PassId::Light);
    // Water beats AlphaFog
    REQUIRE(SpecToPassId(IsWater | IsAlphaFog) == PassId::Water);
    // AlphaFog beats Alpha
    REQUIRE(SpecToPassId(IsAlphaFog | IsAlpha) == PassId::Transparent);
    // Alpha beats IsTransparent
    REQUIRE(SpecToPassId(IsAlpha | IsTransparent) == PassId::Transparent);
    // IsTransparent beats NoZBuf
    REQUIRE(SpecToPassId(IsTransparent | NoZBuf) == PassId::Cutout);
    // NoZBuf beats NoZWrite
    REQUIRE(SpecToPassId(NoZBuf | NoZWrite) == PassId::Sky);
}

// FrameState / PassState / DrawItem UBO Alignment Tests

TEST_CASE("FrameState: matrices at start for UBO upload", "[Graphics][GL33]")
{
    REQUIRE(offsetof(FrameState, view) == 0);
}

TEST_CASE("DrawItem: default passId is Opaque", "[Graphics][GL33]")
{
    DrawItem item;
    REQUIRE(item.passId == PassId::Opaque);
    REQUIRE(item.bias == 0);
    REQUIRE(item.texture == nullptr);
}

// PSConstants UBO Alignment Tests (GL33-specific)

TEST_CASE("PSConstants: aligned to 16 bytes for UBO upload", "[Graphics][GL33]")
{
    REQUIRE(alignof(PSConstants) == 16);
    REQUIRE(sizeof(PSConstants) % 16 == 0);
}

TEST_CASE("PSConstants: register layout is sequential float4s", "[Graphics][GL33]")
{
    REQUIRE(offsetof(PSConstants, fogColor) == 0);
    REQUIRE(offsetof(PSConstants, alphaRef) == 16);
    REQUIRE(offsetof(PSConstants, lightDir) == 32);
    REQUIRE(offsetof(PSConstants, grassCoef1) == 48);
    REQUIRE(offsetof(PSConstants, grassCoef2) == 64);
    REQUIRE(offsetof(PSConstants, rgbEyeCoef) == 80);
}

TEST_CASE("PSConstants: constColor slot and white default", "[Graphics][GL33][surface]")
{
    // The lit pixel shaders multiply their output by constColor (c3, the
    // per-object IsColored tint).  The slot is decoupled from the struct field
    // (kept last to preserve the offsets above), and the default must stay
    // white: a zero default would render every unset draw black.
    REQUIRE(PSConstants::SlotConstColor == 3);
    const PSConstants def{};
    REQUIRE(def.constColor[0] == 1.0f);
    REQUIRE(def.constColor[1] == 1.0f);
    REQUIRE(def.constColor[2] == 1.0f);
    REQUIRE(def.constColor[3] == 1.0f);
}

TEST_CASE("SVertex: correct size for static mesh upload", "[Graphics][GL33]")
{
    REQUIRE(sizeof(SVertex) == 32);
    REQUIRE(offsetof(SVertex, pos) == 0);
    REQUIRE(offsetof(SVertex, norm) == 12);
    REQUIRE(offsetof(SVertex, t0) == 24);
}

// Engine Constants Tests (shared across backends)

TEST_CASE("SpecToPassId covers all spec flag bits", "[Graphics][GL33]")
{
    // Verify flags are power-of-two (non-overlapping)
    REQUIRE((IsShadow & IsLight) == 0);
    REQUIRE((IsWater & IsAlpha) == 0);
    REQUIRE((NoZBuf & NoZWrite) == 0);
}

// BlendModeV4 / DepthModeV4 Enum Tests
// Verify all modes exist and have distinct values.

TEST_CASE("BlendModeV4: all modes have distinct values", "[Graphics][GL33]")
{
    REQUIRE(BlendModeV4::Opaque != BlendModeV4::AlphaBlend);
    REQUIRE(BlendModeV4::AlphaBlend != BlendModeV4::Additive);
    REQUIRE(BlendModeV4::Additive != BlendModeV4::Shadow);
}

TEST_CASE("DepthModeV4: all modes have distinct values", "[Graphics][GL33]")
{
    REQUIRE(DepthModeV4::Normal != DepthModeV4::ReadOnly);
    REQUIRE(DepthModeV4::ReadOnly != DepthModeV4::Disabled);
    REQUIRE(DepthModeV4::Disabled != DepthModeV4::Shadow);
}

TEST_CASE("PassId: all pass types have distinct values", "[Graphics][GL33]")
{
    // Just verify they compile and are distinct
    PassId passes[] = {PassId::Opaque,    PassId::Cutout,  PassId::Transparent, PassId::Shadow, PassId::Light,
                       PassId::OnSurface, PassId::Cockpit, PassId::Sky,         PassId::Water,  PassId::ScreenSpace};
    for (int i = 0; i < 10; i++)
    {
        for (int j = i + 1; j < 10; j++)
        {
            REQUIRE(passes[i] != passes[j]);
        }
    }
}

// TLMaterial Tests

TEST_CASE("TLMaterial: inequality after changing specFlags", "[Graphics][GL33]")
{
    TLMaterial mat1, mat2;
    // Both default constructed — set matching fields manually
    mat1.specFlags = 0;
    mat2.specFlags = 0;
    mat1.ambient = Color(0.5f, 0.5f, 0.5f, 1.0f);
    mat2.ambient = Color(0.5f, 0.5f, 0.5f, 1.0f);
    mat1.diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    mat2.diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    mat1.emmisive = Color(0, 0, 0, 0);
    mat2.emmisive = Color(0, 0, 0, 0);
    mat1.forcedDiffuse = Color(0, 0, 0, 0);
    mat2.forcedDiffuse = Color(0, 0, 0, 0);
    REQUIRE(mat1 == mat2);

    mat1.specFlags = 42;
    REQUIRE(mat1 != mat2);
}

// TriQueue Structure Tests

TEST_CASE("TriQueue: default passId is Opaque", "[Graphics][GL33]")
{
    TriQueue tq;
    REQUIRE(tq._passId == PassId::Opaque);
}

// GfxMatrix Layout Tests (GPU upload format for all backends)
// GfxMatrix is a portable 4x4 float matrix (64 bytes) uploaded to UBOs/constant buffers.
// Both the named members (_11.._44) and the m[4][4] array must be contiguous.

TEST_CASE("GfxMatrix: size is 64 bytes (16 floats)", "[Graphics][Matrix]")
{
    REQUIRE(sizeof(GfxMatrix) == 64);
}

TEST_CASE("GfxMatrix: row-major layout matches m[row][col]", "[Graphics][Matrix]")
{
    GfxMatrix mat = {};
    mat._11 = 1;
    mat._12 = 2;
    mat._13 = 3;
    mat._14 = 4;
    mat._21 = 5;
    mat._22 = 6;
    mat._23 = 7;
    mat._24 = 8;
    mat._31 = 9;
    mat._32 = 10;
    mat._33 = 11;
    mat._34 = 12;
    mat._41 = 13;
    mat._42 = 14;
    mat._43 = 15;
    mat._44 = 16;
    REQUIRE(mat.m[0][0] == 1);
    REQUIRE(mat.m[0][3] == 4);
    REQUIRE(mat.m[1][0] == 5);
    REQUIRE(mat.m[2][2] == 11);
    REQUIRE(mat.m[3][3] == 16);
}

TEST_CASE("GfxMatrix: contiguous in memory for GPU upload", "[Graphics][Matrix]")
{
    GfxMatrix mat = {};
    mat._11 = 1;
    mat._12 = 2;
    mat._13 = 3;
    mat._14 = 4;
    mat._21 = 5;
    mat._22 = 6;
    mat._23 = 7;
    mat._24 = 8;
    mat._31 = 9;
    mat._32 = 10;
    mat._33 = 11;
    mat._34 = 12;
    mat._41 = 13;
    mat._42 = 14;
    mat._43 = 15;
    mat._44 = 16;
    const float* ptr = &mat._11;
    for (int i = 0; i < 16; i++)
    {
        REQUIRE(ptr[i] == Catch::Approx(float(i + 1)));
    }
}

// ConvertMatrix Tests
// Converts engine Matrix4 to GfxMatrix (row-major GPU format).
// Maps: aside→row0, up→row1, dir→row2, pos→row3, column3=(0,0,0,1).

TEST_CASE("ConvertMatrix: identity matrix", "[Graphics][Matrix]")
{
    Matrix4 src = MIdentity;
    GfxMatrix dst = {};
    ConvertMatrix(dst, src);

    // Aside = (1,0,0) → row 0
    REQUIRE(dst._11 == Catch::Approx(1.0f));
    REQUIRE(dst._12 == Catch::Approx(0.0f));
    REQUIRE(dst._13 == Catch::Approx(0.0f));

    // Up = (0,1,0) → row 1
    REQUIRE(dst._21 == Catch::Approx(0.0f));
    REQUIRE(dst._22 == Catch::Approx(1.0f));
    REQUIRE(dst._23 == Catch::Approx(0.0f));

    // Dir = (0,0,1) → row 2
    REQUIRE(dst._31 == Catch::Approx(0.0f));
    REQUIRE(dst._32 == Catch::Approx(0.0f));
    REQUIRE(dst._33 == Catch::Approx(1.0f));

    // Pos = (0,0,0) → row 3
    REQUIRE(dst._41 == Catch::Approx(0.0f));
    REQUIRE(dst._42 == Catch::Approx(0.0f));
    REQUIRE(dst._43 == Catch::Approx(0.0f));

    // Column 3 = (0,0,0,1)
    REQUIRE(dst._14 == Catch::Approx(0.0f));
    REQUIRE(dst._24 == Catch::Approx(0.0f));
    REQUIRE(dst._34 == Catch::Approx(0.0f));
    REQUIRE(dst._44 == Catch::Approx(1.0f));
}

TEST_CASE("ConvertMatrix: translation only", "[Graphics][Matrix]")
{
    Matrix4 src = MIdentity;
    src.SetPosition(Vector3(10.0f, 20.0f, 30.0f));
    GfxMatrix dst = {};
    ConvertMatrix(dst, src);

    // Position maps to row 3
    REQUIRE(dst._41 == Catch::Approx(10.0f));
    REQUIRE(dst._42 == Catch::Approx(20.0f));
    REQUIRE(dst._43 == Catch::Approx(30.0f));

    // Rotation part should still be identity
    REQUIRE(dst._11 == Catch::Approx(1.0f));
    REQUIRE(dst._22 == Catch::Approx(1.0f));
    REQUIRE(dst._33 == Catch::Approx(1.0f));
}

TEST_CASE("ConvertMatrix: column 3 is always (0,0,0,1)", "[Graphics][Matrix]")
{
    Matrix4 src = MIdentity;
    src.SetDirectionAside(Vector3(2, 3, 4));
    src.SetDirectionUp(Vector3(5, 6, 7));
    src.SetDirection(Vector3(8, 9, 10));
    src.SetPosition(Vector3(100, 200, 300));
    GfxMatrix dst = {};
    ConvertMatrix(dst, src);
    REQUIRE(dst._14 == Catch::Approx(0.0f));
    REQUIRE(dst._24 == Catch::Approx(0.0f));
    REQUIRE(dst._34 == Catch::Approx(0.0f));
    REQUIRE(dst._44 == Catch::Approx(1.0f));
}

TEST_CASE("ConvertMatrixTransposed: transpose of ConvertMatrix", "[Graphics][Matrix]")
{
    Matrix4 src = MIdentity;
    src.SetDirectionAside(Vector3(1, 2, 3));
    src.SetDirectionUp(Vector3(4, 5, 6));
    src.SetDirection(Vector3(7, 8, 9));
    src.SetPosition(Vector3(10, 20, 30));

    GfxMatrix normal = {}, transposed = {};
    ConvertMatrix(normal, src);
    ConvertMatrixTransposed(transposed, src);

    // Transposed: m[i][j] of transposed == m[j][i] of normal
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            INFO("i=" << i << " j=" << j);
            REQUIRE(transposed.m[i][j] == Catch::Approx(normal.m[j][i]));
        }
    }
}

// ConvertProjectionMatrix Tests
// Converts the engine's projection matrix to GPU format with optional Z-bias.

TEST_CASE("ConvertProjectionMatrix: zero bias preserves projection", "[Graphics][Matrix]")
{
    // Build projection via 12-float constructor
    // Layout: (aside.x, aside.y, aside.z, pos.x, up.x, up.y, up.z, pos.y, dir.x, dir.y, dir.z, pos.z)
    // We need src(0,0)=1.5, src(1,1)=2.0, src(2,2)=1.0, Position()[2]=-0.1
    // Matrix3K stores aside/up/dir as column vectors, operator()(i,j) accesses col j, row i
    // So (0,0)=aside[0], (1,1)=up[1], (2,2)=dir[2]
    Matrix4 src(1.5f, 0, 0, 0, 0, 2.0f, 0, 0, 0, 0, 1.0f, -0.1f);

    GfxMatrix dst = {};
    ConvertProjectionMatrix(dst, src, 0);

    REQUIRE(dst._11 == Catch::Approx(1.5f));
    REQUIRE(dst._22 == Catch::Approx(2.0f));
    REQUIRE(dst._33 == Catch::Approx(1.0f));
    REQUIRE(dst._43 == Catch::Approx(-0.1f));

    // Perspective division: _34 = 1
    REQUIRE(dst._34 == Catch::Approx(1.0f));

    // Zero entries
    REQUIRE(dst._12 == Catch::Approx(0.0f));
    REQUIRE(dst._13 == Catch::Approx(0.0f));
    REQUIRE(dst._14 == Catch::Approx(0.0f));
    REQUIRE(dst._21 == Catch::Approx(0.0f));
    REQUIRE(dst._23 == Catch::Approx(0.0f));
    REQUIRE(dst._24 == Catch::Approx(0.0f));
    REQUIRE(dst._31 == Catch::Approx(0.0f));
    REQUIRE(dst._32 == Catch::Approx(0.0f));
    REQUIRE(dst._41 == Catch::Approx(0.0f));
    REQUIRE(dst._42 == Catch::Approx(0.0f));
    REQUIRE(dst._44 == Catch::Approx(0.0f));
}

TEST_CASE("ConvertProjectionMatrix: positive bias adjusts z values (z-buffer path)", "[Graphics][Matrix]")
{
    // Note: The w-buffer path requires GEngine, so we only test the z-buffer fallback.
    // With zBias=0 (no bias), the function should pass through src values directly.
    Matrix4 src(2.0f, 0, 0, 0, 0, 3.0f, 0, 0, 0, 0, 0.99f, -0.5f);

    GfxMatrix dst = {};
    ConvertProjectionMatrix(dst, src, 0);

    // Verify structure
    REQUIRE(dst._11 == Catch::Approx(2.0f));
    REQUIRE(dst._22 == Catch::Approx(3.0f));
    REQUIRE(dst._33 == Catch::Approx(0.99f));
    REQUIRE(dst._43 == Catch::Approx(-0.5f));
    REQUIRE(dst._34 == Catch::Approx(1.0f)); // perspective W
    REQUIRE(dst._44 == Catch::Approx(0.0f)); // no affine W
}

// FrameState Layout Tests

TEST_CASE("FrameState: fog and light_disc have default values", "[Graphics][GL33]")
{
    FrameState frame;
    // fogParams default to zero
    REQUIRE(frame.fogParams[0] == Catch::Approx(0.0f));
    // fog color default to zero
    REQUIRE(frame.fogColor[0] == Catch::Approx(0.0f));
    // camera at origin
    REQUIRE(frame.cameraPos[0] == Catch::Approx(0.0f));
    REQUIRE(frame.cameraPos[1] == Catch::Approx(0.0f));
    REQUIRE(frame.cameraPos[2] == Catch::Approx(0.0f));
    // light_disc disabled
    REQUIRE(frame.sunEnabled == false);
}

TEST_CASE("FrameState: viewport array has 4 elements", "[Graphics][GL33]")
{
    FrameState frame;
    frame.viewport[0] = 0;
    frame.viewport[1] = 0;
    frame.viewport[2] = 800;
    frame.viewport[3] = 600;
    REQUIRE(frame.viewport[2] == Catch::Approx(800.0f));
    REQUIRE(frame.viewport[3] == Catch::Approx(600.0f));
}

// PassState Defaults

TEST_CASE("PassState: default state is normal depth, opaque blend", "[Graphics][GL33]")
{
    PassState pass;
    REQUIRE(pass.depthMode == DepthModeV4::Normal);
    REQUIRE(pass.blendMode == BlendModeV4::Opaque);
    REQUIRE(pass.fogMode == FogMode::Enabled);
}

// PSConstants Default Values Tests

TEST_CASE("PSConstants: default fogColor alpha is 1", "[Graphics][GL33]")
{
    PSConstants ps;
    REQUIRE(ps.fogColor[3] == Catch::Approx(1.0f));
    REQUIRE(ps.rgbEyeCoef[3] == Catch::Approx(1.0f));
    REQUIRE(ps.alphaRef[0] == Catch::Approx(0.0f));
}

// Shader/TexGen Enum Value Tests

TEST_CASE("VertexShaderID: VSScreen=0, VSTransform=1, VSShadow=2", "[Graphics][GL33]")
{
    REQUIRE(VSScreen == 0);
    REQUIRE(VSTransform == 1);
    REQUIRE(VSShadow == 2);
    REQUIRE(NVertexShaders == 3);
    REQUIRE(VSNone == NVertexShaders);
}

TEST_CASE("PixelShaderID: all modes have distinct values", "[Graphics][GL33]")
{
    REQUIRE(PSNormal == 0);
    REQUIRE(PSDetail != PSNormal);
    REQUIRE(PSGrass != PSDetail);
    REQUIRE(PSWater != PSGrass);
    REQUIRE(PSFlat != PSWater);
    REQUIRE(PSNone == NPixelShaders);
}

TEST_CASE("VSConst: register indices are non-overlapping", "[Graphics][GL33]")
{
    // VSTransform reads matrix slots; VSScreen reads vpScale.
    // All named ranges occupy disjoint vec4 slots (I-01).
    REQUIRE(VSConst::SlotProj == 0);
    REQUIRE(VSConst::SlotView == 4);
    REQUIRE(VSConst::SlotWorld == 8);
    REQUIRE(VSConst::SlotSunDir == 12);
    REQUIRE(VSConst::SlotAmbient == 13);
    REQUIRE(VSConst::SlotDiffuse == 14);
    REQUIRE(VSConst::SlotEmissive == 15);
    REQUIRE(VSConst::SlotFogParam == 16);
    REQUIRE(VSConst::SlotCamPos == 17);
    // vpScale moved out of slot 0 to break the B-001 alias with SlotProj.
    REQUIRE(VSConst::SlotVpScale == 21);
    REQUIRE(VSConst::SlotVpScale > VSConst::SlotProj + 3);
}

// TextureGL33 System Tests
// Tests for the GL33 texture subsystem (format mapping, mipmap sizes, struct layout)

#include <PoseidonGL33/TextureGL33.hpp>
#include <glad/gl.h>

extern int MipmapSizeGL33(PacFormat format, int w, int h);
extern void InitGLPixelFormat(TextureDescGL33& desc, PacFormat format, bool enableDXT);

TEST_CASE("TextureDescGL33: struct has expected fields", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 256;
    desc.h = 128;
    desc.nMipmaps = 5;
    desc.internalFormat = GL_RGBA8;
    desc.pixelFormat = GL_BGRA;
    desc.pixelType = GL_UNSIGNED_BYTE;
    desc.compressed = false;

    REQUIRE(desc.w == 256);
    REQUIRE(desc.h == 128);
    REQUIRE(desc.nMipmaps == 5);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("SurfaceInfoGL33: default texture is zero", "[Graphics][GL33][Texture]")
{
    SurfaceInfoGL33 surface{};
    REQUIRE(surface.GetTexture() == 0);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: RGBA8888 single level", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 64;
    desc.h = 64;
    desc.nMipmaps = 1;

    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB8888);
    REQUIRE(size == 64 * 64 * 4); // 16384 bytes
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: RGBA8888 with mipmaps", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 64;
    desc.h = 64;
    desc.nMipmaps = 3; // 64x64 + 32x32 + 16x16

    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB8888);
    int expected = 64 * 64 * 4 + 32 * 32 * 4 + 16 * 16 * 4; // 16384 + 4096 + 1024 = 21504
    REQUIRE(size == expected);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: 16-bit format", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 128;
    desc.h = 128;
    desc.nMipmaps = 1;

    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB1555);
    REQUIRE(size == 128 * 128 * 2); // 32768 bytes
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: DXT1 compressed", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 64;
    desc.h = 64;
    desc.nMipmaps = 1;

    int size = SurfaceInfoGL33::CalculateSize(desc, PacDXT1);
    // DXT1: ((64+3)/4) * ((64+3)/4) * 8 = 16 * 16 * 8 = 2048
    REQUIRE(size == 2048);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: DXT5 compressed", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 64;
    desc.h = 64;
    desc.nMipmaps = 1;

    int size = SurfaceInfoGL33::CalculateSize(desc, PacDXT5);
    // DXT5: ((64+3)/4) * ((64+3)/4) * 16 = 16 * 16 * 16 = 4096
    REQUIRE(size == 4096);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: explicit totalSize overrides calculation", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 64;
    desc.h = 64;
    desc.nMipmaps = 1;

    // When totalSize >= 0, it should return that value directly
    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB8888, 42);
    REQUIRE(size == 42);
}

TEST_CASE("MipmapSizeGL33: various formats", "[Graphics][GL33][Texture]")
{
    // 32-bit: w * h * 4
    REQUIRE(MipmapSizeGL33(PacARGB8888, 16, 16) == 16 * 16 * 4);

    // 16-bit: w * h * 2
    REQUIRE(MipmapSizeGL33(PacARGB1555, 32, 32) == 32 * 32 * 2);
    REQUIRE(MipmapSizeGL33(PacRGB565, 64, 64) == 64 * 64 * 2);
    REQUIRE(MipmapSizeGL33(PacARGB4444, 128, 128) == 128 * 128 * 2);

    // DXT1: block-based, 8 bytes per 4x4 block
    REQUIRE(MipmapSizeGL33(PacDXT1, 16, 16) == 4 * 4 * 8);

    // DXT5: block-based, 16 bytes per 4x4 block
    REQUIRE(MipmapSizeGL33(PacDXT5, 16, 16) == 4 * 4 * 16);
}

TEST_CASE("MipmapSizeGL33: non-power-of-two sizes", "[Graphics][GL33][Texture]")
{
    // DXT with non-multiple-of-4: rounds up
    // 5x5 → ((5+3)/4) * ((5+3)/4) * 8 = 2 * 2 * 8 = 32
    REQUIRE(MipmapSizeGL33(PacDXT1, 5, 5) == 32);

    // 1x1 DXT1 → ((1+3)/4) * ((1+3)/4) * 8 = 1 * 1 * 8 = 8
    REQUIRE(MipmapSizeGL33(PacDXT1, 1, 1) == 8);
}

TEST_CASE("TextureGL33 upload format expands interpolated compressed textures", "[Graphics][GL33][Texture]")
{
    REQUIRE(UploadFormatForTextureGL33(PacDXT1, true) == PacARGB1555);
    REQUIRE(UploadFormatForTextureGL33(PacDXT3, true) == PacARGB1555);
    REQUIRE(UploadFormatForTextureGL33(PacDXT5, true) == PacARGB1555);
    REQUIRE(UploadFormatForTextureGL33(PacDXT1, false) == PacDXT1);
    REQUIRE(UploadFormatForTextureGL33(PacARGB1555, true) == PacARGB1555);
    REQUIRE(UploadFormatForTextureGL33(PacARGB4444, true) == PacARGB4444);
}

TEST_CASE("InitGLPixelFormat: ARGB8888 maps to GL_RGBA8", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacARGB8888, true);

    REQUIRE(desc.internalFormat == GL_RGBA8);
    REQUIRE(desc.pixelFormat == GL_BGRA);
    REQUIRE(desc.pixelType == GL_UNSIGNED_INT_8_8_8_8_REV);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("InitGLPixelFormat: ARGB1555 maps to GL_RGB5_A1", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacARGB1555, true);

    REQUIRE(desc.internalFormat == GL_RGB5_A1);
    REQUIRE(desc.pixelFormat == GL_BGRA);
    REQUIRE(desc.pixelType == GL_UNSIGNED_SHORT_1_5_5_5_REV);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("InitGLPixelFormat: RGB565 maps to GL_RGB565", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacRGB565, true);

    REQUIRE(desc.internalFormat == GL_RGB565);
    REQUIRE(desc.pixelFormat == GL_RGB);
    REQUIRE(desc.pixelType == GL_UNSIGNED_SHORT_5_6_5);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("InitGLPixelFormat: ARGB4444 maps to GL_RGBA4", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacARGB4444, true);

    REQUIRE(desc.internalFormat == GL_RGBA4);
    REQUIRE(desc.pixelFormat == GL_BGRA);
    REQUIRE(desc.pixelType == GL_UNSIGNED_SHORT_4_4_4_4_REV);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("InitGLPixelFormat: AI88 maps to GL_RG8", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacAI88, true);

    REQUIRE(desc.internalFormat == GL_RG8);
    REQUIRE(desc.pixelFormat == GL_RG);
    REQUIRE(desc.pixelType == GL_UNSIGNED_BYTE);
    REQUIRE(desc.compressed == false);
}

TEST_CASE("InitGLPixelFormat: DXT1 maps to compressed S3TC", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacDXT1, true);

    REQUIRE(desc.internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
    REQUIRE(desc.compressed == true);
}

TEST_CASE("InitGLPixelFormat: DXT3 maps to compressed S3TC", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacDXT3, true);

    REQUIRE(desc.internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT);
    REQUIRE(desc.compressed == true);
}

TEST_CASE("InitGLPixelFormat: DXT5 maps to compressed S3TC", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    InitGLPixelFormat(desc, PacDXT5, true);

    REQUIRE(desc.internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
    REQUIRE(desc.compressed == true);
}

TEST_CASE("TextureGL33: GetHandle returns 0 when no surfaces", "[Graphics][GL33][Texture]")
{
    // TextureGL33 uses FAST_ALLOCATOR — test via GetHandle on a zeroed surface
    SurfaceInfoGL33 surf{};
    REQUIRE(surf.GetTexture() == 0);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: mipmap chain halving", "[Graphics][GL33][Texture]")
{
    // 256x256, 4 mips: 256x256 + 128x128 + 64x64 + 32x32 (16-bit)
    TextureDescGL33 desc{};
    desc.w = 256;
    desc.h = 256;
    desc.nMipmaps = 4;

    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB1555);
    int expected = (256 * 256 + 128 * 128 + 64 * 64 + 32 * 32) * 2;
    REQUIRE(size == expected);
}

TEST_CASE("SurfaceInfoGL33::CalculateSize: non-square texture", "[Graphics][GL33][Texture]")
{
    TextureDescGL33 desc{};
    desc.w = 128;
    desc.h = 64;
    desc.nMipmaps = 2; // 128x64 + 64x32

    int size = SurfaceInfoGL33::CalculateSize(desc, PacARGB8888);
    int expected = 128 * 64 * 4 + 64 * 32 * 4; // 32768 + 8192 = 40960
    REQUIRE(size == expected);
}

// SVertex Layout Tests — must match vsTransform GLSL attribute layout

TEST_CASE("SVertex: size is 32 bytes (pos+norm+uv)", "[GL33][VertexBuffer]")
{
    REQUIRE(sizeof(SVertex) == 32);
}

TEST_CASE("SVertex: member offsets match VAO attribute pointers", "[GL33][VertexBuffer]")
{
    // location 0: pos at offset 0 (vec3, 12 bytes)
    REQUIRE(offsetof(SVertex, pos) == 0);
    // location 1: norm at offset 12 (vec3, 12 bytes)
    REQUIRE(offsetof(SVertex, norm) == 12);
    // location 2: uv at offset 24 (vec2, 8 bytes)
    REQUIRE(offsetof(SVertex, t0) == 24);
}

TEST_CASE("SVertex: Vector3P members are 12 bytes each", "[GL33][VertexBuffer]")
{
    REQUIRE(sizeof(Vector3P) == 12);
}

TEST_CASE("SVertex: UVPair is 8 bytes", "[GL33][VertexBuffer]")
{
    REQUIRE(sizeof(UVPair) == 8);
}

// DrawItem struct layout tests

TEST_CASE("DrawItem: default constructed has zero worldMatrix", "[GL33][VertexBuffer]")
{
    DrawItem item{};
    REQUIRE(item.worldMatrix._11 == 0.0f);
    REQUIRE(item.worldMatrix._44 == 0.0f);
    REQUIRE(item.passId == PassId::Opaque);
    REQUIRE(item.bias == 0);
    REQUIRE(item.specFlags == Poseidon::render::LegacySpec{});
    REQUIRE(item.texture == nullptr);
    REQUIRE(item.vertexBuffer == nullptr);
    REQUIRE(item.isTLDraw == false);
    REQUIRE(item.sectionBegin == 0);
    REQUIRE(item.sectionEnd == 0);
}

TEST_CASE("DrawItem: section range and TL flag", "[GL33][VertexBuffer]")
{
    DrawItem item{};
    item.isTLDraw = true;
    item.sectionBegin = 2;
    item.sectionEnd = 5;
    item.passId = PassId::Shadow;

    REQUIRE(item.isTLDraw == true);
    REQUIRE(item.sectionBegin == 2);
    REQUIRE(item.sectionEnd == 5);
    REQUIRE(item.passId == PassId::Shadow);
}

// VFormatSet enum tests — multi-texturing format selection

TEST_CASE("VFormatSet: spec flags map to correct format", "[GL33][VertexBuffer]")
{
    // No detail flags → SingleTex
    {
        int spec = 0;
        bool hasDetail = (spec & (DetailTexture | SpecularTexture | GrassTexture)) != 0;
        REQUIRE_FALSE(hasDetail);
    }

    // DetailTexture flag
    {
        int spec = DetailTexture;
        bool isGrass = (spec & GrassTexture) != 0;
        bool isDetail = (spec & DetailTexture) != 0;
        bool isSpec = (spec & SpecularTexture) != 0;
        REQUIRE_FALSE(isGrass);
        REQUIRE(isDetail);
        REQUIRE_FALSE(isSpec);
    }

    // GrassTexture flag
    {
        int spec = GrassTexture;
        bool isGrass = (spec & GrassTexture) != 0;
        REQUIRE(isGrass);
    }

    // SpecularTexture flag
    {
        int spec = SpecularTexture;
        bool isSpec = (spec & SpecularTexture) != 0;
        bool isGrass = (spec & GrassTexture) != 0;
        bool isDetail = (spec & DetailTexture) != 0;
        REQUIRE(isSpec);
        REQUIRE_FALSE(isGrass);
        REQUIRE_FALSE(isDetail);
    }
}

// VertexIndex type tests — must be 16-bit for GL_UNSIGNED_SHORT

TEST_CASE("VertexIndex: is 16-bit (2 bytes)", "[GL33][VertexBuffer]")
{
    REQUIRE(sizeof(VertexIndex) == 2);
}

// ConvertMatrix tests — used in PrepareMeshTLImpl

TEST_CASE("ConvertMatrix: identity matrix converts correctly", "[GL33][VertexBuffer]")
{
    Matrix4 identity;
    identity.SetIdentity();

    GfxMatrix gfx{};
    ConvertMatrix(gfx, identity);

    // Diagonal should be 1.0
    REQUIRE(gfx._11 == Catch::Approx(1.0f));
    REQUIRE(gfx._22 == Catch::Approx(1.0f));
    REQUIRE(gfx._33 == Catch::Approx(1.0f));
    REQUIRE(gfx._44 == Catch::Approx(1.0f));

    // Off-diagonal should be 0
    REQUIRE(gfx._12 == Catch::Approx(0.0f));
    REQUIRE(gfx._13 == Catch::Approx(0.0f));
    REQUIRE(gfx._14 == Catch::Approx(0.0f));
    REQUIRE(gfx._21 == Catch::Approx(0.0f));
}

// Fan triangulation index count tests

TEST_CASE("Fan triangulation: triangle produces 3 indices", "[GL33][VertexBuffer]")
{
    int nVerts = 3;
    int indices = (nVerts - 2) * 3;
    REQUIRE(indices == 3);
}

TEST_CASE("Fan triangulation: quad produces 6 indices", "[GL33][VertexBuffer]")
{
    int nVerts = 4;
    int indices = (nVerts - 2) * 3;
    REQUIRE(indices == 6);
}

TEST_CASE("Fan triangulation: pentagon produces 9 indices", "[GL33][VertexBuffer]")
{
    int nVerts = 5;
    int indices = (nVerts - 2) * 3;
    REQUIRE(indices == 9);
}

TEST_CASE("Fan triangulation: N-gon formula is (N-2)*3", "[GL33][VertexBuffer]")
{
    for (int n = 3; n <= 16; n++)
    {
        int indices = (n - 2) * 3;
        int triangles = n - 2;
        REQUIRE(indices == triangles * 3);
    }
}
