"""Actual native game UI fields through scoped Play providers and MCP receipts."""
import json

from mcp_test_client import failed


def check_ui_fields_play(root, client, read, ok, control, mutate, arguments, until, advertised):
    names = {'forge_play_ui_fields', 'forge_play_ui_focus', 'forge_play_ui_text', 'forge_play_ui_key'}
    assert names <= set(advertised)
    assert not advertised['forge_play_ui_fields']['annotations']['openWorldHint']
    assert advertised['forge_play_ui_text']['annotations']['openWorldHint']
    hud = ok('forge_source_ui_example')
    saved = read('forge_source_text_read', path=hud['path'])
    markup = saved['text'].replace('</head>', '''<style>
#agent-fields {position:absolute;left:20px;top:280px;pointer-events:auto;}
input,textarea {display:block;width:300px;height:40px;font-family:Lato;pointer-events:auto;}
</style></head>''').replace('</body>', '''<div id="agent-fields">
<input id="entry" type="text" value="áb"/>
<input id="limited" type="text" value="ab" maxlength="3"/>
<input id="password" type="password" value="native-secret"/>
<textarea id="area">row</textarea>
<input id="disabled" type="text" value="Locked" disabled="disabled"/>
</div></body>''')
    assert markup != saved['text']
    ok('forge_source_text_write', path=hud['path'], state=saved['state'], text=markup)
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

    assert field(value, 'entry')['value'] == 'áb'
    assert 'native-secret' not in json.dumps(value)
    assert not field(value, 'password')['value_available'] and 'value' not in field(value, 'password')
    assert set(value['keys']) == set(advertised['forge_play_ui_key']['inputSchema']['properties']['key']['enum'])
    assert {'select_left', 'select_right'} <= set(value['keys'])
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
    assert control('forge_play_ui_reload')['reloaded']
    fresh = fields()
    assert fresh['presentation_revision'] != revision and field(fresh, 'entry')['value'] == 'áb'
    failed(mutate('forge_play_ui_focus', **scope(), instance=instance, element='entry',
                  presentation_revision=revision), 'stale_ui')
    control('forge_play_stop')
    assert read('forge_play_ui_fields', instance=instance)['value'] is None
    assert read('forge_scene_read') == authored
    assert (root/'Game'/hud['path']).read_bytes() == source
    assert (root/'Game/forge.project.json').read_bytes() == settings
