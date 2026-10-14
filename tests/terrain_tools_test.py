"""Real CLI heightfield discovery, exact cook, cache reuse and last-good failure."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

executable = Path(sys.argv[1]).resolve()
gallery = Path(sys.argv[2]).resolve()
scratch = Path(sys.argv[3]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    project = Path(temporary) / 'Terrain project with spaces'
    (project / 'Assets').mkdir(parents=True)
    source = json.loads((gallery / 'Assets/Terrain/LayeredHills.terrain.json').read_text())
    source['asset_id'] = identity = str(uuid.uuid4())
    path = project / 'Assets/Field.terrain.json'
    path.write_text(json.dumps(source))

    def request(*args, success=True):
        result = subprocess.run([str(executable), '--assets', *map(str, args)],
                                capture_output=True, text=True, timeout=30)
        value = json.loads(result.stdout)
        assert value['ok'] == success and (result.returncode == 0) == success, (result.stdout, result.stderr)
        return value

    scan = request('scan', project)
    assert scan['files'][0]['source'] == 'Assets/Field.terrain.json'
    assert scan['files'][0]['source_kind'] == 'terrain'
    target = json.dumps(dict(platform='linux', backend='none', profile='cpu'))
    first = request('import-target', project, 'Assets/Field.terrain.json', target)
    assert first['asset'] == identity and first['build_key']
    second = request('import-target', project, 'Assets/Field.terrain.json', target)
    assert second['cache_hit'] and second['build_key'] == first['build_key']
    source['layers'][1]['strength'] = .25
    path.write_text(json.dumps(source))
    third = request('import-target', project, 'Assets/Field.terrain.json', target)
    assert third['asset'] == identity and third['build_key'] != first['build_key']
    index = project / 'forge.assets.json'
    good = index.read_bytes()
    record = json.loads(good)['assets'][0]
    assert record['type'] == 'mesh' and record['metadata']['forge.terrain']['profile'] == 'heightfield_mesh1'
    request('import-target', project, 'Assets/Field.terrain.json',
            json.dumps(dict(platform='linux', backend='vulkan', profile='cpu')), success=False)
    assert index.read_bytes() == good
    source['tiles'][0]['layers'][0]['mask'][0] = 2
    path.write_text(json.dumps(source))
    request('import-target', project, 'Assets/Field.terrain.json', target, success=False)
    assert index.read_bytes() == good
    package_target = json.dumps(dict(platform='linux', backend='none'))
    output = Path(temporary) / 'Exported terrain'
    request('package', project, output, package_target, json.dumps([identity]))
    assert request('verify-package', output, package_target)['assets'] == 1
    assert not (output / 'Assets/Field.terrain.json').exists()
print('Terrain CLI discovery, cook/cache, target rejection, last-good and source-free package passed')
