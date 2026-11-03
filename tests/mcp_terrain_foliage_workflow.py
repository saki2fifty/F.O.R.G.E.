"""Published source preparation and ordinary Scene placement through MCP."""
import json
from mcp_test_client import failed


def check_terrain_foliage_workflow(read, mutate, edit, content, save_published,
                                  discover, client, root):
    assert mutate('forge_asset_document_create', editor='terrain',
                  path='Assets/AgentGround.terrain.json')['ok']
    source = content('terrain')
    source['side'] = 3
    source['tile_size'] = 4
    source['tiles'][0]['layers'][0]['heights'] = [0, 1, 2, .5, 1.5, 2.5, 1, 2, 3]
    source['tiles'][0]['layers'][0]['mask'] = [1] * 9
    assert edit('forge_asset_document_patch', 'terrain',
                patch=[dict(op='replace', path='', value=source)])['ok']
    save_published('terrain')

    def create(name):
        value = mutate('forge_entity_create', arguments=dict(recipe='primitive.0', name=name))
        assert value['ok'], value
        return value['result']['results'][0]['selected']

    surface, prototype = create('Terrain'), create('Foliage template')
    assert mutate('forge_property_set', arguments=dict(entity=surface,
        component='forge.mesh_renderer', field='mesh', value=source['asset_id']))['ok']
    arguments = dict(entity=prototype, name='Projected meadow', center=dict(x=2, y=7, z=2),
                     radius=.8, spacing=.1, count=12, seed=3,
                     scale_min=.8, scale_max=1.2, yaw_min=0, yaw_max=360)
    path = root / 'Game'
    original_source = (path / 'Assets/AgentGround.terrain.json').read_bytes()
    original_catalog = (path / 'forge.assets.json').read_bytes()
    before = read('forge_scene_read')
    ground = next(r for r in before['entities'] if r['id'] == surface)['components']['forge.local_translation']
    revision = discover()['revision']
    plan = read('forge_foliage_terrain_prepare', terrain=surface, arguments=arguments,
                height_offset=.4)
    assert plan['scene_revision'] == revision and plan['terrain_mesh'] == source['asset_id']
    assert read('forge_scene_read') == before and discover()['revision'] == revision
    assert (path / 'Assets/AgentGround.terrain.json').read_bytes() == original_source
    assert (path / 'forge.assets.json').read_bytes() == original_catalog
    state = discover()
    request = dict(target=state['result']['target'], expected_revision=plan['scene_revision'],
                   sequence=state['next_sequence'], arguments=plan['arguments'])
    placed = client.tool('forge_mesh_scatter', **request)
    assert placed['ok'], placed
    assert client.tool('forge_mesh_scatter', **request) == placed
    after = read('forge_scene_read')
    rows = {r['id']: r for r in after['entities']}
    for id_ in placed['result']['results'][0]['created']:
        p = rows[id_]['components']['forge.local_translation']
        expected = ground['y'] + .5 * (2 + p['x'] - ground['x']) + .25 * (2 + p['z'] - ground['z']) + .4
        assert abs(7 + p['y'] - expected) < 1e-10
    assert mutate('forge_history_undo')['result']['changed']
    assert read('forge_scene_read') == before
    assert mutate('forge_history_redo')['result']['changed']
    assert read('forge_scene_read') == after
    failed(client.tool('forge_mesh_scatter', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    for extra in (dict(terrain=prototype), dict(height_offset=True), dict(height_offset=1001),
                  dict(arguments=dict(arguments, world_heights=[0] * 12)),
                  dict(arguments=dict(arguments, center=dict(x=1000, y=0, z=1000)))):
        state = discover()
        result = client.tool('forge_foliage_terrain_prepare', target=state['result']['target'],
            **dict(dict(terrain=surface, arguments=arguments), **extra))
        assert not result['ok'] and discover()['revision'] == state['revision'], result
        assert read('forge_scene_read') == after
    changed = json.loads(original_source)
    changed['tiles'][0]['layers'][0]['heights'][4] = 10
    (path / 'Assets/AgentGround.terrain.json').write_text(json.dumps(changed))
    state = discover()
    failed(client.tool('forge_foliage_terrain_prepare', target=state['result']['target'],
                       terrain=surface, arguments=arguments), 'surface_source_changed')
    assert read('forge_scene_read') == after and discover()['revision'] == state['revision']
    (path / 'Assets/AgentGround.terrain.json').write_bytes(original_source)
    assert mutate('forge_document_save')['ok']
    assert json.loads((path / 'Scenes/main.scene.json').read_bytes()) == after
