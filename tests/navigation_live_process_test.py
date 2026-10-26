"""Actual ordinary-runtime live agent controls, authority and fresh recovery."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from runtime_client import Runtime

runtime, prepare, worker = (Path(value).resolve() for value in sys.argv[1:4])
planar = '--planar' in sys.argv[4:]
root = Path(subprocess.check_output(
    [prepare, runtime.parent / 'navigation-live-process-data', worker,
     '--prepare-planar' if planar else '--prepare'], text=True, timeout=30).strip())
source = (root / 'scene.json').read_bytes()
catalog = (root / 'forge.assets.json').read_bytes()
scene = json.loads(source)
first = Runtime(runtime, ['--project', root])
second = None


def actor(state):
    return next(row for row in state['effective_scene']['entities'] if row['name'] == 'Agent')


def position(state):
    affine = actor(state)['world_affine']
    return tuple(affine[i] for i in (3, 7, 11))


try:
    state = first.request('replace', scene=scene)
    target = dict(generation=state['world_generation'], scene_asset=scene['asset_id'],
                  entity=actor(state)['id'], type='forge.navigation_agent')
    state = first.request('step')
    before = position(state)
    tick = state['timing']['tick']

    def tune(field, value):
        result = first.request('property_tune', **target, property=field, value=value)
        assert result['property_tune']['value'] == value, result
        assert result['timing']['tick'] == tick and result['timing']['paused'], result
        return result

    tune('speed', 0)
    state = first.request('step')
    tick = state['timing']['tick']
    assert position(state) == before and actor(state)['navigation_debug']['phase'] == 'Paused'
    tune('speed', 3)
    state = first.request('step')
    tick = state['timing']['tick']
    assert position(state) != before and actor(state)['navigation_debug']['phase'] == 'Moving'
    tune('has_destination', False)
    before = position(state)
    state = first.request('step')
    tick = state['timing']['tick']
    assert position(state) == before and actor(state)['navigation_debug']['phase'] == 'Idle'
    tune('destination_x', -7)
    tune('destination_y' if planar else 'destination_z', 4)
    tune('stopping_distance', .25)
    tune('has_destination', True)
    state = first.request('step')
    tick = state['timing']['tick']
    assert actor(state)['navigation_debug']['sampled']
    assert abs(actor(state)['navigation_debug']['projected_destination'][0] + 7) < .01
    if planar:
        assert position(state)[2] == 37.125 and actor(state)['navigation_debug']['velocity'][2] == 0
    tune('enabled', False)
    before = position(state)
    state = first.request('step')
    tick = state['timing']['tick']
    assert position(state) == before and actor(state)['navigation_debug']['phase'] == 'Disabled'
    tune('enabled', True)
    baseline = first.request('snapshot')
    for field, value in [('speed', -1), ('speed', 'fast'), ('enabled', 1), ('destination_x', 4091),
                         ('navmesh', None), ('horizon', 2)]:
        try:
            first.request('property_tune', **target, property=field, value=value)
        except RuntimeError:
            pass
        else:
            raise AssertionError(('Rejected field/value was accepted', field, value))
    for changed in [dict(target, generation=target['generation'] + 1),
                    dict(target, scene_asset=str(uuid.uuid4())),
                    dict(target, entity=str(uuid.uuid4()))]:
        try:
            first.request('property_tune', **changed, property='speed', value=4)
        except RuntimeError:
            pass
        else:
            raise AssertionError(('Stale/foreign target was accepted', changed))
    assert actor(first.request('snapshot')) == actor(baseline)
    state = first.request('step')
    checkpoint = state['recovery']
    assert checkpoint['navigation']['version'] == 2
    expected = first.request('step')
    second = Runtime(runtime, ['--project', root])
    restored = second.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                              recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
    assert actor(restored)['components']['forge.navigation_agent'] == actor(state)['components']['forge.navigation_agent']
    actual = second.request('step')
    assert all(abs(a-b) < .001 for a, b in zip(position(expected), position(actual)))
    assert (root / 'scene.json').read_bytes() == source
    assert (root / 'forge.assets.json').read_bytes() == catalog
    print(('XY' if planar else '3D') + ': live agent controls, pause/retarget, invalid/stale/foreign rejection, fresh recovery and source preservation passed')
finally:
    first.close()
    if second:
        second.close()
    shutil.rmtree(root)
