"""Worker configuration and original C++ motion through existing authoring/Play owners."""
from mcp_test_client import failed


def check_runtime_workers_play(read, ok, control, mutate, until, discovery):
    setting = read('forge_project_settings_read')
    edited = ok('forge_project_settings_edit', state=setting['state'],
                patch=[dict(op='replace', path='/ecs_workers', value=4)])
    assert edited['draft']['ecs_workers'] == 4 and edited['dirty']
    failed(mutate('forge_project_settings_save', state=setting['state']), 'stale_settings')
    ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
    actor = ok('forge_entity_create', arguments=dict(recipe='primitive.0', name='Parallel actor'))['results'][0]['selected']
    until(lambda: mutate('forge_component_add', arguments=dict(entity=actor, component='project.parallelmotion')),
          lambda row: row['ok'])
    ok('forge_property_set_many', arguments=dict(entity=actor, component='project.parallelmotion', values=dict(speed=6)))
    camera = ok('forge_entity_create', arguments=dict(recipe='render.camera', name='Game camera'))['results'][0]['selected']
    ok('forge_property_set_many', arguments=dict(entity=camera, component='forge.local_translation', values=dict(x=0, y=0, z=-6)))
    ok('forge_document_save_as', path='Workers.scene.json')
    authored = read('forge_scene_read')

    def position():
        return next(row['components']['forge.local_translation']['x']
                    for row in read('forge_play_snapshot')['entities'] if row['id'] == actor)

    for attempt in range(2):
        ok('forge_play_start')
        until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
        control('forge_play_pause')
        paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
        start = position()
        control('forge_play_profile_control', enabled=True, clear=True)
        control('forge_play_step')
        until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
              row['timing']['tick'] == paused['timing']['tick'] + 1)
        until(position, lambda value: abs(value - start - .1) < 1e-7)
        control('forge_play_profile_refresh')
        profile = until(lambda: read('forge_play_profile')['value'], lambda row: row.get('ecs', {}).get('available'))
        ecs = profile['ecs']
        assert ecs['ecs_workers'] == 4 and not ecs['native_system_timing']
        assert ecs['system_trace_scope'] == 'owner_thread'
        assert any(row.get('name', '').endswith('ParallelMotionUpdate') and row['multi_threaded']
                   for row in ecs['entries'])
        scope = read('forge_play_status')
        failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'],
                      generation=scope['generation'] + 1, entity=actor,
                      component='project.parallelmotion', property='speed', value=-3), 'stale_play')
        control('forge_play_tune', entity=actor, component='project.parallelmotion', property='speed', value=-3)
        until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        before = position()
        for _ in range(3):
            assert position() == before
        control('forge_play_step')
        until(position, lambda value: abs(value - before + .05) < 1e-7)
        assert read('forge_scene_read') == authored
        if attempt:
            control('forge_play_tuning_capture', entity=actor)
        control('forge_play_stop')
        assert read('forge_scene_read') == authored
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    changed = read('forge_scene_read')
    assert next(row for row in changed['entities'] if row['id'] == actor)['components']['project.parallelmotion']['speed'] == -3
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == changed
