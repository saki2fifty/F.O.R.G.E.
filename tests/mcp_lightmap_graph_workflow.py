"""Imported-irradiance graph authoring through the existing document owner."""
import copy
import json
import uuid


def check_lightmap_graph_workflow(read, mutate, edit, status, content, failed, root):
    owner = 'material.graph'
    path = 'Assets/ImportedIrradiance.shader.json'
    output = read('forge_asset_graph_node', schema='surface')
    port = next(p for p in output['inputs'] if p['key'] == 'lightmap_irradiance')
    assert port == dict(key='lightmap_irradiance', label='Lightmap irradiance', type='color3')
    assert mutate('forge_asset_document_create', editor=owner, path=path, domain='mesh')['ok']
    before = content(owner)
    graph = copy.deepcopy(before['graph'])
    surface = next(n for n in graph['nodes'] if n['type'] == 'surface')
    texture = read('forge_asset_graph_node', schema='texture2d')['node']
    texture['data'].update(key='lightmap', label='Imported irradiance', semantic='hdr', uv_set=3)
    assert read('forge_asset_graph_node', node=texture)['outputs'][0]['type'] == 'color4'
    convert = read('forge_asset_graph_node', schema='convert')['node']
    convert['data'].update({'from': 'color4', 'type': 'color3'})
    gain = read('forge_asset_graph_node', schema='parameter')['node']
    gain['data'].update(key='lightmap_gain', label='Lightmap gain', type='color3', value=[1, 1, 1])
    multiply = read('forge_asset_graph_node', schema='multiply')['node']
    multiply['data']['type'] = 'color3'
    for node in (texture, convert, gain, multiply):
        graph['nodes'].append(node)
    for source, target, port in ((texture, convert, 'value'), (convert, multiply, 'a'),
                                 (gain, multiply, 'b'), (multiply, surface, 'lightmap_irradiance')):
        graph['edges'].append(dict(id=str(uuid.uuid4()),
                                  **{'from': dict(node=source['id'], port='out'),
                                     'to': dict(node=target['id'], port=port)}))
    old = status(owner)
    patch = [dict(op='replace', path='/graph', value=graph)]
    changed = edit('forge_asset_document_patch', owner, patch=patch, label='Imported irradiance')
    assert changed['ok'], changed
    authored = content(owner)
    assert authored['graph'] == graph and authored['asset_id'] == before['asset_id']
    failed(mutate('forge_asset_document_patch', editor=owner, document_session=old['session'],
                  document_revision=old['revision'], patch=patch), 'stale_document')
    assert content(owner) == authored
    assert edit('forge_asset_document_undo', owner)['ok']
    assert content(owner) == before
    assert edit('forge_asset_document_redo', owner)['ok']
    assert content(owner) == authored
    # Draft source permits incomplete connections; compile/publication validates
    # shader expressions separately. This host has no Windows cook backend.
    saved = edit('forge_asset_document_save', owner)
    assert saved['ok'], saved
    assert json.loads((root / 'Game' / path).read_text(encoding='utf-8')) == authored
    current = status(owner)
    assert not current['source_dirty'] and current['unpublished'] and current['save_requested'], current
    # The source-only host intentionally does not pump Shader compilation. Leave
    # its publication request queued for clean host shutdown; native source
    # document tests cover reopening, and Windows tests own actual publication.
