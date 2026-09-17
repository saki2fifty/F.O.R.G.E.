"""Build from installed SDK, inspect binary dependencies, run from a relocated clean package."""
from pathlib import Path
import json,os,shutil,subprocess,sys
build=Path(sys.argv[1]).resolve();cmake=sys.argv[2];ninja=sys.argv[3]
stage=build/'sdk-package-test'
if stage.exists():shutil.rmtree(stage)
subprocess.run([cmake,'--install',str(build),'--prefix',str(stage),'--component','NativeSdk'],check=True)
client=build/'sdk-client-test'
subprocess.run([cmake,'-S',str(stage/'sdk/sample'),'-B',str(client),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,'-DFORGE_NATIVE_SDK='+str(stage)],check=True)
subprocess.run([cmake,'--build',str(client),'--parallel','2'],check=True)
windows=os.name=='nt';exe='.exe' if windows else '';ext='.dll' if windows else '.so'
runtime=stage/'bin'/('forge_runtime'+exe)
module=client/('gameplay'+ext)
# Imports must refer to the shared Flecs library; no private exported Flecs implementation.
if windows:
    imports=subprocess.check_output(['dumpbin','/imports',str(module)],text=True)
    exports=subprocess.check_output(['dumpbin','/exports',str(module)],text=True)
    assert 'flecs.dll' in imports.lower() and 'ecs_init' in imports
    assert ' ecs_init' not in exports
else:
    imports=subprocess.check_output(['readelf','-d',str(module)],text=True)
    exports=subprocess.check_output(['nm','-D','--defined-only',str(module)],text=True)
    assert 'libflecs.so.4' in imports and ' ecs_init' not in exports
    hostdeps=subprocess.check_output(['readelf','-d',str(runtime)],text=True)
    assert 'libflecs.so.4' in hostdeps and '$ORIGIN' in hostdeps
project=stage/'project';project.mkdir();shutil.copy2(module,project/module.name)
# Relocate after building: no build-tree or old install-path dependence for execution.
relocated=build/'sdk-relocated-test'
if relocated.exists():shutil.rmtree(relocated)
stage.rename(relocated);stage=relocated;project=stage/'project';runtime=stage/'bin'/('forge_runtime'+exe)
env=os.environ.copy();env.pop('LD_LIBRARY_PATH',None);env.pop('LD_PRELOAD',None)
env['PATH']=str(Path(env.get('SystemRoot','C:/Windows'))/'System32') if windows else '/usr/bin:/bin'
env['FORGE_SDK_TRACE']=str(stage/'trace.txt')
info=json.loads(subprocess.check_output([str(runtime),'--sdk-info'],env=env,cwd=stage,text=True))
assert info['profile']=='shared-native-sdk'
action='12345678-1234-4234-8234-123456789abc'
manifest=dict(version=2,name='SDK test',startup_scene=None,simulation_hz=120,input=dict(version=1,actions=[dict(id=action,name='Probe',kind='digital',bindings=[dict(control='key.space')])]),modules=[dict(id='project.sdk_probe',sdk='experimental-1',implementation='1',fingerprint=info['fingerprint'],library=module.name,dependencies=['forge.input','forge.transforms'])])
(project/'forge.project.json').write_text(json.dumps(manifest))
p=subprocess.Popen([str(runtime),'--sdk-project',str(project)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env,cwd=stage)
session='';seq=0
def request(command,**extra):
 global session,seq
 seq+=1;p.stdin.write(json.dumps(dict(protocol=2,id=seq,session=session,command=command,**extra))+'\n');p.stdin.flush()
 line=p.stdout.readline();assert line,p.stderr.read()
 r=json.loads(line);assert r['ok'],r
 if command=='hello':session=r['session']
 return r
try:
 r=request('hello');assert r['timing']['simulation_hz']==120
 request('step',input_events=[dict(control='key.space',value=1)])
 request('step');request('quit');assert p.wait(timeout=10)==0
finally:
 if p.poll() is None:p.kill();p.wait()
trace=(stage/'trace.txt').read_text();assert trace.count('tick\n')==2 and trace.endswith('unload\n'),trace
print('Installed SDK client, PE/ELF shared dependency, relocated runtime, fixed ticks and unload verified')
# Prove the installed audio headers and callback boundary from the relocated host too.
subprocess.run([sys.executable,str(Path(__file__).with_name('audio_process_test.py')),str(runtime),str(client/('audio_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=40)
# Compile and load the installed Animator component across the same shared Flecs boundary.
subprocess.run([sys.executable,str(Path(__file__).with_name('animation_process_test.py')),str(runtime),str(build/('forge_animation_tests'+exe)),str(build/'tools'/('gltf2ozz'+exe)),str(Path(__file__).resolve().parents[1]/'samples/animation/two-joints.gltf'),str(build/('forge_sample'+ext)),str(client/('animation_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=60)
# Retain a clean install for CI artifact, without the test project and trace.
(stage/'trace.txt').unlink();shutil.rmtree(project)

# Preserve executable modes and Linux SONAME symlinks through artifact transport.
shutil.make_archive(str(build/'experimental-native-sdk'), 'gztar', root_dir=stage)
