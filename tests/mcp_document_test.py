"""MCP against real EditorFiles/SceneDocument owners, leases and disk files."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from mcp_test_client import CAPTURE, Client, failed, write_capture, finish_host
from mcp_fast_aa_workflow import check_fast_aa_workflow
from mcp_morphological_aa_workflow import check_morphological_aa_workflow
from mcp_occlusion_culling_workflow import check_occlusion_culling_workflow
from mcp_mesh_scatter_workflow import check_mesh_scatter_workflow
from mcp_sequence_clip_workflow import check_sequence_clip_workflow
from mcp_nested_sequence_workflow import check_nested_sequence_workflow
from mcp_input_message_workflow import check_input_message_workflow
from mcp_fog_volume_workflow import check_fog_volume_workflow

TOOLS, HOST = map(str, map(Path, sys.argv[1:3]))
NESTED_SEQUENCES_ONLY = "--nested-sequences-only" in sys.argv[4:]
INPUT_MESSAGES_ONLY = "--input-messages-only" in sys.argv[4:]
FOG_VOLUMES_ONLY = "--fog-volumes-only" in sys.argv[4:]
SEQUENCE_CLIPS_ONLY = "--sequence-clips-only" in sys.argv[4:]
OCCLUSION_CULLING_ONLY = "--occlusion-culling-only" in sys.argv[4:]
MORPHOLOGICAL_AA_ONLY = "--morphological-aa-only" in sys.argv[4:]
FAST_AA_ONLY = "--fast-aa-only" in sys.argv[4:]
MESH_SCATTER_ONLY = "--mesh-scatter-only" in sys.argv[4:]


@contextmanager
def editor(mode='files'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-doc-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, process, json.loads(line)
        finally:
            finish_host(process, root)


def native(config, request, sequence=None):
    wire = dict(transport=1, token=config['token'], id='document-test', request=request)
    if sequence is not None:
        wire['sequence'] = sequence
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        channel.sendall((json.dumps(wire) + '\n').encode())
        with channel.makefile('rb') as reader:
            return json.loads(reader.readline(16777217))['response']


def wait_for(predicate):
    end = time.monotonic() + 4
    while not predicate():
        assert time.monotonic() < end, 'Owner did not process the test control marker'
        time.sleep(.01)


for legacy in (False, True):
    with editor() as (root, process, config), Client(config, legacy) as client:
        project = root / 'Game'

        def discover():
            result = client.tool('forge_discover')
            assert result['ok'], result
            return result

        def arguments(**extra):
            state = discover()
            return dict(target=state['result']['target'], expected_revision=state['revision'],
                        sequence=state['next_sequence'], **extra)

        def mutate(name, **extra):
            return client.tool(name, **arguments(**extra))

        def status():
            state = discover()
            result = client.tool('forge_project_read', target=state['result']['target'])
            assert result['ok'], result
            return result['result']

        catalog = client.request('tools/list')['result']['tools']
        assert len(catalog) == 43, len(catalog)
        state = discover()
        probe_schema = next(c for c in state['result']['schema']['components']
                            if c['id'] == 'forge.reflection_probe')
        probe_fields = {field['id']: field for field in probe_schema['fields']}
        assert probe_schema['optional'] and len(probe_fields) == 17
        assert probe_fields['diffuse_intensity']['default'] == 0
        assert probe_fields['diffuse_intensity']['minimum'] == 0 and probe_fields['diffuse_intensity']['maximum'] == 1000000
        assert probe_fields['runtime_capture']['default'] is False
        assert probe_fields['capture_size']['default'] == 128
        assert probe_fields['capture_interval']['default'] == .25
        assert probe_fields['capture_layers']['default'] == 4294967295
        assert probe_fields['texture']['asset_type'] == 'texture'
        assert probe_fields['half_extent_x']['minimum'] == .0001
        assert probe_fields['half_extent_x']['maximum'] == 1000000
        create_command = next(c for c in state['result']['commands'] if c['id'] == 'entity.create')
        assert 'render.reflection_probe' in create_command['input_schema']['properties']['recipe']['enum']
        create_tool = next(tool for tool in catalog if tool['name'] == 'forge_entity_create')
        assert 'render.reflection_probe' in create_tool['inputSchema']['properties']['arguments']['properties']['recipe']['enum']
        assert 'render.decal' in create_command['input_schema']['properties']['recipe']['enum']
        assert 'render.decal' in create_tool['inputSchema']['properties']['arguments']['properties']['recipe']['enum']
        decal_schema = next(c for c in state['result']['schema']['components'] if c['id'] == 'forge.decal')
        decal_fields = {field['id']: field for field in decal_schema['fields']}
        assert decal_schema['optional'] and len(decal_fields) == 16
        for field, default in [('color_influence', 1), ('normal_influence', 0), ('roughness_influence', 0)]:
            assert decal_fields[field]['minimum'] == 0 and decal_fields[field]['maximum'] == 1
            assert decal_fields[field]['default'] == default
        assert decal_fields['material']['asset_type'] == 'material'
        assert abs(decal_fields['width']['minimum'] - .001) < 1e-9 and decal_fields['width']['maximum'] == 10000
        body_schema = next(c for c in state['result']['schema']['components']
                           if c['id'] == 'forge.physics_body_2d')
        fields = {f['id']: f for f in body_schema['fields']}
        assert fields['one_way']['display_name'] == 'One-way collision'
        assert fields['one_way_margin']['unit'] == 'm'
        controller_schema = next(c for c in state['result']['schema']['components']
                                 if c['id'] == 'forge.character_controller_2d')
        carry = next(f for f in controller_schema['fields'] if f['id'] == 'platform_carry')
        assert carry['display_name'] == 'Follow moving platforms' and carry['default'] is False, carry
        crouch = next(f for f in controller_schema['fields'] if f['id'] == 'crouch_height')
        assert crouch['unit'] == 'm' and crouch['default'] == 0, crouch
        controller_fields = {f['id']: f for f in controller_schema['fields']}
        assert controller_fields['floor_stop_on_slope']['display_name'] == 'Stop on slopes'
        assert controller_fields['floor_stop_on_slope']['default'] is False
        assert controller_fields['slide_on_ceiling']['display_name'] == 'Slide on ceilings'
        assert controller_fields['slide_on_ceiling']['default'] is True
        step = controller_fields['step_height']
        assert step['display_name'] == 'Step Height'
        assert step['default'] == 0 and step['unit'] == 'm' and step['maximum'] == 1
        workflows = state['result']['workflow_methods']
        assert {item['method'] for item in workflows} == {
            'project.read', 'document.save', 'document.save_as', 'document.new', 'document.open'}
        assert set(state['result']['capabilities']) == {
            'scene.read', 'scene.edit', 'history', 'document.read', 'document.write'}
        for method in workflows:
            tool = next(item for item in catalog if item['name'] ==
                        'forge_' + method['method'].replace('.', '_'))
            props = dict(tool['inputSchema']['properties'])
            for field in ('target', 'expected_revision', 'sequence'):
                props.pop(field, None)
            assert props == method['input_schema']['properties']
            assert tool['annotations']['readOnlyHint'] == (not method['mutation'])
        assert status()['scene_path'] == 'Scenes/main.scene.json'
        assert not status()['dirty'] and status()['persisted']
        original = discover()['result']['target']
        # Automatic exposure uses the same Scene operation, schema, revision,
        # history and file owner as human authoring, in both MCP versions.
        exposure_command = next(item for item in state['result']['commands']
                                if item['id'] == 'scene.rendering.set')
        exposure_schema = exposure_command['input_schema']['properties']['auto_exposure']
        exposure_tool = next(item for item in catalog
                             if item['name'] == 'forge_scene_rendering_set')
        if FOG_VOLUMES_ONLY:
            assert 'render.fog_volume' in create_command['input_schema']['properties']['recipe']['enum']
            assert 'render.fog_volume' in create_tool['inputSchema']['properties']['arguments']['properties']['recipe']['enum']
            check_fog_volume_workflow(client, discover, arguments, mutate, project)
            continue
        if INPUT_MESSAGES_ONLY:
            check_input_message_workflow(client, discover, arguments, mutate, project)
            continue
        if NESTED_SEQUENCES_ONLY:
            check_nested_sequence_workflow(client, discover, arguments, mutate, project)
            continue
        if SEQUENCE_CLIPS_ONLY:
            check_sequence_clip_workflow(client, discover, arguments, mutate, project)
            check_sequence_clip_workflow(client, discover, arguments, mutate, project,
                                         component_id='forge.animator')
            continue
        if MESH_SCATTER_ONLY:
            check_mesh_scatter_workflow(client, discover, arguments, mutate, project, catalog)
            continue
        if OCCLUSION_CULLING_ONLY:
            check_occlusion_culling_workflow(client, discover, arguments, mutate, project,
                                             exposure_command, exposure_tool)
            continue
        if MORPHOLOGICAL_AA_ONLY:
            check_morphological_aa_workflow(client, discover, arguments, mutate, project,
                                            exposure_command, exposure_tool)
            continue
        if FAST_AA_ONLY:
            check_fast_aa_workflow(client, discover, arguments, mutate, project,
                                   exposure_command, exposure_tool)
            continue
        check_mesh_scatter_workflow(client, discover, arguments, mutate, project, catalog)
        for _ in range(3):
            assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_document_save')['ok']
        weighted_schema = exposure_command['input_schema']['properties']['weighted_transparency']
        assert weighted_schema == {'type': 'boolean'}
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['weighted_transparency'] == weighted_schema
        weighted_arguments = arguments(arguments=dict(weighted_transparency=True))
        weighted = client.tool('forge_scene_rendering_set', **weighted_arguments)
        assert weighted['ok'] and client.tool('forge_scene_rendering_set', **weighted_arguments) == weighted
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (0, None, 'weighted', {}):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(weighted_transparency=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        before = discover()
        result = mutate('forge_scene_rendering_set', arguments=dict(weighted_transparency=False, exposure=21))
        assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_document_save')['ok']
        assert json.loads((project / 'Scenes/main.scene.json').read_text(encoding='utf-8'))['rendering']['weighted_transparency'] is True
        # Sprite/Tile glow uses the existing Scene-owned bloom operation. Its
        # source/MCP schemas, replay, history and saved values share one owner.
        bloom_schema = exposure_command['input_schema']['properties']['bloom']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['bloom'] == bloom_schema
        assert set(bloom_schema['properties']) == {'enabled', 'threshold', 'intensity', 'radius'}
        assert bloom_schema['properties']['threshold']['maximum'] == 65504
        assert bloom_schema['properties']['radius']['minimum'] == .5
        bloom_arguments = arguments(arguments=dict(bloom=dict(
            enabled=True, threshold=.25, intensity=1.25, radius=3)))
        before_bloom = discover()
        glow = client.tool('forge_scene_rendering_set', **bloom_arguments)
        assert glow['ok'] and glow['revision'] != before_bloom['revision'], glow
        assert client.tool('forge_scene_rendering_set', **bloom_arguments) == glow
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(threshold=-1), dict(intensity=4.01),
                    dict(radius=.49), dict(enabled=False, threshold=65505)):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(bloom=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set', arguments=dict(bloom=dict(intensity=1.5)))['ok']
        assert mutate('forge_document_save')['ok']
        saved_bloom = json.loads((project / 'Scenes/main.scene.json').read_text(encoding='utf-8'))['rendering']['bloom']
        assert saved_bloom == dict(enabled=True, threshold=.25, intensity=1.5, radius=3)
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['auto_exposure'] == exposure_schema
        assert set(exposure_schema['properties']) == {
            'enabled', 'min_ev', 'max_ev', 'low_percentile', 'high_percentile',
            'target_gray', 'speed_up', 'speed_down'}
        assert exposure_schema['properties']['min_ev']['minimum'] == -20
        assert exposure_schema['properties']['speed_down']['maximum'] == 20
        before_exposure = discover()
        exposure_arguments = arguments(arguments=dict(auto_exposure=dict(
            enabled=True, min_ev=-4, max_ev=6, low_percentile=.1,
            high_percentile=.9, target_gray=.2, speed_up=2, speed_down=4)))
        changed = client.tool('forge_scene_rendering_set', **exposure_arguments)
        assert changed['ok'] and changed['revision'] != before_exposure['revision'], changed
        assert client.tool('forge_scene_rendering_set', **exposure_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(low_percentile=.95), dict(min_ev=7),
                    dict(speed_up=0), dict(target_gray='0.2')):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(auto_exposure=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(auto_exposure=dict(speed_up=3)))['ok']
        # Fog is the same scene command, with schema, permission, revision,
        # replay, history and persistence shared by both MCP protocol versions.
        fog_schema = exposure_command['input_schema']['properties']['distance_fog']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['distance_fog'] == fog_schema
        assert set(fog_schema['properties']) == {'enabled', 'color', 'start', 'end'}
        assert fog_schema['properties']['color']['items']['maximum'] == 65504
        assert fog_schema['properties']['end']['minimum'] == .001
        fog_arguments = arguments(arguments=dict(distance_fog=dict(
            enabled=True, color=[.1, .2, 2], start=5, end=50)))
        before_fog = discover()
        changed = client.tool('forge_scene_rendering_set', **fog_arguments)
        assert changed['ok'] and changed['revision'] != before_fog['revision'], changed
        assert client.tool('forge_scene_rendering_set', **fog_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(start=51), dict(end=5), dict(end=1000001),
                    dict(color=[1, 2]), dict(enabled=1), dict(color=[0, 0, -1])):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(distance_fog=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(distance_fog=dict(end=60)))['ok']
        temporal_schema = exposure_command['input_schema']['properties']['temporal_anti_aliasing']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['temporal_anti_aliasing'] == temporal_schema
        assert set(temporal_schema['properties']) == {'enabled', 'stability'}
        assert temporal_schema['properties']['stability']['maximum'] == .95
        temporal_arguments = arguments(arguments=dict(temporal_anti_aliasing=dict(
            enabled=True, stability=.85)))
        before_temporal = discover()
        changed = client.tool('forge_scene_rendering_set', **temporal_arguments)
        assert changed['ok'] and changed['revision'] != before_temporal['revision'], changed
        assert client.tool('forge_scene_rendering_set', **temporal_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(stability=-.01), dict(stability=.951),
                    dict(stability='high')):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(temporal_anti_aliasing=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(temporal_anti_aliasing=dict(stability=.9)))['ok']
        check_fast_aa_workflow(client, discover, arguments, mutate, project,
                               exposure_command, exposure_tool)
        focus_schema = exposure_command['input_schema']['properties']['depth_of_field']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['depth_of_field'] == focus_schema
        assert set(focus_schema['properties']) == {'enabled', 'focus_distance', 'focus_range', 'blur_radius'}
        assert focus_schema['properties']['focus_distance']['minimum'] == .001
        assert focus_schema['properties']['blur_radius']['maximum'] == 12
        focus_arguments = arguments(arguments=dict(depth_of_field=dict(
            enabled=True, focus_distance=10, focus_range=5, blur_radius=6)))
        before_focus = discover()
        changed = client.tool('forge_scene_rendering_set', **focus_arguments)
        assert changed['ok'] and changed['revision'] != before_focus['revision'], changed
        assert client.tool('forge_scene_rendering_set', **focus_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(focus_distance=0), dict(focus_range=-1),
                    dict(blur_radius=12.00000001), dict(focus_distance='near')):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(depth_of_field=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(depth_of_field=dict(focus_range=4)))['ok']
        motion_schema = exposure_command['input_schema']['properties']['motion_blur']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['motion_blur'] == motion_schema
        assert set(motion_schema['properties']) == {'enabled', 'shutter', 'max_pixels'}
        assert motion_schema['properties']['shutter']['maximum'] == 1
        assert motion_schema['properties']['max_pixels']['maximum'] == 32
        motion_arguments = arguments(arguments=dict(motion_blur=dict(
            enabled=True, shutter=.75, max_pixels=24)))
        before_motion = discover()
        changed = client.tool('forge_scene_rendering_set', **motion_arguments)
        assert changed['ok'] and changed['revision'] != before_motion['revision'], changed
        assert client.tool('forge_scene_rendering_set', **motion_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(shutter=-.01), dict(shutter=1.000000001),
                    dict(max_pixels=32.000000001), dict(max_pixels=-1), dict(shutter='half')):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(motion_blur=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(motion_blur=dict(shutter=.5)))['ok']
        ambient_schema = exposure_command['input_schema']['properties']['ambient_occlusion']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['ambient_occlusion'] == ambient_schema
        assert set(ambient_schema['properties']) == {'enabled', 'radius', 'strength', 'bias', 'normal_influence'}
        assert ambient_schema['properties']['radius']['maximum'] == 10
        assert ambient_schema['properties']['normal_influence'] == dict(type='number', minimum=0, maximum=1)
        ambient_arguments = arguments(arguments=dict(ambient_occlusion=dict(
            enabled=True, radius=1, strength=.75, bias=.02, normal_influence=.7)))
        before_ambient = discover()
        changed = client.tool('forge_scene_rendering_set', **ambient_arguments)
        assert changed['ok'] and changed['revision'] != before_ambient['revision'], changed
        assert client.tool('forge_scene_rendering_set', **ambient_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(radius=10.000000001), dict(strength=1.000000001),
                    dict(bias=1.000000001), dict(radius=-.01), dict(bias=-1), dict(radius='near'),
                    dict(normal_influence=1.000000001), dict(normal_influence=-.000000001),
                    dict(normal_influence=True), dict(normal_influence=None)):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(ambient_occlusion=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(ambient_occlusion=dict(strength=.5)))['ok']
        reflection_schema = exposure_command['input_schema']['properties']['screen_space_reflections']
        assert exposure_tool['inputSchema']['properties']['arguments']['properties']['screen_space_reflections'] == reflection_schema
        assert set(reflection_schema['properties']) == {'enabled', 'max_distance', 'thickness', 'strength', 'max_roughness', 'roughness_filtering'}
        assert reflection_schema['properties']['thickness']['minimum'] == .0001
        assert reflection_schema['properties']['roughness_filtering']['type'] == 'boolean'
        reflection_arguments = arguments(arguments=dict(screen_space_reflections=dict(
            enabled=True, max_distance=20, thickness=.05, strength=.75, max_roughness=.4, roughness_filtering=True)))
        before_reflection = discover()
        changed = client.tool('forge_scene_rendering_set', **reflection_arguments)
        assert changed['ok'] and changed['revision'] != before_reflection['revision'], changed
        assert client.tool('forge_scene_rendering_set', **reflection_arguments) == changed
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for bad in (dict(enabled=1), dict(max_distance=100.000000001),
                    dict(thickness=.000099999999), dict(thickness=1.000000001),
                    dict(strength=1.000000001), dict(max_roughness=1.000000001),
                    dict(max_distance=-.01), dict(thickness='thin'),
                    dict(roughness_filtering=1), dict(roughness_filtering=None),
                    dict(roughness_filtering='true')):
            before = discover()
            result = mutate('forge_scene_rendering_set', arguments=dict(screen_space_reflections=bad))
            assert not result['ok'] and discover()['revision'] == before['revision'], result
        assert mutate('forge_scene_rendering_set',
                      arguments=dict(screen_space_reflections=dict(strength=.5)))['ok']
        # Existing project writer ownership remains exclusive across processes.
        lease = subprocess.run([sys.argv[1].replace('forge_tools', 'forge_live_tests'),
                                '--lease', str(project)], input='', capture_output=True,
                               text=True, timeout=5)
        assert lease.returncode == 1 and 'owned' not in lease.stdout, lease

        created = mutate('forge_entity_create', arguments=dict(kind=0, name='Saved agent cube'))
        assert created['ok'], created
        cube = created['result']['results'][0]['selected']
        for component in ('forge.physics_body_2d', 'forge.box_collider_2d'):
            result = mutate('forge_component_add', arguments=dict(entity=cube, component=component))
            assert result['ok'], result
        for field, value in (('one_way', True), ('one_way_margin', .05)):
            result = mutate('forge_property_set', arguments=dict(entity=cube,
                component='forge.physics_body_2d', field=field, value=value))
            assert result['ok'], result
        before_invalid = discover()
        invalid = mutate('forge_property_set', arguments=dict(entity=cube,
            component='forge.physics_body_2d', field='one_way_margin', value=-1))
        assert not invalid['ok'] and discover()['revision'] == before_invalid['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        character = mutate('forge_entity_create', arguments=dict(kind=0, name='Platform rider'))
        assert character['ok'], character
        rider = character['result']['results'][0]['selected']
        assert mutate('forge_component_add', arguments=dict(
            entity=rider, component='forge.character_controller_2d'))['ok']
        assert mutate('forge_property_set', arguments=dict(entity=rider,
            component='forge.character_controller_2d', field='platform_carry', value=True))['ok']
        before_invalid = discover()
        invalid = mutate('forge_property_set', arguments=dict(entity=rider,
            component='forge.character_controller_2d', field='platform_carry', value=1))
        assert not invalid['ok'] and discover()['revision'] == before_invalid['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        assert status()['dirty']
        assert mutate('forge_property_set', arguments=dict(entity=rider,
            component='forge.character_controller_2d', field='crouch_height', value=.2))['ok']
        for invalid_height in (-.01, .7):
            before_invalid = discover()
            invalid = mutate('forge_property_set', arguments=dict(entity=rider,
                component='forge.character_controller_2d', field='crouch_height', value=invalid_height))
            assert not invalid['ok'] and discover()['revision'] == before_invalid['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        for field, value in (('floor_stop_on_slope', True), ('slide_on_ceiling', False)):
            assert mutate('forge_property_set', arguments=dict(entity=rider,
                component='forge.character_controller_2d', field=field, value=value))['ok']
            before_invalid = discover()
            invalid = mutate('forge_property_set', arguments=dict(entity=rider,
                component='forge.character_controller_2d', field=field, value=1))
            assert not invalid['ok'] and discover()['revision'] == before_invalid['revision'], invalid
            assert mutate('forge_history_undo')['result']['changed']
            assert mutate('forge_history_redo')['result']['changed']
        assert mutate('forge_property_set', arguments=dict(entity=rider,
            component='forge.character_controller_2d', field='step_height', value=.3))['ok']
        for invalid_step in (-.01, 1.01, '0.3'):
            before_invalid = discover()
            invalid = mutate('forge_property_set', arguments=dict(entity=rider,
                component='forge.character_controller_2d', field='step_height', value=invalid_step))
            assert not invalid['ok'] and discover()['revision'] == before_invalid['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        created_decal = mutate('forge_entity_create', arguments=dict(recipe='render.decal', name='Wall paint'))
        assert created_decal['ok'], created_decal
        decal = created_decal['result']['results'][0]['selected']
        decal_values = dict(material='9d087798-a173-48e0-938d-7607eed42f91', width=2., height=3.,
                            depth=4., opacity=.25, priority=-7, angle_fade_start=.5, angle_fade_end=1.,
                            color_influence=.25, normal_influence=.5, roughness_influence=.75)
        request = arguments(arguments=dict(entity=decal, component='forge.decal', values=decal_values))
        changed_decal = client.tool('forge_property_set_many', **request)
        assert changed_decal['ok'], changed_decal
        assert client.tool('forge_property_set_many', **request)['result'] == changed_decal['result']
        # The renderer profile does not require a second agent authoring path.
        paint_commands = [dict(operation='entity.create', arguments=dict(
            recipe='render.decal', name='Shared paint ' + str(index),
            component_values=dict(material=decal_values['material'], width=1., height=1.,
                                  opacity=.5, priority=index))) for index in range(127)]
        paint_request = arguments(commands=paint_commands)
        batch_started = time.monotonic()
        while True:
            painted = client.tool('forge_scene_apply', _response_timeout=20, **paint_request)
            if painted['ok']:
                break
            # A slow instrumented owner can outlive the adapter's transport deadline.
            # Recover the exact receipt; a fresh sequence could apply the batch twice.
            assert painted['error']['code'] == 'connection_unavailable', painted
            assert time.monotonic() - batch_started < 60, painted
            time.sleep(.1)
        print(('legacy' if legacy else 'current') + ': 127-projector batch and receipt seconds=' +
              str(round(time.monotonic() - batch_started, 3)), flush=True)
        assert painted['ok'] and len(painted['result']['results']) == 127, painted
        assert client.tool('forge_scene_apply', **paint_request)['result'] == painted['result']
        def paint_count():
            state = discover()
            snapshot = client.tool('forge_scene_read', target=state['result']['target'])
            assert snapshot['ok'], snapshot
            return sum('forge.decal' in entity['components'] for entity in snapshot['result']['entities'])
        assert paint_count() == 128
        before_bad_paint = discover()
        bad_paint = mutate('forge_scene_apply', commands=[paint_commands[0], dict(
            operation='entity.create', arguments=dict(recipe='render.decal', name='Invalid paint',
                component_values=dict(opacity=2.)))])
        assert not bad_paint['ok'] and discover()['revision'] == before_bad_paint['revision'], bad_paint
        assert paint_count() == 128
        assert mutate('forge_history_undo')['result']['changed'] and paint_count() == 1
        assert mutate('forge_history_redo')['result']['changed'] and paint_count() == 128
        assert mutate('forge_document_save')['ok']
        saved_paint = json.loads((project / 'Scenes/main.scene.json').read_bytes())
        assert sum('forge.decal' in entity['components'] for entity in saved_paint['entities']) == 128
        assert mutate('forge_history_undo')['result']['changed'] and paint_count() == 1
        for field, value in [('width', 0), ('height', True), ('opacity', 1.01),
                             ('priority', 2147483648), ('angle_fade_end', .4), ('color_influence', -.01),
                             ('normal_influence', 1.01), ('roughness_influence', True)]:
            before_decal = discover()
            invalid = mutate('forge_property_set', arguments=dict(entity=decal,
                component='forge.decal', field=field, value=value))
            assert not invalid['ok'] and discover()['revision'] == before_decal['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        created_probe = mutate('forge_entity_create', arguments=dict(
            recipe='render.reflection_probe', name='Room reflections'))
        assert created_probe['ok'], created_probe
        probe = created_probe['result']['results'][0]['selected']
        probe_values = dict(texture='d4c60053-60c8-4456-8771-a7cf8182a902', intensity=2.5,
                            half_extent_x=.0001, priority=-7, rotation_degrees=90, diffuse_intensity=.75,
                            box_projection=False, runtime_capture=True, capture_size=64,
                            capture_interval=.5, capture_near=.2, capture_far=50., capture_layers=3)
        for field, value in probe_values.items():
            request = arguments(arguments=dict(entity=probe, component='forge.reflection_probe',
                                               field=field, value=value))
            changed_probe = client.tool('forge_property_set', **request)
            assert changed_probe['ok'], changed_probe
            assert client.tool('forge_property_set', **request)['result'] == changed_probe['result']
        for field, value in (('half_extent_x', .000099999999), ('priority', 2147483648),
                             ('enabled', 1), ('intensity', -1), ('diffuse_intensity', -1),
                             ('diffuse_intensity', 1000000.1), ('diffuse_intensity', True), ('diffuse_intensity', '1'),
                             ('runtime_capture', 1), ('capture_size', 17), ('capture_size', 512),
                             ('capture_interval', -1), ('capture_near', 0), ('capture_far', .1),
                             ('capture_layers', -1)):
            before_probe = discover()
            invalid = mutate('forge_property_set', arguments=dict(entity=probe,
                component='forge.reflection_probe', field=field, value=value))
            assert not invalid['ok'] and discover()['revision'] == before_probe['revision'], invalid
        assert mutate('forge_history_undo')['result']['changed']
        assert mutate('forge_history_redo')['result']['changed']
        saved = mutate('forge_document_save')
        assert saved['ok'] and not saved['result']['rediscover'], saved
        saved_path = project / 'Scenes/main.scene.json'
        saved_bytes = saved_path.read_bytes()
        saved_doc = json.loads(saved_bytes)
        saved_decal = next(entity for entity in saved_doc['entities'] if entity['id'] == decal)
        assert saved_decal['name'] == 'Wall paint'
        for field, value in decal_values.items():
            assert saved_decal['components']['forge.decal'][field] == value
        saved_probe = next(entity for entity in saved_doc['entities'] if entity['id'] == probe)
        assert saved_probe['name'] == 'Room reflections'
        for field, value in probe_values.items():
            assert saved_probe['components']['forge.reflection_probe'][field] == value
        assert saved_doc['rendering']['auto_exposure'] == dict(
            enabled=True, min_ev=-4, max_ev=6, low_percentile=.1,
            high_percentile=.9, target_gray=.2, speed_up=3, speed_down=4)
        assert saved_doc['rendering']['distance_fog'] == dict(
            enabled=True, color=[.1, .2, 2], start=5, end=60)
        assert saved_doc['rendering']['temporal_anti_aliasing'] == dict(enabled=True, stability=.9)
        assert saved_doc['rendering']['depth_of_field'] == dict(
            enabled=True, focus_distance=10, focus_range=4, blur_radius=6)
        assert saved_doc['rendering']['motion_blur'] == dict(enabled=True, shutter=.5, max_pixels=24)
        assert saved_doc['rendering']['ambient_occlusion'] == dict(enabled=True, radius=1, strength=.5, bias=.02, normal_influence=.7)
        assert saved_doc['rendering']['screen_space_reflections'] == dict(
            enabled=True, max_distance=20, thickness=.05, strength=.5, max_roughness=.4, roughness_filtering=True)
        assert saved_doc['entities'][0]['name'] == 'Saved agent cube'
        body = saved_doc['entities'][0]['components']['forge.physics_body_2d']
        assert body['one_way'] and abs(body['one_way_margin'] - .05) < 1e-6, body
        assert saved_doc['entities'][1]['components']['forge.character_controller_2d']['platform_carry']
        assert abs(saved_doc['entities'][1]['components']['forge.character_controller_2d']['crouch_height'] - .2) < 1e-6
        assert saved_doc['entities'][1]['components']['forge.character_controller_2d']['floor_stop_on_slope']
        assert not saved_doc['entities'][1]['components']['forge.character_controller_2d']['slide_on_ceiling']
        assert abs(saved_doc['entities'][1]['components']['forge.character_controller_2d']['step_height'] - .3) < 1e-6
        assert discover()['result']['target'] == original

        # Disk conflict/deletion is not silently overwritten, even for a clean draft.
        disk_edit = dict(saved_doc, entities=[])
        saved_path.write_text(json.dumps(disk_edit), encoding='utf-8')
        failed(mutate('forge_document_save'), 'document_error')
        assert json.loads(saved_path.read_bytes()) == disk_edit
        saved_path.unlink()
        failed(mutate('forge_document_save'), 'document_error')
        assert not saved_path.exists()
        saved_path.write_bytes(saved_bytes)

        # Save As delegates copy identity/history semantics to SceneDocument.
        copied_args = arguments(path='Scenes/é copy.scene.json')
        # Lose the original response after sending a complete file operation.
        request = dict(api=1, method='document.save_as', **copied_args)
        sequence = request.pop('sequence')
        wire = dict(transport=1, token=config['token'], id='lost-file-ack',
                    request=request, sequence=sequence)
        with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
            channel.sendall((json.dumps(wire) + '\n').encode())
        copy_path = project / 'Scenes/é copy.scene.json'
        wait_for(copy_path.exists)
        copied = client.tool('forge_document_save_as', **copied_args)
        assert copied['ok'] and copied['result']['rediscover'], copied
        copy_bytes = copy_path.read_bytes()
        copy_doc = json.loads(copy_bytes)
        assert copy_doc['asset_id'] != saved_doc['asset_id']
        assert copy_doc['entities'][0]['id'] != saved_doc['entities'][0]['id']
        current = discover()
        assert current['result']['target']['session'] != original['session']
        assert current['result']['target']['id'] == copy_doc['asset_id']
        assert not mutate('forge_history_undo')['result']['changed']
        # Replaying an old file mutation after retargeting returns the original receipt.
        replay = client.tool('forge_document_save_as', **copied_args)
        assert replay['result'] == copied['result'] and copy_path.read_bytes() == copy_bytes
        failed(client.tool('forge_document_save_as', **dict(copied_args, path='Scenes/conflict.scene.json')),
               'sequence_conflict')
        assert not (project / 'Scenes/conflict.scene.json').exists()
        failed(client.tool('forge_document_save', **dict(arguments(), target=original)), 'wrong_target')
        failed(client.tool('forge_document_save', **dict(arguments(), expected_revision=0)),
               'stale_revision')
        before = status()
        for path in ('../escape.scene.json', str(root / 'escape.scene.json'),
                     '.forge/recovery/invalid.scene.json', 'forge.project.json',
                     'Scenes/invalid.txt', 'Scenes/bad\x00.scene.json',
                     'Scenes/back\\slash.scene.json', 'Scenes/stream:bad.scene.json'):
            failed(mutate('forge_document_save_as', path=path), 'document_error')
        failed(mutate('forge_document_save_as', path='Scenes/main.scene.json'), 'document_error')
        assert status() == before and saved_path.read_bytes() == saved_bytes
        assert not (root / 'escape.scene.json').exists()

        # Shared native service also rejects unknown and incorrectly typed arguments.
        for extra in (dict(path=False), dict(path=''), dict(path='x' * 1025),
                      dict(path='Scenes/test.scene.json', unexpected=True)):
            args = arguments(**extra)
            sequence = args.pop('sequence')
            failed(native(config, dict(api=1, method='document.save_as', **args), sequence),
                   'invalid_arguments')
        args = arguments(discard='yes')
        sequence = args.pop('sequence')
        failed(native(config, dict(api=1, method='document.new', **args), sequence),
               'invalid_arguments')

        # Editor work gates do not queue a later save/switch behind the agent's back.
        for marker in ('block_all', 'file_busy', 'build_busy', 'gesture'):
            (root / marker).touch()
            failed(mutate('forge_document_new'), 'busy' if marker == 'gesture' else 'document_error')
            assert status()['scene_path'] == before['scene_path']
            (root / marker).unlink()
        (root / 'source_draft').touch()
        failed(mutate('forge_document_open', path='Scenes/main.scene.json', discard=True),
               'document_error')
        assert mutate('forge_document_save')['ok']
        (root / 'source_draft').unlink()

        # Dirty-switch loss is opt-in, with normal recovery preservation on failure.
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='Unsaved'))['ok']
        (root / 'autosave').touch()
        wait_for(lambda: status()['recovery_available'])
        recovery = list((project / '.forge/recovery').glob('*.json'))
        assert len(recovery) == 1
        recovery_bytes = recovery[0].read_bytes()
        failed(mutate('forge_document_new'), 'document_error')
        failed(mutate('forge_document_open', path='Scenes/main.scene.json'), 'document_error')
        malformed = project / 'Scenes/malformed.scene.json'
        malformed.write_text('{bad', encoding='utf-8')
        generation = status()['generation']
        failed(mutate('forge_document_open', path='Scenes/malformed.scene.json', discard=True),
               'document_error')
        assert status()['generation'] == generation and status()['dirty']
        assert recovery[0].read_bytes() == recovery_bytes
        opened = mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)
        assert opened['ok'] and opened['result']['rediscover'], opened
        assert discover()['result']['target']['id'] == saved_doc['asset_id']
        assert not status()['dirty']
        assert not recovery[0].exists()
        assert mutate('forge_document_new')['ok']
        assert status()['scene_path'] is None and not status()['persisted']
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='First save'))['ok']
        untitled_target = discover()['result']['target']
        failed(mutate('forge_document_save'), 'document_error')
        first = mutate('forge_document_save_as', path='Scenes/first.scene.json')
        assert first['ok'] and first['result']['rediscover'], first
        assert discover()['result']['target']['id'] == untitled_target['id']
        failed(client.tool('forge_document_save', **dict(arguments(), target=untitled_target)),
               'wrong_target')
        assert mutate('forge_history_undo')['result']['changed']

        # Symlink escapes use the existing canonical project containment checks.
        try:
            (project / 'Scenes/outside').symlink_to(root / 'Other/Scenes', target_is_directory=True)
        except OSError:
            pass  # Windows may prohibit symlinks without developer mode/privilege.
        else:
            failed(mutate('forge_document_save_as', path='Scenes/outside/escaped.scene.json'),
                   'document_error')
            assert not (root / 'Other/Scenes/escaped.scene.json').exists()

        # A file grant survives UI scene changes; old targets cannot mutate the new document.
        previous = discover()['result']['target']
        (root / 'ui_new').touch()
        wait_for(lambda: not (root / 'ui_new').exists())
        wait_for(lambda: discover()['result']['target'] != previous)
        failed(client.tool('forge_document_save', **dict(arguments(), target=previous)), 'wrong_target')
        assert config['token'] not in json.dumps(CAPTURE)
        assert config['token'] not in ''.join(client.stderr)
        (root / 'ui_project').touch()
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

if FOG_VOLUMES_ONLY:
    for legacy in (False, True):
        with editor('read') as (root, process, config), Client(config, legacy) as client:
            state = client.tool('forge_discover')
            assert not any(t['name'] == 'forge_property_set'
                           for t in client.request('tools/list')['result']['tools'])
            denied = native(config, dict(api=1, method='scene.apply', target=state['result']['target'],
                expected_revision=state['revision'], commands=[dict(operation='property.set', arguments=dict(
                    entity='8a4fe69d-5818-4cc6-b1ed-8510642d9e6f', component='forge.fog_volume',
                    field='density', value=.5))]), state['next_sequence'])
            assert not denied['ok'] and client.tool('forge_discover')['revision'] == state['revision']
    write_capture(sys.argv[3])
    print('MCP Fog Volumes: both framings, permissions, schema, replay, revisions, history and save/reopen passed')
    raise SystemExit(0)

if SEQUENCE_CLIPS_ONLY or NESTED_SEQUENCES_ONLY:
    with editor('read') as (root, process, config), Client(config) as client:
        state = client.tool('forge_discover')
        assert not any(t['name'] == 'forge_property_set_many'
                       for t in client.request('tools/list')['result']['tools'])
        denied = native(config, dict(api=1, method='scene.apply', target=state['result']['target'],
            expected_revision=state['revision'], commands=[dict(operation='property.set', arguments=dict(
                entity='8a4fe69d-5818-4cc6-b1ed-8510642d9e6f',
                component='forge.animation_sequence' if NESTED_SEQUENCES_ONLY else 'forge.transform_animator',
                field='sequence_start_seconds' if NESTED_SEQUENCES_ONLY else 'sequence_next_start_seconds', value=.5))]), state['next_sequence'])
        assert not denied['ok'] and client.tool('forge_discover')['revision'] == state['revision']
    write_capture(sys.argv[3])
    print(('MCP nested sequences' if NESTED_SEQUENCES_ONLY else 'MCP sequence clips') +
          ': both framings, schema, permissions, replay, revisions, history, save/reopen and graceful exits passed')
    raise SystemExit(0)

if MESH_SCATTER_ONLY:
    with editor('read') as (root, process, config), Client(config) as client:
        catalog = client.request('tools/list')['result']['tools']
        assert not any(t['name'] == 'forge_mesh_scatter' for t in catalog)
        state = client.tool('forge_discover')
        denied = native(config, dict(api=1, method='scene.apply', target=state['result']['target'],
            expected_revision=state['revision'], commands=[dict(operation='mesh.scatter', arguments={})]),
            state['next_sequence'])
        assert not denied['ok'] and client.tool('forge_discover')['revision'] == state['revision']
    write_capture(sys.argv[3])
    print('MCP mesh scatter: both framings, schema, permissions, replay, revisions, history, save and graceful exits passed')
    raise SystemExit(0)

if OCCLUSION_CULLING_ONLY:
    with editor('read') as (root, process, config), Client(config) as client:
        catalog = client.request('tools/list')['result']['tools']
        assert not any(t['name'] == 'forge_scene_rendering_set' for t in catalog)
        state = client.tool('forge_discover')
        denied = native(config, dict(api=1, method='scene.apply', target=state['result']['target'],
            expected_revision=state['revision'], commands=[dict(operation='scene.rendering.set',
            arguments=dict(occlusion_culling=dict(enabled=True)))]), state['next_sequence'])
        assert not denied['ok'] and client.tool('forge_discover')['revision'] == state['revision']
    write_capture(sys.argv[3])
    print('MCP occlusion Scene schema, revisions, replay, history, saving and read-only denial passed')
    raise SystemExit(0)

if MORPHOLOGICAL_AA_ONLY:
    for legacy in (False, True):
        with editor('read') as (root, process, config), Client(config, legacy) as client:
            state = client.tool('forge_discover')
            assert not any(t['name'] == 'forge_scene_rendering_set' for t in client.request('tools/list')['result']['tools'])
            denied = native(config, dict(api=1, method='scene.apply', target=state['result']['target'],
                expected_revision=state['revision'], commands=[dict(operation='scene.rendering.set',
                    arguments=dict(morphological_anti_aliasing=dict(enabled=True)))]), state['next_sequence'])
            assert not denied['ok'] and client.tool('forge_discover')['revision'] == state['revision']
    write_capture(sys.argv[3])
    print('MCP morphological AA: both framings, shared schemas, permission denial, replay/revisions, explicit method switch, history/save and graceful exits passed')
    raise SystemExit(0)

if FAST_AA_ONLY:
    write_capture(sys.argv[3])
    print('MCP fast AA: both framings, schema, replay, history, save and graceful native exits passed')
    raise SystemExit(0)

for mode, count in (('read', 5), ('edit', 39)):
    with editor(mode) as (root, process, config), Client(config) as client:
        catalog = client.request('tools/list')['result']['tools']
        assert len(catalog) == count
        assert not any(item['name'].startswith('forge_document_') for item in catalog)
        state = client.tool('forge_discover')
        assert client.tool('forge_project_read', target=state['result']['target'])['ok']
        if mode == 'read':
            assert not any(item['name'] == 'forge_scene_rendering_set' for item in catalog)
            denied = native(config, dict(api=1, method='scene.apply',
                target=state['result']['target'], expected_revision=state['revision'],
                commands=[dict(operation='scene.rendering.set', arguments=dict(
                    auto_exposure=dict(enabled=True)))]), state['next_sequence'])
            assert not denied['ok']
            assert client.tool('forge_discover')['revision'] == state['revision']
        request = dict(api=1, method='document.save', target=state['result']['target'],
                       expected_revision=state['revision'])
        failed(native(config, request, state['next_sequence']), 'unknown_method')
        assert client.request('tools/call', dict(name='forge_document_save', arguments={}))['error']['code'] == -32602
        (root / 'ui_new').touch()
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

if os.name == 'posix':
    with editor() as (root, process, config), Client(config) as client:
        # Replacing the lock pathname invalidates the owner's held inode identity.
        # Windows denies this replacement while the exclusive handle is open.
        lock = root / 'Game/.forge/writer.lock'
        lock.unlink()
        lock.write_text('replacement', encoding='utf-8')
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

if len(sys.argv) > 3:
    write_capture(sys.argv[3])
print('MCP documents: owner saves, copy/first-save identities, disk conflicts, scope, '
      'dirty/busy gates, leases, fresh targets, replay and revocation passed')
