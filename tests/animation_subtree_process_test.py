"""Execute the ordinary C++ subtree helper and recover actual shared workers."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker
runtime, fixture, converter, source, module = (Path(sys.argv[i]).resolve() for i in (1,2,4,5,6))
worker = sys.argv[3] if sys.argv[3]=='--direct' else str(Path(sys.argv[3]).resolve())
prepared=subprocess.run([fixture,runtime.parent/'subtree-sdk-data',worker,converter,source,'--prepare'],capture_output=True,text=True,encoding='utf-8',timeout=120)
assert prepared.returncode==0,(prepared.stdout,prepared.stderr)
root=Path(prepared.stdout.strip().splitlines()[-1]);shutil.copy2(module,root/module.name)
fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2,name='Subtree SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.animation_subtree_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=module.name,dependencies=['forge.animation'])])),encoding='utf-8')
declaration,=json.loads(subprocess.check_output([runtime,'--inspect-sdk',root],text=True))['components']
stamp=dict(format='forge.authored-component',version=1,module=declaration['module'],schema_version=declaration['schema_version'],digest=declaration['digest'])
scene=json.loads((root/'main.scene.json').read_text(encoding='utf-8'))
actor,=[r for r in scene['entities'] if 'forge.animator' in r['components']]
upper=actor['components']['forge.animator']['blend_root'];actor['components']['project.subtree_probe']=dict(ticks=0,upper='',**{'$forge':stamp})
def row(s):return next(r for r in s['effective_scene']['entities'] if 'forge.animator' in r['components'])
dt=struct.unpack('f',struct.pack('f',1/60))[0]
a=Worker(runtime,root);restored=[]
try:
 s=a.request('replace',scene=scene);deadline=time.monotonic()+15
 while s['recovery'] is None or not row(s).get('animation_pose'):
  assert time.monotonic()<deadline,s
  time.sleep(.002);s=a.request('snapshot')
 for tick in range(1,25):
  s=a.request('step');r=row(s);p=r['animation_pose'];mask=None if 5<=tick<10 else upper
  assert r['components']['project.subtree_probe']['ticks']==tick
  assert r['components']['forge.animator']['blend_root']==mask
  assert abs(p['time']-tick*dt)<3e-6,p
  # Primary lasts 2s; secondary root moves 2m in1s at their shared normalized phase.
  assert abs(p['local'][0]['translation'][0]-(tick*dt if mask is None else 0))<.003,p
  rig=p['joint_assets'][0]
  written=next(r for r in s['effective_scene']['entities'] if r['components'].get('forge.model_source',{}).get('node')==rig)
  assert abs(written['components']['forge.local_translation']['x']-(tick*dt if mask is None else 0))<.003,(tick,written,p)
  for b in restored:
   t=b.request('step');assert row(t)['animation_pose']==p,(tick,row(t),r)
  if tick in (4,7,10):
   checkpoint=s['recovery'];assert checkpoint is not None
   b=Worker(runtime,root);t=b.request('replace',scene=checkpoint['scene'],recovery=checkpoint,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
   assert row(t)['animation_pose']==p;restored.append(b)
finally:
 a.close()
 for b in restored:b.close()
print('Actual C++ subtree switching retains phase and three shared-worker recoveries')
