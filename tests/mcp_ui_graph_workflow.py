"""UI graph authoring through the shared source-document MCP operations."""
import json


def check_ui_graph_workflow(read, mutate, edit, status, content, failed, root):
    owner = 'material.graph'
    output = read('forge_asset_graph_node', schema='ui_output')
    assert not output['outputs'] and output['inputs'][0]['key'] == 'color'
    assert read('forge_asset_graph_node', schema='ui_time')['outputs']
    assert read('forge_asset_graph_node', schema='ui_size')['outputs']
    template = read('forge_asset_document_template', editor=owner, domain='ui')
    assert any(node['type'] == 'ui_output' for node in template['graph']['nodes'])
    path = 'Assets/MCP-ui.graph.shader.json'
    assert mutate('forge_asset_document_create', editor=owner, path=path, domain='ui')['ok']
    initial = content(owner)
    before = status(owner)
    assert initial['asset_id'] != template['asset_id']
    assert any(node['type'] == 'ui_output' for node in initial['graph']['nodes'])
    patch = [dict(op='replace', path='/graph/nodes/0/data/value', value=[.1, .7, .4, .8])]
    assert edit('forge_asset_document_patch', owner, patch=patch)['ok']
    changed = content(owner)
    assert changed['graph']['nodes'][0]['data']['value'] == [.1, .7, .4, .8]
    failed(mutate('forge_asset_document_patch', editor=owner,
                  document_session=before['session'], document_revision=before['revision'],
                  patch=patch), 'stale_document')
    assert content(owner) == changed
    assert edit('forge_asset_document_undo', owner)['ok']
    assert content(owner) == initial
    assert edit('forge_asset_document_redo', owner)['ok']
    assert content(owner) == changed
    assert edit('forge_asset_document_close', owner, discard=True)['ok']
    assert json.loads((root / 'Game' / path).read_text()) == initial
    wrong = mutate('forge_asset_document_create', editor='material',
                   path='Assets/Rejected-ui-domain.material.json', domain='ui')
    assert not wrong['ok'] and not (root / 'Game/Assets/Rejected-ui-domain.material.json').exists()
