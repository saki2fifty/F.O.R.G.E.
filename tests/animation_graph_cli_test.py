"""The public asset CLI discovers and publishes the same graph source owner."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

tools, gallery = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='forge-animation-graph-cli-') as temporary:
    root = Path(temporary) / 'Project'
    shutil.copytree(gallery, root)
    path = 'Assets/AnimationGraphs/Layered.animgraph.json'
    def invoke(*args):
        completed = subprocess.run([str(tools), '--assets', *map(str, args)],
                                   capture_output=True, text=True, encoding='utf-8', timeout=30)
        result = json.loads(completed.stdout)
        assert (completed.returncode == 0) == result['ok'], (completed, result)
        return result
    scanned = invoke('scan', root, 'Assets/AnimationGraphs')
    assert scanned['ok']
    assert next(row for row in scanned['files'] if row['source'] == path)['source_kind'] == 'animation_graph'
    before = json.loads((root / 'forge.assets.json').read_text())['assets']
    graph = next(row for row in before if row['source'] == path)
    target = json.dumps(dict(platform='windows', backend='d3d12', profile='desktop'))
    published = invoke('import-target', root, path, target)
    assert published['ok'] and published['asset'] == graph['id'], published
    assert published['cache_hit'] and published['build_key'] == graph['metadata']['forge.import']['key'], published
    current = json.loads((root / 'forge.assets.json').read_text())['assets']
    selected = next(row for row in current if row['id'] == graph['id'])
    source = json.loads((root / path).read_text())
    source['nodes'][-1]['first'] = source['nodes'][-1]['id']
    (root / path).write_text(json.dumps(source))
    failed = invoke('import-target', root, path, target)
    assert not failed['ok']
    after = json.loads((root / 'forge.assets.json').read_text())['assets']
    assert next(row for row in after if row['id'] == graph['id']) == selected
print('CLI graph discovery, verified cache publication and failed-cook preservation passed')
