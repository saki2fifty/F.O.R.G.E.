"""Admission and durable-identity check for shipped editable feature scenes."""
import json
from pathlib import Path
import subprocess
import sys
import uuid

project_root = Path(sys.argv[2])
project = json.loads((project_root / 'forge.project.json').read_text())
scenes = sorted((project_root / 'Scenes').glob('*.scene.json'))
assert len(scenes) == 4
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
        assert loaded == source, path
        assert context['revision'] == result['revision']
        if path.name.startswith('01-'):
            assert sum('parent' in row for row in loaded['entities']) == 3
        if path.name.startswith('02-'):
            assert {row['components']['forge.light']['kind'] for row in loaded['entities']
                    if 'forge.light' in row['components']} == {0, 1, 2}
        if path.name.startswith('03-'):
            assert sum(row['components'].get('forge.physics_body', {}).get('motion') == 2
                       for row in loaded['entities']) == 3
        if path.name.startswith('04-'):
            cameras = [row['components']['forge.camera'] for row in loaded['entities']
                       if 'forge.camera' in row['components']]
            assert {camera['projection'] for camera in cameras} == {0, 1}
            assert sorted(camera['viewport_x'] for camera in cameras) == [0, .5]
    assert project['startup_scene']['asset'] == json.loads(scenes[0].read_text())['asset_id']
finally:
    process.stdin.close()
    assert process.wait(timeout=10) == 0
print('Feature Gallery: four authored scenes admitted and unchanged by FORGE')
