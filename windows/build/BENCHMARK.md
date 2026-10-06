# Native Windows network benchmark

Run `build_benchmark.py` in an x64 MSVC developer shell, from the repository root.
It reads the production host defines from `arch/rdna4.sh`. Supply existing Vulkan
headers and an x64 Vulkan import library; it installs no toolchain.

```powershell
python windows/build/build_benchmark.py --headers <vulkan-include> --vulkan-lib <vulkan-1.lib>
build/windows/benchmark/nr_benchmark.exe 2258 1270 build/windows/benchmark/native.plan --reuse --model-pack <private-model> --spv-dir <saved-shaders> --pipeline-cache build/windows/benchmark/pipeline.cache --warmup 100 --repeats 200 --out-image build/windows/benchmark/output.rgba
```

The first three arguments are source width, source height and the generated plan
file. The existing `nr_graph` options follow. `--reuse` matches the runtime's arena
policy. The native planner reports the working extent separately from the source
extent (2258x1270 currently uses 2368x1280). This is network-only timing, not game
or D3D12/Vulkan interop timing. Warmup/repeats must be positive and bounded; avoid
long submissions that can trigger Windows GPU timeout recovery.

Add `--per-layer` to attribute time to kernels. Then remove it for final total-time
comparisons, since timestamp instrumentation can affect scheduling. With no
`--in-image`, the input is a synthetic gradient. An explicit input must contain
exactly width * height * 4 little-endian FP32 values in RGBA order. Preserve an
independent baseline output and compare the complete output, not just its mean.
This runner does not validate temporal sequences or the host's input conversions.

Run baseline/candidate in alternating order under the same workload, after closing
games and other GPU workloads. Repeat measurements; record compiler/driver and
shader identity. Native Windows sysfs clock telemetry is unavailable: do not
interpret the absence of clock readings as a measured clock of zero.

For driver register/LDS statistics, build into a separate directory with
`--pipeline-stats`. This enables the existing `NR_PIPELINE_STATS` diagnostic.
When supported by the driver, setting `NR_DUMP_IR` to an existing directory also
captures its internal representations. Use the ordinary build for final timing.

## Small ViT contraction

The Windows recipe includes `gemmprojws`, an eight-wave variant of `gemmprojw`
with the same 64x128 output tile, packed weights and ascending K reductions.
The production host selects it for ViT contractions with 768..1024 tokens;
larger contractions retain the original variant. The shader manifest and host
must agree on `projws_max_tokens`; rebuild both together.

Use the same current executable and complete shader directory on both sides of
an A/B run. Add `--no-projws` for the baseline and omit it for the candidate.
This diagnostic restores the original dispatch choice without changing precision.
At 2258x1270, six interleaved pairs without per-layer instrumentation measured
10.695 ms versus 10.504 ms on RX 9070 XT / Windows driver 32.0.31041.1004.
See [EXP-002](../../.ai/experiments/EXP-002-windows-small-vit-gemm.md) for inputs,
rejected alternatives, complete-output checks and the limits of that result.

## Host C256 edge profile

Some Mochizuki host recipes disable edge-specialized bodies to reduce first-build
time. The following tool restores them only for `fswinpds256` and `fswinpup256`.
The upstream Windows recipe already enables them, so this is a host-specific
restoration, not a new speedup over that upstream configuration.

```powershell
python windows/build/build_edge_profile.py --source-root <host-vendored-mochizuki> --baseline <saved-host-shaders> --glslang <pinned-glslang.exe> --out build/windows/c256-edge-shaders
```

Keep the host's source tree and compiler patches. The tool first rebuilds both
baseline shaders with `NR_EDGE_BODIES=0` and requires exact SPIR-V equality with
the saved baseline. It then generates the candidate with the flag enabled. It
requires a new output directory and writes asset hashes to `profile.json`.
The output contains shader assets only, no model, DLL or pipeline cache.

There is a first-compilation cost. The measured benefit at 2258x1270 was small;
see [the experiment record](../../.ai/experiments/EXP-001-windows-2258-baseline.md).
These assets are for benchmark/integration work, not a standalone user update.
Host integration must rebuild and validate the complete matching package before
game deployment. Do not overwrite game files with a partial experimental package.
