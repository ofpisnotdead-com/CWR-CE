# PoseidonStudio UI tests

`PoseidonStudioTests` drives `StudioApp::render()` in a headless SDL + ImGui loop
using the [Dear ImGui Test Engine](https://github.com/ocornut/imgui_test_engine).
It covers the Studio asset browser, preview panels, config search, the game VFS,
cross-references, multiselect/grid, and the log panel against the synthetic
fixtures in `tests/fixtures/studio`.

The suite needs the `test-engine` feature on the `imgui` dependency (already set
in `vcpkg.json`). Each ImGui test case is registered with CTest:

```
ctest -R PoseidonStudioTests
```

or run the binary directly with the fixtures directory as its argument:

```
PoseidonStudioTests <fixtures_dir>
```
