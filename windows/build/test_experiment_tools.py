"""CPU regression checks for experiment cache integrity and measurement gates."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest
from unittest.mock import patch
import uuid

import build_network as builder
from experiment_support import ShaderCache, StageTimings, digest
from isa_gate import check_gate, driver_text, shader_tree
from run_experiment import run_plan


ROOT = Path(__file__).resolve().parents[2]


def fake_elf(code):
    header = bytearray(64)
    header[:6] = b'\x7fELF\x02\x01'
    names = b'\0.shstrtab\0.text\0'
    payload = bytes(header) + names + code
    struct.pack_into('<Q', header, 40, len(payload))
    struct.pack_into('<HHH', header, 58, 64, 3, 1)
    sections = bytes(64)
    sections += struct.pack('<IIQQQQIIQQ', 1, 3, 0, 0, 64, len(names), 0, 0, 1, 0)
    sections += struct.pack('<IIQQQQIIQQ', 11, 1, 0, 0, 64 + len(names), len(code), 0, 0, 4, 0)
    return b'Driver binary dump\n' + bytes(header) + payload[64:] + sections


class ExperimentToolsTests(unittest.TestCase):
    def setUp(self):
        # Inherit workspace ACLs instead of TemporaryDirectory's private ACL.
        self.directory = ROOT / 'build/windows/experiment-tooling/tests' / uuid.uuid4().hex
        self.directory.mkdir(parents=True)
        self.saved = {name: getattr(builder, name) for name in ('R', 'GLSLANG', 'CACHE', 'TIMINGS', 'TOOL_IDENTITY', 'QUAD')}

    def tearDown(self):
        for name, value in self.saved.items():
            setattr(builder, name, value)

    def record(self, name, blob):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
        return {'path': name, 'sha256': digest(path)}

    def gate_fixture(self):
        for arm in ('baseline', 'candidate'):
            self.record(arm + '/g_test.spv', b'\x03\x02\x23\x07' + arm.encode())
            self.record(arm + '/shader-constants.txt', b'matched constants')
        unit = {'shader': 'g_test.spv', 'assessment': {'mechanism_realized': True, 'reason': 'Synthetic test assessment'}}
        capture = {'gpu': 'test GPU', 'driver': 'test driver'}
        for arm in ('baseline', 'candidate'):
            unit[arm + '_spv_sha256'] = digest(self.directory / arm / 'g_test.spv')
            unit[arm + '_isa'] = self.record(arm + '.isa', b'instruction ' + arm.encode())
            unit[arm + '_elf'] = self.record(arm + '.elf', fake_elf(arm.encode()))
            capture[arm] = {'spv_sha256': unit[arm + '_spv_sha256'],
                            'isa_sha256': unit[arm + '_isa']['sha256'],
                            'elf_sha256': unit[arm + '_elf']['sha256']}
        unit['capture_identity'] = self.record('capture.json', json.dumps(capture).encode())
        evidence = {'schema_version': 1, 'gpu': capture['gpu'], 'driver': capture['driver'],
                    'semantic_argument': 'Synthetic test argument', 'units': [unit],
                    'exact_outputs': [{'baseline': self.record('output-a.raw', b'exact output'),
                                       'candidate': self.record('output-b.raw', b'exact output')}]}
        for arm in ('baseline', 'candidate'):
            evidence[arm + '_shaders'] = arm
            evidence[arm + '_files'] = shader_tree(self.directory / arm)
        path = self.directory / 'gate.json'
        path.write_text(json.dumps(evidence), encoding='utf-8')
        return path, evidence

    def test_cache_corruption_is_a_miss(self):
        cache = ShaderCache(self.directory / 'cache')
        output = self.directory / 'shader.spv'
        output.write_bytes(b'\x03\x02\x23\x07shader')
        cache.store('key', output)
        self.assertTrue(cache.restore('key', self.directory / 'restored.spv'))
        (cache.directory / 'key.spv').write_bytes(b'corrupt')
        self.assertFalse(cache.restore('key', self.directory / 'restored.spv'))

    def test_shader_key_covers_includes_defines_transform_environment_and_tools(self):
        compiler = self.directory / 'compiler'
        compiler.write_bytes(b'compiler')
        builder.R = self.directory
        builder.GLSLANG = compiler
        builder.CACHE = ShaderCache(self.directory / 'cache')
        builder.TOOL_IDENTITY = {'compiler': digest(compiler), 'scripts': 'version-one'}
        builder.TIMINGS = StageTimings(self.directory / 'timings.jsonl')
        source = self.directory / 'test.comp'
        source.write_text('#include "nested.glsl"', encoding='utf-8')
        include = self.directory / 'nested.glsl'
        include.write_text('first include', encoding='utf-8')
        compiled = []

        def compile_command(command, **kwargs):
            if '-E' in command:
                text = source.read_text() + include.read_text() + repr(command[3:-1])
                return type('Result', (), {'returncode': 0, 'stdout': text, 'stderr': ''})()
            target = Path(command[command.index('-o') + 1])
            target.write_bytes(b'\x03\x02\x23\x07' + str(len(compiled)).encode())
            compiled.append(command)
            return type('Result', (), {'returncode': 0, 'stdout': '', 'stderr': ''})()

        output = self.directory / 'test.spv'
        with patch.object(builder.subprocess, 'run', side_effect=compile_command):
            builder.glslang('rdna4', source, ['MODE=1'], output)
            builder.glslang('rdna4', source, ['MODE=1'], output)
            self.assertEqual(len(compiled), 1)
            include.write_text('changed include', encoding='utf-8')
            builder.glslang('rdna4', source, ['MODE=1'], output)
            builder.glslang('rdna4', source, ['MODE=2'], output)
            with patch.dict(os.environ, {'NR_Q32_DIRECT': '0'}):
                builder.glslang('rdna4', source, ['MODE=2'], output)
            builder.TOOL_IDENTITY['scripts'] = 'version-two'
            builder.glslang('rdna4', source, ['MODE=2'], output)
            builder.TOOL_IDENTITY['compiler'] = 'different compiler'
            builder.glslang('rdna4', source, ['MODE=2'], output)
        self.assertEqual(len(compiled), 6)

    def test_gate_accepts_complete_evidence_and_rejects_stale_shader(self):
        path, evidence = self.gate_fixture()
        self.assertEqual(check_gate(path)['status'], 'passed')
        (self.directory / 'candidate/g_test.spv').write_bytes(b'changed after review')
        with self.assertRaisesRegex(ValueError, 'differs from reviewed'):
            check_gate(path)

    def test_gate_rejects_failed_mechanism_or_missing_coverage(self):
        path, evidence = self.gate_fixture()
        evidence['units'][0]['assessment']['mechanism_realized'] = False
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'rejected or unreviewed'):
            check_gate(path)
        evidence['units'] = []
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'every changed shader'):
            check_gate(path)

    def test_gate_rejects_unequal_outputs_and_unbound_capture(self):
        path, evidence = self.gate_fixture()
        evidence['exact_outputs'][0]['candidate'] = self.record('output-b.raw', b'wrong output')
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'comparison failed'):
            check_gate(path)
        evidence['exact_outputs'][0]['candidate'] = self.record('output-b.raw', b'exact output')
        capture_record = evidence['units'][0]['capture_identity']
        capture = json.loads((self.directory / capture_record['path']).read_text())
        capture['candidate']['spv_sha256'] = 'wrong shader'
        evidence['units'][0]['capture_identity'] = self.record('capture.json', json.dumps(capture).encode())
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'does not bind'):
            check_gate(path)

    def test_truncated_elf_is_rejected(self):
        path = self.directory / 'bad.elf'
        path.write_bytes(fake_elf(b'code')[:-20])
        with self.assertRaises(ValueError):
            driver_text(path)

    def test_gate_rejects_unchanged_code_and_linked_outputs(self):
        path, evidence = self.gate_fixture()
        unit = evidence['units'][0]
        unit['candidate_elf'] = self.record('candidate.elf', fake_elf(b'baseline'))
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unchanged'):
            check_gate(path)
        path, evidence = self.gate_fixture()
        linked = self.directory / 'linked.raw'
        linked.hardlink_to(self.directory / 'output-a.raw')
        evidence['exact_outputs'][0]['candidate'] = {'path': linked.name, 'sha256': digest(linked)}
        path.write_text(json.dumps(evidence), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'comparison failed'):
            check_gate(path)

    def test_runner_blocks_measurement_and_records_failure(self):
        sentinel = self.directory / 'measurement-started'
        command = [sys.executable, '-c', 'from pathlib import Path; Path("measurement-started").touch()']
        plan = {'schema_version': 1, 'gate': 'missing.json', 'stages': [
            {'phase': 'correctness', 'command': [sys.executable, '-c', 'pass']},
            {'phase': 'isa', 'command': [sys.executable, '-c', 'pass']},
            {'phase': 'measure', 'command': command}]}
        path = self.directory / 'plan.json'
        path.write_text(json.dumps(plan), encoding='utf-8')
        with self.assertRaises(OSError):
            run_plan(path)
        self.assertFalse(sentinel.exists())
        events = [json.loads(line) for line in (self.directory / 'stage-timings.jsonl').read_text().splitlines()]
        self.assertTrue(any(event.get('stage') == 'isa_gate' and event.get('status') == 'failed' for event in events))
        self.assertFalse(any(event['stage'] == 'measure' for event in events))

    def test_runner_runs_reviewed_measurement_and_times_failed_commands(self):
        gate, evidence = self.gate_fixture()
        plan = {'schema_version': 1, 'gate': gate.name, 'stages': [
            {'phase': 'correctness', 'command': [sys.executable, '-c', 'pass']},
            {'phase': 'isa', 'command': [sys.executable, '-c', 'pass']},
            {'phase': 'measure', 'command': [sys.executable, '-c', 'pass']}]}
        plan.update(baseline_shaders='baseline', candidate_shaders='candidate')
        path = self.directory / 'plan.json'
        path.write_text(json.dumps(plan), encoding='utf-8')
        timings = run_plan(path, from_stage='measure')
        self.assertGreater(timings.totals['measure'], 0)
        plan['stages'][0]['command'] = [sys.executable, '-c', 'raise SystemExit(7)']
        path.write_text(json.dumps(plan), encoding='utf-8')
        with self.assertRaises(subprocess.CalledProcessError):
            run_plan(path)
        events = [json.loads(line) for line in (self.directory / 'stage-timings.jsonl').read_text().splitlines()]
        self.assertTrue(any(event.get('stage') == 'correctness' and event.get('status') == 'failed' for event in events))

    def test_runner_rejects_evidence_for_another_candidate(self):
        gate, evidence = self.gate_fixture()
        plan = {'schema_version': 1, 'gate': gate.name, 'baseline_shaders': 'baseline',
                'candidate_shaders': 'other-candidate', 'stages': [
                    {'phase': 'correctness', 'command': [sys.executable, '-c', 'pass']},
                    {'phase': 'isa', 'command': [sys.executable, '-c', 'pass']},
                    {'phase': 'measure', 'command': [sys.executable, '-c', 'raise SystemExit(99)']}]}
        path = self.directory / 'plan.json'
        path.write_text(json.dumps(plan), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'does not cover'):
            run_plan(path, from_stage='measure')


if __name__ == '__main__':
    unittest.main()
