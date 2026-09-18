"""Run the packaged navigation worker from a relocated editor without developer PATH."""
import hashlib,json,os,subprocess,sys,tempfile,zipfile
from pathlib import Path
archive,fixture=map(lambda x:Path(x).resolve(),sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='navigation-relocation-',dir=archive.parent) as temporary:
 root=Path(temporary)/'Relocated FORGE';root.mkdir()
 with zipfile.ZipFile(archive) as package:package.extractall(root)
 manifest=json.loads((root/'manifest.json').read_text());worker=root/'forge_nav_build.exe'
 assert hashlib.sha256(worker.read_bytes()).hexdigest()==manifest['files'][worker.name]
 assert (root/'licenses/recast-src/License.txt').is_file()
 env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
 project=Path(subprocess.check_output([fixture,root/'projects',worker,'--prepare'],env=env,cwd=root,text=True,timeout=60).strip())
 record=next(a for a in json.loads((project/'forge.assets.json').read_text())['assets'] if a['type']=='navmesh')
 assert hashlib.sha256((project/record['source']).read_bytes()).hexdigest()==record['metadata']['sha256']
print('Packaged navigation worker, provenance and relocation without developer PATH passed')
