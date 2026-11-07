"""Compatible property pairs use shared Scene/history and real scoped Play owners."""
from mcp_test_client import failed


def check_sequence_property_play(read, ok, control, mutate, until, discovery, binding_pairs=False):
    path = ('Scenes/184-sequence-binding-crossfades.scene.json' if binding_pairs
            else 'Scenes/183-sequence-property-crossfades.scene.json')
    ok('forge_document_open', path=path, discard=True)
    source = read('forge_scene_read')
    controller = next(row['id'] for row in source['entities']
                      if 'forge.animation_sequence' in row['components'])
    primary_component = 'forge.mesh_renderer' if binding_pairs else 'forge.light'
    secondary_component = 'forge.ui_values' if binding_pairs else 'forge.sprite_renderer'
    light = next(row['id'] for row in source['entities'] if
                 'forge.transform_animator' in row['components'] and primary_component in row['components'])
    sprite = next(row['id'] for row in source['entities'] if
                  'forge.transform_animator' in row['components'] and secondary_component in row['components'])
    component = next(row for row in discovery()['result']['schema']['components']
                     if row['id'] == 'forge.transform_animator')
    fields = {row['id']: row for row in component['fields']}
    assert fields['sequence_next_clip']['asset_type'] == 'transform_animation'
    assert fields['sequence_next_start_seconds']['minimum'] == 0
    ok('forge_property_set_many', arguments=dict(entity=light, component=component['id'],
                                                values=dict(sequence_next_start_seconds=1.25)))
    changed = read('forge_scene_read')
    assert changed != source
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == source
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == changed
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == source
    ok('forge_document_save')
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
    failed(mutate('forge_play_animation_control', launch=paused['launch'], session=paused['session'],
                  generation=paused['generation'], entity=controller, operation='seek',
                  position_seconds=1.5), 'stale_watch')
    control('forge_play_watch', entity=controller)
    until(lambda: read('forge_play_animation'), lambda row: (row.get('value') or {}).get('available'))

    def sample():
        value = read('forge_play_snapshot')
        rows = {row['id']: row['components'] for row in value['entities']}
        if binding_pairs:
            return rows[light][primary_component], {v['name']: v['value'] for v in
                                                    rows[sprite][secondary_component]['values']}
        return rows[light]['forge.light'], rows[sprite]['forge.sprite_renderer']['frame']

    def matches(value, weight):
        if binding_pairs:
            mesh, values = value
            parameters = {v['key']: v['parameter']['value'] for v in mesh['material_parameters']}
            expected = (.1+.8*weight, .2+.1*weight, .9-.85*weight, 1.)
            return (len(parameters['baseColorFactor']) == 4 and len(values) == 2
                    and all(abs(x-y) < 1e-5 for x, y in zip(parameters['baseColorFactor'], expected))
                    and abs(parameters['roughnessFactor'][0]-(.1+.8*weight)) < 1e-5
                    and abs(values['amount']-(.2+.6*weight)) < 1e-5
                    and abs(values['width']-(80+140*weight)) < 1e-5)
        lamp, frame = value
        expected = dict(intensity=2+2*weight, color_r=.1+.9*weight,
                        color_g=.2+.15*weight, color_b=1-.95*weight)
        return all(abs(lamp[key]-number) < 1e-5 for key, number in expected.items()) and frame == (0 if weight < .5 else 3)

    for seconds, weight in ((0, 0), (1.25, .25), (1.49999, .49999), (1.5, .5), (2, 1), (3.5, 1)):
        control('forge_play_animation_control', entity=controller, operation='seek',
                position_seconds=seconds)
        until(sample, lambda value: matches(value, weight))
        if binding_pairs:
            def ui_matches(row):
                documents = (row.get('value') or {}).get('documents', [])
                return (len(documents) == 1
                        and abs(documents[0]['model'].get('amount', -1)-(.2+.6*weight)) < 1e-5
                        and abs(documents[0]['model'].get('width', -1)-(80+140*weight)) < 1e-5)
            until(lambda: read('forge_play_ui'), ui_matches)
        assert read('forge_play_status')['timing']['tick'] == paused['timing']['tick']
        assert read('forge_scene_read') == saved
    current = read('forge_play_status')
    failed(mutate('forge_play_animation_control', launch=current['launch'], session=current['session'],
                  generation=current['generation']+1, entity=controller, operation='seek',
                  position_seconds=1.5), 'stale_play')
    assert matches(sample(), 1)
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    # A fresh launch derives values from saved sources, not the previous runtime.
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
    control('forge_play_watch', entity=controller)
    until(lambda: read('forge_play_animation'), lambda row: (row.get('value') or {}).get('available'))
    control('forge_play_animation_control', entity=controller, operation='seek', position_seconds=1.5)
    until(sample, lambda value: matches(value, .5))
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
