"""Wind authoring and transient Play controls through the shared native owners."""
from mcp_test_client import failed


def check_mesh_wind_play(read, ok, control, mutate, until, discovery, advertised, sdk=False):
    assert 'wind value' in advertised['forge_play_tune']['description']
    schema = next(row for row in discovery()['result']['schema']['components']
                  if row['id'] == 'forge.mesh_renderer')
    field = next(row for row in schema['fields'] if row['id'] == 'wind')
    members = {row['id']: row for row in field['fields']}
    assert field['type'] == 'struct' and not field['default']['enabled']
    assert members['amplitude']['maximum'] == 100 and members['amplitude']['unit'] == 'm'
    actor = ok('forge_entity_create', arguments=dict(recipe='primitive.0', name='Wind template'))['results'][0]['selected']
    original = read('forge_scene_read')
    value = dict(field['default'], enabled=True, amplitude=.4, direction_x=3, direction_z=-4,
                 frequency=.75, phase=40)

    def authored():
        return next(row for row in read('forge_scene_read')['entities'] if row['id'] == actor)['components']['forge.mesh_renderer']['wind']

    ok('forge_property_set', arguments=dict(entity=actor, component='forge.mesh_renderer', field='wind', value=value))
    changed = read('forge_scene_read')
    assert authored() == value
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == original
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == changed
    for extra in (dict(amplitude=101), dict(height=0), dict(direction_x=0, direction_z=0),
                  dict(frequency=True), dict(phase=361)):
        state = discovery()
        result = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.mesh_renderer', field='wind', value=dict(value, **extra)))
        assert not result['ok'] and discovery()['revision'] == state['revision']
        assert read('forge_scene_read') == changed
    placed = ok('forge_mesh_scatter', arguments=dict(entity=actor, name='Wind copies',
        center=dict(x=0, y=0, z=0), count=3, radius=3, spacing=.2, seed=7,
        scale_min=1, scale_max=1, yaw_min=0, yaw_max=0))['results'][0]
    rows = {row['id']: row for row in read('forge_scene_read')['entities']}
    assert all(rows[id_]['components']['forge.mesh_renderer']['wind'] == value
               for id_ in placed['created'])
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == changed
    camera = ok('forge_entity_create', arguments=dict(name='Game camera'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=camera, component='forge.camera'))
    ok('forge_property_set_many', arguments=dict(entity=camera, component='forge.local_translation',
                                                values=dict(x=0, y=.5, z=-6)))
    ok('forge_document_save')
    if sdk:
        # The SDK host's storage scope requires the ordinary game application ID.
        ok('forge_project_settings_game_defaults', state=read('forge_project_settings_read')['state'])
        ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])

    def snapshot():
        return read('forge_play_snapshot')

    def live():
        return next(row for row in snapshot()['entities'] if row['id'] == actor)['components']['forge.mesh_renderer']['wind']

    sample = snapshot()['simulation_seconds']
    assert read('forge_play_status')['timing']['presentation_seconds'] == sample
    tuned = dict(value, amplitude=.8, phase=-90)
    control('forge_play_tune', entity=actor, component='forge.mesh_renderer', property='wind', value=tuned)
    ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
    assert ack['tune_ack']['value'] == tuned, ack
    until(live, lambda row: row == tuned)
    assert read('forge_scene_read') == saved and snapshot()['simulation_seconds'] == sample
    scope = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'],
                  generation=scope['generation']+1, entity=actor,
                  component='forge.mesh_renderer', property='wind', value=value), 'stale_play')
    control('forge_play_tune', entity=actor, component='forge.mesh_renderer', property='wind', value=dict(tuned, height=0))
    ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
    assert not ack['tune_ack']['ok'] and live() == tuned
    control('forge_play_step')
    stepped = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and row['timing']['tick'] == paused['timing']['tick']+1)
    assert stepped['timing']['presentation_seconds'] > sample
    review = control('forge_play_tuning_capture', entity=actor)
    assert len(review['fields']) == 1 and review['fields'][0]['property'] == 'wind'
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    assert authored() == tuned
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == saved
    assert ok('forge_history_redo')['changed'] and authored() == tuned
