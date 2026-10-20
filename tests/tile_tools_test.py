"""Actual CLI tile discovery, UUID/local-key admission, recook and moved package."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

executable, gallery, scratch = map(lambda p: Path(p).resolve(), sys.argv[1:4])
scratch.mkdir(parents=True, exist_ok=True)
target = json.dumps(dict(platform='linux', backend='none', profile='cpu'))
package_target = json.dumps(dict(platform='linux', backend='none'))
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    project = root / 'Tile project with spaces'
    assets = project / 'Assets'
    assets.mkdir(parents=True)

    def request(*args, success=True):
        result = subprocess.run([str(executable), '--assets', *map(str, args)],
                                capture_output=True, text=True, timeout=60)
        value = json.loads(result.stdout)
        assert value['ok'] == success and (result.returncode == 0) == success, (result.stdout, result.stderr)
        return value

    material = json.loads((gallery / 'Assets/Checker.material.json').read_text())
    material['asset_id'] = material_id = str(uuid.uuid4())
    material['overrides']['model'] = 'forge.gltf.unlit.v1'
    (assets / 'Atlas.material.json').write_text(json.dumps(material))
    request('import-target', project, 'Assets/Atlas.material.json', target)
    set_id, map_id = str(uuid.uuid4()), str(uuid.uuid4())
    image_key, tile_key = 2**63, 2**63 + 1
    definitions = dict(format='forge.tile-set', version=1, asset_id=set_id, next_id=tile_key + 1,
                       images=[dict(id=image_key, name='Image', material=material_id,
                                    grid=dict(columns=2, rows=2, margin_x=0, margin_y=0, spacing_x=0, spacing_y=0))],
                       tiles=[dict(id=tile_key, name='Original', image=image_key, frame=3)])
    set_path = assets / 'Atlas.TILESET.JSON'
    set_path.write_text(json.dumps(definitions))
    cells = [None] * 1024
    cells[1023] = dict(tile=tile_key, flags=7)
    world = dict(format='forge.tile-map', version=1, asset_id=map_id, tile_set=set_id,
                 next_id=2, cell_size=[1, 1], layers=[dict(id=1, name='Layer', visible=True, order=0)],
                 chunks={'1:-1:-1': dict(cells=cells)})
    map_path = assets / 'World.TILEMAP.JSON'
    map_path.write_text(json.dumps(world))
    scan = request('scan', project)
    kinds = {row['source']: row['source_kind'] for row in scan['files']}
    assert kinds['Assets/Atlas.TILESET.JSON'] == 'tile_set'
    assert kinds['Assets/World.TILEMAP.JSON'] == 'tile_map'
    first = request('import-target', project, 'Assets/Atlas.TILESET.JSON', target)
    assert first['asset'] == set_id
    request('import-target', project, 'Assets/World.TILEMAP.JSON', target)
    cached = request('import-target', project, 'Assets/Atlas.TILESET.JSON', target)
    assert cached['cache_hit'] and cached['build_key'] == first['build_key']
    index = project / 'forge.assets.json'
    good = index.read_bytes()
    cells[1023]['tile'] = tile_key + 99
    map_path.write_text(json.dumps(world))
    request('import-target', project, 'Assets/World.TILEMAP.JSON', target, success=False)
    assert index.read_bytes() == good
    cells[1023]['tile'] = tile_key
    map_path.write_text(json.dumps(world))
    definitions['tiles'][0]['name'] = 'Renamed'
    set_path.write_text(json.dumps(definitions))
    changed = request('import-target', project, 'Assets/Atlas.TILESET.JSON', target)
    assert changed['asset'] == set_id and changed['build_key'] != first['build_key']
    package = root / 'Content package'
    request('package', project, package, package_target, json.dumps([map_id]), success=False)
    request('import-target', project, 'Assets/World.TILEMAP.JSON', target)
    request('package', project, package, package_target, json.dumps([map_id]))
    moved = root / 'Moved content package'
    package.rename(moved)
    assert request('verify-package', moved, package_target)['assets'] == 3
    assert not (moved / 'Assets').exists()
print('Tile CLI discovery, exact local keys, cache/last-good, stale dependency and moved source-free package passed')
