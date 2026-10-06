#!/usr/bin/env python3
"""Build an isolated offline D3D12/Vulkan host study from a supplied host checkout.

Run in an x64 MSVC developer prompt. The host checkout is read only; all output
stays below this repository's ignored build directory. No model is copied.
Diagnostic builds have extra timestamp markers and an original-input A/B control.
Neither build mode creates an installable package or a release manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/windows/host-profile')
    parser.add_argument('--production', action='store_true', help='compile without diagnostic hooks')
    args = parser.parse_args()
    host, out = args.host.resolve(), args.out.resolve()
    if not out.is_relative_to((ROOT / 'build').resolve()) or out == ROOT / 'build':
        parser.error('--out must be a subdirectory of this repository build directory')
    core = host / 'third_party/mochizuki/windows/src/core'
    product = host / 'OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler'
    adapter = product / 'dlssnr/backend/mochizuki_runtime'
    headers = host / 'exports/mochizuki-toolchain/Vulkan-Headers-e3b1eec08173d6b825cd3ac88c885a63b621504a/include'
    glslang = host / 'exports/mochizuki-toolchain/glslang/bin/glslang.exe'
    library = product / 'library/vulkan/vulkan-1.lib'
    for path in (headers / 'vulkan/vulkan.h', glslang, library, core / 'nr_profile.hpp', adapter / 'MochizukiProfile.h'):
        if not path.is_file():
            parser.error(f'missing matched host source/toolchain: {path}')
    out.mkdir(parents=True, exist_ok=True)
    # The host ABI fixture owns the D3D12 setup and frame helpers. Reuse only its
    # helper prefix; fail closed if its entrypoint changes instead of guessing.
    fixture_path = host / 'tests/mochizuki/runtime.cpp'
    fixture = fixture_path.read_text(encoding='utf-8')
    entry = 'int wmain(int argc,wchar_t**argv) try {'
    if fixture.count(entry) != 1:
        parser.error('host ABI fixture entrypoint changed; review the helper extraction')
    (out / 'host_fixture.hpp').write_text(fixture.split(entry)[0], encoding='utf-8')
    recipe = host / 'third_party/mochizuki/windows/build/arch/rdna4.sh'
    defines = re.findall(r'-D([A-Z0-9_]+=[A-Za-z0-9_]+)', recipe.read_text(encoding='utf-8'))
    flags = ['/nologo', '/std:c++20', '/O2', '/EHsc', '/MT', '/utf-8', '/bigobj', '/W3',
             '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/DNDEBUG', '/wd4244', '/wd4267', '/wd4305', '/wd4018']
    if not args.production:
        flags.append('/DNR_DIAGNOSTICS=1')
    includes = ['/I' + str(p) for p in (headers, core, adapter / 'compat', product / 'dlssnr/backend/lmxxf_runtime')]
    redirects = ['vkCreateComputePipelines', 'vkCreateShaderModule', 'vkCreateDescriptorSetLayout',
                 'vkCreatePipelineLayout', 'vkCreatePipelineCache', 'vkDestroyPipelineCache']
    sources = [(core / 'nr_runtime.cpp', ['/D' + d for d in defines] + ['/D' + f + '=mzi_' + f for f in redirects]),
               (core / 'nr_native_plan.cpp', []), (adapter / 'mz_interpose.cpp', []),
               (adapter / 'MochizukiNrRuntime.cpp', ['/DLMXXF_NR_RUNTIME_EXPORTS'])]
    watched = []
    for folder in (host / 'third_party/mochizuki/windows', adapter):
        watched.extend(p for p in folder.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    watched.extend([fixture_path, product / 'dlssnr/backend/lmxxf_runtime/LmxxfNrApi.h',
                    product / 'dlssnr/NrPerformance.h', library, glslang])
    identities = {str(p.relative_to(host)): digest(p) for p in sorted(watched)}
    commands = []

    def run(command):
        commands.append([str(s) for s in command])
        subprocess.run(command, check=True)

    objects = []
    for source, extra in sources:
        obj = out / (source.stem + '.obj')
        run(['cl', *flags, *includes, *extra, '/c', str(source), '/Fo' + str(obj)])
        objects.append(str(obj))
    run(['link', '/nologo', '/DLL', '/OUT:' + str(out / 'MochizukiNrRuntime.dll'), *objects,
         str(library), 'd3d12.lib', 'delayimp.lib', '/DELAYLOAD:vulkan-1.dll'])
    for source, name, inc, libs in (
        (ROOT / 'windows/build/profile_host.cpp', 'profile_host', [product, adapter, out], ['d3d12.lib', 'dxgi.lib']),
        (ROOT / 'windows/build/check_encode.cpp', 'check_encode', [headers, ROOT / 'windows/src/core'], [str(library)]),
        (fixture_path, 'host_contract', [product], ['d3d12.lib', 'dxgi.lib']),
    ):
        tool_flags = flags if name == 'check_encode' else [f for f in flags if f != '/DNOMINMAX']
        run(['cl', *tool_flags, *['/I' + str(p) for p in inc], str(source), '/Fo' + str(out / (name + '.obj')),
             '/Fe' + str(out / (name + '.exe')), '/link', *libs])
    run([sys.executable, str(host / 'third_party/mochizuki/windows/build/build_network.py'), 'rdna4',
         '--glslang', str(glslang), '--out', str(out / 'dlssnr-amd/shaders')])
    if identities != {str(p.relative_to(host)): digest(p) for p in sorted(watched)}:
        raise SystemExit('source/toolchain changed during build; discard this result and rebuild')
    result = {'kind': 'offline-production-check' if args.production else 'offline-diagnostics-only',
              'host_sources': identities, 'commands': commands,
              'study_sources': {str(p.relative_to(ROOT)): digest(p) for p in
                                (Path(__file__), ROOT / 'windows/build/profile_host.cpp', ROOT / 'windows/build/check_encode.cpp')},
              'artifacts': {str(p.relative_to(out)): digest(p) for p in sorted(out.rglob('*'))
                            if p.suffix in ('.dll', '.exe', '.spv', '.txt')}}
    (out / 'offline-build.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('OFFLINE_BUILD_OK', out)


if __name__ == '__main__':
    main()
