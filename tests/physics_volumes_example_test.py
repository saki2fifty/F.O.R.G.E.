"""Compiled editable volume queries: independent geometry and worker recovery."""
import json
from pathlib import Path
import re
import struct
import sys
from physics_velocity_example_test import Worker
from runtime_client import Runtime, activate


def assert_ray(state):
    control = next(e['components']['project.ray_probe'] for e in state['scene']['entities']
                   if 'project.ray_probe' in e['components'])
    dimensions = 2 if control['planar'] else 3
    suffix = '_2d' if control['planar'] else ''
    origin = [control['origin_'+axis] for axis in 'xyz'[:dimensions]]
    delta = [control['delta_'+axis] for axis in 'xyz'[:dimensions]]
    fractions = []
    # Independent axis-aligned slab intersection for these original box scenes.
    for entity in state['scene']['entities']:
        c = entity['components']
        body, box = c.get('forge.physics_body'+suffix), c.get('forge.box_collider'+suffix)
        if body is None or box is None or not body.get('enabled', True):
            continue
        if body.get('sensor', False) and not control['include_sensors']:
            continue
        position, scale = c['forge.local_translation'], c.get('forge.local_scale', {})
        near, far = 0., 1.
        for i, axis in enumerate('xyz'[:dimensions]):
            half = box[axis]*abs(scale.get(axis, 1))/2
            low, high = position[axis]-half-origin[i], position[axis]+half-origin[i]
            if delta[i] == 0:
                if low > 0 or high < 0:
                    far = -1
                    break
            else:
                values = sorted((low/delta[i], high/delta[i]))
                near, far = max(near, values[0]), min(far, values[1])
        # Native float geometry can admit an endpoint within this tolerance.
        if near <= far + 1e-6:
            fractions.append(max(0., min(1., near)))
    assert control['observed'] and control['last_count'] == len(fractions), (control, fractions)
    expected = min(fractions) if fractions else -1
    assert abs(control['nearest_fraction']-expected) < 2e-6, (control, expected)
    return control


def verify_ray_scenes(runtime, project, env):
    for source in ('body-volumes.scene.json', 'planar-rays.scene.json'):
        path = project/'Scenes'/source
        original = path.read_bytes()
        for sdk_play in (False, True):
            for include, miss, reverse in ((True, False, False), (False, False, True), (True, True, False)):
                scene = json.loads(original)
                data = next(e['components']['project.ray_probe'] for e in scene['entities']
                            if 'project.ray_probe' in e['components'])
                data['include_sensors'] = include
                if miss:
                    data['origin_y'] = 5
                if reverse:
                    data.update(origin_x=5, delta_x=-10)
                args = ['--sdk-project', project]
                if sdk_play:
                    args += ['--sdk-play', 'on', '--user-data', project/'ray-user-data']
                worker = Runtime(runtime, args, env=env, cwd=project)
                fresh = None
                try:
                    activate(worker, scene, sdk_play)
                    for tick in range(1, 5):
                        state = worker.request('step')
                        control = assert_ray(state)
                        assert control['last_count'] == (0 if miss else 3 if include else 2)
                        if tick == 2 and not sdk_play:
                            checkpoint = state['recovery']
                            fresh = Runtime(runtime, args, env=env, cwd=project)
                            fresh.request('replace', scene=state['scene'], recovery=checkpoint,
                                recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                        elif fresh:
                            other = fresh.request('step')
                            assert assert_ray(other) == control
                        rows = [row for row in state['diagnostics']
                                if row['context'].get('tick') == state['timing']['tick']
                                and row['text'].startswith('Ray query ')]
                        assert len(rows) == (1 if tick == 1 else 0), rows
                finally:
                    worker.close()
                    if fresh:
                        fresh.close()
                assert path.read_bytes() == original, 'Ray example changed its scene source'


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
                assert_ray(left)
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
    verify_ray_scenes(runtime, project, env)
    print('Compiled C++ volumes: sphere/box equations, sensor filtering, Inspector '
          'settings, change-only diagnostics and fresh native worker recovery; complete '
          '3D/XY rays, reversed segments/misses/sensors and both actual hosts passed')


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
