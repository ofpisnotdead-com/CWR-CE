# Metal Port — Progress Notes (WIP)

Status as of the latest commit on `feature/metal-renderer`. Written so a
future session can pick this up cold without re-deriving context.

## Repo structure (as of the last restructuring pass)

- `upstream` — exact untouched mirror of `BohemiaInteractive/CWR`.
- `main` — upstream + ARM64/Apple-Silicon portability fixes only (sse2neon,
  `finite()`→`isfinite()`, Linux-header gating, pthread portability, Apple
  linker workaround). No Metal code. GL33 is the only renderer; confirmed it
  renders the **entire** game correctly on Apple Silicon (menus, fonts, 3D
  content) — this is the proven-good baseline to diff against whenever Metal
  output looks wrong.
- `feature/metal-renderer` (current branch) — `main` + the Metal backend
  (`engine/PoseidonMTL/`).

GitHub fork: `McArdle-Systems/Poseidon-ARM64` (renamed from `Poseidon-Metal` —
the ARM64 portability work is the broadly useful part; Metal is the
experimental extra on its own branch).

## This session's milestone: hardware T&L mesh pipeline

The actual blocker for "no 3D models render" turned out to be architectural,
not a small bug: `EngineMTL` had never implemented the hardware
transform+lighting virtuals (`BeginMeshTL`/`PrepareMeshTL`/`EndMeshTL`/
`DrawSectionTL`/`CreateVertexBuffer`/`SetMaterial`) — these are the *only*
path that draws terrain/vehicles/characters (`Shape::Draw`, see
`engine/Poseidon/Graphics/Rendering/Shape/ShapeDraw.cpp:78`, checks
`engine->GetTL() && _buffer`). Everything was silently falling through to a
much simpler legacy/software-TL fallback that was only ever meant for
sky/cloud/sun/moon-style flat-colored content.

Implemented tonight, scoped deliberately minimal (see "Known limitations /
explicit follow-up scope" below):

- `engine/PoseidonMTL/VertexBufferMTL.{hpp,cpp}` (**new**) — per-`Shape`
  vertex+index GPU buffer pair, mirrors `VertexBufferGL33`
  (`engine/PoseidonGL33/EngineGL33_VertexBuffer.cpp`) almost line-for-line:
  same 32-byte vertex layout (pos/negated-norm/uv), same fan-triangulation,
  same per-section index-range bookkeeping. Holds opaque mesh-buffer handles
  from `EngineMTLBootstrap`, not `MTL::Buffer*` directly (same
  metal-cpp/Poseidon-core header-collision constraint as everything else in
  this backend).
- `engine/PoseidonMTL/EngineMTLBootstrap.{hpp,cpp}`:
  - New POD types (`VertexMeshMTL`, `Mat4RowsMTL`, `FrameConstantsMTL`,
    `ObjectConstantsMTL`) and mesh-buffer handle bank
    (`CreateMeshBuffer`/`UpdateMeshBuffer`/`DestroyMeshBuffer`, same pattern
    as the existing texture handle bank).
  - A second render pipeline (`pipelineStateTL`) with its own embedded MSL
    shader (`kShaderSourceMesh`) — separate from the 2D pipeline because the
    vertex layout differs. Both pipelines share one render pass/encoder per
    frame; `DrawTriangles2D` and `DrawSectionTL` each explicitly rebind their
    own pipeline/depth-state/scissor at the top of the call now, since draw
    order between the two paths within one frame isn't guaranteed.
  - A real depth buffer (`MTL::PixelFormatDepth32Float`, recreated on
    resize) — didn't exist before this session at all. Two
    `MTL::DepthStencilState`s: test+write for 3D mesh draws, always-pass/no-
    write for 2D UI draws sharing the same pass.
  - `BeginFrame()` gained a `clearZ` parameter (was always implicitly "don't
    care" before) so the engine's existing `Clear(bool clearZ, bool clear,
    ...)` split actually does something for the depth attachment now.
  - `setMaximumDrawableCount(2)` pinned in `SetupDevice()` — unrelated to
    this milestone, a leftover from chasing a ghosting bug earlier in the
    session (see "Dead-end investigated" below); kept because it's a
    correct, low-risk default independent of whether it fixed that bug.
- `engine/PoseidonMTL/EngineMTL.{hpp,cpp}`: new overrides —
  `CreateVertexBuffer`, `SetMaterial` (CPU-side sun+material color
  combination, ported from `EngineGL33_Material.cpp`'s
  `UploadVSMaterialConstants`), `PrepareTriangleTL` (per-section texture
  hook), `PrepareMeshTL` (per-frame camera/sun/fog + per-object
  camera-relative world matrix, ported from `EngineGL33_Mesh.cpp`),
  `BeginMeshTL`/`DrawSectionTL`, and `GetTL() override { return true; }` —
  this last one is the actual switch that makes every `Shape` with a GPU
  vertex buffer (sky, the menu's 3D prop, terrain, vehicles, characters) use
  this new path instead of the legacy fallback, in one shot, repo-wide.

### Two real bugs found and fixed in the new shader (both empirically diagnosed, not guessed)

1. **Ambient/diffuse incorrectly zeroed.** The shader multiplied *both*
   ambient and diffuse by a `sunEnabled` uniform. `SetMaterial`'s CPU-side
   sun+material combination (mirroring GL33) happens *unconditionally* —
   the colors already have the sun baked in regardless of whether
   `EnableSunLight(true)` was ever called for that scene. The menu's 3D
   scene has `sunEnabled=false` (confirmed via temporary logging), so
   multiplying by it zeroed everything except emissive (which is 0 for most
   props) → pure black. Fix: only gate the literal per-vertex `NdotL`
   falloff by `sunEnabled` (a directional term genuinely needs a real sun
   direction to mean anything); ambient/emissive are never gated.
2. **Alpha output bug.** Fragment shader returned `texColor.a` instead of
   `1.0`, contradicting this v1's own "opaque-only" design. Legacy
   diffuse-only textures commonly have an empty/zero alpha channel (no real
   alpha data was ever authored) — combined with the pipeline's blending
   state, that composited as fully transparent, again showing through to
   the black clear color. Fix: hardcode output alpha to `1.0`.

### Verified working (by direct user observation, windowed `--render mtl --window`)

The network-games menu screen (`Down`, `Enter` from the main menu) — the 3D
laptop prop and its UI highlight now render correctly: lit, no corruption,
no ghosting. This was the "radiating fin pattern" bug from earlier in the
session — root-caused as the legacy-path object now correctly routing
through the new hardware-TL pipeline instead, not the swap-chain theory
that was tried and ruled out first (see below).

### Terrain/vehicles/characters — verified in a later session, mixed results

See "Real per-frame render hookup" below for the full writeup (that session
also fixed the bug that was blocking *any* of this from rendering at all).
Short version: vehicles render well after a backface-culling fix; terrain
has some remaining holes; one animated/multi-part character model renders
exploded (root cause not yet found).

### Dead-end investigated (for the record, so it isn't re-tried blind)

Earlier in the session, before the hardware-TL gap was identified, the menu
prop's corruption looked like classic frame-buffer ghosting (multiple stale
swap-chain buffers showing simultaneously). Pinned `CAMetalLayer`
`maximumDrawableCount` to 2 to match GL33's strict double-buffering
assumption — rebuilt, retested, **no change**, which is what actually led to
digging into `GetTL()`/`ShapeDraw.cpp` and finding the real cause. The
drawable-count pin is still in the tree (it's a reasonable default on its
own merits), but credit the actual fix to the hardware-TL implementation +
the two shader bugs above, not that change.

## Known limitations / explicit follow-up scope (not regressions — deliberately deferred)

- **Lighting**: sun + ambient + emissive + linear fog only. No local
  point/spot lights, no specular, no shadow maps, no instancing (grass
  etc.). GL33's equivalent (`EngineGL33_Mesh.cpp`/`_Material.cpp`/
  `_Shaders.cpp`) has all of these; porting them is real future work, not a
  quick fix — see those files for the exact uniform layouts/math when that
  work starts (`VSConst::MaxLocalLights = 8`, Blinn-Phong specular, PCF
  cascaded shadows).
- **Alpha test / blend passes**: v1 is opaque-only (output alpha hardcoded
  to 1). Cutout/Transparent `PassId`s (`engine/Poseidon/Graphics/Core/
  RenderState.hpp`) aren't distinguished yet — every section draws through
  the same opaque-ish blend state.
- **Queue/pass batching**: GL33 batches draws by texture/material for
  performance (`EngineGL33_Queue.cpp`). v1 emits one immediate
  `drawIndexedPrimitives` per `DrawSectionTL` call — correct, just not
  batched. Fine at menu/early-mission scale; revisit if profiling shows it
  matters.
- **`PrepareMeshTL` recomputes per-frame constants on every call** (every
  object), not cached across a pass's whole run of draws the way GL33's
  `_frameState`/`BeginPass`'s `IsIn3DPass()` check does. Correctness over
  performance for this milestone; an easy follow-up if it shows up in
  profiling.

## Audio backend now initializes on macOS (this session, separate from the Metal/T&L work)

**Root cause:** `engine/PoseidonOpenAL/OpenALRuntime.hpp` `dlopen`s OpenAL by
filename at runtime instead of being link-time-linked (deliberate, per
`cmake/vcpkg-triplets/x64-windows-clang.cmake`'s "LGPL: openal-soft must be
dynamically linked so users can replace the implementation" comment — same
intent applies to all platforms). The loader only tried Linux `.so` names, so
on macOS it always failed to find anything and `RegisterOpenALAudioBackend()`
silently lost out to the Dummy backend in `AudioFactory`'s priority order —
**no error was ever logged**, audio was just silently absent. Compounding
this: even with the right filename, `ResolveFunctions()` required *every*
listed symbol (including the 8 `EFX`/`ALC_EXT_EFX` reverb functions) to
resolve or it failed the whole load. macOS does ship a system
`OpenAL.framework` (deprecated since 10.15 but present and functional —
confirmed core AL/ALC symbols resolve via a standalone `dlopen` test), but it
implements core OpenAL 1.1 only, no EFX — so even pointing `dlopen` at it
would still have failed without also fixing the all-or-nothing resolution.

**Fix** (`engine/PoseidonOpenAL/OpenALRuntime.hpp`):
- Split `OPENAL_RUNTIME_FUNCTIONS` into `OPENAL_CORE_FUNCTIONS` (required) and
  `OPENAL_EFX_FUNCTIONS` (resolved best-effort, left null if missing — safe
  because `SoundSystemOAL.cpp` already gates every EFX call behind a runtime
  `alcIsExtensionPresent(ALC_EXT_EFX)` check and never calls
  `InitEFX`/`alGenEffects`/etc. unless that passed).
- Added an `__APPLE__` branch to `TryLoadModule()`: tries `libopenal.1.dylib`
  / `libopenal.dylib` (bare names, in case of a Homebrew openal-soft on
  `DYLD_LIBRARY_PATH`), then the Homebrew keg-only install paths directly
  (`/opt/homebrew/opt/openal-soft/lib/...`, `/usr/local/opt/...` for Intel),
  then falls back to `/System/Library/Frameworks/OpenAL.framework/OpenAL`,
  which is always present.

**Verified**: windowed `--render mtl --window` run now logs
`[AUDIO] EFX extension: not available` (expected — system framework, no EFX)
followed by `[AUDIO] Init OK: MacBook Pro Speakers` — a real OpenAL device is
opened where before this fix the backend silently never loaded at all. No
audio-related errors/warnings in the run.

**Not yet verified**: actual audible output from in-game content (menu
music/UI sounds, weapon/footstep SFX in a loaded mission) — only backend
*initialization* was confirmed this session via log inspection, not a human
listening to speaker output. Do this next if picking up audio work again.

## Real per-frame render hookup — the actual fix for "nothing renders" (this session)

**This supersedes the old theory below it.** The top-level main menu's
black screen, the runaway uncapped FPS (thousands of FPS instead of a real
~40-60), and "nothing visible during normal gameplay" were all **one root
cause**, not three separate bugs and not the GModeIntro/audio theory from
the previous write-up of this section.

**Root cause:** `World::Simulate()` calls `GEngine->InitDraw(clear, color)`
**unconditionally once per frame, for every backend** (`World.cpp:1364`) —
this is the real per-frame begin-scene call, not `Clear()`. `EngineMTL` never
overrode `InitDraw`/`InitDrawDone`/`FinishDraw` at all, so it silently fell
back to the base `Engine::InitDraw()`, which only does eye-adaptation
brightness bookkeeping — **zero GPU work**. The only place `EngineMTL` ever
acquired a Metal drawable/opened a render encoder was inside `Clear()` (→
`BeginFrame()`), and `Clear()` is called from just three places in the whole
codebase (a map-screen 3D preview, a vehicle-transport preview, and the
unrelated Tetris app) — **never during normal menu/gameplay rendering.**
Confirmed empirically: instrumented `Clear()`/`BeginFrame()`/`EndFrame()` —
zero `Clear()` calls, zero `BeginFrame()` entries, `EndFrame()` early-
returning on every single one of 12,000+ calls in a 6-second run, while the
CPU spun at 100% re-simulating the same instant (`deltaT≈0` forever, since
nothing ever blocked on real GPU/vsync work either — same root cause as the
uncapped-FPS symptom).

**Fix** (`EngineMTL.hpp`/`.cpp`): added real `InitDraw`/`FinishDraw`/
`InitDrawDone` overrides mirroring `EngineGL33`'s (`_frameOpen` flag, calls
`_bootstrap.BeginFrame(...)` to actually acquire the drawable + open the
encoder, `_textBank->StartFrame()/FinishFrame()`). `Clear()` is untouched
and still works for its narrow existing call sites — `BeginFrame()`'s
existing "first pass vs. mid-frame reuse" branch already handles either one
calling it first in a given frame.

**Verified**: the actual top-level main menu now renders — title art, all
menu items, the background scene's terrain/vehicles. Frame pacing is now
real (~40-50 FPS matching GL33, not thousands). `deltaT` reflects real
elapsed time, so the intro cutscene plays at the correct speed instead of
being frozen.

**Two more real bugs found and fixed alongside this** (both in
`EngineMTLBootstrap.cpp`, both pre-existing in the hardware-TL pipeline,
unmasked by the above fix actually letting frames render):
1. **Clear color was hardcoded to pure red** in the TL pass's
   `RenderPassColorAttachmentDescriptor` (`MTL::ClearColor::Make(1.0, 0.0,
   0.0, 1.0)` — a leftover debug override, literally commented `// DEBUG:
   force-red clear`). Fixed to use the actual passed-in `r,g,b,a`.
2. **Backface culling was never enabled anywhere** (`MTLCullModeNone` is
   Metal's default, and nothing ever called `setCullMode`/
   `setFrontFacingWinding`). Both triangle winding directions of every mesh
   were rendering — solid-hulled vehicles (e.g. the M113) showed their
   back-facing interior walls through gaps in the hull. Fixed:
   `setCullMode(MTL::CullModeBack)` + `setFrontFacingWinding(
   MTL::WindingClockwise)` in `DrawSectionTL`, matching GL33's default
   (`EngineGL33_Queue.cpp`: `cull::Back()` + `cull::FrontFaceCW()`).

**`displaySyncEnabled(true)`** was also added to the `CAMetalLayer` in
`SetupDevice()` as part of investigating the FPS issue — real vsync pacing
that GL33 gets for free from `SDL_GL_SwapWindow`, that Metal needs to ask
for explicitly. Kept as a correct default even though the `InitDraw` fix
turned out to be the actual cause of the unthrottled loop.

**Still open / found by interactive testing after this fix landed:**
- **A specific animated/multi-part model (a 3rd-person soldier w/ M16) renders
  as an exploded mass of long spike triangles** — body parts flung to wrong
  positions while still internally connected, like a bone/attachment
  transform gone wrong. Investigated and ruled out: the per-object camera-
  relative world transform (`PrepareMeshTL`) — checked for NaN/Inf and gross
  non-orthonormality across real captures, zero hits. Also ruled out: raw
  local-space vertex source data (`Shape::Pos()/Norm()` via
  `VertexBufferMTL::CopyVertices`) — bounding-box-sanity-checked down to a
  3-unit threshold for dynamic/animated shapes specifically, every hit was a
  legitimate normal-sized LOD level. **So the corruption is downstream of
  both of those** — leading suspects: the index buffer / fan-triangulation
  in `VertexBufferMTL::Init()` (no `poly.N() >= 3` guard, unlike GL33's
  `PoseidonAssert`), or `VertexBufferMTL::Update()`/`UpdateMeshBuffer()` not
  validating that a dynamic shape's vertex count hasn't changed since
  `Init()` (`UpdateMeshBuffer` silently drops the write if `byteSize >
  buf->length()` — would leave the GPU buffer stale while section/index
  ranges assume the new layout, classic out-of-bounds-read territory).
  A tank's camo netting shows a similar (smaller-scale) spiky artifact and is
  much more reliably reproducible than the soldier (same point in the menu's
  background scene every run) — better repro target than the soldier for
  next session. A real GPU Frame Capture of the corrupted draw call (Xcode →
  attach or launch via a scheme → Debug → Capture GPU Frame, inspect the
  bound vertex/index buffer contents directly) will settle this far faster
  than more printf-style instrumentation.
- **Some terrain still shows holes/missing patches.** Not yet root-caused.
  Candidate: `EngineMTL` never overrides `GetTLOnSurface()` (base class
  defaults to `false`; GL33 explicitly returns `true`,
  `EngineGL33.hpp:797`) — under Metal this forces all `OnSurface`-flagged
  content (roads, ground decals) through the legacy/software path
  unconditionally regardless of whether a GPU buffer exists for it. Base
  terrain ground itself draws with `spec=0` (not `OnSurface`) so this
  specific gap shouldn't affect it directly, but is still a real, easy,
  worthwhile fix on its own.
- **Minor residual ghosting in the black letterbox margins** outside the
  actual rendered viewport (window mode, aspect-ratio bars) — cosmetic, low
  priority, not investigated this session either.

<details>
<summary>Superseded theory (kept for history, was wrong)</summary>

The previous write-up of this section speculated the black main menu was
caused by the `GModeIntro` intro-mission world failing to load/start under
Metal, based on the render log showing zero lines mentioning "intro" and
audio being silent. Both of those signals turned out to be artifacts of
*other* bugs (the log-capture working directory, and the audio dlopen bug,
respectively) — the intro mission loads and plays fine; it just never got
drawn because of the `InitDraw` gap above.

</details>

## Verification commands

```sh
export VCPKG_ROOT=/Users/alex/vcpkg
cmake --build build/macos-arm64-clang-rwdi --target PoseidonGame -j8

# Windowed is important for debugging -- fullscreen puts the game on its own
# macOS Space, which makes `screencapture`/System Events unreliable. -w/-h
# bump the window past the default 800x600 -- much easier to read screenshots.
cd packages/Remaster
/Users/alex/Projects/Poseidon-ARM64/build/macos-arm64-clang-rwdi/apps/cwr/Game/PoseidonGame \
  --render mtl --window -w 1600 -h 1200

# GL33 baseline for comparison, same flags pattern:
/Users/alex/Projects/Poseidon-ARM64/build/macos-arm64-clang-rwdi/apps/cwr/Game/PoseidonGame \
  --render gl33 --window -w 1600 -h 1200
```

No debug logging is currently left in the tree as of this commit. This
session's instrumentation (per-frame path-usage tallies, `nextDrawable()`
timing, `Clear`/`BeginFrame`/`EndFrame` entry counters, a `PrepareMeshTL`
transform-anomaly check, a `CopyVertices` local-bbox-anomaly check) all
served their diagnostic purpose and were removed before committing — see
git log for what they found rather than re-adding blind. If you need similar
instrumentation again, the working pattern throughout this file is a
`static long long` call counter + `LOG_INFO` gated to `% N == 1` (or `< N`
for a one-time cap), e.g. `EngineMTL::Clear`'s `sBeginFailCount`.

## Current working-tree state (branch `feature/metal-renderer`)

Committed. `git log --oneline -5` on this branch for the actual commits;
don't trust a stale file listing here to stay accurate across sessions.

## Key files

| File | Purpose |
|------|---------|
| `engine/PoseidonMTL/EngineMTL.{hpp,cpp}` | The real `Poseidon::Engine` backend — 2D + legacy software-TL + hardware-TL mesh paths |
| `engine/PoseidonMTL/EngineMTLBootstrap.{hpp,cpp}` | Metal device/layer/queue/pipeline/texture/mesh-buffer wrapper (Engine-agnostic, metal-cpp-isolated) |
| `engine/PoseidonMTL/VertexBufferMTL.{hpp,cpp}` | Per-Shape GPU vertex/index buffer for the hardware-TL path |
| `engine/PoseidonMTL/TextureMTL.{hpp,cpp}` / `TextBankMTL.{hpp,cpp}` | Real PAA decode + GPU texture upload |
| `engine/PoseidonMTL/GraphicsBackendMTL.cpp` | Factory registration (`"mtl"`) |
| `engine/PoseidonMTL/MetalCppImpl.cpp` | metal-cpp's `*_PRIVATE_IMPLEMENTATION` macros (one definition per binary) |
| `apps/tools/MetalSmokeTest/` | Milestone-0 standalone smoke test |

Reference (GL33, what the Metal hardware-TL path was ported from):
`engine/PoseidonGL33/EngineGL33_VertexBuffer.cpp`,
`EngineGL33_Mesh.cpp`, `EngineGL33_Material.cpp`, `EngineGL33_Shaders.cpp`.
