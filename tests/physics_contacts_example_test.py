"""Compiled editable contact reaction: independent motion and pending recovery."""
import json
from pathlib import Path
import re
import struct
import sys
from physics_velocity_example_test import Worker


def verify(runtime, project, env=None):
    settings = json.loads((project/'forge.project.json').read_text())
    path = project/settings['startup_scene']['source']
    original = path.read_bytes()
    dt = struct.unpack('f', struct.pack('f', 1/60))[0]

    def actor(state):
        return next(e['components'] for e in state['scene']['entities']
                    if 'project.contact_reaction' in e['components'])

    def events(state):
        result = []
        for row in state['diagnostics']:
            if row['context'].get('tick') != state['timing']['tick']:
                continue
            if not row['text'].startswith('Contact reaction: '):
                continue
            match = re.fullmatch(r'Contact reaction: (Begin|End) \| tick (\d+)', row['text'])
            assert match and int(match[2]) == state['timing']['tick'], row
            assert row['context']['module']=='project.gameplay', row
            result.append(match[1])
        return result

    for reverse, speed in ((True,2.),(False,4.)):
        scene = json.loads(original)
        control = next(e['components']['project.contact_reaction'] for e in scene['entities']
                       if 'project.contact_reaction' in e['components'])
        control.update(speed=speed, reverse_on_begin=reverse)
        a = Worker(runtime, project, env)
        recovered = []
        observed, begin_tick = [], None
        try:
            a.request('replace', scene=scene)
            for tick in range(1,151):
                left = a.request('step')
                components = actor(left)
                data = components['project.contact_reaction']
                current = events(left)
                observed.extend((tick, event) for event in current)
                if 'Begin' in current:
                    assert begin_tick is None, 'Native Begin repeated for the one collider pair'
                    begin_tick = tick
                    assert len(current)==1
                    commands = left['recovery']['physics']['commands']
                    assert [c['kind'] for c in commands] == ([6] if reverse else []), commands
                expected = -3 + speed*dt*(2*begin_tick-tick if reverse and begin_tick
                                         and tick>begin_tick else tick)
                p = components['forge.local_translation']
                assert abs(p['x']-expected)<2e-5 and p['y']==p['z']==0, (tick,p,expected)
                q = components['forge.local_rotation']
                assert q==dict(x=0.,y=0.,z=0.,w=1.), (tick,q)
                assert data['launched'] and data['reacted']==bool(reverse and begin_tick), data
                assert data['begin_count']==sum(e=='Begin' for _,e in observed), data
                assert data['end_count']==sum(e=='End' for _,e in observed), data
                for b in recovered:
                    right = b.request('step')
                    assert events(right)==current, (tick,left,right)
                    other = actor(right)
                    assert data==other['project.contact_reaction'], (tick,data,other)
                    for component in ('forge.local_translation','forge.local_rotation'):
                        assert all(abs(v-other[component][k])<1e-6
                                   for k,v in components[component].items())
                if tick==10 or current==['Begin']:
                    # Cover both pre-contact state and the PostPhysics pending
                    # reverse command, with genuinely new worker processes.
                    b = Worker(runtime, project, env)
                    recovered.append(b)
                    checkpoint = left['recovery']
                    restored = b.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick']==tick
                    assert actor(restored)['project.contact_reaction']==data
            assert len(observed)==2 and [e for _,e in observed]==['Begin','End'], observed
            # Native cache timing permits speculative contacts; independently
            # bound its entry to the known box volumes without equating cache to overlap.
            assert abs((-3+speed*dt*begin_tick)-(-1.5))<.15, (begin_tick,speed)
            assert path.read_bytes()==original, 'Contact example changed authored source'
        finally:
            a.close()
            for b in recovered:
                b.close()
    print('Compiled C++ contacts: sensor cache events, Inspector settings, independent '
          'pass/reversal equations, one-shot counters and fresh pending-reaction recovery passed')


if __name__=='__main__':
    verify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
