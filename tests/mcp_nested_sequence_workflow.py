"""Nested controller fields use the same Scene schema, history and file owner."""
import json
from mcp_test_client import failed


def check_nested_sequence_workflow(client, discover, arguments, mutate, project):
    state = discover()
    component = next(c for c in state['result']['schema']['components']
                     if c['id'] == 'forge.animation_sequence')
    fields = {field['id']: field for field in component['fields']}
    assert fields['sequence']['default'] is None
    assert fields['sequence']['display_name'] == 'Parent sequence'
    assert fields['sequence_start_seconds']['default'] == 0
    assert fields['sequence_start_seconds']['minimum'] == 0
    assert fields['sequence_start_seconds']['maximum'] == 3600
    assert fields['sequence_rate']['default'] == 1
    assert fields['sequence_rate']['minimum'] == .25
    assert fields['sequence_rate']['maximum'] == 4
    assert fields['sequence_rate']['display_name'] == 'Placement rate'
    ids = []
    for name in ('Parent', 'Child'):
        result = mutate('forge_entity_create', arguments=dict(recipe='animation.sequence', name=name))
        assert result['ok'], result
        ids.append(result['result']['results'][0]['selected'])
    parent, child = ids

    def snapshot():
        value = discover()
        result = client.tool('forge_scene_read', target=value['result']['target'])
        assert result['ok'], result
        return result['result']

    def config():
        return next(r for r in snapshot()['entities'] if r['id'] == child)['components'][component['id']]

    before = config()
    values = dict(sequence=dict(scene=snapshot()['asset_id'], entity=parent),
                  sequence_start_seconds=.5, sequence_rate=2, loop=True, playback_speed=0, play_on_start=False)
    request = arguments(arguments=dict(entity=child, component=component['id'], values=values))
    changed = client.tool('forge_property_set_many', **request)
    assert changed['ok'], changed
    assert client.tool('forge_property_set_many', **request) == changed
    assert all(config()[field] == value for field, value in values.items())
    assert mutate('forge_history_undo')['result']['changed']
    assert config() == before
    assert mutate('forge_history_redo')['result']['changed']
    placed = config()
    failed(client.tool('forge_property_set_many', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    for value in (-.1, 3600.1, 'later', False):
        state = discover()
        result = mutate('forge_property_set', arguments=dict(entity=child, component=component['id'],
                        field='sequence_start_seconds', value=value))
        assert not result['ok'] and discover()['revision'] == state['revision'], result
        assert config() == placed
    for value in (0, -.5, .249, 4.001, 'fast', False):
        state = discover()
        result = mutate('forge_property_set', arguments=dict(entity=child, component=component['id'],
                        field='sequence_rate', value=value))
        assert not result['ok'] and discover()['revision'] == state['revision'], result
        assert config() == placed
    assert mutate('forge_document_save')['ok']
    saved = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    assert next(r for r in saved['entities'] if r['id'] == child)['components'][component['id']] == placed
    assert mutate('forge_property_set', arguments=dict(entity=child, component=component['id'],
                   field='sequence', value=None))['ok']
    unbound = config()
    assert unbound['sequence'] is None
    assert all(unbound[field] == value for field, value in values.items() if field != 'sequence')
    assert mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)['ok']
    assert config() == placed
