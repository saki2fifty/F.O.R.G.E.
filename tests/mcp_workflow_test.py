"""Native project/source/Play/UI/navigation/export owners through actual MCP."""
from contextlib import contextmanager
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed, test_symlink, finish_host

TOOLS, HOST, RUNTIME, KIT, NAV = sys.argv[1:6]
NAVIGATION_ONLY = sys.argv[7:] == ['--navigation-only']
SCOPE_TRACE_ONLY = sys.argv[7:] == ['--scope-trace-only']
MAP_PROFILE_ONLY = sys.argv[7:] == ['--map-profile-only']
SPRITE_FRAMES_ONLY = sys.argv[7:] == ['--sprite-frames-only']
TIME_ONLY = sys.argv[7:] == ['--time-only']
PROFILE_ONLY = sys.argv[7:] == ['--profile-only']
GESTURES_ONLY = sys.argv[7:] == ['--gestures-only']
TOUCH_ONLY = sys.argv[7:] == ['--touch-only']
ANALOG_ONLY = sys.argv[7:] == ['--analog-only']
AUDIO_PAUSE_ONLY = sys.argv[7:] == ['--audio-pause-only']


@contextmanager
def editor(mode='full'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-workflows-') as temporary:
        root = Path(temporary)
        command = [HOST, str(root), RUNTIME, KIT, NAV, mode]
        if MAP_PROFILE_ONLY or AUDIO_PAUSE_ONLY or SPRITE_FRAMES_ONLY:
            command.append(str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'))
        if AUDIO_PAUSE_ONLY:
            command.append('--sdk')
        process = subprocess.Popen(command,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, json.loads(line)
        finally:
            finish_host(process)


def until(fn, predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = fn()
        if predicate(value):
            return value
        time.sleep(.025)
    raise AssertionError(('Native owner timed out', value))


for legacy in (False, True):
    with editor() as (root, config), Client(config, legacy) as client:
        def discovery():
            value = client.tool('forge_discover')
            assert value['ok'], value
            return value

        def read(tool, **extra):
            value = client.tool(tool, target=discovery()['result']['target'], **extra)
            assert value['ok'], value
            return value['result']

        def arguments(**extra):
            value = discovery()
            return dict(target=value['result']['target'], expected_revision=value['revision'],
                        sequence=value['next_sequence'], **extra)

        def mutate(tool, **extra):
            return client.tool(tool, **arguments(**extra))

        def ok(tool, **extra):
            value = mutate(tool, **extra)
            assert value['ok'], value
            return value['result']

        def control(tool, **extra):
            state = until(lambda: read('forge_play_status'), lambda r: r['control_ready'])
            return ok(tool, launch=state['launch'], session=state['session'], generation=state['generation'], **extra)

        def edit_settings(tool, **extra):
            return ok(tool, state=read('forge_project_settings_read')['state'], **extra)

        def text(path): return read('forge_source_text_read', path=path)

        def write(path, value):
            return ok('forge_source_text_write', path=path, state=text(path)['state'], text=value)

        def navigation_floor():
            floor = ok('forge_entity_create', arguments=dict(kind=0, name='Navigation floor'))['results'][0]['selected']
            floor_row = next(row for row in read('forge_scene_read')['entities'] if row['id'] == floor)
            if 'forge.local_scale' not in floor_row['components']:
                ok('forge_component_add', arguments=dict(entity=floor, component='forge.local_scale'))
            ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.local_scale',
                                                        values=dict(x=20, y=1, z=20)))
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.navigation_surface'))
            ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
                                                       cell_size=.2, cell_height=.1))
            navigation = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert navigation['asset'] and not navigation['error'], navigation
            return floor, navigation

        advertised = {t['name']: t for t in client.request('tools/list')['result']['tools']}
        names = set(advertised)
        assert advertised['forge_play_start']['annotations']['openWorldHint']
        assert advertised['forge_export_start']['annotations']['openWorldHint']
        assert not advertised['forge_source_text_write']['annotations']['openWorldHint']
        assert {'forge_play_start', 'forge_play_ui_command', 'forge_export_start',
                'forge_script_preview', 'forge_navigation_build', 'forge_navigation_overlay',
                'forge_dependencies_save'} <= names
        if GESTURES_ONLY:
            assert 'log-scale' in advertised['forge_play_controls']['description']
            controls = {row['id']: row for row in read('forge_play_controls')['controls']}
            assert controls['touch.gesture.active']['digital']
            assert all(controls['touch.gesture.'+f]['relative'] for f in ('pan_x','pan_y','pinch','rotation'))
            fields = ('active','pan_x','pan_y','pinch','rotation')
            actions = [dict(id=f'15600000-1560-4156-8156-00000000001{i}', name='Gesture '+field,
                            kind='digital' if field == 'active' else 'axis1',
                            bindings=[dict(control='touch.gesture.'+field)]) for i,field in enumerate(fields)]
            mapping = dict(version=1, actions=actions)
            edit_settings('forge_project_settings_edit', patch=[dict(op='replace', path='/input', value=mapping)])
            before = read('forge_project_settings_read')
            bad = mutate('forge_project_settings_edit', state=before['state'],
                         patch=[dict(op='add', path='/input/actions/1/bindings/0/deadzone',value=.2)])
            assert not bad['ok'] and read('forge_project_settings_read')['state'] == before['state']
            edit_settings('forge_project_settings_save')
            authored = read('forge_scene_read');ok('forge_play_start')
            control('forge_play_pause')
            until(lambda: read('forge_play_status'), lambda s: s['paused'] and s['control_ready'])
            control('forge_play_input_submit', events=[dict(control='touch.gesture.'+f,value=v)
                for f,v in [('active',1),('pan_x',-.25),('pan_y',.125),('pinch',.5),('rotation',-.75),('active',0)]])
            control('forge_play_step')
            def values():
                return {r['id']:r for r in read('forge_play_input')['value'].get('actions',[])}
            observed = until(values, lambda r: r.get(actions[0]['id'],{}).get('presses',0) == 1)
            assert observed[actions[0]['id']]['releases'] == 1 and not observed[actions[0]['id']]['held']
            for action,expected in zip(actions[1:],(-.25,.125,.5,-.75)):
                assert observed[action['id']]['x'] == expected, observed
            control('forge_play_step')
            until(values, lambda r: all(r.get(a['id'],{}).get('x') == 0 for a in actions))
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert json.loads((root/'Game/forge.project.json').read_text())['input'] == mapping
            print(('legacy' if legacy else 'current') + ': gesture discovery/settings/rejection/relative Step/MCP passed')
            continue
        if TOUCH_ONLY:
            controls = read('forge_play_controls')['controls']
            touches = {row['id']: row for row in controls if row['id'].startswith('touch.') and not row['id'].startswith('touch.gesture.')}
            assert len(touches) == 32 and touches['touch.7.pressure']['minimum'] == 0
            assert touches['touch.0.down']['digital'] and touches['touch.0.x']['maximum'] == 1
            actions = [dict(id=f'15500000-1550-4155-8155-00000000001{i}', name='Touch '+field,
                            kind='digital' if field == 'down' else 'axis1',
                            bindings=[dict(control='touch.0.'+field)])
                       for i, field in enumerate(('down', 'x', 'y', 'pressure'))]
            mapping = dict(version=1, actions=actions)
            edit_settings('forge_project_settings_edit', patch=[dict(op='replace', path='/input', value=mapping)])
            baseline = read('forge_project_settings_read')
            bad = mutate('forge_project_settings_edit', state=baseline['state'],
                         patch=[dict(op='replace', path='/input/actions/0/bindings/0/control', value='touch.8.down')])
            assert not bad['ok'] and read('forge_project_settings_read')['state'] == baseline['state']
            edit_settings('forge_project_settings_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            state = until(lambda: read('forge_play_status'), lambda r: r['control_ready'])
            stale = dict(launch=state['launch'], session=state['session'], generation=state['generation']+1)
            rejected = mutate('forge_play_input_submit', events=[dict(control='touch.0.down', value=1)], **stale)
            assert not rejected['ok']
            for bad in (-.1, 1.1):
                rejected = mutate('forge_play_input_submit', launch=state['launch'], session=state['session'],
                                  generation=state['generation'], events=[dict(control='touch.0.down', value=1),
                                  dict(control='touch.0.x', value=bad)])
                assert not rejected['ok'], rejected
            control('forge_play_input_submit', events=[dict(control='touch.0.'+f, value=v)
                    for f, v in (('down', 1), ('x', .75), ('y', .25), ('pressure', .5))])
            def touch_values():
                rows = read('forge_play_input')['value'].get('actions', [])
                return {row['id']: row for row in rows}
            until(touch_values, lambda r: r.get(actions[0]['id'], {}).get('held') and
                  r.get(actions[1]['id'], {}).get('x') == .75 and
                  r.get(actions[2]['id'], {}).get('x') == .25)
            control('forge_play_input_submit', events=[dict(reset=True)])
            until(touch_values, lambda r: r.get(actions[0]['id'], {}).get('held') is False and
                  r.get(actions[1]['id'], {}).get('x') == 0)
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert json.loads((root/'Game/forge.project.json').read_text())['input'] == mapping
            print(('legacy' if legacy else 'current') + ': touch discovery/settings/save/value/stale/input/Stop passed')
            continue
        if ANALOG_ONLY:
            action = '12345678-1234-4234-8234-123456789abc'
            bindings = [dict(control='pad.left_x', x=1, y=0, radial=True, deadzone=.2,
                             outer_deadzone=.1, response_exponent=2),
                        dict(control='pad.left_y', x=0, y=1, radial=True, deadzone=.2,
                             outer_deadzone=.1, response_exponent=2)]
            mapping = dict(version=1, actions=[dict(id=action, name='Analog aim', kind='axis2', bindings=bindings)])
            changed = edit_settings('forge_project_settings_edit', patch=[dict(op='replace', path='/input', value=mapping)])
            baseline = read('forge_project_settings_read')
            assert baseline['dirty'] and baseline['draft']['input'] == mapping, changed
            rejected = mutate('forge_project_settings_edit', state=baseline['state'],
                              patch=[dict(op='replace', path='/input/actions/0/bindings/0/outer_deadzone', value=.9)])
            assert not rejected['ok'] and read('forge_project_settings_read')['state'] == baseline['state']
            edit_settings('forge_project_settings_save')
            stored = json.loads((root / 'Game/forge.project.json').read_text())
            assert stored['input'] == mapping
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_input_submit', events=[dict(control='pad.left_x', value=.33),
                                                       dict(control='pad.left_y', value=.44)])
            def analog():
                state = read('forge_play_input')['value']
                return next((row for row in state.get('actions', []) if row['id'] == action), {})
            until(analog, lambda row: abs(row.get('x', 0) - .15) < 1e-10 and abs(row.get('y', 0) - .2) < 1e-10)
            control('forge_play_input_submit', events=[dict(reset=True)])
            until(analog, lambda row: row.get('x') == row.get('y') == 0)
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert json.loads((root / 'Game/forge.project.json').read_text())['input'] == mapping
            print(('legacy' if legacy else 'current') + ': analog settings admission/save and actual Play/input/MCP observations passed')
            continue
        if SCOPE_TRACE_ONLY:
            assert 'forge_play_profile_trace' in names
            assert advertised['forge_play_profile_control']['inputSchema']['properties']['native_systems']['type'] == 'boolean'
            assert advertised['forge_play_profile_trace']['annotations']['readOnlyHint']
            cold = client.tool('forge_play_profile_trace', target=discovery()['result']['target'])
            assert not cold['ok'], cold
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            control('forge_play_profile_refresh')
            until(lambda: read('forge_play_profile')['value'], lambda p: p.get('native_systems_available'))
            control('forge_play_profile_control', enabled=True, clear=True, native_systems=True)
            for _ in range(24):
                control('forge_play_step')
            control('forge_play_profile_refresh')
            def profile():
                return read('forge_play_profile')['value']
            until(profile, lambda p: sum('start_seconds' in row for row in p.get('samples',[])) > 128)
            assert profile()['native_systems']
            exported = read('forge_play_profile_trace')
            trace = exported['trace']
            page = read('forge_play_profile_trace', limit=1, profile_state=exported['profile_state'])
            assert page['trace']['otherData']['exported_spans'] == 1
            assert page['trace']['otherData']['measured_spans'] == trace['otherData']['measured_spans']
            all_sequences = []
            total = trace['otherData']['measured_spans']
            assert trace['otherData']['exported_spans'] == 128
            for offset in range(0, total, 128):
                part = read('forge_play_profile_trace', offset=offset,
                            profile_state=exported['profile_state'])['trace']
                assert part['otherData']['origin_seconds'] == trace['otherData']['origin_seconds']
                all_sequences += [e['args']['sequence'] for e in part['traceEvents'] if e['ph'] == 'X']
            assert len(all_sequences) == len(set(all_sequences)) == total
            for fields in (dict(limit=0), dict(limit=129), dict(offset=513)):
                invalid = client.tool('forge_play_profile_trace', target=discovery()['result']['target'], **fields)
                assert not invalid['ok'], invalid
            bad = client.tool('forge_play_profile_trace', target=discovery()['result']['target'],
                              profile_state='0'*64,limit=1)
            assert not bad['ok'], bad
            events = [event for event in trace['traceEvents'] if event['ph'] == 'X']
            assert any(event['cat'] == 'ecs.system' for event in events)
            assert trace['otherData']['source']['native_systems_dropped'] == '0'
            assert events and min(event['ts'] for event in events) == 0
            assert all(event['ts'] >= 0 and event['dur'] >= 0 for event in events)
            assert trace['otherData']['owner_threads'] == 1
            assert trace['otherData']['source']['world_generation'] == str(exported['generation'])
            assert len(trace['otherData']['captured_at']) >= 20
            # Complete-event tracks may nest, but a crossing needs an explicitly
            # logical layout lane rather than inventing another measured thread.
            for left in events:
                for right in events:
                    if left is right or left['tid'] != right['tid']:
                        continue
                    a,b = left['ts'], left['ts']+left['dur']
                    c,d = right['ts'], right['ts']+right['dur']
                    assert b <= c or d <= a or a <= c <= d <= b or c <= a <= b <= d
            control('forge_play_profile_control', enabled=False, clear=False)
            retained = read('forge_play_profile_trace')['trace']
            assert retained['otherData']['measured_spans'] > 0
            control('forge_play_profile_control', enabled=False, clear=True)
            until(profile, lambda p: p.get('samples') == [])
            empty = read('forge_play_profile_trace')['trace']
            assert empty['otherData']['measured_spans'] == 0
            stale = client.tool('forge_play_profile_trace', target=discovery()['result']['target'],
                                profile_state=exported['profile_state'],limit=1)
            assert not stale['ok'], stale
            assert not any(event['ph'] == 'X' for event in empty['traceEvents'])
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            stopped = client.tool('forge_play_profile_trace', target=discovery()['result']['target'])
            assert not stopped['ok'], stopped
            print(('legacy' if legacy else 'current') + ': measured scope trace/read-only metadata/units/lanes/Stop/Clear/MCP passed')
            continue
        if PROFILE_ONLY:
            assert "native ECS" in advertised['forge_play_profile_refresh']['description']
            ok('forge_entity_create', arguments=dict(kind=0, name='Native profile cube'))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            scope = {k: paused[k] for k in ('launch', 'session', 'generation')}
            failed(mutate('forge_play_profile_control', **dict(scope, generation=scope['generation'] + 1),
                          enabled=True, clear=True), 'stale_play')
            stale = arguments(**scope, enabled=True, clear=True)
            stale['expected_revision'] += 1
            failed(client.tool('forge_play_profile_control', **stale), 'stale_revision')
            control('forge_play_profile_control', enabled=True, clear=True)
            def observe():
                control('forge_play_profile_refresh')
                until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
                return read('forge_play_profile')['value']
            fresh = observe()
            assert fresh['enabled'] and not fresh['ecs']['available'], fresh
            control('forge_play_step')
            stepped = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
                            row['timing']['tick'] == paused['timing']['tick'] + 1)
            profile = observe()
            ecs = profile['ecs']
            assert ecs['available'] and ecs['tick'] == stepped['timing']['tick'], profile
            assert ecs['timing'] == 'native_system_lifetime' and not ecs['truncated']
            assert any(row['kind'] == 'merge' for row in ecs['entries'])
            assert any(row.get('name', '').endswith('FinalTransforms') for row in ecs['entries'])
            assert len(ecs['entries']) <= 256 and profile['world_generation'] == stepped['generation']
            control('forge_play_profile_control', enabled=False, clear=False)
            control('forge_play_step')
            until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
                  row['timing']['tick'] == stepped['timing']['tick'] + 1)
            assert observe()['ecs'] == ecs
            control('forge_play_profile_control', enabled=False, clear=True)
            assert not observe()['ecs']['available']
            control('forge_play_stop')
            assert not read('forge_play_profile')['value']
            assert read('forge_scene_read') == authored
            with editor('read') as (_, restricted), Client(restricted, legacy) as reader:
                visible = {t['name'] for t in reader.request('tools/list')['result']['tools']}
                assert 'forge_play_profile' in visible
                assert 'forge_play_profile_control' not in visible
                assert 'forge_play_profile_refresh' not in visible
            print(('legacy' if legacy else 'current') + ': native ECS plan through actual MCP/Play; stale generation, Stop/Clear and authored isolation verified')
            continue
        if AUDIO_PAUSE_ONLY:
            actor = ok('forge_entity_create', arguments=dict(kind=0, name='Independent audio'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.audio_source'))
            clip = next(a for a in json.loads((root/'Game/forge.assets.json').read_text())['assets']
                        if a['source'] == 'Assets/FalloffTone.wav')['id']
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.audio_source',
                values=dict(clip=clip, spatialized=False, loop=True, play_on_start=True,
                            low_pass_enabled=True, low_pass_hz=800.,
                            high_pass_enabled=True, high_pass_hz=100.,
                            echo_enabled=True, echo_seconds=.25, echo_feedback=.35)))
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.audio_source',
                                                   field='pause_with_game', value=False))
            assert ok('forge_history_undo')['changed']
            authored = read('forge_scene_read')
            assert next(e for e in authored['entities'] if e['id'] == actor)['components']['forge.audio_source']['pause_with_game']
            ok('forge_document_save')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            control('forge_play_watch', entity=actor)
            observed = until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('available'))
            assert observed['source']['paused'] and observed['source']['pause_with_game'], observed
            tick = read('forge_play_status')['timing']['tick']
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='pause_with_game', value=False)
            observed = until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('pause_with_game') is False)
            assert not observed['source']['paused'], observed
            assert observed['source']['low_pass_enabled'] and observed['source']['high_pass_enabled'], observed
            assert observed['source']['echo_enabled'] and observed['source']['echo_seconds'] == .25, observed
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='echo_seconds', value=.5)
            until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('echo_seconds') == .5)
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='echo_feedback', value=.625)
            until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('echo_feedback') == .625)
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='low_pass_hz', value=200.)
            until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('low_pass_hz') == 200.)
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='high_pass_enabled', value=False)
            until(lambda: read('forge_play_audio'), lambda row: (row.get('source') or {}).get('high_pass_enabled') is False)
            control('forge_play_audio_control', entity=actor, operation='one_shot')
            until(lambda: read('forge_play_audio'), lambda row: row['value'].get('one_shots') == 1)
            control('forge_play_audio_control', entity=actor, operation='stop')
            until(lambda: read('forge_play_audio'), lambda row: row['value'].get('one_shots') == 0)
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='pause_with_game', value='false')
            rejected = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            assert not rejected['tune_ack']['ok'], rejected
            assert read('forge_play_audio')['source']['pause_with_game'] is False
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='low_pass_hz', value=20001.)
            rejected = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            assert not rejected['tune_ack']['ok'], rejected
            assert read('forge_play_audio')['source']['low_pass_hz'] == 200.
            control('forge_play_tune', entity=actor, component='forge.audio_source', property='echo_feedback', value=.76)
            rejected = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            assert not rejected['tune_ack']['ok'], rejected
            assert read('forge_play_audio')['source']['echo_feedback'] == .625
            assert read('forge_play_status')['timing']['tick'] == tick
            assert read('forge_scene_read') == authored
            control('forge_play_stop')
            until(lambda: read('forge_play_status'), lambda row: not row['active'])
            assert read('forge_scene_read') == authored
            continue

        if TIME_ONLY:
            assert "unscaled_time" in advertised["forge_play_status"]["description"]
            ok('forge_entity_create', arguments=dict(kind=0, name='Unscaled clock cube'))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            later = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
                          row['timing']['unscaled_time'] > paused['timing']['unscaled_time'] + .02)
            assert later['timing']['tick'] == paused['timing']['tick']
            assert later['timing']['unscaled_sequence'] > paused['timing']['unscaled_sequence']
            assert later['timing']['unscaled_delta'] > 0
            control('forge_play_time_scale', value=0)
            until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and row['timing']['time_scale'] == 0)
            control('forge_play_resume')
            held = until(lambda: read('forge_play_status'), lambda row: not row['paused'] and row['control_ready'])
            later = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
                          row['timing']['unscaled_time'] > held['timing']['unscaled_time'] + .02)
            assert later['timing']['tick'] == held['timing']['tick']
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            control('forge_play_step')
            stepped = until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and
                            row['timing']['tick'] == paused['timing']['tick'] + 1)
            assert stepped['timing']['time_scale'] == 0 and stepped['paused']
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            print(('legacy' if legacy else 'current') + ': paused/zero-rate unscaled clock, Step and authored immutability passed')
            continue
        if SPRITE_FRAMES_ONLY:
            component = next(c for c in discovery()['result']['schema']['components']
                             if c['id'] == 'forge.sprite_renderer')
            field = next(f for f in component['fields'] if f['id'] == 'frame_regions')
            assert field['type'] == 'vector' and field['maximum_count'] == 512
            assert field['default'] == [] and field['element']['type'] == 'struct'
            catalog = json.loads((root/'Game/forge.assets.json').read_text())
            material = next(row['id'] for row in catalog['assets']
                            if row['source'] == 'Assets/Sprites/SignalSheet.material.json')
            actor = ok('forge_entity_create', arguments=dict(name='MCP frame bank'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.sprite_renderer'))
            before = read('forge_scene_read')
            frames = [dict(x=1, y=1, width=13, height=14), dict(x=18, y=3, width=12, height=10),
                      dict(x=33, y=2, width=14, height=12)]
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.sprite_renderer',
                                                        values=dict(material=material, frame_regions=frames, frame=2)))
            authored = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == authored
            bad = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.sprite_renderer',
                                                               field='frame_regions', value=[dict(x=-1,y=0,width=1,height=1)]))
            assert not bad['ok'] and read('forge_scene_read') == authored
            ok('forge_document_save')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
            def runtime_sprite():
                return next(e['components']['forge.sprite_renderer']
                            for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
            assert runtime_sprite()['frame_regions'] == frames and runtime_sprite()['frame'] == 2
            live = [dict(row, x=row['x']+1) for row in frames]
            control('forge_play_tune', entity=actor, component='forge.sprite_renderer', property='frame_regions', value=live)
            ack = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
            assert 'error' not in ack['tune_ack'] and ack['timing']['tick'] == paused['timing']['tick'], ack
            assert runtime_sprite()['frame_regions'] == live
            control('forge_play_tune', entity=actor, component='forge.sprite_renderer', property='frame_regions',
                    value=[dict(x=0,y=0,width=0,height=1)])
            ack = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
            assert 'error' in ack['tune_ack'] and runtime_sprite()['frame_regions'] == live, ack
            wrong = {k: paused[k] for k in ('launch', 'session', 'generation')}
            wrong['generation'] += 1
            failed(mutate('forge_play_tune', **wrong, entity=actor, component='forge.sprite_renderer',
                          property='frame', value=0), 'stale_play')
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            print(('legacy' if legacy else 'current') + ': frame-bank native discovery, property/history/Save, Play/live rejection and authored isolation passed')
            continue
        if MAP_PROFILE_ONLY:
            catalog = json.loads((root/'Game/forge.assets.json').read_text())
            tile_map = next(row['id'] for row in catalog['assets']
                            if row['source'] == 'Assets/Tiles/PaintedPlatforms.tilemap.json')
            until(lambda: client.tool('forge_asset_read', target=discovery()['result']['target'],
                                      asset=tile_map), lambda value: value.get('ok', False))
            ok('forge_asset_place', asset=tile_map, position=[0, 0, 7], name='MCP trigger Map')
            placement = until(lambda: read('forge_asset_jobs')['placement'],
                              lambda row: row['state'] in ('succeeded', 'failed'))
            assert placement['state'] == 'succeeded', placement
            actor = placement['entity']
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.physics_body_2d'))
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.tile_map_collider_2d'))
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.tile_map_collider_2d',
                                                    field='layer', value=1))
            original = read('forge_scene_read')
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.physics_body_2d',
                                                        values=dict(motion=1, sensor=True)))
            authored = read('forge_scene_read')
            fields = next(e for e in authored['entities'] if e['id'] == actor)['components']['forge.physics_body_2d']
            assert fields['motion'] == 1 and fields['sensor'] is True
            ok('forge_history_undo')
            assert read('forge_scene_read') == original
            ok('forge_history_redo')
            assert read('forge_scene_read') == authored
            invalid = mutate('forge_property_set_many', arguments=dict(entity=actor,
                component='forge.physics_body_2d', values=dict(motion=3, sensor=False)))
            assert not invalid['ok'] and read('forge_scene_read') == authored, invalid
            stale = arguments(arguments=dict(entity=actor, component='forge.physics_body_2d',
                                             values=dict(motion=0, sensor=False)))
            stale['expected_revision'] += 1
            assert not client.tool('forge_property_set_many', **stale)['ok']
            assert read('forge_scene_read') == authored
            ok('forge_document_save')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            control('forge_play_step')
            until(lambda: read('forge_play_status'), lambda row: row['timing']['tick'] == paused['timing']['tick'] + 1 and row['control_ready'])
            running = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
            assert running['components']['forge.physics_body_2d']['sensor'] is True
            assert running['components']['forge.physics_body_2d']['motion'] == 1
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.physics_body_2d',
                                                        values=dict(motion=0, sensor=False, one_way=True)))
            one_way = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == one_way
            ok('forge_document_save')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            control('forge_play_step')
            running = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
            assert running['components']['forge.physics_body_2d']['one_way'] is True
            assert running['components']['forge.physics_body_2d']['motion'] == 0
            assert running['components']['forge.physics_body_2d']['sensor'] is False
            control('forge_play_stop')
            assert read('forge_scene_read') == one_way
            print(('legacy' if legacy else 'current') + ': native Map placement, Kinematic/sensor and static one-way, history/revision, Save and Play passed')
            continue
        overlay_source = read('forge_scene_read')
        overlay_settings = (root/'Game/forge.project.json').read_bytes()
        for visible in (False, True, False):
            changed = ok('forge_navigation_overlay', visible=visible)
            assert changed['overlay_visible'] is visible
            assert read('forge_navigation_status')['overlay_visible'] is visible
            assert read('forge_scene_read') == overlay_source
            assert (root/'Game/forge.project.json').read_bytes() == overlay_settings
        invalid_overlay = mutate('forge_navigation_overlay', visible='yes')
        assert not invalid_overlay['ok'] and not read('forge_navigation_status')['overlay_visible']
        wrong_revision = arguments(visible=True)
        wrong_revision['expected_revision'] += 1
        assert not client.tool('forge_navigation_overlay', **wrong_revision)['ok']
        replayed = arguments(visible=True)
        accepted_overlay = client.tool('forge_navigation_overlay', **replayed)
        assert accepted_overlay['ok']
        cached_overlay = client.tool('forge_navigation_overlay', **replayed)
        assert cached_overlay['ok'] and cached_overlay['result'] == accepted_overlay['result']
        failed(client.tool('forge_navigation_overlay', **dict(replayed, visible=False)), 'sequence_conflict')
        assert read('forge_navigation_status')['overlay_visible']
        ok('forge_navigation_overlay', visible=False)
        cached_overlay = client.tool('forge_navigation_overlay', **replayed)
        assert cached_overlay['ok'] and cached_overlay['result'] == accepted_overlay['result']
        assert not read('forge_navigation_status')['overlay_visible'], 'Old receipt reapplied a view change'
        if NAVIGATION_ONLY:
            floor, navigation = navigation_floor()
            asset = navigation['asset']
            index = root / 'Game/forge.assets.json'
            preserved = index.read_bytes()
            for component in ('forge.character_controller', 'forge.character_controller_2d'):
                ok('forge_component_add', arguments=dict(entity=floor, component=component))
                ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
                                                          cell_size=.2, cell_height=.1))
                rejected = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
                assert 'enabled character controller' in rejected['error'], rejected
                assert rejected['asset'] == asset and index.read_bytes() == preserved, rejected
                ok('forge_component_revert', arguments=dict(entity=floor, component=component))
            # Switch the same Scene's one logical asset to an explicit XY profile.
            # Existing 3D source geometry remains present and is not selected.
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.navigation_region_2d'))
            ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.local_scale',
                                                        values=dict(x=1, y=1, z=0)))
            ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.navigation_region_2d',
                                                        values=dict(width=20, height=20)))
            obstacle = ok('forge_entity_create', arguments=dict(name='XY obstacle'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=obstacle, component='forge.navigation_region_2d'))
            ok('forge_property_set_many', arguments=dict(entity=obstacle, component='forge.navigation_region_2d',
                                                        values=dict(obstacle=True, width=2, height=4)))
            def obstacle_width():
                row = next(row for row in read('forge_scene_read')['entities'] if row['id'] == obstacle)
                return row['components']['forge.navigation_region_2d']['width']
            ok('forge_property_set', arguments=dict(entity=obstacle, component='forge.navigation_region_2d',
                                                    field='width', value=3))
            assert obstacle_width() == 3
            ok('forge_history_undo')
            assert obstacle_width() == 2
            ok('forge_history_redo')
            assert obstacle_width() == 3
            rejected = mutate('forge_property_set', arguments=dict(entity=obstacle,
                              component='forge.navigation_region_2d', field='width', value=0))
            assert not rejected['ok'] and obstacle_width() == 3, rejected
            settings = dict(radius=.4, height=2, climb=.4, slope=45,
                            cell_size=.2, cell_height=.1, plane=1)
            ok('forge_navigation_build', settings=settings)
            planar = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert planar['asset'] == asset and not planar['error'] and planar['settings']['plane'] == 1, planar
            catalog = json.loads(index.read_text(encoding='utf-8'))
            records = catalog['assets']
            if isinstance(records, dict):
                records = records.values()
            selected = next(record for record in records if record['id'] == asset)
            assert selected['schema_version'] == 3 and selected['metadata']['geometry_version'] == 3, selected
            preserved = index.read_bytes()
            for invalid in (-1, 2, .5, 'xy'):
                rejected = mutate('forge_navigation_build', settings=dict(settings, plane=invalid))
                assert not rejected['ok'] and index.read_bytes() == preserved, rejected
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.character_controller_2d'))
            ok('forge_navigation_build', settings=settings)
            rejected = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert 'enabled character controller' in rejected['error'] and index.read_bytes() == preserved, rejected
            ok('forge_component_revert', arguments=dict(entity=floor, component='forge.character_controller_2d'))
            ok('forge_document_save')
            assert obstacle_width() == 3
            agent = ok('forge_entity_create', arguments=dict(name='Live XY agent'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=agent, component='forge.navigation_agent'))
            original_goal = read('forge_scene_read')
            goal = dict(destination_x=-7.5, destination_y=1.5, destination_z=123)
            ok('forge_property_set_many', arguments=dict(entity=agent, component='forge.navigation_agent', values=goal))
            edited_goal = read('forge_scene_read')
            values = next(row for row in edited_goal['entities'] if row['id'] == agent)['components']['forge.navigation_agent']
            assert all(values[key] == value for key, value in goal.items())
            ok('forge_history_undo')
            assert read('forge_scene_read') == original_goal
            ok('forge_history_redo')
            assert read('forge_scene_read') == edited_goal
            ok('forge_history_undo')
            ok('forge_property_set_many', arguments=dict(entity=agent, component='forge.local_translation',
                                                        values=dict(x=-8, y=1, z=37.125)))
            ok('forge_property_set_many', arguments=dict(entity=agent, component='forge.navigation_agent',
                                                        values=dict(navmesh=asset, has_destination=True, destination_x=8, destination_y=1)))
            edit_settings('forge_project_settings_game_defaults')
            edit_settings('forge_project_settings_save')
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
            def runtime_agent():
                return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == agent)
            position = runtime_agent()['world_affine']
            def tune_agent(field, value, accepted=True):
                control('forge_play_tune', entity=agent, component='forge.navigation_agent', property=field, value=value)
                result = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
                assert ('error' not in result['tune_ack']) is accepted, result
                assert result['timing']['tick'] == paused['timing']['tick'], result
                return result
            tune_agent('speed', 0)
            assert runtime_agent()['components']['forge.navigation_agent']['speed'] == 0
            assert runtime_agent()['world_affine'] == position
            for field, value in [('speed', -1), ('navmesh', None), ('enabled', 'yes'), ('destination_x', 4091)]:
                tune_agent(field, value, False)
            tune_agent('destination_x', 100)
            control('forge_play_step')
            paused = until(lambda: read('forge_play_status'), lambda r: r['timing']['tick'] == paused['timing']['tick'] + 1 and r['control_ready'])
            failure = runtime_agent()['navigation_debug']
            assert failure['phase'] == 'Failed' and failure['diagnostic'] and not failure['sampled'], failure
            tune_agent('destination_x', -8)
            pending = runtime_agent()['navigation_debug']
            assert pending['phase'] == 'Pending' and pending['diagnostic'] == '', pending
            tune_agent('speed', 3)
            control('forge_play_tuning_capture', entity=agent)
            wrong = {key: paused[key] for key in ('launch', 'session', 'generation')}
            wrong['generation'] += 1
            failed(mutate('forge_play_tune', **wrong, entity=agent, component='forge.navigation_agent', property='speed', value=4), 'stale_play')
            control('forge_play_step')
            until(lambda: read('forge_play_status'), lambda r: r['timing']['tick'] == paused['timing']['tick'] + 1 and r['control_ready'])
            assert read('forge_scene_read') == authored
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            review = read('forge_play_tuning_read')
            assert {field['property'] for field in review['fields']} == {'speed', 'destination_x'}, review
            ok('forge_play_tuning_apply', state=review['state'])
            kept = read('forge_scene_read')
            assert next(row for row in kept['entities'] if row['id'] == agent)['components']['forge.navigation_agent']['speed'] == 3
            ok('forge_history_undo')
            assert read('forge_scene_read') == authored
            ok('forge_history_redo')
            assert read('forge_scene_read') == kept
            ok('forge_document_save')
            with editor('read') as (_, read_config), Client(read_config, legacy) as restricted:
                denied_names = {tool['name'] for tool in restricted.request('tools/list')['result']['tools']}
                assert 'forge_play_tune' not in denied_names and 'forge_navigation_overlay' not in denied_names
                denied = restricted.request('tools/call', dict(name='forge_navigation_overlay', arguments=dict(visible=True)))
                assert 'error' in denied or denied['result']['isError'], denied
            print(('legacy' if legacy else 'current') + ': navigation ownership, XY profile, live agent validation, keep-values/history and execution grant verified')
            continue
        ok('forge_source_folder_create', path='Assets/Shaders')
        shader_file = 'Assets/Shaders/test.hlsl'
        assert text(shader_file)['state'] == 'missing'
        saved = write(shader_file, 'float4 main(float3 p:ATTRIB0):SV_POSITION{return float4(p,1);}\n')
        failed(mutate('forge_source_text_write', path=shader_file, state='missing', text='overwrite'), 'source_conflict')
        shader = ok('forge_source_shader_create', path='Assets/Test.shader.json',
                    source_root='Assets/Shaders', stages=[dict(stage='vertex', source='test.hlsl', entry='main')])
        descriptor = text('Assets/Test.shader.json')
        broken = json.loads(descriptor['text']); broken['asset_id'] = '11111111-1111-4111-8111-111111111111'
        failed(mutate('forge_source_text_write', path='Assets/Test.shader.json',
                      state=descriptor['state'], text=json.dumps(broken)), 'identity_changed')
        assert text('Assets/Test.shader.json')['state'] == descriptor['state']
        assert not mutate('forge_source_text_write', path='../outside.hlsl', state='missing', text='bad')['ok']
        assert not mutate('forge_source_text_write', path='Native/evil.cpp', state='missing', text='bad')['ok']
        alias = root / 'Game/Assets/Alias.hlsl'
        scene_path = root / 'Game' / read('forge_project_read')['scene_path']
        with test_symlink(alias, scene_path) as linked:
            if linked:
                before = scene_path.read_bytes()
                assert not mutate('forge_source_text_write', path='Assets/Alias.hlsl', state='missing', text='bad')['ok']
                assert scene_path.read_bytes() == before
        with test_symlink(root / 'Game/Shaders', root / 'Game/Assets', directory=True) as linked:
            if linked:
                assert not client.tool('forge_source_text_list',
                                       target=discovery()['result']['target'])['ok']

        hud = ok('forge_source_ui_example')
        hud_path = hud['path']
        style = ok('forge_source_ui_register', path=str(Path(hud_path).with_suffix('.rcss')).replace('\\', '/'))
        assert read('forge_source_text_list')['total'] >= 4
        # Review finite conditional content using the actual declarations owner.
        # Native browser discovery is asynchronous; a known rejected open is safe to retry.
        def open_review():
            value = mutate('forge_dependencies_open', asset=hud['asset'])
            if not value['ok']:
                assert value['error']['code'] == 'invalid_asset', value
            return value
        review = until(open_review, lambda r: r['ok'])['result']
        changed = ok('forge_dependencies_edit', state=review['state'],
                     dependencies=[dict(asset=style['asset'], type=style['type'], reason='Alternate HUD style')])
        failed(mutate('forge_dependencies_save', state=review['state']), 'stale_dependencies')
        ok('forge_dependencies_save', state=changed['state'])
        declarations = until(lambda: read('forge_dependencies_read'), lambda r: not r['busy'])
        assert not declarations['error'] and not declarations['dirty'], declarations
        assert declarations['edges'][0]['role'] == 'declared:Alternate HUD style'

        # Existing Flecs Script diagnostic world never edits the authored Scene.
        scene_before = read('forge_scene_read')
        script = ok('forge_script_register', path='Assets/Example.flecs')
        ok('forge_script_preview', state=script['state'])
        preview = until(lambda: read('forge_script_read'), lambda r: not r['busy'])
        assert not preview['error'] and preview['preview_matches_draft'], preview
        good_preview = read('forge_script_result')['text']
        assert good_preview
        changed = ok('forge_script_replace', state=preview['state'], text='Broken {{{ definitely_invalid\n')
        ok('forge_script_preview', state=changed['state'])
        invalid = until(lambda: read('forge_script_read'), lambda r: not r['busy'])
        assert invalid['error'] and read('forge_script_result')['text'] == good_preview
        assert read('forge_scene_read') == scene_before
        ok('forge_script_close', state=invalid['state'], discard=True)

        floor, navigation = navigation_floor()
        failed(mutate('forge_navigation_cancel', operation='11111111-1111-4111-8111-111111111111'), 'wrong_job')
        cube = ok('forge_entity_create', arguments=dict(kind=0, name='MCP cube'))['results'][0]['selected']
        ok('forge_component_add', arguments=dict(entity=cube, component='forge.navigation_agent'))
        ok('forge_property_set', arguments=dict(entity=cube, component='forge.navigation_agent',
                                               field='navmesh', value=navigation['asset']))
        ok('forge_component_add', arguments=dict(entity=cube, component='forge.navigation_avoidance'))
        invalid_target = ok('forge_entity_create', arguments=dict(name='Unconfigured avoidance'))['results'][0]['selected']
        ok('forge_component_add', arguments=dict(entity=invalid_target, component='forge.navigation_avoidance'))
        ui_entity = ok('forge_entity_create', arguments=dict(name='HUD'))['results'][0]['selected']
        ok('forge_component_add', arguments=dict(entity=ui_entity, component='forge.ui_document'))
        ok('forge_property_set', arguments=dict(entity=ui_entity, component='forge.ui_document',
                                               field='document', value=hud['asset']))
        edit_settings('forge_project_settings_game_defaults')
        edit_settings('forge_project_settings_save')
        ok('forge_document_save')
        authored_before = read('forge_scene_read')

        started = ok('forge_play_start')
        active = until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
        assert active['launch'] == started['launch']

        wrong = dict(launch=active['launch'], session=active['session'], generation=active['generation'] + 1)
        failed(mutate('forge_play_pause', **wrong), 'stale_play')
        (root / 'gesture').touch()
        failed(mutate('forge_play_pause', launch=active['launch'], session=active['session'], generation=active['generation']), 'play_busy')
        (root / 'gesture').unlink()
        assert any(row['id'] == cube for row in read('forge_play_snapshot')['entities'])
        ui = until(lambda: read('forge_play_ui'), lambda r: r['value'] and r['value'].get('documents'))
        instance = ui['value']['documents'][0]['instance']
        control('forge_play_ui_command', instance=instance, command='Pause')
        paused = until(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
        assert read('forge_play_ui')['ack']['ok']
        tick = paused['timing']['tick']
        control('forge_play_step')
        stepped = until(lambda: read('forge_play_status'), lambda r: r['timing']['tick'] == tick+1 and r['control_ready'])
        assert stepped['paused']
        control('forge_play_tune', entity=invalid_target, component='forge.navigation_avoidance', property='horizon', value=2.25)
        denied_tune = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
        assert not denied_tune['tune_ack']['ok'] and 'participant' in denied_tune['tune_ack']['error']
        control('forge_play_tune', entity=cube, component='forge.navigation_avoidance', property='horizon', value=2.25)
        tuned = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
        assert 'error' not in tuned['tune_ack'], tuned
        runtime_cube = next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == cube)
        assert runtime_cube['components']['forge.navigation_avoidance']['horizon'] == 2.25
        control('forge_play_tuning_capture', entity=cube)
        control('forge_play_input_submit', events=[dict(control='key.space', value=1), dict(reset=True)])
        control('forge_play_time_scale', value=.5)
        until(lambda: read('forge_play_status'), lambda r: r['control_ready'])
        control('forge_play_profile_refresh')
        assert read('forge_play_log')['launch'] == active['launch']
        old = {k: active[k] for k in ('launch', 'session', 'generation')}
        control('forge_play_stop')
        assert not read('forge_play_status')['active']
        assert read('forge_scene_read') == authored_before
        tuning = read('forge_play_tuning_read')
        assert tuning['pending'] and tuning['fields']
        ok('forge_play_tuning_apply', state=tuning['state'])
        changed_scene = read('forge_scene_read')
        assert changed_scene != authored_before
        ok('forge_history_undo')
        assert read('forge_scene_read') == authored_before
        ok('forge_play_start')
        until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
        failed(mutate('forge_play_stop', **old), 'stale_play')
        control('forge_play_stop')
        ok('forge_document_save')

        assert not mutate('forge_export_start', path='../Outside')['ok']
        alias = root / 'Output/Alias'
        with test_symlink(alias, root / 'Output', directory=True) as linked:
            if linked:
                assert not mutate('forge_export_start', path='Alias/Game')['ok']
        export = ok('forge_export_start', path='Game')
        completed = until(lambda: read('forge_export_status'), lambda r: not r['busy'], timeout=60)
        assert completed['output'] and not completed['error'], completed
        output = root / 'Output/Game'
        manifest = json.loads((output / 'forge.standalone.json').read_text())
        assert manifest['target'] == dict(platform='windows', backend='d3d12')
        selected_kit=json.loads((Path(KIT)/'forge.runtime-kit.json').read_text())
        assert manifest['engine'] == selected_kit['engine']  # Keep the authentic selected kit identity.
        assert (output / 'forge_game.exe').is_file()
        if os.name != 'nt':
            unsupported = mutate('forge_export_run', operation=export['operation'],
                                 verify_startup=True)
            assert not unsupported['ok'] and 'Windows / D3D12' in unsupported['error']['message']
            preserved = read('forge_export_status')
            assert not preserved['busy'] and preserved['operation'] == export['operation']
        failed(mutate('forge_export_start', path='Game'), 'output_exists')
        (root / 'Output/Unrelated').mkdir()
        (root / 'Output/Unrelated/user.txt').write_text('Preserve me')
        ok('forge_export_start', path='Unrelated', replace=True)
        rejected = until(lambda: read('forge_export_status'), lambda r: not r['busy'])
        assert rejected['error'] and (root / 'Output/Unrelated/user.txt').read_text() == 'Preserve me'
        assert (output / 'forge_game.exe').is_file()
        print(('legacy' if legacy else 'current') + ': actual native project, UI/shader/script sources, dependency '
              'publication, Play/UI commands, fixed stepping/live tuning/Undo and authentic Windows-kit export verified')

    for mode in ('read', 'output', 'execute'):
        with editor(mode) as (root, config), Client(config, legacy) as client:
            names = {t['name'] for t in client.request('tools/list')['result']['tools']}
            assert ('forge_play_start' in names) == (mode == 'execute')
            assert ('forge_export_start' in names) == (mode == 'output')
            assert 'forge_export_run' not in names
            assert ('forge_script_preview' in names) == (mode != 'read')

with gzip.open(sys.argv[6], 'wt', encoding='utf-8') as output:
    json.dump(CAPTURE, output, separators=(',', ':'))
print('Both MCP protocols: navigation ownership, XY authoring and live controls verified; no export or Windows execution' if NAVIGATION_ONLY else
      'Both MCP protocols and independent runtime/output grants verified; no graphical Windows execution claimed')
