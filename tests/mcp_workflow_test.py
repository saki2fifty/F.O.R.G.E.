"""Native project/source/Play/UI/navigation/export owners through actual MCP."""
from contextlib import contextmanager
import gzip
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed, test_symlink, finish_host

TOOLS, HOST, RUNTIME, KIT, NAV = sys.argv[1:6]
NAVIGATION_ONLY = sys.argv[7:] == ['--navigation-only']
NAVIGATION_LINKS_ONLY = sys.argv[7:] == ['--navigation-links-only']
NAVIGATION_COLLECTION_ONLY = sys.argv[7:] == ['--navigation-collection-only']
SCOPE_TRACE_ONLY = sys.argv[7:] == ['--scope-trace-only']
MAP_PROFILE_ONLY = sys.argv[7:] == ['--map-profile-only']
SPRITE_FRAMES_ONLY = sys.argv[7:] == ['--sprite-frames-only']
PROBES_ONLY = sys.argv[7:] == ['--reflection-probes-only']
DECALS_ONLY = sys.argv[7:] == ['--decals-only']
SOFT_SHADOWS_ONLY = sys.argv[7:] == ['--soft-shadows-only']
TIME_ONLY = sys.argv[7:] == ['--time-only']
PROFILE_ONLY = sys.argv[7:] == ['--profile-only']
GESTURES_ONLY = sys.argv[7:] == ['--gestures-only']
TOUCH_ONLY = sys.argv[7:] == ['--touch-only']
ANALOG_ONLY = sys.argv[7:] == ['--analog-only']
AUDIO_PAUSE_ONLY = sys.argv[7:] == ['--audio-pause-only']
GAMEPADS_ONLY = sys.argv[7:] == ['--gamepads-only']
UI_ANIMATION_ONLY = sys.argv[7:] == ['--ui-animation-only']
UI_LOCALIZATION_ONLY = sys.argv[7:] == ['--ui-localization-only']
LIFETIMES_ONLY = len(sys.argv) == 9 and sys.argv[7] == '--state-lifetimes-only'
CONDITIONS_SDK = len(sys.argv) == 9 and sys.argv[7] == '--sdk-state-conditions-only'
CONDITIONS_ONLY = sys.argv[7:] == ['--state-conditions-only'] or CONDITIONS_SDK
APPLICATION_STATES_ONLY = sys.argv[7:] == ['--application-states-only'] or LIFETIMES_ONLY or CONDITIONS_ONLY


def state_project_template(root):
    """Keep actual Signal Lock assets/cache without unrelated conversion jobs."""
    gallery = Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'
    output = root/'State template'
    output.mkdir()
    catalog = json.loads((gallery/'forge.assets.json').read_text(encoding='utf-8'))
    catalog['assets'] = [row for row in catalog['assets'] if row['source'].startswith('Assets/UI/SignalLock/') or
                         row['source'] == 'Scenes/134-signal-lock.scene.json']
    assert len(catalog['assets']) == 4
    for row in catalog['assets']:
        relative = Path(row['source'])
        target = output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(gallery/relative, target)
        sidecar = Path(str(relative)+'.forge-import.json')
        if (gallery/sidecar).exists():
            shutil.copy2(gallery/sidecar, output/sidecar)
        imported = row.get('metadata', {}).get('forge.import')
        if imported:
            cache = Path('.forge/cache/derived')/imported['key']
            shutil.copytree(gallery/cache, output/cache)
    (output/'forge.assets.json').write_text(json.dumps(catalog), encoding='utf-8')
    settings = json.loads((gallery/'forge.project.json').read_text(encoding='utf-8'))
    scene = next(row for row in catalog['assets'] if row['type'] == 'scene')
    settings['startup_scene'] = dict(asset=scene['id'], source=scene['source'])
    if LIFETIMES_ONLY or CONDITIONS_SDK:
        module = Path(sys.argv[8]).resolve()
        fingerprint = json.loads(subprocess.check_output([RUNTIME, '--sdk-info'], text=True))['fingerprint']
        shutil.copy2(module, output/module.name)
        module_id = 'project.state_condition_probe' if CONDITIONS_SDK else 'project.runtime_lifetime_probe'
        settings['modules'] = [dict(id=module_id, implementation='1',
                                   sdk='experimental-1', fingerprint=fingerprint,
                                   library=module.name, dependencies=[])]
    (output/'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
    return output


def map_profile_project_template(root):
    """Keep the two authored Maps and their selected dependency closure."""
    gallery = Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'
    output = root/'Map template'
    output.mkdir()
    catalog = json.loads((gallery/'forge.assets.json').read_text(encoding='utf-8'))
    records = {row['id']: row for row in catalog['assets']}
    pending = [row['id'] for row in records.values() if row['source'] in (
        'Assets/Tiles/PaintedPlatforms.tilemap.json', 'Assets/Tiles/ContourTiles.tilemap.json')]
    selected = set()
    while pending:
        asset = pending.pop()
        if asset in selected or asset not in records:
            continue
        selected.add(asset)
        pending.extend(records[asset]['dependencies'])
    catalog['assets'] = [row for row in catalog['assets'] if row['id'] in selected]
    settings = json.loads((gallery/'forge.project.json').read_text(encoding='utf-8'))
    paths = {settings['startup_scene']['source']}
    for row in catalog['assets']:
        paths.add(row['source'])
        paths.update(edge['source'] for edge in row.get('source_dependencies', []))
        sidecar = row['source']+'.forge-import.json'
        if (gallery/sidecar).exists():
            paths.add(sidecar)
        imported = row.get('metadata', {}).get('forge.import')
        if imported:
            cache = Path('.forge/cache/derived')/imported['key']
            shutil.copytree(gallery/cache, output/cache, dirs_exist_ok=True)
    for relative in paths:
        target = output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(gallery/relative, target)
    (output/'forge.assets.json').write_text(json.dumps(catalog), encoding='utf-8')
    (output/'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
    return output


@contextmanager
def editor(mode='full'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-workflows-') as temporary:
        root = Path(temporary)
        # State-only checks need the real packaged font, but no Windows export kit.
        host_resources = str(Path(__file__).resolve().parents[1]) if APPLICATION_STATES_ONLY else KIT
        command = [HOST, str(root), RUNTIME, host_resources, NAV, mode]
        if APPLICATION_STATES_ONLY:
            command.append(str(state_project_template(root)))
        elif MAP_PROFILE_ONLY:
            command.append(str(map_profile_project_template(root)))
        elif AUDIO_PAUSE_ONLY or SPRITE_FRAMES_ONLY or UI_LOCALIZATION_ONLY:
            command.append(str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'))
        if AUDIO_PAUSE_ONLY or LIFETIMES_ONLY or CONDITIONS_SDK:
            command.append('--sdk')
        process = subprocess.Popen(command,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, json.loads(line)
        finally:
            finish_host(process, root)


def until(fn, predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = fn()
        if predicate(value):
            return value
        time.sleep(.025)
    raise AssertionError(('Native owner timed out', value))


for legacy in (False, True):
    with editor('application_states' if APPLICATION_STATES_ONLY else 'ui_localization' if UI_LOCALIZATION_ONLY else 'gamepads' if GAMEPADS_ONLY else 'full') as (root, config), Client(config, legacy) as client:
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
            if tool == 'forge_play_start':
                def attempt():
                    value = mutate(tool, **extra)
                    if not value['ok']:
                        failed(value, 'play_busy')
                    return value
                value = until(attempt, lambda row: row['ok'])
            else:
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
        if NAVIGATION_COLLECTION_ONLY:
            build_schema = advertised['forge_navigation_build']['inputSchema']['properties']['settings']['properties']
            assert build_schema['collect_static_colliders']['type'] == 'boolean', build_schema
            assert build_schema['collision_layers']['minimum'] == 0 and build_schema['collision_layers']['maximum'] == 4294967295, build_schema
            assert 'collect_static_colliders' in advertised['forge_navigation_build']['description']
            floor = ok('forge_entity_create', arguments=dict(kind=4, name='Walkable XY area'))['results'][0]['selected']
            body = ok('forge_entity_create', arguments=dict(kind=4, name='Unmarked physical obstacle'))['results'][0]['selected']
            for entity in (floor, body):
                ok('forge_property_set_many', arguments=dict(entity=entity, component='forge.local_translation', values=dict(x=0, y=0, z=0)))
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.navigation_region_2d'))
            ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.navigation_region_2d', values=dict(width=20, height=12)))
            ok('forge_component_add', arguments=dict(entity=body, component='forge.physics_body_2d'))
            ok('forge_component_add', arguments=dict(entity=body, component='forge.box_collider_2d'))
            ok('forge_property_set_many', arguments=dict(entity=body, component='forge.box_collider_2d', values=dict(x=2, y=4)))
            before = read('forge_scene_read')
            old = arguments(arguments=dict(entity=body, component='forge.box_collider_2d', field='x', value=3))
            ok('forge_property_set', arguments=old['arguments'])
            edited = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == edited
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
            stale = arguments(arguments=old['arguments'])
            stale['expected_revision'] = old['expected_revision']
            rejected = client.tool('forge_property_set', **stale)
            assert not rejected['ok'] and read('forge_scene_read') == before, rejected
            settings = dict(radius=.2, height=2, climb=.4, slope=45, cell_size=.2, cell_height=.1,
                            plane=1, dynamic_obstacles=True, profile='Collected',
                            collect_static_colliders=True, collision_layers=1)
            for field, value in [('collect_static_colliders', 1), ('collision_layers', -1),
                                 ('collision_layers', 4294967296), ('collision_layers', .5)]:
                invalid = dict(settings)
                invalid[field] = value
                result = mutate('forge_navigation_build', settings=invalid)
                assert not result['ok'] and not read('forge_navigation_status')['busy'], result
                assert read('forge_scene_read') == before
            manual_settings = dict(settings, profile='Marked', collect_static_colliders=False)
            ok('forge_navigation_build', settings=manual_settings)
            marked = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
            assert marked['asset'] and not marked['error'], marked
            ok('forge_navigation_build', settings=settings)
            collected = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
            assert collected['asset'] != marked['asset'] and not collected['error'], collected
            assert collected['settings']['collect_static_colliders'] and collected['settings']['collision_layers'] == 1, collected
            ok('forge_navigation_select_profile', profile='Marked')
            assert read('forge_navigation_status')['asset'] == marked['asset']
            ok('forge_navigation_select_profile', profile='Collected')
            actor = ok('forge_entity_create', arguments=dict(kind=4, name='Physical-route follower'))['results'][0]['selected']
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.local_translation', values=dict(x=-6, y=0, z=23)))
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.navigation_agent'))
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.navigation_agent', values=dict(navmesh=collected['asset'], has_destination=True,
                speed=0, stopping_distance=.05, destination_x=6, destination_y=0, destination_z=0)))
            edit_settings('forge_project_settings_game_defaults')
            edit_settings('forge_project_settings_save')
            ok('forge_document_save')
            saved = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            def follower():
                return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == actor)
            # A slow transport must not let the actor arrive before pause/Step.
            # Begin held, then observe the native route and one controlled tick.
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            control('forge_play_tune', entity=actor, component='forge.navigation_agent', property='speed', value=4)
            until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            first = follower()
            assert first['world_affine'][3] == -6 and first['world_affine'][7] == 0 and first['world_affine'][11] == 23, first
            control('forge_play_step')
            until(lambda: read('forge_play_status'), lambda row: row['timing']['tick'] == paused['timing']['tick'] + 1 and row['control_ready'])
            second = follower()
            distance = sum((second['world_affine'][index] - first['world_affine'][index]) ** 2 for index in (3, 7, 11)) ** .5
            assert 0 < distance <= 4 / 60 + .00001 and second['world_affine'][11] == 23, (first, second)
            path = second['navigation_debug']['path']
            assert any(abs(point[1]) > 2 for point in path) and all(point[2] == 23 for point in path), path
            control('forge_play_tune', entity=actor, component='forge.navigation_agent', property='speed', value=6)
            until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            control('forge_play_resume')
            arrived = until(follower, lambda row: row.get('navigation_debug', {}).get('phase') == 'Arrived')
            assert arrived['world_affine'][3] > 5.8 and arrived['world_affine'][11] == 23, arrived
            control('forge_play_stop')
            assert read('forge_scene_read') == saved
            with editor('read') as (_, restricted_config), Client(restricted_config, legacy) as restricted:
                restricted_names = {tool['name'] for tool in restricted.request('tools/list')['result']['tools']}
                assert 'forge_navigation_status' in restricted_names and 'forge_navigation_build' not in restricted_names
                access = restricted.tool('forge_discover')
                denied = restricted.request('tools/call', dict(name='forge_navigation_build', arguments=dict(target=access['result']['target'],
                    expected_revision=access['revision'], sequence=access['next_sequence'], settings=settings)))
                assert 'error' in denied or denied.get('result', {}).get('isError'), denied
            continue
        if NAVIGATION_LINKS_ONLY:
            link_schema = next(c for c in discovery()['result']['schema']['components']
                               if c['id'] == 'forge.navigation_link')
            fields = {f['id']: f for f in link_schema['fields']}
            assert set(fields) == {'enabled', 'bidirectional', 'plane', 'profile', 'start_x',
                                   'start_y', 'start_z', 'end_x', 'end_y', 'end_z', 'radius'}, fields
            assert [(c['value'], c['label']) for c in fields['plane']['choices']] == [(0, '3D'), (1, '2D (XY)')]
            assert not any(f['animatable'] for f in fields.values())
            assert 'Navigation Link' in advertised['forge_navigation_build']['description']
            for x in (-3, 3):
                floor = ok('forge_entity_create', arguments=dict(kind=4, name='Linked island'))['results'][0]['selected']
                ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.local_translation', values=dict(x=x, y=0, z=0)))
                ok('forge_component_add', arguments=dict(entity=floor, component='forge.navigation_region_2d'))
                ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.navigation_region_2d', values=dict(width=4, height=6)))
            link = ok('forge_entity_create', arguments=dict(kind=4, name='Authored connection'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=link, component='forge.navigation_link'))
            before = read('forge_scene_read')
            old_request = arguments(arguments=dict(entity=link, component='forge.navigation_link', field='radius', value=.6))
            ok('forge_property_set_many', arguments=dict(entity=link, component='forge.navigation_link',
                values=dict(plane=1, profile='Bridge', start_x=-2, end_x=2, bidirectional=False)))
            authored_link = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == authored_link
            stale_request = arguments(arguments=old_request['arguments'])
            stale_request['expected_revision'] = old_request['expected_revision']
            stale = client.tool('forge_property_set', **stale_request)
            assert not stale['ok'] and read('forge_scene_read') == authored_link, stale
            for field, value in [('plane', 2), ('radius', 0), ('profile', '../Bridge'), ('end_x', -2), ('start_z', 1)]:
                rejected = mutate('forge_property_set', arguments=dict(entity=link, component='forge.navigation_link', field=field, value=value))
                assert not rejected['ok'] and read('forge_scene_read') == authored_link, rejected
            settings = dict(radius=.4, height=2, climb=.4, slope=45, cell_size=.2, cell_height=.1,
                            plane=1, dynamic_obstacles=True, profile='Other')
            ok('forge_navigation_build', settings=settings)
            other = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert other['asset'] and not other['error'] and other['link_count'] == 0, other
            ok('forge_navigation_build', settings=dict(settings, profile='Bridge'))
            linked = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert linked['asset'] != other['asset'] and not linked['error'] and linked['link_count'] == 1, linked
            actor = ok('forge_entity_create', arguments=dict(kind=4, name='MCP link traveler'))['results'][0]['selected']
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.local_translation', values=dict(x=-2, y=0, z=7)))
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.navigation_agent'))
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.navigation_agent',
                values=dict(navmesh=linked['asset'], has_destination=True, speed=.2, stopping_distance=.05,
                            destination_x=3, destination_y=0, destination_z=0)))
            edit_settings('forge_project_settings_game_defaults')
            edit_settings('forge_project_settings_save')
            ok('forge_document_save')
            saved = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
            def traveler():
                return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == actor)
            until(traveler, lambda row: row.get('navigation_debug', {}).get('link') is not None)
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
            before = traveler()
            assert before['navigation_debug']['link']['source'] == link, before
            control('forge_play_step')
            stepped = until(lambda: read('forge_play_status'), lambda r: r['timing']['tick'] == paused['timing']['tick'] + 1 and r['control_ready'])
            after = traveler()
            assert after['world_affine'][3] > before['world_affine'][3] and after['world_affine'][11] == 7, after
            control('forge_play_tune', entity=actor, component='forge.navigation_agent', property='speed', value=4)
            until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
            control('forge_play_resume')
            arrived = until(traveler, lambda row: row.get('navigation_debug', {}).get('phase') == 'Arrived')
            assert arrived['navigation_debug']['link'] is None and arrived['world_affine'][3] > 2.8, arrived
            control('forge_play_stop')
            assert read('forge_scene_read') == saved
            continue
        if CONDITIONS_ONLY:
            assert 'State Condition' in advertised['forge_play_tune']['description']
            actor = ok('forge_entity_create', arguments=dict(kind=0, name='MCP conditional body'))['results'][0]['selected']
            for component in ('forge.physics_body_2d', 'forge.box_collider_2d', 'forge.application_state_gate'):
                ok('forge_component_add', arguments=dict(entity=actor, component=component))
            before_condition = read('forge_scene_read')
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.application_state_gate',
                                                        values=dict(enabled=True, domain='app', state='playing')))
            with_condition = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before_condition
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == with_condition
            bad = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.application_state_gate',
                                                              field='domain', value='Bad Key'))
            assert not bad['ok'] and read('forge_scene_read') == with_condition
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            def conditional_actor():
                return next(row for row in read('forge_play_snapshot')['entities'] if row['id'] == actor)
            inactive = until(conditional_actor, lambda row: row.get('state_active') is False)
            assert inactive['components']['forge.physics_body_2d']['enabled']
            assert read('forge_play_physics')['value']['bodies_2d'] == 0
            control('forge_play_application_state_request', domain='app', state='playing', domain_revision=1)
            until(conditional_actor, lambda row: row.get('state_active', True))
            control('forge_play_step')
            until(lambda: read('forge_play_physics'), lambda row: row['value']['bodies_2d'] == 1)
            if CONDITIONS_SDK:
                until(lambda: read('forge_play_diagnostics'), lambda row:
                      'SDK State Condition playing value verified' in json.dumps(row['value']))
            paused = read('forge_play_status')
            tick = paused['timing']['tick']
            control('forge_play_tune', entity=actor, component='forge.application_state_gate', property='state', value='missing')
            rejected = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            assert not rejected['tune_ack']['ok'] and conditional_actor()['components']['forge.application_state_gate']['state'] == 'playing'
            assert rejected['timing']['tick'] == tick
            control('forge_play_tune', entity=actor, component='forge.application_state_gate', property='state', value='menu')
            tuned = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
            assert 'error' not in tuned['tune_ack'] and tuned['timing']['tick'] == tick
            until(conditional_actor, lambda row: row.get('state_active') is False)
            assert read('forge_play_physics')['value']['bodies_2d'] == 0
            if CONDITIONS_SDK:
                control('forge_play_step')
                until(lambda: read('forge_play_diagnostics'), lambda row:
                      'SDK State Condition inactive value verified' in json.dumps(row['value']))
                assert read('forge_play_physics')['value']['bodies_2d'] == 0
            control('forge_play_tune', entity=actor, component='forge.application_state_gate', property='state', value='playing')
            until(conditional_actor, lambda row: row.get('state_active', True))
            control('forge_play_step')
            until(lambda: read('forge_play_physics'), lambda row: row['value']['bodies_2d'] == 1)
            control('forge_play_application_state_request', domain='app', state='menu', domain_revision=2)
            until(conditional_actor, lambda row: row.get('state_active') is False)
            assert read('forge_play_physics')['value']['bodies_2d'] == 0
            assert read('forge_scene_read') == authored
            scope = {key: paused[key] for key in ('launch', 'session', 'generation')}
            control('forge_play_stop')
            ok('forge_play_start')
            control('forge_play_pause')
            failed(mutate('forge_play_tune', **scope, entity=actor, component='forge.application_state_gate',
                          property='enabled', value=False), 'stale_play')
            assert conditional_actor()['components']['forge.application_state_gate']['state'] == 'playing'
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            with editor('read') as (_, read_config), Client(read_config, legacy) as readonly:
                readable = {tool['name'] for tool in readonly.request('tools/list')['result']['tools']}
                assert 'forge_play_tune' not in readable
                denied = readonly.request('tools/call', dict(name='forge_play_tune', arguments={}))
                assert denied['error']['code'] == -32602, denied
            print(('legacy' if legacy else 'current') + ': State Condition authoring/history, live semantic '
                  'validation, native2D admission/paused retirement, restart/stale/read-only scopes PASS')
            continue
        if APPLICATION_STATES_ONLY:
            assert 'forge_play_application_states' in names
            assert advertised['forge_play_application_state_request']['annotations']['openWorldHint']
            assert advertised['forge_play_application_state_scope']['annotations']['openWorldHint']
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            live = until(lambda: read('forge_play_application_states'),
                         lambda row: row['control_ready'] and row['value'] is not None)
            tick = live['timing']['tick']
            app, hud = live['value']['domains'][:2]
            assert app['active'] == 'menu' and hud['active'] == 'hidden'
            assert app['states'][0]['key'] == 'menu' and app['revision'] == 1
            scope = {key: live[key] for key in ('launch', 'session', 'generation')}
            failed(mutate('forge_play_application_state_request', **dict(scope, generation=scope['generation']+1),
                          domain='app', state='playing', domain_revision=1), 'stale_play')
            control('forge_play_application_state_request', domain='app', state='playing', domain_revision=1)
            live = until(lambda: read('forge_play_application_states'),
                         lambda row: not row['application_state_pending'] and row['value']['domains'][0]['active'] == 'playing')
            assert live['value']['domains'][1]['active'] == 'visible'
            if LIFETIMES_ONLY:
                # Actual module creation crosses two fixed publication boundaries.
                for _ in range(2):
                    control('forge_play_step')
                snapshot = until(lambda: read('forge_play_snapshot'),
                                 lambda row: any(e['name'] == 'MCP scoped runtime root' for e in row['entities']))
                actor = next(e for e in snapshot['entities'] if e['name'] == 'MCP scoped runtime root')
                assert read('forge_play_physics')['value']['bodies_2d'] == 1
                reference = dict(scene=snapshot['asset_id'], entity=actor['id'])
                # An authored controller cannot receive the same runtime-only lifetime.
                authored_entity = next(e for e in snapshot['entities'] if e['name'] == 'Modes')
                control('forge_play_application_state_scope',
                        entity=dict(scene=snapshot['asset_id'], entity=authored_entity['id']),
                        domain='app', state='game', domain_revision=2)
                until(lambda: read('forge_play_application_states'),
                      lambda row: not row['application_state_pending'] and bool(row['application_state_error']))
                control('forge_play_application_state_scope', entity=reference,
                        domain='app', state='game', domain_revision=1)
                until(lambda: read('forge_play_application_states'),
                      lambda row: not row['application_state_pending'] and bool(row['application_state_error']))
                control('forge_play_application_state_scope', entity=reference,
                        domain='app', state='game', domain_revision=2)
                live = until(lambda: read('forge_play_application_states'),
                             lambda row: not row['application_state_pending'])
                assert not live['application_state_error']
                tick = live['timing']['tick']
            published = live['value']
            for domain, state, revision in [('app', 'menu', 1), ('hud', 'hidden', published['domains'][1]['revision']),
                                             ('app', 'missing', 2)]:
                control('forge_play_application_state_request', domain=domain, state=state, domain_revision=revision)
                rejected = until(lambda: read('forge_play_application_states'),
                                 lambda row: not row['application_state_pending'] and bool(row['application_state_error']))
                assert rejected['active'] and rejected['value'] == published
                assert rejected['timing']['tick'] == tick and rejected['paused']
            (root/'gesture').touch()
            failed(mutate('forge_play_application_state_request', **scope,
                          domain='app', state='menu', domain_revision=2), 'play_busy')
            (root/'gesture').unlink()
            control('forge_play_application_state_request', domain='app', state='menu', domain_revision=2)
            live = until(lambda: read('forge_play_application_states'),
                         lambda row: not row['application_state_pending'] and row['value']['domains'][0]['active'] == 'menu')
            assert not live['application_state_error'] and live['timing']['tick'] == tick
            if LIFETIMES_ONLY:
                snapshot = read('forge_play_snapshot')
                assert all(e['id'] != reference['entity'] for e in snapshot['entities'])
                assert any(e['name'] == 'Modes' for e in snapshot['entities'])
                assert read('forge_play_physics')['value']['bodies_2d'] == 0
                assert read('forge_scene_read') == authored
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert read('forge_play_application_states')['value'] is None
            ok('forge_play_start')
            control('forge_play_pause')
            live = read('forge_play_application_states')
            assert live['value']['domains'][0]['revision'] == 1
            failed(mutate('forge_play_application_state_request', **scope,
                          domain='app', state='playing', domain_revision=1), 'stale_play')
            control('forge_play_stop')
            with editor('read') as (_, read_config), Client(read_config, legacy) as readonly:
                readable = {tool['name'] for tool in readonly.request('tools/list')['result']['tools']}
                assert 'forge_play_application_states' in readable
                assert 'forge_play_application_state_request' not in readable
                assert 'forge_play_application_state_scope' not in readable
                denied = readonly.request('tools/call', dict(name='forge_play_application_state_request', arguments={}))
                assert denied['error']['code'] == -32602, denied
                denied = readonly.request('tools/call', dict(name='forge_play_application_state_scope', arguments={}))
                assert denied['error']['code'] == -32602, denied
            ok('forge_document_open', path='Scenes/134-signal-lock.scene.json', discard=True)
            puzzle = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            ui = until(lambda: read('forge_play_ui'), lambda row: row.get('presentation_application_states') is not None)
            tick = ui['timing']['tick']
            instance = ui['value']['documents'][0]['instance']
            def domain(rows, key):
                return next(row for row in rows if row['key'] == key)
            for key, choice in [('app', 'playing'), ('route', 'right'), ('signal', 'cyan')]:
                latest = read('forge_play_ui')
                revision = domain(latest['presentation_application_states']['domains'], key)['revision']
                control('forge_play_ui_command', instance=instance, command='forge.state', value=f'{key}:{choice}:{revision}')
                ui = until(lambda: read('forge_play_ui'), lambda row: row['ack'] and row['ack']['ok'] and
                           domain(row['presentation_application_states']['domains'], key)['active'] == choice)
                assert ui['timing']['tick'] == tick and ui['paused']
            assert domain(ui['presentation_application_states']['domains'], 'lock')['active'] == 'open'
            current = ui['presentation_application_states']
            control('forge_play_ui_command', instance=instance, command='forge.state', value='signal:amber:1')
            ui = until(lambda: read('forge_play_ui'), lambda row: row['ack'] and not row['ack']['ok'])
            assert ui['active'] and ui['presentation_application_states'] == current
            control('forge_play_stop')
            assert read('forge_scene_read') == puzzle
            print(('legacy' if legacy else 'current') + ': actual Play state discovery, paused transitions, computed/stale rejection, scope and authored isolation passed')
            continue
        if UI_LOCALIZATION_ONLY:
            ok('forge_document_open', path='Scenes/133-ui-localization.scene.json', discard=True)
            authored = read('forge_scene_read')
            actor = next(e['id'] for e in authored['entities'] if 'forge.ui_localization' in e['components'])
            ok('forge_play_start')
            control('forge_play_pause')
            live = until(lambda: read('forge_play_ui'),
                         lambda row: (row.get('presentation_localization') or {}).get('selected_locale') == 'en')
            assert live['value']['localization']['locale'] == 'en'
            control('forge_play_watch', entity=actor)
            instance = live['value']['documents'][0]['instance']
            tick = read('forge_play_status')['timing']['tick']
            control('forge_play_ui_command', instance=instance, command='forge.locale', value='fr-CA')
            live = until(lambda: read('forge_play_ui'),
                         lambda row: (row.get('presentation_localization') or {}).get('requested_locale') == 'fr-CA')
            assert live['presentation_localization']['selected_locale'] == 'fr'
            assert live['presentation_localization']['fallback_messages'] == 1
            assert live['value']['documents'][0]['instance'] == instance
            control('forge_play_tune', entity=actor, component='forge.ui_localization', property='locale', value='de')
            live = until(lambda: read('forge_play_ui'),
                         lambda row: (row.get('presentation_localization') or {}).get('requested_locale') == 'de')
            assert live['presentation_localization']['selected_locale'] == 'en'
            assert live['presentation_localization']['locale_fallback']
            assert read('forge_play_status')['timing']['tick'] == tick
            state = read('forge_play_status')
            failed(mutate('forge_play_ui_command', launch=state['launch'], session=state['session'],
                          generation=state['generation']+1, instance=instance,
                          command='forge.locale', value='es'), 'stale_play')
            control('forge_play_ui_command', instance=instance, command='forge.locale', value='x')
            until(lambda: read('forge_play_ui'), lambda row: row['ack'] and not row['ack']['ok'])
            assert read('forge_play_ui')['presentation_localization']['requested_locale'] == 'de'
            current = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
            assert current['components']['forge.ui_localization']['locale'] == 'de'
            review = control('forge_play_tuning_capture', entity=actor)
            assert len(review['fields']) == 1 and review['fields'][0]['component'] == 'forge.ui_localization'
            assert review['fields'][0]['property'] == 'locale' and review['fields'][0]['after'] == 'de'
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert read('forge_play_ui')['presentation_localization'] is None
            review = read('forge_play_tuning_read')
            ok('forge_play_tuning_apply', state=review['state'])
            kept = next(e for e in read('forge_scene_read')['entities'] if e['id'] == actor)
            assert kept['components']['forge.ui_localization']['locale'] == 'de'
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == authored
            continue
        if UI_ANIMATION_ONLY:
            assert 'forge_play_ui_reload' in names
            hud = ok('forge_source_ui_example')
            path = str(Path(hud['path']).with_suffix('.rcss')).replace('\\', '/')
            saved = text(path)
            animated = saved['text'] + '\n@keyframes breathe { from {opacity:0.3;} to {opacity:1;} }\n#panel {animation:1s linear-in infinite alternate breathe;}\n'
            stale = arguments(path=path, state=saved['state'], text=animated)
            write(path, animated)
            assert client.tool('forge_source_text_write', **stale)['ok']  # Exact receipt replay.
            outdated = arguments(path=path, state=saved['state'], text=animated)
            outdated['expected_revision'] -= 1
            failed(client.tool('forge_source_text_write', **outdated), 'stale_revision')
            assert text(path)['text'] == animated
            entity = ok('forge_entity_create', arguments=dict(name='Animated HUD'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=entity, component='forge.ui_document'))
            ok('forge_property_set', arguments=dict(entity=entity, component='forge.ui_document', field='document', value=hud['asset']))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_pause')
            live = until(lambda: read('forge_play_ui'), lambda r: r['value'] and r['value'].get('documents'))
            assert live['presentation_localization'] is None  # This legacy HUD has no catalog.
            instance = live['value']['documents'][0]['instance']
            before = read('forge_play_status')
            wrong = dict(launch=before['launch'], session=before['session'], generation=before['generation']+1)
            failed(mutate('forge_play_ui_reload', **wrong), 'stale_play')
            assert control('forge_play_ui_reload')['reloaded']
            failed(mutate('forge_source_text_write', path=path, state=text(path)['state'], text=animated), 'busy')
            # Simulate an external editor saving a bounded but semantically invalid
            # stylesheet. MCP keeps the same Edit-only source-write policy.
            (root/'Game'/path).write_text(animated + '\n#panel {transition:opacity 0.4s linear;}', encoding='utf-8')
            state = read('forge_play_status')
            failed(mutate('forge_play_ui_reload', launch=state['launch'], session=state['session'], generation=state['generation']), 'ui_reload_failed')
            assert read('forge_play_status')['timing']['tick'] == before['timing']['tick']
            control('forge_play_ui_command', instance=instance, command='Step')
            until(lambda: read('forge_play_status'), lambda r: r['timing']['tick'] == before['timing']['tick']+1 and r['control_ready'])
            (root/'Game'/path).write_text(animated, encoding='utf-8')
            assert control('forge_play_ui_reload')['reloaded']
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            with editor('read') as (_, config_read), Client(config_read, legacy) as restricted:
                denied = {tool['name'] for tool in restricted.request('tools/list')['result']['tools']}
                assert 'forge_play_ui_reload' not in denied
                result = restricted.request('tools/call', dict(name='forge_play_ui_reload', arguments={}))
                assert 'error' in result or result['result']['isError'], result
            print(('legacy' if legacy else 'current') + ': animated source edits, revisions, native UI reload/last-good commands and execution grant passed')
            continue
        if GAMEPADS_ONLY:
            assert {'forge_play_gamepads', 'forge_play_gamepads_assign',
                    'forge_play_gamepads_motion', 'forge_play_gamepads_sensor'} <= names
            devices = until(lambda: read('forge_play_gamepads'), lambda d: d['available'] and len(d['devices']) == 5)
            assert [d['slot'] for d in devices['devices']] == [1,2,3,4,0]
            motion = read('forge_play_gamepads_motion')
            assert motion['revision'] == devices['revision'] and not motion['routed']
            assert motion['devices'][0]['gyroscope']['supported'] and not motion['devices'][4]['gyroscope']['supported']
            assert not any(d['gyroscope']['enabled'] or d['gyroscope']['measured'] for d in motion['devices'])
            sensor = dict(revision=motion['revision'],device=motion['devices'][0]['device'],sensor='gyroscope',enabled=True)
            (root/'gesture').write_text('Native UI gesture')
            failed(mutate('forge_play_gamepads_sensor',**sensor),'play_busy')
            (root/'gesture').unlink()
            command = arguments(**sensor)
            enabled = client.tool('forge_play_gamepads_sensor',**command)
            assert enabled['ok'] and enabled['result']['devices'][0]['gyroscope']['enabled'],enabled
            replay = client.tool('forge_play_gamepads_sensor',**command)
            assert replay['ok'] and replay['result'] == enabled['result']
            assert enabled['result']['revision'] == motion['revision']+1 and not enabled['result']['routed']
            assert not mutate('forge_play_gamepads_sensor',**sensor)['ok']
            for bad in (dict(sensor='compass'), dict(enabled=1), dict(device='foreign:connection')):
                invalid = {**sensor, 'revision':enabled['result']['revision'], **bad}
                assert not mutate('forge_play_gamepads_sensor',**invalid)['ok']
            assert not mutate('forge_play_gamepads_sensor',revision=enabled['result']['revision'],
                              device=motion['devices'][4]['device'],sensor='gyroscope',enabled=True)['ok']
            assert read('forge_play_gamepads_motion') == enabled['result']
            ok('forge_play_gamepads_sensor',revision=enabled['result']['revision'],device=sensor['device'],
               sensor='gyroscope',enabled=False)
            devices = read('forge_play_gamepads')
            controls = {row['id']:row for row in read('forge_play_controls')['controls']}
            assert all(controls[f'pad.{p}.south']['digital'] and
                       controls[f'pad.{p}.left_trigger']['minimum'] == 0 for p in range(1,5))
            assert 'pad.5.south' not in controls and 'pad.south' in controls
            actions = [dict(id=f'16700000-1670-4167-8167-{i+10:012}',name=f'Player {i+1}',
                       kind='digital',bindings=[dict(control=f'pad.{i+1}.south')]) for i in range(4)]
            actions.append(dict(id='16700000-1670-4167-8167-000000000014', name='Keyboard',
                                kind='digital', bindings=[dict(control='key.space')]))
            mapping = dict(version=1, actions=actions)
            edit_settings('forge_project_settings_edit', patch=[dict(op='replace',path='/input',value=mapping)])
            edit_settings('forge_project_settings_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            control('forge_play_input_submit',events=[dict(control=a['bindings'][0]['control'],value=1) for a in actions])
            def held():
                rows={r['id']:r for r in read('forge_play_input')['value'].get('actions',[])}
                return [rows.get(a['id'],{}).get('held') for a in actions]
            until(held,lambda states: states == [True]*5)
            edit = dict(revision=devices['revision'],device=devices['devices'][0]['device'],slot=2)
            (root/'gesture').write_text('Native UI gesture')
            failed(mutate('forge_play_gamepads_assign',**edit),'play_busy')
            (root/'gesture').unlink()
            stale = arguments(**edit)
            stale['expected_revision'] += 1
            assert not client.tool('forge_play_gamepads_assign',**stale)['ok']
            assert read('forge_play_gamepads') == devices
            assignment = arguments(**edit)
            response = client.tool('forge_play_gamepads_assign',**assignment)
            assert response['ok'],response
            assigned = response['result']
            replay = client.tool('forge_play_gamepads_assign',**assignment)
            assert replay['ok'] and replay['result'] == assigned
            assert [d['slot'] for d in assigned['devices']] == [2,0,3,4,0]
            until(held,lambda states: states == [False,False,True,True,True])
            assert not mutate('forge_play_gamepads_assign',**edit)['ok']
            assert read('forge_play_gamepads') == assigned
            for value in (-1,5,True,1.5):
                assert not mutate('forge_play_gamepads_assign',revision=assigned['revision'],
                                  device=assigned['devices'][2]['device'],slot=value)['ok']
            assert not mutate('forge_play_gamepads_assign',revision=assigned['revision'],
                              device='foreign-host:connection',slot=3)['ok']
            assert read('forge_play_gamepads') == assigned
            ok('forge_play_gamepads_assign',revision=assigned['revision'],device=assigned['devices'][3]['device'],slot=0)
            until(held,lambda states: states == [False,False,True,False,True])
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            assert json.loads((root/'Game/forge.project.json').read_text())['input'] == mapping
            for grant in ('read','execute'):
                with editor(grant) as (_, limited_config), Client(limited_config, legacy) as limited:
                    granted = {t['name'] for t in limited.request('tools/list')['result']['tools']}
                    assert 'forge_play_gamepads' in granted
                    assert 'forge_play_gamepads_motion' in granted
                    assert ('forge_play_gamepads_sensor' in granted) == (grant == 'execute')
                    assert ('forge_play_gamepads_assign' in granted) == (grant == 'execute')
            print(('legacy' if legacy else 'current')+': real virtual-device assignment/scoped neutralization/stale and invalid requests passed')
            continue
        if GESTURES_ONLY:
            assert 'log-scale' in advertised['forge_play_controls']['description']
            controls = {row['id']: row for row in read('forge_play_controls')['controls']}
            assert controls['touch.gesture.active']['digital']
            assert all(controls['touch.gesture.'+f]['relative'] for f in ('pan_x','pan_y','pinch','rotation'))
            pulses = ('tap','double_tap','long_press','swipe_left','swipe_right','swipe_up','swipe_down')
            assert all(controls['touch.gesture.'+f]['digital'] and not controls['touch.gesture.'+f]['relative'] for f in pulses)
            fields = ('active','pan_x','pan_y','pinch','rotation') + pulses
            actions = [dict(id=f'16000000-1600-4160-8160-{i+100:012}', name='Gesture '+field,
                            kind='axis1' if field in ('pan_x','pan_y','pinch','rotation') else 'digital',
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
                for f,v in [('active',1),('pan_x',-.25),('pan_y',.125),('pinch',.5),('rotation',-.75),('active',0)] + [(p,v) for p in pulses for v in (1,0)]])
            control('forge_play_step')
            def values():
                return {r['id']:r for r in read('forge_play_input')['value'].get('actions',[])}
            observed = until(values, lambda r: r.get(actions[0]['id'],{}).get('presses',0) == 1)
            assert observed[actions[0]['id']]['releases'] == 1 and not observed[actions[0]['id']]['held']
            for action in actions[5:]:
                assert observed[action['id']]['presses'] == 1 and observed[action['id']]['releases'] == 1 and not observed[action['id']]['held']
            for action,expected in zip(actions[1:5],(-.25,.125,.5,-.75)):
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

        if SOFT_SHADOWS_ONLY:
            assert 'image width/height/tiling' in advertised['forge_play_tune']['description']
            assert 'Light shadow source' in advertised['forge_play_tune']['description']
            assert 'emitter radius/full width/full height' in advertised['forge_play_tune']['description']
            actor = ok('forge_entity_create', arguments=dict(recipe='render.light',
                        name='MCP soft-shadow controls'))['results'][0]['selected']
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.light',
                                                        values=dict(kind=3, cast_shadows=False)))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            def runtime_light():
                snapshot = read('forge_play_snapshot')
                return next(row['components']['forge.light'] for row in snapshot['entities']
                            if row['id'] == actor)
            for field, value in [('shadow_source_radius', .25), ('shadow_angular_radius', .125),
                                 ('emitter_radius', .75), ('emitter_width', 2),
                                 ('emitter_height', .5), ('image_width', 4),
                                 ('image_height', 2), ('image_tiled', True)]:
                control('forge_play_tune', entity=actor, component='forge.light', property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row:
                            not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' not in ack['tune_ack'] and runtime_light()[field] == value, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            tuned = runtime_light()
            for field, value in [('shadow_source_radius', -1), ('shadow_source_radius', 10001),
                                 ('shadow_angular_radius', 1), ('shadow_angular_radius', True),
                                 ('emitter_radius', 0), ('emitter_radius', True),
                                 ('emitter_width', .0001), ('emitter_height', 10001),
                                 ('image_width', 0), ('image_height', 10001),
                                 ('image_tiled', 1)]:
                control('forge_play_tune', entity=actor, component='forge.light', property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row:
                            not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' in ack['tune_ack'] and runtime_light() == tuned, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            state = until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
            for field, value in [('cast_shadows', False), ('texture', None)]:
                failed(mutate('forge_play_tune', launch=state['launch'], session=state['session'],
                              generation=state['generation'], entity=actor, component='forge.light',
                              property=field, value=value), 'play_busy')
                assert runtime_light() == tuned
            wrong = {key: paused[key] for key in ('launch', 'session', 'generation')}
            wrong['generation'] += 1
            failed(mutate('forge_play_tune', **wrong, entity=actor, component='forge.light',
                          property='shadow_source_radius', value=1), 'stale_play')
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            print(('legacy' if legacy else 'current') + ': Light source controls, validation, scope, '
                  'paused clock and authored isolation passed')
            continue
        if DECALS_ONLY:
            assert 'Decal Projector' in advertised['forge_play_tune']['description']
            actor = ok('forge_entity_create', arguments=dict(recipe='render.decal',
                       name='Live projected paint'))['results'][0]['selected']
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.decal',
                field='material', value='9d087798-a173-48e0-938d-7607eed42f91'))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            def runtime_decal():
                snapshot = read('forge_play_snapshot')
                return next(row['components']['forge.decal'] for row in snapshot['entities'] if row['id'] == actor)
            for field, value in [('enabled', False), ('width', 2.), ('height', 3.), ('depth', 4.),
                                 ('tint_r', .25), ('tint_g', .5), ('tint_b', .75), ('opacity', .25),
                                 ('angle_fade_start', .5), ('angle_fade_end', 1.), ('layers', 3),
                                 ('priority', -2147483648), ('color_influence', .25),
                                 ('normal_influence', .5), ('roughness_influence', .75)]:
                control('forge_play_tune', entity=actor, component='forge.decal', property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' not in ack['tune_ack'] and runtime_decal()[field] == value, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            tuned = runtime_decal()
            for field, value in [('width', 0), ('opacity', 1.01), ('enabled', 1),
                                 ('priority', 2147483648), ('angle_fade_end', .4), ('color_influence', -.01),
                                 ('normal_influence', 1.01), ('roughness_influence', True)]:
                control('forge_play_tune', entity=actor, component='forge.decal', property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' in ack['tune_ack'] and runtime_decal() == tuned, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            state = until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
            failed(mutate('forge_play_tune', launch=state['launch'], session=state['session'],
                          generation=state['generation'], entity=actor, component='forge.decal',
                          property='material', value=None), 'play_busy')
            wrong = {key: paused[key] for key in ('launch', 'session', 'generation')}
            wrong['generation'] += 1
            failed(mutate('forge_play_tune', **wrong, entity=actor, component='forge.decal',
                          property='opacity', value=1), 'stale_play')
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            print(('legacy' if legacy else 'current') + ': decal creation/Save, fifteen live controls, '
                  'validation, scope, paused clock and authored isolation passed')
            continue
        if PROBES_ONLY:
            assert 'Reflection Probe' in advertised['forge_play_tune']['description']
            actor = ok('forge_entity_create', arguments=dict(recipe='render.reflection_probe',
                        name='MCP local reflection controls'))['results'][0]['selected']
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.reflection_probe',
                   field='texture', value='d4c60053-60c8-4456-8771-a7cf8182a902'))
            ok('forge_document_save')
            authored = read('forge_scene_read')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
            def runtime_probe():
                snapshot = read('forge_play_snapshot')
                return next(row['components']['forge.reflection_probe'] for row in snapshot['entities']
                            if row['id'] == actor)
            for field, value in [('enabled', False), ('intensity', 2.5), ('half_extent_x', .0001),
                                 ('half_extent_y', 3.), ('half_extent_z', 4.), ('blend_distance', .5),
                                 ('rotation_degrees', 90.), ('priority', -2147483648),
                                 ('box_projection', False), ('diffuse_intensity', .75)]:
                control('forge_play_tune', entity=actor, component='forge.reflection_probe',
                        property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row:
                            not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' not in ack['tune_ack'] and runtime_probe()[field] == value, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            tuned = runtime_probe()
            for field, value in [('half_extent_x', .000099), ('priority', 2147483648),
                                 ('enabled', 1), ('intensity', -1), ('diffuse_intensity', -1),
                                 ('diffuse_intensity', 1000000.1), ('diffuse_intensity', True)]:
                control('forge_play_tune', entity=actor, component='forge.reflection_probe',
                        property=field, value=value)
                ack = until(lambda: read('forge_play_status'), lambda row:
                            not row['tune_pending'] and row['tune_ack'] is not None)
                assert 'error' in ack['tune_ack'] and runtime_probe() == tuned, ack
                assert ack['timing']['tick'] == paused['timing']['tick']
            state = until(lambda: read('forge_play_status'), lambda row: row['control_ready'])
            failed(mutate('forge_play_tune', launch=state['launch'], session=state['session'],
                          generation=state['generation'], entity=actor, component='forge.reflection_probe',
                          property='texture', value=None), 'play_busy')
            assert runtime_probe() == tuned
            wrong = {key: paused[key] for key in ('launch', 'session', 'generation')}
            wrong['generation'] += 1
            failed(mutate('forge_play_tune', **wrong, entity=actor, component='forge.reflection_probe',
                          property='intensity', value=1), 'stale_play')
            control('forge_play_stop')
            assert read('forge_scene_read') == authored
            print(('legacy' if legacy else 'current') + ': probe recipe/Save, ten live controls, '
                  'typed/map/stale rejection, paused clock and authored isolation passed')
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
            mip = next(f for f in component['fields'] if f['id'] == 'mip_filter')
            assert mip['default'] == 0
            catalog = json.loads((root/'Game/forge.assets.json').read_text())
            material = next(row['id'] for row in catalog['assets']
                            if row['source'] == 'Assets/Sprites/SignalSheet.material.json')
            actor = ok('forge_entity_create', arguments=dict(name='MCP frame bank'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.sprite_renderer'))
            before = read('forge_scene_read')
            frames = [dict(x=1, y=1, width=13, height=14), dict(x=18, y=3, width=12, height=10),
                      dict(x=33, y=2, width=14, height=12)]
            native_frames = [dict(rotated_clockwise=False, source_width=0, source_height=0,
                                  trim_x=0, trim_y=0, **row) for row in frames]
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.sprite_renderer',
                                                        values=dict(material=material, frame_regions=frames, frame=2, mip_filter=1)))
            authored = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == authored
            bad = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.sprite_renderer',
                                                               field='frame_regions', value=[dict(x=-1,y=0,width=1,height=1)]))
            assert not bad['ok'] and read('forge_scene_read') == authored
            for invalid in (-1, 3, .5, True, 1 << 32):
                bad = mutate('forge_property_set', arguments=dict(entity=actor, component='forge.sprite_renderer',
                                                                 field='mip_filter', value=invalid))
                assert not bad['ok'] and read('forge_scene_read') == authored
            ok('forge_document_save')
            ok('forge_play_start')
            until(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'])
            control('forge_play_pause')
            paused = until(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
            def runtime_sprite():
                return next(e['components']['forge.sprite_renderer']
                            for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
            assert runtime_sprite()['frame_regions'] == native_frames and runtime_sprite()['frame'] == 2
            assert runtime_sprite()['mip_filter'] == 1
            for mode in (0, 2):
                control('forge_play_tune', entity=actor, component='forge.sprite_renderer', property='mip_filter', value=mode)
                ack = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
                assert 'error' not in ack['tune_ack'] and runtime_sprite()['mip_filter'] == mode, ack
            control('forge_play_tune', entity=actor, component='forge.sprite_renderer', property='mip_filter', value=3)
            ack = until(lambda: read('forge_play_status'), lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
            assert 'error' in ack['tune_ack'] and runtime_sprite()['mip_filter'] == 2, ack
            live = [dict(row, x=row['x']+1) for row in native_frames]
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
            contour_map = next(row['id'] for row in catalog['assets']
                               if row['source'] == 'Assets/Tiles/ContourTiles.tilemap.json')
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
            previous = authored
            for selected_map, one_way_motion in ((tile_map, 0), (tile_map, 1),
                                                  (contour_map, 0), (contour_map, 1)):
                before_map = read('forge_scene_read')
                if next(e for e in before_map['entities'] if e['id'] == actor)['components']['forge.tile_map_renderer']['map'] != selected_map:
                    ok('forge_property_set', arguments=dict(entity=actor, component='forge.tile_map_renderer',
                                                            field='map', value=selected_map))
                    changed_map = read('forge_scene_read')
                    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before_map
                    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == changed_map
                    previous = changed_map
                ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.physics_body_2d',
                                                            values=dict(motion=one_way_motion, sensor=False, one_way=True)))
                one_way = read('forge_scene_read')
                assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == previous
                assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == one_way
                ok('forge_document_save')
                ok('forge_play_start')
                until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
                control('forge_play_pause')
                control('forge_play_step')
                running = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
                assert running['components']['forge.physics_body_2d']['one_way'] is True
                assert running['components']['forge.physics_body_2d']['motion'] == one_way_motion
                assert running['components']['forge.physics_body_2d']['sensor'] is False
                control('forge_play_stop')
                assert read('forge_scene_read') == one_way
                previous = one_way
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.tile_map_renderer',
                                                    field='map', value=tile_map))
            before_binding = read('forge_scene_read')
            ok('forge_transform_binding', arguments=dict(entity=actor, spatial=dict(mode='world')))
            world_bound = read('forge_scene_read')
            assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before_binding
            assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == world_bound
            previous = world_bound
            for sensor in (False, True):
                ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.physics_body_2d',
                    values=dict(motion=2, sensor=sensor, one_way=False, density=2.5, mass=12)))
                dynamic = read('forge_scene_read')
                assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == previous
                assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == dynamic
                invalid = mutate('forge_property_set_many', arguments=dict(entity=actor,
                    component='forge.physics_body_2d', values=dict(mass=-1, sensor=not sensor)))
                assert not invalid['ok'] and read('forge_scene_read') == dynamic, invalid
                ok('forge_document_save')
                ok('forge_play_start')
                until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
                control('forge_play_pause')
                paused = until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
                before = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
                control('forge_play_step')
                until(lambda: read('forge_play_status'), lambda row: row['timing']['tick'] == paused['timing']['tick'] + 1 and row['control_ready'])
                running = next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)
                fields = running['components']['forge.physics_body_2d']
                assert fields['motion'] == 2 and fields['sensor'] == sensor and fields['mass'] == 12
                assert running['components']['forge.local_translation']['y'] < before['components']['forge.local_translation']['y']
                assert running['components']['forge.local_translation']['z'] == 7
                control('forge_play_stop')
                assert read('forge_scene_read') == dynamic
                previous = dynamic
            # The same ordinary component/property/bake owners select painted collision.
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.physics_body_2d',
                                                        values=dict(motion=0, one_way=False)))
            ok('forge_component_add', arguments=dict(entity=actor, component='forge.navigation_region_2d'))
            ok('forge_property_set_many', arguments=dict(entity=actor, component='forge.navigation_region_2d',
                                                        values=dict(source=1, obstacle=True)))
            floor = ok('forge_entity_create', arguments=dict(name='Map navigation floor'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.navigation_region_2d'))
            ok('forge_property_set_many', arguments=dict(entity=floor, component='forge.navigation_region_2d',
                                                        values=dict(width=40, height=30)))
            nav_settings = dict(radius=.1, height=2, climb=.4, slope=45,
                                cell_size=.2, cell_height=.1, plane=1)
            ok('forge_navigation_build', settings=nav_settings)
            navigation = until(lambda: read('forge_navigation_status'), lambda value: not value['busy'])
            assert navigation['asset'] and not navigation['error'], navigation
            index = root/'Game/forge.assets.json'
            selected = next(row for row in json.loads(index.read_text())['assets']
                            if row['id'] == navigation['asset'])
            assert selected['schema_version'] == 5 and selected['metadata']['geometry_version'] == 5, selected
            assert [row['asset_id'] for row in selected['metadata']['tile_inputs']] == [tile_map], selected
            assert any(edge['target'] == tile_map and edge['role'] == 'navigation.tile_map'
                       for edge in selected['dependency_edges']), selected
            preserved = index.read_bytes()
            ok('forge_property_set', arguments=dict(entity=actor, component='forge.tile_map_collider_2d',
                                                    field='layer', value=0))
            ok('forge_navigation_build', settings=nav_settings)
            rejected = until(lambda: read('forge_navigation_status'), lambda value: not value['busy'])
            assert rejected['error'] and rejected['asset'] == navigation['asset'] and index.read_bytes() == preserved, rejected
            ok('forge_history_undo')
            print(('legacy' if legacy else 'current') + ': native Map placement, Kinematic/sensor, one-way and World-bound Dynamic solid/sensor, history/revision, Save and Play passed')
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
            region_schema = next(c for c in discovery()['result']['schema']['components']
                                 if c['id'] == 'forge.navigation_region_2d')
            point_schema = next(f for f in region_schema['fields'] if f['id'] == 'points')
            assert point_schema['type'] == 'vector' and point_schema['maximum_count'] == 64, point_schema
            assert point_schema['default'] == [] and point_schema['element']['type'] == 'struct', point_schema
            source_schema = next(f for f in region_schema['fields'] if f['id'] == 'source')
            assert source_schema['default'] == 0 and source_schema['animatable'] is False, source_schema
            assert [(c['value'], c['label']) for c in source_schema['choices']] == [
                (0, 'Rectangle / Points'), (1, 'Collider 2D')], source_schema
            original_region_scene = read('forge_scene_read')
            for invalid_source in (-1, 2, .5, 'collider'):
                invalid = mutate('forge_property_set', arguments=dict(entity=obstacle,
                    component='forge.navigation_region_2d', field='source', value=invalid_source))
                assert not invalid['ok'] and read('forge_scene_read') == original_region_scene, invalid
            ok('forge_component_add', arguments=dict(entity=obstacle, component='forge.circle_collider_2d'))
            original_region_scene = read('forge_scene_read')
            ok('forge_property_set', arguments=dict(entity=obstacle,
                component='forge.navigation_region_2d', field='source', value=1))
            collider_scene = read('forge_scene_read')
            ok('forge_history_undo')
            assert read('forge_scene_read') == original_region_scene
            ok('forge_history_redo')
            assert read('forge_scene_read') == collider_scene
            ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
                                                       cell_size=.2, cell_height=.1, plane=1))
            collision = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert collision['asset'] == asset and not collision['error'], collision
            selected = next(row for row in json.loads(index.read_text(encoding='utf-8'))['assets']
                            if row['id'] == asset)
            assert selected['schema_version'] == 5 and selected['metadata']['tile_inputs'] == [], selected
            ok('forge_property_set', arguments=dict(entity=obstacle,
                component='forge.navigation_region_2d', field='source', value=0))
            ok('forge_component_revert', arguments=dict(entity=obstacle, component='forge.circle_collider_2d'))
            polygon = [dict(x=-3, y=-3), dict(x=3, y=-3), dict(x=3, y=-1),
                       dict(x=-1, y=-1, note='preserve'), dict(x=-1, y=3), dict(x=-3, y=3)]
            rectangle_scene = read('forge_scene_read')
            ok('forge_property_set', arguments=dict(entity=obstacle, component='forge.navigation_region_2d',
                                                   field='points', value=polygon))
            polygon_scene = read('forge_scene_read')
            polyrow = next(e for e in polygon_scene['entities'] if e['id'] == obstacle)
            assert polyrow['components']['forge.navigation_region_2d']['points'] == polygon
            ok('forge_history_undo')
            assert read('forge_scene_read') == rectangle_scene
            ok('forge_history_redo')
            assert read('forge_scene_read') == polygon_scene
            for bad_points in ([dict(x=0, y=0)], [dict(x=0, y=0)] * 65,
                               [dict(x=0, y=0), dict(x=1, y=1), dict(x=0, y=1), dict(x=1, y=0)]):
                invalid = mutate('forge_property_set', arguments=dict(entity=obstacle,
                    component='forge.navigation_region_2d', field='points', value=bad_points))
                assert not invalid['ok'] and read('forge_scene_read') == polygon_scene, invalid
            ok('forge_property_set', arguments=dict(entity=obstacle, component='forge.navigation_region_2d',
                                                   field='points', value=[]))
            cleared = next(e for e in read('forge_scene_read')['entities'] if e['id'] == obstacle)
            assert cleared['components']['forge.navigation_region_2d']['points'] == []
            ok('forge_history_undo')
            assert read('forge_scene_read') == polygon_scene
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
            planar_geometry = selected['metadata']['geometry_sha256']
            # A named build is a separate ordinary asset; default remains untouched.
            profile_snapshot = dict(selected)
            profile_settings = dict(settings, profile='Wide_agent', radius=.9)
            ok('forge_navigation_build', settings=profile_settings)
            wide = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert not wide['error'] and wide['asset'] != asset, wide
            assert wide['profiles']['default'] == asset and wide['profiles']['Wide_agent'] == wide['asset'], wide
            rows = json.loads(index.read_text(encoding='utf-8'))['assets']
            assert next(row for row in rows if row['id'] == asset) == profile_snapshot
            unchanged = index.read_bytes()
            authored = read('forge_scene_read')
            ok('forge_navigation_select_profile', profile='default')
            original_profile = read('forge_navigation_status')
            assert original_profile['asset'] == asset and original_profile['settings'] == planar['settings']
            ok('forge_navigation_select_profile', profile='Unbuilt')
            draft = read('forge_navigation_status')
            assert draft['asset'] is None and draft['settings']['profile'] == 'Unbuilt', draft
            assert 'Unbuilt' not in draft['profiles'] and index.read_bytes() == unchanged
            for invalid in ('', '../wide', 'two words', '1wide', 'x' * 65, 7):
                rejected = mutate('forge_navigation_build', settings=dict(settings, profile=invalid))
                assert not rejected['ok'] and index.read_bytes() == unchanged, rejected
                rejected = mutate('forge_navigation_select_profile', profile=invalid)
                assert not rejected['ok'] and index.read_bytes() == unchanged, rejected
                assert read('forge_navigation_status') == draft
            ok('forge_navigation_select_profile', profile='default')
            assert read('forge_scene_read') == authored and index.read_bytes() == unchanged
            preserved = index.read_bytes()
            for invalid in (-1, 2, .5, 'xy'):
                rejected = mutate('forge_navigation_build', settings=dict(settings, plane=invalid))
                assert not rejected['ok'] and index.read_bytes() == preserved, rejected
            ok('forge_component_add', arguments=dict(entity=floor, component='forge.character_controller_2d'))
            ok('forge_navigation_build', settings=settings)
            rejected = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert 'enabled character controller' in rejected['error'] and index.read_bytes() == preserved, rejected
            ok('forge_component_revert', arguments=dict(entity=floor, component='forge.character_controller_2d'))
            cylinder = ok('forge_entity_create', arguments=dict(name='Moving navigation obstacle'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=cylinder, component='forge.navigation_obstacle'))
            component = next(c for c in discovery()['result']['schema']['components']
                             if c['id'] == 'forge.navigation_obstacle')
            assert {f['id'] for f in component['fields']} == {'enabled', 'radius', 'height'}, component
            def cylinder_value():
                return next(row for row in read('forge_scene_read')['entities'] if row['id'] == cylinder)['components']['forge.navigation_obstacle']
            ok('forge_property_set_many', arguments=dict(entity=cylinder, component='forge.navigation_obstacle',
                                                       values=dict(enabled=False, radius=1, height=3)))
            assert cylinder_value()['radius'] == 1
            ok('forge_history_undo')
            assert cylinder_value()['radius'] == .5
            ok('forge_history_redo')
            assert cylinder_value()['radius'] == 1 and not cylinder_value()['enabled']
            before = cylinder_value()
            rejected = mutate('forge_property_set', arguments=dict(entity=cylinder,
                              component='forge.navigation_obstacle', field='radius', value=0))
            assert not rejected['ok'] and cylinder_value() == before, rejected
            settings = dict(settings, dynamic_obstacles=True)
            ok('forge_navigation_build', settings=settings)
            tiled = until(lambda: read('forge_navigation_status'), lambda r: not r['busy'])
            assert tiled['asset'] == asset and not tiled['error'] and tiled['settings']['dynamic_obstacles'], tiled
            records = json.loads(index.read_text(encoding='utf-8'))['assets']
            if isinstance(records, dict):
                records = records.values()
            selected = next(record for record in records if record['id'] == asset)
            assert selected['schema_version'] == 4 and selected['metadata']['geometry_version'] == 3, selected
            assert selected['metadata']['geometry_sha256'] == planar_geometry
            tiled_index = index.read_bytes()
            for invalid in (0, 1, 'yes', None):
                rejected = mutate('forge_navigation_build', settings=dict(settings, dynamic_obstacles=invalid))
                assert not rejected['ok'] and index.read_bytes() == tiled_index, rejected
            ok('forge_document_save')
            assert cylinder_value() == before
            assert obstacle_width() == 3
            shared_schema = next(c for c in discovery()['result']['schema']['components']
                                 if c['id'] == 'forge.navigation_avoidance')
            shared_field = next(f for f in shared_schema['fields'] if f['id'] == 'shared_group')
            assert shared_field['default'] == 0 and shared_field['maximum'] == 32 and not shared_field['animatable']
            shared_holder = ok('forge_entity_create', arguments=dict(name='Shared group authoring sample'))['results'][0]['selected']
            ok('forge_component_add', arguments=dict(entity=shared_holder, component='forge.navigation_avoidance'))
            before_shared = read('forge_scene_read')
            ok('forge_property_set', arguments=dict(entity=shared_holder,
                component='forge.navigation_avoidance', field='shared_group', value=7))
            with_shared = read('forge_scene_read')
            ok('forge_history_undo')
            assert read('forge_scene_read') == before_shared
            ok('forge_history_redo')
            assert read('forge_scene_read') == with_shared
            for invalid in (-1, 33, .5, 'group'):
                rejected = mutate('forge_property_set', arguments=dict(entity=shared_holder,
                    component='forge.navigation_avoidance', field='shared_group', value=invalid))
                assert not rejected['ok'] and read('forge_scene_read') == with_shared, rejected
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
            before_profile = read('forge_navigation_status')
            rejected_profile = mutate('forge_navigation_select_profile', profile='Wide_agent')
            assert not rejected_profile['ok'] and read('forge_navigation_status') == before_profile
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
                assert 'forge_navigation_select_profile' not in denied_names
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
print('Both MCP protocols: collision collection discovery/validation/history/revisions/permissions, named profiles and actual Play route/step/live tuning verified' if NAVIGATION_COLLECTION_ONLY else
      'Both MCP protocols: authored navigation link discovery/history/revisions/validation, named-profile bake and actual Play traversal verified' if NAVIGATION_LINKS_ONLY else
      'Both MCP protocols: Decal Projector live tuning, validation, scope, paused clock and authored isolation verified' if DECALS_ONLY else
      'Both MCP protocols: current Light source live tuning, validation, scope, paused clock and authored isolation verified' if SOFT_SHADOWS_ONLY else
      'Both MCP protocols: animated UI source/reload, current Play guards and execution grant verified; Windows execution pending' if UI_ANIMATION_ONLY else
      'Both MCP protocols: navigation ownership, XY authoring and live controls verified; no export or Windows execution' if NAVIGATION_ONLY else
      'Both MCP protocols and independent runtime/output grants verified; no graphical Windows execution claimed')
