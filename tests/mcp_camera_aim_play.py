"""Camera Aim through ordinary Scene authoring, scoped Play and value review."""
from math import sqrt
from mcp_test_client import failed


def check_camera_aim_play(read, ok, control, mutate, until, discovery, client, arguments):
    ok('forge_document_open', path='Scenes/68-sequence-camera-shots.scene.json', discard=True)
    source = read('forge_scene_read')
    rows = {row['id']: row for row in source['entities']}
    target = next(row['id'] for row in source['entities'] if 'forge.transform_animator' in row['components'])
    shots = sorted((row for row in source['entities'] if 'forge.camera_shot' in row['components']),
                   key=lambda row: row['components']['forge.camera_shot']['start_seconds'])
    outgoing, incoming = (row['components']['forge.camera_shot']['camera']['entity'] for row in shots[:2])
    controller = shots[0]['components']['forge.camera_shot']['sequence']['entity']
    start = shots[1]['components']['forge.camera_shot']['start_seconds']
    schema = next(row for row in discovery()['result']['schema']['components'] if row['id'] == 'forge.camera_aim')
    camera_tool = next(row for row in client.request('tools/list')['result']['tools'] if row['name'] == 'forge_play_cameras')
    assert camera_tool['annotations']['readOnlyHint'] is True
    fields = {row['id']: row for row in schema['fields']}
    assert schema['optional'] and set(fields) == {'enabled', 'target', 'offset_x', 'offset_y', 'offset_z', 'weight'}
    assert fields['weight']['minimum'] == 0 and fields['weight']['maximum'] == 1
    for actor in (outgoing, incoming):
        ok('forge_component_add', arguments=dict(entity=actor, component='forge.camera_aim'))
        before = read('forge_scene_read')
        values = dict(enabled=True, target=dict(scene=source['asset_id'], entity=target),
                      offset_x=1, offset_y=0, offset_z=0, weight=1)
        ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.camera_aim', values=values))
        configured = read('forge_scene_read')
        assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
        assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == configured
    before = discovery()['revision']
    refused = mutate('forge_property_set', arguments=dict(entity=outgoing, component='forge.camera_aim', field='weight', value=1.1))
    assert not refused['ok'] and discovery()['revision'] == before
    stale = arguments(arguments=dict(entity=outgoing, component='forge.camera_aim', field='weight', value=.5))
    stale['expected_revision'] -= 1
    refused = client.tool('forge_property_set', **stale)
    assert not refused['ok'] and discovery()['revision'] == before
    composition = rows[outgoing]['components']['forge.camera']
    ok('forge_property_set_many', arguments=dict(entity=incoming, component='forge.camera', values={
        key: composition[key] for key in ('background_r', 'background_g', 'background_b', 'background_a')}))
    ok('forge_property_set', arguments=dict(entity=shots[1]['id'], component='forge.camera_shot', field='blend_in_seconds', value=.5))
    ok('forge_document_save')
    authored = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
    control('forge_play_pause')
    inspection_revision = discovery()['revision']
    inspection_tick = read('forge_play_status')['timing']['tick']
    refused = client.tool('forge_play_cameras', target=discovery()['result']['target'], width=0, height=600)
    assert not refused['ok'] and discovery()['revision'] == inspection_revision
    inspected = read('forge_play_cameras', width=800, height=600, limit=1)
    assert inspected['width'] == 800 and inspected['height'] == 600 and inspected['output'] is None
    assert len(inspected['cameras']) <= 1
    assert read('forge_play_status')['timing']['tick'] == inspection_tick
    empty_output = read('forge_play_cameras', width=800, height=600,
                        output='8f5cb432-1032-450e-89b4-8bbf088dcc95')
    assert empty_output['total'] == 0 and empty_output['cameras'] == []
    state = read('forge_play_status')
    refused = mutate('forge_play_tune', launch=state['launch'], session=state['session'],
                     generation=state['generation'], entity=outgoing, component='forge.camera_aim',
                     property='target', value=dict(scene=source['asset_id'], entity=target))
    if refused['ok']:
        # SDK Play admits reflected requests before the native owner validates them.
        acknowledgement = until(lambda: read('forge_play_status'),
                                lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert not acknowledgement['tune_ack']['ok'], acknowledgement
    assert discovery()['revision'] == inspection_revision
    control('forge_play_watch', entity=controller)
    until(lambda: read('forge_play_animation'), lambda row: row.get('value') and row['value'].get('available'))

    def snapshot():
        return {row['id']: row for row in read('forge_play_snapshot')['entities']}

    def observation():
        return snapshot(), read('forge_play_cameras', width=800, height=600)

    def unit(value):
        length = sqrt(sum(item*item for item in value))
        assert length > 0
        return [item/length for item in value]

    def forward(matrix, settings):
        sign = -1 if settings['basis'] == 1 else 1
        return unit([sign*matrix[index] for index in (2, 6, 10)])

    def at_aim(observed, origin):
        data, cameras = observed
        selected = data[incoming]
        view = next((row for row in cameras['cameras'] if row['entity'] == incoming), None)
        if not selected['components']['forge.camera']['enabled'] or view is None:
            return False
        base = data[origin]
        aim = base['components']['forge.camera_aim']
        target_matrix = data[target]['world_affine']
        offset = [aim[key] for key in ('offset_x', 'offset_y', 'offset_z')]
        focus = [target_matrix[4*r+3] + sum(target_matrix[4*r+c]*offset[c] for c in range(3)) for r in range(3)]
        eye = [base['world_affine'][index] for index in (3, 7, 11)]
        toward = unit([point-place for point, place in zip(focus, eye)])
        native = forward(base['world_affine'], base['components']['forge.camera'])
        if aim['weight'] == 0:
            expected = native
        elif aim['weight'] == .5:
            expected = unit([a+b for a, b in zip(native, toward)])
        else:
            assert aim['weight'] == 1
            expected = toward
        actual = view['forward']
        return (not cameras['diagnostics'] and cameras['total'] == 1
                and all(abs(value-place) < 1e-5 for value, place in zip(view['position'], eye))
                and all(abs(a-b) < 1e-5 for a, b in zip(actual, expected)))

    control('forge_play_animation_control', entity=controller, operation='seek', position_seconds=start)
    at_start, _ = until(observation, lambda value: at_aim(value, outgoing))
    preserved = {actor: at_start[actor]['world_affine'] for actor in (outgoing, incoming)}
    control('forge_play_animation_control', entity=controller, operation='seek', position_seconds=start+.5)
    until(observation, lambda value: at_aim(value, incoming))
    control('forge_play_animation_control', entity=controller, operation='seek', position_seconds=start)
    until(observation, lambda value: at_aim(value, outgoing))
    for weight in (0, .5):
        control('forge_play_tune', entity=outgoing, component='forge.camera_aim', property='weight', value=weight)
        until(observation, lambda value: value[0][outgoing]['components']['forge.camera_aim']['weight'] == weight and at_aim(value, outgoing))
    control('forge_play_tune', entity=outgoing, component='forge.camera_aim', property='offset_y', value=1)
    final, _ = until(observation, lambda value: value[0][outgoing]['components']['forge.camera_aim']['offset_y'] == 1 and at_aim(value, outgoing))
    beyond = read('forge_play_cameras', width=800, height=600, offset=10000, limit=1)
    assert beyond['total'] == 1 and beyond['cameras'] == []
    assert all(final[actor]['world_affine'] == preserved[actor] for actor in preserved)
    assert read('forge_scene_read') == authored
    state = read('forge_play_status')
    failed(mutate('forge_play_tune', launch=state['launch'], session=state['session'], generation=state['generation']+1,
                  entity=outgoing, component='forge.camera_aim', property='weight', value=1), 'stale_play')
    review = control('forge_play_tuning_capture', entity=outgoing)
    assert {'weight', 'offset_y'} <= {row['property'] for row in review['fields'] if row['component'] == 'forge.camera_aim'}
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    review = read('forge_play_tuning_read')
    ok('forge_play_tuning_apply', state=review['state'])
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
