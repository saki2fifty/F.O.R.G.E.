"""Installed projected-surface component layout, live admission and both Play hosts."""
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
    version=2, name='Installed Decal SDK', startup_scene=None,
    game=dict(version=1, application_id='org.forge.decals.' + root.name,
              title='Decal SDK', profile='development', save_schema=1,
              display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
              audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
    input=dict(version=1, actions=[]), modules=[dict(
        id='project.decal_example', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=[])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
actor = str(uuid.uuid4())
material_id = '9d087798-a173-48e0-938d-7607eed42f91'
first = Runtime(runtime, ['--sdk-project', root])
restored = None

def values(state):
    entity, = [row for row in state['effective_scene']['entities'] if row['id'] == actor]
    return entity['components']

def verify(state, tick):
    assert not any(d['severity'].lower() in ('error', 'fatal') for d in state['diagnostics']), state['diagnostics']
    components = values(state)
    assert components['project.decal_control']['ticks'] == tick
    decal = components['forge.decal']
    expected = dict(material=material_id, enabled=tick != 2, width=2 + .25*tick,
                    height=3 + .5*tick, depth=4 + .25*tick, tint_r=.125*tick,
                    tint_g=.25, tint_b=.5, opacity=.25*tick, angle_fade_start=.25*tick,
                    angle_fade_end=1.25, layers=1 << tick, priority=-tick,
                    color_influence=1 - .25*tick, normal_influence=.25*tick,
                    roughness_influence=.125*tick, extension=dict(retain=17))
    assert decal == expected, (decal, expected)

def live(client, state):
    generation = state['world_generation'] if 'world_generation' in state else state['activation']['generation']
    target = dict(generation=generation, scene_asset=state['effective_scene']['asset_id'],
                  entity=actor, type='forge.decal')
    for name, value in [('width', .125), ('opacity', .5), ('layers', 3), ('priority', -2147483648),
                        ('color_influence', .25), ('normal_influence', .5), ('roughness_influence', .75)]:
        result = client.request('property_tune', **target, property=name, value=value)
        assert result['ok'] and result['property_tune']['value'] == value, result
    before = client.request('snapshot')
    for name, value in [('width', 0), ('opacity', 1.01), ('enabled', 1),
                         ('priority', 2147483648), ('angle_fade_end', .5), ('material', None),
                         ('color_influence', -.01), ('normal_influence', 1.01), ('roughness_influence', True)]:
        try:
            client.request('property_tune', **target, property=name, value=value)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Invalid live decal value accepted: ' + name)
        after = client.request('snapshot')
        assert values(after) == values(before) and after['timing']['tick'] == before['timing']['tick']

try:
    schema = first.request('snapshot')['schema']
    def defaults(name):
        component, = [item for item in schema['components'] if item['id'] == name]
        return {field['id']: field['default'] for field in component['fields']}
    decal = defaults('forge.decal')
    assert len(decal) == 16
    decal.update(material=material_id, extension=dict(retain=17))
    scene = dict(version=3, asset_id=str(uuid.uuid4()), entities=[dict(
        id=actor, name='SDK projected surface', components={
            'forge.local_translation': defaults('forge.local_translation'),
            'forge.local_rotation': defaults('forge.local_rotation'),
            'forge.local_scale': defaults('forge.local_scale'), 'forge.decal': decal,
            'project.decal_control': dict(ticks=0, **{'$forge': stamp})})])
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
        for tick in (2, 3): verify(client.request('step'), tick)
        live(client, client.request('snapshot'))
finally:
    first.close()
    if restored is not None: restored.close()

play = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'users'])
try:
    activate(play, scene, True)
    for tick in (1, 2, 3): verify(play.request('step'), tick)
    live(play, play.request('snapshot'))
finally:
    play.close()
assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == digest
print('Installed C++ decal layout, all fields, both hosts, live validation, extensions and cold recovery PASS')
