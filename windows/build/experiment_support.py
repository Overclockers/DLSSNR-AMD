"""Offline experiment wall-clock records and verified shader cache entries."""
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import uuid


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.', suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


class StageTimings:
    """Append start/end events so failed or interrupted work remains visible."""

    def __init__(self, path):
        self.path = Path(path)
        self.run_id = uuid.uuid4().hex
        self.totals = {}

    def event(self, **data):
        record = dict(run_id=self.run_id, utc=datetime.now(timezone.utc).isoformat(), **data)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(record, ensure_ascii=False) + '\n')
            stream.flush()

    @contextmanager
    def stage(self, name, **details):
        stage_id = uuid.uuid4().hex
        self.event(stage=name, stage_id=stage_id, event='start', **details)
        started = time.perf_counter()
        status = 'passed'
        try:
            yield
        except BaseException:
            status = 'failed'
            raise
        finally:
            elapsed = time.perf_counter() - started
            self.totals[name] = self.totals.get(name, 0.0) + elapsed
            self.event(stage=name, stage_id=stage_id, event='end', status=status,
                       elapsed_seconds=elapsed, **details)


class ShaderCache:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.hits = 0
        self.misses = 0

    def restore(self, key, output):
        try:
            metadata = json.loads((self.directory / (key + '.json')).read_text(encoding='utf-8'))
            blob = (self.directory / (key + '.spv')).read_bytes()
            if (metadata['key'] == key and blob[:4] == b'\x03\x02\x23\x07'
                    and hashlib.sha256(blob).hexdigest() == metadata['sha256']):
                write_atomic(output, blob)
                self.hits += 1
                return True
        except (OSError, ValueError, KeyError, TypeError):
            pass
        self.misses += 1
        return False

    def store(self, key, output):
        blob = Path(output).read_bytes()
        if blob[:4] != b'\x03\x02\x23\x07':
            raise ValueError('compiler output is not SPIR-V')
        write_atomic(self.directory / (key + '.spv'), blob)
        metadata = {'key': key, 'sha256': hashlib.sha256(blob).hexdigest()}
        write_atomic(self.directory / (key + '.json'), json.dumps(metadata).encode('utf-8'))
