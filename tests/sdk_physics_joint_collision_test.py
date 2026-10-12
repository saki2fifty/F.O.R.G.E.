"""Compiled pair policy, actual contacts/motion, and fresh worker recovery."""
import json
from pathlib import Path
import re
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
    (root / 'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Joint collision SDK proof', startup_scene=None, input=dict(version=1, actions=[]),
        modules=[dict(id='project.physics_joint_collision_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics'])])))
    asset, aid, bid, jid = (str(uuid.uuid4()) for _ in range(4))

    def body(eid, x, motion):
        return dict(id=eid, name='Anchor' if motion == 0 else 'Carriage', spatial=dict(mode='world'), components={
            'forge.local_translation': dict(x=x, y=0., z=0.),
            'forge.physics_body': dict(motion=motion, density=1000., friction=.5, restitution=0., gravity_factor=0., mass=1.,
                linear_damping=0., angular_damping=0., allow_sleeping=False),
            'forge.box_collider': dict(x=1., y=1., z=1.)})

    scene = dict(version=3, asset_id=asset, entities=[body(aid, 0., 0), body(bid, .9, 2),
        dict(id=jid, name='Loose connection', components={
            'forge.physics_joint': dict(enabled=True, kind=0, body_a=dict(scene=asset, entity=aid),
                body_b=dict(scene=asset, entity=bid), min_distance=.001, max_distance=10.,
                anchor_a_x=0., anchor_a_y=0., anchor_a_z=0., anchor_b_x=0., anchor_b_y=0., anchor_b_z=0.,
                axis_a=2, normal_a=0, axis_b=2, normal_b=0, min_angle=-90., max_angle=90.),
            'project.collision_cycle': dict(ticks=0)})])
    inspected = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
    declaration, = inspected['components']
    scene['entities'][-1]['components']['project.collision_cycle']['$forge'] = dict(
        format='forge.authored-component', version=1, module=declaration['module'],
        schema_version=declaration['schema_version'], digest=declaration['digest'])
    a, b = Worker(runtime, root), None
    try:
        a.request('replace', scene=scene)
        last_x = .9
        for tick in range(1, 91):
            result = a.request('step')
            markers = [d for d in result['diagnostics']
                if d['text'].startswith('Joint contacts ') and d['context']['tick'] == tick]
            assert len(markers) == 1, (tick, markers)
            match = re.fullmatch(r'Joint contacts (\d+)/(\d+) \| x ([-\d.]+) \| cycle (\d+)',
                markers[0]['text'])
            assert match and int(match[4]) == tick, markers
            begins, ends, x = int(match[1]), int(match[2]), float(match[3])
            if tick <= 30:
                assert abs(x-.9) < 1e-6 and begins == 0, (tick, match.groups())
            if tick == 31:
                assert x > .905 and begins == 1, match.groups()
            if tick >= 61:
                assert abs(x-last_x) < 1e-6 and begins == 0, (tick, match.groups(), last_x)
            row = next(r for r in result['scene']['entities'] if r['id'] == jid)
            assert row['components']['forge.physics_joint']['collide_connected'] == (31 <= tick < 61)
            assert row['components']['project.collision_cycle']['ticks'] == tick
            if b:
                other = b.request('step')
                by_id = {r['id']: r for r in other['scene']['entities']}
                for current in result['scene']['entities']:
                    target = by_id[current['id']]
                    for key, value in current['components'].items():
                        if key == 'forge.local_translation':
                            assert all(abs(value[k]-target['components'][key][k]) < 1e-6 for k in value)
                        else:
                            assert value == target['components'][key], (tick, key, value, target)
            if tick in (20, 45, 65):
                if b:
                    b.close()
                checkpoint = result['recovery']
                b = Worker(runtime, root)
                b.request('replace', scene=result['scene'], recovery=checkpoint,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            last_x = x
        print('Compiled connected-body controls, native contact transitions and fresh recovery passed')
    finally:
        a.close()
        if b:
            b.close()
