"""Input recording through the existing native Play owner and real MCP envelopes."""
import json
from mcp_test_client import test_symlink


def check_session_input_recording_play(root, read, ok, control, mutate, until, failed):
    """Use the Gallery's real native game requests and the existing MCP owners."""
    scene_a = '306f7bf0-c7cc-4733-9043-7c379d2afff2'
    scene_b = 'b78ea324-266f-490e-94e1-8159d010302a'
    authored = read('forge_scene_read')

    def ready(predicate=lambda row: True):
        return until(lambda: read('forge_play_status'), lambda row:
                     row['control_ready'] and not row['input_recording_pending'] and predicate(row))

    def key(name):
        control('forge_play_input_submit', events=[dict(control=name, value=1),
                                                   dict(control=name, value=0)])

    def room(asset, x=None):
        def matches(row):
            if row['asset_id'] != asset:
                return False
            cube = next(entity for entity in row['entities'] if entity['name'].startswith('Room '))
            return x is None or cube['components']['forge.local_translation']['x'] == x
        return until(lambda: read('forge_play_snapshot'), matches)

    ok('forge_play_start', paused=True)
    first = ready()
    assert first['input_recording_session_available'], first
    scope = {key: first[key] for key in ('launch', 'session', 'generation')}
    control('forge_play_input_recording_control', operation='begin', capture_scope='session')
    ready(lambda row: row['input_recording']['mode'] == 'recording')
    control('forge_play_resume')
    key('key.space')
    room(scene_a, -.5)
    key('key.tab')
    room(scene_b)
    failed(mutate('forge_play_input_recording_control', **scope, operation='finish'), 'stale_play')
    key('key.space')
    room(scene_b, 1.5)
    key('key.tab')
    room(scene_a)
    key('key.space')
    room(scene_a, -.5)
    control('forge_play_pause')
    ready(lambda row: row['paused'])
    control('forge_play_input_recording_control', operation='finish')
    ready(lambda row: row['input_recording']['mode'] == 'recorded')
    control('forge_play_input_recording_control', operation='read')
    ready(lambda row: row['input_recording_ready'])
    path = 'Recordings/across-scenes.json'
    control('forge_play_input_recording_save', path=path)
    document = json.loads((root / 'Game' / path).read_text())
    assert [segment['scene'] for segment in document['segments']] == [scene_a, scene_b, scene_a]
    control('forge_play_stop')

    ok('forge_play_start', paused=True)
    ready()
    control('forge_play_input_recording_control', operation='replay', path=path)
    ready(lambda row: row['input_recording']['mode'] == 'replaying')
    control('forge_play_resume')
    completed = ready(lambda row: row['input_recording']['mode'] == 'finished')
    assert completed['input_recording']['consumed_ticks'] == sum(
        len(segment['frames']) for segment in document['segments'])
    room(scene_a, -.5)
    control('forge_play_stop')
    assert read('forge_scene_read') == authored

    changed = json.loads(json.dumps(document))
    changed['segments'][1]['source'] = '0' * 64
    bad_path = 'Recordings/mismatching-scene.json'
    (root / 'Game' / bad_path).write_text(json.dumps(changed))
    ok('forge_play_start', paused=True)
    ready()
    control('forge_play_input_recording_control', operation='replay', path=bad_path)
    ready(lambda row: row['input_recording']['mode'] == 'replaying')
    control('forge_play_resume')
    ready(lambda row: row['input_recording'].get('waiting_for_scene', False))
    room(scene_a)
    control('forge_play_input_recording_control', operation='finish')
    ready(lambda row: row['input_recording']['mode'] == 'canceled')
    control('forge_play_stop')
    assert read('forge_scene_read') == authored
    assert json.loads((root / 'Game' / path).read_text()) == document


def check_input_recording_play(root, read, ok, control, mutate, edit_settings, until, failed):
    action = '23000000-0000-4230-8230-000000000001'
    mapping = dict(version=1, actions=[dict(id=action, name='Recorded action', kind='digital',
                                          bindings=[dict(control='key.space')])])
    edit_settings('forge_project_settings_game_defaults')
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
    scope_now = {key: initial[key] for key in ('launch', 'session', 'generation')}
    for operation, extra in [('begin', dict(capture_scope='invalid')),
                             ('finish', dict(capture_scope='scene')),
                             ('read', dict(capture_scope='session'))]:
        rejected = mutate('forge_play_input_recording_control', **scope_now,
                          operation=operation, **extra)
        assert not rejected['ok'] and ready()['input_recording']['mode'] == 'idle', rejected
    if not initial['input_recording_session_available']:
        failed(mutate('forge_play_input_recording_control', **scope_now,
                      operation='begin', capture_scope='session'), 'unsupported_profile')
        assert ready()['input_recording']['mode'] == 'idle'
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

    if initial['input_recording_session_available']:
        ok('forge_play_start', paused=True)
        session_initial = ready()
        assert session_initial['input_recording_session_available']
        control('forge_play_input_recording_control', operation='begin', capture_scope='session')
        ready(lambda row: row['input_recording']['mode'] == 'recording')
        control('forge_play_input_submit', events=[dict(control='key.space', value=1)])
        session_actions = step()
        control('forge_play_input_recording_control', operation='finish')
        ready(lambda row: row['input_recording']['mode'] == 'recorded')
        control('forge_play_input_recording_control', operation='read')
        ready(lambda row: row['input_recording_ready'])
        session_path = 'Recordings/session-take.json'
        control('forge_play_input_recording_save', path=session_path)
        session_document = json.loads((root / 'Game' / session_path).read_text())
        assert session_document['version'] == 2 and len(session_document['segments']) == 1
        assert len(session_document['segments'][0]['frames']) == 1
        control('forge_play_stop')
        ok('forge_play_start', paused=True)
        ready()
        control('forge_play_input_recording_control', operation='replay', path=session_path)
        ready(lambda row: row['input_recording']['mode'] == 'replaying')
        assert step() == session_actions
        ready(lambda row: row['input_recording']['mode'] == 'finished')
        control('forge_play_stop')
        assert read('forge_scene_read') == authored
        assert json.loads((root / 'Game' / session_path).read_text()) == session_document
