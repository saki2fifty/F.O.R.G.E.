"""Canonical installed C++ example, sampled fields/rotation, both hosts and recovery."""
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import uuid
from runtime_client import Runtime, activate

runtime, module, example, scratch = (Path(x).resolve() for x in sys.argv[1:5])
root = scratch / str(uuid.uuid4())
shutil.copytree(example, root)
shutil.copy2(module, root / module.name)
settings = json.loads((root / 'forge.project.json').read_text())
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
settings['modules'] = [dict(id='project.gameplay', implementation='1', sdk='experimental-1',
                          fingerprint=fingerprint, library=module.name, dependencies=['forge.animation'])]
(root / 'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
inspection = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
declaration, = inspection['components']
capabilities = next(row for row in inspection['reference_schema']['components']
                    if row['id'] == declaration['id'])
assert all(field['animatable'] for field in capabilities['fields'])
assert next(field for field in declaration['structure']['fields']
            if field['id'] == 'enabled')['animatable'] is False
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
scene_path = root / settings['startup_scene']['source']
saved = scene_path.read_bytes()
scene = json.loads(saved)
actor, = [row for row in scene['entities'] if 'project.animated_spin' in row['components']]
assert actor['components']['project.animated_spin']['$forge'] == stamp
original = copy.deepcopy(scene)
clip = root / 'Assets/SpinSpeed.animation.json'
clip_digest = hashlib.sha256(clip.read_bytes()).hexdigest()
reference = dict(scene=scene['asset_id'], entity=actor['id'])
dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]


def sampled_speed(tick):
    seconds = (tick * dt) % 4
    fraction = (seconds if seconds < 2 else seconds - 2) / 2
    ease = 3 * fraction * fraction - 2 * fraction * fraction * fraction
    value = 90 + 180 * ease if seconds < 2 else 270 - 180 * ease
    return struct.unpack('f', struct.pack('f', value))[0]


def sampled_discrete(tick):
    seconds = (tick * dt) % 4
    return not (1 <= seconds < 2), 1 if seconds < 2 else -1


def values(state):
    assert not any(row['severity'].lower() in ('error', 'fatal')
                   for row in state['diagnostics']), state['diagnostics']
    return next(row['components'] for row in state['effective_scene']['entities']
                if row['id'] == actor['id'])


def ready(worker, sdk_play):
    state = activate(worker, scene, sdk_play)
    deadline = time.monotonic() + 20
    while not state.get('animation_playback', {}).get('available', False):
        values(state)
        assert time.monotonic() < deadline, state.get('diagnostics')
        state = worker.request('snapshot', animation_watch=reference)
        time.sleep(.002)
    return state


# Source-free runtime content retains Scene metadata and selected compiled bytes.
# Native source/build files and editable animation source are not consumed by Play.
shutil.rmtree(root / 'Native')
clip.unlink()
clip.with_name(clip.name + '.forge-import.json').unlink()
for sdk_play in (False, True):
    arguments = ['--sdk-project', root]
    if sdk_play:
        arguments += ['--sdk-play', 'on', '--user-data', root / 'users']
    worker = Runtime(runtime, arguments, cwd=root)
    cold = None
    try:
        ready(worker, sdk_play)
        angle = 0
        for tick in range(1, 271):
            state = worker.request('step')
            current = values(state)
            speed = sampled_speed(tick)
            assert abs(current['project.animated_spin']['speed'] - speed) < 3e-5, (tick, current)
            enabled, direction = sampled_discrete(tick)
            assert current['project.animated_spin']['enabled'] is enabled, (tick, current)
            assert current['project.animated_spin']['direction'] == direction, (tick, current)
            if enabled:
                angle += speed * direction * dt * math.pi / 180
            rotation = current['forge.local_rotation']
            assert abs(rotation['x']) < 1e-7 and abs(rotation['z']) < 1e-7, rotation
            assert abs(rotation['y'] - math.sin(angle / 2)) < 2e-5, (tick, rotation, angle)
            assert abs(rotation['w'] - math.cos(angle / 2)) < 2e-5, (tick, rotation, angle)
            if cold:
                replay = cold.request('step')
                assert values(replay)['project.animated_spin'] == current['project.animated_spin']
                assert values(replay)['forge.local_rotation'] == rotation
            if tick == 72 and not sdk_play:
                checkpoint = state['recovery']
                assert checkpoint is not None
                cold = Runtime(runtime, ['--sdk-project', root], cwd=root)
                replay = cold.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                      recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert values(replay)['project.animated_spin'] == current['project.animated_spin']
                assert values(replay)['forge.local_rotation'] == rotation
        assert scene == original and scene_path.read_bytes() == saved
    finally:
        worker.close()
        if cold:
            cold.close()
assert hashlib.sha256((example / 'Assets/SpinSpeed.animation.json').read_bytes()).hexdigest() == clip_digest
print('Canonical installed C++ field animation: 270 ticks per host, independent cubic/held/quaternion '
      'oracles, source-free selected clip, true cold recovery and authored isolation PASS')
