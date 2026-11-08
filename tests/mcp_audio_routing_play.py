"""Audio routing uses existing settings, Scene/history and actual Play owners through MCP."""
import json

from mcp_test_client import failed


def check_audio_routing_play(read, ok, control, mutate, until, edit_settings, root, advertised):
    values_schema = advertised['forge_play_audio_mix']['inputSchema']['properties']['values']
    assert values_schema['properties']['bus_volumes']['maxProperties'] == 16
    assert values_schema['properties']['channel_volumes']['minItems'] == 3
    assert 'bus_volumes' not in values_schema['required']
    graph = dict(version=1, buses=[dict(key='rain', label='Rain', volume=.5, output='effects',
        sends=[dict(target='music', gain=.25)]), dict(key='room', label='Room', volume=.75,
        output='rain', sends=[])])
    graph['buses'][0]['effects'] = dict(echo_enabled=True, echo_seconds=.02, echo_feedback=.5)
    graph['buses'][1]['effects'] = dict(reverb_enabled=True, reverb_decay=.4, reverb_wet=.3)
    catalog = json.loads((root / 'Game/forge.assets.json').read_text())['assets']
    impulse = next(row['id'] for row in catalog if row['source'] == 'Assets/SyntheticRoom.wav')
    graph['buses'][1]['effects']['impulse_response'] = dict(clip=impulse, wet=.75)
    assert 'optional effects' in advertised['forge_project_settings_edit']['description']
    assert 'impulse_response' in advertised['forge_project_settings_edit']['description']
    settings = read('forge_project_settings_read')
    edit_settings('forge_project_settings_edit', patch=[dict(op='add', path='/game/audio/routing', value=graph)])
    failed(mutate('forge_project_settings_save', state=settings['state']), 'stale_settings')
    edit_settings('forge_project_settings_save')
    saved_settings = read('forge_project_settings_read')
    invalid = dict(version=1, buses=[dict(key='rain', output='rain')])
    result = mutate('forge_project_settings_edit', state=saved_settings['state'],
                    patch=[dict(op='replace', path='/game/audio/routing', value=invalid)])
    assert not result['ok'] and read('forge_project_settings_read')['state'] == saved_settings['state'], result
    invalid_fx = json.loads(json.dumps(graph))
    invalid_fx['buses'][0]['effects']['echo_feedback'] = .9
    result = mutate('forge_project_settings_edit', state=saved_settings['state'],
                    patch=[dict(op='replace', path='/game/audio/routing', value=invalid_fx)])
    assert not result['ok'] and read('forge_project_settings_read')['state'] == saved_settings['state'], result
    actor = ok('forge_entity_create', arguments=dict(recipe='primitive.0', name='Routed source'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=actor, component='forge.audio_source'))
    clip = next(row['id'] for row in catalog if row['source'] == 'Assets/Signal.wav')
    ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.audio_source',
        values=dict(clip=clip, spatialized=False, loop=True, play_on_start=True, custom_bus='room')))
    before = read('forge_scene_read')
    ok('forge_property_set', arguments=dict(entity=actor, component='forge.audio_source', field='custom_bus', value='rain'))
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
    assert ok('forge_history_redo')['changed']
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
    result = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.audio_source', field='custom_bus', value='Not a key'))
    assert not result['ok'] and read('forge_scene_read') == before, result
    ok('forge_document_save')
    authored = read('forge_scene_read')
    project_bytes = (root / 'Game/forge.project.json').read_bytes()
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    control('forge_play_watch', entity=actor)
    audio = until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('available'))
    assert audio['value']['bus_volumes'] == dict(rain=.5, room=.75)
    assert audio['value']['bus_processing'] and audio['value']['bus_effect_chains'] == 4
    assert audio['value']['bus_echo_buffer_bytes'] > 0 and audio['value']['bus_reverb_buffer_bytes'] > 0
    assert audio['value']['bus_impulse_buffer_bytes'] > 0
    assert not audio['value']['pending_bus_effects'] and not audio['value']['bus_effect_error']
    assert audio['source']['custom_bus'] == 'room'
    tick = read('forge_play_status')['timing']['tick']
    mix = dict(master_volume=.5, channel_volumes=[.25, .5, .75], bus_volumes=dict(rain=.125, room=.25))
    control('forge_play_audio_mix', values=mix)
    until(lambda: read('forge_play_audio'), lambda row: row['value'].get('bus_volumes') == mix['bus_volumes'])
    control('forge_play_audio_mix', values=dict(master_volume=.75, channel_volumes=[1, 1, 1]))
    audio = until(lambda: read('forge_play_audio'), lambda row: row['value'].get('master_volume') == .75)
    assert audio['value']['bus_volumes'] == mix['bus_volumes']
    control('forge_play_audio_mix', values=dict(master_volume=0, channel_volumes=[0, 0, 0], bus_volumes={}))
    invalid = until(lambda: read('forge_play_audio'), lambda row: bool(row['mix_error']))
    assert invalid['value']['master_volume'] == .75 and invalid['value']['bus_volumes'] == mix['bus_volumes']
    control('forge_play_audio_mix', values=mix)
    until(lambda: read('forge_play_audio'), lambda row: not row['mix_error'] and row['value'].get('master_volume') == .5)
    control('forge_play_tune', entity=actor, component='forge.audio_source', property='custom_bus', value='missing')
    rejected = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
    assert not rejected['tune_ack']['ok'], rejected
    assert read('forge_play_audio')['source']['custom_bus'] == 'room'
    control('forge_play_tune', entity=actor, component='forge.audio_source', property='custom_bus', value='rain')
    until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('custom_bus') == 'rain')
    assert read('forge_play_status')['timing']['tick'] == tick
    control('forge_play_stop')
    until(lambda: read('forge_play_status'), lambda row: not row['active'])
    assert read('forge_scene_read') == authored and (root / 'Game/forge.project.json').read_bytes() == project_bytes
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    audio = read('forge_play_audio')
    assert audio['value']['bus_volumes'] == dict(rain=.5, room=.75)
    control('forge_play_stop')
    until(lambda: read('forge_play_status'), lambda row: not row['active'])
