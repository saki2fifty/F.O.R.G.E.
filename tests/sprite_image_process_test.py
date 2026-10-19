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

runtime, module, scratch = (Path(x).resolve() for x in sys.argv[1:4])
root = scratch / str(uuid.uuid4())
root.mkdir(parents=True)
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(
    version=2, name='Installed Sprite SDK', startup_scene=None,
    input=dict(version=1, actions=[]), modules=[dict(
        id='project.sprite_image_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=[])])),
    encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
first = Worker(runtime, root)
restored = None
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
    def values(state):
        assert not any(d['severity'].lower() in ('error', 'fatal') for d in state['diagnostics']), state['diagnostics']
        row, = [r for r in state['effective_scene']['entities'] if r['id'] == actor]
        return row['components']
    assert values(initial)['forge.sprite_renderer']['image_mode'] == 0
    one = first.request('step')
    mapped = values(one)
    assert mapped['project.sprite_image_probe']['ticks'] == 1
    image = mapped['forge.sprite_renderer']
    assert image['image_mode'] == 1 and image['pixels_per_meter'] == 4, image
    assert all(image[name] == 1 for name in ('border_left', 'border_right', 'border_top', 'border_bottom')), image
    assert image['future_sprite_extension'] == dict(keep=17), image
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
    assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == digest
finally:
    first.close()
    if restored is not None:
        restored.close()
print('Installed C++ Sprite fields, fixed-system writes, extensions, cold recovery and source isolation PASS')
