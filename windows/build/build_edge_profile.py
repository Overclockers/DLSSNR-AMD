"""Build a conservative C256 edge profile against an independently saved host baseline.

Only shader assets are copied; no model, DLL or pipeline cache is packaged.
Use the host's vendored source root to preserve its compiler/driver workarounds.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--glslang', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    source, baseline, compiler, out = (p.resolve() for p in
        (args.source_root, args.baseline, args.glslang, args.out))
    if out.exists() or out.is_relative_to(baseline) or baseline.is_relative_to(out):
        parser.error('output must be new and separate from the saved baseline')
    if not compiler.is_file() or not (baseline / 'shader-constants.txt').is_file():
        parser.error('missing compiler or baseline shader manifest')
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('nr_shader_builder', source / 'windows/build/build_network.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    if not hasattr(builder, 'GLSLANG'):
        parser.error('source builder needs explicit GLSLANG support')
    builder.GLSLANG = compiler
    recipe_path = source / 'windows/shaders/rdna4/pipelines.json'
    recipe = json.loads(recipe_path.read_text())
    names = ('fswinpds256', 'fswinpup256')
    out.parent.mkdir(parents=True, exist_ok=True)
    proof = {'profile': 'c256-edge', 'compiler_sha256': digest(compiler),
             'recipe_sha256': digest(recipe_path), 'pipelines': {},
             'validation': 'Baseline rebuild verified; game acceptance still required.'}
    # Verify unchanged compilations first: unrelated source changes must not be
    # silently bundled into a supposed one-define performance experiment.
    # Inherit workspace ACLs. Python 3.13 TemporaryDirectory applies a private
    # Windows ACL that can exclude a restricted-token build process itself.
    scratch = out.parent / ('edge-profile-' + uuid.uuid4().hex)
    scratch.mkdir()
    try:
        for name in names:
            entry = recipe['pipelines'][name]
            shader = source / 'windows/shaders/rdna4' / entry['source']
            target = scratch / ('g_' + name + '.spv')
            unroll = entry['source'] in getattr(builder, 'UNROLLED', {'fswin_t.comp'})
            control = [d for d in entry['defines'] if not d.startswith('NR_EDGE_BODIES=')]
            control.append('NR_EDGE_BODIES=0')
            builder.glslang('rdna4', shader, control, target, unroll=unroll, network=True)
            reference = baseline / target.name
            if target.read_bytes() != reference.read_bytes():
                raise SystemExit(f'{name}: source/compiler does not reproduce baseline; refusing mixed profile')
            defines = [d for d in entry['defines'] if not d.startswith('NR_EDGE_BODIES=')]
            defines.append('NR_EDGE_BODIES=1')
            builder.glslang('rdna4', shader, defines, target, unroll=unroll, network=True)
            proof['pipelines'][name] = {'baseline_sha256': digest(reference), 'candidate_sha256': digest(target)}
        out.mkdir()
        # Deliberately exclude arbitrary files from baseline, especially private models.
        for item in baseline.rglob('*'):
            if item.is_file() and item.suffix in ('.spv', '.txt'):
                destination = out / item.relative_to(baseline)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, destination)
        for name in names:
            shutil.copy2(scratch / ('g_' + name + '.spv'), out / ('g_' + name + '.spv'))
    finally:
        # Only remove this invocation's flat compiler work directory.
        for item in scratch.iterdir():
            if item.is_file():
                item.unlink()
        scratch.rmdir()
    proof['assets'] = {str(p.relative_to(out)).replace('\\', '/'): digest(p)
                       for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'profile.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(out)


if __name__ == '__main__':
    main()
