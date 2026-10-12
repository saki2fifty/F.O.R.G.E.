"""Execute ordinary C++ mix helpers with real shared workers and fresh recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker
runtime, fixture, converter, source, module = (Path(sys.argv[i]).resolve() for i in (1,2,4,5,6))
worker=sys.argv[3] if sys.argv[3]=='--direct' else str(Path(sys.argv[3]).resolve())
prepared=subprocess.run([fixture,runtime.parent/'overlay-sdk-data',worker,converter,source,'--prepare'],capture_output=True,text=True,encoding='utf-8',timeout=120)
assert prepared.returncode==0,(prepared.stdout,prepared.stderr)
root=Path(prepared.stdout.strip().splitlines()[-1]);shutil.copy2(module,root/module.name)
fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
(root/'forge.project.json').write_text(json.dumps(dict(version=2,name='Overlay SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.animation_overlay_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=module.name,dependencies=['forge.animation'])])),encoding='utf-8')
declaration,=json.loads(subprocess.check_output([runtime,'--inspect-sdk',root],text=True))['components']
stamp=dict(format='forge.authored-component',version=1,module=declaration['module'],schema_version=declaration['schema_version'],digest=declaration['digest'])
scene=json.loads((root/'main.scene.json').read_text(encoding='utf-8'))
actor,=[r for r in scene['entities'] if 'forge.animator' in r['components']]
sidecar=json.loads((root/'Assets/OverlayColumn.gltf.forge-import.json').read_text(encoding='utf-8'))
upper=next(e['id'] for e in sidecar['identity']['entries'] if e['type']=='model_node' and e['display_name']=='Upper joint')
actor['components']['project.overlay_probe']=dict(ticks=0,upper=upper,**{'$forge':stamp})
def row(s):return next(r for r in s['effective_scene']['entities'] if 'forge.animator' in r['components'])
dt=struct.unpack('f',struct.pack('f',1/60))[0]
a=Worker(runtime,root);restored=[]
try:
 s=a.request('replace',scene=scene);deadline=time.monotonic()+15
 while s['recovery'] is None or not row(s).get('animation_pose'):
  assert time.monotonic()<deadline,s
  time.sleep(.002);s=a.request('snapshot')
 for tick in range(1,25):
  s=a.request('step');r=row(s);p=r['animation_pose'];seconds=tick*dt
  mode=0 if 5<=tick<10 else 1;weight=.5 if 10<=tick<20 else 1
  mask=upper if 15<=tick<20 else None
  assert r['components']['project.overlay_probe']['ticks']==tick
  assert r['components']['forge.animator']['blend_mode']==mode
  assert r['components']['forge.animator']['blend_root']==mask
  assert abs(p['time']-seconds)<3e-6,p
  # Base duration2, Overlay duration1, same phase; independently specified curves.
  base=(.25+.25*seconds,-.5*seconds);second=(.25+1.25*seconds,.25*seconds)
  w=0 if mask else weight
  expected=tuple(base[i]+w*(second[i]-(.25,0)[i]) if mode else (1-w)*base[i]+w*second[i] for i in (0,1))
  values=p['morphs'][0]['weights']
  assert all(abs(values[i]-expected[i])<3e-6 for i in (0,1)),(tick,values,expected)
  root_id=p['joint_assets'][0]
  written=next(x for x in s['effective_scene']['entities'] if x['components'].get('forge.model_source',{}).get('node')==root_id)
  assert abs(written['components']['forge.local_translation']['z']-(0 if mode==0 else seconds))<.003,(tick,written)
  for b in restored:
   t=b.request('step');assert row(t)['animation_pose']==p,(tick,row(t),r)
  if tick in (4,8,12,17):
   cp=s['recovery'];assert cp is not None
   b=Worker(runtime,root);t=b.request('replace',scene=cp['scene'],recovery=cp,recovery_session=cp['session'],recovery_tick=cp['tick'])
   assert row(t)['animation_pose']==p;restored.append(b)
finally:
 a.close()
 for b in restored:b.close()
print('Actual C++ additive/morph/mask switching retains phase and four fresh shared-worker recoveries')
