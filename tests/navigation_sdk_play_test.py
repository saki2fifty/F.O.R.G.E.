"""XY queries/observation and clean restart in the production SDK Play owner."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

from runtime_client import Runtime, activate

runtime, prepare, worker, module = map(lambda p: Path(p).resolve(), sys.argv[1:5])
root = Path(subprocess.check_output(
    [prepare, runtime.parent / 'navigation-sdk-play-data', worker, '--prepare-planar'], text=True).strip())
shutil.copy2(module, root / module.name)
info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
(root / 'forge.project.json').write_text(json.dumps(dict(
    version=2, name='XY navigation SDK Play', startup_scene=None,
    game=dict(version=1, application_id='org.forge.navigation-xy-test', title='XY navigation',
              profile='development', save_schema=1,
              display=dict(mode='windowed', width=640, height=480, display=0, vsync=False),
              audio=dict(master_volume=1), input=dict(mouse_sensitivity=1, gamepad_sensitivity=1,
                  invert_mouse_y=False, invert_gamepad_y=False, stick_deadzone=.15, trigger_deadzone=.05)),
    input=dict(version=1, actions=[]), modules=[dict(
        id='project.navigation_probe', implementation='1', sdk='experimental-1',
        fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.navigation', 'forge.input'])])), encoding='utf-8')
source = (root / 'scene.json').read_bytes()
scene = json.loads(source)
next(row for row in scene['entities'] if row['name'] == 'Agent')['components']['forge.navigation_agent']['speed'] = 20
arguments = ['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'test-users']

def observation(state):
    row = next(row for row in state['effective_scene']['entities'] if row['name'] == 'Agent')
    out = row['navigation_debug']
    assert out['sampled'] and out['has_path'] and out['status'] == 'success', out
    assert out['position'][2] == 37.125 and out['velocity'][2] == 0, out
    assert out['projected_destination'][2] == 37.125, out
    return out

first = Runtime(runtime, arguments)
second = None
try:
    activate(first, scene, True)
    expected = []
    for _ in range(12):
        state = first.request('step')
        expected.append(observation(state)['position'])
    assert any('navigation SDK queries passed' in item['text'] for item in state['diagnostics']), state
    assert state['recovery'] is None, 'SDK Play must not advertise unsupported native recovery'
    try:
        first.request('replace', scene=scene, recovery={})
    except RuntimeError as error:
        assert 'Recovery is not supported' in str(error), error
    else:
        raise AssertionError('SDK Play accepted an unsupported recovery request')
    first.close()
    first = None
    # SDK Play owns the project writer lease and restarts from authored content.
    # Semantic recovery is tested separately through the supporting runtime path.
    second = Runtime(runtime, arguments)
    activate(second, scene, True)
    for position in expected:
        actual = observation(second.request('step'))['position']
        assert all(abs(x-y) < .001 for x, y in zip(position, actual)), (position, actual)
    for _ in range(90):
        state = second.request('step')
        observation(state)
        if any('navigation agent arrived; C++ changed destination' in d['text'] for d in state['diagnostics']):
            break
    else:
        raise AssertionError('SDK Play C++ did not observe arrival and change destination')
    assert (root / 'scene.json').read_bytes() == source
finally:
    if first:
        first.close()
    if second:
        second.close()
print('Production SDK Play: XY queries, copied progress, return journey and clean authored restart passed')
