"""Responsive layout candidates through the guarded shared source owner."""
from mcp_test_client import failed


def check_layout_presets(read, ok, mutate, root, advertised, client, arguments,
                         guard, edit, before_scene):
    tool = 'forge_ui_source_layout_preset'
    schema = advertised[tool]['inputSchema']['properties']
    assert len(schema['preset']['enum']) == 16
    assert {'center', 'bottom_right', 'stretch_all'} <= set(schema['preset']['enum'])
    assert schema['width']['minimum'] == 1
    assert schema['horizontal_offset']['minimum'] == -65536
    edit('save')
    ok('forge_source_ui_register', path='Assets/Hud.rml')
    disk = (root/'Game/Assets/Hud.rml').read_bytes()
    catalog = (root/'Game/forge.assets.json').read_bytes()
    canvas = ('<!-- keep --><rml><head><style>body{display:block;position:relative;'
              'width:100%;height:100%;font-family:Lato;}div{display:block;}</style>'
              '</head><body><div id="panel" class="keep" style="color:red;"></div></body></rml>')
    original = edit('replace', text=canvas)['text']
    index = next(node['index'] for node in read('forge_ui_source_read')['elements']
                 if node['tag'] == 'div')
    old = guard()
    request = arguments(**old, element=index, preset='center', width=120, height=40,
                        horizontal_offset=8, vertical_offset=-6)
    first = client.tool(tool, **request)
    replay = client.tool(tool, **request)
    assert first['ok'] and replay['ok'] and first['result'] == replay['result']
    centered = read('forge_ui_source_read')
    assert centered['selection'] == [index] and 'class="keep"' in centered['text']
    assert 'color:red;' in centered['text'] and '<!-- keep -->' in centered['text']
    failed(mutate(tool, **old, element=index, preset='center',
                  width=120, height=40), 'stale_ui_source')
    assert edit('layout_preset', element=index, preset='center', width=120, height=40,
                horizontal_offset=8, vertical_offset=-6) == centered
    for extra in [dict(width=0), dict(preset='unknown'), dict(element=0),
                  dict(horizontal_offset=65537), dict(height=True)]:
        values = dict(element=index, preset='center', width=120, height=40) | extra
        assert not mutate(tool, **guard(), **values)['ok']
        assert read('forge_ui_source_read') == centered
    assert edit('undo')['text'] == original, 'Preset replay/no-op needed multiple Undo'
    assert edit('redo')['text'] == centered['text']
    for preset in ['center', 'bottom_right', 'stretch_all']:
        edit('layout_preset', element=index, preset=preset, width=120, height=40,
             horizontal_offset=8, vertical_offset=6)
        for width, height in [(800, 600), (480, 360)]:
            shown = read('forge_ui_source_preview', **guard(), width=width, height=height)
            panel = next(row for row in shown['elements'] if row['id'] == 'panel')
            expected = ([width/2-60+8, height/2-20+6, 120, 40] if preset == 'center' else
                        [width-120-8, height-40-6, 120, 40] if preset == 'bottom_right' else
                        [8, 6, width-16, height-12])
            assert panel['bounds'] == expected, (preset, panel, expected)
            assert not panel['editable']
    assert (root/'Game/Assets/Hud.rml').read_bytes() == disk
    assert (root/'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == before_scene
    edit('save')
    saved = read('forge_ui_source_read')
    assert not saved['dirty'] and (root/'Game/Assets/Hud.rml').read_text() == saved['text']
    edit('replace', text=saved['text'].replace('class="keep"', 'class="keep" data-model="bound"'))
    bound = read('forge_ui_source_read')
    assert not mutate(tool, **guard(), element=index, preset='center', width=120, height=40)['ok']
    assert read('forge_ui_source_read') == bound
    edit('close', discard=True)

