"""Execute velocity SDK guards and retain pending setters across a new worker."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from physics_velocity_example_test import Worker

runtime, module = (Path(p).resolve() for p in sys.argv[1:3])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root / module.name)
    settings = dict(version=2, name='Velocity SDK proof', startup_scene=None,
        input=dict(version=1, actions=[]), modules=[dict(id='project.physics_velocity_probe',
        implementation='1', sdk='experimental-1', fingerprint=fingerprint,
        library=module.name, dependencies=['forge.physics'])])
    (root/'forge.project.json').write_text(json.dumps(settings))
    scene = dict(version=1, entities=[dict(id='cube', name='Cube', components={
        'forge.position': dict(x=0, y=0, z=0), 'forge.physics_body': dict(motion=2,
        mass=2, density=1000, friction=.5, restitution=0, sensor=True,
        gravity_factor=0, linear_damping=0, angular_damping=0),
        'forge.box_collider': dict(x=1, y=1, z=1)})])
    a, b = Worker(runtime, root), None
    try:
        a.request('replace', scene=scene)
        for tick in range(1, 31):
            left = a.request('step')
            position = left['scene']['entities'][0]['components']['forge.local_translation']
            assert abs(position['x']-tick*5/60) < 3e-6 and position['y'] == position['z'] == 0
            pending = left['recovery']['physics']['commands']
            assert left['recovery']['physics']['version'] == 5
            assert [c['kind'] for c in pending] == [6, 7], pending
            if b:
                right = b.request('step')
                for component in ('forge.local_translation', 'forge.local_rotation'):
                    lv = left['scene']['entities'][0]['components'][component]
                    rv = right['scene']['entities'][0]['components'][component]
                    assert all(abs(lv[k]-rv[k]) < 1e-6 for k in lv), (left, right)
            if tick == 4:
                b = Worker(runtime, root)
                checkpoint = left['recovery']
                recovered = b.request('replace', scene=left['scene'], recovery=checkpoint,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert recovered['timing']['paused'] and recovered['timing']['tick'] == 4
    finally:
        a.close()
        if b:
            b.close()
print('Compiled velocity SDK: fixed/owner/input/channel guards, copied FIFO equations '
      'and true pending-command recovery passed')
