# Efficient offline shader experiments

Use this workflow for new Windows shader scheduling/code-generation experiments.
It changes tool execution, not NR arithmetic, attempt accounting, or retention
criteria. Charge the hypothesis in its run ledger before implementing it. Keep
GPU work sequential. Do not restart an exhausted run to try these tools.

## Build and cache

Core shaders support opt-in caching:

```text
python -X utf8 windows/build/build_network.py rdna4 --glslang <pinned-glslang> --out build/windows/<study>/candidate --incremental
```

Omit `--incremental` for the independently saved clean build. Before retention,
rebuild without caching and use `--check <saved-cached-build>` to compare all
files recursively, including runtime and temporal modules and markers.

For a host checkout or isolated host source snapshot, preserve its own builder:

```text
python -X utf8 windows/build/build_network_cached.py --source-root <mochizuki-source-root> --out build/windows/<study>/candidate rdna4 --glslang <pinned-glslang>
```

The adapter wraps that checkout's `glslang` function, retaining its exact
unrolling and Windows compiler workarounds. Add `--clean` for a full rebuild,
and `--check <baseline>` for a recursive comparison. It reads host sources and
writes only under the core repository's ignored `build/` directory.
`profile_host.py --incremental` uses this adapter for its host shader build;
native DLLs and executables still rebuild normally.

Keys bind compiler bytes, preprocessed source (including actual transitive and
conditional includes), ordered defines, transformation scripts, Python version,
unroll/quad settings and `NR_Q32_DIRECT`. Cache restoration verifies SPIR-V magic
and the saved output hash. Corrupt/missing entries rebuild. On cache misses the
preprocessed input is checked again after compilation. Cache files live outside
the shader output tree, by default at `build/windows/shader-cache/`.
Fresh output trees still replace previous output; never point `--out` at the
independent baseline. Cache reuse skips compilation, not output-tree recreation.

## Check the mechanism before timing

First establish source-level semantic preservation and representative exact
outputs. Then inspect the affected modules using existing driver ISA/ELF tools.
For scheduling hypotheses, check the actual load, WMMA and wait positions;
descriptor register renaming, SPIR-V hashes and opcode counts are insufficient.
Use the driver ISA; follow EXP-028 for RDNA4 instruction-boundary limitations.
No new SQTT capture or decoder installation is required by this workflow.

Write a private `gate.json` after review. Its schema is:

```json
{
  "schema_version": 1,
  "gpu": "<verified GPU>",
  "driver": "<verified driver>",
  "semantic_argument": "<why precision, rounding, reductions and synchronization are preserved>",
  "baseline_shaders": "baseline/shaders",
  "candidate_shaders": "candidate/shaders",
  "baseline_files": {"g_example.spv": "<sha256>", "shader-constants.txt": "<sha256>"},
  "candidate_files": {"g_example.spv": "<sha256>", "shader-constants.txt": "<sha256>"},
  "exact_outputs": [{
    "baseline": {"path": "baseline/output.raw", "sha256": "<sha256>"},
    "candidate": {"path": "candidate/output.raw", "sha256": "<sha256>"}
  }],
  "units": [{
    "shader": "g_example.spv",
    "baseline_spv_sha256": "<sha256>",
    "candidate_spv_sha256": "<sha256>",
    "baseline_isa": {"path": "baseline/driver.isa", "sha256": "<sha256>"},
    "candidate_isa": {"path": "candidate/driver.isa", "sha256": "<sha256>"},
    "baseline_elf": {"path": "baseline/driver.elf", "sha256": "<sha256>"},
    "candidate_elf": {"path": "candidate/driver.elf", "sha256": "<sha256>"},
    "capture_identity": {"path": "capture-example.json", "sha256": "<sha256>"},
    "assessment": {"mechanism_realized": true, "reason": "<specific driver instruction evidence>"}
  }]
}
```

All paths are relative to `gate.json`; absolute local paths also work. Populate
the complete file maps with `isa_gate.shader_tree`, not just the example entries.
Every changed SPIR-V needs exactly one unit, including temporal/runtime variants.
Each unit's capture report must contain `gpu`, `driver`, `baseline` and `candidate`.
Each arm is exactly an object containing `spv_sha256`, `isa_sha256`, `elf_sha256`,
recorded from that arm's actual probe/capture inputs and outputs. These bindings
are provenance assertions supplied by the probe/reviewer; hashes alone cannot
prove that a dump came from a particular pipeline.

```text
python -X utf8 windows/build/isa_gate.py build/windows/<study>/gate.json
```

The gate rejects missing/stale evidence, unequal independent outputs, changed
markers, incomplete shader coverage, unchanged ELF `.text`, rejected/unreviewed
mechanisms, and unbound capture identities. A different `.text` alone never passes:
an explicit mechanism assessment is required. This gate targets shader code and
scheduling changes. Host-only or data-layout hypotheses whose driver code is
unchanged require a separately designed mechanism check; do not invent an ISA
change or mark them passed here. Passing is permission to screen, not retention
or proof of equivalence for all inputs.

## Execute stages and record elapsed time

A private plan uses native argument arrays, never shell strings:

```json
{
  "schema_version": 1,
  "cwd": "<repository-or-private-study-directory>",
  "gate": "gate.json",
  "baseline_shaders": "baseline/shaders",
  "candidate_shaders": "candidate/shaders",
  "stages": [
    {"phase": "build", "command": ["python", "<build-script>", "<arguments>"]},
    {"phase": "correctness", "command": ["python", "<exact-output-check-script>"]},
    {"phase": "isa", "command": ["python", "<driver-ISA-check-script>"]},
    {"phase": "screen", "command": ["python", "<short-screen-script>"]},
    {"phase": "measure", "command": ["python", "<full-ABBA-script>"], "timeout_seconds": 1800},
    {"phase": "validate", "command": ["python", "<full-validation-script>"]}
  ]
}
```

Optional `prepare` comes first. Use existing correctness/probe/measurement tools
from the charged study. The plan does not replace their acceptance decisions.
Stop after extraction to review the mechanism, then continue:

```text
python -X utf8 windows/build/run_experiment.py build/windows/<study>/plan.json --through isa
python -X utf8 windows/build/run_experiment.py build/windows/<study>/plan.json --from-stage screen
```

Before every screen/measure/validate command, the runner rechecks the gate,
including its planned shader directories. Missing, stale or failed evidence
stops execution before the command. All commands run sequentially; failures and
timeouts stop subsequent stages. Logs and append-only `stage-timings.jsonl`
stay beside the plan. Start/end events have unique run/stage IDs, UTC timestamps,
status and elapsed wall-clock seconds. An unmatched start indicates interruption.
The build scripts also emit timing JSONL automatically; `--timings` overrides
its location. Host build manifests include the timing run ID and totals.

These are CPU/process wall-clock durations, not GPU timestamps. Workflow totals
include child stages, so do not add the total to its children. Commands containing
both preheat and measurement are timed together; split them into named commands
if separate costs matter. Agent reading/thinking and time between CLI invocations
are not measured automatically. Preserve all batches and the existing full-path
controls; this tooling does not authorize shorter acceptance runs or weaker
correctness coverage.

CPU regression checks:

```text
python -X utf8 windows/build/test_experiment_tools.py
```
