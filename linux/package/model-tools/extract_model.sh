#!/usr/bin/env bash
# Extract verified HT (310.8.0) or CC_SMALL_FP8 (310.9.16), without loading the DLL.
#
#   bash extract_model.sh <nvngx_dlssnr.dll or .zip> <output .bin> [--model auto|ht|small]
#
# Only reads the weight data in the DLL; never loads or runs it. Every entry is checked
# against model-files.sha256, and the file is written only if it matches the tested model
# byte for byte.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }
exec python3 "$here/extract_model.py" "$@"
