"""Fixed-clock animation presentation, pause/step, ABI1 reload and crash reconstruction."""
import copy,hashlib,json,shutil,subprocess,sys,time
from pathlib import Path
runtime,prepare,converter,source,module=map(lambda p:Path(p).resolve(),sys.argv[1:6])
sdk=Path(sys.argv[6]).resolve() if len(sys.argv)>6 else None
root=Path(subprocess.check_output([prepare,runtime.parent/'animation-process-data',converter,source,'--prepare'],text=True).strip())
for record in json.loads((root/'forge.assets.json').read_text())['assets']:
 if record['type'] in ('skeleton','animation_clip'):
  assert hashlib.sha256((root/record['source']).read_bytes()).hexdigest()==record['metadata']['artifact_sha256']
  assert hashlib.sha256(converter.read_bytes()).hexdigest()==record['metadata']['converter_sha256']
if sdk:
 shutil.copy2(sdk,root/sdk.name)
 fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
 project=dict(version=2,name='Animation SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.animation_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=sdk.name,dependencies=['forge.animation','forge.input'])])
 (root/'forge.project.json').write_text(json.dumps(project))
class Worker:
 def __init__(self):
  self.p=subprocess.Popen([runtime,'--sdk-project' if sdk else '--project',root],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  self.session='';self.sequence=0
 def request(self,command,**extra):
  self.sequence+=1;self.p.stdin.write(json.dumps(dict(protocol=2,id=self.sequence,session=self.session,command=command,**extra))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,self.p.stderr.read()
  r=json.loads(line);self.session=r['session'];assert r['ok'],r;return r
 def close(self):
  if self.p.poll() is None:
   try:self.request('quit');self.p.wait(timeout=5)
   finally:
    if self.p.poll() is None:self.p.kill();self.p.wait()
def pose(result):return result['effective_scene']['entities'][0]['animation_pose']
def restore(worker,checkpoint):
 return worker.request('replace',scene=checkpoint['scene'],recovery=checkpoint,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
a=Worker();b=None
try:
 a.request('hello',simulation_hz=60)
 first=a.request('replace',scene=json.loads((root/'scene.json').read_text()))
 assert pose(first)['time']==0
 if not sdk:a.request('load_module',path=str(module))
 stepped=a.request('step')
 if not sdk:assert stepped['activation']['state']=='active'
 assert abs(pose(stepped)['time']-(2 if sdk else 1)/60)<1e-6
 time.sleep(.03);assert pose(a.request('snapshot'))==pose(stepped)
 for _ in range(4):stepped=a.request('step')
 checkpoint=stepped['recovery'];assert checkpoint['animation']['version']==1
 assert 'model' not in json.dumps(checkpoint['animation'])
 a.request('resume');time.sleep(.05);paused=a.request('pause');assert paused['timing']['tick']>5
 assert pose(restore(a,checkpoint))==pose(stepped)
 # Simulate a runtime crash: editor-owned data is represented by the untouched checkpoint.
 a.p.kill();a.p.wait();b=Worker();b.request('hello',simulation_hz=60)
 rebuilt=restore(b,checkpoint);assert pose(rebuilt)==pose(stepped) and rebuilt['timing']['paused']
 next_frame=b.request('step');assert abs(pose(next_frame)['time']-(12 if sdk else 6)/60)<1e-6
 bad=copy.deepcopy(checkpoint);bad['animation']['entries'][0]['clip_revision']='invalid'
 try:restore(b,bad)
 except AssertionError:pass
 else:raise AssertionError('Invalid animation checkpoint accepted')
 assert pose(b.request('snapshot'))==pose(next_frame)
 print('Animation process pause/step/resume, ABI1 activation and crash recovery passed')
finally:
 a.close()
 if b:b.close()
 shutil.rmtree(root)
