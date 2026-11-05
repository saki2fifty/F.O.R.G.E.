"""Morphological Scene controls use the shared revision/history/source owner."""
import json


def check_morphological_aa_workflow(client, discover, arguments, mutate, project, command, tool):
    schema = command['input_schema']['properties']['morphological_anti_aliasing']
    assert tool['inputSchema']['properties']['arguments']['properties']['morphological_anti_aliasing'] == schema
    assert set(schema['properties']) == {'enabled', 'edge_threshold', 'search_steps', 'corner_rounding'}
    assert schema['properties']['search_steps'] == dict(type='integer', minimum=1, maximum=32)
    assert schema['properties']['edge_threshold']['minimum'] == .01
    values = dict(enabled=True, edge_threshold=.125, search_steps=24, corner_rounding=.25)
    before = discover()
    request = arguments(arguments=dict(morphological_anti_aliasing=values,
                                      fast_approximate_anti_aliasing=dict(enabled=False)))
    changed = client.tool('forge_scene_rendering_set', **request)
    assert changed['ok'] and changed['revision'] != before['revision'], changed
    assert client.tool('forge_scene_rendering_set', **request) == changed
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_history_redo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    saved = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    assert saved['rendering']['morphological_anti_aliasing'] == values
    assert not saved['rendering']['fast_approximate_anti_aliasing']['enabled']
    for bad in (dict(enabled=1), dict(edge_threshold=.009999999999),
                dict(edge_threshold=.50000000001), dict(search_steps=0),
                dict(search_steps=33), dict(search_steps=1.5), dict(search_steps=True),
                dict(corner_rounding=-1e-10), dict(corner_rounding=1.00000000001)):
        previous = discover()
        result = mutate('forge_scene_rendering_set', arguments=dict(morphological_anti_aliasing=bad))
        assert not result['ok'] and discover()['revision'] == previous['revision'], result
    previous = discover()
    conflict = mutate('forge_scene_rendering_set', arguments=dict(fast_approximate_anti_aliasing=dict(enabled=True)))
    assert not conflict['ok'] and discover()['revision'] == previous['revision'], conflict
    stale = arguments(arguments=dict(morphological_anti_aliasing=dict(search_steps=32)))
    stale['expected_revision'] -= 1
    assert not client.tool('forge_scene_rendering_set', **stale)['ok']
    assert discover()['revision'] == previous['revision']
    assert mutate('forge_scene_rendering_set', arguments=dict(
        morphological_anti_aliasing=dict(enabled=False), fast_approximate_anti_aliasing=dict(enabled=True)))['ok']
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    assert json.loads((project / 'Scenes/main.scene.json').read_bytes())['rendering']['morphological_anti_aliasing'] == values
