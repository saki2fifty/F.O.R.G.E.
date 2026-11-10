"""Width-conditioned stylesheet rules through the actual source/MCP owner."""
from mcp_test_client import failed


def check_media_rules(read, ok, mutate, root, advertised, client, arguments, guard, edit, before_scene):
    names = {'forge_ui_source_media_' + action for action in ('read', 'query', 'rule', 'remove')}
    assert names <= advertised.keys()
    assert advertised['forge_ui_source_media_read']['annotations']['readOnlyHint']
    schema = advertised['forge_ui_source_media_query']['inputSchema']['properties']
    assert schema['minimum']['oneOf'][0]['properties']['unit']['enum'] == ['px', 'dp']
    edit('save')
    base = ('/*keep*/body{display:block;width:100%;height:100%;font-family:Lato;}'
            '#target{display:block;position:absolute;left:10px;top:10px;width:80px;height:40px;}')
    path = 'Assets/responsive.rcss'
    ok('forge_source_text_write', path=path, state='missing', text=base)
    ok('forge_source_ui_register', path=path)
    edit('replace', text='<rml><head><link type="text/rcss" href="responsive.rcss"/></head>'
                        '<body><div id="target"></div></body></rml>')
    edit('save')
    ok('forge_source_ui_register', path='Assets/Hud.rml')
    edit('close')
    ok('forge_ui_source_open', path=path)
    baseline = read('forge_ui_source_read')
    disk = (root/'Game'/path).read_bytes()
    catalog = (root/'Game/forge.assets.json').read_bytes()
    minimum, maximum = dict(value=600, unit='px'), dict(value=700, unit='px')
    old = guard()
    request = arguments(**old, minimum=minimum, maximum=maximum)
    first = client.tool('forge_ui_source_media_query', **request)
    replay = client.tool('forge_ui_source_media_query', **request)
    assert first['ok'] and replay['ok'] and first['result'] == replay['result']
    created = read('forge_ui_source_read')
    rule = created['rule_selection']
    assert rule == 2
    failed(mutate('forge_ui_source_media_query', **old, minimum=minimum), 'stale_ui_source')
    assert read('forge_ui_source_media_read', **guard(), rule=rule) == dict(
        minimum=minimum, maximum=maximum, children=[])
    assert read('forge_ui_source_read') == created
    assert edit('media_query', rule=rule, minimum=minimum, maximum=maximum) == created
    assert edit('undo')['text'] == baseline['text'], 'Query replay/no-op needed multiple Undo'
    assert edit('redo')['text'] == created['text']
    child = edit('media_rule', rule=rule, selector='#target', properties={'width': '120px', 'color': '#3355bb'})
    assert child['rule_selection'] == rule and child['media_child_selection'] == 0
    view = read('forge_ui_source_media_read', **guard(), rule=rule)
    assert len(view['children']) == 1
    selector = view['children'][0]['selector']
    assert {p['name']: p['value'].strip() for p in view['children'][0]['properties']} == dict(width='120px', color='#3355bb')
    assert edit('media_rule', rule=rule, child=0, selector=selector,
                properties={'width': '120px', 'color': '#3355bb'}) == child
    for operation, values in [
        ('media_query', dict(rule=rule)),
        ('media_query', dict(rule=rule, minimum=dict(value=800, unit='px'), maximum=maximum)),
        ('media_query', dict(rule=rule, minimum=dict(value=600, unit='%'))),
        ('media_rule', dict(rule=0, selector='.bad')),
        ('media_rule', dict(rule=rule, child=8, selector='.bad')),
        ('media_rule', dict(rule=rule, child=0, selector='.renamed', properties={'color': 'red', 'width': '1px;bad:2'})),
        ('media_remove', dict(rule=rule, child=8))]:
        assert not mutate('forge_ui_source_' + operation, **guard(), **values)['ok']
        assert read('forge_ui_source_read') == child
    assert edit('undo')['text'] == created['text'], 'Composed child creation needed multiple Undo'
    assert edit('redo')['text'] == child['text']
    edit('media_remove', rule=rule, child=0)
    assert not read('forge_ui_source_media_read', **guard(), rule=rule)['children']
    assert edit('undo')['text'] == child['text']
    edit('media_remove', rule=rule)
    assert '@media' not in read('forge_ui_source_read')['text']
    assert edit('undo')['text'] == child['text']
    escaped_selector = r'.escaped\:name '
    assert child['text'].count(selector) == 1
    escaped = edit('replace', text=child['text'].replace(selector, escaped_selector))
    assert read('forge_ui_source_media_read', **guard(), rule=rule)['children'][0]['selector'] == escaped_selector
    edited = edit('media_rule', rule=rule, child=0, selector=escaped_selector,
                  properties={'color': '#112233'})
    assert escaped_selector in edited['text'] and '#112233' in edited['text']
    assert edit('undo')['text'] == escaped['text'], 'Escaped-selector property edit needed multiple Undo'
    assert edit('undo')['text'] == child['text']
    assert (root/'Game'/path).read_bytes() == disk
    assert (root/'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == before_scene
    edit('save')
    assert not read('forge_ui_source_read')['dirty']
    edit('close')
    ok('forge_source_ui_register', path=path)
    ok('forge_ui_source_open', path='Assets/Hud.rml')
    for width in (599, 600, 700, 701):
        shown = read('forge_ui_source_preview', **guard(), width=width, height=320)
        target = next(row for row in shown['elements'] if row['id'] == 'target')
        assert target['bounds'][2] == (120 if 600 <= width <= 700 else 80)
    edit('close')

