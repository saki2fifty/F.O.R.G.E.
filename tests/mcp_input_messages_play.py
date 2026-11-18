"""Typed messages use existing Play launch scopes and input injection owners."""
import json
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
    first_sources = {row['entity']['entity']: row for row in first['sources']}
    assert set(first_sources) == set(ids) and first['preview_limit_bytes'] == 2048
    names = {row['id']: row['name'] for row in authored['entities']}
    for source, row in first_sources.items():
        assert row['name'] == names[source] and row['action_name']
        assert row['emitted_total'] == 1 and row['last_sent']['tick'] == first['last_sent_tick']
        assert not row['last_sent']['truncated']
        assert row['last_sent']['encoded_bytes'] == len(row['last_sent']['preview'].encode('utf-8'))
        assert json.loads(row['last_sent']['preview'])['x'] == baseline[source]
    until(positions, lambda rows: rows == {key: value + .5 for key, value in baseline.items()})
    until(messages, lambda row: row.get('tick', 0) >= first['tick'] + 4)
    assert messages()['emitted_total'] == 2
    for row in messages()['sources']:
        assert row['emitted_total'] == 1
        assert row['last_sent'] == first_sources[row['entity']['entity']]['last_sent']
    scope = read('forge_play_status')
    failed(mutate('forge_play_input_submit', launch=scope['launch'], session=scope['session'],
                  generation=scope['generation'] + 1, events=[dict(control='key.space', value=0)]),
           'stale_play')
    control('forge_play_input_submit', events=[dict(control='key.space', value=0),
                                             dict(control='key.space', value=1)])
    second = until(messages, lambda row: row.get('emitted_total') == 4)
    for row in second['sources']:
        assert row['emitted_total'] == 2
        assert json.loads(row['last_sent']['preview'])['x'] == baseline[row['entity']['entity']] + .5
    until(positions, lambda rows: rows == {key: value + 1 for key, value in baseline.items()})
    control('forge_play_pause')
    assert messages()['emitted_total'] == 4 and read('forge_scene_read') == authored
    until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
    paused_messages = messages()
    assert messages() == paused_messages
    control('forge_play_stop')
    ok('forge_play_start')
    control('forge_play_pause')
    assert messages()['emitted_total'] == 0 and positions() == baseline
    assert all(row['emitted_total'] == 0 and row['last_sent'] is None for row in messages()['sources'])
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    check_input_trigger_play(read, ok, control, until)


def check_input_trigger_play(read, ok, control, until):
    ok('forge_document_open', path='Scenes/159-input-message-triggers.scene.json', discard=True)
    authored = read('forge_scene_read')
    sources = {row['components']['forge.input_message']['action']: row
               for row in authored['entities'] if 'forge.input_message' in row['components']}
    assert sorted(row['components']['forge.input_message']['trigger'] for row in sources.values()) == [1, 2, 2]
    release = sources['8c68fc99-bdb7-4bf4-9b91-dae060349ff0']['id']
    axis1 = sources['15900000-1590-4159-8159-000000000010']['id']
    axis2 = sources['15900000-1590-4159-8159-000000000011']['id']

    def messages():
        return read('forge_play_input')['value'].get('messages', {})

    def positions():
        return {row['id']: row['components']['forge.local_translation']
                for row in read('forge_play_snapshot')['entities'] if row['id'] in (release, axis1, axis2)}

    baseline = {row['id']: row['components']['forge.local_translation'] for row in sources.values()}
    ok('forge_play_start')
    control('forge_play_pause')
    assert messages()['emitted_total'] == 0 and positions() == baseline
    control('forge_play_input_submit', events=[dict(control='key.e', value=1)])
    control('forge_play_step')
    assert messages()['emitted_total'] == 0 and positions() == baseline
    control('forge_play_resume')
    control('forge_play_input_submit', events=[dict(control='key.space', value=1)])
    first = messages()
    until(messages, lambda row: row.get('tick', 0) >= first.get('tick', 0) + 3)
    assert messages()['emitted_total'] == 0
    control('forge_play_input_submit', events=[dict(control='key.space', value=0)])
    until(messages, lambda row: row.get('emitted_total') == 1)
    until(positions, lambda rows: rows[release]['y'] == 2)
    control('forge_play_input_submit', events=[dict(control='key.e', value=1), dict(control='key.d', value=1)])
    changed = until(messages, lambda row: row.get('emitted_total') == 3)
    until(positions, lambda rows: rows[axis1]['y'] == 3.5 and rows[axis2]['x'] == 3.5)
    until(messages, lambda row: row.get('tick', 0) >= changed['tick'] + 4)
    assert messages()['emitted_total'] == 3
    assert {row['trigger'] for row in messages()['sources']} == {1, 2}
    control('forge_play_input_submit', events=[dict(control='key.e', value=0), dict(control='key.d', value=0)])
    until(messages, lambda row: row.get('emitted_total') == 5)
    for row in messages()['sources']:
        assert row['emitted_total'] == (1 if row['entity']['entity'] == release else 2)
        assert row['last_sent'] is not None and not row['last_sent']['truncated']
    until(positions, lambda rows: rows[axis1]['y'] == 1.5 and rows[axis2]['x'] == 1.5)
    control('forge_play_pause')
    assert read('forge_scene_read') == authored
    control('forge_play_stop')
    ok('forge_play_start')
    control('forge_play_pause')
    assert messages()['emitted_total'] == 0 and positions() == baseline
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
