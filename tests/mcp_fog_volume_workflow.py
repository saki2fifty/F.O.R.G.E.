"""Fog values use the shared component/schema/history and saved document owners."""
import json
import math
from mcp_test_client import failed


def check_fog_volume_workflow(client, discover, arguments, mutate, project):
    key = 'forge.fog_volume'
    schema = next(row for row in discover()['result']['schema']['components'] if row['id'] == key)
    fields = {row['id']: row for row in schema['fields']}
    assert schema['optional'] and schema['category'] == 'Rendering' and len(fields) == 11
    assert fields['shape']['choices'] == [dict(label='Box', value=0), dict(label='Sphere', value=1)]
    assert fields['color_r']['maximum'] == 65504 and fields['layers']['maximum'] == 2**32 - 1
    assert math.isclose(fields['density']['default'], .15, abs_tol=1e-7)
    made = mutate('forge_entity_create', arguments=dict(recipe='render.fog_volume', name='Fog Volume'))
    assert made['ok'], made
    entity = made['result']['results'][0]['selected']

    def value():
        read = client.tool('forge_scene_read', target=discover()['result']['target'])
        assert read['ok'], read
        return next(row for row in read['result']['entities'] if row['id'] == entity)['components'][key]

    original = value()
    edit = dict(shape=1, color_r=.125, color_g=.25, color_b=.75, density=.5,
                half_extent_x=3, half_extent_y=2, half_extent_z=1, layers=3, priority=10)
    request = arguments(arguments=dict(entity=entity, component=key, values=edit))
    changed = client.tool('forge_property_set_many', **request)
    assert changed['ok'] and client.tool('forge_property_set_many', **request) == changed
    placed = value()
    assert all(placed[field] == expected for field, expected in edit.items())
    assert mutate('forge_history_undo')['result']['changed'] and value() == original
    assert mutate('forge_history_redo')['result']['changed'] and value() == placed
    failed(client.tool('forge_property_set_many', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    for field, invalid in (('shape', -1), ('shape', 2), ('shape', 2**32), ('shape', True),
                            ('shape', .5), ('enabled', 1), ('density', -1), ('density', 1000.001),
                            ('color_r', 65504.001), ('half_extent_x', .0001),
                            ('layers', 2**32), ('layers', False), ('priority', 2**31)):
        revision = discover()['revision']
        result = mutate('forge_property_set', arguments=dict(entity=entity, component=key,
                                                             field=field, value=invalid))
        assert not result['ok'] and discover()['revision'] == revision and value() == placed, result
    assert mutate('forge_document_save')['ok']
    source = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    assert next(row for row in source['entities'] if row['id'] == entity)['components'][key] == placed
    assert mutate('forge_component_revert', arguments=dict(entity=entity, component=key))['ok']
    assert mutate('forge_history_undo')['result']['changed'] and value() == placed
    assert mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)['ok']
    assert value() == placed
