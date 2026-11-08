"""Shared atomic surface-stroke workflow through the production MCP boundary."""
import copy
from mcp_test_client import failed


def check_foliage_stroke(read, mutate, client, discover, declared, surface, arguments,
                         profile, expected_height):
    preparation = next(t for t in declared if t['name'] == 'forge_foliage_stroke_prepare')
    assert preparation['annotations']['readOnlyHint']
    command = next(t for t in declared if t['name'] == 'forge_mesh_stroke')
    assert not command['annotations']['readOnlyHint']
    dab = {key: value for key, value in arguments.items() if key not in ('entity', 'name')}
    stroke = dict(entity=arguments['entity'], name='Atomic surface brush', dabs=[dab, copy.deepcopy(dab)])
    stroke['dabs'][1]['center'] = dict(dab['center'], x=dab['center']['x'] + .2)
    before = read('forge_scene_read')
    plan = read('forge_foliage_stroke_prepare', surface=surface, surface_profile=profile,
                arguments=stroke, height_offset=.4, align_to_slope=True)
    assert plan['candidate_count'] == plan['retained_count'] == 24
    assert read('forge_scene_read') == before
    state = discover()
    request = dict(target=state['result']['target'], expected_revision=plan['scene_revision'],
                   sequence=state['next_sequence'], arguments=plan['arguments'])
    result = client.tool('forge_mesh_stroke', **request)
    assert result['ok'], result
    assert client.tool('forge_mesh_stroke', **request) == result
    placed = result['result']['results'][0]
    assert len(placed['groups']) == 2 and len(placed['created']) == 24
    after = read('forge_scene_read')
    rows = {row['id']: row for row in after['entities']}
    for entity in placed['created']:
        point = [0., 0., 0.]
        row = rows[entity]
        for _ in range(3):
            translation = row['components']['forge.local_translation']
            point = [point[i] + translation[axis] for i, axis in enumerate(('x', 'y', 'z'))]
            if not row.get('parent'):
                break
            row = rows[row['parent']]
        assert abs(point[1] - expected_height(point[0], point[2])) < 1e-9, point
    assert mutate('forge_history_undo')['result']['changed'] and read('forge_scene_read') == before
    assert mutate('forge_history_redo')['result']['changed'] and read('forge_scene_read') == after
    failed(client.tool('forge_mesh_stroke', **dict(request, sequence=discover()['next_sequence'])),
           'stale_revision')
    assert mutate('forge_history_undo')['result']['changed'] and read('forge_scene_read') == before
    for change in ('outside', 'over_budget', 'derived', 'bad_profile'):
        invalid = copy.deepcopy(stroke)
        bad_profile = profile
        if change == 'outside':
            invalid['dabs'][1]['center']['x'] = 10000
        elif change == 'over_budget':
            for dab in invalid['dabs']:
                dab['count'] = 257
                dab['spacing'] = 0
        elif change == 'derived':
            invalid['dabs'][1]['world_heights'] = [0] * 12
        else:
            bad_profile = 'unavailable'
        state = discover()
        failed(client.tool('forge_foliage_stroke_prepare', target=state['result']['target'],
                           surface=surface, surface_profile=bad_profile, arguments=invalid),
               'surface_coverage' if change == 'outside' else 'invalid_arguments')
        assert discover()['revision'] == state['revision'] and read('forge_scene_read') == before
