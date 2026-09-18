"""Private UI protocol: autonomous clock, duplicate actions and process recovery."""
import copy,json,shutil,subprocess,sys,time,uuid
from pathlib import Path
runtime=Path(sys.argv[1]).resolve()
sdk=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
root=runtime.parent/('ui-process-'+str(uuid.uuid4()));root.mkdir()
asset=str(uuid.uuid4())
(root/'hud.rml').write_text('<rml><head></head><body>Tick {{tick}}</body></rml>')
(root/'forge.assets.json').write_text(json.dumps(dict(version=1,assets=[dict(id=asset,type='ui_document',source='hud.rml',schema_version=1,dependencies=[],metadata={})])))
scene=dict(version=1,entities=[dict(id='hud',name='HUD',components={'forge.ui_document':dict(document=asset,enabled=True,visible=True,layer=0)})])
if sdk:
 shutil.copy2(sdk,root/sdk.name)
 fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
 (root/'forge.project.json').write_text(json.dumps(dict(version=2,name='UI SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.ui_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=sdk.name,dependencies=['forge.ui','forge.input'])])))
class Worker:
 def __init__(self):
  self.p=subprocess.Popen([runtime,'--sdk-project' if sdk else '--project',root,'--ui','on'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);self.session='';self.sequence=0
 def request(self,command,ok=True,**extra):
  self.sequence+=1;self.p.stdin.write(json.dumps(dict(protocol=2,id=self.sequence,session=self.session,command=command,**extra))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,self.p.stderr.read();r=json.loads(line);self.session=r['session'];assert r['ok']==ok,r;return r
 def close(self):
  if self.p.poll() is None:
   try:self.request('quit');self.p.wait(timeout=5)
   finally:
    if self.p.poll() is None:self.p.kill();self.p.wait()
def ui_command(frame,name,index):
 u=frame['ui'];return dict(version=1,session=u['session'],generation=u['generation'],id=index,instance=u['documents'][0]['instance'],command=name)
def restore(w,c):return w.request('replace',scene=c['scene'],recovery=c,recovery_session=c['session'],recovery_tick=c['tick'])
a=Worker();b=None
try:
 a.request('hello');frame=a.request('replace',scene=scene);assert len(frame['ui']['documents'])==1
 command=ui_command(frame,'Step',1)
 stepped=a.request('ui',ui_command=command);assert stepped['ui_ack']['ok'] and stepped['timing']['tick']==1
 retry=a.request('ui',ui_command=command);assert retry['ui_ack']==stepped['ui_ack'] and retry['timing']['tick']==1
 time.sleep(.02);assert a.request('snapshot')['timing']['tick']==1
 next_id=2
 if sdk:
  assert stepped['ui']['documents'][0]['model']['health']==100,stepped
  health=ui_command(stepped,'DecreaseHealth',next_id);next_id+=1
  queued=a.request('ui',ui_command=health);assert queued['ui_ack']['ok'] and queued['ui']['documents'][0]['model']['health']==100
  a.request('ui',ui_command=health)
  changed=a.request('step');assert changed['ui']['documents'][0]['model']['health']==90,changed
 resume=ui_command(frame,'Resume',next_id);next_id+=1
 a.request('ui',ui_command=resume);time.sleep(.07) # No presenter requests while runtime advances.
 paused=a.request('ui',ui_command=ui_command(frame,'Pause',next_id));assert paused['timing']['paused'] and paused['timing']['tick']>1
 checkpoint=paused['recovery'];old=copy.deepcopy(command)
 rebuilt=restore(a,checkpoint);assert rebuilt['ui']['generation']!=frame['ui']['generation']
 a.request('ui',ok=False,ui_command=old);assert a.request('snapshot')['timing']['tick']==rebuilt['timing']['tick']
 a.p.kill();a.p.wait();b=Worker();b.request('hello');recovered=restore(b,checkpoint)
 assert recovered['ui']['session']!=frame['ui']['session'] and recovered['ui']['documents']
 b.request('ui',ok=False,ui_command=old)
 assert recovered['timing']['paused']
 hidden=copy.deepcopy(recovered['scene']);hidden['entities'][0]['components']['forge.ui_document']['visible']=False
 frame=b.request('replace',scene=hidden)
 ack=b.request('ui',ui_command=ui_command(frame,'Resume',1));assert not ack['ui_ack']['ok'] and ack['timing']['paused']
 print('UI autonomous runtime, pause/step, correlated commands, duplicate suppression and recovery passed')
finally:
 a.close()
 if b:b.close()
 shutil.rmtree(root)
