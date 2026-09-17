"""Verify the shipped converter runs after relocation without developer PATH entries."""
import hashlib,json,os,shutil,subprocess,sys,tempfile,zipfile
from pathlib import Path
archive,fixture=map(lambda x:Path(x).resolve(),sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='animation-relocation-',dir=archive.parent) as temporary:
 root=Path(temporary)/'Relocated FORGE';root.mkdir()
 with zipfile.ZipFile(archive) as package:package.extractall(root)
 manifest=json.loads((root/'manifest.json').read_text())
 tool=root/'tools/gltf2ozz.exe'
 assert hashlib.sha256(tool.read_bytes()).hexdigest()==manifest['files']['tools/gltf2ozz.exe']
 assert (root/'licenses/ozz-converter.txt').is_file()
 env=os.environ.copy();env['PATH']=str(Path(env['SystemRoot'])/'System32')
 prepared=subprocess.check_output([fixture,root/'projects',tool,root/'Examples/Animation/two-joints.gltf','--prepare'],env=env,cwd=root,text=True,timeout=60)
 project=Path(prepared.strip())
 records=json.loads((project/'forge.assets.json').read_text())['assets']
 assert {r['type'] for r in records}=={'animation_source','skeleton','animation_clip'}
 for record in records:
  if record['type']!='animation_source':
   assert hashlib.sha256(tool.read_bytes()).hexdigest()==record['metadata']['converter_sha256']
   assert hashlib.sha256((project/record['source']).read_bytes()).hexdigest()==record['metadata']['artifact_sha256']
print('Packaged converter, provenance and relocation without developer PATH passed')
