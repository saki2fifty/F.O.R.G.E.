"""Actual native gameplay requests Scenes while capturing/replaying fixed input."""
from pathlib import Path
from contextlib import contextmanager
import argparse
import base64
import json
import shutil
import subprocess
import sys
import time
from runtime_client import Runtime, activate
from touch_example import touch_example

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('runtime', type=Path)
parser.add_argument('module', type=Path)
parser.add_argument('gallery', type=Path)
parser.add_argument('tools', type=Path, nargs='?')
parser.add_argument('--observe-seconds', type=float, default=10)
parser.add_argument('--movie', action='store_true')
arguments = parser.parse_args()
if not 1 <= arguments.observe_seconds <= 120:
    parser.error('Observation budget must be within 1..120 seconds')
runtime, module, gallery, tools = (arguments.runtime, arguments.module,
                                  arguments.gallery, arguments.tools)
scenes = ('Scenes/235-session-input-a.scene.json', 'Scenes/236-session-input-b.scene.json')
scene_a = '306f7bf0-c7cc-4733-9043-7c379d2afff2'
scene_b = 'b78ea324-266f-490e-94e1-8159d010302a'


@contextmanager
def launch(recording=False):
    with touch_example(runtime, module, gallery, Path(scenes[0]).name, 'project.session_input',
                       True, launch=False,
                       dependencies=['forge.input', 'forge.transforms', 'forge.game'],
                       sources=scenes) as (source, scene):
        project = source
        if tools:
            project = source.parent / 'Relocated cooked content'
            result = subprocess.run([str(tools), '--assets', 'package', str(source), str(project),
                                     json.dumps(dict(platform='windows', backend='d3d12')),
                                     json.dumps([scene_a])], capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (result.stdout[:2000], result.stderr[-2000:])
            assert json.loads(result.stdout)['ok']
            shutil.copy2(source / 'forge.project.json', project / 'forge.project.json')
            shutil.copy2(source / module.name, project / module.name)
            assert not (project / 'Scenes').exists()
            assert not list(project.rglob('*.cpp')) and not list(project.rglob('*.hpp'))
            # The fixture retains its original bytes for its preservation check;
            # moving the root proves the runtime cannot read its authored paths.
            source.rename(source.with_name('Hidden authored source'))
        options = ['--sdk-project', project, '--sdk-play', 'on',
                   '--user-data', source.parent / 'Users']
        if recording:
            options += ['--recording', 'on', '--audio', 'offline']
        worker = Runtime(runtime, options)
        try:
            yield worker, scene
        finally:
            worker.close()
            if tools:
                source.with_name('Hidden authored source').rename(source)


def pose(state):
    return next(row['components']['forge.local_translation']['x']
                for row in state['scene']['entities'] if row['name'].startswith('Room '))


def traffic(worker, state):
    """Honor real platform effects and renderer candidate ownership."""
    acknowledgements = []
    for effect in state.get('platform_effects', []):
        assert effect['kind'] == 'cursor' and effect['value'] is False, effect
        acknowledgements.append(dict(effect, accepted=True, session=worker.session))
    fields = dict(platform_acks=acknowledgements) if acknowledgements else {}
    if state.get('release_required'):
        worker.observation_epoch += 1
        fields['editor_epoch'] = dict(epoch=worker.observation_epoch,
                                     captured=False, input_device='EditorHost')
    if state.get('candidate') and state['candidate']['ticket'] != getattr(worker, 'last_candidate', 0):
        ticket = state['candidate']['ticket']
        worker.request('candidate', ticket=ticket)
        fields['candidate_ack'] = dict(session=worker.session, ticket=ticket, accepted=True)
        worker.last_candidate = ticket
    return worker.request('snapshot', **fields)


def observe(worker, predicate):
    deadline = time.monotonic() + arguments.observe_seconds
    state = worker.request('snapshot')
    while not predicate(state):
        assert time.monotonic() < deadline, dict(loading=state.get('loading'),
                                                recording=state.get('input', {}).get('recording'),
                                                timing=state.get('timing'),
                                                activation=state.get('activation'))
        state = traffic(worker, state)
        time.sleep(.002)
    return state


def key(worker, control):
    worker.request('snapshot', input_events=[dict(control=control, value=1),
                                             dict(control=control, value=0)])


document = None
for replay in (False, True):
    with launch() as (worker, scene):
        try:
            initial = activate(worker, scene, True)
            generation = initial['activation']['generation']
            worker.observation_epoch = generation  # activate() reports release at this epoch.
            if replay:
                worker.request('input_recording', operation='replay', generation=generation,
                               document=document)
            else:
                worker.request('input_recording', operation='begin', generation=generation,
                               capture_scope='session')
            worker.request('resume')
            if not replay:
                key(worker, 'key.space')
                observe(worker, lambda state: pose(state) == -.5)
                key(worker, 'key.tab')
                observe(worker, lambda state: state['scene']['asset_id'] == scene_b)
                key(worker, 'key.space')
                observe(worker, lambda state: pose(state) == 1.5)
                key(worker, 'key.tab')
                observe(worker, lambda state: state['scene']['asset_id'] == scene_a)
                key(worker, 'key.space')
                final = observe(worker, lambda state: pose(state) == -.5)
                generation = final['activation']['generation']
                worker.request('pause')
                worker.request('input_recording', operation='finish', generation=generation)
                document = worker.request('input_recording', operation='read',
                                          generation=generation)['input_recording_document']
                assert [row['scene'] for row in document['segments']] == [scene_a, scene_b, scene_a]
                assert all(row['frames'] for row in document['segments'])
                document = json.loads(json.dumps(document))
            else:
                final = observe(worker, lambda state: state['input']['recording']['mode'] == 'finished')
                assert final['scene']['asset_id'] == scene_a and pose(final) == -.5, final['input']
                assert final['input']['recording']['consumed_ticks'] == sum(
                    len(segment['frames']) for segment in document['segments'])
                key(worker, 'key.tab')
                key(worker, 'key.space')
                for _ in range(10):
                    final = traffic(worker, final)
                    time.sleep(.002)
                assert final['scene']['asset_id'] == scene_a and pose(final) == -.5
            worker.quit()
        finally:
            worker.close()
print('Three native gameplay Scene visits, fixed-action replay, source relocation and both exits0 passed')
if arguments.movie:
    with launch(recording=True) as (worker, scene):
        state = activate(worker, scene, True)
        worker.observation_epoch = state['activation']['generation']
        frames = (sum(len(segment['frames']) for segment in document['segments']) + 59) // 60 + 2
        state = worker.request('record_begin', generation=worker.observation_epoch, fps=1,
                               frames=frames, capture_scope='session',
                               input_recording_document=document)
        assert state['input']['recording']['mode'] == 'replaying' and state['timing']['tick'] == 0
        total_bytes = 0
        pending = 0
        for frame in range(frames):
            deadline = time.monotonic() + arguments.observe_seconds
            while True:
                assert time.monotonic() < deadline, state.get('recording')
                state = worker.request('record_frame',
                                       generation=state['activation']['generation'], frame=frame)
                if 'recording_frame' in state:
                    audio = state['recording_frame']
                    pcm = base64.b64decode(audio['data'], validate=True)
                    assert audio['frame'] == frame and audio['start_audio_frame'] == total_bytes // 4
                    assert audio['audio_frames'] == 48000 and len(pcm) == 192000
                    assert not any(pcm), 'These Scene fixtures have no audio emitters'
                    total_bytes += len(pcm)
                    break
                pending += 1
                held = state['recording']['audio_frames']
                state = traffic(worker, state)
                assert state['recording']['audio_frames'] == held
        visits = state['recording']['visits']
        assert [visit['scene'] for visit in visits] == [scene_a, scene_b, scene_a], visits
        assert visits[0]['audio_frame'] == 0 and all(
            left['audio_frame'] <= right['audio_frame'] for left, right in zip(visits, visits[1:]))
        assert len({visit['generation'] for visit in visits}) == 3 and pending > frames
        assert state['recording']['complete'] and total_bytes == frames * 192000
        assert state['input']['recording']['mode'] == 'finished' and pose(state) == -.5
        worker.quit()
    print('Offline session movie replay, held preparation, complete PCM and native exit0 passed')
    with launch(recording=True) as (worker, scene):
        state = activate(worker, scene, True)
        worker.observation_epoch = state['activation']['generation']
        first_generation = worker.observation_epoch
        changed = json.loads(json.dumps(document))
        changed['segments'][1]['source'] = '0' * 64
        state = worker.request('record_begin', generation=first_generation, fps=1,
                               frames=frames, capture_scope='session',
                               input_recording_document=changed)
        deadline = time.monotonic() + arguments.observe_seconds
        rejected = False
        while not rejected:
            assert time.monotonic() < deadline, state.get('recording')
            try:
                state = worker.request('record_frame', generation=state['activation']['generation'],
                                       frame=state['recording']['completed_frames'])
            except RuntimeError as error:
                assert str(error).startswith('Offline recording failed:'), error
                assert 'Scene activation failed' in str(error), error
                rejected = True
            else:
                state = traffic(worker, state)
        state = worker.request('snapshot')
        assert state['activation']['generation'] == first_generation
        assert state['scene']['asset_id'] == scene_a and state['recording']['error']
        assert state['input']['recording']['mode'] == 'replaying'
        worker.quit()
    print('Later Scene source mismatch retained the active world and failed the movie with exit0')
