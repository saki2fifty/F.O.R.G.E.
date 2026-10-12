"""Actual exact C++ consumer, pre-physics kinematic target, and worker recovery."""
import json
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
prepared = subprocess.run([fixture, runtime.parent/'root-sdk-data', worker, converter,
                          source, '--prepare'], capture_output=True, text=True,
                          encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(module, root/module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2, name='Root motion SDK',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[dict(
        id='project.root_motion_probe', implementation='1', sdk='experimental-1',
        fingerprint=fingerprint, library=module.name,
        dependencies=['forge.animation', 'forge.input', 'forge.physics'])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
original = json.loads((root/'main.scene.json').read_text(encoding='utf-8'))
dt = struct.unpack('f', struct.pack('f', 1/60))[0]

def actor_row(state):
    row, = [row for row in state['effective_scene']['entities']
            if 'forge.animator' in row['components']]
    return row

def ready(worker, scene):
    initial = worker.request('replace', scene=scene)
    deadline = time.monotonic()+15
    while initial['recovery'] is None or not actor_row(initial).get('animation_pose'):
        assert time.monotonic() < deadline, initial
        time.sleep(.002)
        initial = worker.request('snapshot')
    assert initial['timing']['tick'] == 0
    return initial

def compare(left, right):
    lc, rc = actor_row(left)['components'], actor_row(right)['components']
    for component in ('forge.local_translation', 'forge.local_rotation', 'project.root_probe'):
        for name, value in lc[component].items():
            other = rc[component][name]
            if isinstance(value, (float, int)) and not isinstance(value, bool):
                assert abs(value-other) < 3e-6, (component, name, value, other)
            else:
                assert value == other, (component, name, value, other)

try:
    for mode, drive in [(1, False), (2, True)]:
        scene = json.loads(json.dumps(original))
        actor, = [row for row in scene['entities'] if 'forge.animator' in row['components']]
        actor['components']['forge.animator']['root_motion'] = mode
        actor['components']['project.root_probe'] = dict(
            ticks=0, applied=0, x=0, z=0, drive_body=drive, **{'$forge': stamp})
        if drive:
            actor['components']['forge.physics_body'] = dict(enabled=True, motion=1,
                mass=0, friction=.5, restitution=0, gravity_factor=0, layer=0,
                mask=4294967295, sensor=False, continuous_collision=False, density=1000)
            actor['components']['forge.box_collider'] = dict(x=.7, y=2, z=.4)
        first, recovered = Worker(runtime, root), []
        try:
            ready(first, scene)
            for tick in range(1, 101):
                current = first.request('step')
                components = actor_row(current)['components']
                probe = components['project.root_probe']
                expected = 2*dt*tick
                assert probe['ticks'] == tick and probe['applied'] == (mode == 1), probe
                assert abs(probe['z']-expected) < .003, (tick, probe)
                assert abs(components['forge.local_translation']['z']-expected) < .003, (
                    tick, components['forge.local_translation'], expected)
                pose = actor_row(current)['animation_pose']
                assert abs(pose['local'][0]['translation'][2]) < .0001, pose
                for restored in recovered:
                    compare(current, restored.request('step'))
                if tick in (27, 59, 61):
                    checkpoint = current['recovery']
                    assert checkpoint is not None, current
                    fresh = Worker(runtime, root)
                    restored = fresh.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    compare(current, restored)
                    recovered.append(fresh)
        finally:
            first.close()
            for restored in recovered:
                restored.close()
    # Actual solver ownership: a Dynamic actor cannot accept Apply pose writes.
    scene = json.loads(json.dumps(original))
    actor, = [row for row in scene['entities'] if 'forge.animator' in row['components']]
    actor['spatial'] = dict(mode='world')
    actor['components']['forge.physics_body'] = dict(enabled=True, motion=2,
        mass=1, friction=.5, restitution=0, gravity_factor=0, layer=0,
        mask=4294967295, sensor=False, continuous_collision=False, density=1000)
    actor['components']['forge.box_collider'] = dict(x=.7, y=2, z=.4)
    dynamic = Worker(runtime, root)
    try:
        ready(dynamic, scene)
        current = dynamic.request('step')
        assert abs(actor_row(current)['components']['forge.local_translation']['z']) < .0001
        assert current['recovery'] is None, 'Solver-owned root failure was checkpointable'
    finally:
        dynamic.close()
    print('Exact C++ PostAnimation delta, queued kinematic movement, dynamic ownership, loop crossing and six fresh-worker recoveries passed')
finally:
    shutil.rmtree(root)
