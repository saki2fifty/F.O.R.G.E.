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


@contextmanager
def editor(mode='full'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-workflows-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), RUNTIME, KIT, NAV, mode],
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

        def edit_settings(tool, **extra):
            return ok(tool, state=read('forge_project_settings_read')['state'], **extra)

        def text(path): return read('forge_source_text_read', path=path)

        def write(path, value):
            return ok('forge_source_text_write', path=path, state=text(path)['state'], text=value)

        advertised = {t['name']: t for t in client.request('tools/list')['result']['tools']}
        names = set(advertised)
        assert advertised['forge_play_start']['annotations']['openWorldHint']
        assert advertised['forge_export_start']['annotations']['openWorldHint']
        assert not advertised['forge_source_text_write']['annotations']['openWorldHint']
        assert {'forge_play_start', 'forge_play_ui_command', 'forge_export_start',
                'forge_script_preview', 'forge_navigation_build', 'forge_dependencies_save'} <= names
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
        def control(tool, **extra):
            state = until(lambda: read('forge_play_status'), lambda r: r['control_ready'])
            return ok(tool, launch=state['launch'], session=state['session'], generation=state['generation'], **extra)

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
print('Both MCP protocols and independent runtime/output grants verified; no graphical Windows execution claimed')
