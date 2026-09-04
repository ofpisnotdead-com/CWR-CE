# Third-Party Dependencies

Vendored libraries and headers included directly in the build.

| Directory | License | Description |
|-----------|---------|-------------|
| `glad/` | Generated | OpenGL 4.5 Core loader |
| `renderdoc/` | MIT | RenderDoc in-application API header |
| `sse2neon/` | MIT | x86 SSE intrinsics implemented on top of ARM NEON (Apple Silicon) |
| `imgui_backends/` | MIT | ImGui's Metal rendering backend (imgui_impl_metal) |

Additional dependencies managed via **vcpkg** (see `vcpkg.json`):
catch2, cli11, stb, mimalloc, freetype, sdl3, openal-soft, opus, enkits, imgui,
spdlog, and Apple's Apache-2.0-licensed metal-cpp headers.
