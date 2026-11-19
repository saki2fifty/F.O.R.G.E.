"""Run the optional Gallery System through a real, matching native SDK host."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from runtime_client import Runtime, activate


def verify(runtime, module, gallery, scratch, env=None, *, timeout_seconds=20):
    if not 0 < timeout_seconds <= 120:
        raise ValueError("Gallery test allowance must be positive and at most120seconds")
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='navigation-rebuild-gallery-', dir=scratch) as directory:
        root = Path(directory) / 'Game'
        shutil.copytree(gallery, root)
        shutil.copy2(module, root / module.name)
        settings = json.loads((root / 'forge.project.json').read_text())
        info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True, env=env))
        settings['modules'] = [dict(id='project.navigation_rebuild_gallery', implementation='1',
            sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
            dependencies=['forge.navigation', 'forge.input'])]
        (root / 'forge.project.json').write_text(json.dumps(settings))
        path = root / 'Scenes/237-runtime-navigation-rebuild.scene.json'
        scene = json.loads(path.read_text())
        actor = next(row['id'] for row in scene['entities']
                     if row['name'] == 'Cyan Agent - resumes after runtime rebuild')
        for restart in range(2):
            host = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on',
                                     '--user-data', root / 'test-users'], env=env)
            try:
                activate(host, scene, True)
                deadline = time.monotonic() + timeout_seconds
                rebuilt = arrived = False
                while not (rebuilt and arrived):
                    state = host.request('step')
                    messages = {row['text'] for row in state['diagnostics']}
                    rebuilt |= 'Gallery runtime navigation rebuilt' in messages
                    row = next(row for row in state['effective_scene']['entities']
                               if row['id'] == actor)
                    arrived = row['navigation_debug']['phase'] == 'Arrived'
                    rebuild = state.get('navigation_rebuild')
                    assert not isinstance(rebuild, dict) or rebuild['phase'] not in (
                        'Cancelled', 'Stale', 'Failed'), rebuild
                    assert time.monotonic() < deadline, (messages, row, rebuild)
                    time.sleep(.005)
                assert state['navigation_rebuild']['phase'] == 'Applied', state
                assert json.loads(path.read_text()) == scene
                host.quit()
            finally:
                host.close()
    print('Optional original Gallery System: automatic one-shot rebuild, Agent arrival '
          'and two graceful fresh SDK hosts passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'module', 'gallery', 'scratch'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--timeout-seconds', type=float, default=20,
                        help='Per-host test-only wall-time allowance; engine/IPC limits stay fixed')
    args = parser.parse_args()
    verify(args.runtime.resolve(), args.module.resolve(), args.gallery.resolve(),
           args.scratch.resolve(), timeout_seconds=args.timeout_seconds)
