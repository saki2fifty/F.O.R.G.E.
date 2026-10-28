"""Shared project/source isolation for original public-SDK input examples."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from runtime_client import Runtime

@contextmanager
def touch_example(runtime, module, gallery, scene_name, module_id, sdk, *, launch=True):
    with tempfile.TemporaryDirectory(prefix='touch-example-', dir=runtime.parent) as temporary:
        root = Path(temporary) / 'Game'
        shutil.copytree(gallery, root)
        source = root / 'Scenes' / scene_name
        original = json.loads(source.read_text())
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        project = json.loads((root / 'forge.project.json').read_text())
        project['startup_scene'] = dict(asset=original['asset_id'], source=source.relative_to(root).as_posix())
        project['game']['application_id'] = 'org.forge.touch-example-test'
        shutil.copy2(module, root / module.name)
        fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
        project['modules'] = [dict(id=module_id, implementation='1', sdk='experimental-1',
                                   fingerprint=fingerprint, library=module.name,
                                   dependencies=['forge.input', 'forge.transforms'] +
                                   (['forge.game'] if module_id == 'project.gamepad_motion' else []))]
        (root / 'forge.project.json').write_text(json.dumps(project))
        if not launch:
            yield root, original
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
            return
        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on' if sdk else 'off',
                                   '--user-data', Path(temporary) / 'Users'])
        try:
            yield worker, original
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        finally:
            worker.close()

def observe_touch(worker, predicate):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        state = worker.request('snapshot')
        if predicate(state):
            return state
        time.sleep(.005)
    raise AssertionError(state)
