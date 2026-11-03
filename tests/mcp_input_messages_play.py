"""Typed messages use existing Play launch scopes and input injection owners."""
from mcp_test_client import failed


def check_input_messages_play(read, ok, control, mutate, until):
    authored = read('forge_scene_read')
    ids = [row['id'] for row in authored['entities'] if 'forge.input_message' in row['components']]
    assert len(ids) == 2
    baseline = {row['id']: row['components']['forge.local_translation']['x']
                for row in authored['entities'] if row['id'] in ids}

    def messages():
        return read('forge_play_input')['value'].get('messages', {})

    def positions():
        return {row['id']: row['components']['forge.local_translation']['x']
                for row in read('forge_play_snapshot')['entities'] if row['id'] in ids}

    ok('forge_play_start')
    control('forge_play_pause')
    assert messages()['emitted_total'] == 0
    control('forge_play_input_submit', events=[dict(control='key.space', value=1)])
    control('forge_play_step')
    assert messages()['emitted_total'] == 0, 'SDK Step must retain clock input neutralization'
    control('forge_play_resume')
    control('forge_play_input_submit', events=[dict(control='key.space', value=1)])
    first = until(messages, lambda row: row.get('emitted_total') == 2)
    until(positions, lambda rows: rows == {key: value + .5 for key, value in baseline.items()})
    until(messages, lambda row: row.get('tick', 0) >= first['tick'] + 4)
    assert messages()['emitted_total'] == 2
    scope = read('forge_play_status')
    failed(mutate('forge_play_input_submit', launch=scope['launch'], session=scope['session'],
                  generation=scope['generation'] + 1, events=[dict(control='key.space', value=0)]),
           'stale_play')
    control('forge_play_input_submit', events=[dict(control='key.space', value=0),
                                             dict(control='key.space', value=1)])
    until(messages, lambda row: row.get('emitted_total') == 4)
    until(positions, lambda rows: rows == {key: value + 1 for key, value in baseline.items()})
    control('forge_play_pause')
    assert messages()['emitted_total'] == 4 and read('forge_scene_read') == authored
    control('forge_play_stop')
    ok('forge_play_start')
    control('forge_play_pause')
    assert messages()['emitted_total'] == 0 and positions() == baseline
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
