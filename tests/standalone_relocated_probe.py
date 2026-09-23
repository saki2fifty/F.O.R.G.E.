"""Fresh Windows runner: no source checkout, SDK, original project or build tree."""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
root,evidence=map(lambda p:Path(p).resolve(),sys.argv[1:])
evidence.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
manifest=json.loads((root/'forge.standalone.json').read_text())
assert manifest['engine']['profile']=='shared-native-sdk'
assert len(manifest['settings']['modules'])==1
for name,info in manifest['files'].items():
 assert hashlib.sha256((root/name).read_bytes()).hexdigest()==info['sha256'],name
for command in [[root/'forge_game.exe','--verify-startup'],[root/'forge_game_fixture.exe','--packaged-mixed',evidence]]:
 result=subprocess.run(list(map(str,command)),cwd=root,env=env,text=True,capture_output=True,timeout=120)
 (evidence/(Path(command[0]).stem+'.log')).write_text(result.stdout+'\n'+result.stderr)
 assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
assert {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}==set(manifest['files'])|{'forge.standalone.json'}
storage=json.loads((evidence/'storage-result.json').read_text())
assert not storage['reopened']
assert not Path(storage['root']).is_relative_to(root)
assert (Path(storage['root'])/'runtime.log').is_file()
relocated=root.with_name(root.name+' moved again')
shutil.move(str(root),relocated)
restarted=evidence/'restarted'
result=subprocess.run([str(relocated/'forge_game_fixture.exe'),'--packaged-mixed',str(restarted)],cwd=relocated,env=env,text=True,capture_output=True,timeout=120)
(evidence/'restarted.log').write_text(result.stdout+'\n'+result.stderr)
assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
restored=json.loads((restarted/'storage-result.json').read_text())
assert restored['reopened'] and restored['root']==storage['root'] and restored['scene']==storage['scene']
assert {p.relative_to(relocated).as_posix() for p in relocated.rglob('*') if p.is_file()}==set(manifest['files'])|{'forge.standalone.json'}
(evidence/'fresh-runner.json').write_text(json.dumps({'engine':manifest['engine'],'production_startup':True,'fresh_runner_no_checkout_or_sdk':True,'installation_unchanged':True,'saves_settings_survive_second_move':True},indent=2))
