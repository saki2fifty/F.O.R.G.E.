"""Actual C++ seeking, independent movement equations and fresh-worker recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker

runtime, fixture, converter, source, module = (Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5, 6))
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'seek-sdk-data', worker, converter, source,
                           '--prepare'], capture_output=True, text=True, encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(module, root/module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2, name='Animation seek SDK',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[dict(
        id='project.animation_seek_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name, dependencies=['forge.animation', 'forge.input'])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
scene = json.loads((root/'main.scene.json').read_text(encoding='utf-8'))
actor, = [r for r in scene['entities'] if 'forge.animator' in r['components']]
actor['components']['project.seek_probe'] = dict(ticks=0, time=0, movement=0, **{'$forge': stamp})
dt = struct.unpack('f', struct.pack('f', 1/60))[0]
def row(state):
    return next(r for r in state['effective_scene']['entities'] if 'forge.animator' in r['components'])
def compare(a, b):
    for key in ('forge.local_translation', 'forge.local_rotation', 'project.seek_probe'):
        for name, value in row(a)['components'][key].items():
            other = row(b)['components'][key][name]
            if isinstance(value, (float, int)) and not isinstance(value, bool):
                assert abs(value-other) < 3e-6, (key, name, value, other)
            else: assert value == other, (key, name)
first = Worker(runtime, root)
restored = []
try:
    initial = first.request('replace', scene=scene)
    deadline = time.monotonic()+15
    while initial['recovery'] is None or not row(initial).get('animation_pose'):
        assert time.monotonic() < deadline, initial
        time.sleep(.002)
        initial = first.request('snapshot')
    for tick in range(1, 72):
        state = first.request('step')
        assert not any(d['severity'].lower() in ('error', 'fatal') for d in state['diagnostics']), state
        probe = row(state)['components']['project.seek_probe']
        expected_time = {1: .75, 2: .9+dt, 3: dt, 4: .25}.get(tick, ((tick-4)*dt) % 1)
        moved = (tick-1-(1 if tick >= 4 else 0))*2*dt
        assert probe['ticks'] == tick and abs(probe['time']-expected_time) < 3e-6, (tick, probe, expected_time)
        # Cooked Ozz translations use half-float storage. Match the independently
        # established root-motion geometry tolerance; clocks and recovery remain strict.
        assert abs(probe['movement']-moved) < .003, (tick, probe, moved)
        assert abs(row(state)['components']['forge.local_translation']['z']-moved) < .003, (tick, state)
        for fresh in restored: compare(state, fresh.request('step'))
        if tick in (1, 4, 5, 60):
            checkpoint = state['recovery']; assert checkpoint is not None
            fresh = Worker(runtime, root)
            result = fresh.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                   recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            compare(state, result); restored.append(fresh)
finally:
    first.close()
    for fresh in restored: fresh.close()
print('Actual C++ seek, pause/restart, loop boundary, stage/thread/bounds and four recoveries passed')
