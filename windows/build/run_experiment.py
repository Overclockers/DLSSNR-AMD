"""Run a charged offline shader experiment sequentially with an ISA gate.

The plan contains argument arrays, never shell commands. This utility does not
allocate attempts, choose hypotheses, retain code, or alter measurement policy.
"""
import argparse
import json
from pathlib import Path
import subprocess

from experiment_support import StageTimings, digest
from isa_gate import check_gate


PHASES = ('prepare', 'build', 'correctness', 'isa', 'screen', 'measure', 'validate')


def run_plan(path, from_stage=None, through=None):
    path = Path(path).resolve()
    plan = json.loads(path.read_text(encoding='utf-8'))
    stages = plan['stages']
    if plan.get('schema_version') != 1 or not stages:
        raise ValueError('expected schema_version=1 and nonempty stages')
    ranks = [PHASES.index(item['phase']) for item in stages]
    if ranks != sorted(ranks):
        raise ValueError('stages must follow prepare/build/correctness/isa/screen/measure/validate order')
    if not {'correctness', 'isa'}.issubset({item['phase'] for item in stages}):
        raise ValueError('plan must include correctness and isa stages')
    for item in stages:
        command = item['command']
        if not isinstance(command, list) or not command or any(not isinstance(a, str) or not a for a in command):
            raise ValueError('commands must be nonempty argument arrays')
        if item.get('timeout_seconds', 900) <= 0:
            raise ValueError('command timeout must be positive')
    first = PHASES.index(from_stage) if from_stage else 0
    last = PHASES.index(through) if through else len(PHASES) - 1
    if first > last:
        raise ValueError('--from-stage must not follow --through')
    selected = [item for item in stages if first <= PHASES.index(item['phase']) <= last]
    if not selected:
        raise ValueError('no stages selected')
    timings = StageTimings(path.parent / plan.get('timings', 'stage-timings.jsonl'))
    logs = path.parent / plan.get('logs', 'stage-logs')
    logs.mkdir(parents=True, exist_ok=True)
    cwd = (path.parent / plan.get('cwd', '.')).resolve()
    with timings.stage('workflow', plan_sha256=digest(path)):
        for index, item in enumerate(selected):
            if item['phase'] in ('screen', 'measure', 'validate'):
                with timings.stage('isa_gate'):
                    gate = check_gate(path.parent / plan['gate'])
                    for arm in ('baseline', 'candidate'):
                        if str((path.parent / plan[arm + '_shaders']).resolve()) != gate['shader_roots'][arm]:
                            raise ValueError(f'{arm} gate does not cover the planned shader directory')
                    timings.event(stage='isa_gate', event='verdict', **gate)
            label = item.get('name', item['phase'])
            print(f'EXPERIMENT_STAGE {item["phase"]}: {label}', flush=True)
            log_path = logs / f'{timings.run_id}-{index}.log'
            with timings.stage(item['phase'], label=label, command=item['command'], log=str(log_path)):
                with log_path.open('w', encoding='utf-8') as stream:
                    subprocess.run(item['command'], cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                   timeout=item.get('timeout_seconds', 900), check=True, shell=False)
    print('wall-clock seconds:', json.dumps(timings.totals, sort_keys=True))
    return timings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('--from-stage', choices=PHASES)
    parser.add_argument('--through', choices=PHASES)
    args = parser.parse_args()
    try:
        run_plan(args.plan, args.from_stage, args.through)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'EXPERIMENT_STOPPED: {exc}\n')


if __name__ == '__main__':
    main()
