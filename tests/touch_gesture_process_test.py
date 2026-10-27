"""Gallery89 relative gestures and real fixed-tick C++ behavior in both Play hosts."""
import math
from pathlib import Path
import sys
from runtime_client import activate
from touch_example import touch_example, observe_touch

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
for sdk in (False, True):
    with touch_example(runtime, module, gallery, '89-touch-gestures.scene.json', 'project.touch_gestures', sdk) as (worker, original):
        def marker(state):
            return next(row['components'] for row in state['effective_scene']['entities']
                        if row['id'] == '15600000-1560-4156-8156-000000000002')
        def near(left, right):
            assert abs(left - right) < 1e-6, (sdk, left, right)
        activate(worker, original, sdk)
        worker.request('resume')
        worker.request('snapshot', input_events=[dict(control='touch.gesture.'+field, value=value)
            for field,value in [('active',1), ('pan_x',.125), ('pan_y',.25),
                                ('pinch',math.log(2)), ('rotation',math.pi/2), ('active',0)]])
        state = observe_touch(worker, lambda s: marker(s)['forge.local_translation']['x'] == 2)
        row = marker(state)
        near(row['forge.local_translation']['y'], -2.5)
        near(row['forge.local_scale']['x'], 2)
        near(row['forge.local_rotation']['z'], -math.sqrt(.5))
        near(row['forge.local_rotation']['w'], math.sqrt(.5))
        # Relative values apply once, even after many fixed ticks and a gesture end.
        tick = state['timing']['tick']
        later = observe_touch(worker, lambda s: s['timing']['tick'] >= tick+3)
        assert marker(later) == row
        worker.request('snapshot', input_events=[dict(control='touch.gesture.pinch', value=1000)])
        observe_touch(worker, lambda s: marker(s)['forge.local_scale']['x'] == 4)
        worker.request('snapshot', input_events=[dict(control='touch.gesture.pinch', value=-1000)])
        observe_touch(worker, lambda s: marker(s)['forge.local_scale']['x'] == .25)
        worker.request('snapshot', input_events=[dict(control='touch.gesture.pan_x', value=1000000),
                                               dict(control='touch.gesture.pan_y', value=-1000000)])
        observe_touch(worker, lambda s: marker(s)['forge.local_translation']['x'] == 32 and
                                       marker(s)['forge.local_translation']['y'] == 20)
        worker.request('pause')
        activate(worker, original, sdk)
        initial = marker(worker.request('snapshot'))
        assert initial['forge.local_translation'] == dict(x=0,y=0,z=0)
        assert initial['forge.local_scale'] == dict(x=1,y=1,z=1)
        assert initial['forge.local_rotation'] == dict(x=0,y=0,z=0,w=1)
        worker.request('quit');worker.process.wait(timeout=5)
        assert worker.process.returncode == 0
print('Gallery89 relative pan/log-pinch/rotation, public C++, both hosts, one-latch and neutral restart passed')
# The same installed example module also owns Gallery91's independent marker.
for sdk in (False, True):
    with touch_example(runtime, module, gallery, '91-single-touch-gestures.scene.json', 'project.touch_gestures', sdk) as (worker, original):
        def marker(state):
            return next(row['components'] for row in state['effective_scene']['entities']
                        if row['id'] == '16000000-1600-4160-8160-000000000002')
        activate(worker, original, sdk)
        worker.request('resume')
        def pulse(*names):
            before = worker.request('snapshot')
            action_ids = {row['name'].removeprefix('Single touch '):row['id'] for row in before['input']['actions'] if row['name'].startswith('Single touch ')}
            previous = {row['id']:row['presses'] for row in before['input']['actions']}
            worker.request('snapshot', input_events=[dict(control='touch.gesture.'+name,value=value)
                                                    for name in names for value in (1,0)])
            def consumed(state):
                observed = {row['id']:row['presses'] for row in state['input']['actions']}
                return all(observed[action_ids[name]] == previous[action_ids[name]]+1 for name in names)
            return marker(observe_touch(worker, consumed))
        row = pulse('tap')
        assert abs(row['forge.local_rotation']['z'] - math.sqrt(.5)) < 1e-6, (sdk, row)
        assert abs(row['forge.local_rotation']['w'] - math.sqrt(.5)) < 1e-6
        assert pulse('long_press')['forge.local_scale'] == dict(x=.5,y=.5,z=.5)
        assert pulse('long_press')['forge.local_scale'] == dict(x=1,y=1,z=1)
        assert pulse('swipe_right')['forge.local_translation'] == dict(x=1,y=0,z=0)
        assert pulse('swipe_down')['forge.local_translation'] == dict(x=1,y=-1,z=0)
        assert pulse('swipe_left')['forge.local_translation'] == dict(x=0,y=-1,z=0)
        row = pulse('swipe_up')
        assert row['forge.local_translation'] == dict(x=0,y=0,z=0)
        # A valid pulse applies once across subsequent running ticks. SDK Step
        # deliberately releases/neutralizes physical input; keep that owner policy.
        before = worker.request('snapshot')['timing']['tick']
        assert marker(observe_touch(worker, lambda s: s['timing']['tick'] >= before+3)) == row
        try:
            worker.request('snapshot', input_events=[dict(control='touch.gesture.tap',value=1),
                                                     dict(control='touch.gesture.double_tap',value=2)])
        except RuntimeError:
            pass
        else:
            raise AssertionError('Invalid digital pulse batch was accepted')
        before = worker.request('snapshot')['timing']['tick']
        assert marker(observe_touch(worker, lambda s: s['timing']['tick'] >= before+3)) == row
        row = pulse('tap','double_tap')
        assert row['forge.local_translation'] == dict(x=0,y=0,z=0)
        assert row['forge.local_scale'] == dict(x=1,y=1,z=1)
        assert row['forge.local_rotation'] == dict(x=0,y=0,z=0,w=1)
        pulse('tap','swipe_right','long_press')
        worker.request('pause')
        activate(worker, original, sdk)
        row = marker(worker.request('snapshot'))
        assert row['forge.local_translation'] == dict(x=0,y=0,z=0)
        assert row['forge.local_scale'] == dict(x=1,y=1,z=1)
        assert row['forge.local_rotation'] == dict(x=0,y=0,z=0,w=1)
        worker.request('quit');worker.process.wait(timeout=5)
        assert worker.process.returncode == 0
print('Gallery91 single-touch pulses, public C++, both hosts, one-latch/rejected batch/reset/restart passed')
