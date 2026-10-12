"""Prove compiled typed joint writes and true native motor recovery."""
import json
from pathlib import Path
import shutil
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
    settings = dict(version=2, name='Joint motor SDK proof', startup_scene=None,
        input=dict(version=1, actions=[]), modules=[dict(id='project.physics_motor_probe',
        implementation='1', sdk='experimental-1', fingerprint=fingerprint,
        library=module.name, dependencies=['forge.physics'])])
    (root / 'forge.project.json').write_text(json.dumps(settings))
    source = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Scenes/25-velocity-motors.scene.json'
    gallery = json.loads(source.read_text())
    selected = [row for row in gallery['entities'] if row['name'].startswith('Driven slider')]
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
            joint.update(motor_enabled=False, min_travel=-20., max_travel=20.)
            for key in ('body_a', 'body_b'):
                joint[key] = dict(scene=scene['asset_id'], entity=ids[joint[key]['entity']])
    a, b = Worker(runtime, root), None
    try:
        a.request('replace', scene=scene)
        dt = 1. / 60
        for _ in range(60): result = a.request('step')
        markers = [d for d in result['diagnostics'] if d['text'].startswith('Motor native velocity ')]
        assert len(markers) == 1 and markers[0]['context']['tick'] == 60, markers
        assert abs(float(markers[0]['text'].split()[-1]) - .5) < 1e-5, markers
        carriage = next(r for r in result['scene']['entities'] if r['name'] == 'Driven slider carriage')
        expected = -2 + .5 * dt * dt * (60 * 61 / 2)
        assert abs(carriage['components']['forge.local_translation']['x'] - expected) < 1e-4, carriage
        checkpoint = result['recovery']
        b = Worker(runtime, root)
        b.request('replace', scene=result['scene'], recovery=checkpoint,
            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
        for _ in range(60):
            live, restored = a.request('step'), b.request('step')
            lhs = next(r for r in live['scene']['entities'] if r['name'] == 'Driven slider carriage')
            rhs = next(r for r in restored['scene']['entities'] if r['name'] == 'Driven slider carriage')
            assert abs(lhs['components']['forge.local_translation']['x'] - rhs['components']['forge.local_translation']['x']) < 1e-5, (lhs, rhs)
        expected = -2 + .5 * dt * dt * (120 * 121 / 2)
        assert abs(lhs['components']['forge.local_translation']['x'] - expected) < 1e-4, lhs
        print('Compiled typed velocity motor, copied F/m observation and fresh native recovery passed')
    finally:
        a.close()
        if b: b.close()
