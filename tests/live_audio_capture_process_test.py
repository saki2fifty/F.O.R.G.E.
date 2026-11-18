"""Clocked live mixer capture through native IPC, including graceful child shutdown."""
import base64
from pathlib import Path
import sys
import time
from runtime_client import Runtime, activate
from touch_example import touch_example

runtime, module, gallery = map(Path, sys.argv[1:4])
action = '8c68fc99-bdb7-4bf4-9b91-dae060349ff0'

with touch_example(runtime, module, gallery, '209-input-recording.scene.json',
                   'project.input_messages', True, launch=False,
                   sources=('Scenes/209-input-recording.scene.json',)) as (project, scene):
    worker = Runtime(runtime, ['--sdk-project', project, '--sdk-play', 'on',
                               '--audio', 'null-test', '--user-data', project.parent / 'Users'])
    try:
        initial = activate(worker, scene, True)
        world = initial['activation']['generation']
        assert initial['runtime_contract']['live_audio_capture']
        begin = worker.request('audio_capture', operation='begin', generation=world)
        capture = begin['audio_capture']['generation']
        assert begin['audio_capture']['state'] == 'recording' and 'scene' not in begin
        cursor = 0

        def read():
            global cursor
            response = worker.request('audio_capture', operation='read', generation=world,
                                      capture_generation=capture, audio_frame=cursor,
                                      maximum_frames=4096)
            assert 'scene' not in response and 'effective_scene' not in response
            status, chunk = response['audio_capture'], response['audio_capture_chunk']
            pcm = base64.b64decode(chunk['data'], validate=True)
            assert chunk['format'] == 's16le-stereo-48000-v1'
            assert chunk['start_audio_frame'] == cursor and chunk['audio_frames'] <= 4096
            assert len(pcm) == chunk['audio_frames'] * 4 and not any(pcm)
            cursor += chunk['audio_frames']
            assert status['generation'] == capture and status['world_generation'] == world
            assert status['read_frames'] == cursor
            assert status['captured_frames'] - cursor == status['queued_frames']
            return status

        def observe(predicate):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                read()
                state = worker.request('snapshot')
                if predicate(state):
                    return state
                time.sleep(.005)
            raise AssertionError(state)

        worker.request('resume')
        worker.request('snapshot', input_events=[dict(control='key.space', value=1),
                                                 dict(control='key.space', value=0)])
        def input_observed(state):
            value = next(row for row in state['input']['actions'] if row['id'] == action)
            return value['presses'] == 1 and value['releases'] == 1 and cursor > 0
        observe(input_observed)
        paused = worker.request('pause')
        before = cursor
        still = observe(lambda state: cursor > before)
        assert still['timing']['tick'] == paused['timing']['tick']
        end = worker.request('audio_capture', operation='end', generation=world,
                             capture_generation=capture)['audio_capture']
        while end['queued_frames']:
            end = read()
        assert end['state'] == 'stopped' and end['captured_frames'] == cursor > 0
        resumed = worker.request('resume')
        assert not resumed['timing']['paused']
        restarted = worker.request('audio_capture', operation='begin', generation=world)
        assert restarted['audio_capture']['generation'] > capture
        # Quit while the observer is active: the native runtime must join its
        # callback and release the world/module rather than relying on kill cleanup.
        worker.quit(timeout=30)
    finally:
        worker.close()
print('Native live PCM, ordinary input/time controls, restart and active-capture quit0 passed')
