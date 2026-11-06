"""Editable pixels through the editor's source owner and MCP transport."""
import json
import time


def check_paint_texture_workflow(read, mutate, edit, status, content, failed, root,
                                 client, discovery):
    owner = 'paint_texture'
    catalog = read('forge_asset_paint_texture_catalog')
    assert catalog['format'] == 'forge.paint-texture' and catalog['limits']['dimension'] == 1024
    template = read('forge_asset_document_template', editor=owner,
                    canvas=dict(width=67, height=35, semantic='data'))
    assert template['width'] == 67 and template['height'] == 35 and template['semantic'] == 'data'
    assert not status(owner)['open']
    invalid = mutate('forge_asset_document_create', editor=owner,
                     path='Assets/Invalid.paint-texture.json', canvas=dict(width=0))
    assert not invalid['ok'] and not (root / 'Game/Assets/Invalid.paint-texture.json').exists()
    path = 'Assets/MCP-Painted.paint-texture.json'
    created = mutate('forge_asset_document_create', editor=owner, path=path,
                     canvas=dict(width=67, height=35, semantic='data'))
    assert created['ok'], created
    original = status(owner)
    asset = original['asset']
    disk = root / 'Game' / path
    baseline = disk.read_bytes()

    def pixels(points):
        return read('forge_asset_document_pixels', editor=owner,
                    document_session=status(owner)['session'], points=points)['pixels']

    assert pixels([[0, 0], [66, 34]]) == [[0, 0, 0, 0]] * 2
    assert edit('forge_asset_document_fill', owner, rgba=[20, 40, 60, 80])['ok']
    assert pixels([[0, 0], [66, 34]]) == [[20, 40, 60, 80]] * 2
    assert disk.read_bytes() == baseline
    failed(mutate('forge_asset_document_fill', editor=owner,
                  document_session=original['session'], document_revision=original['revision'],
                  rgba=[1, 2, 3, 4]), 'stale_document')
    assert edit('forge_asset_document_undo', owner)['ok']
    assert pixels([[0, 0]]) == [[0, 0, 0, 0]]
    assert edit('forge_asset_document_redo', owner)['ok']

    before = status(owner)
    envelope = discovery()
    arguments = dict(target=envelope['result']['target'], expected_revision=envelope['revision'],
                     sequence=envelope['next_sequence'], editor=owner,
                     document_session=before['session'], document_revision=before['revision'],
                     points=[[4.5, 8.5], [58.5, 8.5]],
                     brush=dict(color=[1, 0, 0, 1], opacity=.5, radius=4, hardness=1))
    accepted = client.tool('forge_asset_document_stroke', **arguments)
    assert accepted['ok'] and accepted['result']['transient_draft'], accepted
    # An identical transport retry must return its receipt without another stroke.
    assert client.tool('forge_asset_document_stroke', **arguments) == accepted
    deadline = time.monotonic() + 15
    while status(owner)['transient_draft'] and time.monotonic() < deadline:
        time.sleep(.01)
    after = status(owner)
    assert not after['transient_draft'] and not after['diagnostic']
    assert after['revision'] == before['revision'] + 1
    assert pixels([[20, 8], [0, 34]]) == [[138, 20, 30, 168], [20, 40, 60, 80]]
    assert edit('forge_asset_document_undo', owner)['ok']
    assert pixels([[20, 8]]) == [[20, 40, 60, 80]]
    assert edit('forge_asset_document_redo', owner)['ok']
    assert pixels([[20, 8]]) == [[138, 20, 30, 168]]
    settled = status(owner)
    # Metadata cannot replace the canvas or identity, and failures preserve pixels.
    for pointer, value in [('/width', 68), ('/asset_id', '00000000-0000-4000-8000-000000000099')]:
        result = edit('forge_asset_document_patch', owner,
                      patch=[dict(op='replace', path=pointer, value=value)])
        assert not result['ok'] and status(owner)['revision'] == settled['revision']
    for changes in [dict(radius=0), dict(color=[True, 0, 0, 1]), dict(opacity=2)]:
        brush = dict(color=[1, 0, 0, 1], radius=4)
        brush.update(changes)
        assert not edit('forge_asset_document_stroke', owner,
                        points=[[1, 1]], brush=brush)['ok']
    assert status(owner)['revision'] == settled['revision']
    assert not client.tool('forge_asset_document_pixels', target=discovery()['result']['target'],
                           editor=owner, document_session=settled['session'], points=[[67, 0]])['ok']
    # Exact disk baseline conflict retains both the authored file and the draft.
    disk.write_bytes(baseline + b' ')
    rejected = edit('forge_asset_document_save', owner)
    assert not rejected['ok'] and status(owner)['source_dirty'] and disk.read_bytes() == baseline + b' '
    disk.write_bytes(baseline)
    assert edit('forge_asset_document_save', owner)['ok']
    deadline = time.monotonic() + 20
    while (status(owner)['publishing'] or status(owner)['save_requested']) and time.monotonic() < deadline:
        time.sleep(.02)
    published = status(owner)
    assert not published['source_dirty'] and not published['unpublished'] and not published['diagnostic'], published
    records = json.loads((root / 'Game/forge.assets.json').read_text())['assets']
    selected = next(row for row in records if row['id'] == asset)
    assert selected['type'] == 'texture' and selected['metadata']['forge.import']['importer'] == 'forge.texture.paint'
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert pixels([[20, 8], [0, 34]]) == [[138, 20, 30, 168], [20, 40, 60, 80]]
    assert content(owner)['asset_id'] == asset
    assert edit('forge_asset_document_fill', owner, rgba=[1, 2, 3, 4])['ok']
    assert edit('forge_asset_document_copy_pixels', owner, texture=asset)['ok']
    deadline = time.monotonic() + 15
    while status(owner)['transient_draft'] and time.monotonic() < deadline:
        time.sleep(.01)
    assert not status(owner)['diagnostic']
    assert pixels([[20, 8], [0, 34]]) == [[138, 20, 30, 168], [20, 40, 60, 80]]
    assert edit('forge_asset_document_undo', owner)['ok']
    assert pixels([[20, 8]]) == [[1, 2, 3, 4]]
    assert edit('forge_asset_document_redo', owner)['ok']
    assert pixels([[20, 8]]) == [[138, 20, 30, 168]]
    assert edit('forge_asset_document_close', owner, discard=True)['ok']

    # Color paint stores straight alpha and encodes linear brush RGB once.
    assert mutate('forge_asset_document_create', editor=owner,
                  path='Assets/Color.paint-texture.json', canvas=dict(width=9, height=7))['ok']
    assert edit('forge_asset_document_stroke', owner, points=[[4.5, 3.5]],
                brush=dict(color=[.5, 0, 0, .5], radius=2, hardness=1))['ok']
    deadline = time.monotonic() + 15
    while status(owner)['transient_draft'] and time.monotonic() < deadline:
        time.sleep(.01)
    assert pixels([[4, 3]]) == [[188, 0, 0, 128]]
    assert edit('forge_asset_document_stroke', owner, points=[[4.5, 3.5]],
                brush=dict(color=[0, 0, 0, 0], opacity=.5, erase=True, radius=2, hardness=1))['ok']
    deadline = time.monotonic() + 15
    while status(owner)['transient_draft'] and time.monotonic() < deadline:
        time.sleep(.01)
    assert pixels([[4, 3]]) == [[188, 0, 0, 64]]
    assert edit('forge_asset_document_cancel_pixels', owner)['ok']
    assert edit('forge_asset_document_close', owner, discard=True)['ok']

    # Explicit published mesh coordinates use the same texture draft and brush.
    def settled_pixels():
        deadline = time.monotonic() + 15
        while status(owner)['transient_draft'] and time.monotonic() < deadline:
            time.sleep(.01)
        result = status(owner)
        assert not result['transient_draft'], result
        return result

    def publish(editor):
        assert edit('forge_asset_document_save', editor)['ok']
        deadline = time.monotonic() + 20
        while (status(editor)['publishing'] or status(editor)['save_requested']) and time.monotonic() < deadline:
            time.sleep(.02)
        result = status(editor)
        assert not result['publishing'] and not result['source_dirty'] and not result['diagnostic'], result

    assert mutate('forge_asset_document_create', editor=owner,
                  path='Assets/MeshPaint.paint-texture.json', canvas=dict(width=32, height=32))['ok']
    paint_id = status(owner)['asset']
    publish(owner)
    assert mutate('forge_asset_document_create', editor='material',
                  path='Assets/MeshPaint.material.json')['ok']
    image_slot = read('forge_asset_render_texture_catalog')['material']['binding_template']['slot']
    assert edit('forge_asset_document_patch', 'material', patch=[
        dict(op='replace', path='/overrides', value=dict(
            model='forge.gltf.unlit.v1', double_sided=True,
            textures=dict(baseColorTexture=dict(asset=paint_id, slot=image_slot))))])['ok']
    material_id = content('material')['asset_id']
    publish('material')
    assert edit('forge_asset_document_close', 'material')['ok']
    quad = read('forge_asset_search', text='Quad', type='mesh', limit=1)['assets'][0]
    geometry = read('forge_asset_mesh_triangles', mesh=quad['asset'], limit=1)
    assert geometry['revision'] == quad['revision'] and geometry['triangle_count'] == 2
    triangle = geometry['triangles'][0]
    assert triangle['primitive'] == 0 and len(triangle['positions']) == len(triangle['uv0']) == 3
    assert len(read('forge_asset_mesh_triangles', mesh=quad['asset'], offset=1, limit=1)['triangles']) == 1
    assert read('forge_asset_mesh_triangles', mesh=quad['asset'], offset=2, limit=1)['triangles'] == []
    rejected = client.tool('forge_asset_mesh_triangles', target=discovery()['result']['target'],
                           mesh=quad['asset'], revision='0' * 64)
    assert not rejected['ok']
    material_record = read('forge_asset_read', asset=material_id)
    mesh_arguments = dict(mesh=quad['asset'], mesh_revision=quad['revision'],
                          material=material_id,
                          material_revision=material_record['metadata']['forge.import']['key'],
                          samples=[dict(primitive=0, barycentric=[1/3, 1/3, 1/3])],
                          brush=dict(color=[1, 0, 0, 1], radius=2, hardness=1))
    all_points = [[x, y] for y in range(32) for x in range(32)]
    empty = pixels(all_points)
    assert all(p == [0, 0, 0, 0] for p in empty)
    before = status(owner)
    assert edit('forge_asset_document_mesh_stroke', owner, **mesh_arguments)['ok']
    result = settled_pixels()
    assert not result['diagnostic'] and result['revision'] == before['revision'] + 1, result
    painted = pixels(all_points)
    assert sum(p[3] > 0 for p in painted) > 4
    assert all(p[0] == 255 and p[1] == p[2] == 0 for p in painted if p[3])
    uv = [sum(p[axis] for p in triangle['uv0']) / 3 for axis in range(2)]
    assert pixels([[min(31, int(uv[0] * 32)), min(31, int(uv[1] * 32))]]) == [[255, 0, 0, 255]]
    assert edit('forge_asset_document_undo', owner)['ok'] and pixels(all_points) == empty
    assert edit('forge_asset_document_redo', owner)['ok'] and pixels(all_points) == painted
    # Invalid revisions, triangle bounds and barycentrics leave both draft/history intact.
    before = status(owner)
    for change in [dict(mesh_revision='0' * 64), dict(material_revision='0' * 64),
                   dict(samples=[dict(primitive=999, barycentric=[1, 0, 0])]),
                   dict(samples=[dict(primitive=0, barycentric=[.5, .5, .5])])]:
        arguments = mesh_arguments | change
        assert edit('forge_asset_document_mesh_stroke', owner, **arguments)['ok']
        result = settled_pixels()
        assert result['diagnostic'] and result['revision'] == before['revision'], result
        assert pixels(all_points) == painted
    # Explicit path breaks preserve a hole instead of drawing between islands.
    assert edit('forge_asset_document_fill', owner, rgba=[0, 0, 0, 0])['ok']
    assert edit('forge_asset_document_stroke', owner,
                points=[[3.5, 16.5], [28.5, 16.5]], breaks=[1],
                brush=dict(color=[0, 1, 0, 1], radius=1, hardness=1))['ok']
    assert not settled_pixels()['diagnostic']
    assert pixels([[3, 16], [16, 16], [28, 16]]) == [[0, 255, 0, 255], [0, 0, 0, 0], [0, 255, 0, 255]]
    revision = status(owner)['revision']
    for breaks in ([0], [2], [1, 1]):
        assert not edit('forge_asset_document_stroke', owner,
                        points=[[3, 16], [28, 16]], breaks=breaks,
                        brush=dict(color=[1, 0, 0, 1]))['ok']
        assert status(owner)['revision'] == revision
    assert edit('forge_asset_document_close', owner, discard=True)['ok']
