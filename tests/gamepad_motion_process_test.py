"""Original raw-motion Gallery through public SDK, including legacy unavailability."""
from pathlib import Path
import math
import subprocess
import sys
from runtime_client import activate
from touch_example import touch_example, observe_touch as observe

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
def sample(enabled=False, measured=False, value=None):
    return dict(supported=True,enabled=enabled,measured=measured,rate_hz=200,
                timestamp_ns=123456789 if measured else 0,
                sensor_timestamp_ns=987654321 if measured else 0,
                value=value if measured else [0,0,0])
def positions(state):
    entities = {row['id']:row['components']['forge.local_translation']
                for row in state['effective_scene']['entities']}
    return [entities[f'16800000-1680-4168-8168-{i:012}'] for i in (2,3)]

for sdk in (False, True):
    with touch_example(runtime,module,gallery,'93-gamepad-motion.scene.json',
                       'project.gamepad_motion',sdk) as (worker, original):
        activate(worker,original,sdk)
        assert positions(worker.request('step')) == [dict(x=-3,y=0,z=0),dict(x=3,y=0,z=0)]
        if not sdk:
            print('Ordinary simulation: Game motion unavailable, original markers remain neutral')
            continue
        topology = dict(available=True,revision=1,active='fixture:motion',devices=[
            dict(device='fixture:motion',name='Copied sensor fixture',slot=1,connected=True,rumble=False)])
        motion = dict(available=True,routed=False,revision=1,active='fixture:motion',devices=[
            dict(device='fixture:motion',slot=1,connected=True,accelerometer=sample(),gyroscope=sample())])
        worker.request('snapshot',gamepads=topology,gamepad_motion=motion)
        for kind in ('accelerometer','gyroscope'):
            state = observe(worker,lambda s: any(e['kind']=='gamepad_sensor' and e['value']['sensor']==kind
                                               for e in s.get('platform_effects',[])))
            effect = next(e for e in state['platform_effects'] if e['kind']=='gamepad_sensor' and e['value']['sensor']==kind)
            assert effect['value']['revision'] == topology['revision'] and effect['value']['enabled']
            topology['revision'] += 1
            motion['revision'] = topology['revision']
            motion['devices'][0][kind]['enabled'] = True
            ack = {k:effect[k] for k in ('token','sequence','epoch')}
            ack.update(session=worker.session,accepted=True,diagnostic='')
            worker.request('snapshot',gamepads=topology,gamepad_motion=motion,platform_acks=[ack])
        motion['routed'] = True
        motion['devices'][0]['accelerometer'] = sample(True,True,[2,9.80665,-1])
        motion['devices'][0]['gyroscope'] = sample(True,True,[-1.5,2.5,.25])
        state = worker.request('step',gamepad_motion=motion)
        markers = positions(state)
        for actual,expected in zip(markers,[(-2.6,1.96133),(1.5,2.5)]):
            assert math.isclose(actual['x'],expected[0],abs_tol=1e-9),markers
            assert math.isclose(actual['y'],expected[1],abs_tol=1e-9),markers
        assert state['timing']['paused']
        tick = state['timing']['tick']
        for _ in range(3):
            read = worker.request('snapshot')
            assert read['timing']['tick'] == tick and positions(read) == markers
        motion['routed'] = False
        motion['devices'][0]['accelerometer'] = sample(True)
        motion['devices'][0]['gyroscope'] = sample(True)
        state = worker.request('step',gamepad_motion=motion)
        assert positions(state) == [dict(x=-3,y=0,z=0),dict(x=3,y=0,z=0)]
        print('SDK host: current mode offers/acks, raw SI markers, paused reads and release/Step passed')

if len(sys.argv) == 5:
    with touch_example(runtime,module,gallery,'93-gamepad-motion.scene.json',
                       'project.gamepad_motion',True,launch=False) as (root, original):
        subprocess.run([sys.argv[4],str(runtime),str(root)],check=True,timeout=60)
