"""Bounded opt-in runtime bootstrap diagnostics preserve the normal protocol."""
import os
from pathlib import Path
import re
import sys
import tempfile
import uuid

from runtime_client import Runtime

runtime = Path(sys.argv[1]).resolve()
key = 'FORGE_SDK_DIAGNOSTIC_DIR'
previous = os.environ.get(key)
try:
    with tempfile.TemporaryDirectory(dir=runtime.parent) as directory:
        root = Path(directory)
        modes = [()]
        if len(sys.argv) > 2:
            modes.append(('--project', Path(sys.argv[2]).resolve(), '--audio', 'offline', '--ui', 'on'))
        for index, arguments in enumerate(modes):
            diagnostics = root / str(index)
            diagnostics.mkdir()
            os.environ[key] = str(diagnostics)
            worker = Runtime(runtime, arguments)
            try:
                files = list(diagnostics.glob('runtime-startup-*.log'))
                assert len(files) == 1, files
                data = files[0].read_text(encoding='utf-8')
                lines = data.splitlines()
                assert 8 <= len(lines) <= 12 and len(data.encode()) <= 2048, data
                parsed = [re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ '
                                      r'elapsed_ms=(\d+) phase=([a-z-]+)', line) for line in lines]
                assert all(parsed), data
                elapsed = [int(row.group(1)) for row in parsed]
                assert elapsed == sorted(elapsed), data
                assert [row.group(2) for row in parsed] == [
                    'main', 'arguments-parsed', 'world-begin', 'world-ready',
                    'pipes-ready', 'session-ready', 'first-request-received', 'hello-response-queued']
                for _ in range(10):
                    assert worker.request('ping')['ok']
                assert files[0].read_text(encoding='utf-8') == data, 'Bootstrap trace grew after startup'
                state = worker.request('replace', scene=dict(
                    version=5, asset_id=str(uuid.uuid4()), entities=[]))
                replacement = files[0].read_text(encoding='utf-8')
                lines = replacement.splitlines()
                assert 22 <= len(lines) <= 24 and len(replacement.encode()) <= 4096, replacement
                parsed = [re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ '
                                      r'elapsed_ms=(\d+) phase=([a-z-]+)', line) for line in lines]
                assert all(parsed), replacement
                elapsed = [int(row.group(1)) for row in parsed]
                assert elapsed == sorted(elapsed), replacement
                assert [row.group(2) for row in parsed[8:]] == [
                    'replace-world-begin', 'replace-world-ready', 'replace-scene-begin',
                    'replace-scene-ready', 'replace-maps-begin', 'replace-maps-ready',
                    'replace-physics-begin', 'replace-physics-ready', 'replace-audio-begin',
                    'replace-audio-ready', 'replace-published', 'replace-retired',
                    'replace-state-ready', 'replace-response-queued']
                checkpoint = state['recovery']
                worker.request('replace', scene=state['scene'], recovery=checkpoint,
                               recovery_session=checkpoint['session'],
                               recovery_tick=checkpoint['tick'])
                for _ in range(10):
                    assert worker.request('ping')['ok']
                assert files[0].read_text(encoding='utf-8') == replacement, 'Replacement trace grew'
            finally:
                worker.close()
        # Optional diagnostics cannot create directories or make valid startup fail.
        missing = root / 'missing' / 'diagnostics'
        os.environ[key] = str(missing)
        worker = Runtime(runtime)
        worker.close()
        assert not missing.exists()
finally:
    if previous is None:
        os.environ.pop(key, None)
    else:
        os.environ[key] = previous

print('Opt-in UTC/monotonic bounded startup/first-replacement trace, protocol and unavailable path passed')
