"""Graph source ownership, exact target-bound clips and revision safety over MCP."""
import time
import uuid
from mcp_animation_retarget_workflow import check_animation_retarget_workflow


def check_animation_graph_workflow(read, mutate, edit, status, content, failed,
                                   root, client, discovery):
    owner = 'animation_graph'
    profile = read('forge_asset_animation_graph_catalog')
    assert profile['editor'] == owner and profile['suffix'] == '.animgraph.json'
    assert profile['limits']['nodes'] == 32 and profile['limits']['clip_nodes'] == 8
    template = read('forge_asset_document_template', editor=owner)
    assert template['skeleton'] is None and template['nodes'] == [] and template['output'] is None
    assert not mutate('forge_asset_document_create', editor=owner, path='../Outside.animgraph.json')['ok']
    assert not (root / 'Outside.animgraph.json').exists()

    # Establish real imported and baked clips through the existing owner workflow.
    # No direct registry injection or fabricated cooked artifacts.
    check_animation_retarget_workflow(read, mutate, edit, status, content, failed,
                                      root, client, discovery)
    def asset(kind, path):
        rows = read('forge_asset_search', type=kind, text=path, include_engine=False)['assets']
        assert len(rows) == 1, rows
        return rows[0]['asset']
    skeleton = asset('skeleton', 'Assets/RetargetTarget.gltf')
    imported = asset('animation_clip', 'Assets/RetargetTarget.gltf')
    baked = asset('animation_clip', 'Assets/Transferred.retarget.json')
    foreign = asset('animation_clip', 'Assets/RetargetSource.gltf')
    first, second, output = [str(uuid.uuid4()) for _ in range(3)]
    nodes = [dict(id=output, kind='additive', name='Layer', first=first, second=second,
                  mask=None, weight=dict(input=4, scale=1, bias=0)),
             dict(id=second, kind='clip', name='Baked motion', clip=baked),
             dict(id=first, kind='clip', name='Imported rest', clip=imported)]
    path = 'Assets/Layered.animgraph.json'
    patch = [dict(op='replace', path='/' + key, value=value) for key, value in dict(
        skeleton=skeleton, cycle_seconds=2.5,
        inputs=[dict(slot=4, name='Layer amount', initial=.5)], nodes=nodes, output=output).items()]
    result = mutate('forge_asset_document_create', editor=owner, path=path, patch=patch)
    assert result['ok'], result
    initial = status(owner)
    graph = initial['asset']
    original = content(owner)
    # The exact same request replays its receipt without a second source edit.
    before = discovery()
    request = dict(target=before['result']['target'], expected_revision=before['revision'],
                   sequence=before['next_sequence'], editor=owner,
                   document_session=initial['session'], document_revision=initial['revision'],
                   patch=[dict(op='replace', path='/inputs/0/initial', value=.25)])
    accepted = client.tool('forge_asset_document_patch', **request)
    assert accepted['ok'], accepted
    assert client.tool('forge_asset_document_patch', **request) == accepted
    assert status(owner)['revision'] == initial['revision'] + 1
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok']
    settled = status(owner)
    failed(mutate('forge_asset_document_patch', editor=owner,
                  document_session=initial['session'], document_revision=initial['revision'],
                  patch=[dict(op='replace', path='/cycle_seconds', value=3)]), 'stale_document')
    for patch in ([dict(op='replace', path='/nodes/0/first', value=output)],
                  [dict(op='replace', path='/nodes/0/second', value=str(uuid.uuid4()))],
                  [dict(op='replace', path='/nodes/0/weight/input', value=1)],
                  [dict(op='replace', path='/asset_id', value=baked)],
                  [dict(op='replace', path='/cycle_seconds', value=0)]):
        unchanged = content(owner)
        assert not edit('forge_asset_document_patch', owner, patch=patch)['ok']
        assert status(owner)['revision'] == settled['revision'] and content(owner) == unchanged
    layout = dict(format='forge.animation-graph-layout', version=1,
                  positions=[dict(node=output, x=300, y=-50)])
    assert edit('forge_asset_document_patch', owner,
                patch=[dict(op='add', path='/editor_layout', value=layout)])['ok']
    assert content(owner)['editor_layout'] == layout
    assert not edit('forge_asset_document_patch', owner,
                    patch=[dict(op='replace', path='/editor_layout/positions/0/node', value=baked)])['ok']

    def publish(success=True):
        result = edit('forge_asset_document_save', owner)
        assert result['ok'], result
        deadline = time.monotonic() + 35
        while True:
            value = status(owner)
            if not value['save_requested'] and not value['publishing']:
                break
            assert time.monotonic() < deadline, value
            time.sleep(.02)
        if success:
            assert not value['dirty'] and not value['unpublished'] and not value['diagnostic'], value
        else:
            assert value['unpublished'] and value['diagnostic'], value
    publish()
    selected = read('forge_asset_read', asset=graph)
    assert selected['type'] == 'animation_graph'
    metadata = selected['metadata']
    assert metadata['forge.import']['importer'] == 'forge.animation.graph'
    assert metadata['forge.animation_graph']['file'] == 'graph.json'
    # A well-formed but foreign clip reaches exact-skeleton cook admission.
    assert edit('forge_asset_document_patch', owner,
                patch=[dict(op='replace', path='/nodes/1/clip', value=foreign)])['ok']
    publish(False)
    assert read('forge_asset_read', asset=graph)['metadata'] == metadata
    assert edit('forge_asset_document_undo', owner)['ok']
    publish()
    # Graph mode and all four stable overrides use normal reflected Scene edits.
    created = mutate('forge_entity_create', arguments=dict(kind=0, name='Graph Animator'))
    assert created['ok'], created
    actor = created['result']['results'][0]['selected']
    assert mutate('forge_component_add', arguments=dict(entity=actor, component='forge.animator'))['ok']
    values = dict(skeleton=skeleton, graph=graph, clip=None, graph_override_4=True, graph_input_4=.75)
    result = mutate('forge_property_set_many', arguments=dict(entity=actor, component='forge.animator', values=values))
    assert result['ok'], result
    assert mutate('forge_history_undo')['ok'] and mutate('forge_history_redo')['ok']
    final = content(owner)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == final and status(owner)['session'] != initial['session']
    failed(mutate('forge_asset_document_patch', editor=owner,
                  document_session=initial['session'], document_revision=1,
                  patch=[dict(op='replace', path='/cycle_seconds', value=1)]), 'wrong_document')
    assert edit('forge_asset_document_close', owner)['ok']
