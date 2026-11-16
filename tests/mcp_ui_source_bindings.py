"""Simple source bindings use the same native draft and existing runtime authority."""
from mcp_test_client import failed


def check_bindings(read, mutate, root, advertised, client, arguments, guard, edit, path):
    disk = (root / 'Game' / path).read_bytes()
    catalog = (root / 'Game/forge.assets.json').read_bytes()
    scene = read('forge_scene_read')
    source = "<rml><body><p id='value'>Literal</p><button id='action'>Pause</button></body></rml>"
    baseline = edit('replace', text=source)
    source = baseline['text']
    assert advertised['forge_ui_source_bindings']['annotations']['readOnlyHint']
    assert not advertised['forge_ui_source_binding']['annotations']['readOnlyHint']
    schema = advertised['forge_ui_source_binding']['inputSchema']['properties']
    assert schema['kind']['enum'] == ['text', 'click_command']
    assert read('forge_ui_source_bindings', **guard(), element=2) == {
        'text': dict(available=True, authored=False, value=None, reason='', prefix='', suffix=''),
        'click_command': dict(available=False, authored=False, value=None,
                              argument=None, reason='Click command controls require a button or input button'),
        'kinds': [dict(id='text', label='Text value'), dict(id='click_command', label='Click command')],
        'builtin_variables': ['tick', 'paused'], 'model_read_only': True,
        'command_arguments': True, 'argument_limit_bytes': 1024, 'text_literal_limit_bytes': 1024,
        'name_limit': 64,
        'text_expression': '{{name}}', 'click_expression': "command('name')",
        'click_value_expression': "command('name','value')"}
    absent = edit('binding', element=2, kind='text', value=None)
    assert absent['text'] == source and absent['revision'] == baseline['revision']
    old_guard = guard()
    request = arguments(**old_guard, element=2, kind='text', value='health')
    once = client.tool('forge_ui_source_binding', **request)
    replay = client.tool('forge_ui_source_binding', **request)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    bound = read('forge_ui_source_read')
    assert bound['text'] == source.replace('Literal', '{{health}}') and bound['selection'] == [2]
    assert read('forge_ui_source_bindings', **guard(), element=2)['text']['value'] == 'health'
    assert edit('binding', element=2, kind='text', value='health')['revision'] == bound['revision']
    failed(mutate('forge_ui_source_binding', **old_guard, element=2, kind='text', value='tick'),
           'stale_ui_source')
    target = client.tool('forge_discover')['result']['target']
    failed(client.tool('forge_ui_source_bindings', target=target, **old_guard, element=2),
           'stale_ui_source')
    for extra in (dict(element=2, kind='text', value='bad name'),
                  dict(element=2, kind='text', value='true'),
                  dict(element=2, kind='text', value='x' * 65),
                  dict(element=2, kind='click_command', value='Pause'),
                  dict(element=2, kind='other', value='tick'),
                  dict(element=8191, kind='text', value='tick')):
        assert not mutate('forge_ui_source_binding', **guard(), **extra)['ok']
        assert read('forge_ui_source_read') == bound, 'Invalid binding partially published source'
    assert edit('undo')['text'] == source, 'Text binding needed more than one Undo'
    assert edit('redo')['text'] == bound['text']
    labeled = edit('binding', element=2, kind='text', value='health',
                   prefix='Health & <', suffix='> points ✓')
    view = read('forge_ui_source_bindings', **guard(), element=2)['text']
    assert view['prefix'] == 'Health & <' and view['suffix'] == '> points ✓'
    assert 'Health &amp; &lt;{{health}}&gt; points ✓' in labeled['text']
    assert edit('binding', element=2, kind='text', value='health',
                prefix='Health & <', suffix='> points ✓')['revision'] == labeled['revision']
    for extra in (dict(element=2, kind='text', value='health', prefix='{{tick}}'),
                  dict(element=2, kind='text', value='health', suffix='}}'),
                  dict(element=2, kind='text', value='health', prefix='ends {'),
                  dict(element=2, kind='text', value='health', prefix='é' * 513),
                  dict(element=2, kind='text', value=None, suffix='Keep'),
                  dict(element=3, kind='click_command', value='Pause', prefix='Keep')):
        assert not mutate('forge_ui_source_binding', **guard(), **extra)['ok']
        assert read('forge_ui_source_read') == labeled
    assert edit('undo')['text'] == bound['text'], 'Label text needed more than one Undo'
    assert edit('redo')['text'] == labeled['text']
    cleared = edit('binding', element=2, kind='text', value=None)
    assert '<p id=\'value\'></p>' in cleared['text']
    assert edit('undo')['text'] == labeled['text']
    command = edit('binding', element=3, kind='click_command', value='Resume')
    assert command['selection'] == [3] and 'data-event-click="command(\'Resume\')"' in command['text']
    assert 'data-model' not in command['text']
    assert read('forge_ui_source_bindings', **guard(), element=3)['click_command']['value'] == 'Resume'
    assert edit('binding', element=3, kind='click_command', value='Resume')['revision'] == command['revision']
    for argument in ('0.5', '', "Élan & 'quoted' \\\\ path (a,b)", 'x' * 1024):
        valued = edit('binding', element=3, kind='click_command', value='TimeScale', argument=argument)
        view = read('forge_ui_source_bindings', **guard(), element=3)['click_command']
        assert view['available'] and view['value'] == 'TimeScale' and view['argument'] == argument
        assert edit('binding', element=3, kind='click_command', value='TimeScale',
                    argument=argument)['revision'] == valued['revision']
        assert edit('undo')['text'] == command['text']
        assert edit('redo')['text'] == valued['text']
        assert edit('binding', element=3, kind='click_command', value='Resume')['text'] == command['text']
    for extra in (dict(element=3, kind='click_command', value='TimeScale', argument='x' * 1025),
                  dict(element=3, kind='click_command', value='TimeScale', argument='é' * 513),
                  dict(element=3, kind='click_command', value='TimeScale', argument='a\0b'),
                  dict(element=3, kind='click_command', value=None, argument='0.5'),
                  dict(element=2, kind='text', value='tick', argument='0.5')):
        before = read('forge_ui_source_read')
        assert not mutate('forge_ui_source_binding', **guard(), **extra)['ok']
        assert read('forge_ui_source_read') == before
    command = read('forge_ui_source_read')
    removed = edit('binding', element=3, kind='click_command', value=None)
    assert 'data-event-click' not in removed['text']
    assert edit('undo')['text'] == command['text']
    assert (root / 'Game' / path).read_bytes() == disk
    edit('save')
    assert (root / 'Game' / path).read_text(encoding='utf-8') == command['text']
    custom = edit('replace', text=source.replace('Literal', 'Score {{health}} / {{tick}}'))
    view = read('forge_ui_source_bindings', **guard(), element=2)['text']
    assert view['authored'] and not view['available'] and view['reason']
    for value in ('tick', None):
        assert not mutate('forge_ui_source_binding', **guard(), element=2,
                          kind='text', value=value)['ok']
        assert read('forge_ui_source_read') == custom
    assert (root / 'Game/forge.assets.json').read_bytes() == catalog
    assert read('forge_scene_read') == scene, 'Source binding authoring executed or changed Scene state'
