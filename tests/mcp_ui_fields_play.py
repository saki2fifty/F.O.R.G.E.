"""Actual native game UI fields through scoped Play providers and MCP receipts."""
import json

from mcp_test_client import failed


def check_ui_fields_play(root, client, read, ok, control, mutate, arguments, until, advertised, forms_only=False):
    names = {'forge_play_ui_fields', 'forge_play_ui_focus', 'forge_play_ui_text', 'forge_play_ui_key',
             'forge_play_ui_controls', 'forge_play_ui_options', 'forge_play_ui_check',
             'forge_play_ui_choose', 'forge_play_ui_select', 'forge_play_ui_buttons', 'forge_play_ui_press'}
    assert names <= set(advertised)
    assert not advertised['forge_play_ui_fields']['annotations']['openWorldHint']
    assert advertised['forge_play_ui_text']['annotations']['openWorldHint']
    assert not advertised['forge_play_ui_controls']['annotations']['openWorldHint']
    assert not advertised['forge_play_ui_options']['annotations']['openWorldHint']
    assert not advertised['forge_play_ui_buttons']['annotations']['openWorldHint']
    assert advertised['forge_play_ui_press']['annotations']['openWorldHint']
    assert all(advertised[name]['annotations']['openWorldHint'] for name in
               ('forge_play_ui_check', 'forge_play_ui_choose', 'forge_play_ui_select'))
    hud = ok('forge_source_ui_example')
    saved = read('forge_source_text_read', path=hud['path'])
    markup = saved['text'].replace('</head>', '''<style>
#agent-fields {position:absolute;left:20px;top:280px;pointer-events:auto;}
input,textarea {display:block;width:300px;height:40px;font-family:Lato;pointer-events:auto;}
#agent-controls {position:absolute;left:400px;top:280px;pointer-events:auto;}
#agent-controls select {display:block;width:300px;height:35px;}
#agent-controls progress {display:block;width:300px;height:20px;}
</style></head>''').replace('</body>', '''<div id="agent-fields">
<input id="entry" type="text" value="áb"/>
<input id="limited" type="text" value="ab" maxlength="3"/>
<input id="password" type="password" value="native-secret"/>
<textarea id="area">row</textarea>
<input id="disabled" type="text" value="Locked" disabled="disabled"/>
</div><div id="agent-controls">
<input id="agent-check" type="checkbox" value="yes"/>
<input id="agent-radio-a" type="radio" name="quality" value="a"/>
<input id="agent-radio-b" type="radio" name="quality" value="b"/>
<select id="agent-choice"><option value="easy" selected="selected">Easy &amp; safe</option><option value="normal">Normal</option><option value="locked" disabled="disabled">Locked</option><option value="hidden" style="visibility:hidden;">Hidden</option></select>
<progress id="agent-progress" value="0.25"></progress>
<input id="agent-locked" type="checkbox" disabled="disabled"/>
<input id="agent-hidden" type="checkbox" style="visibility:hidden;"/>
<input id="agent-ambiguous" type="checkbox"/><p id="agent-ambiguous">Other element</p>
<button id="agent-button" data-event-click="command('Step')">Agent step</button>
<input id="agent-button-locked" type="button" value="Locked" disabled="disabled"/>
<button id="agent-button-hidden" style="visibility:hidden;">Hidden</button>
<button id="agent-button-no-focus" style="focus:none;">No focus</button>
</div></body>''')
    assert markup != saved['text']
    ok('forge_source_text_write', path=hud['path'], state=saved['state'], text=markup)
    ok('forge_source_ui_register', path=hud['path'])
    # The same shared source operation creates native input buttons, not raw test-only markup.
    source_state = ok('forge_ui_source_open', path=hud['path'])

    def source_edit(operation, **extra):
        nonlocal source_state
        guards = {key: source_state[key] for key in ('path', 'session', 'revision', 'state')}
        source_state = ok('forge_ui_source_' + operation, **guards, **extra)
        return source_state

    kinds = advertised['forge_ui_source_control']['inputSchema']['properties']['kind']['enum']
    assert len(kinds) == len(set(kinds)) == 17 and 'input-button' in kinds
    parent = next(row['index'] for row in source_state['elements'] if row['tag'] == 'body')
    created = source_edit('control', parent=parent, kind='input-button', text='Agent & <safe>')
    selected = created['selection'][0]
    assert '<input type="button" value="Agent &amp; &lt;safe&gt;"/>' in created['text']
    source_edit('attribute', element=selected, name='id', value='agent-input-button')
    source_edit('attribute', element=selected, name='data-event-click', value="command('Step')")
    source_edit('save')
    source_edit('close')
    ok('forge_source_ui_register', path=hud['path'])
    ok('forge_project_settings_game_defaults', state=read('forge_project_settings_read')['state'])
    ok('forge_project_settings_edit', state=read('forge_project_settings_read')['state'], patch=[
        dict(op='add', path='/game/ui', value=dict(grapheme_editing=True))])
    ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
    actor = ok('forge_entity_create', arguments=dict(name='Native field HUD'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=actor, component='forge.ui_document'))
    ok('forge_property_set', arguments=dict(entity=actor, component='forge.ui_document',
                                          field='document', value=hud['asset']))
    ok('forge_document_save')
    authored = read('forge_scene_read')
    source = (root/'Game'/hud['path']).read_bytes()
    settings = (root/'Game/forge.project.json').read_bytes()
    ok('forge_play_start')
    control('forge_play_pause')
    live = until(lambda: read('forge_play_ui'), lambda row: bool((row.get('value') or {}).get('documents')))
    instance = live['value']['documents'][0]['instance']

    def fields():
        return read('forge_play_ui_fields', instance=instance)['value']

    value = until(fields, lambda row: row is not None)
    revision = value['presentation_revision']

    def field(row, key):
        return next(item for item in row['fields'] if item['id'] == key)

    def scope():
        row = read('forge_play_status')
        return dict(launch=row['launch'], session=row['session'], generation=row['generation'])

    def action(tool, element='entry', **extra):
        return control(tool, instance=instance, element=element, presentation_revision=revision, **extra)

    if not forms_only:
        assert field(value, 'entry')['value'] == 'áb'
        assert 'native-secret' not in json.dumps(value)
        assert not field(value, 'password')['value_available'] and 'value' not in field(value, 'password')
        assert set(value['keys']) == set(advertised['forge_play_ui_key']['inputSchema']['properties']['key']['enum'])
        assert {'select_left', 'select_right'} <= set(value['keys'])
        assert {'word_left', 'word_right', 'select_word_left', 'select_word_right'} <= set(value['keys'])
        native_keys = {'up', 'down', 'select_up', 'select_down', 'page_up', 'page_down',
                       'select_page_up', 'select_page_down', 'select_home', 'select_end',
                       'document_home', 'document_end', 'select_document_home',
                       'select_document_end', 'word_backspace', 'word_delete'}
        assert native_keys <= set(value['keys']) and len(value['keys']) == 32
        assert value['limits']['text_bytes'] == 1024
        assert advertised['forge_play_ui_text']['inputSchema']['properties']['text']['maxLength'] == value['limits']['text_bytes']
        assert advertised['forge_play_ui_command']['inputSchema']['properties']['value']['maxLength'] == value['limits']['text_bytes']
        stale = scope()
        stale['generation'] += 1
        failed(mutate('forge_play_ui_focus', **stale, instance=instance, element='entry',
                      presentation_revision=revision), 'stale_play')
        failed(mutate('forge_play_ui_focus', **scope(), instance=instance, element='entry',
                      presentation_revision=revision+1), 'stale_ui')
        failed(mutate('forge_play_ui_focus', **scope(), instance=instance, element='disabled',
                      presentation_revision=revision), 'operation_failed')
        failed(mutate('forge_play_ui_text', **scope(), instance=instance, element='entry',
                      presentation_revision=revision, text='unfocused'), 'operation_failed')
        action('forge_play_ui_focus')
        action('forge_play_ui_key', key='select_all')
        assert field(action('forge_play_ui_text', text='x'*1024), 'entry')['value'] == 'x'*1024
        failed(mutate('forge_play_ui_text', **scope(), instance=instance, element='entry',
                      presentation_revision=revision, text='é'*513), 'invalid_arguments')
        assert field(fields(), 'entry')['value'] == 'x'*1024
        action('forge_play_ui_key', key='select_all')
        action('forge_play_ui_text', text='áb')
        action('forge_play_ui_key', key='home')
        selected = field(action('forge_play_ui_key', key='select_right'), 'entry')
        assert selected['selection'] == dict(begin=0, end=2) and selected['value'] == 'áb'
        shift_args = arguments(**scope(), instance=instance, element='entry',
                               presentation_revision=revision, key='select_right')
        shift_first = client.tool('forge_play_ui_key', **shift_args)
        shift_replay = client.tool('forge_play_ui_key', **shift_args)
        assert shift_first['ok'] and shift_replay['ok'] and shift_first['result'] == shift_replay['result']
        assert field(fields(), 'entry')['selection'] == dict(begin=0, end=3)
        assert field(action('forge_play_ui_key', key='select_left'), 'entry')['selection'] == dict(begin=0, end=2)
        assert field(action('forge_play_ui_key', key='select_left'), 'entry')['selection'] == dict(begin=0, end=0)
        action('forge_play_ui_key', key='select_all')
        action('forge_play_ui_text', text='.́a next')
        action('forge_play_ui_key', key='home')
        word = field(action('forge_play_ui_key', key='word_right'), 'entry')
        assert word['selection'] == dict(begin=2, end=2) and word['value'] == '.́a next'
        assert field(action('forge_play_ui_key', key='word_right'), 'entry')['selection'] == dict(begin=4, end=4)
        assert field(action('forge_play_ui_key', key='word_left'), 'entry')['selection'] == dict(begin=0, end=0)
        assert field(action('forge_play_ui_key', key='select_word_right'), 'entry')['selection'] == dict(begin=0, end=2)
        assert field(action('forge_play_ui_key', key='select_word_right'), 'entry')['selection'] == dict(begin=0, end=4)
        assert field(action('forge_play_ui_key', key='select_word_left'), 'entry')['selection'] == dict(begin=0, end=0)
        action('forge_play_ui_key', key='end')
        assert field(action('forge_play_ui_key', key='select_word_left'), 'entry')['selection'] == dict(begin=4, end=8)
        assert field(action('forge_play_ui_key', key='select_word_left'), 'entry')['selection'] == dict(begin=0, end=8)
        word_args = arguments(**scope(), instance=instance, element='entry',
                              presentation_revision=revision, key='select_word_right')
        word_first = client.tool('forge_play_ui_key', **word_args)
        word_replay = client.tool('forge_play_ui_key', **word_args)
        assert word_first['ok'] and word_replay['ok'] and word_first['result'] == word_replay['result']
        assert field(fields(), 'entry')['selection'] == dict(begin=2, end=8)
        action('forge_play_ui_key', key='select_all')
        action('forge_play_ui_text', text='first second third')
        action('forge_play_ui_key', key='document_end')
        delete_args = arguments(**scope(), instance=instance, element='entry',
                                presentation_revision=revision, key='word_backspace')
        deleted = client.tool('forge_play_ui_key', **delete_args)
        repeated = client.tool('forge_play_ui_key', **delete_args)
        assert deleted['ok'] and repeated['ok'] and deleted['result'] == repeated['result']
        assert field(fields(), 'entry')['value'] == 'first second '
        action('forge_play_ui_key', key='document_home')
        assert field(action('forge_play_ui_key', key='word_delete'), 'entry')['value'] == 'second '
        action('forge_play_ui_key', key='document_end')
        assert field(action('forge_play_ui_key', key='select_home'), 'entry')['selection'] == dict(begin=0, end=7)
        action('forge_play_ui_key', key='document_home')
        assert field(action('forge_play_ui_key', key='select_end'), 'entry')['selection'] == dict(begin=0, end=7)
        action('forge_play_ui_key', key='select_all')
        action('forge_play_ui_text', text='áb')
        action('forge_play_ui_key', key='document_home')
        action('forge_play_ui_key', key='select_right')
        assert field(action('forge_play_ui_key', key='word_delete'), 'entry')['value'] == 'b'
        action('forge_play_ui_key', key='select_all')
        action('forge_play_ui_text', text='áb')
        action('forge_play_ui_key', element='entry', key='end')
        replay_args = arguments(**scope(), instance=instance, element='entry',
                                presentation_revision=revision, key='backspace')
        first = client.tool('forge_play_ui_key', **replay_args)
        second = client.tool('forge_play_ui_key', **replay_args)
        assert first['ok'] and second['ok'] and first['result'] == second['result']
        assert field(fields(), 'entry')['value'] == 'á', 'Receipt replay delivered a second Backspace'
        action('forge_play_ui_key', key='backspace')
        assert field(fields(), 'entry')['value'] == ''
        action('forge_play_ui_text', text='👩‍💻x')
        action('forge_play_ui_key', key='home')
        action('forge_play_ui_key', key='right')
        assert field(fields(), 'entry')['selection'] == dict(begin=3, end=3)
        action('forge_play_ui_key', key='backspace')
        assert field(fields(), 'entry')['value'] == 'x'
        action('forge_play_ui_focus', element='limited')
        action('forge_play_ui_key', element='limited', key='end')
        assert field(action('forge_play_ui_text', element='limited', text='cd'), 'limited')['value'] == 'abc'
        failed(mutate('forge_play_ui_text', **scope(), instance=instance, element='entry',
                      presentation_revision=revision, text='wrong field'), 'operation_failed')
        action('forge_play_ui_focus', element='password')
        action('forge_play_ui_key', element='password', key='select_all')
        hidden = action('forge_play_ui_text', element='password', text='replacement-secret')
        assert 'replacement-secret' not in json.dumps(hidden) and 'value' not in field(hidden, 'password')
        assert read('forge_play_status')['session'] == value['session']
        assert read('forge_play_status')['generation'] == value['generation']
        action('forge_play_ui_focus', element='area')
        action('forge_play_ui_key', element='area', key='select_all')
        assert field(action('forge_play_ui_text', element='area', text='first\nsecond'), 'area')['value'] == 'first\nsecond'
        multiline = 'q\náz\nx́w\nr'
        complete_boundaries = {0, 1, 2, 4, 5, 6, 8, 9, 10, 11}
        action('forge_play_ui_key', element='area', key='select_all')
        action('forge_play_ui_text', element='area', text=multiline)
        # All aliases have raw-vs-named native coverage; RPC cases exercise each family.
        action('forge_play_ui_key', element='area', key='document_home')
        action('forge_play_ui_key', element='area', key='right')
        for key in ('down', 'down', 'up', 'select_up', 'select_down', 'page_up',
                    'select_page_down', 'select_home', 'select_end'):
            row = field(action('forge_play_ui_key', element='area', key=key), 'area')
            assert row['value'] == multiline and 0 <= row['selection']['begin'] <= row['selection']['end'] <= len(multiline)
            assert set(row['selection'].values()) <= complete_boundaries, (key, row['selection'])
        action('forge_play_ui_key', element='area', key='document_end')
        assert field(action('forge_play_ui_key', element='area', key='select_document_home'), 'area')['selection'] == dict(begin=0, end=len(multiline))
        action('forge_play_ui_key', element='area', key='document_home')
        assert field(action('forge_play_ui_key', element='area', key='select_document_end'), 'area')['selection'] == dict(begin=0, end=len(multiline))
    def buttons():
        return read('forge_play_ui_buttons', instance=instance)['value']

    def native_button(row, element):
        return next(item for item in row['buttons'] if item['id'] == element)

    observed_buttons = buttons()
    button_revision = observed_buttons['presentation_revision']
    assert native_button(observed_buttons, 'agent-button')['text'] == 'Agent step'
    assert native_button(observed_buttons, 'agent-button')['enabled'] is None
    assert native_button(observed_buttons, 'agent-input-button')['value'] == 'Agent & <safe>'
    assert native_button(observed_buttons, 'agent-input-button')['enabled']
    assert not native_button(observed_buttons, 'agent-button-locked')['pressable']

    def tick():
        return read('forge_play_ui')['value']['documents'][0]['model']['tick']

    before_tick = tick()
    press_args = arguments(**scope(), instance=instance, element='agent-input-button',
                           presentation_revision=button_revision)
    once = client.tool('forge_play_ui_press', **press_args)
    replay = client.tool('forge_play_ui_press', **press_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result'] and once['result']['delivered']
    until(tick, lambda value: value == before_tick + 1)
    assert tick() == before_tick + 1, 'Receipt replay clicked twice'
    for element in ('agent-input-button', 'agent-button'):
        current_buttons = buttons()
        assert control('forge_play_ui_press', instance=instance, element=element,
                       presentation_revision=current_buttons['presentation_revision'])['delivered']
        before_tick += 1
        until(tick, lambda value: value == before_tick + 1)
    for element in ('agent-button-locked', 'agent-button-hidden', 'agent-button-no-focus', 'password'):
        assert not mutate('forge_play_ui_press', **scope(), instance=instance, element=element,
                          presentation_revision=buttons()['presentation_revision'])['ok']
    def forms():
        return read('forge_play_ui_controls', instance=instance)['value']

    def form_field(row, key):
        return next(item for item in row['controls'] if item['id'] == key)

    form_state = until(forms, lambda row: row is not None)
    form_revision = form_state['presentation_revision']
    assert len(form_state['controls']) == 8 and not form_state['truncated']
    assert not form_field(form_state, 'agent-check')['checked']
    assert form_field(form_state, 'agent-progress')['value'] == 0.25
    assert form_field(form_state, 'agent-progress')['read_only']
    assert not form_field(form_state, 'agent-ambiguous')['addressable']
    assert 'native-secret' not in json.dumps(form_state) and 'replacement-secret' not in json.dumps(form_state)

    def form_action(tool, element, **extra):
        return control(tool, instance=instance, element=element,
                       presentation_revision=form_revision, **extra)

    noop = form_action('forge_play_ui_check', 'agent-check', checked=False)
    assert not noop['delivered'] and not noop['changed']
    check_args = arguments(**scope(), instance=instance, element='agent-check',
                           presentation_revision=form_revision, checked=True)
    once = client.tool('forge_play_ui_check', **check_args)
    replay = client.tool('forge_play_ui_check', **check_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    assert once['result']['delivered'] and form_field(forms(), 'agent-check')['checked']
    assert not form_action('forge_play_ui_check', 'agent-check', checked=True)['delivered']
    form_action('forge_play_ui_check', 'agent-check', checked=False)
    form_action('forge_play_ui_choose', 'agent-radio-a')
    chosen = form_action('forge_play_ui_choose', 'agent-radio-b')
    assert not form_field(chosen, 'agent-radio-a')['checked'] and form_field(chosen, 'agent-radio-b')['checked']
    assert not form_action('forge_play_ui_choose', 'agent-radio-b')['delivered']
    page = read('forge_play_ui_options', instance=instance, element='agent-choice', offset=0, limit=2)['value']
    tail = read('forge_play_ui_options', instance=instance, element='agent-choice', offset=2, limit=2)['value']
    assert page['ready'] and page['total'] == 4 and page['next_offset'] == 2
    assert page['options'][0]['text'] == 'Easy & safe' and page['options'][0]['value'] == 'easy'
    assert tail['next_offset'] is None and not tail['options'][0]['enabled'] and not tail['options'][1]['visible']
    assert not form_action('forge_play_ui_select', 'agent-choice', index=0, option_state=page['options'][0]['state'])['delivered']
    select_args = arguments(**scope(), instance=instance, element='agent-choice',
                            presentation_revision=form_revision, index=1, option_state=page['options'][1]['state'])
    once = client.tool('forge_play_ui_select', **select_args)
    replay = client.tool('forge_play_ui_select', **select_args)
    assert once['ok'] and replay['ok'] and once['result'] == replay['result']
    assert once['result']['delivered'] and form_field(forms(), 'agent-choice')['value'] == 'normal'
    assert not form_action('forge_play_ui_select', 'agent-choice', index=1, option_state=page['options'][1]['state'])['delivered']
    failed(mutate('forge_play_ui_select', **scope(), instance=instance, element='agent-choice',
                  presentation_revision=form_revision, index=0, option_state=page['options'][1]['state']), 'stale_ui_option')
    for option in tail['options']:
        assert not mutate('forge_play_ui_select', **scope(), instance=instance, element='agent-choice',
                          presentation_revision=form_revision, index=option['index'], option_state=option['state'])['ok']
    for element in ('password', 'agent-progress', 'agent-locked', 'agent-hidden', 'agent-ambiguous'):
        assert not mutate('forge_play_ui_check', **scope(), instance=instance, element=element,
                          presentation_revision=form_revision, checked=True)['ok']
    failed(mutate('forge_play_ui_check', **scope(), instance=instance, element='agent-check',
                  presentation_revision=form_revision, checked='true'), 'invalid_arguments')
    current_target = client.tool('forge_discover')['result']['target']
    failed(client.tool('forge_play_ui_options', target=current_target, instance=instance,
                       element='agent-choice', limit=33), 'invalid_arguments')
    assert control('forge_play_ui_reload')['reloaded']
    fresh = fields()
    assert fresh['presentation_revision'] != revision and field(fresh, 'entry')['value'] == 'áb'
    failed(mutate('forge_play_ui_focus', **scope(), instance=instance, element='entry',
                  presentation_revision=revision), 'stale_ui')
    failed(mutate('forge_play_ui_check', **scope(), instance=instance, element='agent-check',
                  presentation_revision=form_revision, checked=True), 'stale_ui')
    failed(mutate('forge_play_ui_press', **scope(), instance=instance, element='agent-button',
                  presentation_revision=button_revision), 'stale_ui')
    assert not form_field(forms(), 'agent-check')['checked'] and form_field(forms(), 'agent-choice')['value'] == 'easy'
    control('forge_play_stop')
    assert read('forge_play_ui_fields', instance=instance)['value'] is None
    assert read('forge_play_ui_controls', instance=instance)['value'] is None
    assert read('forge_play_ui_buttons', instance=instance)['value'] is None
    assert read('forge_play_ui_options', instance=instance, element='agent-choice')['value'] is None
    assert read('forge_scene_read') == authored
    assert (root/'Game'/hud['path']).read_bytes() == source
    assert (root/'Game/forge.project.json').read_bytes() == settings
