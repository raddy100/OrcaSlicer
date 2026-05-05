# OrcaSlicer — Copilot Instructions

OrcaSlicer is an open-source C++17 3D slicer with a wxWidgets GUI built on a CMake build system.

## Build Commands

```bash
# macOS
cmake --build build/arm64 --config RelWithDebInfo --target all --

# Linux
cmake --build build --config RelWithDebInfo --target all --

# Windows (replace %build_type% with Debug/Release/RelWithDebInfo)
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

Dependencies are built separately first: `cd deps && mkdir build && cd build && cmake .. && cmake --build .`

## Testing

Catch2 v2 framework. Tests live in `tests/` mirroring the architecture.

```bash
# All tests
cd build && ctest --output-on-failure

# Single test suite
ctest --test-dir ./tests/libslic3r
ctest --test-dir ./tests/fff_print
ctest --test-dir ./tests/sla_print

# Single test case (run binary directly with filter)
./tests/libslic3r/libslic3r_tests "[Geometry]"
./tests/libslic3r/libslic3r_tests "Test case name"

# Recommended test flags (randomize order, warn on missing assertions)
./tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions
```

Test suites: `libslic3r` (core), `fff_print` (FFF slicing), `sla_print` (resin), `libnest2d` (2D nesting), `slic3rutils` (utilities).

## Architecture Overview

The codebase splits cleanly into two layers:

### Core Library: `src/libslic3r/`
Platform-independent slicing engine. Everything that does the actual work:
- **`Print.cpp` / `Print.hpp`** — top-level FFF slicing orchestrator; owns `PrintObject`s, drives the pipeline
- **`PrintConfig.cpp` / `PrintConfig.hpp`** — all print/printer/filament settings as typed `ConfigOption`s; add new settings here
- **`Model.cpp` / `Model.hpp`** — scene graph: `Model` → `ModelObject` → `ModelInstance` → `ModelVolume`
- **`GCode/`** — G-code generation pipeline; `GCode.cpp` is the main entry, sub-modules handle cooling, wipe tower, seam, pressure equalizer, etc.
- **`Fill/`** — infill pattern implementations (rectilinear, gyroid, honeycomb, lightning, TPMS, …)
- **`Support/`** — normal and tree support generation
- **`Arachne/`** — variable-width perimeter engine (Arachne algorithm)
- **`Format/`** — file I/O: `bbs_3mf.cpp` for the native `.3mf` dialect, plus STL, OBJ, AMF, STEP, SVG
- **`Geometry/`** — geometric primitives and algorithms
- **`Preset.cpp` / `PresetBundle.cpp`** — preset loading/saving; presets live in `resources/profiles/[Manufacturer]/`

### GUI: `src/slic3r/GUI/`
wxWidgets application, all UI code:
- **`GUI_App.cpp`** — application object, owns the main frame and preset bundle
- **`MainFrame.cpp`** — top-level window
- **`Plater.cpp`** — central 3D workspace; manages part plates, background slicing, model manipulation
- **`BackgroundSlicingProcess.cpp`** — runs slicing on a background thread, fires wxEvents on completion
- **`Tab.cpp`** — settings tabs (Print / Filament / Printer); wires `PrintConfig` options to UI fields
- **`GLCanvas3D.cpp`** — OpenGL 3D viewport
- **`Jobs/`** — async operations (arrange, orient, emboss, send-to-printer) via `Worker`/`Job` pattern
- **`Gizmos/`** — 3D manipulation tools (move, scale, cut, support paint, …)
- **`Widgets/`** — custom wxWidgets controls

### Application Entry
- **`src/OrcaSlicer.cpp`** — `main()` / `WinMain()`

## Key Conventions

### Config System
Settings are declared with macros in `PrintConfig.cpp` (search for `OPTION_DEF` macros). Each option has a type (`ConfigOptionFloat`, `ConfigOptionEnum<>`, etc.), a default, and metadata. The config splits into:
- `PrintObjectConfig` — per-object settings
- `PrintRegionConfig` — per-region (modifier mesh) settings
- `PrintConfig` / `GCodeConfig` — global print/G-code settings
- `FullPrintConfig` — union of all of the above

### Preset / Profile Format
Printer profiles are JSON files under `resources/profiles/[Manufacturer]/`. The manifest is `[Manufacturer].json`. Presets inherit from a `"base"` key. **Version migration is required** when changing settings; bump the schema version and handle old values in `Preset.cpp`.

### 3MF Dialect
OrcaSlicer uses an extended BambuLab-derived `.3mf` format (`src/libslic3r/Format/bbs_3mf.cpp`). Backward compatibility with existing project files is mandatory — never remove or rename serialized keys.

### Background Jobs (GUI)
Long-running GUI work uses `Jobs/Worker` + `Jobs/Job`. Derive from `Job`, implement `process()` (runs on worker thread) and `finalize()` (runs on UI thread). Never call wxWidgets APIs from `process()`.

### Code Style
- C++17; selective C++20. 4-space indent, 140-column limit (enforced by `.clang-format`)
- `PascalCase` for types/classes, `snake_case` for functions and variables
- `#pragma once` for all headers
- Smart pointers and RAII; raw owning pointers are a bug
- Parallelization via Intel TBB (`tbb::parallel_for`, etc.) — guard shared state carefully
- Namespace: `Slic3r` for core library, `Slic3r::GUI` for GUI code

### Catch2 Test Rules
- Use `DYNAMIC_SECTION("name " << i)` inside loops — never reuse a static section name
- Never call `REQUIRE`/`CHECK` from worker threads; collect results and assert on the main thread
- Prefer separate `REQUIRE(a > 0); REQUIRE(b < 10);` over `REQUIRE(a > 0 && b < 10)`
- Use `WithinAbs` / `WithinRel` matchers for floating-point; `Catch::Approx` is deprecated
- New test files: `test_<feature>.cpp`, placed in the matching suite directory, registered in that directory's `CMakeLists.txt`

### Cross-Platform
All changes must compile and behave correctly on Windows, macOS, and Linux. Platform-specific code goes in `.mm` (macOS/ObjC) or guarded with `#ifdef _WIN32` / `#ifdef __APPLE__`.

### Slicing Pipeline (FFF)
`Plater` → `BackgroundSlicingProcess` → `Print::process()` →
1. `PrintObject::slice()` — triangle mesh → layer slices
2. `PrintObject::make_perimeters()` — perimeter/wall generation (Arachne)
3. `PrintObject::infill()` — infill generation
4. `PrintObject::generate_support_material()` — support
5. `GCode::do_export()` — G-code output
