"""Installed public probe layout through ordinary and SDK Play hosts and cold recovery."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
from runtime_client import Runtime, activate

runtime, module, scratch = (Path(value).resolve() for value in sys.argv[1:4])
root = scratch / str(uuid.uuid4())
root.mkdir(parents=True)
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(
    version=2, name='Installed Reflection Probe SDK', startup_scene=None,
    game=dict(version=1, application_id='org.forge.reflection-probes.' + root.name,
              title='Reflection Probe SDK', profile='development', save_schema=1,
              display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
              audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
    input=dict(version=1, actions=[]), modules=[dict(
        id='project.reflection_probe_example', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=[])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
actor = str(uuid.uuid4())
first = Runtime(runtime, ['--sdk-project', root])
restored = None

def values(state, check_diagnostics=True):
    assert not check_diagnostics or not any(value['severity'].lower() in ('error', 'fatal') for value in state['diagnostics']), state['diagnostics']
    entity, = [row for row in state['effective_scene']['entities'] if row['id'] == actor]
    return entity['components']

def verify(state, tick):
    components = values(state)
    assert components['project.probe_control']['ticks'] == tick
    probe = components['forge.reflection_probe']
    assert probe['texture'] == 'd4c60053-60c8-4456-8771-a7cf8182a902'
    assert probe['intensity'] == 1 + .5 * tick and probe['rotation_degrees'] == 15 * tick
    assert probe['half_extent_x'] == 2 + .25 * tick and probe['priority'] == tick
    assert probe['box_projection'] is (tick != 2)
    assert probe['extension'] == dict(retain=17)

def live(client, state):
    generation = state['world_generation'] if 'world_generation' in state else state['activation']['generation']
    target = dict(generation=generation, scene_asset=state['effective_scene']['asset_id'],
                  entity=actor, type='forge.reflection_probe')
    for name, value in [('half_extent_x', .0001), ('priority', -2147483648), ('enabled', False)]:
        updated = client.request('property_tune', **target, property=name, value=value)
        assert updated['ok'] and updated['property_tune']['value'] == value, updated
    before = client.request('snapshot')
    for name, value in [('half_extent_x', .000099999999), ('priority', 2147483648), ('enabled', 1)]:
        try:
            client.request('property_tune', **target, property=name, value=value)
        except RuntimeError as error:
            assert name in str(error), 'Missing native field validation error: ' + str(error)
        else:
            raise AssertionError('Invalid live probe value accepted: ' + name)
        unchanged = client.request('snapshot')
        assert values(unchanged, False) == values(before) and unchanged['timing']['tick'] == before['timing']['tick']
        if unchanged['diagnostics'] != before['diagnostics']:
            assert unchanged['diagnostics'][-1]['severity'].lower() == 'error'
            assert 'forge.reflection_probe' in unchanged['diagnostics'][-1]['text']

try:
    schema = first.request('snapshot')['schema']
    def defaults(name):
        component, = [item for item in schema['components'] if item['id'] == name]
        return {field['id']: field['default'] for field in component['fields']}
    probe = defaults('forge.reflection_probe')
    probe.update(texture='d4c60053-60c8-4456-8771-a7cf8182a902', extension=dict(retain=17))
    scene = dict(version=3, asset_id=str(uuid.uuid4()), entities=[dict(
        id=actor, name='SDK local reflection volume', components={
            'forge.local_translation': defaults('forge.local_translation'),
            'forge.local_rotation': defaults('forge.local_rotation'),
            'forge.local_scale': defaults('forge.local_scale'), 'forge.reflection_probe': probe,
            'project.probe_control': dict(ticks=0, **{'$forge': stamp})})])
    original = copy.deepcopy(scene)
    source = root / 'Source.scene.json'
    source.write_text(json.dumps(scene), encoding='utf-8')
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert first.request('replace', scene=scene)['ok']
    one = first.request('step')
    verify(one, 1)
    checkpoint = one['recovery']
    restored = Runtime(runtime, ['--sdk-project', root])
    replay = restored.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                              recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
    assert replay['ok'] and values(replay) == values(one), replay
    for client in (first, restored):
        for tick in (2, 3):
            verify(client.request('step'), tick)
        live(client, client.request('snapshot'))
finally:
    first.close()
    if restored is not None:
        restored.close()

play = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'users'])
try:
    activate(play, scene, True)
    for tick in (1, 2, 3):
        verify(play.request('step'), tick)
    live(play, play.request('snapshot'))
finally:
    play.close()
assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == digest
print('Installed C++ probe layout, fixed writes, both hosts, live validation, extensions and cold recovery PASS')
