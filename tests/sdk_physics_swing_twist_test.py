"""Typed limit writes, independent native angular bounds and genuine fresh recovery."""
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
dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]
with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root / module.name)
    (root / 'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Swing Twist SDK proof', startup_scene=None, input=dict(version=1, actions=[]),
        modules=[dict(id='project.physics_swing_twist_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics'])])))
    inspected = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
    declaration, = inspected['components']
    stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
        schema_version=declaration['schema_version'], digest=declaration['digest'])
    for axis in range(3):
        for sign in (-1, 1):
            asset, aid, bid, jid = (str(uuid.uuid4()) for _ in range(4))
            def body(eid, motion):
                return dict(id=eid, name='Anchor' if motion == 0 else 'Attached',
                    spatial=dict(mode='world'), components={
                    'forge.local_translation': dict(x=0., y=0., z=0.),
                    'forge.physics_body': dict(motion=motion, density=1000., friction=.5,
                        restitution=0., gravity_factor=0., mass=1., linear_damping=0.,
                        angular_damping=0., allow_sleeping=False),
                    'forge.box_collider': dict(x=.6, y=.6, z=.6)})
            scene = dict(version=3, asset_id=asset, entities=[body(aid, 0), body(bid, 2),
                dict(id=jid, name='Swing Twist', components={
                    'forge.physics_joint': dict(enabled=True, kind=5,
                        body_a=dict(scene=asset, entity=aid), body_b=dict(scene=asset, entity=bid),
                        min_distance=1., max_distance=1.,
                        anchor_a_x=0., anchor_a_y=0., anchor_a_z=0.,
                        anchor_b_x=0., anchor_b_y=0., anchor_b_z=0.,
                        axis_a=0, normal_a=2, axis_b=0, normal_b=2,
                        min_angle=-25., max_angle=35., collide_connected=False),
                    'project.swing_probe': dict(ticks=0, axis=axis, speed=sign*2., **{'$forge': stamp})})])
            a, b = Worker(runtime, root), None
            try:
                a.request('replace', scene=scene)
                angle = None
                for tick in range(1, 91):
                    result = a.request('step')
                    rows = {r['id']: r for r in result['scene']['entities']}
                    joint = rows[jid]['components']['forge.physics_joint']
                    assert joint['swing_normal_angle'] == 20. and joint['swing_plane_angle'] == 10., joint
                    assert rows[jid]['components']['project.swing_probe']['ticks'] == tick
                    q = rows[bid]['components']['forge.local_rotation']
                    components = [q[k] for k in ('x', 'y', 'z')]
                    assert all(abs(v) < 1e-5 for i, v in enumerate(components) if i != axis), q
                    angle = math.remainder(math.degrees(2*math.atan2(components[axis], q['w'])), 360.)
                    if tick <= 4:
                        assert abs(angle-math.degrees(sign*2*tick*dt)) < .002, (axis, tick, angle)
                    markers = [d for d in result['diagnostics'] if d['context']['tick'] == tick
                        and d['text'].startswith('Swing native angular ')]
                    assert len(markers) == 1 and math.isfinite(float(markers[0]['text'].split()[3]))
                    if b:
                        other = b.request('step')
                        by_id = {r['id']: r for r in other['scene']['entities']}
                        for row in result['scene']['entities']:
                            for key, value in row['components'].items():
                                target = by_id[row['id']]['components'][key]
                                if key in ('forge.local_translation', 'forge.local_rotation'):
                                    assert all(abs(value[k]-target[k]) < 1e-5 for k in value)
                                else:
                                    assert value == target, (tick, key, value, target)
                    if tick in (4, 40):
                        if b:
                            b.close()
                        checkpoint = result['recovery']
                        b = Worker(runtime, root)
                        b.request('replace', scene=result['scene'], recovery=checkpoint,
                            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                bound = (25. if sign < 0 else 35.) if axis == 0 else (10. if axis == 1 else 20.)
                assert abs(angle-sign*bound) < .15, (axis, sign, angle, bound)
            finally:
                a.close()
                if b:
                    b.close()
    print('Compiled swing/twist limits, six independent angular bounds and fresh recovery passed')
