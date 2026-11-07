"""Mixed model/property tracks through existing scoped Scene and Play owners."""
from mcp_test_client import failed


def check_skeletal_sequence_play(read, ok, control, mutate, until, discovery):
    ok('forge_document_open', path='Scenes/62-coordinated-animation.scene.json', discard=True)
    light = next(row['components'] for row in read('forge_scene_read')['entities']
                 if 'forge.light' in row['components'] and 'forge.transform_animator' in row['components'])
    ok('forge_document_open', path='Scenes/36-animation-root-motion.scene.json', discard=True)
    source = read('forge_scene_read')
    actor = next(row for row in source['entities']
                 if row['components'].get('forge.animator', {}).get('root_motion') == 0)
    actor_id = actor['id']
    clip = actor['components']['forge.animator']['clip']
    controller = ok('forge_entity_create', arguments=dict(recipe='animation.sequence', name='Mixed model sequence'))['results'][0]['selected']
    ok('forge_property_set_many', arguments=dict(entity=controller, component='forge.animation_sequence',
                                                values=dict(duration=4, loop=False)))
    binding = dict(scene=source['asset_id'], entity=controller)
    values = dict(sequence=binding, sequence_start_seconds=0, clip=clip, sequence_next_clip=clip,
                  sequence_next_start_seconds=.5, playback_speed=0, play_on_start=False, loop=True)
    ok('forge_property_set_many', arguments=dict(entity=actor_id, component='forge.animator', values=values))
    for component in ('forge.light', 'forge.audio_source', 'forge.transform_animator'):
        ok('forge_component_add', arguments=dict(entity=actor_id, component=component))
        fields = dict(light[component])
        if component == 'forge.transform_animator':
            fields['sequence'] = binding
        ok('forge_property_set_many', arguments=dict(entity=actor_id, component=component, values=fields))
    baseline = read('forge_scene_read')
    ok('forge_property_set_many', arguments=dict(entity=actor_id, component='forge.animator',
                                                values=dict(sequence_next_start_seconds=.75)))
    assert read('forge_scene_read') != baseline
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == baseline
    ok('forge_document_save')
    saved = read('forge_scene_read')

    def status():
        return read('forge_play_animation').get('value') or {}

    def sample():
        return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == actor_id)

    for launch in range(2):
        print('Skeletal MCP: launch', launch, flush=True)
        ok('forge_play_start')
        until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
        control('forge_play_pause')
        paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
        failed(mutate('forge_play_animation_control', launch=paused['launch'], session=paused['session'],
                      generation=paused['generation'], entity=controller, operation='seek', position_seconds=.75), 'stale_watch')
        for seconds in ((0, .75, 1.5, 4) if launch == 0 else (.75,)):
            print('Skeletal MCP: sample', launch, seconds, flush=True)
            control('forge_play_watch', entity=controller)
            until(status, lambda row: row.get('available') and row.get('sequence_controller'))
            control('forge_play_animation_control', entity=controller, operation='seek', position_seconds=seconds)
            until(status, lambda row: row.get('time') == seconds)
            control('forge_play_watch', entity=actor_id)
            observed = until(status, lambda row: row.get('available') and 'players' in row)
            assert observed['component'] == 'forge.transform_animator'
            skeletal = observed['players']['forge.animator']
            properties = observed['players']['forge.transform_animator']
            weight = max(0, min(1, (seconds-.5)*2))
            assert skeletal['sequence'] == binding and skeletal['duration'] == 1.5
            assert abs(skeletal['time']-min(seconds, 1.5)) < 1e-8
            assert abs(skeletal['blend_weight']-weight) < 1e-8
            assert skeletal['sequence_clips']['clip'] == clip
            assert skeletal['sequence_clips']['next']['clip'] == clip
            assert properties['sequence_clips'] == observed['sequence_clips']
            assert properties['sequence_clips']['clip'] == light['forge.transform_animator']['clip']
            assert properties['duration'] == 2 and properties['blend_weight'] == 0
            u = max(0, min(1, (seconds-1)/2))
            until(sample, lambda row: abs(row['components']['forge.light']['intensity']-(.8+2.2*u*u*(3-2*u))) < 1e-5)
            row = sample()
            pose = row['animation_pose']['local'][0]['translation']
            expected_z = 2*(min(seconds, 1)*(1-weight)+max(0, min(1, seconds-.5))*weight)
            assert abs(pose[2]-expected_z) < .003, (launch, seconds, pose, expected_z)
            assert read('forge_play_status')['timing']['tick'] == paused['timing']['tick']
            assert read('forge_scene_read') == saved
        control('forge_play_animation_control', entity=actor_id, operation='seek', position_seconds=.25)
        until(lambda: read('forge_play_animation'), lambda row: bool(row.get('error')))
        assert status()['players']['forge.animator']['time'] == (1.5 if launch == 0 else .75)
        scope = read('forge_play_status')
        failed(mutate('forge_play_animation_control', launch=scope['launch'], session=scope['session'],
                      generation=scope['generation']+1, entity=actor_id, operation='pause'), 'stale_play')
        control('forge_play_stop')
        assert read('forge_scene_read') == saved
        print('Skeletal MCP: stopped', launch, flush=True)
