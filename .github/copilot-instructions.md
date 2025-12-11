# AMP Development Forge - Copilot Instructions

## Repository Overview

**AMP Development Forge** is a development environment for AI media processing pipelines, focusing on real-time video inference using GStreamer elements with ONNX Runtime. The repository builds custom GStreamer plugins (`ampinfer`, `ampperformance`, `ampsink`, etc.) that integrate AI models into video processing pipelines.

**Project Type**: C++20/C11 GStreamer plugin development with Python utilities  
**Build System**: Meson + Ninja  
**Key Technologies**: GStreamer 1.0, ONNX Runtime 1.18.1, Debian Trixie container environment  
**Repository Size**: Small-medium (6 GStreamer elements + common library)

## Critical: This is a DevContainer-Only Project

**IMPORTANT**: All build and test commands MUST be run inside the DevContainer. The host environment lacks necessary dependencies (GStreamer, ONNX Runtime, meson, etc.). Never attempt to build or test outside the container.

### DevContainer Setup (One-Time)
1. Run `./setup-container.sh` on host and select platform (option 1: Default container)
2. This generates `.devcontainer/Dockerfile` and `.devcontainer/devcontainer.json`
3. Open in VS Code and select "Reopen in Container"
4. Container automatically runs `.devcontainer/setup.sh` which:
   - Downloads and installs ONNX Runtime (x64 or aarch64) to `deps/onnxruntime/`
   - Sets up Python venv for lazer tool
   - Installs pre-commit hooks
   - Configures environment variables

## Project Structure

```
/work/                              # Container workspace root
├── .github/workflows/              # CI workflows
│   └── clang-format-check.yml     # Formatting check on PRs
├── .devcontainer/                 # Container configuration
│   ├── Dockerfile.template        # Base container definition
│   ├── devcontainer.template.json # VS Code devcontainer config
│   └── setup.sh                   # Post-create setup script
├── development/                   # Main C++ project
│   ├── meson.build               # Root build file
│   ├── meson.options             # Build options
│   ├── common/                   # Shared library code
│   │   ├── PerformanceTracer.{h,cpp}  # Timing framework
│   │   ├── amp/                  # Utility headers
│   │   ├── gst/                  # GStreamer utilities
│   │   └── onnx/                 # ONNX inference wrappers
│   ├── elements/                 # GStreamer plugin implementations
│   │   ├── ampinfer/            # ONNX inference element
│   │   ├── ampinferonnx/        # Alternative ONNX inference
│   │   ├── ampinferpre/         # Preprocessing element
│   │   ├── ampinferpost/        # Postprocessing element
│   │   ├── ampperformance/      # Performance overlay element
│   │   └── ampsink/             # WebRTC sink element
│   └── subprojects/             # Meson dependencies
│       ├── uniflow.wrap         # Uniflow inference library
│       └── cairo.wrap           # Cairo graphics
├── scripts/                      # Build/test scripts
│   ├── build-elements.sh        # Build wrapper
│   ├── test-elements.sh         # Test pipeline runner
│   └── shtools.sh               # Shared utilities
├── tools/lazer/                 # Python CLI tool (optional)
├── etc/                         # Test assets
│   ├── models/                  # ONNX models (yolov8n, blazeface)
│   ├── videos/                  # Test videos
│   └── images/                  # Test images
└── docs/                        # Documentation
```

## Build Instructions

**ALWAYS build inside the DevContainer.** Build artifacts go to `/work/development/build/`.

### Clean Build (Recommended for Major Changes)
```bash
./scripts/build-elements.sh clean  # Remove build directory
./scripts/build-elements.sh debug  # Fresh debug build
```

### Debug Build (Default for Development)
```bash
./scripts/build-elements.sh debug
```
- First run: Creates build dir with `meson setup --buildtype=debug`
- Subsequent runs: Only recompiles changed files
- Generates `compile_commands.json` for clangd IntelliSense
- Build time: ~30-60 seconds (first build), ~5-15 seconds (incremental)

### Release Build (Optimized)
```bash
./scripts/build-elements.sh release
```
- Uses `-Doptimization=3 -Db_lto=true -Dstrip=true`
- Build time: ~60-90 seconds (LTO adds overhead)

### Build Output
- Plugins: `/work/development/build/*.so` (shared modules)
- `GST_PLUGIN_PATH` automatically set to `/work/development/build`

### Common Build Issues
1. **"uniflow dependency not found"**: Meson automatically fetches subproject dependencies from `subprojects/*.wrap` files during build configuration. Ensure network access and retry build.
2. **"onnxruntime not found"**: Check `deps/onnxruntime/` exists. Re-run `.devcontainer/setup.sh` if missing.
3. **Stale build state**: Use `./scripts/build-elements.sh clean` then rebuild.

## Testing

**Tests are pipeline executions**, not unit tests. Test commands verify elements work in GStreamer pipelines.

### Test Pipeline Examples
```bash
./scripts/test-elements.sh onnx      # YOLOv8n ONNX inference test with overlay
./scripts/test-elements.sh onnxweb   # YOLOv8n ONNX inference with WebRTC output
```

### Test Requirements
- Build must complete successfully first
- Test assets in `/work/etc/` (models, videos, images)
- For UDP output tests: Run `ffplay` on host (see README.md)
- Expected output: Pipeline runs, processes video, no GStreamer errors

### Test Failures
- **"directory /work/development/build does not exist"**: Run build first
- **"Missing tool: gst-launch-1.0"**: Only happens outside container
- **"No element 'ampinfer'"**: `GST_PLUGIN_PATH` not set or build failed

## Code Formatting and Linting

**CRITICAL**: All C/C++ code MUST pass clang-format checks before merging.

### Pre-commit Hook (Automatic)
Pre-commit hooks are installed by `.devcontainer/setup.sh`. They run clang-format automatically on commit.

### Manual Formatting Check
```bash
clang-format --dry-run --Werror <file.cpp>  # Check only
clang-format -i <file.cpp>                  # Fix in-place
```

### Format All Changed Files
```bash
pre-commit run --all-files  # Run all hooks on all files
```

### CI Check (GitHub Actions)
- **Workflow**: `.github/workflows/clang-format-check.yml`
- **Trigger**: All pull requests
- **Container**: `debian:trixie` with `clang-format-19`
- **Check**: Runs `clang-format --dry-run --Werror` on changed C/C++ files
- **Pass Criteria**: Zero formatting violations

### Format Configuration
- **Style**: `.clang-format` (LLVM-based)
- **Column Limit**: 100
- **Indent**: 4 spaces
- **Braces**: Attach style (`if (x) {`)

### Fixing Format Failures
If CI fails:
1. Locally: `clang-format -i <failing-files>`
2. Commit formatting fixes
3. Push to update PR

## Environment Details

### Key Environment Variables
```bash
GST_PLUGIN_PATH=/work/development/build        # Plugin search path
GST_DEBUG=2                                    # GStreamer logging level
LD_LIBRARY_PATH=/work/deps/onnxruntime/lib    # ONNX runtime libs
```

### Dependencies
- **GStreamer 1.0**: Core framework + base/video/audio plugins
- **ONNX Runtime 1.18.1**: Downloaded by setup.sh to `deps/onnxruntime/`
- **Uniflow**: Inference utilities (subproject, fetched via Meson wrap)
- **Cairo**: Graphics rendering (subproject, fetched via Meson wrap)
- **libsoup-3.0** / **json-glib**: For ampsink WebRTC (optional)

### Python Tools (tools/lazer)
Optional CLI tool. Setup by `.devcontainer/setup.sh`:
- Virtual env: `/work/tools/lazer/.venv`
- Auto-activated in interactive shells
- **Not required for C++ builds**

## Common Development Workflows

### Making C++ Changes to Elements
1. Edit element source (e.g., `development/elements/ampinfer/ampinfer.cpp`)
2. **ALWAYS format before commit**: `clang-format -i <file>`
3. Build: `./scripts/build-elements.sh debug`
4. Test: `./scripts/test-elements.sh onnx`
5. Verify: Check console output for errors

### Adding New GStreamer Element
1. Create directory: `development/elements/mynewelem/`
2. Add source: `mynewelem.cpp` and `meson.build`
3. Update `development/meson.build`: Add `subdir('elements/mynewelem')`
4. Follow format conventions from existing elements
5. Build and test as above

### Modifying Common Library
1. Edit files in `development/common/`
2. **All elements rebuild** when common changes (linked library)
3. Build time increases for common changes
4. Format all changed files

### VS Code IntelliSense Setup
After first build:
```bash
ln -s ./development/build/compile_commands.json .
```
Reload VS Code window. Requires clangd extension.

### Debugging with GDB
1. Install Microsoft C/C++ extension in VS Code
2. Create `.vscode/launch.json` (example in README.md)
3. Set breakpoints in element source
4. Run "Start Debugging" (F5)

## Important Notes for Coding Agents

1. **Never build outside container**: Host lacks dependencies. All commands in DevContainer only.
2. **Always clean build after dependency changes**: Meson caches aggressively.
3. **Format before every commit**: CI will fail otherwise. Use pre-commit hooks.
4. **Test assets are committed**: Models and videos in `/work/etc/` - do not delete.
5. **Build artifacts are gitignored**: `development/build/`, `.devcontainer/Dockerfile`, `.devcontainer/devcontainer.json`
6. **Pipeline tests replace unit tests**: Verify element behavior via `./scripts/test-elements.sh`
7. **IntelliSense requires symlink**: `compile_commands.json` must be in project root for clangd.
8. **Performance timing built-in**: Use `PerformanceTracer` for measurements (see `docs/PERFORMANCE_TRACER.md`).
9. **GStreamer plugins are `.so` files**: Built as shared modules, loaded via `GST_PLUGIN_PATH`.
10. **Setup script is idempotent**: Safe to re-run `.devcontainer/setup.sh` if environment broken.

## Trust These Instructions

This file contains validated commands and workflows. **Only search/explore if**:
- Instructions are incomplete for your specific task
- You encounter errors not documented here
- You need to understand implementation details beyond build/test

For build, test, format, and environment setup: **follow these instructions exactly**.
