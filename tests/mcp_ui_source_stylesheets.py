"""Guarded document stylesheet links through the actual shared source owner."""
from mcp_test_client import failed


def check_stylesheet_source(read, ok, mutate, root, advertised, client, arguments,
                            guard, edit, before_scene):
    names = {'forge_ui_source_' + action for action in
             ('stylesheet_reference', 'stylesheet_links', 'stylesheet_link',
              'stylesheet_link_remove', 'stylesheet_link_move')}
    assert names <= advertised.keys()
    assert advertised['forge_ui_source_stylesheet_reference']['annotations']['readOnlyHint']
    assert advertised['forge_ui_source_stylesheet_links']['annotations']['readOnlyHint']
    ok('forge_source_folder_create', path='Assets/Styles')
    base_text, overlay_text = 'p{color:red;}', 'p{color:blue;}'
    assets = []
    for name, text in [('base', base_text), ('override', overlay_text)]:
        path = f'Assets/Styles/{name}.rcss'
        ok('forge_source_text_write', path=path, state='missing', text=text)
        assets.append(ok('forge_source_ui_register', path=path)['asset'])
    base, overlay = assets
    baseline = read('forge_ui_source_read')
    disk = (root/'Game/Assets/Hud.rml').read_bytes()
    catalog = (root/'Game/forge.assets.json').read_bytes()
    assert read('forge_ui_source_stylesheet_links', **guard()) == {'links': []}
    reference = read('forge_ui_source_stylesheet_reference', **guard(), asset=base)
    assert reference['reference'] == 'Styles/base.rcss'
    assert reference['source'] == 'Assets/Styles/base.rcss'
    assert reference['details']['format'] == 'rcss'
    assert read('forge_ui_source_read') == baseline
    old = guard()
    request = arguments(**old, asset=base)
    once = client.tool('forge_ui_source_stylesheet_link', **request)
    replay = client.tool('forge_ui_source_stylesheet_link', **request)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    created = read('forge_ui_source_read')
    links = read('forge_ui_source_stylesheet_links', **guard())['links']
    assert links == [dict(element=2, reference='Styles/base.rcss', type='text/rcss')]
    target = client.tool('forge_discover')['result']['target']
    failed(client.tool('forge_ui_source_stylesheet_links', target=target, **old),
           'stale_ui_source')
    for extra in [dict(element=4), dict(asset='00000000-0000-4000-8000-000000000001')]:
        assert not mutate('forge_ui_source_stylesheet_link', **guard(),
                          **(dict(element=2, asset=base) | extra))['ok']
        assert read('forge_ui_source_read') == created
    current_file = root/'Game/Assets/Styles/base.rcss'
    current_file.write_text("@import 'bad.rcss';")
    failed(client.tool('forge_ui_source_stylesheet_reference', target=target,
                       **guard(), asset=base), 'ui_stylesheet_reference')
    assert not mutate('forge_ui_source_stylesheet_link', **guard(), element=2, asset=base)['ok']
    assert read('forge_ui_source_read') == created
    current_file.write_text(base_text)
    assert edit('undo')['text'] == baseline['text'], 'Stylesheet append/replay needed multiple Undo'
    assert edit('redo')['text'] == created['text']
    appended = edit('stylesheet_link', asset=overlay)
    second = read('forge_ui_source_stylesheet_links', **guard())['links'][1]['element']
    reordered = edit('stylesheet_link_move', element=second, direction=-1)
    links = read('forge_ui_source_stylesheet_links', **guard())['links']
    assert [link['reference'] for link in links] == ['Styles/override.rcss', 'Styles/base.rcss']
    assert reordered['selection'] == [links[0]['element']]
    assert edit('undo')['text'] == appended['text']
    assert edit('redo')['text'] == reordered['text']
    unchanged = read('forge_ui_source_read')
    assert edit('stylesheet_link', element=links[0]['element'], asset=overlay) == unchanged
    assert not mutate('forge_ui_source_stylesheet_link_move', **guard(),
                      element=links[0]['element'], direction=-1)['ok']
    assert read('forge_ui_source_read') == unchanged
    removed = edit('stylesheet_link_remove', element=links[0]['element'])
    assert 'Styles/override.rcss' not in removed['text']
    assert edit('undo')['text'] == reordered['text']
    assert (root/'Game/Assets/Hud.rml').read_bytes() == disk
    assert (root/'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == before_scene
    edit('save')
    assert not read('forge_ui_source_read')['dirty']
    assert b'Styles/override.rcss' in (root/'Game/Assets/Hud.rml').read_bytes()
    edit('close')
