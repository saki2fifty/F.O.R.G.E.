"""Private runtime checkpoint boundary, rejection, paused recovery and isolation."""
import copy,json,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from forge_native import Runtime
runtime=Path(sys.argv[1]).resolve()
def body(name,y,motion):
 return dict(id=name,name=name,components={'forge.position':dict(x=0,y=y,z=0),'forge.physics_body':dict(motion=motion,density=1000,mass=0,friction=.5,restitution=0,gravity_factor=1),'forge.box_collider':dict(x=1,y=1,z=1)})
scene=dict(version=1,entities=[body('floor',-.5,0),body('cube',5,2)])
scene['entities'][0]['components']['forge.box_collider']=dict(x=20,y=1,z=20)
def replace(worker,state):
 c=state['recovery'];return worker.request('replace',scene=state['scene'],recovery=c,recovery_session=c['session'],recovery_tick=c['tick'])
def height(state):
 return next(e['components']['forge.local_translation']['y'] for e in state['scene']['entities'] if e['name']=='cube')
a=Runtime(runtime);b=Runtime(runtime)
try:
 initial=a.request('replace',scene=scene)
 for _ in range(12):state=a.request('step')
 assert height(state)<5
 restored=replace(b,state)
 assert restored['timing']['paused'] and restored['timing']['tick']==12
 time.sleep(.05)
 assert b.request('snapshot')['timing']['tick']==12
 assert height(restored)==height(state)
 for _ in range(12):
  left=a.request('step');right=b.request('step')
  assert abs(height(left)-height(right))<1e-8
 # Candidate failure keeps prior live world; never pose-only recovery.
 for field in ('version','session','tick','scene','physics','integrity'):
  bad=copy.deepcopy(right);bad['recovery'][field]=None
  try:replace(b,bad)
  except RuntimeError:pass
  else:raise AssertionError(field)
  current=b.request('snapshot');assert current['timing']['tick']==24 and height(current)==height(right)
 # Caller must name the expected originating session and completed boundary.
 for field,value in [('recovery_session','wrong'),('recovery_tick',23)]:
  args=dict(scene=right['scene'],recovery=right['recovery'],recovery_session=right['recovery']['session'],recovery_tick=24);args[field]=value
  try:b.request('replace',**args)
  except RuntimeError:pass
  else:raise AssertionError(field)
 # Probe does not advance/change original. Repeated restoration keeps physics velocity.
 checkpoint=right
 for _ in range(3):
  b.close();b=Runtime(runtime);restored=replace(b,checkpoint)
  assert restored['timing']['tick']==24
  stepped=b.request('step');assert stepped['timing']['tick']==25 and height(stepped)<height(restored)
 assert a.request('snapshot')['timing']['tick']==24
 b.request('resume');time.sleep(.08);running=b.request('pause');assert running['timing']['tick']>25
 # Rejected recovery can be followed explicitly by clean Play, with authored location.
 clean=b.request('replace',scene=initial['scene']);assert clean['timing']['tick']==0 and height(clean)==5
 assert clean['physics']['tick']==0
finally:a.close();b.close()
print('Coherent physics recovery, velocity, pause/step/resume, isolation, rejection and clean restart passed')
