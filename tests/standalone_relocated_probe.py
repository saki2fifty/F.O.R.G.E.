"""Fresh Windows runner: no source checkout, SDK, original project or build tree."""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
assert len(sys.argv) in (3,4,5,6,7), "Expected game, evidence and optional Sprite/Tile Map/2D physics/connected2D games"
root,evidence=map(lambda p:Path(p).resolve(),sys.argv[1:3])
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
 assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
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
assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
restored=json.loads((restarted/'storage-result.json').read_text())
assert restored['reopened'] and restored['root']==storage['root'] and restored['scene']==storage['scene']
assert {p.relative_to(relocated).as_posix() for p in relocated.rglob('*') if p.is_file()}==set(manifest['files'])|{'forge.standalone.json'}
(evidence/'fresh-runner.json').write_text(json.dumps({'engine':manifest['engine'],'production_startup':True,'fresh_runner_no_checkout_or_sdk':True,'installation_unchanged':True,'saves_settings_survive_second_move':True},indent=2))

for index, gallery_path in enumerate(sys.argv[3:]):
 label=('sprite-image','tile-map','physics-2d','connected-2d')[index]
 sprite=Path(gallery_path).resolve()
 sprite_manifest=json.loads((sprite/'forge.standalone.json').read_text())
 assert sprite_manifest['engine']['profile']=='shared-native-sdk'
 assert not sprite_manifest['settings']['modules']
 assert not (sprite/'Scenes').exists() and not (sprite/'Assets').exists()
 for attempt in (1,2):
  for name,info in sprite_manifest['files'].items():
   assert hashlib.sha256((sprite/name).read_bytes()).hexdigest()==info['sha256'],name
  result=subprocess.run([str(sprite/'forge_game.exe'),'--verify-startup'],cwd=sprite,env=env,text=True,capture_output=True,timeout=120)
  (evidence/f'{label}-startup-{attempt}.log').write_text(result.stdout+'\n'+result.stderr)
  assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
  assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
  assert {p.relative_to(sprite).as_posix() for p in sprite.rglob('*') if p.is_file()}==set(sprite_manifest['files'])|{'forge.standalone.json'}
  if attempt==1:
   moved=sprite.with_name(sprite.name+' moved again')
   shutil.move(str(sprite),moved)
   sprite=moved
 (evidence/f'{label}-fresh-runner.json').write_text(json.dumps({'engine':sprite_manifest['engine'],'production_startups':2,'fresh_runner_no_checkout_or_sdk':True,'installation_unchanged':True,'manifest_sha256':hashlib.sha256((sprite/'forge.standalone.json').read_bytes()).hexdigest()},indent=2))
