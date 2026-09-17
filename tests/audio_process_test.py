"""No-device audio output policy, recovery, and optional exact SDK commands."""
import copy,json,math,shutil,struct,subprocess,sys,tempfile,time,wave
from pathlib import Path
runtime=Path(sys.argv[1]).resolve()
abi1=Path(sys.argv[3]).resolve() if len(sys.argv)>3 and sys.argv[2]=='--abi1' else None
module=Path(sys.argv[2]).resolve() if len(sys.argv)>2 and not abi1 else None
scene_id='11111111-1111-4111-8111-111111111111'
entity_id='22222222-2222-4222-8222-222222222222'
clip_id='33333333-3333-4333-8333-333333333333'
class Worker:
 def __init__(self,root,audio=True,sdk=False):
  args=[str(runtime),'--sdk-project' if sdk else '--project',str(root)]
  if audio:args+=['--audio','offline']
  self.p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  self.session='';self.sequence=0
 def request(self,command,**fields):
  self.sequence+=1
  self.p.stdin.write(json.dumps(dict(protocol=2,id=self.sequence,session=self.session,command=command,**fields))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,self.p.stderr.read()
  result=json.loads(line);self.session=result['session'];assert result['ok'],result;return result
 def close(self):
  if self.p.poll() is None:
   try:self.request('quit');self.p.wait(timeout=5)
   finally:
    if self.p.poll() is None:self.p.kill();self.p.wait()
with tempfile.TemporaryDirectory(dir=runtime.parent) as folder:
 root=Path(folder)
 with wave.open(str(root/'tone.wav'),'wb') as wav:
  wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(48000)
  wav.writeframes(b''.join(struct.pack('<h',int(2000*math.sin(i*.06))) for i in range(48000)))
 (root/'forge.assets.json').write_text(json.dumps(dict(version=1,assets=[dict(id=clip_id,type='audio_clip',source='tone.wav',schema_version=1,dependencies=[])])))
 scene=dict(version=3,asset_id=scene_id,entities=[dict(id=entity_id,name='Speaker',components={'forge.audio_source':dict(clip=clip_id,play_on_start=True,loop=True,gain=1,pitch=1,spatialized=False,minimum_distance=1,maximum_distance=100)})])
 if module:
  shutil.copy2(module,root/module.name)
  fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
  project=dict(version=2,name='Audio SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.audio_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=module.name,dependencies=['forge.audio','forge.input'])])
  (root/'forge.project.json').write_text(json.dumps(project))
  unavailable=subprocess.run([runtime,'--sdk-project',root],input='',capture_output=True,text=True,timeout=10)
  assert unavailable.returncode!=0 and 'service' in unavailable.stderr.lower(),unavailable.stderr
 worker=Worker(root,sdk=bool(module))
 try:
  hello=worker.request('hello',simulation_hz=60,gravity=[0,-3,0]);assert hello['audio']['output']=='offline'
  if not module:assert hello['physics']['gravity']==[0,-3,0]
  initial=worker.request('replace',scene=scene)
  assert initial['audio']['paused'] and initial['audio']['voices']==1
  assert 'audio' not in initial['scene'] and 'audio' not in initial['recovery']
  if abi1:
   loaded=worker.request('load_module',path=str(abi1));assert loaded['activation']['state']=='loaded_pending_first_tick'
  before=initial['timing']['tick'];time.sleep(.04)
  assert worker.request('snapshot')['timing']['tick']==before
  for n in range(1,5):
   stepped=worker.request('step');assert stepped['timing']['tick']==n and stepped['audio']['paused']
   if abi1:assert stepped['activation']['state']=='active'
   if module:
    assert stepped['audio']['playing_sources']==n%2,(n,stepped['audio'],stepped['diagnostics'])
    assert stepped['scene']['entities'][0]['components']['forge.audio_source']['gain']==.5
    assert any(d['text']=='Audio SDK command queued' for d in stepped['diagnostics'])
  checkpoint=stepped['recovery']
  worker.request('resume');time.sleep(.05);paused=worker.request('pause')
  assert paused['timing']['tick']>4 and paused['audio']['paused']
  restored=worker.request('replace',scene=checkpoint['scene'],recovery=checkpoint,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
  assert restored['timing']['tick']==4 and restored['audio']['paused'] and restored['audio']['playing_sources']==1
  # Corrupt candidate leaves the old audio-bearing world intact and paused.
  bad=copy.deepcopy(checkpoint);bad['integrity']='bad'
  try:worker.request('replace',scene=checkpoint['scene'],recovery=bad,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
  except AssertionError:pass
  else:raise AssertionError('Invalid recovery accepted')
  assert worker.request('snapshot')['audio']['voices']==1
 finally:worker.close()
 if not module:
  headless=Worker(root,audio=False)
  try:
   assert headless.request('hello')['audio']['output']=='disabled'
   assert headless.request('replace',scene=scene)['audio']['output']=='disabled'
   assert headless.request('step')['timing']['tick']==1
  finally:headless.close()
print('Audio protocol/recovery/headless/exact-SDK policy passed; no hardware playback claim')
