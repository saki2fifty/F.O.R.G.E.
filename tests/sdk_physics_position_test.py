"""Independent spring trajectory and new-worker recovery through compiled C++."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
from physics_velocity_example_test import Worker

runtime, module = (Path(p).resolve() for p in sys.argv[1:3])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root / module.name)
    settings = dict(version=2, name='Joint position SDK proof', startup_scene=None,
        input=dict(version=1, actions=[]), modules=[dict(id='project.physics_position_probe',
        implementation='1', sdk='experimental-1', fingerprint=fingerprint,
        library=module.name, dependencies=['forge.physics'])])
    (root / 'forge.project.json').write_text(json.dumps(settings))
    source = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Scenes/26-position-drives.scene.json'
    gallery = json.loads(source.read_text())
    selected = [row for row in gallery['entities'] if row['name'].startswith('Return slider')]
    scene = dict(version=3, asset_id=str(uuid.uuid4()), entities=selected)
    ids = {row['id']: str(uuid.uuid4()) for row in selected}
    for row in scene['entities']:
        row['id'] = ids[row['id']]
        c = row['components']
        for key in list(c):
            if key not in ('forge.local_translation', 'forge.local_rotation', 'forge.local_scale',
                           'forge.physics_body', 'forge.box_collider', 'forge.physics_joint'):
                del c[key]
        if 'forge.physics_joint' in c:
            joint = c['forge.physics_joint']
            joint.update(motor_enabled=False, motor_mode=0, slider_motor_position=0., min_travel=-20., max_travel=20.)
            for key in ('body_a', 'body_b'):
                joint[key] = dict(scene=scene['asset_id'], entity=ids[joint[key]['entity']])
    a, b = Worker(runtime, root), None
    try:
        a.request('replace', scene=scene)
        dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]
        omega = 2 * math.pi
        divisor = 1 + 2 * omega * dt + omega * omega * dt * dt
        position = velocity = 0.
        for tick in range(1, 121):
            velocity = (velocity + dt * omega * omega * (.5 - position)) / divisor
            position += dt * velocity
            result = a.request('step')
            markers = [d for d in result['diagnostics']
                if d['text'].startswith('Position native velocity ') and d['context']['tick'] == tick]
            assert len(markers) == 1 and markers[0]['context']['tick'] == tick, markers
            assert abs(float(markers[0]['text'].split()[-1]) - velocity) < 1e-4, (tick, markers, velocity)
            carriage = next(r for r in result['scene']['entities'] if r['name'] == 'Return slider carriage')
            x = carriage['components']['forge.local_translation']['x']
            assert abs(x - (-2 + position)) < 1e-4, (tick, x, position)
            if b:
                restored = b.request('step')
                other = next(r for r in restored['scene']['entities'] if r['name'] == 'Return slider carriage')
                assert abs(x - other['components']['forge.local_translation']['x']) < 1e-5, (tick, carriage, other)
            if tick == 20:
                checkpoint = result['recovery']
                b = Worker(runtime, root)
                b.request('replace', scene=result['scene'], recovery=checkpoint,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
        assert abs(x + 1.5) < 3e-4, x
        print('Compiled C++ position target, independent implicit spring and fresh native recovery passed')
    finally:
        a.close()
        if b: b.close()
