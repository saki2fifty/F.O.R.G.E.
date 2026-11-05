"""Typed paint source identity survives CLI discovery and source suffix case."""
import json
from pathlib import Path
import subprocess
import sys
import uuid

executable = Path(sys.argv[1]).resolve()
root = Path(sys.argv[2]).resolve() / str(uuid.uuid4())
(root / 'Assets').mkdir(parents=True)
for index, suffix in enumerate(('.paint-texture.json', '.PAINT-TEXTURE.JSON')):
    identity = str(uuid.uuid4())
    source = f'Assets/Canvas{index}{suffix}'
    (root / source).write_text(json.dumps(dict(
        format='forge.paint-texture', version=1, asset_id=identity,
        width=3, height=2, semantic='color', background=[255, 64, 12, 255], tiles=[])),
        encoding='utf-8')
    result = subprocess.run([str(executable), '--assets', 'import', str(root), source],
                            capture_output=True, text=True, encoding='utf-8', timeout=30)
    response = json.loads(result.stdout)
    assert result.returncode == 0 and response['ok'], (response, result.stderr)
    records = json.loads((root / 'forge.assets.json').read_text(encoding='utf-8'))['assets']
    selected = next(row for row in records if row['source'] == source)
    assert selected['id'] == identity and selected['type'] == 'texture'
    assert selected['metadata']['forge.import']['importer'] == 'forge.texture.paint'
print('Lowercase/uppercase paint CLI imports retain authored identity and publish Texture assets.')
