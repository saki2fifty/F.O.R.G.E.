"""XY queries/observation and clean restart in the production SDK Play owner."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

from runtime_client import Runtime, activate

runtime, prepare, worker, module = map(lambda p: Path(p).resolve(), sys.argv[1:5])
grounded = '--grounded' in sys.argv[5:]
character = '--character' in sys.argv[5:] or grounded
root = Path(subprocess.check_output(
    [prepare, runtime.parent / 'navigation-sdk-play-data', worker,
     '--prepare-grounded-planar-character' if grounded else '--prepare-planar-character' if character else '--prepare-planar'], text=True).strip())
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
next(row for row in scene['entities'] if row['name'] == 'Agent')['components']['forge.navigation_link_collision'] = dict(
    enabled=True, radius=.3, straight_height=1.2, clearance=.02, mask=4294967295, include_sensors=False)
arguments = ['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'test-users']

def observation(state):
    row = next(row for row in state['effective_scene']['entities'] if row['name'] == 'Agent')
    out = row['navigation_debug']
    assert out['sampled'] and out['has_path'] and out['status'] == 'success', out
    assert out['position'][2] == 37.125 and out['velocity'][2] == 0, out
    assert out['projected_destination'][2] == 37.125, out
    if character:
        assert all(abs(out['position'][axis] - row['world_affine'][offset]) < 1e-6
                   for axis, offset in enumerate((3, 7, 11))), row
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
    assert any('navigation polygon native values passed' in item['text'] for item in state['diagnostics']), state
    assert state['recovery'] is None, 'SDK Play must not advertise unsupported native recovery'
    # Exercise the production host's shared live owner while the world is paused.
    generation = state['activation']['generation']
    target = dict(generation=generation, scene_asset=scene['asset_id'],
                  entity=next(row for row in state['effective_scene']['entities'] if row['name'] == 'Agent')['id'],
                  type='forge.navigation_agent')
    tick = state['timing']['tick']
    original_pose = observation(state)['position']
    def tune(field, value):
        result = first.request('property_tune', **target, property=field, value=value)
        assert result['property_tune']['value'] == value
        assert result['timing']['tick'] == tick
        return result
    tune('enabled', False)
    tune('has_destination', False)
    tune('destination_x', -8)
    tune('destination_y', 0 if grounded else 1)
    tune('destination_z', -99)
    tune('stopping_distance', .25)
    tune('speed', 0)
    tune('enabled', True)
    tune('has_destination', True)
    paused = first.request('step')
    parked = observation(paused)['position']
    if grounded:
        assert parked[0] == original_pose[0] and parked[2] == original_pose[2] and abs(parked[1] - original_pose[1]) < .001
    else:
        assert parked == original_pose
    assert next(row for row in paused['effective_scene']['entities'] if row['name'] == 'Agent')['navigation_debug']['phase'] == 'Paused'
    tick = paused['timing']['tick']
    for changed in [dict(target, generation=generation+1), dict(target, scene_asset='11111111-1111-4111-8111-111111111111')]:
        try:
            first.request('property_tune', **changed, property='speed', value=3)
        except RuntimeError:
            pass
        else:
            raise AssertionError('SDK Play accepted stale/foreign live target')
    for field, value in [('navmesh', None), ('speed', -1), ('enabled', 'yes')]:
        try:
            first.request('property_tune', **target, property=field, value=value)
        except RuntimeError:
            pass
        else:
            raise AssertionError(('SDK Play accepted invalid live field', field, value))
    tune('speed', 3)
    collision_target = dict(target, type='forge.navigation_link_collision')
    for field, value in [('radius', .75), ('straight_height', 2), ('clearance', .125),
                         ('mask', 7), ('include_sensors', True), ('enabled', False)]:
        result = first.request('property_tune', **collision_target, property=field, value=value)
        assert result['property_tune']['value'] == value and result['timing']['tick'] == tick, result
    for field, value in [('radius', 0), ('straight_height', 0), ('clearance', 2), ('mask', .5),
                         ('mask', -1), ('mask', 4294967296), ('include_sensors', 1)]:
        try:
            first.request('property_tune', **collision_target, property=field, value=value)
        except RuntimeError:
            pass
        else:
            raise AssertionError(('SDK Play accepted invalid collision field', field, value))
    resumed = first.request('step')
    assert observation(resumed)['position'] != original_pose
    try:
        first.request('replace', scene=scene, recovery={})
    except RuntimeError as error:
        assert 'Recovery is not supported' in str(error), error
    else:
        raise AssertionError('SDK Play accepted an unsupported recovery request')
    first.quit()
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
        if sys.exc_info()[0] is None:
            first.quit()
        else:
            first.close()
    if second:
        if sys.exc_info()[0] is None:
            second.quit()
        else:
            second.close()
print('Production SDK Play: XY queries, live controls/authority, copied progress, return journey and clean authored restart passed')
