"""Fog tuning uses the scoped native Play owner and ordinary source review."""
from mcp_test_client import failed


def check_fog_volume_play(read, ok, control, mutate, until, discovery, sdk=False):
    key = 'forge.fog_volume'
    actor = ok('forge_entity_create', arguments=dict(recipe='render.fog_volume', name='Live mist'))['results'][0]['selected']
    camera = ok('forge_entity_create', arguments=dict(recipe='render.camera', name='Game camera'))['results'][0]['selected']
    ok('forge_property_set_many', arguments=dict(entity=camera, component='forge.local_translation',
                                                values=dict(x=0, y=0, z=-6)))
    ok('forge_document_save')
    if sdk:
        ok('forge_project_settings_game_defaults', state=read('forge_project_settings_read')['state'])
        ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])

    def live():
        return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == actor)['components'][key]

    before = live()
    for field, value in (('density', .5), ('shape', 1), ('half_extent_z', 3)):
        control('forge_play_tune', entity=actor, component=key, property=field, value=value)
        ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert ack['tune_ack']['value'] == value, ack
        until(live, lambda row: row[field] == value)
    tuned = live()
    assert tuned != before and read('forge_scene_read') == saved
    assert read('forge_play_status')['timing']['tick'] == paused['timing']['tick']
    scope = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'],
                  generation=scope['generation'] + 1, entity=actor, component=key,
                  property='density', value=.25), 'stale_play')
    for field, value in (('density', -1), ('shape', 2), ('color_r', 65504.001)):
        control('forge_play_tune', entity=actor, component=key, property=field, value=value)
        ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert not ack['tune_ack']['ok'] and live() == tuned, ack
    review = control('forge_play_tuning_capture', entity=actor)
    assert {row['property'] for row in review['fields']} == {'density', 'shape', 'half_extent_z'}
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    kept = read('forge_scene_read')
    assert next(row for row in kept['entities'] if row['id'] == actor)['components'][key] == tuned
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == saved
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == kept
