"""All Phase6 runtime consumers in one world; no presentation-driven gameplay."""
import copy,json,math,shutil,struct,subprocess,sys,tempfile,time,uuid,wave
from pathlib import Path
runtime,nav_prepare,nav_worker,animation_prepare,converter,source=map(lambda p:Path(p).resolve(),sys.argv[1:7])
module=Path(sys.argv[7]).resolve() if len(sys.argv)>7 else None
uid=lambda:str(uuid.uuid4())
with tempfile.TemporaryDirectory(prefix='phase6-integration-',dir=runtime.parent) as work:
 base=Path(work)
 root=Path(subprocess.check_output([nav_prepare,base/'nav',nav_worker,'--prepare'],text=True).strip())
 anim=Path(subprocess.check_output([animation_prepare,base/'anim',converter,source,'--prepare'],text=True).strip())
 catalog=json.loads((root/'forge.assets.json').read_text())
 catalog['assets']+=json.loads((anim/'forge.assets.json').read_text())['assets']
 for path in anim.rglob('*'):
  if path.is_file() and path.name not in ('scene.json','forge.assets.json'):
   destination=root/path.relative_to(anim);destination.parent.mkdir(parents=True,exist_ok=True)
   assert not destination.exists(),destination
   shutil.copy2(path,destination)
 ui_asset,audio_asset=uid(),uid()
 (root/'hud.rml').write_text('<rml><head></head><body>{{tick}} {{example_ticks}}</body></rml>')
 with wave.open(str(root/'tone.wav'),'wb') as wav:
  wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(48000)
  wav.writeframes(b''.join(struct.pack('<h',int(1000*math.sin(i*.06))) for i in range(48000)))
 for asset,kind,path in [(ui_asset,'ui_document','hud.rml'),(audio_asset,'audio_clip','tone.wav')]:
  catalog['assets'].append(dict(id=asset,type=kind,source=path,schema_version=1,dependencies=[],metadata={}))
 (root/'forge.assets.json').write_text(json.dumps(catalog))
 scene=json.loads((root/'scene.json').read_text())
 animator=json.loads((anim/'scene.json').read_text())['entities'][0]['components']['forge.animator']
 scene['entities'] += [
  dict(id=uid(),name='Actor',components={'forge.animator':animator}),
  dict(id=uid(),name='HUD',components={'forge.ui_document':dict(document=ui_asset,enabled=True,visible=True,layer=0)}),
  dict(id=uid(),name='Speaker',components={'forge.audio_source':dict(clip=audio_asset,play_on_start=True,loop=True,gain=1,pitch=1,spatialized=False,minimum_distance=1,maximum_distance=100)}),
  dict(id=uid(),name='Falling',spatial=dict(mode='world'),components={'forge.local_translation':dict(x=30,y=5,z=0),'forge.physics_body':dict(motion=2,density=1000,mass=0,friction=.5,restitution=0,gravity_factor=1),'forge.box_collider':dict(x=1,y=1,z=1)})]
 project=dict(version=2,name='Phase6 integration',startup_scene=None,input=dict(version=1,actions=[dict(id='12345678-1234-4234-8234-123456789abc',name='Example',kind='digital',bindings=[dict(control='key.space')])]))
 if module:
  shutil.copy2(module,root/module.name)
  info=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))
  project['modules']=[dict(id='project.example',implementation='1',sdk='experimental-1',fingerprint=info['fingerprint'],library=module.name,dependencies=['forge.input','forge.transforms'])]
 (root/'forge.project.json').write_text(json.dumps(project))
 class Worker:
  def __init__(self):
   self.p=subprocess.Popen([runtime,'--sdk-project' if module else '--project',root,'--ui','on','--audio','offline'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   self.session='';self.seq=0
  def request(self,command,ok=True,**extra):
   self.seq+=1;self.p.stdin.write(json.dumps(dict(protocol=2,id=self.seq,session=self.session,command=command,**extra))+'\n');self.p.stdin.flush()
   line=self.p.stdout.readline();assert line,self.p.stderr.read()
   r=json.loads(line);self.session=r['session'];assert r['ok']==ok,r;return r
  def close(self):
   if self.p.poll() is None:
    try:self.request('quit');self.p.wait(timeout=5)
    finally:
     if self.p.poll() is None:self.p.kill();self.p.wait()
 def state(r):
  entities={e['name']:e for e in r['effective_scene']['entities']}
  return (entities['Falling']['world_affine'][7],entities['Agent']['world_affine'][3],entities['Actor']['animation_pose']['time'])
 def command(r,name,index):
  u=r['ui'];return dict(version=1,session=u['session'],generation=u['generation'],id=index,instance=u['documents'][0]['instance'],command=name)
 def restore(w,c):return w.request('replace',scene=c['scene'],recovery=c,recovery_session=c['session'],recovery_tick=c['tick'])
 a=Worker();b=None
 try:
  a.request('hello');first=a.request('replace',scene=scene)
  assert first['audio']['voices']==1 and first['audio']['paused'] and first['physics']['bodies']==1,first
  stepped=a.request('step',input_events=[dict(control='key.space',value=1)] if module else [])
  before,after=state(first),state(stepped)
  assert after[0]<before[0] and after[1]!=before[1] and after[2]>before[2],(before,after)
  if module:
   assert stepped['ui']['documents'][0]['model']['example_ticks']==1
   assert any('tick=1 presses=1' in d['text'] for d in stepped['diagnostics'])
   click=command(stepped,'ExampleIncrement',1)
   ack=a.request('ui',ui_command=click);assert ack['ui_ack']['ok']
   assert a.request('ui',ui_command=click)['ui_ack']==ack['ui_ack']
  else:click=command(stepped,'Step',1)
  time.sleep(.03);paused=a.request('snapshot');assert state(paused)==after and paused['timing']['tick']==1
  twice=a.request('step');assert twice['timing']['tick']==2 and twice['audio']['paused']
  if module:assert any('tick=2 presses=1 clicks=1' in d['text'] for d in twice['diagnostics']),twice['diagnostics']
  checkpoint=twice['recovery'];assert all(k in checkpoint for k in ['physics','animation','navigation']) and 'ui' not in checkpoint and 'audio' not in checkpoint
  a.request('resume');time.sleep(.06);running=a.request('pause');assert running['timing']['tick']>2 and state(running)!=state(twice)
  recovered=restore(a,checkpoint);assert state(recovered)==state(twice) and recovered['audio']['paused']
  a.request('ui',ok=False,ui_command=click)
  bad=copy.deepcopy(checkpoint);bad['integrity']='invalid';a.request('replace',ok=False,scene=bad['scene'],recovery=bad,recovery_session=bad['session'],recovery_tick=bad['tick'])
  assert state(a.request('snapshot'))==state(recovered)
  a.p.kill();a.p.wait();b=Worker();b.request('hello');rebuilt=restore(b,checkpoint)
  assert state(rebuilt)==state(twice) and rebuilt['ui']['session']!=twice['ui']['session'] and rebuilt['audio']['playing_sources']==1
  assert b.request('step')['timing']['tick']==3
  print('Phase6 combined physics/audio/animation/navigation/UI pause/step/recovery and SDK integration passed')
 finally:
  a.close()
  if b:b.close()
