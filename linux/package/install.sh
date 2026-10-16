#!/usr/bin/env bash
# DLSSNR-AMD-Vulkan installer (Linux)
#
#   bash install.sh <folder with the game's exe> [route] [--new-model | --old-model] [--int4 | --no-int4]
#                   [--renodx | --no-renodx] [--dll <nvngx_dlssnr.dll or .zip>]
#
# Unreal Engine games: the folder is <project>/Binaries/Win64 (the *-Shipping.exe), not the top
# folder with the launcher exe.
#
# Routes:
#   optiscaler             the game has a DLSS, FSR or XeSS option: OptiScaler, with this project as its DLSS-NR backend (64-bit only)
#   reshade-feeder         D3D10/11/12 games without a usable upscaler: ReShade + VORT motion vectors
#   reshade-feeder-vulkan  Vulkan games, or D3D9 games (DXVK turns D3D9 into Vulkan under Proton)
#   reshade-feeder-dx9     as reshade-feeder-vulkan, plus the depth direction for old D3D9 games
#   remove                 uninstall
# Without a route a menu is shown.
#
# If the package has no model file, point --dll at NVIDIA's nvngx_dlssnr.dll (310.9.16 or 310.8.0, or a
# zip containing it); the model is extracted from it during installation and kept in this
# package's dlssnr-amd/, so later installs from this package (into other games too) need no --dll.
# The 310.9.16 DLL holds two models, the new and the old one; the 310.8.0 DLL only the old one.
#
# Every model the package has is installed; the choice below only sets [Network] Model in dlssnr-amd.ini
# (change it after installing to switch models):
# --new-model  use the new model (the default model of 310.9.16, faster). Not together with --int4.
# --old-model  use the old model (the model of 310.8.0).
# With neither, the installer asks first; Enter = the new model (with --int4: the old model; not asked when not run
# in a terminal: the new model). With the new model, int4 mixed is not asked. Later: [Network] Model in
# dlssnr-amd.ini, applied after a game restart.
#
# --int4     also install int4 mixed (old model only): part of the network runs in int4 and other lower precisions; faster, the
#            picture differs somewhat. On by default; Ctrl+F11 in game switches between int4 mixed and the default network.
# --no-int4  do not install int4 mixed: the default network only, no int4 mixed option and no switching.
#            To use int4 mixed later, run the installer again and choose it (it uninstalls and installs again).
# With neither, the installer asks once; Enter = do not install (not asked when not run in a terminal: not installed).
#
# --renodx     turn on the RenoDX style: the picture before and after NR is processed the way RenoDX's DLSS5 add-on
#              does it, and every NR setting moves to [RenoDX] (on the OptiScaler route in the NR Control menu, on the
#              ReShade routes on the Add-ons page).
# --no-renodx  leave it off. With neither, the installer asks once after int4 mixed; Enter = off (not asked when not run
#              in a terminal: off). Later: [RenoDX] Enabled in dlssnr-amd.ini, applied after a game restart.
# The old route name optiscaler-renodx still works and means optiscaler --renodx.
#
# Every installed file is listed in dlssnr-amd-install.txt in the game folder; remove deletes by it.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
# The package's models: bundled, or kept there by the first --dll install. Checked by SHA256.
# dlssnr.bin = the old model (310.8.0, also preset 1 of 310.9.16), dlssnr-small.bin = the new model (310.9.16's default)
pkg_model=$here/dlssnr-amd/dlssnr.bin
pkg_small=$here/dlssnr-amd/dlssnr-small.bin
model_sha256=2b41c888cf4155b8958c665ba64018ab0bd25c85fc71a2b6db86d0d04d1f7fbd
small_sha256=7e966e2639dbec6596aa0bac3b8761137d2c40ca8cb4aca30df7ca10043c6e33
sha_ok() { [[ -f "$1" && "$(sha256sum -- "$1" | cut -d' ' -f1)" == "$2" ]]; }
model_ok() { sha_ok "$1" "$model_sha256"; }
small_ok() { sha_ok "$1" "$small_sha256"; }

game="" route="" dll=""
newmodel=""
int4=""
renodx=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dll) dll=${2:?--dll needs a file path}; shift 2;;
        --new-model) newmodel=1; shift;;
        --old-model) newmodel=0; shift;;
        --int4) int4=1; shift;;
        --no-int4) int4=0; shift;;
        --renodx) renodx=1; shift;;
        --no-renodx) renodx=0; shift;;
        -h|--help) sed -n '2,44p' "$0"; exit 0;;
        *) if [[ -z "$game" ]]; then game=$1; elif [[ -z "$route" ]]; then route=$1;
           else echo "unexpected argument: $1" >&2; exit 1; fi; shift;;
    esac
done
[[ -n "$game" ]] || { sed -n '2,44p' "$0"; exit 1; }
[[ -d "$game" ]] || { echo "no such folder: $game" >&2; exit 1; }
game=$(cd -- "$game" && pwd)
manifest="$game/dlssnr-amd-install.txt"
if compgen -G "$game/*/Binaries/Win64/*.exe" > /dev/null; then
    echo "Note: this looks like the top folder of an Unreal Engine game; install into <project>/Binaries/Win64 (the *-Shipping.exe) instead." >&2
fi
bits=64; [[ -f "$here/reshade/dlssnr_amd.addon32" ]] && bits=32

exe_bits=$(python3 - "$game" <<'PY' 2>/dev/null || true
import sys, pathlib, struct
seen = set()
for exe in pathlib.Path(sys.argv[1]).glob("*.exe"):
    try:
        b = exe.read_bytes()
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        seen.add({0x14C: "32", 0x8664: "64"}.get(struct.unpack_from("<H", b, pe + 4)[0], "?"))
    except Exception:
        pass
print(" ".join(sorted(seen)))
PY
)

if [[ -z "$route" ]]; then
    echo
    echo "This is the $bits-bit package; the game folder's exe is ${exe_bits:-unknown}-bit. Choose a route:"
    echo "  1) optiscaler             the game has a DLSS, FSR or XeSS option (OptiScaler, 64-bit only)"
    echo "  2) reshade-feeder         D3D10/11/12 game without a usable upscaler"
    echo "  3) reshade-feeder-vulkan  Vulkan game, or D3D9 game"
    echo "  4) reshade-feeder-dx9     as 3, plus the depth setting for old D3D9 games"
    echo "  5) remove                 uninstall"
    read -r -p "1-5: " pick
    case "$pick" in
        1) route=optiscaler;; 2) route=reshade-feeder;; 3) route=reshade-feeder-vulkan;;
        4) route=reshade-feeder-dx9;; 5) route=remove;;
        *) echo "invalid choice" >&2; exit 1;;
    esac
fi

record() { printf '%s\n' "$1" >> "$manifest"; }
put_file() { cp -- "$1" "$game/$2"; record "$2"; }
put_tree() { mkdir -p -- "$game/$2"; cp -r -- "$1"/. "$game/$2/"; record "$2/"; }

remove_installed() {
    [[ -f "$manifest" ]] || { echo "$manifest not found, nothing to uninstall."; return; }
    while IFS= read -r entry; do
        [[ -n "$entry" ]] || continue
        case "$entry" in /*|..*|*/..*) echo "skipping suspicious entry: $entry" >&2; continue;; esac
        rm -rf -- "$game/$entry"
    done < "$manifest"
    rm -f -- "$manifest"
    echo "Uninstalled from $game."
}

case "$route" in
    remove) remove_installed; exit 0;;
    optiscaler) kind=optiscaler;;
    optiscaler-renodx) kind=optiscaler; route=optiscaler; [[ -z "$renodx" ]] && renodx=1;;
    reshade-feeder) kind=reshade;;
    reshade-feeder-vulkan) kind=vulkan;;
    reshade-feeder-dx9) kind=dx9;;
    reshade|vulkan|dx9)
        echo "unknown route: $route (this route is now called reshade-feeder$([[ "$route" == reshade ]] || echo "-$route"))" >&2; exit 1;;
    *) echo "unknown route: $route" >&2; exit 1;;
esac
if [[ "$kind" == optiscaler && ! -d "$here/optiscaler" ]]; then
    echo "The OptiScaler route is only in the 64-bit package." >&2; exit 1
fi
if [[ -z "$newmodel" ]]; then
    if [[ "$int4" == 1 ]]; then
        newmodel=0   # int4 mixed exists for the old model only
    else
        newmodel=1
        if [[ -t 0 ]]; then
            echo
            echo "Model: NVIDIA 310.9.16 brings a new model, faster than the old one (310.8.0)."
            echo "  Y: the new model (default). int4 mixed exists for the old model only and is not installed with the new one."
            echo "  n: the old model; the installer then asks about int4 mixed."
            echo "  Later: [Network] Model in dlssnr-amd.ini (small = new model, original = old model), applied after a game restart."
            read -r -p "Use the new model? [Y/n] " pick || pick=""
            [[ "$pick" == n || "$pick" == N ]] && newmodel=0
        fi
    fi
fi
if [[ $newmodel == 1 && "$int4" == 1 ]]; then
    echo "int4 mixed exists for the old model only; --new-model and --int4 cannot be used together." >&2; exit 1
fi
[[ $newmodel == 1 ]] && int4=0
if [[ -z "$int4" ]]; then
    int4=0
    if [[ -d "$here/int4" && -t 0 ]]; then
        echo
        echo "int4 mixed (optional): part of the network runs in int4 and other lower precisions; faster, the picture differs somewhat."
        echo "  y: install it. On by default; Ctrl+F11 in game switches between int4 mixed and the default network."
        echo "  N: do not install it. The default network only, no int4 mixed option and no switching; to use it later, run the installer again and choose y."
        read -r -p "Install int4 mixed? [y/N] " pick || pick=""
        [[ "$pick" == y || "$pick" == Y ]] && int4=1
    fi
fi
if [[ -z "$renodx" ]]; then
    renodx=0
    if [[ -t 0 ]]; then
        echo
        echo "RenoDX style (optional): the picture before and after NR is processed the way RenoDX's DLSS5 add-on does it (the NR network itself is the same)."
        if [[ "$kind" == optiscaler ]]; then
            echo "  y: turn it on. Every NR setting is in NR Control (End in game); the OptiScaler menu only handles upscaling and frame generation."
        else
            echo "  y: turn it on. Every NR setting is on ReShade's Add-ons page ([RenoDX] in dlssnr-amd.ini)."
        fi
        echo "  N: leave it off, as before. To turn it on later, set [RenoDX] Enabled = 1 in dlssnr-amd.ini and restart the game."
        read -r -p "Turn on the RenoDX style? [y/N] " pick || pick=""
        [[ "$pick" == y || "$pick" == Y ]] && renodx=1
    fi
fi
if [[ $int4 == 1 && ! -x "$here/int4/dlssnr-int4-weights" ]]; then
    echo "This package has no int4 mixed." >&2; exit 1
fi
model_src="" small_src=""
# An extracted model is kept in the package; if that fails (package folder not writable?) it is installed directly
# this time and the next install needs --dll again
keep() {   # <extracted file> <place in the package>
    if cp -- "$1" "$2.part" && mv -f -- "$2.part" "$2"; then
        chmod 644 -- "$2"; echo "The model is kept in $2; later installs from this package need no --dll."; return 0
    fi
    rm -f -- "$2.part"; echo "Note: the model could not be kept in $2 (package folder not writable?); the next install needs --dll again." >&2; return 1
}
if [[ -n "$dll" ]] && ! { model_ok "$pkg_model" && small_ok "$pkg_small"; }; then
    model_tmp=$(mktemp -d)
    trap 'rm -rf -- "$model_tmp"; rm -f -- "$pkg_model.part" "$pkg_small.part"' EXIT
    echo "Extracting the model from $dll ..."
    bash "$here/model-tools/extract_model.sh" "$dll" "$model_tmp"
    model_ok "$model_tmp/dlssnr.bin" || { echo "The extracted model has the wrong checksum; nothing was installed." >&2; exit 1; }
    keep "$model_tmp/dlssnr.bin" "$pkg_model" || model_src=$model_tmp/dlssnr.bin
    if [[ -f "$model_tmp/dlssnr-small.bin" ]]; then
        small_ok "$model_tmp/dlssnr-small.bin" || { echo "The extracted new model has the wrong checksum; nothing was installed." >&2; exit 1; }
        keep "$model_tmp/dlssnr-small.bin" "$pkg_small" || small_src=$model_tmp/dlssnr-small.bin
    fi
elif model_ok "$pkg_model"; then
    :   # the package has the model; it is installed with dlssnr-amd/
elif [[ -f "$pkg_model" ]]; then
    echo "$pkg_model is damaged or from another version; extract it again with --dll." >&2; exit 1
else
    echo "The package has no model file: point --dll at nvngx_dlssnr.dll (310.9.16 - both models - or 310.8.0) or its zip." >&2
    echo "Only the first time: the extracted model is kept in this package's dlssnr-amd/ and used by later installs." >&2
    exit 1
fi
if [[ -f "$pkg_small" ]] && ! small_ok "$pkg_small"; then
    echo "$pkg_small is damaged or from another version; delete it or extract it again with --dll." >&2; exit 1
fi
if [[ -z "$small_src" ]] && ! small_ok "$pkg_small"; then
    echo "Note: the package has no new model (dlssnr-small.bin); only the old model was installed.$([[ $newmodel == 1 ]] && echo " The game runs the old model for now (the log says why).")"
    echo "To add the new model, install again with --dll pointing at the 310.9.16 nvngx_dlssnr.dll or its zip."
fi
if [[ $int4 == 1 ]]; then
    # The int4 weights are not in the package: made here from this model and the package's recovery tables, installed
    # only when their SHA-256 matches
    echo "Making the int4 mixed weights ..."
    int4_tmp=$(mktemp -d)
    trap 'rm -rf -- "$int4_tmp"; [[ -n "${model_tmp:-}" ]] && rm -rf -- "$model_tmp"; rm -f -- "$pkg_model.part" "$pkg_small.part"' EXIT
    "$here/int4/dlssnr-int4-weights" "${model_src:-$pkg_model}" "$here/int4/dlssnr-amd/int4" "$int4_tmp/dlssnr-int4.bin" \
        > "$int4_tmp/log.txt" 2>&1 || { cat "$int4_tmp/log.txt" >&2; echo "Making the int4 mixed weights failed; nothing was installed." >&2; exit 1; }
    if [[ "$(sha256sum -- "$int4_tmp/dlssnr-int4.bin" | cut -d' ' -f1)" != "$(cut -d' ' -f1 "$here/int4/dlssnr-int4.bin.sha256")" ]]; then
        echo "The int4 mixed weights have the wrong checksum (model or package incomplete?); nothing was installed." >&2; exit 1
    fi
fi
if [[ -f "$manifest" ]]; then
    echo "Found a previous installation, removing it first."
    remove_installed
fi
if [[ -n "$exe_bits" && "$exe_bits" != "?" && "$exe_bits" != *"$bits"* ]]; then
    echo "Note: this is the $bits-bit package but the game exe is $exe_bits-bit; DLLs of the other bitness will not load." >&2
fi

: > "$manifest"; record "dlssnr-amd-install.txt"
put_tree "$here/dlssnr-amd" dlssnr-amd
[[ -n "$model_src" ]] && cp -- "$model_src" "$game/dlssnr-amd/dlssnr.bin"
[[ -n "$small_src" ]] && cp -- "$small_src" "$game/dlssnr-amd/dlssnr-small.bin"

case "$kind" in
    optiscaler)
        tmp=$(mktemp -d)
        python3 "$here/optiscaler/extract_release.py" "$here"/optiscaler/OptiScaler*.zip "$tmp" > /dev/null
        rm -f -- "$tmp/!! EXTRACT ALL FILES TO GAME FOLDER !!" "$tmp/setup_windows.bat" "$tmp/setup_linux.sh" \
                 "$tmp/nvngx.dll_dlssnr.dll"
        mv -- "$tmp/OptiScaler.dll" "$tmp/dxgi.dll"
        for f in "$tmp"/*; do
            name=$(basename -- "$f")
            if [[ -d "$f" ]]; then put_tree "$f" "$name"; else put_file "$f" "$name"; fi
        done
        rm -rf -- "$tmp"
        for f in nvngx.dll_dlssnr.dll nvngx_dlssnr.dll dlssnr_core.dll; do put_file "$here/optiscaler/$f" "$f"; done
        # NR Control (the NR menu opened with End, a Linux Vulkan layer): recorded and removed with dlssnr-amd/
        mkdir -p -- "$game/dlssnr-amd/control"
        cp -r -- "$here/optiscaler/control/layer" "$game/dlssnr-amd/control/"
        python3 "$here/optiscaler/patch_ini.py" "$game"
        record OptiScaler.log; record dlssnr-amd.log; record dlssnr-amd.ini
        overrides="dxgi=n,b"
        ;;
    reshade)
        for f in "$here"/reshade/*; do
            name=$(basename -- "$f")
            if [[ -d "$f" ]]; then put_tree "$f" "$name"; else put_file "$f" "$name"; fi
        done
        overrides="dxgi=n,b"
        ;;
    vulkan|dx9)
        for f in "$here"/reshade/* "$here"/vulkan/*; do
            name=$(basename -- "$f")
            [[ "$name" == dxgi.dll || "$name" == ReShadePreset-d3d9.ini ]] && continue
            if [[ -d "$f" ]]; then put_tree "$f" "$name"; else put_file "$f" "$name"; fi
        done
        [[ "$kind" == dx9 ]] && cp -- "$here/vulkan/ReShadePreset-d3d9.ini" "$game/ReShadePreset.ini"
        overrides="winevulkan=n,b;vulkan-1=n,b"
        ;;
esac
[[ "$kind" != optiscaler ]] && { record dlssnr-amd.ini; record dlssnr-amd.log; record ReShade.log; }
if [[ $int4 == 1 ]]; then
    # The int4 mixed programs replace the route's files of the same name just installed; dlssnr-amd/int4: network,
    # quantisers, settings, Vulkan layer (the recovery tables *.fix.bin, *.mu.bin stay in the package); the weights
    # made above go into dlssnr-amd/
    d=reshade; [[ "$kind" == optiscaler ]] && d=optiscaler
    for f in "$here/int4/$d"/*; do
        name=$(basename -- "$f")
        [[ -e "$game/$name" ]] && cp -- "$f" "$game/$name"
    done
    mkdir -p -- "$game/dlssnr-amd/int4"
    (cd -- "$here/int4/dlssnr-amd/int4" && find . -type f ! -name '*.fix.bin' ! -name '*.mu.bin' -print0 |
        while IFS= read -r -d '' f; do mkdir -p -- "$game/dlssnr-amd/int4/$(dirname -- "$f")"; cp -- "$f" "$game/dlssnr-amd/int4/$f"; done)
    mv -- "$int4_tmp/dlssnr-int4.bin" "$game/dlssnr-amd/dlssnr-int4.bin"
fi
# dlssnr-amd.ini is put in place at install, with every setting and its description (a reinstall puts a fresh default)
ini_dir=$here/ini; [[ $int4 == 1 ]] && ini_dir=$here/int4/ini
if [[ "$kind" == optiscaler && $renodx == 1 ]]; then cp -- "$ini_dir/dlssnr-amd-optiscaler-renodx.ini" "$game/dlssnr-amd.ini"
elif [[ "$kind" == optiscaler ]]; then cp -- "$ini_dir/dlssnr-amd-optiscaler.ini" "$game/dlssnr-amd.ini"
else
    cp -- "$ini_dir/dlssnr-amd-addon.ini" "$game/dlssnr-amd.ini"
    # the RenoDX style: Enabled = 1 in the [RenoDX] section
    [[ $renodx == 1 ]] && sed -i '/^\[RenoDX\]/,/^\[/ s/^Enabled = 0$/Enabled = 1/' "$game/dlssnr-amd.ini"
fi
# the old model: [Network] Model = original (int4 mixed's ini has it already)
[[ $newmodel == 0 ]] && sed -i '/^\[Network\]/,/^\[/ s/^Model = small$/Model = original/' "$game/dlssnr-amd.ini"

echo
echo "Installed the $route route into: $game$([[ $renodx == 1 ]] && echo " (RenoDX style)")"
echo "Model: $([[ $newmodel == 1 ]] && echo "the new model (310.9.16)" || echo "the old model (310.8.0)"); switch it with [Network] Model in dlssnr-amd.ini, applied after a game restart."
# Vulkan layers in the launch options: int4 mixed's (with int4 mixed), NR Control's (the OptiScaler route)
layer_paths=() layer_names=()
[[ $int4 == 1 ]] && { layer_paths+=("$game/dlssnr-amd/int4/layer"); layer_names+=(VK_LAYER_DLSSNR_int4); }
[[ "$kind" == optiscaler ]] && { layer_paths+=("$game/dlssnr-amd/control/layer"); layer_names+=(VK_LAYER_DLSSNR_control); }
if [[ ${#layer_paths[@]} -gt 0 ]]; then
    echo "Steam launch options:  VK_ADD_LAYER_PATH=\"$(IFS=:; echo "${layer_paths[*]}")\" VK_INSTANCE_LAYERS=$(IFS=:; echo "${layer_names[*]}") WINEDLLOVERRIDES=\"$overrides\" %command%"
    echo "VK_ADD_LAYER_PATH and VK_INSTANCE_LAYERS in the launch options are needed by$([[ $int4 == 1 ]] && echo " int4 mixed")$([[ $int4 == 1 && "$kind" == optiscaler ]] && echo " and")$([[ "$kind" == optiscaler ]] && echo " the NR Control menu"); copy the whole line."
else
    echo "Steam launch options:  WINEDLLOVERRIDES=\"$overrides\" %command%"
fi
[[ $int4 == 1 ]] &&
    echo "int4 mixed is on by default; Ctrl+F11 in game switches to the default network and back. Settings: [Int4Mixed] in dlssnr-amd.ini, applied when saved."
if [[ "$kind" == optiscaler ]]; then
    if [[ $renodx == 1 ]]; then
        echo "In the game, turn on DLSS, FSR or XeSS in its settings; Insert opens the OptiScaler menu (with the RenoDX style on, only for upscaling and frame generation)."
    else
        echo "In the game, turn on DLSS, FSR or XeSS in its settings; Insert opens the OptiScaler menu, DLSS Neural Rendering has its own page."
    fi
    echo "If the NR page keeps showing 'Waiting for the upscaler to run', set [Spoofing] Dxgi=false in OptiScaler.ini and try again."
    echo "If colours turn green or grainy after NR (007 First Light, for example), try [Preprocess] in dlssnr-amd.ini, or press Ctrl+F10 in the game."
else
    echo "In the game, Home opens ReShade; the settings are on the Add-ons page, or edit dlssnr-amd.ini directly."
fi
[[ "$kind" == optiscaler ]] &&
    echo "End opens NR Control (this project's NR menu: int4 mixed, Preprocess$([[ $renodx == 1 ]] && echo ", and every NR setting of the RenoDX style"))."
[[ "$kind" == optiscaler && $renodx == 1 ]] &&
    echo "The RenoDX style is on ([RenoDX] Enabled = 1 in dlssnr-amd.ini). With it on, every NR setting is in NR Control ([RenoDX]); the OptiScaler menu only handles upscaling and frame generation, and its NR page has no effect. Turning the RenoDX style off (Enabled = 0, restart the game) brings back the previous behaviour, with the NR settings in the OptiScaler menu. F5 saves a screenshot (the same frame without and with NR) to NR Screenshots in the game folder."
[[ "$kind" != optiscaler && $renodx == 1 ]] &&
    echo "The RenoDX style is on ([RenoDX] Enabled = 1 in dlssnr-amd.ini). With it on, every NR setting is in the RenoDX part of the Add-ons page ([RenoDX]); the NR settings in [DlssNr] have no effect. Turning it off (Enabled = 0, restart the game) brings back the previous behaviour. F5 saves a screenshot (the same frame without and with NR) to NR Screenshots in the game folder."
echo "Logs: dlssnr-amd.log$([[ "$kind" == optiscaler ]] && echo ", OptiScaler.log" || echo ", ReShade.log")"
echo "Uninstall:  bash install.sh \"$game\" remove"
