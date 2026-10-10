# Offline Windows host profiling

`windows/build/profile_host.py` builds the current `dlss-5-amd-project` Mochizuki
adapter, an offline D3D12/Vulkan timing program, the host's existing ABI/GPU
fixture, and full-precision encoding/composition comparisons. It reads the host
checkout and writes only below this repository's ignored `build/` directory.
It requires the matching host source with `nr_profile.hpp`, `MochizukiProfile.h`
and the linear-input binding integration, plus the host's pinned Vulkan headers
and glslang. Run from an x64 MSVC developer prompt. No model is copied or shipped.

For incremental shader builds, ISA checks before performance screening, and
automatic wall-clock stage records, use the
[offline experiment workflow](windows-experiment-workflow.md).

```powershell
python -X utf8 windows/build/profile_host.py --host ../dlss-5-amd-project
```

The default output is `build/windows/host-profile`. Use `--out` for a different
subdirectory of `build/`; `--production` compiles the same sources without the
diagnostic exports or additional timestamps. Both are isolated offline builds,
not installation packages. `offline-build.json` records source/artifact hashes
and compiler arguments; it is not a host release manifest.

Prepare a private asset directory containing the matching build's
`dlssnr-amd/shaders/` and the user's own extracted `dlssnr-amd/dlssnr.bin`.
Never publish that directory, its model, or local pipeline caches.

## Measurement

The timing executable takes positional arguments:

```text
profile_host.exe DLL ASSETS CSV DEPTH DETAILED FRAMES WARMUP WIDTH HEIGHT OUTPUT [PHOTO] [BLOCKS] [--same-path]
```

- `DEPTH`: 1 for serial diagnostics, 3 for three frames in flight.
- `DETAILED`: 0 for two D3D12 timestamps around the complete NR route; 1 also
  records D3D12 input/output intervals and ten Vulkan segment boundaries.
- `FRAMES` is the number of measured frames per block; warmup is excluded.
- Input is R11G11B10_FLOAT, linear, one pass, scale 1, no motion/history or
  preprocessing. Optional `PHOTO` contains exactly width×height×4 FP32 values;
  otherwise the host fixture supplies a deterministic gradient/checker input.
- `BLOCKS` defaults to 1. A diagnostic DLL with multiple blocks alternates the
  original and direct-input routes as A/B/B/A. `--same-path` keeps every block
  on the original route for a control measurement. Supply an empty `PHOTO`
  argument to reach `BLOCKS` when using the generated input.
- `OUTPUT` is a final packed image readback outside all timed frames. For A/B
  blocks it reflects the last selected path, not a comparison of every frame.

For example, after setting `$assets` to the private asset directory:

```powershell
$study = (Resolve-Path build/windows/host-profile).Path
$nativeArgs = @(
    "$study/MochizukiNrRuntime.dll", $assets, "$study/segments.csv",
    '3', '1', '200', '100', '2258', '1270', "$study/output.raw"
)
& "$study/profile_host.exe" @nativeArgs
```

For low-overhead A/B measurement use `DETAILED=0`, a diagnostic DLL, and 12 or
24 blocks. Ordinary production-check DLLs accept one block and `DETAILED=0`.
Capture serial and pipelined cases separately. Avoid concurrent GPU benchmarks
or game load; repeat in reversed order and retain all batches.

## Timestamp scope and ownership

The harness follows the current recording ABI: prepare and record, Begin before
producer submission, enqueue Vulkan, submit the consumer, then End with the
actual D3D12 tail fence. Each query range is associated with a submission serial.
The harness waits for its consumer tail before reading D3D12 queries; the
diagnostic runtime additionally checks the matching Vulkan slot fence before
returning its queries. A serial mismatch or overwritten slot fails the run.
Command allocators, recordings and query ranges are reused only after completion.

| CSV field | Scope |
| --- | --- |
| `total_gpu_ms` | D3D12 input copy through D3D12 output copy, including the Vulkan round trip |
| `completion_interval_ms` | Distance between consecutive completed NR intervals on the same D3D12 queue; also includes the gap between them |
| `d3d_input_ms`, `d3d_output_ms` | D3D12 texture↔shared-buffer work and its resource transitions |
| `vk_upload_ms`, `vk_download_ms` | Outer Vulkan shared-buffer↔image work, including applicable padding/transitions |
| `input_convert_ms`, `input_encode_ms` | Core input conversion/preparation and linear encoding; direct input removes the conversion dispatch |
| `pre_network_ms`, `operators_ms` | Work preceding the network loop and the single-pass network dispatch loop |
| `compose_ms`, `core_writeback_ms` | Work after the loop through composition, then alpha restoration/format conversion/writeback |
| `handoff_residual_ms` | D3D12 wait interval minus that submission's Vulkan elapsed interval; combines submission gaps and both handoffs |
| `cpu_*` | CPU prepare/record/enqueue/submit and explicit waits; not additive GPU execution time |

Vulkan and D3D12 raw timestamps are never subtracted from each other. The
handoff residual uses separately converted elapsed durations and cannot locate
each directional queue wait. Segment diagnostics include timestamp overhead;
use the separate low-overhead mode for final total-cost comparisons. The
existing runtime's usual timing pair remains enabled in both modes.

This measures the NR host route, not a game's frame, FSR4, XeSS FG, or target-size
temporal gameplay. The legacy UI's network measurement covers the core's full
`record_all`, including internal encoding/composition; it is not just operators.

## Correctness checks

```powershell
& "$study/check_encode.exe" "$study/dlssnr-amd/shaders/runtime"
& "$study/check_direct_original.exe" "$study/dlssnr-amd/shaders/runtime"
& "$study/host_contract.exe" "$study/MochizukiNrRuntime.dll"
& "$study/host_contract.exe" "$study/MochizukiNrRuntime.dll" $assets
```

`check_encode` compares the original nearest-blit→encode path with direct sampled
encoding at full FP32 proxy/keep precision. It covers 2258×1270 R11G11B10 and
RGBA16F finite input patterns, including subnormals, negative half floats,
large HDR values and alpha, at white points 0.01/1/2/100. It fails on any changed
32-bit value. It does not assert equivalence for NaN/Inf payloads or other GPUs.
The existing host fixture checks ABI, replay, two queues, geometry retention,
history ordering, formats, DRS, cancellation and completed-tail destruction.

`check_direct_original` compares the FP32 proxy and composition results when the
transfer pass samples the original packed image instead of its RGBA32F copy.
It checks both input formats at 257x129, four white points and five composition
settings, including finite half encodings, subnormals, negative half values and
HDR values. All 48 arrays (6,365,376 32-bit values) must match exactly. This does
not cover NaN/Inf payloads or replace the independent full-output and host
lifetime checks. The extra `runtime_encode_direct.spv` is required by a matching
runtime build; the previous encode variants remain available for fallback.

Compare separately saved full outputs as well. Passing an encode probe or a
host fixture alone does not establish real-game performance or image quality.

`check_packed_output` compares nearest-blit conversion with R11G11B10 storage
writes over finite format boundaries, then checks the actual composition with
four white points and five control settings. R11G11B10 composition reads and
writes the same texel, matching the runtime's alias; RGBA16F inputs use separate
packed output for arithmetic coverage. The runtime enables this route only for
eligible linear R11G11B10 frames when the host enabled
`shaderStorageImageExtendedFormats`, the format supports storage, and the image
has storage usage. Other routes keep the existing output conversion.

```powershell
& "$study/check_packed_output.exe" "$study/dlssnr-amd/shaders/runtime" "$study/check_packed_output.spv"
```

The probe requests the optional device feature only in its own test context.
Its conversion shader is an offline test artifact; `runtime_transfer_packed.spv`
is the additional runtime module. The probe does not establish NaN/Inf payload
equivalence or results on other GPUs/drivers.
