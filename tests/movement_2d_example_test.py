"""Original compiled 2D movement projects: input, support, tuning, both Play hosts."""
import json
from pathlib import Path
import sys
import time

from runtime_client import Runtime


def activate(worker, scene, sdk_play):
    state = worker.request('replace', scene=scene)
    if not sdk_play:
        return state
    deadline = time.monotonic() + 15
    while not state.get('candidate'):
        assert time.monotonic() < deadline, state
        time.sleep(.005)
        state = worker.request('snapshot')
    ticket = state['candidate']['ticket']
    candidate = worker.request('candidate', ticket=ticket)['candidate']
    assert candidate['ticket'] == ticket and candidate['session'] == worker.session
    state = worker.request('snapshot', candidate_ack=dict(
        session=worker.session, ticket=ticket, accepted=True, diagnostic=''))
    while state['activation']['generation'] != ticket:
        assert time.monotonic() < deadline, state
        time.sleep(.005)
        state = worker.request('snapshot')
    return worker.request('snapshot', editor_epoch=dict(
        epoch=ticket, captured=False, input_device='EditorHost'))


def verify(runtime, project):
    runtime, project = Path(runtime).resolve(), Path(project).resolve()
    settings = json.loads((project / 'forge.project.json').read_text())
    scene_path = project / settings['startup_scene']['source']
    saved = scene_path.read_bytes()
    original = json.loads(saved)
    controller = any('project.character_movement_2d' in row['components']
                     for row in original['entities'])
    component = 'project.character_movement_2d' if controller else 'project.movement_2d'
    feet_offset = .6 if controller else 0

    def player(state):
        return next(row['components'] for row in state['scene']['entities']
                    if component in row['components'])

    initial_position = next(row['components']['forge.local_translation']
                            for row in original['entities'] if component in row['components'])
    observations = []
    for sdk_play in (False, True):
        for speed, jump_speed in ((4., 6.), (2., 8.)):
            scene = json.loads(saved)
            data = next(row['components'][component] for row in scene['entities']
                        if component in row['components'])
            data.update(speed=speed, jump_speed=jump_speed)
            args = ['--sdk-project', project]
            if sdk_play:
                args += ['--sdk-play', 'on', '--user-data', project / 'test-users']
            worker = Runtime(runtime, args)
            try:
                activate(worker, scene, sdk_play)
                for _ in range(100):
                    state = worker.request('step')
                components = player(state)
                position = components['forge.local_translation']
                assert abs(position['y'] - (1.6 - feet_offset)) < .03, position
                assert components[component]['grounded'], components
                assert abs(position['x'] + 4) < .001, position
                initial_x = position['x']
                if sdk_play:
                    # SDK Play's Step deliberately releases gameplay input. Use
                    # its real fixed clock for input tests instead of bypassing
                    # that host contract or inventing a caller-supplied delta.
                    worker.request('resume')
                    start = worker.request('snapshot', input_events=[dict(control='key.d', value=1)])
                    start_tick = start['timing']['tick']
                    deadline = time.monotonic() + 15
                    while state['timing']['tick'] < start_tick + 18:
                        assert time.monotonic() < deadline, state
                        time.sleep(.005)
                        state = worker.request('snapshot')
                    state = worker.request('pause')
                    ticks = state['timing']['tick'] - start_tick
                    moved = player(state)['forge.local_translation']['x']
                    assert abs(moved - initial_x - speed * ticks / 60) < speed * 3 / 60, (ticks, moved)
                    base = player(state)['forge.local_translation']['y']
                    worker.request('resume')
                    start = worker.request('snapshot', input_events=[dict(control='key.space', value=1)])
                    apex = base
                    deadline = time.monotonic() + 15
                    while state['timing']['tick'] < start['timing']['tick'] + 150:
                        assert time.monotonic() < deadline, state
                        time.sleep(.005)
                        state = worker.request('snapshot')
                        apex = max(apex, player(state)['forge.local_translation']['y'])
                    state = worker.request('pause')
                    assert apex - base > (1.6 if jump_speed == 6 else 3.), (jump_speed, apex, base)
                    assert player(state)[component]['grounded'], player(state)
                    assert abs(player(state)['forge.local_translation']['y'] - base) < .03
                    observations.append(dict(sdk_play=True, clock='runtime', speed=speed,
                                             jump_speed=jump_speed, jump_rise=apex - base))
                    reset = activate(worker, original, True)
                    assert player(reset)[component]['speed'] == 4
                    assert player(reset)['forge.local_translation'] == initial_position
                    continue
                for tick in range(18):
                    event = dict(input_events=[dict(control='key.d', value=1)]) if tick == 0 else {}
                    state = worker.request('step', **event)
                moved = player(state)['forge.local_translation']['x']
                assert abs(moved - initial_x - speed * 18 / 60) < .002, (speed, moved)
                stopped = worker.request('step', input_events=[dict(control='key.d', value=0)])
                assert abs(player(stopped)['forge.local_translation']['x'] - moved) < .002

                base = player(stopped)['forge.local_translation']['y']
                state = worker.request('step', input_events=[dict(control='key.space', value=1)])
                first_rise = player(state)['forge.local_translation']['y'] - base
                assert abs(first_rise - (jump_speed if controller else jump_speed - 9.81 / 60) / 60) < .003, first_rise
                apex = base
                for _ in range(150):
                    state = worker.request('step')
                    apex = max(apex, player(state)['forge.local_translation']['y'])
                assert apex - base > (1.6 if jump_speed == 6 else 3.), (jump_speed, apex, base)
                assert player(state)[component]['grounded'], player(state)
                assert abs(player(state)['forge.local_translation']['y'] - base) < .03
                # Holding Space never auto-repeats after landing; a fresh edge is required.
                for _ in range(20):
                    state = worker.request('step')
                assert abs(player(state)['forge.local_translation']['y'] - base) < .03
                worker.request('step', input_events=[dict(control='key.space', value=0)])

                # Walk off the painted ledge and land on the ordinary box stone.
                steps = round((-.6 - moved) / speed * 60)
                for tick in range(steps):
                    event = dict(input_events=[dict(control='key.d', value=1)]) if tick == 0 else {}
                    state = worker.request('step', **event)
                worker.request('step', input_events=[dict(control='key.d', value=0)])
                for _ in range(60):
                    state = worker.request('step')
                assert abs(player(state)['forge.local_translation']['y'] - (1.1 - feet_offset)) < .03, player(state)
                assert player(state)[component]['grounded'], player(state)

                # A resting side-wall overlap must not hide the floor from the sweep.
                for tick in range(240):
                    event = dict(input_events=[dict(control='key.a', value=1)]) if tick == 0 else {}
                    state = worker.request('step', **event)
                components = player(state)
                assert -6.75 < components['forge.local_translation']['x'] < -6.6, components
                assert components[component]['grounded'], components
                assert abs(components['forge.local_translation']['y'] + 2.4 + feet_offset) < .03, components
                assert components['forge.local_translation']['z'] == 0
                assert components['forge.local_scale'] == dict(x=1, y=1, z=1)
                if controller:
                    assert 'forge.physics_body_2d' not in components and 'forge.capsule_collider_2d' not in components
                observations.append(dict(sdk_play=sdk_play, speed=speed, jump_speed=jump_speed,
                                         jump_rise=apex - base, wall_grounded=True, controller=controller))
                reset = activate(worker, original, sdk_play)
                assert player(reset)[component]['speed'] == 4
                assert player(reset)['forge.local_translation'] == initial_position
                assert reset['timing']['tick'] == 0
            finally:
                worker.close()
    assert scene_path.read_bytes() == saved, 'Play changed the saved Scene'
    print(json.dumps(observations))
    return observations


if __name__ == '__main__':
    verify(*sys.argv[1:3])
    print('C++ 2D movement, painted/box support, wall exclusion, input edges, tuning and both hosts passed')
