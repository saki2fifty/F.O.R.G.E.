"""Original Gallery touch actions and installed-only C++ through both real Play hosts."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from runtime_client import Runtime, activate

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
for sdk in (False, True):
    with tempfile.TemporaryDirectory(prefix='touch-input-', dir=runtime.parent) as temporary:
        root = Path(temporary) / 'Game'
        shutil.copytree(gallery, root)
        source = root / 'Scenes/88-touch-input.scene.json'
        original = json.loads(source.read_text())
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        project = json.loads((root / 'forge.project.json').read_text())
        project['startup_scene'] = dict(asset=original['asset_id'], source=source.relative_to(root).as_posix())
        project['game']['application_id'] = 'org.forge.touch-input-test'
        shutil.copy2(module, root / module.name)
        fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
        project['modules'] = [dict(id='project.touch_input', implementation='1', sdk='experimental-1',
                                   fingerprint=fingerprint, library=module.name,
                                   dependencies=['forge.input', 'forge.transforms'])]
        (root / 'forge.project.json').write_text(json.dumps(project))
        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on' if sdk else 'off',
                                   '--user-data', Path(temporary) / 'Users'])
        def cursor(state):
            return next(row['components'] for row in state['effective_scene']['entities']
                        if row['id'] == '15500000-1550-4155-8155-000000000002')
        def until(predicate):
            end = time.monotonic() + 10
            while time.monotonic() < end:
                state = worker.request('snapshot')
                if predicate(state):
                    return state
                time.sleep(.005)
            raise AssertionError((sdk, state))
        try:
            activate(worker, original, sdk)
            worker.request('resume')
            worker.request('snapshot', input_events=[dict(control='touch.0.' + field, value=value)
                for field, value in (('down', 1), ('x', .75), ('y', .25), ('pressure', .5))])
            seen = until(lambda s: abs(cursor(s)['forge.local_translation']['x'] - 4) < 1e-8 and
                                  cursor(s)['forge.sprite_renderer']['visible'])
            assert cursor(seen)['forge.local_translation']['y'] == 2.5, seen
            assert abs(cursor(seen)['forge.sprite_renderer']['width'] - .8) < 1e-6, seen
            # A second contact cannot change the sample's deliberately chosen slot0.
            worker.request('snapshot', input_events=[dict(control='touch.1.x', value=0)])
            assert cursor(worker.request('snapshot'))['forge.local_translation']['x'] == 4
            worker.request('snapshot', input_events=[dict(control='touch.0.pressure', value=1)])
            until(lambda s: abs(cursor(s)['forge.sprite_renderer']['width'] - 1.2) < 1e-6)
            worker.request('snapshot', input_events=[dict(reset=True)])
            until(lambda s: not cursor(s)['forge.sprite_renderer']['visible'])
            worker.request('pause')
            # Restart starts neutral. No touch identity or input state enters source.
            activate(worker, original, sdk)
            worker.request('resume')
            until(lambda s: not cursor(s)['forge.sprite_renderer']['visible'])
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
            worker.request('quit')
            worker.process.wait(timeout=5)
            assert worker.process.returncode == 0
        finally:
            worker.close()
print('Gallery88 independent touch coordinates/pressure, public C++, both hosts and neutral restart passed')
