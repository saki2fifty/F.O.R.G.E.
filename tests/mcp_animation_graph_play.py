"""Scoped graph inputs through native Play, MCP and ordinary post-Stop history."""
from mcp_test_client import failed


def check_animation_graph_play(read, ok, control, mutate, until, discovery, advertised, sdk=False,
                               module=False):
    assert 'graph_input_1..4' in advertised['forge_play_tune']['description']
    ok('forge_document_open', path='Scenes/180-animation-graphs.scene.json', discard=True)
    source = read('forge_scene_read')
    actor = next(row['id'] for row in source['entities'] if row['name'].startswith('Left -'))
    graph = next(row['components']['forge.animator']['graph'] for row in source['entities'] if row['id'] == actor)
    if sdk:
        ok('forge_project_settings_game_defaults', state=read('forge_project_settings_read')['state'])
        ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    if module:
        for _ in range(2):
            control('forge_play_step')
    control('forge_play_watch', entity=actor)
    def status():
        return read('forge_play_animation').get('value') or {}
    initial = until(status, lambda row: row.get('available') and row.get('graph') == graph)
    assert [row['slot'] for row in initial['graph_inputs']] == [1, 2]
    if module:
        assert initial['graph_inputs'][0]['override'] and initial['graph_inputs'][0]['value'] == .625
        assert not initial['graph_inputs'][1]['override']
    else:
        assert all(not row['override'] for row in initial['graph_inputs'])
    control('forge_play_animation_control', entity=actor, operation='seek', position_seconds=.5)
    until(status, lambda row: row['time'] == .5)
    timing = read('forge_play_status')['timing']

    def tune(property, value, success=True):
        control('forge_play_tune', entity=actor, component='forge.animator', property=property, value=value)
        ack = until(lambda: read('forge_play_status'),
                    lambda row: not row['tune_pending'] and row['tune_ack'] is not None)['tune_ack']
        if success:
            assert 'value' in ack and 'error' not in ack and ack['property'] == property, ack
        else:
            assert ack.get('ok') is False and ack.get('error'), ack
        return ack
    assert tune('graph_override_1', True)['value'] is True
    assert tune('graph_input_1', .25)['value'] == .25
    tuned = until(status, lambda row: row['graph_inputs'][0]['value'] == .25)
    assert tuned['graph_inputs'][0]['override'] and tuned['time'] == .5
    assert read('forge_play_status')['timing']['tick'] == timing['tick']
    assert read('forge_scene_read') == saved
    for property, value in [('graph_input_1', 1000001), ('graph_override_1', 1),
                            ('graph_input_4', .5), ('graph_override_4', True)]:
        tune(property, value, False)
        assert status()['graph_inputs'] == tuned['graph_inputs'] and status()['time'] == .5
    scope = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'],
                  generation=scope['generation'] + 1, entity=actor, component='forge.animator',
                  property='graph_input_1', value=.75), 'stale_play')
    review = control('forge_play_tuning_capture', entity=actor)
    assert {row['property'] for row in review['fields']} == {'graph_override_1', 'graph_input_1'}
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    applied = read('forge_scene_read')
    config = next(row for row in applied['entities'] if row['id'] == actor)['components']['forge.animator']
    assert config['graph_override_1'] and config['graph_input_1'] == .25 and config['graph'] == graph
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == saved
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == applied
