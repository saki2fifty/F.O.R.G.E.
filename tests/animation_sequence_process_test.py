"""Exact-SDK C++ sequence controls, independent poses, and fresh-worker recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from physics_velocity_example_test import Worker

def multiway_layers(time, duration, step):
    weights = [0.] * 8
    suffix = 1.
    for index in range(7, 0, -1):
        fade = max(0., min(1., (time-step*index)/(duration-step)))
        weights[index] = fade*suffix
        suffix *= 1-fade
    weights[0] = suffix
    if sum(value > 0 for value in weights) > 2:
        first, second = sorted(range(8), key=lambda index: (weights[index], index), reverse=True)[:2]
        relative = weights[second]/(weights[first]+weights[second])
    else:
        first = 0
        while first+2 < 8 and time >= step*first+duration:
            first += 1
        second = first+1
        relative = max(0., min(1., (time-step*second)/(duration-step)))
    return weights, first, second, relative


def check_multiway_watch(status, player, time, duration, step):
    weights, primary, incoming, relative = multiway_layers(time, duration, step)
    complete = status['sequence_clips']
    assert complete['multiway'] is True
    assert complete['summary_only'] == (sum(value > 0 for value in weights) > 2)
    assert complete['active_primary'] == primary and complete['active_next'] == incoming
    assert abs(status['blend_weight']-relative) < 1e-9
    assert abs(complete['next']['weight']-relative) < 1e-9
    for index, layer in enumerate(complete['clips']):
        assert abs(layer['weight']-weights[index]) < 1e-9
        assert abs(layer['time_seconds']-max(0., min(duration, time-step*index))) < 1e-9
    identities = [player['clip'], player['sequence_next_clip']] + [row['clip'] for row in player['sequence_extra_clips']]
    assert complete['clip'] == identities[primary] and complete['next']['clip'] == identities[incoming]


runtime, gallery, module = (Path(arg).resolve() for arg in sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix='forge-sequence-sdk-', dir=runtime.parent) as scratch:
    root = Path(scratch) / 'project'
    shutil.copytree(gallery, root, ignore=shutil.ignore_patterns('*.lock'))
    shutil.copyfile(module, root / module.name)
    defaults = json.loads((root / 'forge.project.json').read_text(encoding='utf-8'))['game']
    defaults['application_id'] = 'org.forge.sequence-sdk-probe'
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    (root / 'forge.project.json').write_text(json.dumps(dict(version=2, name='Sequence SDK',
        startup_scene=None, game=defaults, input=dict(version=1, actions=[]), modules=[dict(
            id='project.sequence_probe', implementation='1', sdk='experimental-1',
            fingerprint=fingerprint, library=module.name, dependencies=['forge.animation'])])),
        encoding='utf-8')
    declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root],
                                                       text=True))['components']
    stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
                 schema_version=declaration['schema_version'], digest=declaration['digest'])
    scene_names = ('62-coordinated-animation.scene.json', '68-sequence-camera-shots.scene.json',
                   '70-sequence-audio-cues.scene.json', '145-sequence-clip-blending.scene.json',
                   '146-nested-sequences.scene.json', '36-animation-root-motion.scene.json')
    scenarios = [(name, False, False, False, False) for name in scene_names]
    scenarios.append(('36-animation-root-motion.scene.json', True, False, False, False))
    scenarios.append(('146-nested-sequences.scene.json', False, True, False, False))
    scenarios.append(('146-nested-sequences.scene.json', False, True, False, True))
    scenarios.extend((name, False, False, True, False) for name in (
        '36-animation-root-motion.scene.json', '145-sequence-clip-blending.scene.json'))
    scenarios = [(*scenario, False) for scenario in scenarios]
    scenarios.append(('146-nested-sequences.scene.json', False, True, False, True, True))
    scenarios = [(*scenario, False) for scenario in scenarios]
    scenarios.extend((name, False, False, True, False, False, True) for name in (
        '36-animation-root-motion.scene.json', '145-sequence-clip-blending.scene.json'))
    for scene_name, mixed, rated, collections, child_loop, placements, multiway in scenarios:
        label = (scene_name, mixed, rated, collections, child_loop, placements, multiway)
        print('SDK sequence scenario:', label, flush=True)
        source_path = root / 'Scenes' / scene_name
        original = source_path.read_bytes()
        source = json.loads(original)
        skeletal = scene_name == '36-animation-root-motion.scene.json'
        skeletal_actor = None
        skeletal_clip = None
        if skeletal:
            controller_id = str(uuid.uuid4())
            binding = dict(scene=source['asset_id'], entity=controller_id)
            for row in source['entities']:
                player = row['components'].get('forge.animator')
                if player is None:
                    continue
                if player['root_motion'] == 0 and skeletal_actor is None:
                    skeletal_actor = row['id']
                    skeletal_clip = player['clip']
                    player.update(sequence=binding, sequence_start_seconds=0,
                                  sequence_next_clip=player['clip'], sequence_next_start_seconds=.5,
                                  playback_speed=0, play_on_start=False, loop=True)
                else:
                    player['enabled'] = False
            assert skeletal_actor is not None
            if mixed:
                property_scene = json.loads((root / 'Scenes' / '62-coordinated-animation.scene.json').read_bytes())
                light = next(row['components'] for row in property_scene['entities']
                             if 'forge.transform_animator' in row['components'] and
                             'forge.light' in row['components'])
                target = next(row for row in source['entities'] if row['id'] == skeletal_actor)
                target['components']['forge.light'] = light['forge.light']
                target['components']['forge.audio_source'] = light['forge.audio_source']
                target['components']['forge.transform_animator'] = dict(
                    light['forge.transform_animator'], sequence=binding)
            source['entities'].append(dict(id=controller_id, name='Skeletal SDK sequence',
                spatial=dict(mode='world'), components={'forge.animation_sequence':dict(
                    duration=4, enabled=True, play_on_start=True, loop=False, playback_speed=1,
                    sequence=None, sequence_start_seconds=0)}))
        controllers = [r for r in source['entities'] if 'forge.animation_sequence' in r['components']]
        controller, = [r for r in controllers if not r['components']['forge.animation_sequence'].get('sequence')]
        nested = len(controllers) > 1
        if collections:
            controller['components']['forge.animation_sequence'].update(duration=5 if skeletal else 10, loop=False)
            for row in source['entities']:
                player = row['components'].get('forge.animator' if skeletal else 'forge.transform_animator')
                if not player or not player.get('sequence'):
                    continue
                step = (.125 if skeletal else .25) if multiway else (.5 if skeletal else 1.)
                player['sequence_next_start_seconds'] = step
                player['sequence_multiway'] = multiway
                player['sequence_extra_clips'] = [dict(
                    clip=player['sequence_next_clip'] if i % 2 else player['clip'],
                    start_seconds=step*i) for i in range(2, 8)]
        if rated:
            for child in controllers:
                placement = child['components']['forge.animation_sequence']
                if placement.get('sequence'):
                    placement['sequence_rate'] = 2
        if child_loop:
            controller['components']['forge.animation_sequence']['loop'] = False
            for child in controllers:
                placement = child['components']['forge.animation_sequence']
                if placement.get('sequence'):
                    placement['sequence_loop'] = True
                    if placements:
                        placement['sequence_extra_placements'] = [dict(
                            start_seconds=4.5, rate=.5, loop=False)]
        controller['components']['project.sequence_probe'] = dict(ticks=0, time=0, blend=0, **{'$forge': stamp})
        shot_tracks = sorted([r['components']['forge.camera_shot'] for r in source['entities']
                              if 'forge.camera_shot' in r['components']], key=lambda r: r['start_seconds'])
        transition = scene_name == '68-sequence-camera-shots.scene.json'
        if transition:
            by_id = {row['id']: row for row in source['entities']}
            first_camera = by_id[shot_tracks[0]['camera']['entity']]['components']['forge.camera']
            second_camera = by_id[shot_tracks[1]['camera']['entity']]['components']['forge.camera']
            for name in ('background_r', 'background_g', 'background_b', 'background_a'):
                second_camera[name] = first_camera[name]
            shot_tracks[1]['blend_in_seconds'] = .75
        dt = struct.unpack('f', struct.pack('f', 1/60))[0]

        def rows(state):
            return {row['id']: row['components'] for row in state['effective_scene']['entities']}

        def nested_time(expected, for_camera=False):
            if placements and expected >= 4.5:
                local = (expected - 4.5) * .5
                return local if for_camera else min(6, local)
            unwrapped = max(0, (expected - 1) * (2 if rated else 1))
            local = unwrapped % 6 if child_loop else unwrapped if for_camera else min(6, unwrapped)
            if child_loop and expected == controller['components']['forge.animation_sequence']['duration'] and unwrapped > 0 and local == 0:
                local = 6
            return local

        def check_camera(state, expected):
            if not shot_tracks:
                return
            local = (nested_time(expected, True) if expected >= 1 else -1) if nested else expected
            target = next((s['camera']['entity'] for s in shot_tracks
                           if s['start_seconds'] <= local < s['end_seconds'] or
                           (nested and local == 6 and s['end_seconds'] == 6)), None)
            if target is None:
                target, = [r['id'] for r in source['entities']
                           if r['components'].get('forge.camera', {}).get('enabled')]
            active = [key for key, c in rows(state).items() if c.get('forge.camera', {}).get('enabled')]
            assert active == [target], (scene_name, expected, active, target)

        def check(state, tick):
            assert not any(d['severity'].lower() in ('error', 'fatal')
                           for d in state['diagnostics']), state['diagnostics']
            values = rows(state)
            probe = values[controller['id']]['project.sequence_probe']
            expected = {1: 2.5, 2: .5+dt, 3: .5+2*dt, 4: (0 if controller['components']['forge.animation_sequence']['loop'] else
                            controller['components']['forge.animation_sequence']['duration']), 5: dt,
                        6: .75}.get(tick, .75+(tick-6)*dt)
            assert probe['ticks'] == tick and abs(probe['time']-expected) < 2e-6, (tick, probe, expected)
            assert probe['blend'] == ((.75 if tick < 3 else 1) if transition else 0), (tick, probe)
            check_camera(state, expected)
            if skeletal:
                actor = next(row for row in state['effective_scene']['entities']
                             if row['id'] == skeletal_actor)
                player = actor['components']['forge.animator']
                assert player['sequence'] == binding and player['sequence_start_seconds'] == 0
                assert player['sequence_next_clip'] == player['clip']
                assert player['sequence_next_start_seconds'] == (.125 if multiway else .5)
                assert not player['play_on_start'] and player['loop'] and player['playback_speed'] == 0
                status = actor['animation_playback']
                if mixed:
                    properties = actor['components']['forge.transform_animator']
                    assert status['clip'] == properties['clip'] and status['blend_weight'] == 0
                    assert abs(status['time'] - max(0, min(2, expected-1))) < 2e-6
                    # Native row status preserves the public transform-first observation.
                    # The SDK Play watch below checks both typed observations.
                    status = None
                if status is not None:
                    duration = 1.875 if multiway else 4.5 if collections else 1.5
                    assert status['duration'] == duration and status['speed'] == 1 and not status['loop']
                    assert abs(status['time'] - min(expected, duration)) < 2e-6
                    active = max(0, min(6, int(status['time']*2)-1)) if collections else 0
                    weight = max(0, min(1, (status['time']-.5*(active+1))*2))
                    if multiway:
                        weights, active, incoming, weight = multiway_layers(status['time'], 1., .125)
                        assert player['sequence_multiway'] is True
                        expected_z = 2*sum(value*max(0., min(1., status['time']-.125*index))
                                           for index, value in enumerate(weights))
                        assert abs(actor['animation_pose']['local'][0]['translation'][2]-expected_z) < .003
                    assert len(player.get('sequence_extra_clips', [])) == (6 if collections else 0)
                    assert abs(status['blend_weight']-weight) < 2e-6
            if transition:
                # Native C++ updates only the duration. The actual copied view
                # uses that value while preserving both native/local Camera poses.
                target = next(row for row in state['effective_scene']['entities']
                              if row['id'] == shot_tracks[1]['camera']['entity'])
                if 2 <= expected < 2 + probe['blend']:
                    cameras = {row['id']: row for row in source['entities']}
                    a = cameras[shot_tracks[0]['camera']['entity']]['components']['forge.local_translation']
                    b = cameras[shot_tracks[1]['camera']['entity']]['components']['forge.local_translation']
                    linear = (expected - 2) / probe['blend']
                    weight = linear*linear*(3-2*linear)
                    for index, name in ((3, 'x'), (7, 'y'), (11, 'z')):
                        assert abs(target['world_affine'][index] - (a[name]+(b[name]-a[name])*weight)) < 2e-6
                    assert target['components']['forge.local_translation'] == b
            if nested:
                expected = nested_time(expected)
            for actor in source['entities']:
                if 'forge.transform_animator' not in actor['components']:
                    continue
                actual = values[actor['id']]
                if 'forge.camera' in actual:
                    u = min(1., max(0., expected / 3.5))
                    z = -10+2*u*u*(3-2*u)
                    assert abs(actual['forge.local_translation']['z']-z) < 2e-6, (tick, actual, z)
                elif 'forge.light' in actual:
                    u = min(1., max(0., (expected-1)/2))
                    intensity = .8+2.2*u*u*(3-2*u)
                    assert abs(actual['forge.light']['intensity']-intensity) < 2e-6
                elif actor['components']['forge.transform_animator'].get('sequence_next_clip'):
                    if multiway:
                        weights, _, _, _ = multiway_layers(expected, 2., .25)
                        x = sum(weight * ((3-2*max(0., min(2., expected-.25*index))) if index % 2
                                          else (-3+2*max(0., min(2., expected-.25*index))))
                                for index, weight in enumerate(weights))
                        assert actor['components']['forge.transform_animator']['sequence_multiway'] is True
                    else:
                        active = max(0, min(6, int(expected)-1)) if collections else 0
                        at = min(1., max(0., (expected-active)/2))
                        bt = min(1., max(0., (expected-active-1)/2))
                        a = 3-4*at if active % 2 else -3+4*at
                        b = -3+4*bt if active % 2 else 3-4*bt
                        weight = min(1., max(0., expected-active-1))
                        x = a*(1-weight) + b*weight
                    assert abs(actual['forge.local_translation']['x']-x) < 2e-6, (tick, actual, x)
                else:
                    u = min(1., max(0., (expected-.5)/2))
                    x = -2+4*u*u*(3-2*u)
                    assert abs(actual['forge.local_translation']['x']-x) < 2e-6, (tick, actual, x)

        first = Worker(runtime, root, arguments=('--audio', 'offline'))
        recovered = []
        try:
            state = first.request('replace', scene=source)
            deadline = time.monotonic()+20
            while rows(state)[controller['id']]['project.sequence_probe']['ticks'] == 0:
                assert time.monotonic() < deadline, dict(timing=state.get('timing'),
                                                           diagnostics=state.get('diagnostics'))
                time.sleep(.002)
                state = first.request('step')
            for tick in range(1, 25):
                if tick > 1:
                    state = first.request('step')
                check(state, tick)
                for other in recovered:
                    restored = other.request('step')
                    check(restored, tick)
                    assert rows(restored) == rows(state), (tick, rows(restored), rows(state))
                if tick in (1, 4, 6, 12):
                    checkpoint = state['recovery']
                    assert checkpoint is not None
                    if collections:
                        assert checkpoint['animation']['version'] == (15 if multiway else 12)
                        if multiway:
                            assert checkpoint['animation']['sequence_mixing_profile'] == 12
                        assert checkpoint['animation']['sequence_arrangement_profile'] == (10 if skeletal else 6)
                    elif skeletal:
                        assert checkpoint['animation']['version'] == 10
                    if rated:
                        assert checkpoint['animation']['version'] == (14 if placements else 13 if child_loop else 11)
                        assert checkpoint['animation']['sequence_rate_profile'] == 7
                    if child_loop:
                        assert checkpoint['animation']['sequence_loop_profile'] == 11
                    if placements:
                        assert checkpoint['animation']['sequence_placement_profile'] == 13
                    other = Worker(runtime, root, arguments=('--audio', 'offline'))
                    restored = other.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    recovered.append(other)
                    check(restored, tick)
                    assert rows(restored) == rows(state)
            assert source_path.read_bytes() == original, 'C++ sequence playback rewrote authored source'
        finally:
            first.close()
            for other in recovered:
                other.close()

        class EditorWorker(Worker):
            def __init__(self):
                super().__init__(runtime, root, arguments=('--sdk-play', 'on', '--audio', 'offline',
                                                          '--user-data', str(root / 'users')))

        editor = EditorWorker()
        try:
            state = editor.request('replace', scene=source)
            deadline = time.monotonic()+20
            while state.get('candidate') is None:
                assert time.monotonic() < deadline, dict(loading=state.get('loading'),
                                                           diagnostics=state.get('diagnostics'))
                time.sleep(.005)
                state = editor.request('snapshot')
            ticket = state['candidate']['ticket']
            envelope = editor.request('candidate', ticket=ticket)
            assert envelope.get('candidate')
            state = editor.request('snapshot', editor_epoch=dict(epoch=1, captured=False,
                        input_device='KeyboardMouse'), candidate_ack=dict(session=editor.session,
                        ticket=ticket, accepted=True, diagnostic=''))
            while state.get('activation', {}).get('state') not in ('ready', 'active'):
                assert time.monotonic() < deadline, state.get('activation')
                time.sleep(.005)
                state = editor.request('snapshot')
            for tick in range(1, 25):
                check(editor.request('step'), tick)
            ref = dict(scene=source['asset_id'], entity=controller['id'])
            watched = editor.request('snapshot', animation_watch=ref)
            assert watched['animation_playback']['sequence_controller']
            generation = watched['activation']['generation']
            editor.request('animation_playback', generation=generation, source=ref, operation='pause')
            seek_seconds = .75 if skeletal else 4.5 if shot_tracks else 1.5
            sought = editor.request('animation_playback', generation=generation, source=ref,
                                    operation='seek', position_seconds=seek_seconds, animation_watch=ref)
            assert abs(sought['animation_playback']['time']-seek_seconds) < 1e-12
            check_camera(sought, seek_seconds)
            assert not sought['animation_playback']['playing']
            if skeletal:
                actor_ref = dict(scene=source['asset_id'], entity=skeletal_actor)
                track_status = editor.request('snapshot', animation_watch=actor_ref)['animation_playback']
                if mixed:
                    assert track_status['component'] == 'forge.transform_animator'
                    assert track_status['sequence_clips']['clip'] == target['components'][
                        'forge.transform_animator']['clip']
                    assert track_status['players']['forge.transform_animator']['sequence_clips'] == track_status['sequence_clips']
                    track_status = track_status['players']['forge.animator']
                assert track_status['sequence'] == binding
                if multiway:
                    player = next(row['components']['forge.animator'] for row in source['entities'] if row['id'] == skeletal_actor)
                    check_multiway_watch(track_status, player, seek_seconds, 1., .125)
                else:
                    assert track_status['blend_weight'] == .5
                    assert track_status['sequence_clips']['next']['weight'] == .5
                    assert track_status['sequence_clips']['next']['clip'] == skeletal_clip
            pair_actor = next((r for r in source['entities'] if r['components'].get(
                'forge.transform_animator', {}).get('sequence_next_clip')), None)
            if pair_actor is not None:
                actor_ref = dict(scene=source['asset_id'], entity=pair_actor['id'])
                track_status = editor.request('snapshot', animation_watch=actor_ref)['animation_playback']
                if multiway:
                    check_multiway_watch(track_status, pair_actor['components']['forge.transform_animator'],
                                         seek_seconds, 2., .25)
                else:
                    assert track_status['blend_weight'] == .5, track_status
                    assert track_status['sequence_clips']['next']['weight'] == .5, track_status
                    assert track_status['sequence_clips']['next']['clip'] == pair_actor['components'][
                        'forge.transform_animator']['sequence_next_clip'], track_status
            restarted = editor.request('animation_playback', generation=generation, source=ref,
                                       operation='restart', animation_watch=ref)
            assert restarted['animation_playback']['time'] == 0
            assert restarted['animation_playback']['playing']
            check_camera(restarted, 0)
        finally:
            editor.close()
        print('SDK sequence passed:', label, flush=True)
print('Exact C++ object/skeletal/eight-clip/nested-loop/camera-shot controls and typed fields, four recoveries per scene and SDK Play PASS')
