"""Build the standalone Windows network benchmark in an x64 MSVC developer shell."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--headers', required=True, type=Path, help='Vulkan include directory')
    parser.add_argument('--vulkan-lib', required=True, type=Path)
    parser.add_argument('--out', type=Path, default=Path('build/windows/benchmark'))
    parser.add_argument('--pipeline-stats', action='store_true',
                        help='capture driver register/LDS statistics (diagnostic build)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    if not out.is_relative_to(root / 'build'):
        parser.error('output must be under the repository build directory')
    headers, library = args.headers.resolve(), args.vulkan_lib.resolve()
    if not (headers / 'vulkan/vulkan.h').is_file() or not library.is_file():
        parser.error('Vulkan headers or import library not found')
    out.mkdir(parents=True, exist_ok=True)
    defines = re.findall(r'-D([A-Z0-9_]+=[A-Za-z0-9_]+)',
                         (root / 'windows/build/arch/rdna4.sh').read_text())
    if args.pipeline_stats:
        defines.append('NR_PIPELINE_STATS=1')
    subprocess.run(['cl', '/nologo', '/std:c++17', '/O2', '/EHsc', '/MT', '/utf-8',
                    '/bigobj', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/DNDEBUG',
                    *['/D' + d for d in defines], '/I' + str(headers),
                    str(root / 'windows/build/benchmark.cpp'),
                    str(root / 'windows/src/core/nr_native_plan.cpp'),
                    '/Fe:' + str(out / 'nr_benchmark.exe'), '/link', str(library)],
                   cwd=out, check=True)


if __name__ == '__main__':
    main()
