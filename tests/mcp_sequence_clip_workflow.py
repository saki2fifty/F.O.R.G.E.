"""Sequence clip fields use the existing Scene schema, history and file owner."""
import json
from mcp_test_client import failed


def check_sequence_clip_workflow(client, discover, arguments, mutate, project):
    state = discover()
    component = next(c for c in state['result']['schema']['components']
                     if c['id'] == 'forge.transform_animator')
    fields = {field['id']: field for field in component['fields']}
    assert fields['sequence_next_clip']['asset_type'] == 'transform_animation'
    assert fields['sequence_next_clip']['default'] is None
    assert fields['sequence_next_start_seconds']['default'] == 0
    assert fields['sequence_next_start_seconds']['minimum'] == 0
    assert fields['sequence_next_start_seconds']['maximum'] == 3600
    controller = mutate('forge_entity_create', arguments=dict(recipe='animation.sequence', name='Cut'))
    assert controller['ok'], controller
    controller = controller['result']['results'][0]['selected']
    actor = mutate('forge_entity_create', arguments=dict(recipe='primitive.0', name='Blended panel'))
    assert actor['ok'], actor
    actor = actor['result']['results'][0]['selected']
    assert mutate('forge_component_add', arguments=dict(entity=actor, component=component['id']))['ok']

    def snapshot():
        value = discover()
        result = client.tool('forge_scene_read', target=value['result']['target'])
        assert result['ok'], result
        return result['result']

    def track():
        return next(row for row in snapshot()['entities'] if row['id'] == actor)['components'][component['id']]

    baseline = track()
    values = dict(clip='939f0601-ce25-4f57-99fe-e359f4ad2de1',
                  sequence=dict(scene=snapshot()['asset_id'], entity=controller),
                  sequence_next_clip='b198df2e-54af-47ca-bcb8-7a70f873f79b',
                  sequence_next_start_seconds=.5)
    request = arguments(arguments=dict(entity=actor, component=component['id'], values=values))
    changed = client.tool('forge_property_set_many', **request)
    assert changed['ok'], changed
    assert client.tool('forge_property_set_many', **request) == changed
    assert all(track()[field] == value for field, value in values.items())
    assert mutate('forge_history_undo')['result']['changed']
    assert track() == baseline
    assert mutate('forge_history_redo')['result']['changed']
    expected = track()
    stale = dict(request, sequence=discover()['next_sequence'])
    failed(client.tool('forge_property_set_many', **stale), 'stale_revision')
    for invalid in (-.01, 3600.01, False, 'one'):
        before = discover()
        result = mutate('forge_property_set', arguments=dict(entity=actor, component=component['id'],
                        field='sequence_next_start_seconds', value=invalid))
        assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert track() == expected
    assert mutate('forge_document_save')['ok']
    saved = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    saved_track = next(row for row in saved['entities'] if row['id'] == actor)['components'][component['id']]
    assert saved_track == expected
    assert mutate('forge_property_set', arguments=dict(entity=actor, component=component['id'],
                   field='sequence_next_clip', value=None))['ok']
    assert track()['sequence_next_clip'] is None and track()['clip'] == values['clip']
    assert mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)['ok']
    assert track() == expected
