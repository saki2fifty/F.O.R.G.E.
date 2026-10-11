"""Actual fixed SDK contact copies and native cache recovery in a fresh worker."""
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


def events(state):
    result = []
    for row in state['diagnostics']:
        if row['context'].get('tick') != state['timing']['tick']:
            continue
        if not row['text'].startswith('Contact cache '):
            continue
        match = re.fullmatch(r'Contact cache (Begin|End) \| tick (\d+)', row['text'])
        assert match and int(match[2]) == state['timing']['tick'], row
        assert row['context']['module'] == 'project.physics_contacts_probe', row
        result.append(match[1])
    return result


with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copyfile(module, root / module.name)
    (root/'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Contact SDK proof', startup_scene=None, input=dict(version=1, actions=[]),
        modules=[dict(id='project.physics_contacts_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics'])])))
    def body(name, x, dynamic):
        return dict(id=name, name=name, components={'forge.position':dict(x=x,y=0,z=0),
            'forge.physics_body':dict(motion=2 if dynamic else 0, mass=1, density=1000,
            friction=.5, restitution=0, sensor=dynamic, gravity_factor=0,
            linear_damping=0, angular_damping=0, allow_sleeping=False),
            'forge.box_collider':dict(x=1,y=1,z=1)})
    barrier = body('Barrier',0,False)
    character = dict(id='Character', name='Character', components={
        'forge.position':dict(x=0,y=-.35,z=0),
        'forge.character_controller':dict(enabled=True, shape=0, radius=.35,
        height=1.1, crouch_height=.45, mass=70, max_strength=100, max_slope=50,
        step_height=.4, step_forward=.15, floor_probe=.5, gravity_factor=0,
        layer=0, mask=4294967295)})
    for target in (barrier, character):
        a, b = Worker(runtime, root), None
        observed = []
        try:
            a.request('replace', scene=dict(version=1, entities=[target, body('Actor',-3,True)]))
            for tick in range(1,181):
                left = a.request('step')
                actor = next(e for e in left['scene']['entities'] if e['name']=='Actor')
                p = actor['components']['forge.local_translation']
                assert abs(p['x']-(-3+2*dt*tick)) < 1e-5 and p['y']==p['z']==0, (tick,p)
                current = events(left)
                observed.extend((tick, event) for event in current)
                if b:
                    right = b.request('step')
                    assert events(right) == current, (tick, left, right)
                    other = next(e for e in right['scene']['entities'] if e['name']=='Actor')
                    for component in ('forge.local_translation','forge.local_rotation'):
                        assert actor['components'][component] == other['components'][component]
                if current == ['Begin']:
                    assert b is None, 'Duplicate Begin after uninterrupted native cache'
                    b = Worker(runtime, root)
                    checkpoint = left['recovery']
                    restored = b.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick']==tick
            assert len(observed)==2 and observed[0][1]=='Begin' and observed[1][1]=='End', observed
            assert 55<=observed[0][0]<=75 and 115<=observed[1][0]<=135, observed
        finally:
            a.close()
            if b:
                b.close()
print('Compiled contacts SDK: canonical copies/ticks, broadcast, output/owner/fixed '
      'guards, no prior-tick replay, sensor equations and fresh-cache recovery passed')
