"""Compiled C++ observes timed exits, markers and independent fresh-worker recovery."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker

runtime, fixture, converter, source, module = (
    Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5, 6))
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'exits-sdk-data', worker, converter,
                          source, '--exits-prepare'], capture_output=True, text=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(module, root/module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2, name='Timed exits SDK',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[dict(
        id='project.animation_exit_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=['forge.animation', 'forge.input'])])))
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
scene = json.loads((root/'main.scene.json').read_text())
actor, = [row for row in scene['entities'] if 'forge.animator' in row['components']]
actor['components']['forge.animator']['state'] = 1
actor['components']['project.exit_probe'] = dict(ticks=0, state=0, marker_bits=0,
    marker_state=0, return_tick=60, time=0, weight=1, **{'$forge': stamp})
dt = struct.unpack('f', struct.pack('f', 1/60))[0]
first, recovered = Worker(runtime, root), []

def values(result):
    row, = [row for row in result['effective_scene']['entities'] if row['id'] == actor['id']]
    return row.get('animation_pose'), row['components']['project.exit_probe'], row['components']['forge.animator']

def compare(left, right):
    lp, lo, la = values(left)
    rp, ro, ra = values(right)
    assert lo == ro and la == ra, (lo, ro, la, ra)
    assert lp['state'] == rp['state'] and abs(lp['time']-rp['time']) < 1e-7
    for a, b in zip(lp['local'], rp['local']):
        for field in ('translation', 'rotation', 'scale'):
            assert all(abs(x-y) < 2e-6 for x, y in zip(a[field], b[field])), (a, b)

try:
    initial = first.request('replace', scene=scene)
    deadline = time.monotonic()+15
    while initial['recovery'] is None or not values(initial)[0]:
        assert time.monotonic() < deadline, initial
        time.sleep(.002)
        initial = first.request('snapshot')
    assert initial['timing']['tick'] == 0 and initial['timing']['paused']
    gate = math.ceil(.25*values(initial)[0]['duration']/dt)
    assert gate == 30, gate
    for tick in range(1, 91):
        current = first.request('step')
        pose, probe, animator = values(current)
        expected_state = 1 if tick < gate or 60 <= tick < 60+gate-1 else 2
        assert probe['ticks'] == tick and probe['state'] == pose['state'] == animator['state'] == expected_state, (tick, probe, pose)
        if tick in (gate, 60+gate-1):
            assert pose['time'] == probe['time'] == 0 and probe['weight'] == 0, (tick, probe)
            assert probe['marker_state'] == 1, 'Final source markers lost their originating state'
        if tick == gate+1:
            assert probe['marker_state'] == 2 and probe['marker_bits'] & (1 << 5)
            assert abs(probe['time']-dt) < 1e-7 and abs(probe['weight']-dt/.4) < 1e-6
        if tick == 60:
            assert abs(probe['time']-dt) < 1e-7, 'C++ return to initial state did not restart incoming clock'
        for restored in recovered:
            compare(current, restored.request('step'))
        if tick in (gate-1, gate, gate+16):
            checkpoint = current['recovery']
            assert checkpoint is not None
            fresh = Worker(runtime, root)
            restored = fresh.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            compare(current, restored)
            recovered.append(fresh)
    print('Installed/public C++ timed exit, final-source/next-entry markers, manual return and three fresh-worker recoveries passed')
finally:
    first.close()
    for restored in recovered:
        restored.close()
    shutil.rmtree(root)
