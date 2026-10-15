"""Ordinary installed C++ object values, true recovery and transient live edits."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
from physics_velocity_example_test import Worker

runtime, gallery, module, scratch = (Path(x).resolve() for x in sys.argv[1:5])
root = scratch / str(uuid.uuid4())
shutil.copytree(gallery, root)
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(version=2, name='Object material SDK',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[dict(
    id='project.material_value_probe', implementation='1', sdk='experimental-1',
    fingerprint=fingerprint, library=module.name, dependencies=[])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
source_path = root / 'Scenes/66-object-material-values.scene.json'
authored_hash = hashlib.sha256(source_path.read_bytes()).hexdigest()
scene = json.loads(source_path.read_text())
actor, = [r for r in scene['entities'] if r['name'] == 'Red - polished']
actor['components']['project.material_value_probe'] = dict(ticks=0, **{'$forge': stamp})
for value in actor['components']['forge.mesh_renderer']['material_parameters']:
    value['future_owner'] = value['slot'] + '/' + value['key']
    value['parameter']['future_value'] = value['key']
initial = copy.deepcopy(actor['components']['forge.mesh_renderer']['material_parameters'])

def row(snapshot):
    return next(r for r in snapshot['effective_scene']['entities'] if r['id'] == actor['id'])

def values(snapshot):
    return row(snapshot)['components']['forge.mesh_renderer']['material_parameters']

def known_values(items):
    return [dict(slot=item['slot'], key=item['key'], parameter=dict(
        type=item['parameter']['type'], value=item['parameter']['value'])) for item in items]

def check_values(snapshot, tick):
    current = values(snapshot)
    by_key = {v['key']: v for v in current}
    assert row(snapshot)['components']['project.material_value_probe']['ticks'] == tick
    assert len(by_key) == len(current) == (2 if tick < 15 else 1), (tick, current)
    if tick < 15:
        expected = .12 if tick < 5 else .2
        assert abs(by_key['roughnessFactor']['parameter']['value'][0] - expected) < 2e-6
    expected = [.9, .03, .02, 1] if tick < 10 else [.03, .8, .1, 1]
    assert all(abs(a-b)<2e-6 for a,b in zip(by_key['baseColorFactor']['parameter']['value'], expected))
    for value in current:
        assert value['future_owner'] == value['slot'] + '/' + value['key'], (tick, value)
        assert value['parameter']['future_value'] == value['key'], (tick, value)
    if 12 <= tick < 15:
        assert current[0]['key'] == 'roughnessFactor', current

first = Worker(runtime, root)
others = []
try:
    snapshot = first.request('replace', scene=scene)
    for tick in range(1, 26):
        snapshot = first.request('step')
        check_values(snapshot, tick)
        for other in others:
            recovered = other.request('step')
            check_values(recovered, tick)
            assert values(recovered) == values(snapshot)
        if tick in (4, 8, 12, 18, 23):
            checkpoint = snapshot['recovery']
            assert checkpoint, snapshot
            other = Worker(runtime, root)
            recovered = other.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            check_values(recovered, tick)
            others.append(other)
    # Live edits use the same narrow, generation-checked built-in property path
    # in a legacy worker as in SDK Play. No fixed step is needed to tune a value.
    live = copy.deepcopy(values(snapshot))
    live[0]['parameter']['value'] = [.03, .1, .8, 1]
    target = dict(generation=snapshot['world_generation'], scene_asset=scene['asset_id'],
        entity=actor['id'], type='forge.mesh_renderer', property='material_parameters', value=live)
    tuned = first.request('property_tune', **target)
    saved = first.request('snapshot')
    assert saved['timing']['tick'] == snapshot['timing']['tick']
    # The native acknowledgement intentionally projects known value fields;
    # the Scene snapshot also retains unknown extension data by slot/key.
    assert known_values(values(saved)) == known_values(tuned['property_tune']['value'])
    assert values(saved)[0]['future_owner'] == 'surface/baseColorFactor'
    assert abs(values(saved)[0]['parameter']['value'][2] - .8) < 2e-6
    for changes in (dict(property='mesh', value=None), dict(generation=target['generation']+1),
                    dict(value=[dict(slot='bad/slot', key='baseColorFactor',
                                     parameter=dict(type=5, value=[1, 1, 1, 1]))])):
        try:
            first.request('property_tune', **dict(target, **changes))
        except AssertionError as error:
            assert isinstance(error.args[0], dict) and not error.args[0]['ok'], error
        else:
            raise AssertionError('Invalid/stale/unrelated live material change was admitted')
        assert values(first.request('snapshot')) == values(saved)
    # Exercise the actual long-key collection across the process boundary, not
    # only a parser fixture. Resource-domain admission remains the renderer's
    # separate responsibility; these intentionally have no selected declaration.
    bounded = [dict(slot='slot_'+'s'*245, key='parameter_'+str(i)+'_'+'p'*235,
                    parameter=dict(type=0, value=[.5, 0, 0, 0])) for i in range(64)]
    assert 8192 < len(json.dumps(bounded)) < 64*1024
    large = first.request('property_tune', **dict(target, value=bounded))
    assert len(large['property_tune']['value']) == 64
    before_limit = values(first.request('snapshot'))
    try:
        first.request('property_tune', **dict(target, value=bounded+[dict(
            slot='another-slot', key='another-key', parameter=dict(type=0, value=[.5, 0, 0, 0]))]))
    except AssertionError as error:
        assert isinstance(error.args[0], dict) and not error.args[0]['ok'], error
    else:
        raise AssertionError('A65th material parameter passed runtime native admission')
    assert values(first.request('snapshot')) == before_limit
    restarted = first.request('replace', scene=scene)
    reset = {v['key']: v['parameter'] for v in values(restarted)}
    assert all(reset[v['key']] == v['parameter'] for v in initial)
    assert hashlib.sha256(source_path.read_bytes()).hexdigest() == authored_hash
finally:
    first.close()
    for other in others:
        other.close()
print('Installed C++ material edits/reorder/revert, composite extensions, five cold recoveries, '
      'live narrow/generation admission, authored isolation and fresh-Play reset passed')
