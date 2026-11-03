"""Smooth Camera Shot authoring and scoped Play through the existing MCP owners."""
from mcp_test_client import failed


def check_camera_shot_play(read, ok, control, mutate, until, discovery):
    ok('forge_document_open', path='Scenes/68-sequence-camera-shots.scene.json', discard=True)
    source = read('forge_scene_read')
    by_id = {row['id']: row for row in source['entities']}
    shots = sorted((row for row in source['entities'] if 'forge.camera_shot' in row['components']),
                   key=lambda row: row['components']['forge.camera_shot']['start_seconds'])
    outgoing, incoming = shots[:2]
    first, second = (row['components']['forge.camera_shot'] for row in (outgoing, incoming))
    actor, root = incoming['id'], second['sequence']['entity']
    previous_camera, camera = first['camera']['entity'], second['camera']['entity']
    schema = next(row for row in discovery()['result']['schema']['components'] if row['id'] == 'forge.camera_shot')
    field = next(row for row in schema['fields'] if row['id'] == 'blend_in_seconds')
    assert field['default_if_missing'] == 0 and field['minimum'] == 0
    assert 'match' in field['description']
    # These original cameras deliberately have distinct backgrounds. Author a
    # compatible composition before enabling the incoming view/lens transition.
    first_camera = by_id[previous_camera]['components']['forge.camera']
    for name in ('background_r', 'background_g', 'background_b', 'background_a'):
        ok('forge_property_set', arguments=dict(entity=camera, component='forge.camera', field=name, value=first_camera[name]))

    def value():
        row = next(row for row in read('forge_scene_read')['entities'] if row['id'] == actor)
        return row['components']['forge.camera_shot']['blend_in_seconds']

    before = discovery()
    rejected = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.camera_shot', field='blend_in_seconds', value=3))
    assert not rejected['ok'] and discovery()['revision'] == before['revision'] and value() == 0
    ok('forge_property_set', arguments=dict(entity=actor, component='forge.camera_shot', field='blend_in_seconds', value=.5))
    assert value() == .5
    assert ok('forge_history_undo')['changed'] and value() == 0
    assert ok('forge_history_redo')['changed'] and value() == .5
    ok('forge_document_save')
    authored = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
    control('forge_play_pause')
    control('forge_play_watch', entity=root)
    until(lambda: read('forge_play_animation'), lambda row: row.get('value') and row['value'].get('available'))
    control('forge_play_animation_control', entity=root, operation='seek', position_seconds=second['start_seconds'] - .01)

    def poses():
        return {row['id']: row for row in read('forge_play_snapshot')['entities']}

    baseline = until(poses, lambda rows: rows[previous_camera]['components']['forge.camera']['enabled'])
    start = [baseline[previous_camera]['world_affine'][index] for index in (3, 7, 11)]
    end = [baseline[camera]['world_affine'][index] for index in (3, 7, 11)]
    tick = read('forge_play_status')['timing']['tick']
    control('forge_play_animation_control', entity=root, operation='seek', position_seconds=second['start_seconds'] + .25)

    def at_weight(rows, weight):
        row = rows[camera]
        return row['components']['forge.camera']['enabled'] and all(
            abs(row['world_affine'][index] - (a + (b-a)*weight)) < 1e-4
            for index, a, b in zip((3, 7, 11), start, end))

    until(poses, lambda rows: at_weight(rows, .5))
    assert read('forge_play_status')['timing']['tick'] == tick
    control('forge_play_tune', entity=actor, component='forge.camera_shot', property='blend_in_seconds', value=.75)
    until(lambda: read('forge_play_status'), lambda row: row['tune_ack'] is not None)
    linear = .25 / .75
    until(poses, lambda rows: at_weight(rows, linear*linear*(3-2*linear)))
    scope = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=scope['launch'], session=scope['session'], generation=scope['generation']+1,
                  entity=actor, component='forge.camera_shot', property='blend_in_seconds', value=.5), 'stale_play')
    review = control('forge_play_tuning_capture', entity=actor)
    assert any(row['component'] == 'forge.camera_shot' and row['property'] == 'blend_in_seconds' for row in review['fields'])
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    assert value() == .75
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
