"""Foliage placement through the shared command and real MCP document owner."""
import json
import math


def check_mesh_scatter_workflow(client, discover, arguments, mutate, project, catalog):
    command = next(c for c in discover()['result']['commands'] if c['id'] == 'mesh.scatter')
    tool = next(t for t in catalog if t['name'] == 'forge_mesh_scatter')
    assert tool['inputSchema']['properties']['arguments'] == command['input_schema']
    assert not tool['annotations']['readOnlyHint']
    assert command['input_schema']['properties']['count']['maximum'] == 512
    assert command['input_schema']['properties']['seed']['maximum'] == 4294967295
    template = mutate('forge_entity_create', arguments=dict(recipe='primitive.0', name='Fern'))
    assert template['ok'], template
    source = template['result']['results'][0]['selected']
    values = dict(entity=source, name='Agent meadow', center=dict(x=10, y=2, z=-5),
                  count=32, radius=5, spacing=.5, seed=42, scale_min=.8, scale_max=1.2,
                  yaw_min=-20, yaw_max=45)
    before = discover()
    request = arguments(arguments=values)
    changed = client.tool('forge_mesh_scatter', **request)
    assert changed['ok'] and changed['revision'] != before['revision'], changed
    created = changed['result']['results'][0]
    assert len(created['created']) == 32 and len(set(created['created'])) == 32
    assert client.tool('forge_mesh_scatter', **request) == changed
    state = discover()
    doc = client.tool('forge_scene_read', target=state['result']['target'])['result']
    assert len(doc['entities']) == 34
    rows = {e['id']: e for e in doc['entities']}
    assert rows[created['selected']]['name'] == 'Agent meadow'
    positions = []
    for id_ in created['created']:
        row = rows[id_]
        assert row['parent'] == created['selected']
        assert row['components']['forge.mesh_renderer'] == rows[source]['components']['forge.mesh_renderer']
        assert len(row['components']) == 4
        position = row['components']['forge.local_translation']
        assert position['y'] == 0 and math.hypot(position['x'], position['z']) <= 5
        for x, z in positions:
            assert math.hypot(position['x']-x, position['z']-z) >= .5
        positions.append((position['x'], position['z']))
    assert mutate('forge_history_undo')['result']['changed']
    state = discover()
    assert len(client.tool('forge_scene_read', target=state['result']['target'])['result']['entities']) == 1
    assert mutate('forge_history_redo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    assert json.loads((project/'Scenes/main.scene.json').read_bytes()) == doc
    # New operation uses the same stale-revision and target checks as all edits.
    stale = arguments(arguments=values)
    assert mutate('forge_entity_rename', arguments=dict(entity=source, name='Fern renamed'))['ok']
    current = discover()
    stale['sequence'] = current['next_sequence']
    result = client.tool('forge_mesh_scatter', **stale)
    assert not result['ok'] and result['error']['code'] == 'stale_revision', result
    for bad in (dict(count=513), dict(count=True), dict(count=1.0), dict(seed=-1),
                dict(seed=4294967296), dict(radius=0), dict(scale_min=2, scale_max=1),
                dict(yaw_min=30, yaw_max=10), dict(name=' '),
                dict(count=2, radius=.01, spacing=100), dict(extra=True)):
        initial = discover()
        result = mutate('forge_mesh_scatter', arguments=dict(values, **bad))
        assert not result['ok'] and discover()['revision'] == initial['revision'], result
    state = discover()
    assert len(client.tool('forge_scene_read', target=state['result']['target'])['result']['entities']) == 34

    distant_values = dict(values, center=dict(x=1e12, y=0, z=-1e12), count=512,
                          radius=.01, spacing=.0002, seed=3)
    distant = mutate('forge_mesh_scatter', arguments=distant_values)
    assert distant['ok'], distant
    created_far = distant['result']['results'][0]['created']
    state = discover()
    far_doc = client.tool('forge_scene_read', target=state['result']['target'])['result']
    far_rows = {e['id']: e for e in far_doc['entities']}
    world_points = []
    for id_ in created_far:
        position = far_rows[id_]['components']['forge.local_translation']
        point = (1e12 + position['x'], -1e12 + position['z'])
        assert math.hypot(point[0]-1e12, point[1]+1e12) <= .01
        for old in world_points:
            assert math.hypot(point[0]-old[0], point[1]-old[1]) >= .0002
        world_points.append(point)
    assert mutate('forge_history_undo')['result']['changed']
