"""Headless fixed navigation, pause/step/resume and semantic recovery."""
import copy,json,shutil,subprocess,sys,time
from pathlib import Path
runtime,prepare,worker=map(lambda p:Path(p).resolve(),sys.argv[1:4])
sdk=Path(sys.argv[4]).resolve() if len(sys.argv)>4 else None
root=Path(subprocess.check_output([prepare,runtime.parent/'navigation-process-data',worker,'--prepare'],text=True).strip())
if sdk:
 shutil.copy2(sdk,root/sdk.name)
 fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
 (root/'forge.project.json').write_text(json.dumps(dict(version=2,name='Navigation SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.navigation_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=sdk.name,dependencies=['forge.navigation','forge.input'])])))
class Worker:
 def __init__(self):
  self.p=subprocess.Popen([runtime,'--sdk-project' if sdk else '--project',root],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);self.session='';self.sequence=0
 def request(self,command,**extra):
  self.sequence+=1;self.p.stdin.write(json.dumps(dict(protocol=2,id=self.sequence,session=self.session,command=command,**extra))+'\n');self.p.stdin.flush();line=self.p.stdout.readline();assert line,self.p.stderr.read();r=json.loads(line);self.session=r['session'];assert r['ok'],r;return r
 def close(self):
  if self.p.poll() is None:
   try:self.request('quit');self.p.wait(timeout=5)
   finally:
    if self.p.poll() is None:self.p.kill();self.p.wait()
def position(r):
 e=next(e for e in r['effective_scene']['entities'] if e['name']=='Agent');return e['world_affine'][3],e['world_affine'][7],e['world_affine'][11]
def restore(w,c):return w.request('replace',scene=c['scene'],recovery=c,recovery_session=c['session'],recovery_tick=c['tick'])
a=Worker();b=None
try:
 a.request('hello',simulation_hz=60);first=a.request('replace',scene=json.loads((root/'scene.json').read_text()));stepped=a.request('step');assert position(stepped)!=position(first)
 time.sleep(.03);assert position(a.request('snapshot'))==position(stepped)
 for _ in range(6):stepped=a.request('step')
 if sdk:assert any('navigation SDK queries passed' in d['text'] for d in stepped['diagnostics']),stepped
 checkpoint=stepped['recovery'];assert checkpoint['navigation']['version']==1 and len(checkpoint['navigation']['assets'])==1
 assert set(checkpoint['navigation'])=={'version','assets','agents'}
 a.request('resume');time.sleep(.06);paused=a.request('pause');assert paused['timing']['tick']>7
 rebuilt=restore(a,checkpoint);assert position(rebuilt)==position(stepped)
 a.p.kill();a.p.wait();b=Worker();b.request('hello',simulation_hz=60);rebuilt=restore(b,checkpoint);assert position(rebuilt)==position(stepped) and rebuilt['timing']['paused'];next_frame=b.request('step');assert position(next_frame)!=position(rebuilt)
 bad=copy.deepcopy(checkpoint);bad['navigation']['assets'][0]['sha256']='bad'
 try:restore(b,bad)
 except AssertionError:pass
 else:raise AssertionError('Invalid checkpoint accepted')
 assert position(b.request('snapshot'))==position(next_frame)
 print('Navigation headless pause/step/resume and crash recovery passed')
finally:
 a.close()
 if b:b.close()
 shutil.rmtree(root)
