# Third-Party Dependencies

Vendored libraries and headers included directly in the build.

| Directory | License | Description |
|-----------|---------|-------------|
| `glad/` | Generated | OpenGL 4.5 Core loader |
| `renderdoc/` | MIT | RenderDoc in-application API header |
| `sse2neon/` | MIT | x86 SSE intrinsic API implemented on ARM NEON, from [DLTcollab/sse2neon](https://github.com/DLTcollab/sse2neon) — lets the engine's legacy x86 SIMD math/shape code (`V3QuadsP3.cpp`, `Occlusion.cpp`, etc.) compile unchanged on Apple Silicon. See `engine/Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp` for the dispatch + the handful of legacy MMX intrinsics sse2neon doesn't cover. |

Additional dependencies managed via **vcpkg** (see `vcpkg.json`):
catch2, cli11, stb, mimalloc, freetype, sdl3, openal-soft, opus, enkits, imgui, spdlog.
