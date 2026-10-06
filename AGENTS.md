# Repository Guidelines

## Fork Work Context

Read [.ai/README.md](.ai/README.md) and its context/status links before continuing fork work.
`CLAUDE.md` remains the authority for implementation invariants. `.ai/` records goals,
experiments and host integration status; it does not duplicate the implementation manual.
Chinese prose is allowed in `.ai/`; filenames, code identifiers and commands stay in English.
Optional quality-reducing experiments must follow `.ai/decisions.md`: independent controls,
off by default, with the original path restored when disabled. The default path retains
the existing precision floor. Machine-specific paths belong only in ignored `.ai/local/`.

## Project Structure & Module Organization

`linux/` is the reference implementation; `windows/` is an intentionally separate copy with AMD Windows driver adaptations. Apply shared fixes deliberately to both; keep platform workarounds local.

- `*/src/core/`: C++ network runtime, graph, and Vulkan planning.
- `*/src/pe/`: NGX backend, ReShade add-on, and game interop.
- `*/shaders/rdna4/` and `*/shaders/passes/`: GLSL network kernels and supporting passes.
- `*/build/`, `*/vulkan-loader/`, and `*/package/`: build scripts, loader patches, installers, and model extraction tools.
- `docs/`: architecture and NGX verification results, inputs, and comparison images.

Read `CLAUDE.md` for implementation invariants. Root `build/`, `toolchain/`, and `artifacts/` contain generated or downloaded files; do not commit them.

## Build, Test, and Development Commands

Build on Linux (tested on Ubuntu 24.04), including Windows DLLs via MinGW. Install prerequisites with `sudo apt install git curl python3 cmake ninja-build mingw-w64`.

```sh
bash fetch_deps.sh                          # Fetch pinned dependencies
bash linux/build/build_vulkan_loader.sh     # Build the patched Vulkan loader
NR_ARCH=x86_64 bash linux/build/build_package.sh # Build Linux package
NR_ARCH=i686 bash linux/build/build_package.sh   # Build 32-bit package
bash fetch_deps.sh --windows                # Fetch Windows dependencies
bash windows/build/build_package.sh         # Build Windows preview package
```

Packages land in their platform's `package/` directory. Use the corresponding installer and README for in-game runs.

## Coding Style & Naming Conventions

Follow surrounding formatting: C++17 and Python generally use four-space indentation; C++ opening braces stay on the declaration line. Preserve existing `nr_` filenames, snake_case functions, and PascalCase types. No repository-wide formatter or linter is configured. Write comments and user-facing text in English; explain constraints and measurements.

Keep host defines in `*/build/arch/rdna4.sh` aligned with shader `pipelines.json`. Preserve operation precision; register new weight-reading kernels in `nr_graph.cpp` packing.

## Testing Guidelines

No automated unit-test suite or coverage threshold is configured. Rebuild affected platforms and validate relevant game routes on supported hardware. For arithmetic-preserving shader changes, compare against a separately saved baseline:

```sh
python3 linux/build/build_network.py rdna4 --check /path/to/baseline
```

Use `docs/ngx-verification/NGX-VERIFICATION.md` for image-quality methodology. Report GPU, driver, resolution, route, and repeated frame-time measurements.

## Commit & Pull Request Guidelines

History uses descriptive, plain-English subjects, sometimes prefixed by a topic (`Preprocess:`, `Package names:`). Keep commits focused. PRs should explain behavior changes, affected platforms, validation, and related issues; include comparison images for rendering changes.

## Model Distribution

Never commit NVIDIA DLLs, extracted tensors, weights, `dlssnr.bin`, or packages containing them. Extract models only from the user's own DLL; keep `NR_MODEL` packages private.
