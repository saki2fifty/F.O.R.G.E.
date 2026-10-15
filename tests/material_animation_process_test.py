"""Real C++ material animation control, native values and cold-worker recovery."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import uuid
from physics_velocity_example_test import Worker

runtime, gallery, module, scratch = (Path(x).resolve() for x in sys.argv[1:5])
root = scratch / str(uuid.uuid4())
shutil.copytree(gallery, root)
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(version=2, name='Material animation SDK',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[dict(
        id='project.material_animation_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=['forge.animation'])])),
    encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
source = root / 'Scenes/67-animated-material-values.scene.json'
source_digest = hashlib.sha256(source.read_bytes()).hexdigest()
scene = json.loads(source.read_text(encoding='utf-8'))
# The worker consumes the editor's complete Scene snapshot, including its
# retained Prefab definitions, rather than loading a source locator itself.
scene['_prefab_sources'] = [json.loads(
    (root / 'Assets/ObjectValues/SharedCube.prefab.json').read_text(encoding='utf-8'))]
actor, = [r for r in scene['entities'] if r['name'] == 'Linear color and roughness']
actor['components']['project.material_animation_probe'] = dict(ticks=0, time=0, roughness=0,
                                                              **{'$forge': stamp})
actor['components']['forge.mesh_renderer']['material_parameters'] = [dict(
    slot='surface', key='roughnessFactor', future_owner='keep',
    parameter=dict(type=0, value=[.9, 0, 0, 0], future_value=True))]
original = copy.deepcopy(scene)
dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]

def row(snapshot):
    return next(r for r in snapshot['effective_scene']['entities'] if r['id'] == actor['id'])

def check_state(snapshot, tick):
    assert not any(d['severity'].lower() in ('error', 'fatal') for d in snapshot['diagnostics']), snapshot['diagnostics']
    probe = row(snapshot)['components']['project.material_animation_probe']
    expected_time = .5 if tick == 1 else (1 + (tick - 1) * dt * .5 if tick < 8 else (tick - 7) * dt * .5)
    expected_roughness = .15 + .7 * expected_time / 2
    assert probe['ticks'] == tick and abs(probe['time'] - expected_time) < 3e-6, (tick, probe, expected_time)
    assert abs(probe['roughness'] - expected_roughness) < 3e-6, (tick, probe, expected_roughness)
    parameters = row(snapshot)['components']['forge.mesh_renderer']['material_parameters']
    value, = [v for v in parameters if v['key'] == 'roughnessFactor']
    assert value['future_owner'] == 'keep' and value['parameter']['future_value'] is True, value
    assert abs(value['parameter']['value'][0] - expected_roughness) < 3e-6, (tick, value)

first = Worker(runtime, root)
restored = []
try:
    initial = first.request('replace', scene=scene)
    deadline = time.monotonic() + 20
    reference = dict(scene=scene['asset_id'], entity=actor['id'])
    while not initial.get('animation_playback', {}).get('available', False):
        assert not any(d['severity'].lower() in ('error', 'fatal') for d in initial['diagnostics']), initial['diagnostics']
        assert time.monotonic() < deadline, dict(diagnostics=initial['diagnostics'], timing=initial['timing'])
        time.sleep(.002)
        initial = first.request('snapshot', animation_watch=reference)
    # Initial transform players are discontinuous until their first complete
    # fixed boundary. Readiness and recoverability intentionally differ.
    assert initial['recovery'] is None
    for tick in range(1, 33):
        state = first.request('step')
        check_state(state, tick)
        for fresh in restored:
            result = fresh.request('step')
            check_state(result, tick)
            assert row(result)['components'] == row(state)['components'], (tick, row(result), row(state))
        if tick in (1, 3, 7, 8, 24):
            checkpoint = state['recovery']
            assert checkpoint is not None
            fresh = Worker(runtime, root)
            result = fresh.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                   recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            check_state(result, tick)
            assert row(result)['components'] == row(state)['components']
            restored.append(fresh)
    assert scene == original and hashlib.sha256(source.read_bytes()).hexdigest() == source_digest
finally:
    first.close()
    for fresh in restored:
        fresh.close()
print('Actual C++ material animation pause/seek/speed/restart, native observation, extensions, '
      'five cold recoveries and authored isolation PASS')
