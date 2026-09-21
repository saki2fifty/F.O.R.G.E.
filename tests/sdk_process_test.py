"""Rich registration is startup-only; a module crash stays in the disposable runtime."""
import json,subprocess,sys,tempfile,os
from pathlib import Path
runtime,good,crash=map(lambda p:Path(p).resolve(),sys.argv[1:4])
fingerprint=json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
with tempfile.TemporaryDirectory(dir=runtime.parent) as work:
 root=Path(work)
 def launch(library):
  import shutil
  shutil.copy2(library,root/library.name)
  project=dict(version=2,name='SDK process',startup_scene=None,input=dict(version=1,actions=[]),modules=[dict(id='project.sdk_probe',implementation='1',sdk='experimental-1',fingerprint=fingerprint,library=library.name,dependencies=['forge.input','forge.transforms'])])
  (root/'forge.project.json').write_text(json.dumps(project))
  return subprocess.Popen([runtime,'--sdk-project',root],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 def hello(p):
  p.stdin.write(json.dumps(dict(protocol=2,id=1,command='hello'))+'\n');p.stdin.flush()
  r=json.loads(p.stdout.readline());assert r['ok'];return r['session']
 p=launch(crash)
 try:
  session=hello(p)
  p.stdin.write(json.dumps(dict(protocol=2,id=2,session=session,command='step'))+'\n');p.stdin.flush()
  assert p.wait(timeout=10)!=0
 finally:
  if p.poll() is None:p.kill();p.wait()
 p=launch(good)
 try:
  trace=root/'inspection-trace.txt'
  inspected=subprocess.run([runtime,'--inspect-sdk',root],text=True,capture_output=True,timeout=30,
      env=dict(os.environ,FORGE_SDK_TRACE=str(trace)))
  assert inspected.returncode==0,inspected.stderr
  manifest=json.loads(inspected.stdout)
  assert manifest['format']=='forge.authored-types' and manifest['fingerprint']==fingerprint,manifest
  assert len(manifest['components'])==1,manifest
  health=manifest['components'][0]
  assert health['id']=='project.health' and health['schema_version']==1 and health['defaults']==dict(health=100,lives=3),health
  assert len(health['digest'])==64 and health['module']=='project.sdk_probe',health
  assert 'start' not in trace.read_text().splitlines() and 'tick' not in trace.read_text().splitlines()
  session=hello(p)
  for i,command in enumerate(['reload_sdk','step','quit'],2):
   p.stdin.write(json.dumps(dict(protocol=2,id=i,session=session,command=command))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline())
   assert r['ok']==(command!='reload_sdk'),r
  assert p.wait(timeout=10)==0
 finally:
  if p.poll() is None:p.kill();p.wait()
print('SDK crash process isolated; new runtime starts cleanly; in-place rich reload unavailable')
