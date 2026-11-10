"""Guarded source font declarations through the actual MCP host and shared owners."""
from pathlib import Path
from mcp_test_client import failed


def check_font_source(read, ok, mutate, root, advertised, client, arguments, guard, edit, before_scene):
    names = {'forge_ui_source_' + name for name in
             ('font_reference', 'font_face_read', 'font_face', 'font_face_remove')}
    assert names <= advertised.keys()
    assert advertised['forge_ui_source_font_reference']['annotations']['readOnlyHint']
    assert advertised['forge_ui_source_font_face_read']['annotations']['readOnlyHint']
    edit('close')
    path = 'Assets/Fonts.rcss'
    source = '/* saved */\n.heading{font-family:MenuFace;color:red;}\n'
    ok('forge_source_text_write', path=path, state='missing', text=source)
    font_path = root/'Game/Assets/Menu.ttf'
    font_bytes = (Path(__file__).resolve().parents[1]/'resources/ui/LatoLatin-Regular.ttf').read_bytes()
    font_path.write_bytes(font_bytes)
    font = ok('forge_source_ui_register', path='Assets/Menu.ttf')['asset']
    ok('forge_ui_source_open', path=path)
    baseline = read('forge_ui_source_read')
    catalog = (root/'Game/forge.assets.json').read_bytes()
    reference = read('forge_ui_source_font_reference', **guard(), asset=font)
    assert reference['source'] == 'Assets/Menu.ttf' and reference['face_index'] == 0
    assert reference['selected_face']['index'] == 0 and reference['details']['faces'] == 1
    assert 'alias' not in reference and read('forge_ui_source_read') == baseline
    target = client.tool('forge_discover')['result']['target']
    for asset, index in [(font, 1), ('00000000-0000-4000-8000-000000000001', 0)]:
        failed(client.tool('forge_ui_source_font_reference', target=target, **guard(),
                           asset=asset, face_index=index), 'ui_font_reference')
    assert read('forge_ui_source_read') == baseline
    old = guard()
    request = arguments(**old, asset=font, family='MenuFace', face_index=0)
    once = client.tool('forge_ui_source_font_face', **request)
    replay = client.tool('forge_ui_source_font_face', **request)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    created = read('forge_ui_source_read')
    assert created['rule_selection'] == 1 and created['text'].startswith(source)
    profile = read('forge_ui_source_font_face_read', **guard(), rule=1)
    assert profile == dict(rule=1, values={'font-family': 'MenuFace',
                                         'src': '"Assets/Menu.ttf"', '-rmlui-face-index': '0'})
    failed(client.tool('forge_ui_source_font_reference', target=target, **old, asset=font),
           'stale_ui_source')
    for extra in [dict(family='bad family'), dict(face_index=1), dict(rule=0),
                  dict(asset='00000000-0000-4000-8000-000000000001')]:
        values = dict(rule=1, family='MenuFace', asset=font, face_index=0) | extra
        assert not mutate('forge_ui_source_font_face', **guard(), **values)['ok']
        assert read('forge_ui_source_read') == created
    font_path.write_bytes(b'bad font')
    failed(client.tool('forge_ui_source_font_reference', target=target, **guard(), asset=font),
           'ui_font_reference')
    assert not mutate('forge_ui_source_font_face', **guard(), rule=1, family='MenuFace', asset=font)['ok']
    assert read('forge_ui_source_read') == created
    font_path.write_bytes(font_bytes)
    assert edit('undo')['text'] == baseline['text'], 'Font append/replay needed multiple Undo steps'
    assert edit('redo')['text'] == created['text']
    changed = edit('font_face', rule=1, family='MenuUpdated', asset=font)
    assert changed['rule_selection'] == 1
    assert read('forge_ui_source_font_face_read', **guard(), rule=1)['values']['font-family'] == 'MenuUpdated'
    assert edit('undo')['text'] == created['text']
    assert edit('redo')['text'] == changed['text']
    removed = edit('font_face_remove', rule=1)
    assert '@font-face' not in removed['text'] and '.heading{font-family:MenuFace;color:red;}' in removed['text']
    assert edit('undo')['text'] == changed['text']
    assert (root/'Game'/path).read_text() == source, 'Font source saved implicitly'
    assert (root/'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == before_scene
    edit('save')
    assert not read('forge_ui_source_read')['dirty']
    assert 'MenuUpdated' in (root/'Game'/path).read_text()
    edit('close')
