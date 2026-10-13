"""Ordinary C++ numeric blend positions across real fixed ticks and cold workers."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker

runtime, fixture, gallery, module = (Path(x).resolve() for x in sys.argv[1:5])
prepared = subprocess.run([fixture, gallery, runtime.parent / 'blend-space-sdk-data', '--prepare'],
                          capture_output=True, text=True, encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(version=2, name='Blend Space SDK', startup_scene=None,
    input=dict(version=1, actions=[]), modules=[dict(id='project.animation_blend_space_probe', implementation='1',
    sdk='experimental-1', fingerprint=fingerprint, library=module.name, dependencies=['forge.animation'])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
scene = json.loads((root / 'main.scene.json').read_text(encoding='utf-8'))
actor, = [r for r in scene['entities'] if 'forge.animator' in r['components']]
actor['components']['project.blend_space_probe'] = dict(ticks=0, **{'$forge': stamp})

def row(snapshot):
    return next(r for r in snapshot['effective_scene']['entities'] if 'forge.animator' in r['components'])

def admitted(worker, snapshot):
    deadline = time.monotonic() + 15
    while snapshot['recovery'] is None or not row(snapshot).get('animation_pose'):
        assert time.monotonic() < deadline, snapshot
        time.sleep(.002)
        snapshot = worker.request('snapshot')
    return snapshot

dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]
first = Worker(runtime, root)
restored = []
try:
    snapshot = admitted(first, first.request('replace', scene=scene))
    for tick in range(1, 31):
        snapshot = admitted(first, first.request('step'))
        current = row(snapshot)
        pose = current['animation_pose']
        seconds = tick * dt
        position = 1.5 if tick < 5 else 0 if tick < 10 else .5 if tick < 15 else 2 if tick < 20 else 3 if tick < 25 else -1
        if tick == 11:
            position = .75
        if tick == 12:
            position = .25
        assert current['components']['project.blend_space_probe']['ticks'] == tick
        assert current['components']['forge.animator']['blend_position'] == position
        assert abs(pose['time'] - seconds) < 3e-6 and pose['duration'] == 2, pose
        base, second = ((.25 + .25*seconds, -.5*seconds), (.25 + 1.25*seconds, .25*seconds))
        expected = tuple((base[i]+second[i])*.5 for i in (0, 1)) if position == 1.5 else \
                   tuple((.25, 0)[i]*(1-position)+base[i]*position for i in (0, 1)) if 0 < position < 1 else \
                   second if position == 2 else base if position == 3 else (.25, 0)
        assert all(abs(pose['morphs'][0]['weights'][i] - expected[i]) < 3e-6 for i in (0, 1)), (tick, pose, expected)
        written = next(r for r in snapshot['effective_scene']['entities'] if
                       r['components'].get('forge.model_source', {}).get('node') == pose['joint_assets'][0])
        tr = written['components']['forge.local_translation']
        x = seconds * (.5 if position == 1.5 else 1 if position == 2 else 0)
        z = seconds * (.5 if position == 1.5 else position if 0 < position < 1 else 1 if position == 3 else 0)
        assert abs(tr['x'] - x) < .003 and abs(tr['z'] - z) < .003, (tick, tr, x, z)
        for other in restored:
            recovered = admitted(other, other.request('step'))
            assert row(recovered)['animation_pose'] == pose, (tick, row(recovered), current)
        if tick in (4, 8, 12, 18, 23):
            checkpoint = snapshot['recovery']
            other = Worker(runtime, root)
            recovered = other.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                      recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            assert row(recovered)['animation_pose'] == pose
            restored.append(other)
finally:
    first.close()
    for other in restored:
        other.close()
print('Actual C++ Blend Space edits preserve shared cycle, written root/morph values and five fresh-worker recoveries')
