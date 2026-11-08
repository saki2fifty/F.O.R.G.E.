"""Published source preparation and ordinary Scene placement through MCP."""
import json
from mcp_test_client import failed
from mcp_foliage_stroke_workflow import check_foliage_stroke


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
    assert not plan['aligned_to_slope'] and 'world_normals' not in plan['arguments']
    assert plan['scene_revision'] == revision and plan['terrain_mesh'] == source['asset_id']
    assert read('forge_scene_read') == before and discover()['revision'] == revision
    assert (path / 'Assets/AgentGround.terrain.json').read_bytes() == original_source
    assert (path / 'forge.assets.json').read_bytes() == original_catalog
    selected = read('forge_foliage_terrain_prepare', terrain=surface, arguments=arguments,
                    height_offset=.4, minimum_slope=20, maximum_slope=40, align_to_slope=True)
    assert selected['candidate_count'] == 12 and selected['retained_count'] == 12
    assert selected['arguments']['placement_mask'] == [True] * 12
    assert selected['aligned_to_slope'] and len(selected['arguments']['world_normals']) == 12
    assert all(abs(n['x'] / n['y'] + .5) < 1e-9 and abs(n['z'] / n['y'] + .25) < 1e-9
               for n in selected['arguments']['world_normals'])
    malformed = dict(selected['arguments'], world_normals=[dict(x=0, y=1, z=0)])
    failed(mutate('forge_mesh_scatter', arguments=malformed), 'invalid_arguments')
    assert read('forge_scene_read') == before
    state = discover()
    request = dict(target=state['result']['target'], expected_revision=plan['scene_revision'],
                   sequence=state['next_sequence'], arguments=selected['arguments'])
    placed = client.tool('forge_mesh_scatter', **request)
    assert placed['ok'], placed
    assert client.tool('forge_mesh_scatter', **request) == placed
    assert placed['result']['results'][0]['aligned_to_slope']
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
    check_foliage_stroke(
        read, mutate, client, discover, client.request('tools/list')['result']['tools'],
        surface, arguments, 'heightfield',
        lambda x, z: ground['y'] + .5 * (x - ground['x']) +
                     .25 * (z - ground['z']) + .4)
    failed(client.tool('forge_mesh_scatter', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    for extra in (dict(terrain=prototype), dict(height_offset=True), dict(height_offset=1001), dict(minimum_slope=True), dict(align_to_slope=1),
                  dict(arguments=dict(arguments, world_normals=[dict(x=0,y=1,z=0)] * 12)), dict(maximum_slope=91),
                  dict(minimum_slope=40, maximum_slope=20), dict(maximum_slope=10),
                  dict(arguments=dict(arguments, placement_mask=[True] * 12)),
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

    mixed = content('terrain')
    mixed['tiles'][0]['layers'][0]['heights'] = [0, 0, 4, 0, 0, 4, 0, 0, 4]
    assert edit('forge_asset_document_patch', 'terrain',
                patch=[dict(op='replace', path='', value=mixed)])['ok']
    save_published('terrain')
    selected = read('forge_foliage_terrain_prepare', terrain=surface, arguments=arguments,
                    maximum_slope=10, align_to_slope=True)
    mask = selected['arguments']['placement_mask']
    assert len(mask) == 12 and 0 < sum(mask) < 12
    assert selected['candidate_count'] == 12 and selected['retained_count'] == sum(mask)
    state = discover()
    request = dict(target=state['result']['target'], expected_revision=selected['scene_revision'],
                   sequence=state['next_sequence'], arguments=selected['arguments'])
    placed = client.tool('forge_mesh_scatter', **request)
    assert placed['ok'] and client.tool('forge_mesh_scatter', **request) == placed
    result = placed['result']['results'][0]
    assert result['aligned_to_slope']
    assert result['placed_count'] == sum(mask) and len(result['created']) == sum(mask)
    subset = read('forge_scene_read')
    assert mutate('forge_history_undo')['ok'] and read('forge_scene_read') == after
    assert mutate('forge_history_redo')['ok'] and read('forge_scene_read') == subset

    # Compose the same slope/normal result with real native collider clearance.
    obstacle = create('Clearance obstacle')
    for component in ('forge.physics_body', 'forge.box_collider'):
        assert mutate('forge_component_add', arguments=dict(entity=obstacle, component=component))['ok']
    for component, field, value in (('forge.physics_body', 'motion', 0),
                                    ('forge.box_collider', 'x', 10),
                                    ('forge.box_collider', 'y', 20),
                                    ('forge.box_collider', 'z', 1.6)):
        assert mutate('forge_property_set', arguments=dict(entity=obstacle, component=component, field=field, value=value))['ok']
    assert mutate('forge_transform_position', arguments=dict(entity=obstacle, value=dict(x=2, y=5, z=2.8)))['ok']
    selected = read('forge_foliage_terrain_prepare', terrain=surface, arguments=arguments,
                    maximum_slope=10, align_to_slope=True)
    before_clearance = read('forge_scene_read')
    original_revision = discover()['revision']
    started = read('forge_foliage_collision_start', arguments=selected['arguments'], surface=surface, offset=dict(x=0, y=.5, z=0))
    operation = started['operation']
    assert read('forge_foliage_collision_start', arguments=selected['arguments'], surface=surface)['operation'] == operation
    import time
    deadline = time.monotonic() + 15
    while True:
        checked = read('forge_foliage_collision_read', operation=operation)
        if checked['state'] not in ('preparing', 'querying'):
            break
        assert time.monotonic() < deadline, checked
        time.sleep(.01)
    assert checked['state'] == 'ready', checked
    clearance = checked['result']
    assert clearance['physics_tick'] == 0
    assert clearance['scene_revision'] == original_revision
    assert clearance['arguments']['world_heights'] == selected['arguments']['world_heights']
    assert clearance['arguments']['world_normals'] == selected['arguments']['world_normals']
    combined = clearance['arguments']['placement_mask']
    assert len(combined) == 12 and 0 < sum(combined) < sum(mask), checked
    assert all(not combined[i] or selected['arguments']['placement_mask'][i] for i in range(12))
    assert read('forge_scene_read') == before_clearance and discover()['revision'] == original_revision
    read('forge_foliage_collision_read', operation=prototype, _expected_error='wrong_operation')
    for extra in (dict(radius=0), dict(mask=True), dict(offset=dict(x=0, y=0)),
                  dict(ignored=[obstacle, obstacle])):
        bad = client.tool('forge_foliage_collision_start', target=discover()['result']['target'],
                          **dict(dict(arguments=selected['arguments'], surface=surface), **extra))
        failed(bad, 'invalid_arguments')
        assert read('forge_scene_read') == before_clearance
    state = discover()
    request = dict(target=state['result']['target'], expected_revision=clearance['scene_revision'],
                   sequence=state['next_sequence'], arguments=clearance['arguments'])
    placed = client.tool('forge_mesh_scatter', **request)
    assert placed['ok'] and client.tool('forge_mesh_scatter', **request) == placed, placed
    assert placed['result']['results'][0]['placed_count'] == sum(combined)
    cleared_scene = read('forge_scene_read')
    assert mutate('forge_history_undo')['ok'] and read('forge_scene_read') == before_clearance
    assert mutate('forge_history_redo')['ok'] and read('forge_scene_read') == cleared_scene
    assert read('forge_foliage_collision_read', operation=operation)['state'] == 'cancelled'
    cancel = read('forge_foliage_collision_start', arguments=selected['arguments'], surface=surface)
    assert read('forge_foliage_collision_cancel', operation=cancel['operation'])['state'] == 'cancelled'
    assert read('forge_scene_read') == cleared_scene
