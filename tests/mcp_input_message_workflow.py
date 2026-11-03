"""Authored messages use the shared component schema, revision and file owners."""
import json
from mcp_test_client import failed


def check_input_message_workflow(client, discover, arguments, mutate, project):
    component = 'forge.input_message'
    schema = next(c for c in discover()['result']['schema']['components'] if c['id'] == component)
    assert schema['optional'] and schema['category'] == 'Gameplay'
    fields = {f['id']: f for f in schema['fields']}
    assert {k: v['default'] for k, v in fields.items()} == dict(enabled=True, action='', payload='')
    created = mutate('forge_entity_create', arguments=dict(kind=0, name='Message source'))
    assert created['ok'], created
    entity = created['result']['results'][0]['selected']
    assert mutate('forge_component_add', arguments=dict(entity=entity, component=component))['ok']

    def config():
        result = client.tool('forge_scene_read', target=discover()['result']['target'])
        assert result['ok'], result
        return next(row for row in result['result']['entities'] if row['id'] == entity)['components'][component]

    before = config()
    values = dict(action='23000000-0000-4230-8230-000000000001', payload='forge.tint')
    request = arguments(arguments=dict(entity=entity, component=component, values=values))
    changed = client.tool('forge_property_set_many', **request)
    assert changed['ok'] and client.tool('forge_property_set_many', **request) == changed
    assert all(config()[key] == value for key, value in values.items())
    assert mutate('forge_history_undo')['result']['changed'] and config() == before
    assert mutate('forge_history_redo')['result']['changed']
    placed = config()
    failed(client.tool('forge_property_set_many', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    for field, bad in (('enabled', 1), ('action', 'Space'), ('payload', 'invalid key'),
                       ('payload', 'x' * 129)):
        revision = discover()['revision']
        rejected = mutate('forge_property_set', arguments=dict(entity=entity, component=component,
                                                               field=field, value=bad))
        assert not rejected['ok'] and discover()['revision'] == revision, rejected
        assert config() == placed
    assert mutate('forge_document_save')['ok']
    saved = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    assert next(row for row in saved['entities'] if row['id'] == entity)['components'][component] == placed
    assert mutate('forge_component_revert', arguments=dict(entity=entity, component=component))['ok']
    assert mutate('forge_history_undo')['result']['changed'] and config() == placed
    assert mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)['ok']
    assert config() == placed
