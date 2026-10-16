#!/usr/bin/env python3
"""Extract the verified HT or CC_SMALL_FP8 model without loading NVIDIA's DLL.

310.8.0 selects HT; 310.9.16 selects Small. Use --model ht to extract HT from
either version. Input may be a DLL or a ZIP containing one nvngx_dlssnr.dll.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from inspect_nr import load_input

DLL_HT = "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e"
DLL_SMALL = "2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--model", choices=("auto", "ht", "small"), default="auto")
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    digest = hashlib.sha256(load_input(args.input)).hexdigest()
    if digest not in (DLL_HT, DLL_SMALL):
        sys.exit(f"Unsupported nvngx_dlssnr.dll (SHA256 {digest}). Use 310.8.0 or 310.9.16.")
    small = args.model == "small" or (args.model == "auto" and digest == DLL_SMALL)
    if small and digest != DLL_SMALL:
        sys.exit("CC_SMALL_FP8 requires nvngx_dlssnr.dll 310.9.16.")
    suffix = "-small" if small else ""
    with tempfile.TemporaryDirectory(prefix="dlssnr-model-") as temporary:
        work = Path(temporary)
        (work / "graph").mkdir()
        shutil.copyfile(here / f"descriptor{suffix}.json", work / "graph/descriptor.json")

        def run(script, *arguments):
            subprocess.run([sys.executable, str(here / script), *map(str, arguments)],
                           check=True, stdout=subprocess.DEVNULL)

        run("inspect_nr.py", args.input.resolve(), "--output", work / "inventory", "--extract",
            "--resource", "WEIGHTS_CC_SMALL_FP8" if small else "WEIGHTS_HT")
        run("unpack_swin_family.py", "--artifacts", work, "--output", work / "unpacked", "--max-c", 256)
        run("unpack_splitswin.py", "--artifacts", work, "--output", work / "unpacked-splitswin")
        run("unpack_vit.py", "--artifacts", work, "--output", work / "unpacked-vit")
        run("unpack_preblock.py", work / "inventory/weights/block0.layer0.layer.bin", work / "unpacked-preblock")
        post = 30 if small else 70
        run("unpack_postblock.py", work / f"inventory/weights/block{post}.layer0.layer.bin", work / "unpacked-postblock")
        # Verify before replacing the destination, including when it already exists.
        packed = work / "dlssnr.bin"
        run("pack_model.py", "--root", work, "--list", here / f"model-files{suffix}.txt",
            "--out", packed, "--verify", here / f"model-files{suffix}.sha256")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        staged = args.output.with_name(args.output.name + ".tmp")
        try:
            shutil.copyfile(packed, staged)
            staged.replace(args.output)
        finally:
            staged.unlink(missing_ok=True)
        entries = 256 if small else 599
        print(f"{args.output}: {entries} entries, {args.output.stat().st_size / 2**20:.1f} MiB "
              f"({'CC_SMALL_FP8' if small else 'HT'})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
