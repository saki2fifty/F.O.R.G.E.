"""Named Scene UI values use shared property, Timeline and actual Play owners."""
from mcp_test_client import failed


def check_ui_sequence_play(read, ok, control, mutate, until, discovery, advertised):
    assert 'UI Values' in advertised['forge_play_tune']['description']
    ok('forge_document_open', path='Scenes/151-sequence-ui.scene.json', discard=True)
    original = read('forge_scene_read')
    actor = next(row['id'] for row in original['entities'] if 'forge.ui_values' in row['components'])
    root = next(row['id'] for row in original['entities'] if row['name'] == 'Root - four-second loop')
    schema = next(row for row in discovery()['result']['schema']['components'] if row['id'] == 'forge.ui_values')
    field = schema['fields'][0]
    assert field['element_key'] == 'name' and field['maximum_count'] == 16
    assert schema['admission']['format'] == 'forge.builtin-component'

    def values():
        row = next(row for row in read('forge_scene_read')['entities'] if row['id'] == actor)
        return row['components']['forge.ui_values']['values']

    baseline = values()
    changed = [dict(row) for row in baseline]
    changed[0]['value'] = .4
    assert ok('forge_property_set', arguments=dict(entity=actor, component='forge.ui_values', field='values', value=changed))
    assert values() == changed
    assert ok('forge_history_undo')['changed'] and values() == baseline
    assert ok('forge_history_redo')['changed'] and values() == changed
    assert ok('forge_history_undo')['changed'] and values() == baseline
    before = discovery()
    bad = [dict(name='tick', value=1)]
    rejected = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.ui_values', field='values', value=bad))
    assert not rejected['ok'] and discovery()['revision'] == before['revision'] and values() == baseline
    baseline = baseline + [dict(name='manual', value=.5)]
    ok('forge_property_set', arguments=dict(entity=actor, component='forge.ui_values', field='values', value=baseline))
    ok('forge_document_save')
    authored = read('forge_scene_read')
    ok('forge_play_start')
    state = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] or 'Invalid Transform Animator configuration' in row['status'])
    assert state['control_ready'], state
    control('forge_play_pause')
    control('forge_play_watch', entity=root)
    until(lambda: read('forge_play_animation'), lambda row: row.get('value') and row['value'].get('available'))
    tick = read('forge_play_status')['timing']['tick']
    control('forge_play_animation_control', entity=root, operation='seek', position_seconds=1.5)

    def ui():
        return read('forge_play_ui')

    def at_amount(row, amount):
        docs = (row.get('value') or {}).get('documents', [])
        return bool(docs) and abs(docs[0]['model'].get('amount', -1) - amount) < 1e-5

    state = until(ui, lambda row: at_amount(row, .6))
    instance = state['value']['documents'][0]['instance']
    assert state['value']['documents'][0]['model']['width'] == 220
    assert state['timing']['tick'] == tick
    control('forge_play_tune', entity=actor, component='forge.ui_values', property='values',
            value=[dict(name='amount', value=.6), dict(name='width', value=220), dict(name='manual', value=.35)])
    until(lambda: read('forge_play_status'), lambda row: row['tune_ack'] is not None)
    live = until(ui, lambda row: at_amount(row, .6) and row['value']['documents'][0]['model']['manual'] == .35)
    assert live['value']['documents'][0]['instance'] == instance and live['timing']['tick'] == tick
    scope = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'], generation=scope['generation']+1,
                  entity=actor, component='forge.ui_values', property='values', value=baseline), 'stale_play')
    review = control('forge_play_tuning_capture', entity=actor)
    assert len(review['fields']) == 1 and review['fields'][0]['component'] == 'forge.ui_values'
    assert review['fields'][0]['property'] == 'values'
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    kept = values()
    assert [row['name'] for row in kept] == ['amount', 'width', 'manual'], kept
    assert all(abs(row['value'] - value) < 1e-5 for row, value in zip(kept, (.6, 220, .35))), kept
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
