"""Exact-SDK C++ sequence controls, independent poses, and fresh-worker recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from physics_velocity_example_test import Worker

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
    for scene_name in ('62-coordinated-animation.scene.json', '68-sequence-camera-shots.scene.json',
                       '70-sequence-audio-cues.scene.json'):
        source_path = root / 'Scenes' / scene_name
        original = source_path.read_bytes()
        source = json.loads(original)
        controller, = [r for r in source['entities'] if 'forge.animation_sequence' in r['components']]
        controller['components']['project.sequence_probe'] = dict(ticks=0, time=0, **{'$forge': stamp})
        shot_tracks = sorted([r['components']['forge.camera_shot'] for r in source['entities']
                              if 'forge.camera_shot' in r['components']], key=lambda r: r['start_seconds'])
        dt = struct.unpack('f', struct.pack('f', 1/60))[0]

        def rows(state):
            return {row['id']: row['components'] for row in state['effective_scene']['entities']}

        def check_camera(state, expected):
            if not shot_tracks:
                return
            target = next((s['camera']['entity'] for s in shot_tracks
                           if s['start_seconds'] <= expected < s['end_seconds']), None)
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
            expected = {1: 2.5, 2: .5+dt, 3: .5+2*dt, 4: 0, 5: dt,
                        6: .75}.get(tick, .75+(tick-6)*dt)
            assert probe['ticks'] == tick and abs(probe['time']-expected) < 2e-6, (tick, probe, expected)
            check_camera(state, expected)
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
                else:
                    u = min(1., max(0., (expected-.5)/2))
                    x = -2+4*u*u*(3-2*u)
                    assert abs(actual['forge.local_translation']['x']-x) < 2e-6, (tick, actual, x)

        first = Worker(runtime, root)
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
                    other = Worker(runtime, root)
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
                self.process = subprocess.Popen([runtime, '--sdk-project', root, '--sdk-play', 'on',
                    '--audio', 'offline', '--user-data', root / 'users'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, text=True, cwd=root)
                self.sequence, self.session = 0, ''
                self.request('hello')

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
            seek_seconds = 4.5 if shot_tracks else 1.5
            sought = editor.request('animation_playback', generation=generation, source=ref,
                                    operation='seek', position_seconds=seek_seconds, animation_watch=ref)
            assert abs(sought['animation_playback']['time']-seek_seconds) < 1e-12
            check_camera(sought, seek_seconds)
            assert not sought['animation_playback']['playing']
            restarted = editor.request('animation_playback', generation=generation, source=ref,
                                       operation='restart', animation_watch=ref)
            assert restarted['animation_playback']['time'] == 0
            assert restarted['animation_playback']['playing']
            check_camera(restarted, 0)
        finally:
            editor.close()
print('Exact C++ object/camera-shot controls and typed fields, four recoveries per scene and SDK Play PASS')
