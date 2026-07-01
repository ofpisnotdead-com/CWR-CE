# Tests

## Structure

| Directory | Framework | What |
|-----------|-----------|------|
| `unit/` | Catch2 / ImGui Test Engine | C++ unit tests for engine and apps |
| `integration/` | Trident (tri) | SQF-driven in-game scenarios |
| `smoke/` | Pester | Boot log verification |
| `fixtures/` | — | Test data files (P3D, PAA, PBO, RTM, configs, audio, etc.) |

## Unit Test Suites (`unit/`)

| Suite | Location | Tests |
|-------|----------|-------|
| PoseidonFoundationTests | `unit/engine/Poseidon/Foundation/` | Low-level Poseidon foundations (containers, strings, memory, threads, IO/runtime glue) |
| PoseidonCoreTests | `unit/engine/Poseidon/` | Folded Poseidon core support coverage: evaluator, config/ParamFile, QStream, locale/stringtable, preproc, CSV |
| PoseidonTests | `unit/engine/Poseidon/` | Engine — P3D formats, graphics, audio, core, AI, entities |
| PoseidonTests (`[config]`) | `unit/engine/Poseidon/Core/` | Config store, profiles |
| PoseidonTests (`[graphics]`) | `unit/engine/Poseidon/Graphics/` | Graphics + shader compilation coverage |
| PoseidonServerTests | `unit/apps/Server/` | Server simulate mode |
| PoseidonEvaluatorTests | `unit/apps/Evaluator/` | SQF evaluation and evaluator tooling layer |

## Running

Build first with a CMake preset from the repository root:

```sh
cmake --preset linux-x64-clang-rwdi
cmake --build build/linux-x64-clang-rwdi
```

Run CTest against the build directory:

```sh
ctest --test-dir build/linux-x64-clang-rwdi --output-on-failure
ctest --test-dir build/linux-x64-clang-rwdi -R "<test name>" --output-on-failure
```

Use the matching build directory for other presets, for example
`build/win-x64-clang-rwdi` on Windows.

Trident integration, screenshot, and other game-data-backed tests need a local
`.trident.env` file. Copy `.trident.env.example` to `.trident.env`, set
`OFPR_GAME_DIR` to the build or distribution directory containing the binaries,
and set `OFPR_DATA_DIR` to the Demo game data. The recommended local layout is
`packages/Demo`; the whole `packages/` tree is ignored by Git.

Build the Rust workspace before running integration tests:

```sh
cargo build
```

The binary is `tri` and lives under `target/debug/` for the default Cargo build.
Put that directory on `PATH`, or call the binary by relative path:

```sh
tri test  -j6 --retries 2 tests/integration
./target/debug/tri test -j6 --retries 2 tests/integration
```

On Windows, use `target\debug\tri.exe` when calling it directly.
