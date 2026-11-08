"""Live SDK capture and fresh relocated replay through actual protocol2 child processes."""
import json
from pathlib import Path
import sys
import time
from runtime_client import activate
from touch_example import touch_example

runtime, module, gallery = map(Path, sys.argv[1:4])
action = '8c68fc99-bdb7-4bf4-9b91-dae060349ff0'


def pose(state):
    return [row['components']['forge.local_translation']['x']
            for row in state['effective_scene']['entities']
            if 'forge.input_message' in row['components']]


def observed(worker, predicate):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        state = worker.request('snapshot')
        if predicate(state):
            return state
        time.sleep(.005)
    raise AssertionError(state)


def action_state(state):
    return next(row for row in state['input']['actions'] if row['id'] == action)


def quit_cleanly(worker):
    worker.request('quit')
    assert worker.process.wait(timeout=10) == 0, worker.process.returncode


with touch_example(runtime, module, gallery, '209-input-recording.scene.json',
                   'project.input_messages', True) as (worker, scene):
    try:
        initial = activate(worker, scene, True)
        generation = initial['activation']['generation']
        worker.request('input_recording', operation='begin', generation=generation)
        worker.request('resume')
        worker.request('snapshot', input_events=[dict(control='key.space', value=1),
                                                 dict(control='key.space', value=0)])
        observed(worker, lambda state: action_state(state)['presses'] == 1)
        worker.request('snapshot', input_events=[dict(control='key.space', value=1)])
        observed(worker, lambda state: action_state(state)['presses'] == 2)
        worker.request('snapshot', input_events=[dict(reset=True)])
        observed(worker, lambda state: action_state(state)['releases'] == 2)
        worker.request('snapshot', input_events=[dict(control='key.space', value=1)])
        observed(worker, lambda state: action_state(state)['presses'] == 3)
        worker.request('pause')
        worker.request('input_recording', operation='finish', generation=generation)
        document = worker.request('input_recording', operation='read',
                                   generation=generation)['input_recording_document']
        assert document['frames'] and pose(worker.request('snapshot')) == [1.5]
        document = json.loads(json.dumps(document))
    finally:
        quit_cleanly(worker)

# A new private copy gives the same authored source a different absolute root.
with touch_example(runtime, module, gallery, '209-input-recording.scene.json',
                   'project.input_messages', True) as (worker, scene):
    try:
        initial = activate(worker, scene, True)
        generation = initial['activation']['generation']
        worker.request('input_recording', operation='replay', generation=generation,
                         document=document)
        contexts = {row['name']: row.get('phase', 'fixed')
                    for row in document['input_map'].get('contexts', [])}
        actions = [row for row in document['input_map']['actions']
                   if contexts.get(row.get('context'), 'fixed') != 'control']
        column = next(i for i, row in enumerate(actions) if row['id'] == action)
        presses = 0
        for frame in document['frames']:
            presses += bool(frame[column][2] & 2)
            state = worker.request('step', input_events=[dict(control='key.space', value=0)])
            assert pose(state) == [presses * .5], state
        assert presses == 3 and state['input']['recording']['mode'] == 'finished'
        neutral = worker.request('step')
        assert not action_state(neutral)['held'] and pose(neutral) == [1.5]
    finally:
        quit_cleanly(worker)
print('Live SDK input capture, fresh relocated step replay, native movement and both child exits0 passed')
