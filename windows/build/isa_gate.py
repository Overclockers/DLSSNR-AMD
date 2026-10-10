"""Fail closed on missing, stale, or rejected pre-measurement evidence.

Mechanism assessment is authored after reviewing driver ISA. Hash differences
and instruction counts cannot establish that a scheduling hypothesis worked.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from experiment_support import digest


def driver_text(path):
    """Read .text from the driver's ELF64 dump, including a text preamble."""
    raw = Path(path).read_bytes()
    start = raw.find(b'\x7fELF')
    if start < 0:
        raise ValueError(f'missing driver ELF: {path}')
    raw = raw[start:]
    if len(raw) < 64 or raw[4:6] != b'\x02\x01':
        raise ValueError(f'expected little-endian ELF64: {path}')
    offset = struct.unpack_from('<Q', raw, 40)[0]
    stride, count, names_index = struct.unpack_from('<HHH', raw, 58)
    if stride < 64 or not count or names_index >= count or offset + count * stride > len(raw):
        raise ValueError(f'invalid ELF section table: {path}')
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, offset + i * stride) for i in range(count)]

    def data(section):
        begin, size = section[4:6]
        if begin + size > len(raw):
            raise ValueError(f'truncated ELF section: {path}')
        return raw[begin:begin + size]

    names = data(sections[names_index])
    for section in sections:
        if section[0] < len(names) and names[section[0]:].split(b'\0', 1)[0] == b'.text':
            text = data(section)
            if text:
                return text
    raise ValueError(f'missing or empty ELF .text: {path}')


def shader_tree(directory):
    directory = Path(directory)
    if not directory.is_dir():
        raise ValueError(f'missing shader directory: {directory}')
    files = {p.relative_to(directory).as_posix(): digest(p)
             for p in directory.rglob('*') if p.is_file()}
    if not any(name.endswith('.spv') for name in files):
        raise ValueError('shader tree contains no SPIR-V')
    return files


def check_gate(manifest):
    manifest = Path(manifest).resolve()
    evidence = json.loads(manifest.read_text(encoding='utf-8'))

    def resolve(name):
        return (manifest.parent / name).resolve()

    def verified(record):
        path = resolve(record['path'])
        if not path.is_file() or not path.stat().st_size or digest(path) != record['sha256']:
            raise ValueError(f'missing, empty or stale evidence: {path}')
        return path

    if evidence.get('schema_version') != 1:
        raise ValueError('unsupported gate schema')
    for field in ('gpu', 'driver', 'semantic_argument'):
        if not isinstance(evidence.get(field), str) or not evidence[field].strip():
            raise ValueError(f'missing gate field: {field}')
    roots = {arm: resolve(evidence[arm + '_shaders']) for arm in ('baseline', 'candidate')}
    if roots['baseline'] == roots['candidate']:
        raise ValueError('baseline and candidate must be independently saved')
    trees = {arm: shader_tree(root) for arm, root in roots.items()}
    for arm, tree in trees.items():
        if tree != evidence[arm + '_files']:
            raise ValueError(f'{arm} shader tree differs from reviewed evidence')
    all_names = trees['baseline'].keys() | trees['candidate'].keys()
    changed = {name for name in all_names if trees['baseline'].get(name) != trees['candidate'].get(name)}
    if not changed or any(not name.endswith('.spv') for name in changed):
        raise ValueError('gate expects shader changes with unchanged host/shader markers')
    units = evidence['units']
    names = [unit['shader'] for unit in units]
    if len(set(names)) != len(names) or set(names) != changed:
        raise ValueError('ISA evidence must cover every changed shader exactly once')
    result = []
    for unit in units:
        assessment = unit['assessment']
        reason = assessment.get('reason')
        if assessment.get('mechanism_realized') is not True or not isinstance(reason, str) or not reason.strip():
            raise ValueError(f'mechanism rejected or unreviewed: {unit["shader"]}')
        code = {}
        for arm in ('baseline', 'candidate'):
            # These dump identities must be tied to the exact SPIR-V by the
            # capture/probe report, not inferred from a file name.
            if unit[arm + '_spv_sha256'] != trees[arm][unit['shader']]:
                raise ValueError(f'stale shader binding: {unit["shader"]}')
            verified(unit[arm + '_isa'])
            code[arm] = driver_text(verified(unit[arm + '_elf']))
        if code['baseline'] == code['candidate']:
            raise ValueError(f'driver .text unchanged: {unit["shader"]}')
        capture = json.loads(verified(unit['capture_identity']).read_text(encoding='utf-8'))
        if capture.get('gpu') != evidence['gpu'] or capture.get('driver') != evidence['driver']:
            raise ValueError('driver capture environment differs from reviewed evidence')
        for arm in ('baseline', 'candidate'):
            expected = {'spv_sha256': unit[arm + '_spv_sha256'],
                        'isa_sha256': unit[arm + '_isa']['sha256'],
                        'elf_sha256': unit[arm + '_elf']['sha256']}
            if capture.get(arm) != expected:
                raise ValueError(f'driver capture does not bind the {arm} SPIR-V/ISA/ELF')
        result.append({'shader': unit['shader'], 'assessment': assessment,
                       'text_sha256': {arm: hashlib.sha256(blob).hexdigest() for arm, blob in code.items()}})
    pairs = evidence['exact_outputs']
    if not pairs:
        raise ValueError('at least one exact output comparison is required')
    for pair in pairs:
        a, b = verified(pair['baseline']), verified(pair['candidate'])
        if a.samefile(b) or a.stat().st_size != b.stat().st_size or digest(a) != digest(b):
            raise ValueError('independent output comparison failed')
    return {'status': 'passed', 'manifest_sha256': digest(manifest), 'units': result,
            'shader_roots': {arm: str(root) for arm, root in roots.items()},
            'exact_output_pairs': len(pairs), 'scope': 'screening only; retention requires full validation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(check_gate(args.evidence), indent=2))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        parser.exit(1, f'ISA_GATE_FAILED: {exc}\n')


if __name__ == '__main__':
    main()
