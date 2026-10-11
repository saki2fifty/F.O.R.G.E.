"""Real SDK volume copies, geometry and fresh-process native recovery."""
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from physics_velocity_example_test import Worker

runtime, module = (Path(p).resolve() for p in sys.argv[1:3])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
dt = struct.unpack('f', struct.pack('f', 1/60))[0]


def observation(state):
    rows = [row for row in state['diagnostics']
            if row['text'].startswith('Volume probe:')
            and row['context'].get('tick') == state['timing']['tick']]
    assert len(rows) == 1, rows
    assert rows[0]['context']['module'] == 'project.physics_overlap_probe', rows
    match = re.fullmatch(r'Volume probe: count ([23]) \| sensor ([01])', rows[0]['text'])
    assert match, rows
    return int(match[1]), int(match[2])


def actor(state):
    return next(e for e in state['scene']['entities'] if e['name'] == 'Actor')


with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root / module.name)
    (root/'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Volume SDK proof', startup_scene=None, input=dict(version=1, actions=[]),
        modules=[dict(id='project.physics_overlap_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics'])])))

    def body(name, x, dynamic):
        return dict(id=name, name=name, components={'forge.position':dict(x=x,y=0,z=0),
            'forge.physics_body':dict(motion=2 if dynamic else 0, mass=1, density=1000,
            friction=.5, restitution=0, sensor=dynamic, gravity_factor=0,
            linear_damping=0, angular_damping=0, allow_sleeping=False),
            'forge.box_collider':dict(x=1,y=1,z=1)})

    workers = [Worker(runtime, root)]
    counts = set()
    try:
        workers[0].request('replace', scene=dict(version=1, entities=[
            body('Left',-.5,False), body('Right',.5,False), body('Actor',-3,True)]))
        for tick in range(1,331):
            left = workers[0].request('step')
            position = actor(left)['components']['forge.local_translation']
            assert abs(position['x'] - (-3+dt*tick)) < 2e-5, (tick, position)
            assert position['y'] == position['z'] == 0, position
            count, sensor = observation(left)
            counts.add(count)
            assert sensor == (count == 3), (tick, count, sensor)
            if abs(position['x']) < 1.49:
                assert count == 3, (tick, position, count)
            if abs(position['x']) > 1.51:
                assert count == 2, (tick, position, count)
            for worker in workers[1:]:
                right = worker.request('step')
                assert observation(right) == (count, sensor), (tick, right)
                for component in ('forge.local_translation', 'forge.local_rotation'):
                    assert actor(right)['components'][component] == actor(left)['components'][component]
            if tick in (60, 120):
                fresh = Worker(runtime, root)
                workers.append(fresh)
                checkpoint = left['recovery']
                restored = fresh.request('replace', scene=left['scene'], recovery=checkpoint,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert restored['timing']['paused'] and restored['timing']['tick'] == tick
        assert counts == {2, 3}, counts
    finally:
        for worker in workers:
            worker.close()
print('Compiled volume SDK: known geometry, copied sorted identities, sensor/mask '
      'filters, bounded output/owner/fixed guards and fresh-worker recovery passed')
