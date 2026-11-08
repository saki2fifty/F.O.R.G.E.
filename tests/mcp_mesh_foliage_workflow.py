"""Published static geometry prepares ordinary Scene scatter through MCP."""
import json
from mcp_test_client import failed


def check_mesh_foliage_workflow(read, mutate, edit, status, content, root, declared, client, discovery):
    method = next(t for t in declared if t['name'] == 'forge_foliage_surface_prepare')
    assert method['annotations']['readOnlyHint']
    assert set(method['inputSchema']['required']) == {'target', 'surface', 'arguments'}
    owner = 'editable_mesh'
    assert mutate('forge_asset_document_create', editor=owner, path='Assets/Surface.mesh.json')['ok']
    source = content(owner)
    for vertex in source['vertices']:
        x, y, z = vertex['position']
        vertex['position'] = [x * 8, y + 2 * x + 4 * z, z * 8]
    assert edit('forge_asset_document_patch', owner,
                patch=[dict(op='replace', path='', value=source)])['ok']

    def save():
        import time
        assert edit('forge_asset_document_save', owner)['ok']
        end = time.monotonic() + 35
        while True:
            current = status(owner)
            if not current['save_requested'] and not current['publishing']:
                break
            assert time.monotonic() < end, current
            time.sleep(.02)
        assert not current['dirty'] and not current['diagnostic'], current

    save()

    def create(name):
        result = mutate('forge_entity_create', arguments=dict(recipe='primitive.0', name=name))
        assert result['ok'], result
        return result['result']['results'][0]['selected']

    surface, prototype = create('Published ramp'), create('Foliage template')
    assert mutate('forge_property_set', arguments=dict(entity=surface,
        component='forge.mesh_renderer', field='mesh', value=source['asset_id']))['ok']
    assert mutate('forge_transform_local', arguments=dict(entity=surface,
        translation=dict(x=3, y=2, z=-4), scale=dict(x=-2, y=1.5, z=1)))['ok']
    arguments = dict(entity=prototype, name='Mesh meadow', center=dict(x=3, y=7, z=-4),
                     radius=.8, spacing=.1, count=12, seed=3,
                     scale_min=.8, scale_max=1.2, yaw_min=0, yaw_max=360)
    game = root / 'Game'
    before = read('forge_scene_read')
    catalog = (game / 'forge.assets.json').read_bytes()
    plan = read('forge_foliage_surface_prepare', surface=surface, arguments=arguments,
                height_offset=.4, align_to_slope=True, minimum_slope=10, maximum_slope=60)
    assert plan['surface_profile'] == 'static_mesh_lod0_v1'
    assert plan['surface_mesh'] == source['asset_id']
    assert plan['scene_revision'] == discovery()['revision']
    assert plan['candidate_count'] == plan['retained_count'] == 12
    assert plan['arguments']['placement_mask'] == [True] * 12
    assert all(abs(n['x'] / n['y'] - .1875) < 1e-10 and abs(n['z'] / n['y'] + .75) < 1e-10
               for n in plan['arguments']['world_normals'])
    assert read('forge_scene_read') == before and (game / 'forge.assets.json').read_bytes() == catalog
    placed = mutate('forge_mesh_scatter', arguments=plan['arguments'])
    assert placed['ok'], placed
    after = read('forge_scene_read')
    rows = {row['id']: row for row in after['entities']}
    for id_ in placed['result']['results'][0]['created']:
        p = rows[id_]['components']['forge.local_translation']
        expected = 2.75 - .1875 * p['x'] + .75 * p['z'] + .4
        assert abs(7 + p['y'] - expected) < 1e-10
    assert mutate('forge_history_undo')['result']['changed'] and read('forge_scene_read') == before
    assert mutate('forge_history_redo')['result']['changed'] and read('forge_scene_read') == after
    assert mutate('forge_document_save')['ok']
    assert json.loads((game / 'Scenes/main.scene.json').read_bytes()) == after

    for extra in [dict(surface=prototype, arguments=dict(arguments, center=dict(x=100, y=0, z=100))),
                  dict(arguments=dict(arguments, world_heights=[0] * 12)),
                  dict(arguments=dict(arguments, placement_mask=[True] * 12)),
                  dict(arguments=dict(arguments, world_normals=[dict(x=0, y=1, z=0)] * 12)),
                  dict(maximum_slope=5), dict(minimum_slope=60, maximum_slope=10),
                  dict(height_offset=1001), dict(align_to_slope=1)]:
        state = discovery()
        result = client.tool('forge_foliage_surface_prepare', target=state['result']['target'],
                             **dict(dict(surface=surface, arguments=arguments), **extra))
        assert not result['ok'] and read('forge_scene_read') == after, result
        assert discovery()['revision'] == state['revision']

    # Published selection stays authoritative over unpublished on-disk geometry.
    path = game / 'Assets/Surface.mesh.json'
    original = path.read_bytes()
    changed = json.loads(original)
    changed['vertices'][0]['position'][1] += 1000
    path.write_text(json.dumps(changed))
    retained = read('forge_foliage_surface_prepare', surface=surface, arguments=arguments,
                    height_offset=.4, align_to_slope=True, minimum_slope=10, maximum_slope=60)
    assert retained["scene_revision"] == discovery()["revision"]
    assert dict(retained, scene_revision=plan["scene_revision"]) == plan
    path.write_bytes(original)

    cache, hidden = game / '.forge/cache/derived', game / '.forge/cache/hidden'
    cache.rename(hidden)
    try:
        state = discovery()
        result = client.tool('forge_foliage_surface_prepare', target=state['result']['target'],
                             surface=surface, arguments=arguments)
        assert not result['ok'] and not cache.exists(), result
        assert read('forge_scene_read') == after
    finally:
        hidden.rename(cache)
    assert (game / 'forge.assets.json').read_bytes() == catalog
