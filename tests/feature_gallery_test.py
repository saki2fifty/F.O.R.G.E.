"""Admission and durable-identity check for shipped editable feature scenes."""
import json
import math
from pathlib import Path
import subprocess
import sys
import uuid

project_root = Path(sys.argv[2])
project = json.loads((project_root / 'forge.project.json').read_text())
assets = json.loads((project_root / 'forge.assets.json').read_text())['assets']
by_source = {row['source']: row for row in assets}
for source, cooked in (('Assets/Shape.mesh.json', 'mesh.bin'),
                       ('Assets/SculptSphere.mesh.json', 'mesh.bin'),
                       ('Assets/PaintedSphere.mesh.json', 'mesh.bin'),
                       ('Assets/Checker.material.json', 'material.values')):
    asset = by_source[source]
    assert json.loads((project_root / source).read_text())['asset_id'] == asset['id']
    assert (project_root / '.forge/cache/derived' /
            asset['metadata']['forge.import']['key'] / cooked).is_file()
scenes = sorted((project_root / 'Scenes').glob('*.scene.json'))
assert {'01-transforms.scene.json', '02-lighting.scene.json',
        '03-physics.scene.json', '04-cameras.scene.json',
        '05-editable-mesh.scene.json', '06-sculpt.scene.json',
        '07-vertex-paint.scene.json'} <= {p.name for p in scenes}
assert project['version'] == 2 and project['input']['version'] == 1
assert project['game']['application_id'] == 'org.forge.feature-gallery'
assert project['game']['display']['mode'] == 'windowed'
assert project['startup_scene']['source'] == 'Scenes/' + scenes[0].name

process = subprocess.Popen([sys.argv[1], '--stdio'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)

def call(method, **fields):
    process.stdin.write(json.dumps(dict(api=1, method=method, **fields)) + '\n')
    process.stdin.flush()
    result = json.loads(process.stdout.readline())
    assert result['ok'], (method, result)
    return result

def same_authored_value(actual, expected, location='$'):
    # SceneDraft recasts reflected float32 values. Native JSON float parsing may
    # differ by a final float32 ULP across platforms; document structure and
    # discrete identities must still match exactly.
    if isinstance(expected, dict):
        assert isinstance(actual, dict) and actual.keys() == expected.keys(), location
        for key in expected:
            same_authored_value(actual[key], expected[key], location + '.' + key)
    elif isinstance(expected, list):
        assert isinstance(actual, list) and len(actual) == len(expected), location
        for index, (item, value) in enumerate(zip(actual, expected)):
            same_authored_value(item, value, f'{location}[{index}]')
    elif isinstance(expected, float):
        assert isinstance(actual, (float, int)) and not isinstance(actual, bool), location
        assert math.isclose(actual, expected, rel_tol=2e-7, abs_tol=2e-7), (
            location, actual, expected)
    else:
        assert actual == expected and type(actual) is type(expected), (
            location, actual, expected)

try:
    context = call('discover')['result']
    all_ids = set()
    for path in scenes:
        source = json.loads(path.read_text())
        assert source['version'] == 3
        assert str(uuid.UUID(source['asset_id'])) == source['asset_id']
        assert source['asset_id'] not in all_ids
        all_ids.add(source['asset_id'])
        ids = [entity['id'] for entity in source['entities']]
        assert len(ids) == len(set(ids)) and len(ids) >= 6
        for entity_id in ids:
            assert str(uuid.UUID(entity_id)) == entity_id and entity_id not in all_ids
            all_ids.add(entity_id)
        assert any('forge.camera' in row['components'] for row in source['entities'])
        result = call('scene.replace', target=context['target'],
                      expected_revision=context['revision'], document=source)
        context = call('discover')['result']
        loaded = call('scene.read', target=context['target'])['result']
        same_authored_value(loaded, source, str(path))
        assert context['revision'] == result['revision']
        if path.name.startswith('01-'):
            assert sum('parent' in row for row in loaded['entities']) == 3
        if path.name.startswith('02-'):
            assert {row['components']['forge.light']['kind'] for row in loaded['entities']
                    if 'forge.light' in row['components']} == {0, 1, 2}
        if path.name.startswith('03-'):
            assert sum(row['components'].get('forge.physics_body', {}).get('motion') == 2
                       for row in loaded['entities']) == 3
        if path.name.startswith('05-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/Shape.mesh.json']['id']]
            bindings = next(row['components']['forge.mesh_renderer']['materials']
                            for row in loaded['entities']
                            if 'forge.mesh_renderer' in row['components'])
            assert bindings == [{'slot': 'surface',
                                 'material': by_source['Assets/Checker.material.json']['id']}]
        if path.name.startswith('06-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/SculptSphere.mesh.json']['id']]
        if path.name.startswith('07-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/PaintedSphere.mesh.json']['id']]
            painted = json.loads((project_root / 'Assets/PaintedSphere.mesh.json').read_text())
            assert all('color' in vertex for vertex in painted['vertices'])
        if path.name.startswith('04-'):
            cameras = [row['components']['forge.camera'] for row in loaded['entities']
                       if 'forge.camera' in row['components']]
            assert {camera['projection'] for camera in cameras} == {0, 1}
            assert sorted(camera['viewport_x'] for camera in cameras) == [0, .5]
    assert project['startup_scene']['asset'] == json.loads(scenes[0].read_text())['asset_id']
finally:
    process.stdin.close()
    assert process.wait(timeout=10) == 0
print(f'Feature Gallery: {len(scenes)} authored scenes admitted and unchanged by FORGE')
