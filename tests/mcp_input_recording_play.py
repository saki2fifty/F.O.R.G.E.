"""Input recording through the existing native Play owner and real MCP envelopes."""
import json
from mcp_test_client import test_symlink


def check_input_recording_play(root, read, ok, control, mutate, edit_settings, until, failed):
    action = '23000000-0000-4230-8230-000000000001'
    mapping = dict(version=1, actions=[dict(id=action, name='Recorded action', kind='digital',
                                          bindings=[dict(control='key.space')])])
    edit_settings('forge_project_settings_edit',
                  patch=[dict(op='replace', path='/input', value=mapping)])
    edit_settings('forge_project_settings_save')
    ok('forge_document_save')
    authored = read('forge_scene_read')

    def ready(predicate=lambda row: True):
        return until(lambda: read('forge_play_status'),
                     lambda row: row['control_ready'] and
                     not row['input_recording_pending'] and predicate(row))

    def step():
        tick = ready()['timing']['tick']
        control('forge_play_step')
        ready(lambda row: row['timing']['tick'] == tick + 1)
        return read('forge_play_input')['value']['actions']

    ok('forge_play_start', paused=True)
    initial = ready()
    assert initial['paused'] and initial['timing']['tick'] == 0, initial
    control('forge_play_input_recording_control', operation='begin')
    ready(lambda row: row['input_recording']['mode'] == 'recording')
    expected = []
    for events in ([dict(control='key.space', value=1), dict(control='key.space', value=0)],
                   [dict(control='key.space', value=1)], [], [dict(reset=True)]):
        if events:
            control('forge_play_input_submit', events=events)
        expected.append(step())
    control('forge_play_input_recording_control', operation='finish')
    ready(lambda row: row['input_recording']['mode'] == 'recorded')
    control('forge_play_input_recording_control', operation='read')
    ready(lambda row: row['input_recording_ready'])
    path = 'Recordings/take.json'
    control('forge_play_input_recording_save', path=path)
    document = json.loads((root / 'Game' / path).read_text())
    assert len(document['frames']) == 4 and document['input_map'] == mapping
    scope = {key: ready()[key] for key in ('launch', 'session', 'generation')}
    overwrite = mutate('forge_play_input_recording_save', **scope, path=path)
    assert not overwrite['ok'], overwrite
    for invalid in ('forge.project.json', '../outside.json', 'Recordings/../forge.project.json'):
        rejected = mutate('forge_play_input_recording_save', **scope, path=invalid, replace=True)
        assert not rejected['ok'], rejected
    settings_path = root / 'Game/forge.project.json'
    settings_bytes = settings_path.read_bytes()
    with test_symlink(root / 'Game/Recordings/linked.json', settings_path) as available:
        if available:
            rejected = mutate('forge_play_input_recording_save', **scope,
                              path='Recordings/linked.json', replace=True)
            assert not rejected['ok'] and settings_path.read_bytes() == settings_bytes, rejected
    control('forge_play_stop')
    ok('forge_play_start', paused=True)
    replay = ready()
    malformed = root / 'Game/Recordings/duplicate.json'
    malformed.write_text('{"version":1,' + json.dumps(document)[1:])
    bad = mutate('forge_play_input_recording_control',
                 **{key: replay[key] for key in ('launch', 'session', 'generation')},
                 operation='replay', path='Recordings/duplicate.json')
    assert not bad['ok'] and ready()['input_recording']['mode'] == 'idle', bad
    stale = mutate('forge_play_input_recording_control', **scope, operation='begin')
    failed(stale, 'stale_play')
    control('forge_play_input_recording_control', operation='replay', path=path)
    ready(lambda row: row['input_recording']['mode'] == 'replaying')
    for expected_actions in expected:
        control('forge_play_input_submit', events=[dict(control='key.space', value=1)])
        assert step() == expected_actions
    ready(lambda row: row['input_recording']['mode'] == 'finished')
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    assert json.loads((root / 'Game' / path).read_text()) == document
