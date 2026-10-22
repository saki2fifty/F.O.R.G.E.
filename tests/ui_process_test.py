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
 paused=a.request('ui',ui_command=ui_command(frame,'Pause',next_id));next_id+=1
 assert paused['timing']['paused'] and paused['timing']['tick']>1
 assert 'TimeScale' in paused['ui']['documents'][0]['commands']
 held_command=ui_command(frame,'TimeScale',next_id);next_id+=1;held_command['value']='0'
 held=a.request('ui',ui_command=held_command);assert held['ui_ack']['ok'] and held['timing']['time_scale']==0
 assert a.request('ui',ui_command=held_command)['ui_ack']==held['ui_ack']
 resume=ui_command(frame,'Resume',next_id);next_id+=1
 held=a.request('ui',ui_command=resume);time.sleep(.08)
 observed=a.request('snapshot');assert observed['timing']['tick']==held['timing']['tick'] and not observed['timing']['paused']
 for invalid in ('-1','4.1','nan','1x',1,True,None):
  rejected=ui_command(frame,'TimeScale',next_id);next_id+=1;rejected['value']=invalid
  result=a.request('ui',ok=invalid is not None,ui_command=rejected)
  assert (not result['ok'] or not result['ui_ack']['ok']) and result['timing']['time_scale']==0
  if invalid is None:next_id-=1 # Malformed envelope was never admitted by CommandGate.
 restore_rate=ui_command(frame,'TimeScale',next_id);next_id+=1;restore_rate['value']='1'
 continued=a.request('ui',ui_command=restore_rate);assert continued['ui_ack']['ok']
 time.sleep(.08)
 paused=a.request('ui',ui_command=ui_command(frame,'Pause',next_id));next_id+=1
 assert paused['timing']['tick']>held['timing']['tick']
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
 if sdk:
  # The prepared Editor Play host has a separate candidate/admission boundary.
  # Exercise the actual matching module there as well as in the ordinary worker.
  from runtime_client import Runtime
  a.close();b.close()
  settings=json.loads((root/'forge.project.json').read_text())
  settings['game']=json.loads((Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery/forge.project.json').read_text())['game']
  settings['game']['application_id']='org.forge.ui-prepared-test'
  (root/'forge.project.json').write_text(json.dumps(settings))
  worker=Runtime(runtime,['--sdk-project',root,'--sdk-play','on','--user-data',root/'users','--ui','on'])
  def activate(source):
   pending=worker.request('replace',scene=source);deadline=time.monotonic()+10
   while not pending.get('candidate'):
    assert time.monotonic()<deadline,pending
    time.sleep(.005);pending=worker.request('snapshot')
   ticket=pending['candidate']['ticket']
   frozen=worker.request('candidate',ticket=ticket)['candidate']
   assert frozen['ui']['errors']==[],frozen['ui']
   live=worker.request('snapshot',editor_epoch=dict(epoch=ticket,captured=False,input_device='EditorHost'),
     candidate_ack=dict(session=worker.session,ticket=ticket,accepted=True,diagnostic=''))
   while live['activation']['generation']!=ticket:
    assert time.monotonic()<deadline,live
    time.sleep(.005);live=worker.request('snapshot')
   assert live['ui']['documents']==frozen['ui']['documents'],(live['ui'],frozen['ui'])
   return live
  try:
   active=activate(scene)
   assert len(active['ui']['documents'])==1
   stepped=worker.request('ui',ui_command=ui_command(active,'Step',1))
   assert stepped['ui_ack']['ok'] and stepped['timing']['tick']==1
   assert stepped['ui']['documents'][0]['model']['health']==100
   decrease=ui_command(stepped,'DecreaseHealth',2)
   queued=worker.request('ui',ui_command=decrease)
   duplicate=worker.request('ui',ui_command=decrease)
   assert queued['ui_ack']==duplicate['ui_ack'] and queued['ui_ack']['ok']
   changed=worker.request('step')
   assert changed['ui']['documents'][0]['model']['health']==90,changed['ui']
   hidden=copy.deepcopy(scene);hidden['entities'][0]['components']['forge.ui_document']['visible']=False
   active=activate(hidden)
   rejected=worker.request('ui',ui_command=ui_command(active,'Resume',1))
   assert not rejected['ui_ack']['ok'] and rejected['timing']['paused']
   active=activate(dict(version=1,entities=[]))
   assert active['ui']['documents']==[] and active['ui']['errors']==[]
   assert worker.request('quit')['ok'] and worker.process.wait(timeout=5)==0
   print('Matching C++ UI module: prepared candidate, frozen/live parity, controls, duplicate action, hidden and empty scenes passed')
  finally:worker.close()
 print('UI autonomous runtime, pause/step, correlated commands, duplicate suppression and recovery passed')
finally:
 a.close()
 if b:b.close()
 shutil.rmtree(root)
