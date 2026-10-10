"""Cache a source checkout's own shader builder without changing its recipe.

This adapter preserves the host's separate unrolling and compiler workarounds.
All output must stay under this repository's ignored build directory.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

from experiment_support import ShaderCache, StageTimings, digest


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--cache-dir', type=Path, default=ROOT / 'build/windows/shader-cache')
    parser.add_argument('--timings', type=Path)
    parser.add_argument('--check', type=Path, help='compare every output, including runtime/temporal')
    parser.add_argument('--clean', action='store_true', help='bypass cache for an independent full rebuild')
    args, forwarded = parser.parse_known_args()
    source, out = args.source_root.resolve(), args.out.resolve()
    if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        parser.error('--out must be a subdirectory of this repository build directory')
    if source == out or source.is_relative_to(out) or out.is_relative_to(source / 'windows'):
        parser.error('--out must not overwrite the source snapshot')
    cache = ShaderCache(args.cache_dir)
    if cache.directory.resolve().is_relative_to(out):
        parser.error('cache must be outside --out')
    builder_path = source / 'windows/build/build_network.py'
    spec = importlib.util.spec_from_file_location('nr_source_builder', builder_path)
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    original = builder.glslang
    timings = StageTimings(args.timings or out.parent / (out.name + '.timings.jsonl'))
    tool = None

    def cached(arch, src, defines, output, unroll=False, network=False):
        nonlocal tool
        if tool is None:
            tool = {'compiler': digest(builder.GLSLANG), 'python': sys.version,
                    'scripts': {str(p): digest(p) for p in
                                (builder_path, source / 'windows/build/quad_quant_glsl.py',
                                 source / 'windows/build/unroll_glsl.py', Path(__file__),
                                 Path(__file__).with_name('experiment_support.py'))}}
        command = [str(builder.GLSLANG), '-E', '-I' + str(source / 'windows/shaders' / arch / 'include')]
        command += ['-D' + d for d in defines] + [str(src)]

        def preprocess():
            with timings.stage('preprocess', source=str(src)):
                return subprocess.run(command, capture_output=True, text=True, check=True).stdout

        text = preprocess()
        identity = dict(tool=tool, source=str(src.resolve()), preprocessed=text, defines=defines,
                        arch=arch, unroll=unroll, network=network, quad=builder.QUAD,
                        q32_direct=os.environ.get('NR_Q32_DIRECT', '1'))
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode('utf-8')).hexdigest()
        with timings.stage('cache', output=str(output)):
            if cache.restore(key, output):
                return
        with timings.stage('transform_compile', output=str(output)):
            original(arch, src, defines, output, unroll=unroll, network=network)
        if preprocess() != text:
            output.unlink(missing_ok=True)
            raise RuntimeError('shader inputs changed during compilation; discard this build')
        cache.store(key, output)

    if not args.clean:
        builder.glslang = cached
    previous = sys.argv
    try:
        sys.argv = [str(builder_path), *forwarded, '--out', str(out)]
        with timings.stage('build'):
            builder.main()
        if args.check:
            saved = args.check.resolve()
            actual = {p.relative_to(out) for p in out.rglob('*') if p.is_file()}
            expected = {p.relative_to(saved) for p in saved.rglob('*') if p.is_file()}
            changed = [str(p) for p in sorted(actual | expected)
                       if p not in actual or p not in expected or digest(out / p) != digest(saved / p)]
            if changed:
                raise ValueError('full shader comparison failed: ' + ', '.join(changed))
            print('full shader comparison: identical')
    finally:
        sys.argv = previous
        print('wall-clock seconds:', json.dumps(timings.totals, sort_keys=True))
        if not args.clean:
            print(f'shader cache: {cache.hits} hits, {cache.misses} misses')


if __name__ == '__main__':
    main()
