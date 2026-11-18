"""Exercise a matching native policy consumer through fresh SDK Play hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from runtime_client import Runtime, activate


def verify(runtime, module, prepare, worker, scratch, planar):
    flag = '--prepare-sdk-planar' if planar else '--prepare-sdk-spatial'
    root = Path(subprocess.check_output([prepare, scratch, worker, flag], text=True).strip())
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    settings = json.loads((root / 'forge.project.json').read_text())
    settings['modules'] = [dict(id='project.navigation_area_probe', implementation='1',
        sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.navigation', 'forge.input'])]
    settings['game'] = dict(version=1, application_id='org.forge.navigation-area-test',
        title='Navigation areas', profile='development', save_schema=1,
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
            deadline = time.monotonic() + 15
            messages = set()
            while 'Navigation area SDK boundary passed' not in messages:
                state = host.request('step')
                messages.update(row['text'] for row in state['diagnostics'])
                assert 'Navigation area SDK boundary failed' not in messages, state
                assert time.monotonic() < deadline, (messages, state)
                time.sleep(.005)
            host.quit()
        finally:
            host.close()
    print(('XY' if planar else '3D') + ': native policy Meta/query boundaries and two fresh SDK Play hosts passed')


if __name__ == '__main__':
    runtime, module, prepare, worker, scratch = [Path(p).resolve() for p in sys.argv[1:6]]
    verify(runtime, module, prepare, worker, scratch, sys.argv[6:] == ['--planar'])
