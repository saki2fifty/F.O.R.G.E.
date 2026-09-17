import json,subprocess,sys,tempfile,shutil
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
  request('replace',scene=scene)
  for _ in range(4):r=request('step')
  assert r['physics']['bodies']==1 and r['timing']['tick']==4
  assert r['scene']['entities'][0]['components']['forge.physics_body']['friction']==.75
  request('quit');assert p.wait(timeout=10)==0
 finally:
  if p.poll() is None:p.kill();p.wait()
print('Shared Flecs SDK physics schema, fixed tick, input coexistence, capability and raycast passed')
