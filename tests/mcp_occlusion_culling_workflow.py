"""Occlusion Scene controls share human authoring, revisions, history and saving."""
import json


def check_occlusion_culling_workflow(client, discover, arguments, mutate, project, command, tool):
    schema = command['input_schema']['properties']['occlusion_culling']
    assert tool['inputSchema']['properties']['arguments']['properties']['occlusion_culling'] == schema
    assert set(schema['properties']) == {'enabled', 'depth_bias'}
    assert schema['properties']['depth_bias'] == dict(type='number', minimum=0, maximum=.01)
    before = discover()
    values = dict(enabled=True, depth_bias=.002)
    request = arguments(arguments=dict(occlusion_culling=values))
    changed = client.tool('forge_scene_rendering_set', **request)
    assert changed['ok'] and changed['revision'] != before['revision'], changed
    assert client.tool('forge_scene_rendering_set', **request) == changed
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_history_redo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    assert json.loads((project / 'Scenes/main.scene.json').read_bytes())['rendering']['occlusion_culling'] == values
    for bad in (dict(enabled=1), dict(depth_bias=True), dict(depth_bias='0'),
                dict(depth_bias=-1e-30), dict(depth_bias=.01000000000001)):
        previous = discover()
        result = mutate('forge_scene_rendering_set', arguments=dict(occlusion_culling=bad))
        assert not result['ok'] and discover()['revision'] == previous['revision'], result
    previous = discover()
    stale = arguments(arguments=dict(occlusion_culling=dict(enabled=False)))
    stale['expected_revision'] -= 1
    assert not client.tool('forge_scene_rendering_set', **stale)['ok']
    assert discover()['revision'] == previous['revision']
    for bias in (0, .01):
        assert mutate('forge_scene_rendering_set', arguments=dict(occlusion_culling=dict(depth_bias=bias)))['ok']
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_history_undo')['result']['changed']
    assert mutate('forge_document_save')['ok']
    assert json.loads((project / 'Scenes/main.scene.json').read_bytes())['rendering']['occlusion_culling'] == values
