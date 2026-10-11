import json,subprocess,sys,tempfile,shutil,uuid
from pathlib import Path
runtime,module=map(lambda x:Path(x).resolve(),sys.argv[1:])
fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
with tempfile.TemporaryDirectory(dir=runtime.parent) as work:
 root=Path(work);shutil.copy2(module,root/module.name)
 project=dict(version=2,name='Physics SDK',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.physics_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=module.name,dependencies=['forge.physics','forge.input'])])
 (root/'forge.project.json').write_text(json.dumps(project))
 p=subprocess.Popen([runtime,'--sdk-project',root],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 session='';sequence=0
 def request(command,**fields):
  global session,sequence
  sequence+=1;p.stdin.write(json.dumps(dict(protocol=2,id=sequence,command=command,session=session,**fields))+'\n');p.stdin.flush()
  line=p.stdout.readline();assert line,p.stderr.read();result=json.loads(line);assert result['ok'],result;session=result['session'];return result
 try:
  request('hello')
  scene=dict(version=1,entities=[dict(id='cube',name='Cube',components={'forge.position':dict(x=0,y=5,z=0),'forge.physics_body':dict(motion=2,density=1000,mass=0,friction=.5,restitution=0,gravity_factor=1),'forge.box_collider':dict(x=1,y=1,z=1)})])
  scene=request('replace',scene=scene)['scene']
  scene['entities'].append(dict(id=str(uuid.uuid4()),name='Character',spatial=dict(mode='world'),components={
   'forge.local_translation':dict(x=3,y=5,z=0),
   'forge.character_controller':dict(enabled=True,shape=0,radius=.35,height=1.1,crouch_height=.45,mass=70,max_strength=100,max_slope=50,step_height=.4,step_forward=.15,floor_probe=.5,gravity_factor=1,layer=0,mask=4294967295)}))
  request('replace',scene=scene)
  for _ in range(4):r=request('step')
  assert r['physics']['bodies']==1 and r['physics']['characters']==1 and r['timing']['tick']==4
  character=next(e for e in r['scene']['entities'] if e['name']=='Character')
  assert character['components']['forge.local_translation']['x']>3.03,character
  cube=next(e for e in r['scene']['entities'] if e['name']=='Cube')
  assert cube['components']['forge.physics_body']['friction']==.75
  position=cube['components']['forge.local_translation']
  assert .015<position['x']<.018 and .0009<position['z']<.0011,position
  rotation=cube['components']['forge.local_rotation']
  assert rotation['x']>.002 and rotation['z']>.00002,rotation
  # PostPhysics queues all four kinds for the following fixed step and private
  # recovery must preserve that pending intent, not only already integrated pose.
  saved=r
  commands=saved['recovery']['physics']['commands']
  assert [c['kind'] for c in commands]==[2,3,4,5],commands
  dt=1/60;damping=1-.05*dt;velocity=0;expected=5
  for tick in range(4):
   velocity=(velocity-9.81*dt+(60*dt+.1)/1000*(tick>0))*damping;expected+=velocity*dt
  assert abs(position['y']-expected)<1e-6,(position,expected)
  trajectory=[request('step')['scene'] for _ in range(12)]
  request('quit');assert p.wait(timeout=10)==0
  p=subprocess.Popen([runtime,'--sdk-project',root],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  session='';sequence=0;request('hello')
  checkpoint=saved['recovery']
  recovered=request('replace',scene=saved['scene'],recovery=checkpoint,
                    recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
  assert recovered['timing']['paused'] and recovered['timing']['tick']==4,recovered
  for expected_scene in trajectory:
   result=request('step')['scene']
   expected_entities={e['name']:e for e in expected_scene['entities']}
   for e in result['entities']:
    for component in ('forge.local_translation','forge.local_rotation'):
     if component in e['components']:
      for field,value in e['components'][component].items():
       assert abs(value-expected_entities[e['name']]['components'][component][field])<1e-6,(e,component,field)
  request('quit');assert p.wait(timeout=10)==0
 finally:
  if p.poll() is None:p.kill();p.wait()
print('Shared Flecs SDK physics schema, numerical loads, fixed/owner guards, input coexistence and raycast passed')
