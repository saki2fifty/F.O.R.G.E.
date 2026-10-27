"""Original Gallery movement through public input/actions in both running hosts."""
from pathlib import Path
import json
import sys
from runtime_client import activate
from touch_example import touch_example as sdk_input_example, observe_touch as observe

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
defaults = json.loads((gallery/'forge.project.json').read_text())['game']['input']
for sdk in (False, True):
    with sdk_input_example(runtime, module, gallery, '92-multiple-gamepads.scene.json',
                           'project.multiple_gamepads', sdk) as (worker, original):
        ids = [f'16700000-1670-4167-8167-{i+2:012}' for i in range(4)]
        actions = [f'16700000-1670-4167-8167-{i+10:012}' for i in range(4)]
        # SDK/standalone Game settings override the authored binding's inner
        # deadzone. The ordinary simulation host reads the binding directly.
        inner = defaults['stick_deadzone'] if sdk else .2
        amount = (.6-inner)/(1-inner)
        vectors = ((amount,0),(0,-amount),(-amount,0),(0,amount))
        def positions(state):
            entities = {e['id']:e['components']['forge.local_translation']
                        for e in state['effective_scene']['entities']}
            return [entities[e] for e in ids]
        def moving(state):
            rows = {a['id']:a for a in state['input']['actions']}
            return all(abs(rows[a]['x']-x)<1e-9 and abs(rows[a]['y']-y)<1e-9
                       for a,(x,y) in zip(actions,vectors))
        activate(worker, original, sdk)
        initial = positions(worker.request('snapshot'))
        worker.request('resume')
        worker.request('snapshot', input_events=[dict(control=f'pad.{i+1}.{axis}',value=value)
            for i,(axis,value) in enumerate((('left_x',.6),('left_y',.6),('left_x',-.6),('left_y',-.6)))])
        first = observe(worker, moving)
        second = observe(worker,lambda s: s['timing']['tick'] >= first['timing']['tick']+6)
        elapsed = (second['timing']['tick']-first['timing']['tick']) / 60
        for before,after,velocity in zip(positions(first),positions(second),vectors):
            for axis,speed in zip(('x','y'),velocity):
                assert abs(after[axis]-before[axis]-3*speed*elapsed) < 1e-6, (sdk,before,after,elapsed)
        # Scoped neutral input stops only player 2; other held axes keep moving.
        worker.request('snapshot', input_events=[dict(control='pad.2.left_y',value=0)])
        first = observe(worker,lambda s: next(a for a in s['input']['actions'] if a['id']==actions[1])['y'] == 0)
        second = observe(worker,lambda s: s['timing']['tick'] >= first['timing']['tick']+4)
        assert positions(first)[1] == positions(second)[1]
        assert all(positions(first)[i] != positions(second)[i] for i in (0,2,3))
        # A rejected batch cannot partially release the other players.
        try:
            worker.request('snapshot', input_events=[dict(control='pad.3.left_x',value=0),
                                                     dict(control='pad.5.south',value=1)])
        except RuntimeError:
            pass
        else:
            raise AssertionError('Unknown player slot admitted')
        state=worker.request('snapshot')
        assert abs(next(a for a in state['input']['actions'] if a['id']==actions[2])['x']+amount)<1e-9
        worker.request('pause')
        activate(worker, original, sdk)
        assert positions(worker.request('snapshot')) == initial
        worker.request('quit'); worker.process.wait(timeout=5)
        assert worker.process.returncode == 0
print('Four independent radial actions, fixed movement, neutralization, invalid batches and restart passed in both hosts')
