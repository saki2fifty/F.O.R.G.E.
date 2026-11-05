"""Camera output and mesh Material assignment through the shared MCP owners."""
import json


def check_render_texture_workflow(read, mutate, edit, status, content, save_published, output, root, discovery):
    def ok(tool, **arguments):
        result = mutate(tool, **arguments)
        assert result['ok'], result
        return result['result']

    catalog = read('forge_asset_render_texture_catalog')
    assert catalog['surface_input']['component'] == 'forge.ui_surface_input'
    assert catalog['surface_input']['profile'] == 1
    assert catalog['surface_input']['maximum_surfaces'] == 64
    binding = json.loads(json.dumps(catalog['material']['binding_template']))
    assert binding['render_texture'] is None and isinstance(binding['slot'], dict)
    binding['render_texture'] = output
    ok('forge_asset_document_create', editor='material', path='Assets/MCP-CameraScreen.material.json')
    assert edit('forge_asset_document_patch', 'material', patch=[
        dict(op='replace', path='/version', value=3),
        dict(op='replace', path='/overrides', value=dict(
            model='forge.gltf.unlit.v1', double_sided=True,
            textures=dict(baseColorTexture=binding)))])['ok']
    material = content('material')['asset_id']
    save_published('material')
    records = json.loads((root / 'Game/forge.assets.json').read_text())['assets']
    selected = next(row for row in records if row['id'] == material)
    assert selected['metadata']['forge.material']['render_textures']['baseColorTexture'] == output
    assert edit('forge_asset_document_close', 'material')['ok']

    camera = ok('forge_entity_create', arguments=dict(kind=4, name='MCP output camera'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=camera, component='forge.camera'))
    ok('forge_property_set', arguments=dict(entity=camera, component='forge.camera', field='render_texture', value=output))

    def component(entity, name):
        row = next(row for row in read('forge_scene_read')['entities'] if row['id'] == entity)
        return row['components'][name]

    assert component(camera, 'forge.camera')['render_texture'] == output
    ok('forge_history_undo')
    assert component(camera, 'forge.camera')['render_texture'] is None
    ok('forge_history_redo')
    assert component(camera, 'forge.camera')['render_texture'] == output

    meshes = read('forge_asset_search', text='Quad', type='mesh', limit=1)
    assert meshes['assets'] and meshes['assets'][0]['engine_owned']
    quad = meshes['assets'][0]['asset']
    screen = ok('forge_entity_create', arguments=dict(kind=4, name='MCP camera screen'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=screen, component='forge.mesh_renderer'))
    ok('forge_property_set', arguments=dict(entity=screen, component='forge.mesh_renderer', field='mesh', value=quad))
    ok('forge_property_set', arguments=dict(entity=screen, component='forge.mesh_renderer', field='materials',
                                          value=[dict(slot='surface', material=material)]))
    assert component(screen, 'forge.mesh_renderer')['materials'] == [dict(slot='surface', material=material)]
    surface_schema = next(row for row in discovery()['result']['schema']['components']
                          if row['id'] == 'forge.ui_surface_input')
    assert surface_schema['display_name'] == 'UI Surface Input'
    target = next(row for row in surface_schema['fields'] if row['id'] == 'render_texture')
    assert target['asset_type'] == 'render_texture' and target['nullable']
    ok('forge_component_add', arguments=dict(entity=screen, component='forge.ui_surface_input'))
    assert component(screen, 'forge.ui_surface_input') == dict(render_texture=None, enabled=True)
    revision = discovery()['revision']
    rejected = mutate('forge_property_set', arguments=dict(entity=screen,
        component='forge.ui_surface_input', field='render_texture', value=42))
    assert not rejected['ok'] and discovery()['revision'] == revision
    ok('forge_property_set', arguments=dict(entity=screen, component='forge.ui_surface_input',
                                           field='render_texture', value=output))
    assert component(screen, 'forge.ui_surface_input')['render_texture'] == output
    ok('forge_history_undo')
    assert component(screen, 'forge.ui_surface_input')['render_texture'] is None
    ok('forge_history_redo')
    assert component(screen, 'forge.ui_surface_input')['render_texture'] == output
    ok('forge_property_set', arguments=dict(entity=screen, component='forge.ui_surface_input',
                                           field='enabled', value=False))
    assert not component(screen, 'forge.ui_surface_input')['enabled']
    ok('forge_history_undo')
    assert component(screen, 'forge.ui_surface_input')['enabled']
    # Runtime UI uses the same reflected assignment/history owners as Cameras.
    schema = next(row for row in discovery()['result']['schema']['components']
                  if row['id'] == 'forge.ui_document')
    field = next(row for row in schema['fields'] if row['id'] == 'render_texture')
    assert field['asset_type'] == 'render_texture' and field['nullable']
    assert field['default_if_missing'] is None
    panel = ok('forge_entity_create', arguments=dict(kind=4, name='MCP output UI'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=panel, component='forge.ui_document'))
    assert component(panel, 'forge.ui_document')['render_texture'] is None
    before = discovery()['revision']
    rejected = mutate('forge_property_set', arguments=dict(entity=panel, component='forge.ui_document',
                                                          field='render_texture', value=42))
    assert not rejected['ok'] and discovery()['revision'] == before
    assert component(panel, 'forge.ui_document')['render_texture'] is None
    ok('forge_property_set', arguments=dict(entity=panel, component='forge.ui_document',
                                           field='render_texture', value=output))
    assert component(panel, 'forge.ui_document')['render_texture'] == output
    ok('forge_history_undo')
    assert component(panel, 'forge.ui_document')['render_texture'] is None
    ok('forge_history_redo')
    assert component(panel, 'forge.ui_document')['render_texture'] == output
    ok('forge_property_set', arguments=dict(entity=panel, component='forge.ui_document',
                                           field='render_texture', value=None))
    assert component(panel, 'forge.ui_document')['render_texture'] is None
    ok('forge_history_undo')
    assert component(panel, 'forge.ui_document')['render_texture'] == output
    ok('forge_document_save')
    # Rejected source edits did not replace the output's persistent identity.
    assert any(row['id'] == output and row['type'] == 'render_texture' for row in records)
