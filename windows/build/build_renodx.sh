#!/usr/bin/env bash
# The RenoDX-style option's shaders (windows/shaders/renodx, RenoDX's own; its README lists our speed changes) ->
# SPIR-V, the way RenoDX builds its Vulkan path: Slang with RENODX_VULKAN defined (its vk_binding.hlsli: t0..t3 -> bindings 0..3,
# u0/u1 -> 4/5, the cbuffer as push constants). legacy_reduce/apply have no Vulkan build upstream and declare
# their second UAV without VK_BINDING; the same numbering comes from -fvk-u-shift for those two only.
#   build_renodx.sh <out dir>        NR_SLANGC overrides the compiler (default toolchain/slang-2026.19)
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=$(realpath -m -- "${1:?usage: build_renodx.sh <out dir>}")
slangc=${NR_SLANGC:-toolchain/slang-2026.19/bin/slangc}
src=windows/shaders/renodx
tmp=$(mktemp -d "${TMPDIR:-/tmp}/renodx.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p -- "$out"
for k in v6_linearize v6_autoscale v6_exposure_scale v6_encode v6_resolve v6_pedestal_reduce v6_pedestal_reduce_unshaped \
         v6_commit v6_commit_exposure v6_resolve_commit \
         legacy_encode legacy_decode legacy_reduce legacy_reduce_unshaped legacy_apply look_compose look_upsample look_band; do
    printf '#define RENODX_VULKAN 1\n#include "%s.cs_5_1.hlsl"\n' "$k" > "$tmp/vk_$k.comp.slang"
    shift_args=()
    case $k in legacy_reduce*|legacy_apply) shift_args=(-fvk-t-shift 0 all -fvk-u-shift 4 all);; esac
    "$slangc" "$tmp/vk_$k.comp.slang" -I "$src" -target spirv -profile glsl_460 -entry main -stage compute -O2 \
        "${shift_args[@]}" -o "$out/renodx_$k.spv" 2> "$tmp/log" || { cat "$tmp/log" >&2; exit 1; }
done
# Preprocess on the RenoDX path (windows/shaders/renodx-prep, ours, glslang like every other pass): its math is a
# copy of runtime_prep.comp's, which must not have drifted.
prep_src=windows/shaders/passes/runtime_prep.comp
if ! diff <(sed -n '/^\/\/ ---- the curves, scene-linear/,/^void main() {/p' "$prep_src" | sed '$d') \
          <(sed -n '/^\/\/ >>> runtime_prep.comp$/,/^\/\/ <<< runtime_prep.comp$/p' windows/shaders/renodx-prep/prep_math.glsl \
            | sed '1d;$d') > /dev/null; then
    echo "windows/shaders/renodx-prep/prep_math.glsl is not runtime_prep.comp's math any more; copy it again" >&2; exit 1
fi
glslang=${NR_GLSLANG:-toolchain/glslang/bin/glslang}
for k in renodx_prep renodx_prep_encode; do
    "$glslang" -V --target-env vulkan1.3 -Iwindows/shaders/renodx-prep windows/shaders/renodx-prep/$k.comp -o "$out/$k.spv" \
        > "$tmp/log" || { cat "$tmp/log" >&2; exit 1; }
done
echo "$out: $(ls "$out"/renodx_*.spv | wc -l) RenoDX shaders"
