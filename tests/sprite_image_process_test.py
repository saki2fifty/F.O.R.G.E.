"""Installed-only C++ Sprite values through the shared Flecs and cold worker boundary."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
from physics_velocity_example_test import Worker
from movement_2d_example_test import activate
from runtime_client import Runtime

runtime, module, scratch = (Path(x).resolve() for x in sys.argv[1:4])
root = scratch / str(uuid.uuid4())
root.mkdir(parents=True)
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(
    version=2, name='Installed Sprite SDK', startup_scene=None,
    game=dict(version=1, application_id='org.forge.sprite-image-test.' + root.name,
              title='Sprite image SDK', profile='development', save_schema=1,
              display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
              audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
    input=dict(version=1, actions=[]), modules=[dict(
        id='project.sprite_image_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=[])])),
    encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
first = Worker(runtime, root)
restored = None


def region(image, x, y, width=3, height=3, enabled=True):
    assert image['region_enabled'] is enabled, image
    assert [image['region_' + field] for field in ('x', 'y', 'width', 'height')] == [
        x, y, width, height], image


def values(state):
    assert not any(d['severity'].lower() in ('error', 'fatal') for d in state['diagnostics']), state['diagnostics']
    row, = [r for r in state['effective_scene']['entities'] if r['id'] == actor]
    return row['components']


def live_region(worker, state):
    generation = (state['world_generation'] if 'world_generation' in state
                  else state['activation']['generation'])
    target = dict(generation=generation, scene_asset=state['effective_scene']['asset_id'],
                  entity=actor, type='forge.sprite_renderer')
    for property, value in (('region_enabled', False), ('region_x', 1), ('region_y', 0),
                            ('region_width', 2), ('region_height', 3), ('region_enabled', True)):
        changed = worker.request('property_tune', **target, property=property, value=value)
        assert changed['property_tune']['value'] == value, changed
    tuned = worker.request('snapshot')
    assert tuned['timing']['tick'] == state['timing']['tick'], tuned['timing']
    region(values(tuned)['forge.sprite_renderer'], 1, 0, 2, 3)
    for changes in (dict(property='region_width', value=-1),
                    dict(property='region_y', value=16385),
                    dict(property='region_enabled', value=1),
                    dict(property='region_x', value=.5),
                    dict(property='region_height', value=2, generation=target['generation']+1),
                    dict(property='region_x', value=0, scene_asset=str(uuid.uuid4()))):
        try:
            worker.request('property_tune', **dict(target, **changes))
        except (AssertionError, RuntimeError):
            pass
        else:
            raise AssertionError(('Invalid live Sprite region accepted', changes))
        # Rejected commands intentionally remain in the bounded diagnostic log.
        # Compare the complete copied Scene instead of requiring a clean log.
        unchanged = worker.request('snapshot')
        assert unchanged['effective_scene'] == tuned['effective_scene']
        assert unchanged['timing']['tick'] == tuned['timing']['tick']


try:
    schema = first.request('snapshot')['schema']
    def defaults(name):
        component, = [row for row in schema['components'] if row['id'] == name]
        return {field['id']: field['default'] for field in component['fields']}
    image = defaults('forge.sprite_renderer')
    image.update(width=2, height=2, future_sprite_extension=dict(keep=17))
    actor = str(uuid.uuid4())
    scene = dict(version=3, asset_id=str(uuid.uuid4()), entities=[dict(
        id=actor, name='SDK Sprite values', components={
            'forge.local_translation': defaults('forge.local_translation'),
            'forge.local_rotation': defaults('forge.local_rotation'),
            'forge.local_scale': defaults('forge.local_scale'),
            'forge.sprite_renderer': image,
            'project.sprite_image_probe': dict(ticks=0, **{'$forge': stamp})})])
    source = root / 'Source.scene.json'
    source.write_text(json.dumps(scene), encoding='utf-8')
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    original = copy.deepcopy(scene)
    initial = first.request('replace', scene=scene)
    assert values(initial)['forge.sprite_renderer']['image_mode'] == 0
    region(values(initial)['forge.sprite_renderer'], 0, 0, 0, 0, False)
    one = first.request('step')
    mapped = values(one)
    assert mapped['project.sprite_image_probe']['ticks'] == 1
    image = mapped['forge.sprite_renderer']
    assert image['image_mode'] == 1 and image['pixels_per_meter'] == 4, image
    assert all(image[name] == 1 for name in ('border_left', 'border_right', 'border_top', 'border_bottom')), image
    assert image['future_sprite_extension'] == dict(keep=17), image
    region(image, 1, 0)
    checkpoint = one['recovery']
    assert checkpoint is not None
    restored = Worker(runtime, root)
    replay = restored.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                              recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
    assert values(replay) == mapped
    for worker in (first, restored):
        state = worker.request('step')
        updated = values(state)
        image = updated['forge.sprite_renderer']
        assert updated['project.sprite_image_probe']['ticks'] == 2
        assert image['image_mode'] == 2 and image['pixels_per_meter'] == 6 and image['flip_x'], image
        assert image['future_sprite_extension'] == dict(keep=17), image
        assert image['width'] == 2 and image['height'] == 2, image
        region(image, 0, 1)
    live_region(first, first.request('snapshot'))
    assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == digest
finally:
    first.close()
    if restored is not None:
        restored.close()

# Exercise the other real host's candidate handoff and live property consumer,
# with the same installed-only C++ component layout and fixed-system writes.
play = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'users'])
try:
    initial = activate(play, scene, True)
    region(values(initial)['forge.sprite_renderer'], 0, 0, 0, 0, False)
    for tick, x, y in ((1, 1, 0), (2, 0, 1)):
        state = play.request('step')
        mapped = values(state)
        assert mapped['project.sprite_image_probe']['ticks'] == tick
        image = mapped['forge.sprite_renderer']
        assert image['image_mode'] == tick and image['pixels_per_meter'] == 2 + 2 * tick, image
        assert image['future_sprite_extension'] == dict(keep=17), image
        region(image, x, y)
    live_region(play, state)
    assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == digest
finally:
    play.close()
print('Installed C++ Sprite fields, both hosts, live regions, fixed writes, extensions, cold recovery and source isolation PASS')
