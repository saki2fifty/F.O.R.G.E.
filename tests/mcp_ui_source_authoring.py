"""Native UI source ownership through the actual MCP workflow host."""
from mcp_test_client import failed


def check_ui_source_authoring(read, ok, mutate, root, advertised, client, arguments, properties_only=False, typography_only=False, placement_only=False, sizing_only=False):
    required = {'forge_ui_source_' + operation for operation in
                ('read', 'open', 'replace', 'attribute', 'text', 'insert', 'remove', 'move',
                 'save', 'undo', 'redo', 'reload', 'close', 'preview', 'layout', 'arrange',
                 'duplicate', 'remove_group', 'reparent', 'style', 'style_read', 'rule_read',
                 'rule_property', 'rule_selector', 'rule_insert', 'rule_remove',
                 'style_group_read', 'classes_read', 'style_group', 'classes', 'rule_copy', 'rule_move', 'control', 'control_properties', 'control_values', 'style_profile', 'place_control')}
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

    paragraph = index('p')
    if sizing_only:
        edit('save')
        ok('forge_source_ui_register', path=path)
        catalog = (root/'Game/forge.assets.json').read_bytes()
        disk = (root/'Game'/path).read_bytes()
        canvas = '<rml><head><style>body{display:block;width:100%;height:100%;font-family:Lato;}</style></head><body><div id="a" style="position:absolute;left:20px;top:30px;width:100px;height:60px;padding:8px;border-width:2px;margin:3px;"></div><div id="b" style="position:absolute;left:220px;top:70px;width:80px;height:90px;padding:3px;border-width:1px;margin:5px;box-sizing:border-box;"></div><div id="c" style="position:absolute;left:400px;top:130px;width:140px;height:100px;padding:1px;border-width:1px;"></div><div id="tiny" style="position:absolute;left:630px;top:20px;width:4px;height:4px;"></div><div id="flow" style="position:relative;width:20px;height:20px;"></div></body></rml>'
        baseline = edit('replace', text=canvas)['text']
        def preview():
            return read('forge_ui_source_preview', **guard(), width=800, height=600, density=1)
        def observation(value):
            return dict(preview_instance=value['instance'], preview_revision=value['revision'])
        def rows(shown):
            return {row['id']: row for row in shown['elements']}
        initial = rows(preview())
        indices = [initial[name]['source_index'] for name in ('a', 'b', 'c')]
        assert [initial[name]['bounds'] for name in ('a', 'b', 'c')] == [[23, 33, 120, 80], [225, 75, 80, 90], [400, 130, 144, 104]]
        schema = advertised['forge_ui_source_arrange']['inputSchema']['properties']
        assert len(schema['action']['enum']) == 13 and {'match_width', 'match_height', 'match_size', 'snap_grid'} <= set(schema['action']['enum'])
        assert schema['grid']['minimum'] == 1 and schema['grid']['maximum'] == 1024 and schema['grid']['default'] == 8
        for action in ('match_width', 'match_height', 'match_size'):
            shown = preview()
            old_guard = guard()
            args = arguments(**old_guard, **observation(shown), elements=indices, action=action)
            first = client.tool('forge_ui_source_arrange', **args)
            replay = client.tool('forge_ui_source_arrange', **args)
            assert first['ok'] and replay['ok'] and first['result'] == replay['result']
            changed = read('forge_ui_source_read')
            after = rows(preview())
            assert changed['selection'] == indices
            for name in ('a', 'b', 'c'):
                assert after[name]['bounds'][:2] == initial[name]['bounds'][:2]
                assert after[name]['bounds'][2] == (initial[name]['bounds'][2] if action == 'match_height' else 144)
                assert after[name]['bounds'][3] == (initial[name]['bounds'][3] if action == 'match_width' else 104)
            assert after['a']['pixels']['width'] == (100 if action == 'match_height' else 124)
            assert after['b']['pixels']['width'] == (80 if action == 'match_height' else 144)
            failed(mutate('forge_ui_source_arrange', **old_guard, **observation(shown), elements=indices, action=action), 'stale_ui_source')
            same = edit('arrange', elements=indices, action=action, **observation(preview()))
            assert same['revision'] == changed['revision'] and same['text'] == changed['text'], 'Already matched group created source history'
            assert edit('undo')['text'] == baseline, 'Matching needed more than one Undo'
            assert edit('redo')['text'] == changed['text']
            assert edit('undo')['text'] == baseline
        for members, extra, positions in [
                (indices[:1], {}, {'a': [24, 32]}),
                (indices, dict(grid=5), {'a': [25, 35], 'b': [225, 75], 'c': [400, 130]}),
                (indices, {}, {'a': [24, 32], 'b': [224, 72], 'c': [400, 128]})]:
            args = arguments(**guard(), **observation(preview()), elements=members, action='snap_grid', **extra)
            first = client.tool('forge_ui_source_arrange', **args)
            replay = client.tool('forge_ui_source_arrange', **args)
            assert first['ok'] and replay['ok'] and first['result'] == replay['result']
            changed = read('forge_ui_source_read')
            after = rows(preview())
            for name in ('a', 'b', 'c'):
                assert after[name]['bounds'][:2] == positions.get(name, initial[name]['bounds'][:2])
                assert after[name]['bounds'][2:] == initial[name]['bounds'][2:]
            assert changed['selection'] == members
            same = edit('arrange', elements=members, action='snap_grid', **observation(preview()), **extra)
            assert same['revision'] == changed['revision'], 'Already aligned grid created history'
            assert edit('undo')['text'] == baseline
            assert edit('redo')['text'] == changed['text']
            assert edit('undo')['text'] == baseline
        shown = preview()
        before = read('forge_ui_source_read')
        for extra in [dict(grid=0), dict(grid=1025), dict(dx=1), dict(elements=[]),
                      dict(action='move', elements=indices[:1]), dict(action='match_size', grid=16)]:
            args = dict(elements=indices, action='snap_grid') | observation(shown) | extra
            assert not mutate('forge_ui_source_arrange', **guard(), **args)['ok']
            assert read('forge_ui_source_read') == before, 'Invalid grid request published source'
        shown = preview()
        before = read('forge_ui_source_read')
        for invalid in [dict(elements=[indices[0], indices[0]]), dict(elements=[indices[0], initial['flow']['source_index']]), dict(dx=1), dict(preview_revision=900000)]:
            args = dict(elements=indices, action='match_size') | observation(shown) | invalid
            assert not mutate('forge_ui_source_arrange', **guard(), **args)['ok']
            assert read('forge_ui_source_read') == before, 'Invalid sizing partially published'
        failed(mutate('forge_ui_source_arrange', **guard(), **observation(shown), elements=[indices[1], initial['tiny']['source_index']], action='match_size'), 'layout_unsupported')
        assert read('forge_ui_source_read') == before, 'Clamped border-box candidate published'
        shown = preview()
        assert not shown['stale'], 'Refused sizing did not recover original preview'
        edit('arrange', elements=list(reversed(indices)), action='match_size', **observation(shown))
        after = rows(preview())
        assert all(after[name]['bounds'][2:] == [120, 80] for name in ('a', 'b', 'c'))
        assert read('forge_ui_source_read')['selection'] == list(reversed(indices))
        assert (root/'Game'/path).read_bytes() == disk and (root/'Game/forge.assets.json').read_bytes() == catalog
        edit('save')
        assert not read('forge_ui_source_read')['dirty'] and read('forge_scene_read') == before_scene
        return
    if placement_only:
        edit('save')
        ok('forge_source_ui_register', path=path)
        catalog = (root/'Game/forge.assets.json').read_bytes()
        disk = (root/'Game'/path).read_bytes()
        canvas = '<rml><head><style>body{display:block;width:100%;height:100%;font-family:Lato;}div{margin:11px;}button{min-width:100px;}</style></head><body><div id="panel" style="position:absolute;left:20px;top:30px;width:200px;height:120px;padding:8px;border-width:2px;margin:3px;"></div><div id="flow" style="position:relative;width:100px;height:20px;"></div></body></rml>'
        baseline = edit('replace', text=canvas)
        def preview():
            return read('forge_ui_source_preview', **guard(), width=800, height=600, density=1)
        def observation(value):
            return dict(preview_instance=value['instance'], preview_revision=value['revision'])
        shown = preview()
        panel = next(row for row in shown['elements'] if row['id'] == 'panel')
        flow = next(row for row in shown['elements'] if row['id'] == 'flow')
        assert panel['editable'] and panel['padding_bounds'] == [25, 35, 216, 136]
        allowed = advertised['forge_ui_source_place_control']['inputSchema']['properties']['kind']['enum']
        assert len(allowed) == 16 and 'line-break' not in allowed and 'option' not in allowed
        values = dict(parent=panel['source_index'], kind='container', id='placed', x=40, y=50, width=40, height=20)
        for bad in [dict(x=0), dict(x=800), dict(width=0), dict(kind='line-break'), dict(id='panel')]:
            assert not mutate('forge_ui_source_place_control', **guard(), **observation(shown), **(values | bad))['ok']
            assert read('forge_ui_source_read') == baseline, 'Invalid placement partially published'
        failed(mutate('forge_ui_source_place_control', **guard(), **observation(shown),
                      **(values | dict(parent=flow['source_index']))), 'layout_unsupported')
        args = arguments(**guard(), **observation(shown), **values)
        first = client.tool('forge_ui_source_place_control', **args)
        replay = client.tool('forge_ui_source_place_control', **args)
        assert first['ok'] and replay['ok'] and first['result'] == replay['result']
        placed = read('forge_ui_source_read')
        after = preview()
        child = next(row for row in after['elements'] if row['id'] == 'placed')
        assert child['editable'] and child['bounds'] == [40, 50, 40, 20]
        assert child['pixels'] == dict(left=15, top=15, width=40, height=20)
        assert placed['selection'] == [child['source_index']] and 'margin:0px' in placed['text']
        failed(mutate('forge_ui_source_place_control', **guard(), **observation(shown),
                      **(values | dict(id='stale'))), 'stale_ui_preview')
        assert edit('undo')['text'] == baseline['text']
        assert edit('redo')['text'] == placed['text']
        current = preview()
        failed(mutate('forge_ui_source_place_control', **guard(), **observation(current),
                      **(values | dict(kind='button', id='clamped'))), 'layout_unsupported')
        assert read('forge_ui_source_read')['text'] == placed['text']
        assert not preview()['stale'], 'Native candidate refusal prevented original draft recovery'
        assert (root/'Game'/path).read_bytes() == disk and (root/'Game/forge.assets.json').read_bytes() == catalog
        edit('save')
        assert not read('forge_ui_source_read')['dirty'] and read('forge_scene_read') == before_scene
        return
    if typography_only:
        before = read('forge_ui_source_read')
        profile = read('forge_ui_source_style_profile', **guard(), name='font-weight')
        assert profile['known'] and profile['kind'] == 'number' and profile['keywords'] == ['normal', 'bold']
        assert profile['numeric'] == dict(units=[''], minimum=1, maximum=1000, integer=True, initial_literal='400')
        assert len(profile['common_properties']) == len(set(profile['common_properties'])) == 53
        assert read('forge_ui_source_read') == before, 'Literal profile query changed the draft'
        for name, keywords in [('display', ['none', 'block', 'inline', 'inline-block', 'flow-root', 'flex', 'inline-flex']),
                               ('flex-direction', ['row', 'row-reverse', 'column', 'column-reverse']),
                               ('flex-wrap', ['nowrap', 'wrap', 'wrap-reverse'])]:
            responsive = read('forge_ui_source_style_profile', **guard(), name=name)
            assert responsive['known'] and responsive['keywords'] == keywords and responsive['numeric'] is None
        for name in ('flex-grow', 'flex-shrink', 'flex-basis', 'row-gap', 'column-gap', 'min-width', 'max-width', 'min-height', 'max-height'):
            responsive = read('forge_ui_source_style_profile', **guard(), name=name)
            assert responsive['known'] and responsive['numeric']['minimum'] == 0
            assert responsive['numeric']['units'] == ([''] if name in ('flex-grow', 'flex-shrink') else ['px', 'dp', '%'])
        assert read('forge_ui_source_read') == before, 'Responsive metadata changed source'
        assert read('forge_ui_source_style_profile', **guard(), name='border-color')['kind'] == 'color'
        for name in ('border-width', 'border-radius'):
            profile = read('forge_ui_source_style_profile', **guard(), name=name)
            assert profile['numeric']['units'] == ['px', 'dp'] and profile['numeric']['minimum'] == 0
        for side in ('top', 'right', 'bottom', 'left'):
            padding = read('forge_ui_source_style_profile', **guard(), name='padding-' + side)
            margin = read('forge_ui_source_style_profile', **guard(), name='margin-' + side)
            assert padding['numeric']['minimum'] == 0 and padding['numeric']['units'] == ['px', 'dp', '%']
            assert margin['numeric']['minimum'] == -65536 and margin['keywords'] == ['auto']
        assert read('forge_ui_source_read') == before, 'Box metadata changed source'
        unknown = read('forge_ui_source_style_profile', **guard(), name='custom-property')
        assert not unknown['known'] and unknown['numeric'] is None
        old = guard()
        args = arguments(**old, element=paragraph, name='font-weight', value='bold')
        once = client.tool('forge_ui_source_style', **args)
        replay = client.tool('forge_ui_source_style', **args)
        assert once['ok'] and replay['ok'] and once['result'] == replay['result']
        bold = read('forge_ui_source_read')
        assert read('forge_ui_source_style_read', **guard(), element=paragraph, name='font-weight') == dict(name='font-weight', value='bold')
        current = client.tool('forge_discover')['result']['target']
        failed(client.tool('forge_ui_source_style_profile', target=current, **old, name='font-style'), 'stale_ui_source')
        assert edit('undo')['text'] == original
        assert edit('redo')['text'] == bold['text']
        child = edit('control', parent=index('body'), kind='paragraph', text='Second')
        members = [paragraph, child['selection'][0]]
        group = edit('style_group', elements=members, name='font-style', value='italic')
        assert read('forge_ui_source_style_group_read', **guard(), elements=members, name='font-style')['value'] == 'italic'
        assert edit('undo')['text'] == child['text']
        assert edit('redo')['text'] == group['text']
        edit('save')
        assert not read('forge_ui_source_read')['dirty'] and read('forge_scene_read') == before_scene
        edit('close')
        sheet_path = 'Assets/Typography.rcss'
        sheet_source = '/* keep */\r\n.caption { color:red; }\r\n'
        ok('forge_source_text_write', path=sheet_path, state='missing', text=sheet_source)
        ok('forge_ui_source_open', path=sheet_path)
        sheet_before = read('forge_ui_source_read')
        for name, value in [('text-align', 'center'), ('line-height', '120%'), ('letter-spacing', '0.5dp'),
                            ('text-decoration', 'underline'), ('white-space', 'pre-wrap'), ('text-transform', 'uppercase'),
                            ('display', 'flex'), ('flex-direction', 'column'), ('flex-wrap', 'wrap'),
                            ('justify-content', 'space-between'), ('align-content', 'center'), ('align-items', 'stretch'),
                            ('align-self', 'auto'), ('flex-grow', '1.5'), ('flex-shrink', '0'),
                            ('flex-basis', '50%'), ('row-gap', '4dp'), ('column-gap', '8px'),
                            ('min-width', '20px'), ('max-width', '80%'), ('min-height', '10dp'), ('max-height', 'none'),
                            ('position', 'relative'), ('box-sizing', 'border-box'), ('overflow-x', 'hidden'), ('overflow-y', 'auto'),
                            ('padding-top', '1dp'), ('padding-right', '2%'), ('padding-bottom', '3px'), ('padding-left', '4px'),
                            ('margin-top', '-1dp'), ('margin-right', 'auto'), ('margin-bottom', '3%'), ('margin-left', '4px'),
                            ('border-width', '2px'), ('border-radius', '4dp'), ('border-color', '#cc8844')]:
            profile = read('forge_ui_source_style_profile', **guard(), name=name)
            assert profile['known']
            edit('rule_property', rule=0, name=name, value=value)
            assert read('forge_ui_source_rule_read', **guard(), rule=0, name=name) == dict(name=name, value=value)
        patched = read('forge_ui_source_read')
        assert (root/'Game'/sheet_path).read_bytes() == sheet_source.encode('utf-8')
        assert patched['text'].startswith('/* keep */')
        edit('save')
        assert not read('forge_ui_source_read')['dirty'] and read('forge_scene_read') == before_scene
        return
    kinds = advertised['forge_ui_source_control']['inputSchema']['properties']['kind']['enum']
    assert len(kinds) == len(set(kinds)) == 18 and {'textarea', 'label', 'checkbox', 'radio', 'select', 'option', 'progress', 'input-button'} <= set(kinds)
    assert not advertised['forge_ui_source_control_properties']['annotations']['openWorldHint']
    assert not advertised['forge_ui_source_control_values']['annotations']['openWorldHint']
    created_range = edit('control', parent=index('body'), kind='range', text='5')
    slider = created_range['selection'][0]
    props = read('forge_ui_source_control_properties', **guard(), element=slider)
    values = {row['name']: row for row in props['properties']}
    assert props['kind'] == 'range' and len(values) == 8
    assert values['value']['value'] == 5 and values['value']['authored']
    assert values['max']['value'] == 100 and not values['max']['authored']
    assert values['tab-index']['value'] is None and values['tab-index']['available']
    before_properties = guard()
    property_args = arguments(**before_properties, element=slider,
                              values=dict(min=-2, max=8, step=2, orientation='vertical',
                                          disabled=True, **{'tab-index': 'auto'}))
    once = client.tool('forge_ui_source_control_values', **property_args)
    replay = client.tool('forge_ui_source_control_values', **property_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    patched_range = read('forge_ui_source_read')
    assert patched_range['selection'] == [slider]
    props = read('forge_ui_source_control_properties', **guard(), element=slider)
    values = {row['name']: row for row in props['properties']}
    assert values['min']['value'] == -2 and values['step']['value'] == 2
    assert values['disabled']['value'] is True and values['orientation']['value'] == 'vertical'
    assert (root/'Game'/path).read_bytes() == source.encode('utf-8')
    for patch in (dict(min=-10, step=0), dict(value='2'), dict(orientation='diagonal')):
        assert not mutate('forge_ui_source_control_values', **guard(), element=slider, values=patch)['ok']
        assert read('forge_ui_source_read') == patched_range, 'Invalid property patch partially changed the draft'
    failed(mutate('forge_ui_source_control_values', **guard(), element=slider, values=dict(id='other')), 'invalid_arguments')
    current_target = client.tool('forge_discover')['result']['target']
    failed(client.tool('forge_ui_source_control_properties', target=current_target,
                       **before_properties, element=slider), 'stale_ui_source')
    assert edit('undo')['text'] == created_range['text'], 'Property transaction/replay needed more than one Undo'
    assert edit('redo')['text'] == patched_range['text']
    edit('undo')
    assert edit('undo')['text'] == original
    assert read('forge_scene_read') == before_scene
    if properties_only:
        schema = advertised['forge_ui_source_control']['inputSchema']['properties']
        assert schema['id']['maxLength'] == 128
        assert set(schema['pixels']['required']) == {'left', 'top', 'width', 'height'}
        assert not schema['pixels']['additionalProperties']
        for name in ('left', 'top', 'width', 'height'):
            assert schema['pixels']['properties'][name] == advertised['forge_ui_source_layout']['inputSchema']['properties'][name]
        pixels = dict(left=24, top=36, width=160, height=40)
        creation_args = arguments(**guard(), parent=index('body'), kind='button', text='Run & test', id='configured', pixels=pixels)
        once = client.tool('forge_ui_source_control', **creation_args)
        replay = client.tool('forge_ui_source_control', **creation_args)
        assert once['ok'] and replay['ok'] and once['result'] == replay['result']
        configured = read('forge_ui_source_read')
        selected = configured['selection'][0]
        for name, value in [('position', 'absolute'), ('left', '24px'), ('top', '36px'), ('width', '160px'), ('height', '40px')]:
            assert read('forge_ui_source_style_read', **guard(), element=selected, name=name) == dict(name=name, value=value)
        for options in [dict(id='configured'), dict(id='new', pixels=dict(pixels, width=0)),
                        dict(id='new', pixels=dict(pixels, top=65537)), dict(pixels=pixels),
                        dict(id='new', pixels=dict(pixels, extra=1)), dict(id='bad{id}')]:
            assert not mutate('forge_ui_source_control', **guard(), parent=index('body'), kind='button', **options)['ok']
            assert read('forge_ui_source_read') == configured, 'Configured creation partially published'
        assert edit('undo')['text'] == original
        assert edit('redo')['text'] == configured['text']
        assert edit('undo')['text'] == original
        assert read('forge_scene_read') == before_scene
        image = edit('control', parent=index('body'), kind='image')
        selected = image['selection'][0]
        profile = read('forge_ui_source_control_properties', **guard(), element=selected)
        rows = {row['name']: row for row in profile['properties']}
        assert profile['kind'] == 'image' and len(rows) == 5
        assert rows['width']['value'] is None and not rows['width']['available'] and rows['width']['editable']
        assert not rows['width']['authored'] and rows['width']['minimum'] == 0
        assert 0 < rows['width']['minimum_positive'] <= 0.000001
        patch = dict(src='badge.tga', sprite='', rect='0 0 2 1', width=32.5)
        applied = edit('control_values', element=selected, values=patch)
        for invalid in [dict(src='other.tga', height=16, rect='0 0 0 1'), dict(rect='1000000 0 0.000001 1'), dict(width=-1), dict(height=1e-45), dict(height='2')]:
            assert not mutate('forge_ui_source_control_values', **guard(), element=selected, values=invalid)['ok']
            assert read('forge_ui_source_read') == applied, 'Image patch partially published'
        tint = read('forge_ui_source_style_profile', **guard(), name='image-color')
        assert tint['known'] and tint['kind'] == 'color' and tint['numeric'] is None
        assert edit('undo')['text'] == image['text']
        assert edit('redo')['text'] == applied['text']
        edit('undo')
        assert edit('undo')['text'] == original
        for kind in ('text-input', 'password-input', 'textarea', 'label'):
            created = edit('control', parent=index('body'), kind=kind, text='Source & <safe>')
            selected = created['selection'][0]
            props = read('forge_ui_source_control_properties', **guard(), element=selected)
            rows = {row['name']: row for row in props['properties']}
            if kind == 'label':
                patch = {'for': 'player-name'}
                assert set(rows) == {'for'}
            else:
                assert rows['maxlength']['integer'] and rows['maxlength']['minimum'] == -1
                assert rows['maxlength']['maximum'] == 1000000
                patch = dict(placeholder='A & <hint>', maxlength=80)
                if kind == 'textarea':
                    patch.update(rows=5, cols=32, wrap='nowrap')
                else:
                    patch.update(size=16, value='Authored & <value>')
                for invalid in (dict(maxlength=1.000000001), dict(maxlength=-2),
                                {'cols' if kind == 'textarea' else 'size': 0}):
                    assert not mutate('forge_ui_source_control_values', **guard(), element=selected,
                                      values=invalid)['ok']
                    assert read('forge_ui_source_read') == created, 'Invalid text settings partially published'
            applied = edit('control_values', element=selected, values=patch)
            props = read('forge_ui_source_control_properties', **guard(), element=selected)
            rows = {row['name']: row for row in props['properties']}
            assert all(rows[name]['value'] == value for name, value in patch.items())
            if kind == 'textarea':
                assert '>Source &amp; &lt;safe&gt;</textarea>' in applied['text']
            assert (root/'Game'/path).read_bytes() == source.encode('utf-8')
            assert edit('undo')['text'] == created['text']
            assert edit('redo')['text'] == applied['text']
            edit('undo')
            assert edit('undo')['text'] == original
        button = edit('control', parent=index('body'), kind='input-button', text='Go')
        target = button['selection'][0]
        caption = edit('control_values', element=target, values=dict(value='C & <safe>', disabled=True))
        assert 'C &amp; &lt;safe&gt;' in caption['text']
        props = read('forge_ui_source_control_properties', **guard(), element=target)
        values = {row['name']: row for row in props['properties']}
        assert values['value']['value'] == 'C & <safe>' and values['disabled']['value'] is True
        reset = edit('control_values', element=target, values=dict(disabled=False, focus=None))
        props = read('forge_ui_source_control_properties', **guard(), element=target)
        assert next(row for row in props['properties'] if row['name'] == 'disabled')['authored'] is False
        previous = guard()
        saved = edit('save')
        assert not saved['dirty'] and b'C &amp; &lt;safe&gt;' in (root/'Game'/path).read_bytes()
        saved_bytes = (root/'Game'/path).read_bytes()
        assert saved_bytes == reset['text'].replace('\n', '\r\n').encode('utf-8')
        closed = edit('close')
        assert not closed['open'], closed
        reopened = ok('forge_ui_source_open', path=path)
        # Native widget SetText/GetText retains its existing final-newline convention.
        assert reopened['text'] == reset['text'] + '\n' and reopened['session'] != previous['session']
        assert not reopened['dirty']
        edit('save')
        assert (root/'Game'/path).read_bytes() == saved_bytes, 'Reopen/no-op Save rewrote stored source'
        props = read('forge_ui_source_control_properties', **guard(), element=target)
        assert next(row for row in props['properties'] if row['name'] == 'value')['value'] == 'C & <safe>'
        current_target = client.tool('forge_discover')['result']['target']
        failed(client.tool('forge_ui_source_control_properties', target=current_target,
                           **previous, element=target), 'stale_ui_source')
        failed(mutate('forge_ui_source_control_values', **previous, element=target,
                      values=dict(value='stale')), 'stale_ui_source')
        assert read('forge_scene_read') == before_scene
        return
    creation_guard = guard()
    creation_args = arguments(**creation_guard, parent=index('body'), kind='select', text='Choice & <one>')
    once = client.tool('forge_ui_source_control', **creation_args)
    replay = client.tool('forge_ui_source_control', **creation_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    created = read('forge_ui_source_read')
    expected = original.replace('</body>', '<select><option value="Choice &amp; &lt;one&gt;" selected="selected">Choice &amp; &lt;one&gt;</option></select></body>')
    assert created['text'] == expected and created['selection'] == [3]
    failed(mutate('forge_ui_source_control', **creation_guard, parent=1, kind='button'), 'stale_ui_source')
    option = edit('control', parent=3, kind='option', text='Next')
    assert option['selection'] == [5] and '<option value="Next">Next</option>' in option['text']
    assert edit('undo')['text'] == expected, 'New dropdown option needed more than one Undo'
    assert edit('undo')['text'] == original, 'Atomic dropdown creation needed more than one Undo'
    assert edit('redo')['text'] == expected
    edit('undo')
    for kind, text, fragment in (
            ('textarea', 'Row & <two>', '<textarea>Row &amp; &lt;two&gt;</textarea>'),
            ('checkbox', 'yes', '<input type="checkbox" value="yes"/>'),
            ('progress', '', '<progress value="0"></progress>')):
        control = edit('control', parent=index('body'), before=index('p'), kind=kind, text=text)
        assert control['selection'] == [2] and fragment in control['text']
        assert edit('undo')['text'] == original, 'Common control creation needed more than one Undo'
    unchanged = read('forge_ui_source_read')
    for extra in (dict(parent=1, kind='unknown'), dict(parent=9999, kind='button'),
                  dict(parent=1, kind='option'), dict(parent=1, kind='progress', text='bad')):
        assert not mutate('forge_ui_source_control', **guard(), **extra)['ok']
        assert read('forge_ui_source_read') == unchanged, 'Rejected control creation partially changed source'
    copy_args = arguments(**guard(), element=paragraph, prefix='second')
    copied_once = client.tool('forge_ui_source_duplicate', **copy_args)
    copied_replay = client.tool('forge_ui_source_duplicate', **copy_args)
    assert copied_once['ok'] and copied_replay['ok'] and copied_once['result'] == copied_replay['result']
    copied = read('forge_ui_source_read')
    copied_text = original.replace('</p>', '</p><p id="second-caption">First</p>', 1)
    assert copied['text'] == copied_text and len(copied['selection']) == 1
    clone = copied['selection'][0]
    assert clone != paragraph and copied['elements'][clone]['tag'] == 'p'
    assert not mutate('forge_ui_source_duplicate', **guard(), element=paragraph, prefix='second')['ok']
    assert read('forge_ui_source_read') == copied, 'Rejected duplicate partially changed the source'
    assert not mutate('forge_ui_source_remove_group', **guard(), elements=[paragraph, index('body')])['ok']
    assert read('forge_ui_source_read') == copied, 'Protected group removal partially changed the source'
    delete_args = arguments(**guard(), elements=[clone, paragraph])
    deleted_once = client.tool('forge_ui_source_remove_group', **delete_args)
    deleted_replay = client.tool('forge_ui_source_remove_group', **delete_args)
    assert deleted_once['ok'] and deleted_replay['ok'] and deleted_once['result'] == deleted_replay['result']
    assert '<p' not in read('forge_ui_source_read')['text']
    assert edit('undo')['text'] == copied_text, 'Group removal needed more than one Undo'
    assert '<p' not in edit('redo')['text']
    edit('undo')
    assert edit('undo')['text'] == original, 'Duplication needed more than one Undo'
    state = read('forge_ui_source_read')

    hierarchy = ("<rml><body><p id='a'>One</p><button id='b'>Two</button>"
                 "<div id='target'><span id='anchor'>Keep</span></div></body></rml>\n")
    hierarchy_baseline = edit('replace', text=hierarchy)
    edit('reparent', elements=[index('div')], parent=index('body'))
    assert read('forge_ui_source_read') == hierarchy_baseline, 'No-op hierarchy move changed source/history'
    moving = [index('button'), index('p')]
    move_guard = guard()
    move_args = arguments(**move_guard, elements=moving, parent=index('div'), before=index('span'))
    moved_once = client.tool('forge_ui_source_reparent', **move_args)
    moved_replay = client.tool('forge_ui_source_reparent', **move_args)
    assert moved_once['ok'] and moved_replay['ok'] and moved_once['result'] == moved_replay['result']
    moved = read('forge_ui_source_read')
    siblings = "<p id='a'>One</p><button id='b'>Two</button>"
    moved_text = hierarchy_baseline['text'].replace(siblings, '', 1).replace(
        "<span id='anchor'>", siblings + "<span id='anchor'>", 1)
    assert moved['text'] == moved_text and not moved['selection']
    failed(mutate('forge_ui_source_reparent', **move_guard, elements=moving,
                  parent=index('div')), 'stale_ui_source')
    assert not mutate('forge_ui_source_reparent', **guard(), elements=[index('div')], parent=index('p'))['ok']
    assert not mutate('forge_ui_source_reparent', **guard(), elements=[index('body')], parent=index('div'))['ok']
    assert read('forge_ui_source_read') == moved, 'Rejected hierarchy move partially changed source'
    assert edit('undo')['text'] == hierarchy_baseline['text'], 'Hierarchy move needed more than one Undo'
    assert edit('redo')['text'] == moved_text
    edit('undo')
    assert edit('undo')['text'] == original, 'Hierarchy fixture left an extra source history record'
    state = read('forge_ui_source_read')

    property_guard = guard()
    assert read('forge_ui_source_style_read', **property_guard, element=index('p'), name='color') == dict(name='color', value=None)
    property_args = arguments(**property_guard, element=index('p'), name='color', value='#77bdf7')
    property_once = client.tool('forge_ui_source_style', **property_args)
    property_replay = client.tool('forge_ui_source_style', **property_args)
    assert property_once['ok'] and property_replay['ok'] and property_once['result'] == property_replay['result']
    styled = read('forge_ui_source_read')
    styled_text = original.replace('id="caption"', 'id="caption" style="color:#77bdf7;"', 1)
    assert styled['text'] == styled_text and styled['selection'] == [index('p')]
    assert read('forge_ui_source_style_read', **guard(), element=index('p'), name='color') == dict(name='color', value='#77bdf7')
    assert read('forge_ui_source_read') == styled, 'Property inspection changed source/selection/history'
    read_schema = advertised['forge_ui_source_style_read']['inputSchema']['properties']
    stale_property_read = {key: value for key, value in property_args.items() if key in read_schema}
    failed(client.tool('forge_ui_source_style_read', **stale_property_read), 'stale_ui_source')
    failed(mutate('forge_ui_source_style', **property_guard, element=index('p'),
                  name='color', value='red'), 'stale_ui_source')
    edit('style', element=index('p'), name='color', value='#77bdf7')
    assert read('forge_ui_source_read') == styled, 'Exact style no-op changed source/history'
    for name, value in (('color', 'red;opacity:0'), ('font-size', '257px'),
                        ('Color', 'red'), ('color', ''), ('font-family', '"Lato"')):
        assert not mutate('forge_ui_source_style', **guard(), element=index('p'), name=name, value=value)['ok']
        assert read('forge_ui_source_read') == styled, 'Rejected style property partially changed source'
    removed = edit('style', element=index('p'), name='color', value=None)
    assert removed['text'] == styled_text.replace('style="color:#77bdf7;"', 'style=""', 1)
    assert edit('undo')['text'] == styled_text, 'Inline removal needed more than one Undo'
    assert edit('undo')['text'] == original, 'Inline set/no-op needed more than one Undo'
    # Typed native controls stage these same raw CSS values; protocols share the writer.
    for name, value in (('background-color', '#01020304'), ('width', '12.5dp'),
                        ('height', '25%'), ('opacity', '0.5')):
        edit('style', element=index('p'), name=name, value=value)
        assert read('forge_ui_source_style_read', **guard(), element=index('p'), name=name) == dict(name=name, value=value)
        assert edit('undo')['text'] == original, 'Typed inline value needed more than one Undo'
    state = read('forge_ui_source_read')

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

    def source_id(value):
        current = read('forge_ui_source_read')
        return next(node['index'] for node in current['elements'] for attr in node['attributes']
                    if attr['name'] == 'id' and current['text'][attr['value']['begin']:attr['value']['end']] == value)

    members = [source_id('first'), source_id('panel')]
    group_guard = guard()
    before_group = read('forge_ui_source_read')
    mixed = read('forge_ui_source_style_group_read', **group_guard, elements=members, name='color')
    assert mixed == dict(name='color', values=[dict(element=members[0], value=None),
                         dict(element=members[1], value='#ffffff')], mixed=True, value=None)
    assert read('forge_ui_source_read') == before_group
    group_args = arguments(**group_guard, elements=members, name='color', value='#77bdf7')
    once = client.tool('forge_ui_source_style_group', **group_args)
    replay = client.tool('forge_ui_source_style_group', **group_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    styled_group = read('forge_ui_source_read')
    expected_group = layout_source.replace('color:#ffffff', 'color:#77bdf7')
    expected_group = expected_group.replace('height:40px;">First', 'height:40px;color:#77bdf7;">First')
    assert styled_group['text'] == expected_group and styled_group['selection'] == members
    uniform = read('forge_ui_source_style_group_read', **guard(), elements=members, name='color')
    assert not uniform['mixed'] and uniform['value'] == '#77bdf7'
    assert [row['value'] for row in uniform['values']] == ['#77bdf7', '#77bdf7']
    allowed = advertised['forge_ui_source_style_group_read']['inputSchema']['properties']
    stale = {key: value for key, value in group_args.items() if key in allowed}
    failed(client.tool('forge_ui_source_style_group_read', **stale), 'stale_ui_source')
    edit('style_group', elements=members, name='color', value='#77bdf7')
    assert read('forge_ui_source_read') == styled_group, 'Group property no-op changed history'
    for name, value in (('background-color', '#aabbcc80'), ('width', '27.5dp')):
        edit('style_group', elements=members, name=name, value=value)
        typed = read('forge_ui_source_style_group_read', **guard(), elements=members, name=name)
        assert not typed['mixed'] and typed['value'] == value
        assert [row['value'] for row in typed['values']] == [value, value]
        assert edit('undo')['text'] == expected_group, 'Typed group value needed more than one Undo'
    styled_group = read('forge_ui_source_read')
    for selection, name, value in (([members[0], index('body')], 'color', 'red'),
                                    ([members[0], members[0]], 'color', 'red'),
                                    (members, 'font-size', '257px'),
                                    (members, 'font-family', '"Lato"')):
        assert not mutate('forge_ui_source_style_group', **guard(), elements=selection, name=name, value=value)['ok']
        assert read('forge_ui_source_read') == styled_group, 'Rejected group style partially changed source'
    cleared = edit('style_group', elements=members, name='color', value=None)
    assert cleared['text'] == expected_group.replace('color:#77bdf7;', '')
    assert edit('undo')['text'] == expected_group
    assert edit('undo')['text'] == layout_source, 'Group styles needed more than one Undo'
    empty_classes = read('forge_ui_source_classes_read', **guard(), elements=members)
    assert not empty_classes['mixed'] and empty_classes['value'] is None
    first_class = edit('classes', elements=members[:1], value='first-class')['text']
    assert first_class == layout_source.replace('">First', '" class="first-class">First')
    mixed_classes = read('forge_ui_source_classes_read', **guard(), elements=members)
    assert mixed_classes['mixed'] and [row['value'] for row in mixed_classes['values']] == ['first-class', None]
    class_args = arguments(**guard(), elements=members, value='shared Active')
    once = client.tool('forge_ui_source_classes', **class_args)
    replay = client.tool('forge_ui_source_classes', **class_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    assigned = read('forge_ui_source_read')
    class_expected = first_class.replace('class="first-class"', 'class="shared Active"')
    class_expected = class_expected.replace('">Draft', '" class="shared Active">Draft')
    assert assigned['text'] == class_expected and assigned['selection'] == members
    class_read = read('forge_ui_source_classes_read', **guard(), elements=members)
    assert not class_read['mixed'] and class_read['value'] == 'shared Active'
    assert read('forge_ui_source_read') == assigned, 'Class inspection changed selection/history'
    allowed = advertised['forge_ui_source_classes_read']['inputSchema']['properties']
    stale = {key: value for key, value in class_args.items() if key in allowed}
    failed(client.tool('forge_ui_source_classes_read', **stale), 'stale_ui_source')
    edit('classes', elements=members, value='shared Active')
    assert read('forge_ui_source_read') == assigned, 'Class no-op changed source/history'
    for value in ('bad:name', 'a a', '9class', '{{dynamic}}', ''):
        assert not mutate('forge_ui_source_classes', **guard(), elements=members, value=value)['ok']
        assert read('forge_ui_source_read') == assigned, 'Invalid class list partially changed group'
    assert edit('classes', elements=members, value=None)['text'] == class_expected.replace('class="shared Active"', '')
    assert edit('undo')['text'] == class_expected
    assert edit('undo')['text'] == first_class
    assert edit('undo')['text'] == layout_source, 'Class operations needed more than one Undo'
    state = read('forge_ui_source_read')

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
    assert group_schema['elements']['minItems'] == 1 and group_schema['elements']['maxItems'] == 32
    assert len(group_schema['action']['enum']) == 13
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
    assert not state['elements'] and not state['outline_error'] and len(state['rules']) == 1
    baseline = 'body { color: red; }\n'
    blue = 'body { color: blue; }\n'
    assert state['text'] == baseline, 'Opened stylesheet lost the widget final-newline contract'
    assert state['rules'][0]['selector'] == 'body ' and state['rules'][0]['editable']
    rule_guard = guard()
    assert read('forge_ui_source_rule_read', **rule_guard, rule=0, name='color') == dict(name='color', value='red')
    assert read('forge_ui_source_rule_read', **rule_guard, rule=0, name='absent') == dict(name='absent', value=None)
    assert read('forge_ui_source_read') == state, 'Rule read changed source/history/selection'
    rule_args = arguments(**rule_guard, rule=0, name='color', value='blue')
    once = client.tool('forge_ui_source_rule_property', **rule_args)
    replay = client.tool('forge_ui_source_rule_property', **rule_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    state = read('forge_ui_source_read')
    assert state['text'] == blue and state['rule_selection'] == 0, state
    failed(mutate('forge_ui_source_rule_property', **rule_guard, rule=0, name='color', value='red'), 'stale_ui_source')
    read_properties = advertised['forge_ui_source_rule_read']['inputSchema']['properties']
    stale_read = {key: value for key, value in rule_args.items() if key in read_properties}
    failed(client.tool('forge_ui_source_rule_read', **stale_read), 'stale_ui_source')
    edit('rule_property', rule=0, name='color', value='blue')
    assert read('forge_ui_source_read') == state, 'Exact rule property no-op changed history'
    for name, value in (('background-color', '#12345680'), ('width', '12.5dp'),
                        ('height', '25%'), ('opacity', '0.5')):
        edit('rule_property', rule=0, name=name, value=value)
        assert read('forge_ui_source_rule_read', **guard(), rule=0, name=name) == dict(name=name, value=value)
        assert edit('undo')['text'] == blue, 'Typed rule value needed more than one Undo'
    state = read('forge_ui_source_read')
    for extra in (dict(name='color', value='red;opacity:0'), dict(name='font-size', value='257px'),
                  dict(name='Color', value='red')):
        assert not mutate('forge_ui_source_rule_property', **guard(), rule=0, **extra)['ok']
        assert read('forge_ui_source_read') == state, 'Rejected rule value partially changed source'
    edit('rule_move', rule=0, direction=-1)
    assert read('forge_ui_source_read') == state, 'Boundary rule move changed history'
    copy_args = arguments(**guard(), rule=0)
    once = client.tool('forge_ui_source_rule_copy', **copy_args)
    replay = client.tool('forge_ui_source_rule_copy', **copy_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    copied_rule = read('forge_ui_source_read')
    assert copied_rule['text'] == blue + blue and copied_rule['rule_selection'] == 1
    edit('rule_move', rule=1, direction=-1)
    assert read('forge_ui_source_read') == copied_rule, 'Identical rule move changed source/history'
    assert edit('rule_property', rule=1, name='color', value='red')['text'] == blue + baseline
    moved = edit('rule_move', rule=1, direction=-1)
    assert moved['text'] == baseline + blue and moved['rule_selection'] == 0
    assert edit('undo')['text'] == blue + baseline
    assert edit('undo')['text'] == blue + blue
    assert edit('undo')['text'] == blue, 'Rule reuse operations needed more than one Undo'
    state = read('forge_ui_source_read')
    assert edit('rule_property', rule=0, name='color', value=None)['text'] == 'body {  }\n'
    assert edit('undo')['text'] == state['text']
    assert edit('rule_selector', rule=0, selector='.renamed > p')['text'] == '.renamed > p{ color: blue; }\n'
    assert edit('undo')['text'] == state['text']
    assert edit('rule_insert', selector='.added')['text'] == state['text'] + '.added {}\n'
    assert edit('rule_remove', rule=1)['text'] == state['text'] + '\n'
    edit('undo')
    assert edit('undo')['text'] == state['text'], 'Rule insertion/removal did not share one-step history'
    assert edit('undo')['text'] == baseline
    state = edit('redo')
    assert state['text'] == blue
    unsupported = edit('replace', text='@media (min-width:20px) { .nested {color:red;} }')
    assert len(unsupported['rules']) == 1 and not unsupported['rules'][0]['editable']
    for operation, extra in (('rule_property', dict(name='color', value='red')),
                             ('rule_selector', dict(selector='.bad')), ('rule_remove', {}),
                             ('rule_copy', {}), ('rule_move', dict(direction=-1))):
        assert not mutate('forge_ui_source_' + operation, **guard(), rule=0, **extra)['ok']
        assert read('forge_ui_source_read') == unsupported
    assert edit('undo')['text'] == state['text']
    edit('save')
    edit('close')
    assert 'blue' in (root / 'Game' / style).read_text()
    assert read('forge_scene_read') == before_scene
