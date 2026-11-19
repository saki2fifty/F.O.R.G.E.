"""Exercise the exact SDK callbacks against real rebuilt geometry in fresh hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from runtime_client import Runtime, activate


def verify(runtime, module, prepare, worker, scratch, planar=False, env=None):
    assert not planar, 'This probe exercises the spatial native SDK boundary'
    root = Path(subprocess.check_output(
        [prepare, scratch, worker, '--prepare-sdk-spatial'], text=True).strip())
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True, env=env))
    settings = json.loads((root / 'forge.project.json').read_text())
    settings['modules'] = [dict(id='project.navigation_rebuild_probe', implementation='1',
        sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.navigation', 'forge.input'])]
    # Use current project defaults through the existing settings contract. The
    # isolated test supplies its own user directory on every fresh host.
    gallery = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery'
    settings['game'] = json.loads((gallery / 'forge.project.json').read_text())['game']
    settings['game']['application_id'] = 'org.forge.navigation-rebuild-test'
    (root / 'forge.project.json').write_text(json.dumps(settings))
    scene = json.loads((root / 'scene.json').read_text())
    for row in scene['entities']:
        if 'forge.navigation_agent' in row['components']:
            row['components']['forge.navigation_agent']['enabled'] = False
    for restart in range(2):
        host = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on',
                                 '--user-data', root / 'test-users'], env=env)
        try:
            activate(host, scene, True)
            deadline = time.monotonic() + 20
            messages = set()
            while 'Navigation rebuild SDK boundary passed' not in messages:
                state = host.request('step')
                messages.update(row['text'] for row in state['diagnostics'])
                assert 'Navigation rebuild SDK boundary failed' not in messages, state
                assert time.monotonic() < deadline, (messages, state)
                time.sleep(.005)
            tick = state['timing']['tick']
            while 'Navigation rebuild SDK control boundary passed' not in messages:
                state = host.request('snapshot')
                messages.update(row['text'] for row in state['diagnostics'])
                assert 'Navigation rebuild SDK control boundary failed' not in messages, state
                assert state['timing']['tick'] == tick, state
                assert time.monotonic() < deadline, (messages, state)
                time.sleep(.005)
            host.quit()
        finally:
            host.close()
    print('Native SDK rebuild: moved source/path, POD validation, token/owner/grant '
          'boundaries, cancellation and two graceful fresh hosts passed')


if __name__ == '__main__':
    runtime, module, prepare, worker, scratch = [Path(p).resolve() for p in sys.argv[1:6]]
    verify(runtime, module, prepare, worker, scratch)
