"""No-device audio output policy, recovery, and optional exact SDK commands."""
import copy,json,math,shutil,struct,subprocess,sys,tempfile,time,wave
from pathlib import Path
runtime=Path(sys.argv[1]).resolve()
module=Path(sys.argv[2]).resolve() if len(sys.argv)>2 and sys.argv[2]!='-' else None
codec_gallery=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else None
scene_id='11111111-1111-4111-8111-111111111111'
entity_id='22222222-2222-4222-8222-222222222222'
reverb_expected = dict(reverb_enabled=bool(module), reverb_decay=.75 if module else 1.5,
                       reverb_damping=.25 if module else .5, reverb_wet=.5 if module else .25)
def check_reverb(values):
 for name, expected in reverb_expected.items():
  assert values[name] == expected, (name, values)

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
  assert self.p.returncode == 0, self.p.stderr.read()
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
  ref=dict(scene=scene_id,entity=entity_id)
  watched=worker.request('snapshot',audio_source_watch=ref)
  assert watched['audio_source']['available'] and watched['audio_source']['playing']
  stopped=worker.request('audio_source',source=ref,play=False,audio_source_watch=ref)
  assert not stopped['audio_source']['playing'] and stopped['audio_source']['paused']
  restarted=worker.request('audio_source',source=ref,play=True,audio_source_watch=ref)
  assert restarted['audio_source']['playing'] and restarted['audio_source']['paused']
  assert restarted['timing']['tick']==initial['timing']['tick'] and restarted['scene']==initial['scene']
  source_paused=worker.request('audio_source',source=ref,operation='pause',audio_source_watch=ref)
  assert source_paused['audio_source']['source_paused'] and not source_paused['audio_source']['playing']
  source_resumed=worker.request('audio_source',source=ref,operation='resume',audio_source_watch=ref)
  assert source_resumed['audio_source']['playing'] and not source_resumed['audio_source']['source_paused']
  assert source_resumed['timing']['tick']==initial['timing']['tick'] and source_resumed['scene']==initial['scene']
  effect=worker.request('audio_source',source=ref,operation='one_shot',audio_source_watch=ref)
  assert effect['audio_source']['one_shots']==1 and effect['audio']['voices']==2
  effect=worker.request('audio_source',source=ref,operation='one_shot',audio_source_watch=ref)
  assert effect['audio_source']['one_shots']==2 and effect['audio']['voices']==3
  held=worker.request('audio_source',source=ref,operation='pause',audio_source_watch=ref)
  assert held['audio_source']['paused_one_shots']==2
  resumed=worker.request('audio_source',source=ref,operation='resume',audio_source_watch=ref)
  assert resumed['audio_source']['playing_one_shots']==2 and resumed['audio']['paused']
  assert resumed['timing']['tick']==initial['timing']['tick'] and resumed['scene']==initial['scene']
  worker.request('audio_source',source=ref,operation='stop')
  cleared=worker.request('audio_source',source=ref,operation='play',audio_source_watch=ref)
  assert cleared['audio_source']['one_shots']==0 and cleared['audio']['voices']==1
  for fields in (dict(source=ref,play=1),dict(source=dict(scene=scene_id,entity=clip_id),play=False),
                 dict(source=ref,operation='seek'),dict(source=ref,operation=2),
                 dict(source=ref,operation='pause',play=True),dict(source=ref)):
   try:worker.request('audio_source',**fields)
   except AssertionError:pass
   else:raise AssertionError('Invalid audio source control accepted')
   assert worker.request('snapshot',audio_source_watch=ref)['audio_source']['playing']
  missing=worker.request('snapshot',audio_source_watch=dict(scene=scene_id,entity=clip_id))
  assert not missing['audio_source']['available'] and missing['audio']['voices']==1
  mix=dict(master_volume=.25,channel_volumes=[.5,0,.75])
  mixed=worker.request('audio_mix',mix=mix)
  assert mixed['audio']['master_volume']==.25 and mixed['audio']['channel_volumes']==[.5,0,.75]
  assert mixed['timing']['tick']==initial['timing']['tick'] and mixed['audio']['paused']
  assert mixed['scene']==initial['scene'] and mixed['audio']['voices']==1
  for invalid in (dict(master_volume=-1e-50,channel_volumes=[1,1,1]),
                  dict(master_volume=1,channel_volumes=[1,1]),
                  dict(master_volume=1,channel_volumes=[1,1,2]),
                  dict(master_volume=True,channel_volumes=[1,1,1]),
                  dict(master_volume=None,channel_volumes=[1,1,1]),
                  dict(master_volume=1,channel_volumes=[1,1,1],unexpected=0)):
   try:worker.request('audio_mix',mix=invalid)
   except AssertionError:pass
   else:raise AssertionError('Invalid audio mix accepted')
   kept=worker.request('snapshot')
   assert kept['audio']['master_volume']==.25 and kept['audio']['channel_volumes']==[.5,0,.75]
   assert kept['timing']['tick']==initial['timing']['tick'] and kept['audio']['voices']==1
  before=initial['timing']['tick'];time.sleep(.04)
  assert worker.request('snapshot')['timing']['tick']==before
  for n in range(1,5):
   stepped=worker.request('step');assert stepped['timing']['tick']==n and stepped['audio']['paused']
   if module:
    status=worker.request('snapshot',audio_source_watch=ref)['audio_source']
    assert status['one_shots']=={1:1,2:1,3:2,4:0}[n],(n,status)
    assert status['paused_one_shots']==(1 if n==2 else 0),(n,status)
    assert not any(d['text']=='Audio SDK one-shot rejected' for d in stepped['diagnostics'])
    assert status['playing']==(n in (1,3)) and status['source_paused']==(n==2),(n,status)
    assert stepped['scene']['entities'][0]['components']['forge.audio_source']['gain']==.5
    assert stepped['scene']['entities'][0]['components']['forge.audio_source']['attenuation']==1
    assert stepped['scene']['entities'][0]['components']['forge.audio_source']['rolloff']==.5
    cone=stepped['scene']['entities'][0]['components']['forge.audio_source']
    assert cone['cone_enabled'] and cone['inner_angle']==40 and cone['outer_angle']==100
    assert abs(cone['outer_gain']-.4)<1e-6
    assert cone['bus']==2
    assert cone['low_pass_enabled'] and cone['high_pass_enabled']
    assert cone['low_pass_hz']==4000 and cone['high_pass_hz']==100
    assert status['low_pass_enabled'] and status['high_pass_enabled']
    assert status['low_pass_hz']==4000 and status['high_pass_hz']==100
    assert cone['echo_enabled'] and cone['echo_seconds']==.125 and cone['echo_feedback']==.375
    assert status['echo_enabled'] and status['echo_seconds']==.125 and status['echo_feedback']==.375
    check_reverb(cone);check_reverb(status)
    assert any(d['text']=='Audio SDK command queued' for d in stepped['diagnostics'])
    assert not any(d['text']=='Invalid audio SDK operation accepted' for d in stepped['diagnostics'])
  transient=worker.request('audio_source',source=ref,operation='one_shot',audio_source_watch=ref)
  assert transient['audio_source']['one_shots']==1 and transient['audio']['paused']
  checkpoint=transient['recovery']
  worker.request('resume');time.sleep(.05);paused=worker.request('pause')
  assert paused['timing']['tick']>4 and paused['audio']['paused']
  restored=worker.request('replace',scene=checkpoint['scene'],recovery=checkpoint,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
  assert restored['timing']['tick']==4 and restored['audio']['paused'] and restored['audio']['playing_sources']==1
  assert restored['audio']['one_shots']==0 and restored['audio']['voices']==1
  assert restored['audio']['master_volume']==.25 and restored['audio']['channel_volumes']==[.5,0,.75]
  check_reverb(worker.request('snapshot',audio_source_watch=ref)['audio_source'])
  cold=Worker(root,sdk=bool(module))
  try:
   cold.request('hello',simulation_hz=60,gravity=[0,-3,0])
   fresh=cold.request('replace',scene=checkpoint['scene'],recovery=checkpoint,
                      recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'],
                      audio_source_watch=ref)
   deadline=time.monotonic()+5
   while not fresh.get('audio_source',{}).get('available'):
    assert time.monotonic()<deadline, fresh
    time.sleep(.01)
    fresh=cold.request('snapshot',audio_source_watch=ref)
   assert fresh['timing']['tick']==4 and fresh['audio']['paused']
   assert fresh['audio']['one_shots']==0
   check_reverb(fresh['audio_source'])
  finally:cold.close()

  # Corrupt candidate leaves the old audio-bearing world intact and paused.
  bad=copy.deepcopy(checkpoint);bad['integrity']='bad'
  try:worker.request('replace',scene=checkpoint['scene'],recovery=bad,recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
  except AssertionError:pass
  else:raise AssertionError('Invalid recovery accepted')
  assert worker.request('snapshot')['audio']['voices']==1
  if codec_gallery:
   # Use published PCM from the editable examples in both actual worker modes.
   # No source recordings are copied to the isolated runtime project.
   catalog=json.loads((codec_gallery/'forge.assets.json').read_text())
   codecs=[record for record in catalog['assets'] if record.get('source') in
           ('Assets/AudioCodecs/Stereo.flac','Assets/AudioCodecs/Stereo.mp3')]
   assert len(codecs)==2
   for record in codecs:
    key=record['metadata']['forge.import']['key']
    dest=root/'.forge/cache/derived'/key
    shutil.copytree(codec_gallery/'.forge/cache/derived'/key,dest)
    (root/'forge.assets.json').write_text(json.dumps(dict(version=1,assets=[record])))
    candidate=copy.deepcopy(scene)
    candidate['entities'][0]['components']['forge.audio_source']['clip']=record['id']
    observed=worker.request('replace',scene=candidate,audio_source_watch=ref)
    assert observed['audio']['voices']==1 and observed['audio_source']['available'],observed
    assert observed['audio_source']['playing']
    assert not (root/record['source']).exists()
    assert worker.request('step')['timing']['tick']==1
 finally:worker.close()
 if not module:
  headless=Worker(root,audio=False)
  try:
   assert headless.request('hello')['audio']['output']=='disabled'
   silent=copy.deepcopy(scene)
   del silent['entities'][0]['components']['forge.audio_source']
   accepted=headless.request('replace',scene=silent)
   assert accepted['audio']['output']=='disabled'
   try:headless.request('replace',scene=scene)
   except AssertionError as error:
    assert 'audio output is unavailable' in str(error), error
   else:raise AssertionError('Audio-bearing runtime Scene accepted without its required provider')
   assert headless.request('snapshot')['scene']==accepted['scene']
   try:headless.request('audio_mix',mix=mix)
   except AssertionError:pass
   else:raise AssertionError('Headless audio mix accepted')
   assert headless.request('step')['timing']['tick']==1
  finally:headless.close()
print('Audio protocol/recovery/headless/exact-SDK and optional cooked codec policy passed; no hardware playback claim')
