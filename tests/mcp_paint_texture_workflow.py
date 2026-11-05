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
