"""Shared fast-AA schema/history/save checks through the actual MCP owner."""
import json


def check_fast_aa_workflow(client, discover, arguments, mutate, project, command, tool):
    fast_schema = command['input_schema']['properties']['fast_approximate_anti_aliasing']
    assert tool['inputSchema']['properties']['arguments']['properties']['fast_approximate_anti_aliasing'] == fast_schema
    assert set(fast_schema['properties']) == {'enabled', 'subpixel_strength', 'search_steps'}
    assert fast_schema['properties']['subpixel_strength']['maximum'] == 1
    assert fast_schema['properties']['search_steps'] == dict(type='integer', minimum=1, maximum=16)
    fast_values = dict(enabled=True, subpixel_strength=.75, search_steps=8)
    fast_arguments = arguments(arguments=dict(fast_approximate_anti_aliasing=fast_values))
    before_fast = discover()
    changed = client.tool('forge_scene_rendering_set', **fast_arguments)
    assert changed['ok'] and changed['revision'] != before_fast['revision'], changed
    assert client.tool('forge_scene_rendering_set', **fast_arguments) == changed
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_history_redo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    saved_fast = json.loads((project / 'Scenes/main.scene.json').read_bytes())
    assert saved_fast['rendering']['fast_approximate_anti_aliasing'] == fast_values
    for bad in (dict(enabled=1), dict(subpixel_strength=-.01),
                dict(subpixel_strength=1.000000001), dict(search_steps=0),
                dict(search_steps=17), dict(search_steps=1.5), dict(search_steps=True)):
        before = discover()
        result = mutate('forge_scene_rendering_set',
                        arguments=dict(fast_approximate_anti_aliasing=bad))
        assert not result['ok'] and discover()['revision'] == before['revision'], result
    assert mutate('forge_scene_rendering_set', arguments=dict(
        fast_approximate_anti_aliasing=dict(subpixel_strength=0, search_steps=16)))['ok']
