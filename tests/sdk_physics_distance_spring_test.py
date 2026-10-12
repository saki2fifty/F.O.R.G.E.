"""Public typed spring controls, independent integration and fresh worker recovery."""
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
dt = struct.unpack('f', struct.pack('f', 1/60))[0]
with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root/module.name)
    (root/'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Distance Spring SDK proof', startup_scene=None, input=dict(version=1, actions=[]),
        modules=[dict(id='project.physics_distance_spring_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics'])])))
    inspected = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
    declaration, = inspected['components']
    stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
        schema_version=declaration['schema_version'], digest=declaration['digest'])
    for mass, damping in ((1., .15), (7., 1.)):
        asset, aid, bid, jid = (str(uuid.uuid4()) for _ in range(4))
        def body(eid, motion, x):
            return dict(id=eid, name='Anchor' if motion == 0 else 'Attached',
                spatial=dict(mode='world'), components={
                'forge.local_translation':dict(x=x,y=0.,z=0.),
                'forge.physics_body':dict(motion=motion,density=1000.,friction=.5,
                    restitution=0.,mass=mass if motion else 0.,gravity_factor=0.,
                    linear_damping=0.,angular_damping=0.,allow_sleeping=False),
                'forge.box_collider':dict(x=.2,y=.2,z=.2)})
        scene = dict(version=3,asset_id=asset,entities=[body(aid,0,0.),body(bid,2,1.4),
            dict(id=jid,name='Elastic link',components={
                'forge.physics_joint':dict(enabled=True,kind=0,body_a=dict(scene=asset,entity=aid),
                    body_b=dict(scene=asset,entity=bid),min_distance=1.,max_distance=1.,
                    anchor_a_x=0.,anchor_a_y=0.,anchor_a_z=0.,
                    anchor_b_x=0.,anchor_b_y=0.,anchor_b_z=0.,
                    axis_a=0,normal_a=1,axis_b=0,normal_b=1,min_angle=-90.,max_angle=90.,
                    collide_connected=False),
                'project.spring_probe':dict(ticks=0,frequency=1.,damping=damping,
                    switch_tick=45,**{'$forge':stamp})})])
        a, b = Worker(runtime,root), None
        try:
            a.request('replace',scene=scene)
            position, velocity = 1.4, 0.
            for tick in range(1,91):
                frequency, ratio = (1.,damping) if tick < 45 else (2.,1.)
                omega = 2*math.pi*frequency
                velocity = (velocity-dt*omega*omega*(position-1))/(1+2*ratio*omega*dt+omega*omega*dt*dt)
                position += dt*velocity
                result = a.request('step')
                rows = {r['id']:r for r in result['scene']['entities']}
                joint = rows[jid]['components']['forge.physics_joint']
                probe = rows[jid]['components']['project.spring_probe']
                assert probe['ticks'] == tick and abs(probe['frequency']-frequency) < 1e-6
                assert abs(joint['distance_spring_frequency']-frequency) < 1e-6
                assert abs(joint['distance_spring_damping']-ratio) < 1e-6
                actual = rows[bid]['components']['forge.local_translation']
                assert abs(actual['x']-position) < 1e-4 and abs(actual['y']) < 1e-6 and abs(actual['z']) < 1e-6, (tick,actual,position)
                markers = [d for d in result['diagnostics'] if d['context']['tick'] == tick
                    and d['text'].startswith('Spring native velocity ')]
                assert len(markers) == 1 and abs(float(markers[0]['text'].split()[3])-velocity) < 1e-4, (tick,markers,velocity)
                if b:
                    other = b.request('step')
                    by_id = {r['id']:r for r in other['scene']['entities']}
                    for row in result['scene']['entities']:
                        for key,value in row['components'].items():
                            target = by_id[row['id']]['components'][key]
                            if key in ('forge.local_translation','forge.local_rotation'):
                                assert all(abs(value[k]-target[k]) < 1e-5 for k in value)
                            else:
                                assert value == target, (tick,key,value,target)
                if tick in (20,50):
                    if b: b.close()
                    checkpoint = result['recovery']
                    b = Worker(runtime,root)
                    b.request('replace',scene=result['scene'],recovery=checkpoint,
                        recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
        finally:
            a.close()
            if b: b.close()
print('Typed distance spring integration, live frequency/damping switch and fresh recovery passed')
