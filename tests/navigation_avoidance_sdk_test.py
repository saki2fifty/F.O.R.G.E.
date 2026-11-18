"""Observe typed avoidance policies and actual yielding in matching SDK hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from runtime_client import Runtime, activate


def verify(runtime, module, prepare, worker, scratch, planar):
    flag = '--prepare-policy-sdk-planar' if planar else '--prepare-policy-sdk'
    root = Path(subprocess.check_output([prepare, scratch, worker, flag], text=True).strip())
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    settings = json.loads((root / 'forge.project.json').read_text())
    settings['modules'] = [dict(id='project.avoidance_policy_probe', implementation='1',
        sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.navigation', 'forge.transforms', 'forge.input'])]
    settings['game'] = dict(version=1, application_id='org.forge.avoidance-policy-test',
        title='Avoidance policies', profile='development', save_schema=1,
        display=dict(mode='windowed', width=640, height=480, display=0, vsync=False),
        audio=dict(master_volume=1), input=dict(mouse_sensitivity=1, gamepad_sensitivity=1,
            invert_mouse_y=False, invert_gamepad_y=False, stick_deadzone=.15, trigger_deadzone=.05))
    (root / 'forge.project.json').write_text(json.dumps(settings))
    scene = json.loads((root / 'scene.json').read_text())
    for restart in range(2):
        host = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on',
                                 '--user-data', root / 'test-users'])
        try:
            activate(host, scene, True)
            # Instrumented native hosts need time to complete the same fixed-tick route.
            deadline = time.monotonic() + 180
            messages = set()
            while 'Avoidance policy SDK passed' not in messages:
                state = host.request('step')
                messages.update(row['text'] for row in state['diagnostics'])
                progress = {row['name']: row['navigation_debug']
                            for row in state.get('effective_scene', {}).get('entities', [])
                            if 'navigation_debug' in row}
                assert 'Avoidance policy SDK failed' not in messages, (messages, progress)
                assert time.monotonic() < deadline, (messages, progress)
            host.quit()
        finally:
            host.close()
    print(('XY' if planar else '3D') + ': typed policy layout, actual yielding and two graceful SDK hosts passed')


if __name__ == '__main__':
    runtime, module, prepare, worker, scratch = [Path(p).resolve() for p in sys.argv[1:6]]
    verify(runtime, module, prepare, worker, scratch, sys.argv[6:] == ['--planar'])
