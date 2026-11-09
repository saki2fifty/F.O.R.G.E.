"""Native UI source ownership through the actual MCP workflow host."""
from mcp_test_client import failed


def check_ui_source_authoring(read, ok, mutate, root, advertised, client, arguments):
    required = {'forge_ui_source_' + operation for operation in
                ('read', 'open', 'replace', 'attribute', 'text', 'insert', 'remove', 'move',
                 'save', 'undo', 'redo', 'reload', 'close', 'preview', 'layout', 'arrange')}
    assert required <= advertised.keys()
    assert not read('forge_ui_source_read')['open']
    if not (root / 'Game/Assets').exists():
        ok('forge_source_folder_create', path='Assets')
    path = 'Assets/Hud.rml'
    source = '<!-- preserve -->\r\n<rml><body><p id="caption">First</p></body></rml>\r\n'
    ok('forge_source_text_write', path=path, state='missing', text=source)
    before_scene = read('forge_scene_read')
    state = ok('forge_ui_source_open', path=path)
    assert state['open'] and not state['dirty'] and len(state['elements']) == 3
    original = state['text']

    def guard(value=None):
        value = value or read('forge_ui_source_read')
        return {key: value[key] for key in ('path', 'session', 'revision', 'state')}

    def edit(operation, **extra):
        return ok('forge_ui_source_' + operation, **guard(), **extra)

    def index(tag):
        return next(node['index'] for node in read('forge_ui_source_read')['elements']
                    if node['tag'] == tag)

    saved = read('forge_source_text_read', path=path)
    failed(mutate('forge_source_text_write', path=path, state=saved['state'], text=source), 'source_owned')
    opened = state
    state = edit('attribute', element=index('p'), name='class', value='bright & literal')
    assert 'class="bright &amp; literal"' in state['text'] and state['dirty']
    assert not mutate('forge_play_start')['ok']
    assert not read('forge_play_status')['active']
    failed(mutate('forge_ui_source_close', **guard(opened), discard=True), 'stale_ui_source')
    assert edit('undo')['text'] == original
    assert 'class="bright &amp; literal"' in edit('redo')['text']
    state = edit('attribute', element=index('p'), name='class', value=None)
    assert 'class=' not in state['text']
    state = edit('text', element=index('p'), text='New & <literal>')
    assert 'New &amp; &lt;literal&gt;' in state['text']
    state = edit('insert', parent=index('body'), tag='button', text='Play & resume', before=index('p'))
    assert [node['tag'] for node in state['elements']] == ['rml', 'body', 'button', 'p']
    state = edit('move', element=index('button'), direction=1)
    assert [node['tag'] for node in state['elements']] == ['rml', 'body', 'p', 'button']
    state = edit('move', element=index('button'), direction=-1)
    assert [node['tag'] for node in state['elements']] == ['rml', 'body', 'button', 'p']
    state = edit('remove', element=index('button'))
    assert len(state['elements']) == 3 and '<!-- preserve -->' in state['text']
    assert (root / 'Game' / path).read_bytes() == source.encode()
    for operation, extra in (
        ('attribute', dict(element=index('p'), name='bad name', value='x')),
        ('attribute', dict(element=index('p'), name='style', value='font-size:999999px;')),
        ('text', dict(element=index('body'), text='erase children')),
        ('remove', dict(element=index('body'))),
        ('move', dict(element=index('p'), direction=-1)),
        ('insert', dict(parent=index('body'), tag='script', text='code')),
    ):
        before = read('forge_ui_source_read')
        assert not mutate('forge_ui_source_' + operation, **guard(before), **extra)['ok']
        assert read('forge_ui_source_read') == before
    for key, bad in (('session', '00000000-0000-4000-8000-000000000001'),
                     ('revision', 900000), ('state', '0' * 64), ('path', 'Assets/Other.rml')):
        before = read('forge_ui_source_read')
        stale = guard(before) | {key: bad}
        failed(mutate('forge_ui_source_replace', **stale, text=original), 'stale_ui_source')
        assert read('forge_ui_source_read') == before
    for invalid in ('../outside.rml', 'forge.project.json', 'Native/gameplay.cpp'):
        assert not mutate('forge_ui_source_open', path=invalid)['ok']
    failed(mutate('forge_ui_source_close', **guard()), 'unsaved_source')
    state = edit('replace', text='<rml><body>')
    assert state['outline_error'] and not state['elements']
    assert not mutate('forge_ui_source_save', **guard(state))['ok']
    assert read('forge_ui_source_read') == state
    assert (root / 'Game' / path).read_bytes() == source.encode()
    edit('undo')
    state = edit('save')
    assert not state['dirty'] and (root / 'Game' / path).read_bytes().endswith(b'\r\n')
    ui = ok('forge_source_ui_register', path=path)
    assert ui['type'] == 'ui_document'
    # Preview observes the unsaved source, while its registered baseline and
    # catalog remain unchanged. Actual native layout supplies geometry.
    clean = read('forge_ui_source_read')['text']
    disk_before = (root / 'Game' / path).read_bytes()
    catalog_before = (root / 'Game/forge.assets.json').read_bytes()
    layout_source = '''<!-- canvas source -->\n<rml><head><style>
body {display:block;width:100%;height:100%;font-family:Lato;font-size:20px;}
</style></head><body><div id="panel" style="position:absolute;left:20px;top:30px;width:100px;height:60px; color:#ffffff;">Draft</div>
<div id="first" style="position:absolute;left:220px;top:50px;width:80px;height:40px;">First</div>
<div id="second" style="position:absolute;left:400px;top:130px;width:60px;height:40px;">Second</div>
<div id="relative" style="position:relative;width:100px;height:20px;">Flow</div></body></rml>\n'''
    state = edit('replace', text=layout_source)

    def preview():
        return read('forge_ui_source_preview', **guard(), width=800, height=600, density=1)

    def layout_guard(value):
        return dict(preview_instance=value['instance'], preview_revision=value['revision'])

    shown = preview()
    panel = next(element for element in shown['elements'] if element['id'] == 'panel')
    relative = next(element for element in shown['elements'] if element['id'] == 'relative')
    assert not shown['stale'] and shown['source_state'] == state['state']
    assert panel['editable'] and panel['bounds'] == [20, 30, 100, 60]
    assert not relative['editable'] and relative['reason']
    assert read('forge_ui_source_read') == state, 'Preview changed source/history'
    values = dict(left=-12.5, top=45, width=210.25, height=90)
    for bad in (dict(preview_instance='00000000-0000-4000-8000-000000000001'),
                dict(preview_revision=900000)):
        failed(mutate('forge_ui_source_layout', **guard(), element=panel['source_index'],
                      **(layout_guard(shown) | bad), **values), 'stale_ui_preview')
        assert read('forge_ui_source_read') == state
    failed(mutate('forge_ui_source_layout', **guard(), element=relative['source_index'],
                  **layout_guard(shown), **values), 'layout_unsupported')
    replay_args = arguments(**guard(), element=panel['source_index'],
                            **layout_guard(shown), **values)
    first = client.tool('forge_ui_source_layout', **replay_args)
    replay = client.tool('forge_ui_source_layout', **replay_args)
    assert first['ok'] and replay['ok'] and first['result'] == replay['result']
    changed = read('forge_ui_source_read')
    expected = layout_source.replace('left:20px', 'left:-12.5px').replace('top:30px', 'top:45px')
    expected = expected.replace('width:100px;height:60px', 'width:210.25px;height:90px')
    assert changed['text'] == expected
    assert edit('undo')['text'] == layout_source, 'Canvas layout did not use one Undo record'
    assert edit('redo')['text'] == expected
    group_start = read('forge_ui_source_read')['text']
    shown = preview()
    indices = [next(item['source_index'] for item in shown['elements'] if item['id'] == key)
               for key in ('second', 'panel', 'first')]
    group_schema = advertised['forge_ui_source_arrange']['inputSchema']['properties']
    assert group_schema['elements']['minItems'] == 2 and group_schema['elements']['maxItems'] == 32
    assert len(group_schema['action']['enum']) == 9
    failed(mutate('forge_ui_source_arrange', **guard(), elements=indices, action='move',
                  **(layout_guard(shown) | dict(preview_revision=900000))), 'stale_ui_preview')
    failed(mutate('forge_ui_source_arrange', **guard(), elements=[indices[0], indices[0]],
                  action='move', **layout_guard(shown)), 'layout_selection')
    failed(mutate('forge_ui_source_arrange', **guard(),
                  elements=[indices[0], relative['source_index']], action='align_top',
                  **layout_guard(shown)), 'layout_unsupported')
    assert read('forge_ui_source_read')['text'] == group_start
    group_args = arguments(**guard(), elements=indices, action='move', dx=10, dy=15,
                           **layout_guard(shown))
    group_first = client.tool('forge_ui_source_arrange', **group_args)
    group_replay = client.tool('forge_ui_source_arrange', **group_args)
    assert group_first['ok'] and group_replay['ok'] and group_first['result'] == group_replay['result']
    moved = read('forge_ui_source_read')
    group_expected = group_start.replace('left:-12.5px;top:45px', 'left:-2.5px;top:60px')
    group_expected = group_expected.replace('left:220px;top:50px', 'left:230px;top:65px')
    group_expected = group_expected.replace('left:400px;top:130px', 'left:410px;top:145px')
    assert moved['text'] == group_expected and moved['selection'] == indices
    assert edit('undo')['text'] == group_start, 'Group movement did not use one source Undo'
    assert edit('redo')['text'] == group_expected
    edit('undo')
    edit('arrange', elements=indices, action='align_left', **layout_guard(preview()))
    aligned = read('forge_ui_source_read')['text']
    assert aligned == group_start.replace('left:220px', 'left:-12.5px').replace('left:400px', 'left:-12.5px')
    assert edit('undo')['text'] == group_start
    edit('arrange', elements=indices, action='distribute_horizontal', **layout_guard(preview()))
    assert read('forge_ui_source_read')['text'] == group_start.replace('left:220px', 'left:258.875px')
    assert edit('undo')['text'] == group_start
    old_guard = guard()
    state = edit('replace', text='<rml>')
    stale_read = client.tool('forge_ui_source_preview', target=arguments()['target'],
                             **old_guard, width=800, height=600)
    failed(stale_read, 'stale_ui_source')
    stale = preview()
    assert stale['stale'] and stale['error']
    failed(mutate('forge_ui_source_layout', **guard(), element=panel['source_index'],
                  **layout_guard(shown), **values), 'stale_ui_preview')
    edit('undo')
    current = preview()
    assert not current['stale'] and current['revision'] != shown['revision']
    assert (root / 'Game' / path).read_bytes() == disk_before
    assert (root / 'Game/forge.assets.json').read_bytes() == catalog_before
    edit('replace', text=clean)
    state = edit('text', element=index('p'), text='Unsaved')
    external = '<rml><body><p>External</p></body></rml>'
    (root / 'Game' / path).write_text(external, encoding='utf-8')
    failed(mutate('forge_ui_source_save', **guard(state)), 'source_conflict')
    assert (root / 'Game' / path).read_text() == external
    failed(mutate('forge_ui_source_reload', **guard()), 'unsaved_source')
    reloaded = edit('reload', discard=True)
    assert reloaded['session'] != state['session'] and not reloaded['dirty']
    assert 'External' in reloaded['text']
    state = edit('close')
    assert not state['open']
    # Existing disk-style authoring remains available after the native owner closes.
    disk = read('forge_source_text_read', path=path)
    ok('forge_source_text_write', path=path, state=disk['state'], text=source)
    style = 'Assets/Hud.rcss'
    ok('forge_source_text_write', path=style, state='missing', text='body { color: red; }')
    state = ok('forge_ui_source_open', path=style)
    assert not state['elements'] and not state['outline_error']
    state = edit('replace', text='body { color: blue; }')
    edit('save')
    edit('close')
    assert 'blue' in (root / 'Game' / style).read_text()
    assert read('forge_scene_read') == before_scene
