"""Ordinary C++ animated targets, actual shared-runtime IK and recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker
runtime,fixture,converter,source,module=(Path(sys.argv[i]).resolve() for i in (1,2,4,5,6))
worker=sys.argv[3] if sys.argv[3]=='--direct' else str(Path(sys.argv[3]).resolve())
prepared=subprocess.run([fixture,runtime.parent/'rig-sdk-data',worker,converter,source,'--prepare'],capture_output=True,text=True,encoding='utf-8',timeout=120)
assert prepared.returncode==0,(prepared.stdout,prepared.stderr)
root=Path(prepared.stdout.strip().splitlines()[-1]);shutil.copy2(module,root/module.name)
fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2,name='Rig SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.animation_rig_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=module.name,dependencies=['forge.animation'])])),encoding='utf-8')
declaration,=json.loads(subprocess.check_output([runtime,'--inspect-sdk',root],text=True))['components']
stamp=dict(format='forge.authored-component',version=1,module=declaration['module'],schema_version=declaration['schema_version'],digest=declaration['digest'])
scene=json.loads((root/'main.scene.json').read_text(encoding='utf-8'))
actor,=[r for r in scene['entities'] if 'forge.animator' in r['components']]
actor['components']['project.rig_probe']=dict(ticks=0,**{'$forge':stamp})
def row(s):return next(r for r in s['effective_scene']['entities'] if 'forge.animator' in r['components'])
dt=struct.unpack('f',struct.pack('f',1/60))[0]
a=Worker(runtime,root);restored=[]
try:
 s=a.request('replace',scene=scene);deadline=time.monotonic()+15
 while s['recovery'] is None or not row(s).get('animation_pose'):
  assert time.monotonic()<deadline,s
  time.sleep(.002);s=a.request('snapshot')
 for tick in range(1,25):
  s=a.request('step');r=row(s);p=r['animation_pose'];c=r['components']['forge.two_bone_ik']
  assert r['components']['project.rig_probe']['ticks']==tick
  assert abs(p['time']-tick*dt)<3e-6,p
  assert abs(c['target_x']-(.7+tick*.005))<3e-6,c
  index=p['joint_assets'].index(c['end']);m=p['model'][index]
  assert all(abs(m[12+i]-c['target_'+axis])<.003 for i,axis in enumerate('xyz')),(tick,m,c)
  # Prove the same correction reached ordinary ECS transforms used by rendering/physics.
  middle_id=c['middle'];written=next(e for e in s['effective_scene']['entities'] if e['components'].get('forge.model_source',{}).get('node')==middle_id)
  q=p['local'][p['joint_assets'].index(middle_id)]['rotation']
  assert all(abs(written['components']['forge.local_rotation'][axis]-q[i])<3e-6 for i,axis in enumerate('xyzw'))
  for b in restored:assert row(b.request('step'))['animation_pose']==p
  if tick in (4,11,17):
   cp=s['recovery'];assert cp is not None
   b=Worker(runtime,root);t=b.request('replace',scene=cp['scene'],recovery=cp,recovery_session=cp['session'],recovery_tick=cp['tick'])
   recovered=row(t)
   assert recovered['animation_pose']==p,dict(tick=tick,expected_target=c,actual_target=recovered['components'].get('forge.two_bone_ik'),probe=recovered['components'].get('project.rig_probe'),fields={k:(p[k],recovered['animation_pose'].get(k)) for k in p if p[k]!=recovered['animation_pose'].get(k)})
   restored.append(b)
finally:
 a.close()
 for b in restored:b.close()
print('Actual C++ animated IK targets, exact phase/ECS writes and three fresh shared-worker recoveries passed')
