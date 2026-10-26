"""Original compiled 2D movement projects: input, support, tuning, both Play hosts."""
import json
from pathlib import Path
import sys
import time

from runtime_client import Runtime, activate


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
    def one_way_jump(worker, sdk_play, speed, jump_speed):
        # Exercise the shipped platform with the actual C++ input/movement
        # systems. SDK Play owns its fixed clock; Step releases its input.
        source = json.loads(saved)
        actor = next(row for row in source['entities'] if component in row['components'])
        actor['components']['forge.local_translation'].update(x=-4.8, y=1.0)
        actor['components'][component].update(speed=speed, jump_speed=jump_speed)
        activate(worker, source, sdk_play)
        for _ in range(100):
            state = worker.request('step')
        assert player(state)[component]['grounded'], player(state)
        if sdk_play:
            worker.request('resume')
            start = worker.request('snapshot', input_events=[dict(control='key.space', value=1)])
            top = player(start)['forge.local_translation']['y']
            deadline = time.monotonic() + 15
            while state['timing']['tick'] < start['timing']['tick'] + 150:
                assert time.monotonic() < deadline, state
                time.sleep(.005)
                state = worker.request('snapshot')
                top = max(top, player(state)['forge.local_translation']['y'])
            state = worker.request('pause')
        else:
            state = worker.request('step', input_events=[dict(control='key.space', value=1)])
            top = player(state)['forge.local_translation']['y']
            for _ in range(150):
                state = worker.request('step')
                top = max(top, player(state)['forge.local_translation']['y'])
        assert top > 2.7 and player(state)[component]['grounded'], player(state)
        assert abs(player(state)['forge.local_translation']['y'] - 2.6) < .03, player(state)

    def advance_ticks(worker, sdk_play, ticks, events=()):
        input_payload = dict(input_events=list(events)) if events else {}
        if sdk_play:
            worker.request('resume')
            state = worker.request('snapshot', **input_payload)
            target = state['timing']['tick'] + ticks
            deadline = time.monotonic() + 15
            while state['timing']['tick'] < target:
                assert time.monotonic() < deadline, state
                time.sleep(.005)
                state = worker.request('snapshot')
            return worker.request('pause')
        state = worker.request('step', **input_payload)
        for _ in range(ticks - 1):
            state = worker.request('step')
        return state

    def crouch_tunnel(worker, sdk_play):
        source = json.loads(saved)
        actor = next(row for row in source['entities'] if component in row['components'])
        actor['components']['forge.local_translation'].update(x=1.5, y=2)
        actor['components'][component]['speed'] = 2
        activate(worker, source, sdk_play)
        state = advance_ticks(worker, sdk_play, 45,
            [dict(control='key.c', value=1), dict(control='key.d', value=1)])
        position = player(state)['forge.local_translation']
        assert 2.95 < position['x'] < 3.2 and abs(position['y'] - 2) < .02, player(state)
        assert abs(player(state)['forge.sprite_renderer']['height'] - .9) < .001, player(state)
        release = [dict(control='key.c', value=0), dict(control='key.d', value=0)]
        state = worker.request('step', input_events=release)
        assert abs(player(state)['forge.sprite_renderer']['height'] - .9) < .001, player(state)
        assert player(state)[component]['grounded'], player(state)
        state = advance_ticks(worker, sdk_play, 25, [dict(control='key.d', value=1)])
        state = worker.request('step', input_events=release)
        assert player(state)['forge.local_translation']['x'] > 3.8, player(state)
        assert abs(player(state)['forge.sprite_renderer']['height'] - 1.2) < .001, player(state)

    def platform_ride(worker, sdk_play):
        source = json.loads(saved)
        actor = next(row for row in source['entities'] if component in row['components'])
        platform = next(row for row in source['entities']
                        if 'project.moving_platform_2d' in row['components'])
        actor['components']['forge.local_translation'].update(x=1.5, y=3.6)
        # An uphill diagonal path also tests vertical transport through the
        # same authored C++ system, without synthesizing controller intent.
        platform['components']['project.moving_platform_2d']['end_y'] = 4.5
        activate(worker, source, sdk_play)
        state = advance_ticks(worker, sdk_play, 90, [dict(control='key.c', value=1)])
        position = player(state)['forge.local_translation']
        platform_position = next(row['components']['forge.local_translation']
                                 for row in state['scene']['entities']
                                 if 'project.moving_platform_2d' in row['components'])
        assert position['x'] > 2.9 and position['y'] > 4.05, (position, platform_position)
        assert abs(position['x'] - platform_position['x']) < .02, (position, platform_position)
        assert abs(position['y'] - platform_position['y'] - .1) < .02, (position, platform_position)
        assert player(state)[component]['grounded'], player(state)
        assert abs(player(state)['forge.sprite_renderer']['height'] - .9) < .001, player(state)

    def surface_response(worker, sdk_play):
        comparison_path = project / 'Scenes/surface-response.scene.json'
        comparison_saved = comparison_path.read_bytes()
        source = json.loads(comparison_saved)
        activate(worker, source, sdk_play)
        state = advance_ticks(worker, sdk_play, 120)
        positions = {row['components']['forge.character_controller_2d']['floor_stop_on_slope']:
                     row['components'] for row in state['scene']['entities']
                     if component in row['components']}
        stopped, sliding = positions[True], positions[False]
        assert stopped[component]['grounded'] and sliding[component]['grounded'], positions
        assert abs(stopped['forge.local_translation']['x'] + 4) < .005, stopped
        assert sliding['forge.local_translation']['x'] < 3.95, sliding
        before_jump = sliding['forge.local_translation']['x']
        state = advance_ticks(worker, sdk_play, 30, [dict(control='key.space', value=1)])
        positions = {row['components']['forge.character_controller_2d']['floor_stop_on_slope']:
                     row['components'] for row in state['scene']['entities']
                     if component in row['components']}
        stopped, sliding = positions[True], positions[False]
        assert not stopped[component]['grounded'] and not sliding[component]['grounded'], positions
        assert abs(stopped['forge.local_translation']['x'] + 4) < .005, stopped
        assert sliding['forge.local_translation']['x'] > before_jump + .01, sliding
        state = advance_ticks(worker, sdk_play, 90, [dict(control='key.space', value=0)])
        assert all(row['components'][component]['grounded'] for row in state['scene']['entities']
                   if component in row['components']), state
        assert comparison_path.read_bytes() == comparison_saved, 'Play changed the comparison Scene'

    def stair_course(worker, sdk_play):
        course_path = project / 'Scenes/stair-steps.scene.json'
        course_saved = course_path.read_bytes()
        activate(worker, json.loads(course_saved), sdk_play)
        state = advance_ticks(worker, sdk_play, 120, [dict(control='key.d', value=1)])
        actors = {row['components']['forge.character_controller_2d']['step_height'] > 0:
                  row['components'] for row in state['scene']['entities']
                  if component in row['components']}
        stepping, blocked = actors[True], actors[False]
        assert stepping['forge.local_translation']['x'] > .95, stepping
        assert abs(stepping['forge.local_translation']['y'] - .75) < .02, stepping
        assert blocked['forge.local_translation']['x'] < -1.5, blocked
        assert abs(blocked['forge.local_translation']['y'] + 4) < .02, blocked
        assert stepping[component]['grounded'] and blocked[component]['grounded'], actors
        assert course_path.read_bytes() == course_saved, 'Play changed the stair Scene'

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
                    if controller:
                        one_way_jump(worker, True, speed, jump_speed)
                        observations[-1]['one_way_landing'] = True
                        platform_ride(worker, True)
                        observations[-1]['platform_ride'] = True
                        crouch_tunnel(worker, True)
                        observations[-1]['crouch_tunnel'] = True
                        surface_response(worker, True)
                        observations[-1]['surface_response'] = True
                        stair_course(worker, True)
                        observations[-1]['stair_course'] = True
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
                if controller:
                    one_way_jump(worker, sdk_play, speed, jump_speed)
                    observations[-1]['one_way_landing'] = True
                    platform_ride(worker, sdk_play)
                    observations[-1]['platform_ride'] = True
                    crouch_tunnel(worker, sdk_play)
                    observations[-1]['crouch_tunnel'] = True
                    surface_response(worker, sdk_play)
                    observations[-1]['surface_response'] = True
                    stair_course(worker, sdk_play)
                    observations[-1]['stair_course'] = True
            finally:
                worker.close()
    assert scene_path.read_bytes() == saved, 'Play changed the saved Scene'
    print(json.dumps(observations))
    return observations


if __name__ == '__main__':
    verify(*sys.argv[1:3])
    print('C++ 2D movement, painted/box support, wall exclusion, input edges, tuning and both hosts passed')
