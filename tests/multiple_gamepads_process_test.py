"""Original Gallery movement through public input/actions in both running hosts."""
from pathlib import Path
import json
import math
import sys
from runtime_client import activate
from touch_example import touch_example as sdk_input_example, observe_touch as observe

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
settings = json.loads((gallery/'forge.project.json').read_text())
defaults = settings['game']['input']
for scene_name,count in (('92-multiple-gamepads.scene.json',4), ('149-sixteen-controllers.scene.json',16)):
    for sdk in (False, True):
        with sdk_input_example(runtime, module, gallery, scene_name,
                               'project.multiple_gamepads', sdk) as (worker, original):
            ids = [row['id'] for row in original['entities'] if 'forge.sprite_renderer' in row['components']]
            assert len(ids) == count
            actions = [next(a['id'] for a in settings['input']['actions']
                            if a['bindings'][0]['control'] == f'pad.{i+1}.left_x') for i in range(count)]
            # SDK/standalone Game settings override the authored binding's inner
            # deadzone. The ordinary simulation host reads the binding directly.
            inner = defaults['stick_deadzone'] if sdk else .2
            raw = ((.6,0),(0,.6),(-.6,0),(0,-.6)) if count == 4 else [(.35+.01*i,.25+.01*i) for i in range(count)]
            vectors = []
            for x,y in raw:
                length = math.hypot(x,y)
                scale = (length-inner)/((1-inner)*length)
                vectors.append((x*scale,-y*scale))
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
                for i,values in enumerate(raw) for axis,value in zip(('left_x','left_y'),values)])
            first = observe(worker, moving)
            second = observe(worker,lambda s: s['timing']['tick'] >= first['timing']['tick']+6)
            elapsed = (second['timing']['tick']-first['timing']['tick']) / 60
            for before,after,velocity in zip(positions(first),positions(second),vectors):
                for axis,speed in zip(('x','y'),velocity):
                    assert abs(after[axis]-before[axis]-3*speed*elapsed) < 1e-6, (sdk,before,after,elapsed)
            # Scoped neutral input stops one player; every other held action keeps moving.
            stopped = 1 if count == 4 else 9
            worker.request('snapshot', input_events=[dict(control=f'pad.{stopped+1}.{axis}',value=0) for axis in ('left_x','left_y')])
            first = observe(worker,lambda s: all(next(a for a in s['input']['actions'] if a['id']==actions[stopped])[axis] == 0 for axis in ('x','y')))
            second = observe(worker,lambda s: s['timing']['tick'] >= first['timing']['tick']+4)
            assert positions(first)[stopped] == positions(second)[stopped]
            assert all(positions(first)[i] != positions(second)[i] for i in range(count) if i != stopped)
            # A rejected batch cannot partially release the other players.
            try:
                worker.request('snapshot', input_events=[dict(control='pad.3.left_x',value=0),
                                                         dict(control='pad.17.south',value=1)])
            except RuntimeError:
                pass
            else:
                raise AssertionError('Unknown player slot admitted')
            state=worker.request('snapshot')
            assert abs(next(a for a in state['input']['actions'] if a['id']==actions[2])['x']-vectors[2][0])<1e-9
            worker.request('pause')
            activate(worker, original, sdk)
            assert positions(worker.request('snapshot')) == initial
            worker.request('quit'); worker.process.wait(timeout=5)
            assert worker.process.returncode == 0
print('Four/sixteen independent radial actions, fixed movement, scoped neutralization, invalid batches and restart passed in both hosts')
