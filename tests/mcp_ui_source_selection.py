"""Selection uses the native source owner without adding document history."""
from mcp_test_client import failed


def check_selection(read, ok, mutate, root, advertised, client, arguments, guard, edit, path):
    edit('save')
    ok('forge_source_ui_register', path=path)
    catalog = (root / 'Game/forge.assets.json').read_bytes()
    disk = (root / 'Game' / path).read_bytes()
    scene = read('forge_scene_read')
    items = ''.join(f'<div id="item{i}" style="position:absolute;left:{20+i*12}px;top:40px;'
                    'width:10px;height:10px;"></div>' for i in range(33))
    source = ('<rml><body><div id="parent" style="position:absolute;left:0px;top:0px;'
              'width:800px;height:600px;">' + items +
              '<div id="hidden" style="visibility:hidden;position:absolute;left:20px;top:40px;'
              'width:10px;height:10px;"></div>' +
              '<div id="ambiguous" style="position:absolute;left:20px;top:40px;width:10px;height:10px;"></div>' * 2 +
              '<div style="position:absolute;left:20px;top:40px;width:10px;height:10px;"></div>'
              '<div id="flow" style="display:block;position:relative;'
              'width:10px;height:10px;"></div></div></body></rml>')
    baseline = edit('replace', text=source)
    schema = advertised['forge_ui_source_select']['inputSchema']['properties']['elements']
    assert schema['minItems'] == 0 and schema['maxItems'] == 32
    assert edit('select', elements=[0])['selection'] == [0], 'Source-only selection required Preview'
    shown = read('forge_ui_source_preview', **guard(), width=800, height=600)
    rows = {row['id']: row for row in shown['elements']}
    members = [rows[f'item{i}']['source_index'] for i in range(33)]
    assert not rows['hidden']['visible'] and not rows['ambiguous']['id_unique']
    assert rows['flow']['visible'] and not rows['flow']['editable']
    observation = dict(preview_instance=shown['instance'], preview_revision=shown['revision'])

    def unchanged(value):
        for key in ('text', 'revision', 'state', 'dirty', 'can_undo', 'can_redo'):
            assert value[key] == baseline[key], ('Selection changed document state', key)
        assert read('forge_scene_read') == scene

    def box(left=19, top=39, width=24, height=12, **extra):
        return edit('select_rectangle', **observation, left=left, top=top,
                    width=width, height=height, **extra)

    unchanged(edit('select', elements=[members[2], members[0]]))
    args = arguments(**guard(), **observation, left=19, top=39, width=24, height=12)
    first = client.tool('forge_ui_source_select_rectangle', **args)
    assert first['ok'] and first['result']['selection'] == members[:2], first
    edit('select', elements=[members[3]])
    replay = client.tool('forge_ui_source_select_rectangle', **args)
    assert replay['ok'] and replay['result'] == first['result']
    assert read('forge_ui_source_read')['selection'] == [members[3]], 'Replay applied twice'
    value = box(add=True)
    assert value['selection'] == [members[3], *members[:2]]
    unchanged(value)
    assert box(width=22)['selection'] == members[:1], 'Partly enclosed element selected'
    assert box(width=0)['selection'] == []
    edit('select', elements=[members[3]])
    assert box(width=0, add=True)['selection'] == [members[3]]
    assert box(left=19, width=384)['selection'] == members[:32]
    before = read('forge_ui_source_read')
    for invalid in [dict(left=19, top=39, width=397, height=12),
                    dict(left=19, top=39, width=397, height=12, add=True),
                    dict(left=790, top=0, width=11, height=1),
                    dict(left=-1, top=0, width=1, height=1),
                    dict(left=0, top=0, width=0, height=0, preview_revision=999999)]:
        request = dict(observation) | invalid
        assert not mutate('forge_ui_source_select_rectangle', **guard(), **request)['ok']
        assert read('forge_ui_source_read') == before
    for invalid in [members, [members[0], members[0]], [8191]]:
        assert not mutate('forge_ui_source_select', **guard(), elements=invalid)['ok']
        assert read('forge_ui_source_read') == before
    flow = rows['flow']
    x, y, w, h = flow['bounds']
    selected_flow = box(x, y, w, h)['selection']
    assert selected_flow == [flow['source_index']], ('Flow element not inspectable', flow, selected_flow)
    edit('select', elements=[members[1], members[0]])
    changed = edit('style_group', elements=read('forge_ui_source_read')['selection'],
                   name='opacity', value='0.5')
    assert changed['selection'] == [members[1], members[0]]
    failed(mutate('forge_ui_source_select', **guard(baseline), elements=[]), 'stale_ui_source')
    assert edit('undo')['text'] == baseline['text'], 'Selection added an Undo record'
    assert edit('redo')['text'] == changed['text']
    assert edit('undo')['text'] == baseline['text']
    current = read('forge_ui_source_preview', **guard(), width=800, height=600)
    read('forge_ui_source_preview', **guard(), width=700, height=600)
    failed(mutate('forge_ui_source_select_rectangle', **guard(),
                  preview_instance=current['instance'], preview_revision=current['revision'],
                  left=0, top=0, width=1, height=1), 'stale_ui_preview')
    assert (root / 'Game' / path).read_bytes() == disk
    assert (root / 'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == scene
