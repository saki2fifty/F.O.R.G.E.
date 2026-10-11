"""Compiled editable volume queries: independent geometry and worker recovery."""
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
                    if 'project.volume_probe' in e['components'])

    def changes(state):
        result = []
        for row in state['diagnostics']:
            if row['text'].startswith('Volume query:') and row['context'].get('tick') == state['timing']['tick']:
                match = re.fullmatch(r'Volume query: count (\d+)', row['text'])
                assert match and row['context']['module'] == 'project.gameplay', row
                result.append(int(match[1]))
        return result

    for include, speed, radius in ((True,1.,1.), (False,2.,.75)):
        scene = json.loads(original)
        data = next(e['components']['project.volume_probe'] for e in scene['entities']
                    if 'project.volume_probe' in e['components'])
        data.update(include_sensors=include, speed=speed, radius=radius)
        workers = [Worker(runtime, project, env)]
        observed, previous = [], None
        try:
            workers[0].request('replace', scene=scene)
            for tick in range(1,331):
                left = workers[0].request('step')
                components = actor(left)
                control = components['project.volume_probe']
                position = components['forge.local_translation']
                expected_x = -3+speed*dt*tick
                assert abs(position['x']-expected_x) < 3e-5 and position['y'] == position['z'] == 0
                assert components['forge.local_rotation'] == dict(x=0.,y=0.,z=0.,w=1.)
                count = control['last_count']
                assert control['launched'] and control['observed']
                if not include or abs(expected_x) > radius+.51:
                    assert count == 2, (tick,expected_x,control)
                elif abs(expected_x) < radius+.49:
                    assert count == 3, (tick,expected_x,control)
                current = changes(left)
                assert current == ([count] if count != previous else []), (tick,current,previous,count)
                observed.extend(current)
                previous = count
                for worker in workers[1:]:
                    right = worker.request('step')
                    assert changes(right) == current
                    other = actor(right)
                    for component in ('project.volume_probe','forge.local_translation','forge.local_rotation'):
                        assert components[component] == other[component], (tick,component)
                if tick in (60,120):
                    fresh = Worker(runtime, project, env)
                    workers.append(fresh)
                    checkpoint = left['recovery']
                    restored = fresh.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick'] == tick
                    assert actor(restored)['project.volume_probe'] == control
            assert observed == ([2,3,2] if include else [2]), observed
            assert path.read_bytes() == original, 'Volume example changed authored source'
        finally:
            for worker in workers:
                worker.close()
    print('Compiled C++ volumes: sphere/box equations, sensor filtering, Inspector '
          'settings, change-only diagnostics and fresh native worker recovery passed')


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
