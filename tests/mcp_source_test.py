"""Shared editor source/history/publication owners through both MCP envelopes."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import wave
from mcp_test_client import CAPTURE, Client, failed, write_capture, finish_host
from mcp_prefab_workflow import check_prefab_workflow
from mcp_terrain_foliage_workflow import check_terrain_foliage_workflow
from mcp_render_texture_workflow import check_render_texture_workflow
from mcp_paint_texture_workflow import check_paint_texture_workflow
from mcp_animation_retarget_workflow import check_animation_retarget_workflow
from mcp_animation_graph_workflow import check_animation_graph_workflow
from mcp_lightmap_graph_workflow import check_lightmap_graph_workflow
from mcp_sequence_property_sources import check_sequence_property_sources
from mcp_planar_mesh_workflow import check_planar_mesh_workflow
from mcp_mesh_foliage_workflow import check_mesh_foliage_workflow

TOOLS, HOST, WORKER = map(str, map(Path, sys.argv[1:4]))
SEQUENCE_PROPERTY_SOURCES_ONLY = '--sequence-property-sources-only' in sys.argv[5:]
SEQUENCE_BINDING_SOURCES_ONLY = '--sequence-binding-sources-only' in sys.argv[5:]
TERRAIN_FOLIAGE_ONLY = "--terrain-foliage-only" in sys.argv[5:]
PREFAB_ONLY = "--prefab-only" in sys.argv[5:]
# Instrumented native admission can exceed the unchanged production bridge
# deadline for the multi-MiB boundary fixture. Normal CTest always includes it.
SKIP_LARGE_PREFAB_READ = "--skip-large-prefab-read" in sys.argv[5:]
assert not SKIP_LARGE_PREFAB_READ or PREFAB_ONLY
COLLISION_ONLY = "--tile-collision-only" in sys.argv[5:]
ATLAS_ONLY = "--atlas-only" in sys.argv[5:]
SPRITE_GRAPH_ONLY = "--sprite-graph-only" in sys.argv[5:]
LIGHTMAP_GRAPH_ONLY = "--lightmap-graph-only" in sys.argv[5:]
NAVIGATION_BEHAVIOR_ONLY = "--navigation-behavior-only" in sys.argv[5:]
UI_LOCALIZATION_ONLY = "--ui-localization-only" in sys.argv[5:]
UI_VALUES_ONLY = "--ui-values-only" in sys.argv[5:]
ANIMATION_RETARGET_ONLY = "--animation-retarget-only" in sys.argv[5:]
ANIMATION_GRAPH_ONLY = "--animation-graph-only" in sys.argv[5:]
PAINT_TEXTURE_ONLY = "--paint-texture-only" in sys.argv[5:]
RENDER_TEXTURE_ONLY = "--render-texture-only" in sys.argv[5:]
APPLICATION_STATES_ONLY = "--application-states-only" in sys.argv[5:]
PLANAR_MESH_ONLY = "--planar-mesh-only" in sys.argv[5:]
MESH_FOLIAGE_ONLY = "--mesh-foliage-only" in sys.argv[5:]


@contextmanager
def editor(mode='edit'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-sources-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), WORKER, mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, process, json.loads(line)
        finally:
            finish_host(process, root)


def check_audio_settings(read, mutate, root):
    # Typed importer settings use the shared draft, schema and native worker.
    with wave.open(str(root / 'Game/Assets/Settings.wav'), 'wb') as wav:
        wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(8000)
        wav.writeframes(b'\x01\x00\x02\x00' * 8000)
    assert mutate('forge_asset_settings_open', editor='audio_import', path='Assets/Settings.wav')['ok']
    schema = read('forge_asset_settings_schema', editor='audio_import')
    assert {'force_mono', 'normalize_peak', 'playback_storage'} <= {rule['key'] for rule in schema['rules']}
    initial = read('forge_asset_settings_status', editor='audio_import')
    def import_edit(tool, **extra):
        current = read('forge_asset_settings_status', editor='audio_import')
        return mutate(tool, editor='audio_import', session=current['session'], state=current['state'], **extra)
    # Invalid second field must not adopt the otherwise valid first field.
    invalid = import_edit('forge_asset_settings_edit', changes=[dict(key='force_mono', value=True),
                          dict(key='missing_setting', value=True)])
    assert not invalid['ok']
    assert read('forge_asset_settings_status', editor='audio_import')['state'] == initial['state']
    for invalid_storage in ('unlimited', True, 1):
        assert not import_edit('forge_asset_settings_edit', changes=[
            dict(key='playback_storage', value=invalid_storage)])['ok']
    assert read('forge_asset_settings_status', editor='audio_import')['state'] == initial['state']
    assert import_edit('forge_asset_settings_edit', changes=[dict(key='force_mono', value=True),
        dict(key='playback_storage', value='stream')])['ok']
    failed(mutate('forge_asset_settings_publish', editor='audio_import', session=initial['session'],
                  state=initial['state']), 'stale_import')
    assert import_edit('forge_asset_settings_publish')['ok']
    end = time.monotonic() + 35
    while True:
        imported = read('forge_asset_settings_status', editor='audio_import')
        if not imported['job']: break
        assert time.monotonic() < end, imported
        time.sleep(.02)
    assert not imported['dirty'] and not imported['diagnostic'], imported
    assert imported['effective']['force_mono']
    assert imported['effective']['playback_storage'] == 'stream'
    clips = read('forge_asset_search', include_engine=False, type='audio_clip', text='Settings.wav')['assets']
    clip = next(row for row in clips if row['source'] == 'Assets/Settings.wav')
    selected_audio = read('forge_asset_read', asset=clip['asset'])['metadata']['forge.audio']
    assert selected_audio['playback_storage'] == 'stream'
    assert selected_audio['sample_rate'] == 48000 and selected_audio['frames'] == 48000
    assert import_edit('forge_asset_settings_close')['ok']

    # MP3/FLAC share the existing typed settings owner, revision checks and worker.
    fixtures = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Assets/AudioCodecs'
    for extension in ('flac', 'mp3'):
        source_name = 'Stereo.' + extension
        (root / 'Game/Assets' / source_name).write_bytes((fixtures / source_name).read_bytes())
        assert mutate('forge_asset_settings_open', editor='audio_import', path='Assets/' + source_name)['ok']
        initial = read('forge_asset_settings_status', editor='audio_import')
        assert initial['effective']['force_mono'] is False
        assert import_edit('forge_asset_settings_edit', changes=[dict(key='force_mono', value=True)])['ok']
        assert import_edit('forge_asset_settings_publish')['ok']
        end = time.monotonic() + 35
        while True:
            imported = read('forge_asset_settings_status', editor='audio_import')
            if not imported['job']: break
            assert time.monotonic() < end, imported
            time.sleep(.02)
        assert not imported['dirty'] and not imported['diagnostic'], imported
        assert imported['effective']['force_mono']
        assert import_edit('forge_asset_settings_close')['ok']



for legacy in (False, True):
    with editor('read') as (root, process, config), Client(config, legacy) as client:
        readonly_tools = client.request('tools/list')['result']['tools']
        names = {tool['name'] for tool in readonly_tools}
        preparation = next(t for t in readonly_tools if t['name'] == 'forge_foliage_terrain_prepare')
        assert preparation['annotations']['readOnlyHint']
        state = client.tool('forge_discover')
        missing = '00000000-0000-4000-8000-000000000099'
        failed(client.tool('forge_foliage_terrain_prepare', target=state['result']['target'],
            terrain=missing, arguments=dict(entity=missing, name='Read-only preflight',
                center=dict(x=0, y=0, z=0), radius=1, spacing=0, count=1, seed=1,
                scale_min=1, scale_max=1, yaw_min=0, yaw_max=0)), 'invalid_surface')
        if MESH_FOLIAGE_ONLY:
            preparation = next(t for t in readonly_tools if t['name'] == 'forge_foliage_surface_prepare')
            assert preparation['annotations']['readOnlyHint']
            failed(client.tool('forge_foliage_surface_prepare', target=state['result']['target'],
                surface=missing, arguments=dict(entity=missing, name='Read-only Mesh preflight',
                    center=dict(x=0, y=0, z=0), radius=1, spacing=0, count=1, seed=1,
                    scale_min=1, scale_max=1, yaw_min=0, yaw_max=0)), 'invalid_surface')
        stroke_preparation = next(t for t in readonly_tools if t['name'] == 'forge_foliage_stroke_prepare')
        assert stroke_preparation['annotations']['readOnlyHint'] and 'forge_mesh_stroke' not in names
        failed(client.tool('forge_foliage_stroke_prepare', target=state['result']['target'],
            surface=missing, surface_profile='static_mesh',
            arguments=dict(entity=missing, name='Read-only stroke', dabs=[dict(
                center=dict(x=0, y=0, z=0), radius=1, spacing=0, count=1, seed=1,
                scale_min=1, scale_max=1, yaw_min=0, yaw_max=0)])), 'invalid_surface')
        assert client.tool('forge_discover')['revision'] == state['revision']
        assert 'forge_asset_document_types' in names
        assert {'forge_asset_skeleton_joints', 'forge_asset_animation_retarget_catalog',
                'forge_asset_animation_graph_catalog'} <= names
        assert 'forge_prefab_source_read' in names
        assert not {'forge_prefab_variant_create', 'forge_prefab_duplicate', 'forge_prefab_source_rebase', 'forge_prefab_source_revert'} & names
        assert not any('forge_asset_document_' + name in names for name in ('create', 'patch', 'save', 'paint', 'stroke', 'fill', 'copy_pixels', 'cancel_pixels', 'close', 'tile_collision', 'tile_animation', 'tile_autotile'))
        if PLANAR_MESH_ONLY:
            preview = client.tool('forge_asset_document_template', target=state['result']['target'],
                                  editor='editable_mesh', mesh_profile='planar_quad', mesh_size=[2, 1])
            assert preview['ok'] and len(preview['result']['vertices']) == 4, preview

    with editor() as (root, process, config), Client(config, legacy) as client:
        declared_tools = client.request('tools/list')['result']['tools']
        tool_names = {tool['name'] for tool in declared_tools}
        patch_description = next(tool['description'] for tool in declared_tools
                                 if tool['name'] == 'forge_asset_document_patch')
        assert all(word in patch_description for word in ('Source7', 'int64', 'uint64', 'Source8', 'member_path', 'Source9', 'element_path'))
        assert {'forge_asset_document_create', 'forge_asset_settings_publish', 'forge_prefab_apply_commit', 'forge_asset_document_tile_collision', 'forge_asset_document_tile_animation', 'forge_asset_document_tile_autotile'} <= tool_names
        def discovery():
            result = client.tool('forge_discover')
            assert result['ok'], result
            return result

        def read(tool, _expected_error=None, **extra):
            result = client.tool(tool, target=discovery()['result']['target'], **extra)
            if _expected_error is not None:
                failed(result, _expected_error)
                return result
            assert result['ok'], result
            return result['result']

        def mutate(tool, **extra):
            state = discovery()
            return client.tool(tool, target=state['result']['target'],
                               expected_revision=state['revision'], sequence=state['next_sequence'], **extra)

        if sys.argv[5:] == ['--audio-streaming-only']:
            check_audio_settings(read, mutate, root)
            continue

        if PREFAB_ONLY:
            check_prefab_workflow(read, mutate, failed, root,
                                  large_read_budget=not SKIP_LARGE_PREFAB_READ)
            continue

        def status(editor):
            return read('forge_asset_document_status', editor=editor)

        def edit(tool, editor, **extra):
            state = status(editor)
            return mutate(tool, editor=editor, document_session=state['session'],
                          document_revision=state['revision'], **extra)

        def content(editor):
            state = status(editor)
            return read('forge_asset_document_read', editor=editor,
                        document_session=state['session'])['value']

        kinds = read('forge_asset_document_types')['editors']
        assert len(kinds) == 20 and all(not item['status']['open'] for item in kinds), kinds
        if MESH_FOLIAGE_ONLY:
            check_mesh_foliage_workflow(read, mutate, edit, status, content, root, declared_tools, client, discovery)
            print(('legacy' if legacy else 'current') + ': Mesh foliage projection, source selection, slope, history, save and read-only preflight passed')
            continue
        if PLANAR_MESH_ONLY:
            check_planar_mesh_workflow(read, mutate, edit, status, content, root, declared_tools)
            print(('legacy' if legacy else 'current') + ': planar Mesh schema, templates, atomic creation, history, publication, save/reopen, revisions and scope passed')
            continue
        if SEQUENCE_PROPERTY_SOURCES_ONLY:
            check_sequence_property_sources(read, mutate, edit, status, content, root)
            continue
        if SEQUENCE_BINDING_SOURCES_ONLY:
            fixture = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Assets/Sequences'
            templates = [(name, json.loads((fixture/(name + '.animation.json')).read_text()))
                         for name in ('SurfaceFirst', 'SurfaceNext', 'FieldsFirst', 'FieldsNext')]
            check_sequence_property_sources(read, mutate, edit, status, content, root, templates)
            continue
        if ANIMATION_GRAPH_ONLY:
            check_animation_graph_workflow(read, mutate, edit, status, content, failed,
                                           root, client, discovery)
            continue
        if ANIMATION_RETARGET_ONLY:
            check_animation_retarget_workflow(read, mutate, edit, status, content, failed,
                                               root, client, discovery)
            continue
        if PAINT_TEXTURE_ONLY:
            check_paint_texture_workflow(read, mutate, edit, status, content, failed, root, client, discovery)
            continue
        if LIGHTMAP_GRAPH_ONLY:
            check_lightmap_graph_workflow(read, mutate, edit, status, content, failed, root)
            continue
        output_catalog = read('forge_asset_render_texture_catalog')
        assert output_catalog['editor'] == 'render_texture' and output_catalog['asset_type'] == 'render_texture'
        assert output_catalog['limits']['outputs'] == 4 and output_catalog['limits']['maximum_dimension'] == 2048
        assert output_catalog['material']['source_version'] == 3
        localization_catalog = read('forge_asset_localization_catalog')
        assert localization_catalog['editor'] == 'ui_localization'
        assert localization_catalog['limits']['locales'] == 8
        assert localization_catalog['command'] == dict(name='forge.locale', value='fr')
        assert 'tr(forge_locale)' in localization_catalog['binding']
        state_catalog = read('forge_asset_application_states_catalog')
        assert state_catalog['editor'] == 'application_states'
        assert state_catalog['component'] == 'forge.application_states'
        assert state_catalog['limits']['domains'] == 16 and state_catalog['limits']['states'] == 512
        behavior_catalog = read('forge_asset_behavior_catalog')
        assert behavior_catalog['editor'] == 'navigation_behavior'
        assert {a['kind'] for a in behavior_catalog['actions']} == {'wait', 'move_to_point', 'follow_target'}
        assert len(behavior_catalog['conditions']) == 5 and behavior_catalog['limits']['outgoing'] == 8
        assert behavior_catalog['source_versions'] == [1, 2]
        assert set(behavior_catalog['profiles']) == {'state_graph', 'tree'}
        assert behavior_catalog['tree']['limits'] == dict(nodes=32, children=8, depth=8)
        assert {n['kind'] for n in behavior_catalog['tree']['nodes']} == {
            'sequence', 'reactive_sequence', 'selector', 'wait', 'move_to_point',
            'follow_target', 'target_near', 'target_far', 'target_missing'}
        graph_types = read('forge_asset_graph_catalog')['nodes']
        assert len(graph_types) > 10
        assert read('forge_asset_graph_node', schema='constant')['outputs']
        sprite_output = read('forge_asset_graph_node', schema='sprite_output')
        assert sprite_output['node']['data']['tint'] and not sprite_output['outputs']
        assert sprite_output['inputs'][0]['key'] == 'color'
        schemas = {tool['name']: tool['inputSchema']['properties'] for tool in declared_tools}
        assert schemas['forge_asset_document_template']['domain']['enum'] == ['mesh', 'sprite']
        assert schemas['forge_asset_document_create']['domain']['enum'] == ['mesh', 'sprite']
        assert not {'domain', 'textures', 'tile_set', 'patch'} & schemas['forge_asset_document_open'].keys()
        wrong_domain_owner = client.tool('forge_asset_document_template',
                                        target=discovery()['result']['target'],
                                        editor='material', domain='sprite')
        assert not wrong_domain_owner['ok'], wrong_domain_owner
        invalid_domain = client.tool('forge_asset_document_template',
                                     target=discovery()['result']['target'],
                                     editor='material.graph', domain='unknown')
        assert not invalid_domain['ok'], invalid_domain
        wrong_creation = mutate('forge_asset_document_create', editor='material',
                                path='Assets/Rejected-domain.material.json', domain='sprite')
        assert not wrong_creation['ok'] and not (root / 'Game/Assets/Rejected-domain.material.json').exists()
        # A Tile Set source can be authored before its dependent Tile Map.
        kinds.sort(key=lambda item: item['editor'] == 'tile_map')
        if COLLISION_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] in ('tile_set', 'tile_map')]
        if ATLAS_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'sprite_atlas']
        if SPRITE_GRAPH_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'material.graph']
        if NAVIGATION_BEHAVIOR_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'navigation_behavior']
        if UI_LOCALIZATION_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'ui_localization']
        if APPLICATION_STATES_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'application_states']
        if UI_VALUES_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'animation_timeline']
        if RENDER_TEXTURE_ONLY:
            kinds = [kind for kind in kinds if kind['editor'] == 'render_texture']
        atlas_image = None
        if not COLLISION_ONLY and not SPRITE_GRAPH_ONLY and not NAVIGATION_BEHAVIOR_ONLY and not UI_LOCALIZATION_ONLY and not APPLICATION_STATES_ONLY and not UI_VALUES_ONLY and not RENDER_TEXTURE_ONLY:
            # Original owned raster, imported only through the advertised MCP
            # settings/publication workflow; no catalog fixture injection.
            header = bytearray(18)
            header[2] = 2; header[12] = 2; header[14] = 2
            header[16] = 24; header[17] = 32
            (root / 'Game/Assets/AtlasInput.tga').write_bytes(header + bytes([23, 12, 211]) * 4)
            assert mutate('forge_asset_settings_open', editor='texture_import', path='Assets/AtlasInput.tga')['ok']
            selected = read('forge_asset_settings_status', editor='texture_import')
            assert mutate('forge_asset_settings_publish', editor='texture_import', session=selected['session'], state=selected['state'])['ok']
            deadline = time.monotonic() + 20
            while True:
                selected = read('forge_asset_settings_status', editor='texture_import')
                if not selected['job']:
                    break
                assert time.monotonic() < deadline, selected
                time.sleep(.02)
            assert not selected['dirty'] and not selected['diagnostic'], selected
            atlas_image = selected['asset']
            assert mutate('forge_asset_settings_close', editor='texture_import', session=selected['session'], state=selected['state'])['ok']
        image_material = '00000000-0000-4000-8000-000000000021'
        def save_published(owner):
            result = edit('forge_asset_document_save', owner)
            assert result['ok'], result
            end = time.monotonic() + 35
            while True:
                settled = status(owner)
                if not settled['save_requested'] and not settled['publishing']:
                    break
                assert time.monotonic() < end, settled
                time.sleep(.02)
            assert not settled['dirty'] and not settled['unpublished'] and not settled['diagnostic'], settled
            return settled
        if TERRAIN_FOLIAGE_ONLY:
            check_terrain_foliage_workflow(read, mutate, edit, content, save_published,
                                           discovery, client, root)
            continue
        if COLLISION_ONLY:
            # FORGE's own unlit image fixture references the immutable engine Checker
            # texture, so every dependent source is created/published through MCP.
            fixture = Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery/Assets/Physics2D/Solid.material.json'
            overrides = json.loads(fixture.read_text(encoding='utf-8'))['overrides']
            overrides['textures']['baseColorTexture']['asset'] = '619babf9-df8f-4d18-91da-2e8096bd9464'
            result = mutate('forge_asset_document_create', editor='material', path='Assets/MCP-TileImage.material.json')
            assert result['ok'], result
            assert edit('forge_asset_document_patch', 'material',
                        patch=[dict(op='add', path='/overrides', value=overrides)])['ok']
            image_material = content('material')['asset_id']
            save_published('material')
            assert edit('forge_asset_document_close', 'material')['ok']
        set_id = None
        render_texture_id = None
        for kind in kinds:
            owner = kind['editor']
            extra = dict(tile_set=set_id) if owner == 'tile_map' else dict(textures=[atlas_image]) if owner == 'sprite_atlas' else {}
            if owner == 'material.graph':
                mesh = read('forge_asset_document_template', editor=owner)
                assert any(n['type'] == 'surface' for n in mesh['graph']['nodes'])
                extra = dict(domain='sprite')
            template = read('forge_asset_document_template', editor=owner, **extra)
            assert isinstance(template, dict) and template['asset_id']
            path = 'Assets/MCP-' + owner.replace('.', '-') + kind['suffix']
            result = mutate('forge_asset_document_create', editor=owner, path=path, **extra)
            assert result['ok'], result
            initial = status(owner)
            if owner == 'render_texture':
                render_texture_id = initial['asset']
            assert initial['revision'] == 1 and initial['unpublished']
            if owner == 'tile_set':
                set_id = initial['asset']
            before = content(owner)
            if owner == 'material.graph':
                output = next(n for n in before['graph']['nodes'] if n['type'] == 'sprite_output')
                image = next(n for n in before['graph']['nodes'] if n['type'] == 'texture2d')
                assert output['data']['image'] == image['data']['key'] and output['data']['tint']
            key, value = 'mcp_note', {'owner': owner, 'keep': [1, 2, 3]}
            if owner == 'particle_effect': key, value = 'spawn_rate', before['spawn_rate'] * .5
            if owner == 'terrain': key, value = 'tile_size', before['tile_size'] * 2
            patch = [dict(op='add', path='/' + key, value=value)]
            result = edit('forge_asset_document_patch', owner, patch=patch, label='Agent source edit')
            assert result['ok'], result
            changed = status(owner)
            assert changed['revision'] == 2 and content(owner)[key] == value
            assert edit('forge_asset_document_undo', owner)['ok']
            assert content(owner) == before
            assert edit('forge_asset_document_redo', owner)['ok']
            assert content(owner)[key] == value
            current = status(owner)
            stale = mutate('forge_asset_document_patch', editor=owner,
                           document_session=current['session'], document_revision=1, patch=patch)
            failed(stale, 'stale_document')
            failed(mutate('forge_asset_document_patch', editor=owner,
                          document_session=current['session'] + 1000,
                          document_revision=current['revision'], patch=patch), 'wrong_document')
            # Identity and domain validation failures leave draft/history untouched.
            snapshot = content(owner)
            invalid = edit('forge_asset_document_patch', owner,
                           patch=[dict(op='replace', path='/asset_id', value='00000000-0000-4000-8000-000000000001')])
            assert not invalid['ok'], invalid
            assert content(owner) == snapshot and status(owner)['revision'] == current['revision']
            failed(edit('forge_asset_document_close', owner), 'unsaved_document')
            if owner == 'material.graph':
                snapshot = content(owner)
                index = next(i for i, n in enumerate(snapshot['graph']['nodes']) if n['type'] == 'sprite_output')
                assert edit('forge_asset_document_patch', owner, patch=[dict(op='replace', path=f'/graph/nodes/{index}/data/tint', value=False)])['ok']
                assert not content(owner)['graph']['nodes'][index]['data']['tint']
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == snapshot
                assert edit('forge_asset_document_redo', owner)['ok']
                assert not content(owner)['graph']['nodes'][index]['data']['tint']
            if owner == 'sprite_atlas':
                result = edit('forge_asset_document_patch', owner, patch=[
                    dict(op='replace', path='/frames/0/id', value=2**32 - 1),
                    dict(op='replace', path='/next_id', value=2**32)])
                assert result['ok'], result
                assert content(owner)['frames'][0]['id'] == 2**32 - 1
                assert edit('forge_asset_document_undo', owner)['ok']
                assert content(owner)['frames'][0]['id'] == 0 and content(owner)['next_id'] == 2**32
                assert edit('forge_asset_document_redo', owner)['ok']
                assert content(owner)['frames'][0]['id'] == 2**32 - 1
            if owner == 'tile_set':
                original = content(owner)
                tile = 2**64 - 2
                rows = [dict(op='replace', path='/next_id', value=2**64 - 1),
                        dict(op='add', path='/images/-', value=dict(id=1, name='Image',
                             material=image_material,
                             grid=dict(columns=4 if COLLISION_ONLY else 1, rows=1, margin_x=0, margin_y=0, spacing_x=0, spacing_y=0))),
                        dict(op='add', path='/tiles/-', value=dict(id=tile, name='Ramp', image=1, frame=0))]
                assert edit('forge_asset_document_patch', owner, patch=rows)['ok']
                seeded = content(owner)
                collision = dict(kind='convex', points=[[0, 0], [1, 0], [1, 1]])
                old = status(owner)
                result = edit('forge_asset_document_tile_collision', owner, tile=tile, collision=collision)
                assert result['ok'], result
                promoted = content(owner)
                assert promoted['version'] == 2 and promoted['tiles'][0]['collision'] == collision
                failed(mutate('forge_asset_document_tile_collision', editor=owner,
                              document_session=old['session'], document_revision=old['revision'],
                              tile=tile, collision=dict(kind='none')), 'stale_document')
                for bad in (dict(kind='convex', points=[[0, 0], [0, 1], [1, 0]]),
                            dict(kind='convex', points=[[0, 0], [1, 1], [0, 1], [1, 0]]),
                            dict(kind='convex', points=[[0, 0], [2, 0], [0, 1]]),
                            dict(kind='none', points=[[0, 0], [1, 0], [0, 1]]),
                            dict(kind='convex'), dict(kind='circle')):
                    assert not edit('forge_asset_document_tile_collision', owner, tile=tile, collision=bad)['ok']
                    assert content(owner) == promoted
                assert not edit('forge_asset_document_tile_collision', owner, tile=tile-1, collision=collision)['ok']
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == seeded
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == promoted
                polygon = dict(kind='polygon', points=[[0, 0], [1, 0], [1, .5],
                                                       [.5, .5], [.5, 1], [0, 1]])
                result = edit('forge_asset_document_tile_collision', owner, tile=tile, collision=polygon)
                assert result['ok'], result
                concave = content(owner)
                assert concave['version'] == 3 and concave['tiles'][0]['collision'] == polygon
                for bad in (dict(kind='polygon', points=[[0, 0], [1, 1], [0, 1], [1, 0]]),
                            dict(kind='polygon', points=[[0, 0], [1, 0], [1, 1], [.5, 0], [0, 1]]),
                            dict(kind='polygon', points=[[0, 0], [.5, 0], [1, 0], [1, 1], [0, 1]])):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_tile_collision', owner, tile=tile, collision=bad)['ok']
                    assert content(owner) == concave and status(owner)['revision'] == revision
                contours = dict(kind='contours', contours=[
                    dict(points=[[0, 0], [1, 0], [1, 1], [0, 1]],
                         holes=[[[.25, .25], [.75, .25], [.75, .75], [.25, .75]]]),
                    dict(points=[[.375, .375], [.625, .375], [.625, .625], [.375, .625]], holes=[])])
                old = status(owner)
                result = edit('forge_asset_document_tile_collision', owner, tile=tile, collision=contours)
                assert result['ok'], result
                compound = content(owner)
                assert compound['version'] == 6 and compound['tiles'][0]['collision'] == contours
                failed(mutate('forge_asset_document_tile_collision', editor=owner,
                              document_session=old['session'], document_revision=old['revision'],
                              tile=tile, collision=contours), 'stale_document')
                for bad in (dict(kind='contours', contours=[]),
                            dict(kind='contours', contours=contours['contours'] * 3),
                            dict(kind='contours', contours=[dict(points=contours['contours'][0]['points'],
                                holes=[[[0, .25], [.75, .25], [.75, .75], [.25, .75]]])]),
                            dict(kind='none', contours=contours['contours'])):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_tile_collision', owner, tile=tile, collision=bad)['ok']
                    assert content(owner) == compound and status(owner)['revision'] == revision
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == concave
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == compound
                if COLLISION_ONLY:
                    save_published(owner)
                    saved = json.loads((root/'Game'/path).read_text(encoding='utf-8'))
                    assert saved == compound
                    catalog = json.loads((root/'Game/forge.assets.json').read_text(encoding='utf-8'))
                    record = next(row for row in catalog['assets'] if row['id'] == set_id)
                    assert record['schema_version'] == 6 and record['metadata']['forge.import']['output_version'] == 6
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == concave
                if COLLISION_ONLY:
                    save_published(owner)
                    saved = json.loads((root/'Game'/path).read_text(encoding='utf-8'))
                    assert saved == concave and saved['version'] == 3
                    catalog = json.loads((root/'Game/forge.assets.json').read_text(encoding='utf-8'))
                    record = next(row for row in catalog['assets'] if row['id'] == set_id)
                    assert record['schema_version'] == 3 and record['metadata']['forge.import']['output_version'] == 6
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == concave
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                if COLLISION_ONLY:
                    animation = dict(enabled=True, frames=[dict(frame=1, seconds=.125),
                                                           dict(frame=3, seconds=.25),
                                                           dict(frame=0, seconds=.125)])
                    prior = status(owner)['revision']
                    result = edit('forge_asset_document_tile_animation', owner, tile=tile, animation=animation)
                    assert result['ok'], result
                    animated = content(owner)
                    assert animated['version'] == 4 and animated['tiles'][0]['animation'] == animation
                    current = status(owner)
                    stale = mutate('forge_asset_document_tile_animation', editor=owner,
                                   document_session=current['session'], document_revision=prior,
                                   tile=tile, animation=animation)
                    assert not stale['ok'] and content(owner) == animated
                    for bad in (dict(enabled=True, frames=[]),
                                dict(enabled=True, frames=[dict(frame=4, seconds=.1)]),
                                dict(enabled=True, frames=[dict(frame=0, seconds=0)])):
                        revision = status(owner)['revision']
                        assert not edit('forge_asset_document_tile_animation', owner, tile=tile, animation=bad)['ok']
                        assert content(owner) == animated and status(owner)['revision'] == revision
                    save_published(owner)
                    assert json.loads((root/'Game'/path).read_text(encoding='utf-8')) == animated
                    catalog = json.loads((root/'Game/forge.assets.json').read_text(encoding='utf-8'))
                    record = next(row for row in catalog['assets'] if row['id'] == set_id)
                    assert record['schema_version'] == 4 and record['metadata']['forge.import']['output_version'] == 6
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == animated
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                    neighbor = dict(enabled=True, rules=[dict(mask=1, same=1, tile=tile),
                                                         dict(mask=1, same=0, tile=tile)])
                    previous = status(owner)['revision']
                    assert edit('forge_asset_document_tile_autotile', owner, tile=tile, autotile=neighbor)['ok']
                    selected = content(owner)
                    assert selected['version'] == 5 and selected['tiles'][0]['autotile'] == neighbor
                    current = status(owner)
                    failed(mutate('forge_asset_document_tile_autotile', editor=owner,
                                  document_session=current['session'], document_revision=previous,
                                  tile=tile, autotile=neighbor), 'stale_document')
                    for bad in (dict(enabled=True, rules=[]),
                                dict(enabled=True, rules=[dict(mask=1, same=2, tile=tile)]),
                                dict(enabled=True, rules=[dict(mask=0, same=0, tile=1)]),
                                dict(enabled=True, rules=[dict(mask=0, same=0, tile=tile-1)]),
                                dict(enabled=True, rules=[dict(mask=0, same=0, tile=tile)] * 65)):
                        revision = status(owner)['revision']
                        assert not edit('forge_asset_document_tile_autotile', owner, tile=tile, autotile=bad)['ok']
                        assert content(owner) == selected and status(owner)['revision'] == revision
                    save_published(owner)
                    assert json.loads((root/'Game'/path).read_text(encoding='utf-8')) == selected
                    catalog = json.loads((root/'Game/forge.assets.json').read_text(encoding='utf-8'))
                    record = next(row for row in catalog['assets'] if row['id'] == set_id)
                    assert record['schema_version'] == 5 and record['metadata']['forge.import']['output_version'] == 6
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == promoted
                assert edit('forge_asset_document_tile_collision', owner, tile=tile, collision=dict(kind='none'))['ok']
                assert content(owner)['tiles'][0]['collision'] == dict(kind='none')
                assert edit('forge_asset_document_undo', owner)['ok']
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == seeded
                assert edit('forge_asset_document_patch', owner,
                            patch=[dict(op='add', path='/tiles/0/collision', value=dict(legacy=True))])['ok']
                legacy_source = content(owner)
                assert not edit('forge_asset_document_tile_collision', owner, tile=tile, collision=collision)['ok']
                assert content(owner) == legacy_source
                assert edit('forge_asset_document_undo', owner)['ok']
                assert edit('forge_asset_document_undo', owner)['ok']
                restored = content(owner)
                # Allocator high-water persists through Undo; no retired IDs are reused.
                original['next_id'] = restored['next_id']
                assert restored == original
            if owner == 'application_states':
                original = content(owner)
                domains = [
                    dict(key='app', initial='menu', states=[dict(key='menu'), dict(key='game'),
                         dict(key='playing', parent='game'), dict(key='paused', parent='game')]),
                    dict(key='level', initial='warmup', enabled_when=dict(domain='app', state='game'),
                         states=[dict(key='warmup'), dict(key='round')]),
                    dict(key='hud', computed=True, initial='hidden',
                         states=[dict(key='hidden'), dict(key='visible'), dict(key='ready')],
                         rules=[dict(state='ready', all=[dict(domain='app', state='playing'),
                                                        dict(domain='level', state='round')]),
                                dict(state='visible', all=[dict(domain='app', state='game')])])]
                assert edit('forge_asset_document_patch', owner,
                            patch=[dict(op='replace', path='/domains', value=domains)])['ok']
                selected = content(owner)
                for pointer, bad in (('/domains/0/initial', 'missing'),
                                     ('/domains/0/states/1/parent', 'playing'),
                                     ('/domains/1/enabled_when/state', 'missing'),
                                     ('/domains/0/enabled_when', dict(domain='hud', state='ready')),
                                     ('/domains/2/computed', 1)):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_patch', owner,
                                    patch=[dict(op='add', path=pointer, value=bad)])['ok']
                    assert content(owner) == selected and status(owner)['revision'] == revision
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
            if owner == 'render_texture':
                original = content(owner)
                assert edit('forge_asset_document_patch', owner, patch=[
                    dict(op='replace', path='/width', value=128),
                    dict(op='replace', path='/height', value=64)])['ok']
                selected = content(owner)
                assert selected['width'] == 128 and selected['height'] == 64
                for pointer, bad in (('/width', 0), ('/height', 2049), ('/width', 32.5),
                                     ('/height', True), ('/profile', 'unknown')):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_patch', owner,
                                    patch=[dict(op='replace', path=pointer, value=bad)])['ok']
                    assert content(owner) == selected and status(owner)['revision'] == revision
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
            if owner == 'ui_localization':
                original = content(owner)
                assert edit('forge_asset_document_patch', owner, patch=[
                    dict(op='add', path='/locales/en/messages/status', value='<b>Café</b> {{counter}} & text'),
                    dict(op='add', path='/locales/fr/messages/status', value='Prêt'),
                    dict(op='add', path='/locales/de', value=dict(label='Deutsch', messages={}))])['ok']
                selected = content(owner)
                assert selected['locales']['fr']['messages']['status'] == 'Prêt'
                for pointer, bad in (('/default_locale', 'absent'),
                                     ('/locales/en/messages/bad key', 'Invalid'),
                                     ('/locales/fr/messages/unknown_key', 'Unknown'),
                                     ('/locales/en/messages/status', 'x' * 1025)):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_patch', owner, patch=[dict(op='add', path=pointer, value=bad)])['ok']
                    assert content(owner) == selected and status(owner)['revision'] == revision
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
            if owner == 'navigation_behavior':
                original = content(owner)
                graph = json.loads(json.dumps(original))
                graph['states'][0]['transitions'] = [dict(id=3, to=2, condition=dict(kind='elapsed', seconds=1))]
                graph['states'].append(dict(id=2, name='Travel', action=dict(kind='move_to_point', point=[3, 0, -2]),
                    transitions=[dict(id=4, to=1, condition=dict(kind='arrived'))]))
                graph['next_id'] = 5
                assert edit('forge_asset_document_patch', owner, patch=[dict(op='replace', path='', value=graph)])['ok']
                selected = content(owner)
                assert selected == graph
                for pointer, bad in (('/states/0/transitions/0/to', 999),
                                     ('/states/0/transitions/0/condition/seconds', -1),
                                     ('/states/1/id', 3), ('/next_id', 2), ('/entry', 999)):
                    revision = status(owner)['revision']
                    assert not edit('forge_asset_document_patch', owner, patch=[dict(op='replace', path=pointer, value=bad)])['ok']
                    assert content(owner) == selected and status(owner)['revision'] == revision
                assert edit('forge_asset_document_undo', owner)['ok']
                original['next_id'] = 5
                assert content(owner) == original
                assert not edit('forge_asset_document_patch', owner, patch=[
                    dict(op='add', path='/states/-', value=dict(id=2, name='Retired', action=dict(kind='wait'), transitions=[]))])['ok']
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
            if owner == 'animation_timeline':
                # Syntax/source ownership here; native target admission is covered by
                # animation_component with the actual reflected C++ component.
                original = content(owner)
                if UI_VALUES_ONLY:
                    collections = original
                else:
                    clip = json.loads(json.dumps(original))
                    clip['version'] = 6
                    clip['component_bindings'] = [dict(component='project.motion', field='speed',
                        admission=dict(format='forge.authored-component', version=1,
                                       module='project.motion', schema_version=1, digest='a' * 64))]
                    for row in clip['keys']:
                        row['component_values'] = [dict(component='project.motion', field='speed',
                                                        value=90, curve=dict(interpolation='cubic', incoming=0, outgoing=0))]
                    old = status(owner)
                    assert edit('forge_asset_document_patch', owner,
                                patch=[dict(op='replace', path='', value=clip)])['ok']
                    assert content(owner) == clip
                    admitted = status(owner)
                    invalid = edit('forge_asset_document_patch', owner,
                                   patch=[dict(op='replace', path='/keys/0/component_values/0/value', value='invalid')])
                    assert not invalid['ok']
                    assert content(owner) == clip and status(owner)['revision'] == admitted['revision']
                    failed(mutate('forge_asset_document_patch', editor=owner,
                                  document_session=old['session'], document_revision=old['revision'],
                                  patch=[dict(op='replace', path='', value=original)]), 'stale_document')
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == clip
                    typed = json.loads(json.dumps(clip))
                    typed['version'] = 7
                    typed['component_bindings'][0]['value_type'] = 'scalar'
                    discrete = [('enabled', 'bool', False), ('marker', 'int64', -(1 << 63)),
                                ('score', 'uint64', (1 << 64) - 1)]
                    for field_name, value_kind, initial_value in discrete:
                        typed['component_bindings'].append(dict(component='project.motion', field=field_name,
                            value_type=value_kind, admission=clip['component_bindings'][0]['admission']))
                        for row in typed['keys']:
                            row['component_values'].append(dict(component='project.motion', field=field_name,
                                                                 value=initial_value))
                    assert edit('forge_asset_document_patch', owner,
                                patch=[dict(op='replace', path='', value=typed)])['ok']
                    assert content(owner) == typed
                    typed_state = status(owner)
                    for index, invalid_value in [(1, 1), (2, 1.25), (2, 1 << 63), (3, -1)]:
                        invalid = edit('forge_asset_document_patch', owner,
                                       patch=[dict(op='replace', path=f'/keys/0/component_values/{index}/value', value=invalid_value)])
                        assert not invalid['ok'], invalid
                        assert content(owner) == typed and status(owner)['revision'] == typed_state['revision']
                    invalid = edit('forge_asset_document_patch', owner,
                                   patch=[dict(op='add', path='/keys/0/component_values/1/curve',
                                               value=dict(interpolation='linear', incoming=0, outgoing=0))])
                    assert not invalid['ok'] and content(owner) == typed
                    failed(mutate('forge_asset_document_patch', editor=owner,
                                  document_session=admitted['session'], document_revision=admitted['revision'],
                                  patch=[dict(op='replace', path='', value=clip)]), 'stale_document')
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == clip
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == typed
                    nested = json.loads(json.dumps(typed))
                    nested['version'] = 8
                    for leaf_name, value_kind, initial_value in [('speed', 'scalar', 30),
                            ('enabled', 'bool', False), ('score', 'uint64', (1 << 64) - 1)]:
                        nested['component_bindings'].append(dict(component='project.motion',
                            field='settings', member_path=[leaf_name], value_type=value_kind,
                            admission=clip['component_bindings'][0]['admission']))
                        for row in nested['keys']:
                            row['component_values'].append(dict(component='project.motion',
                                field='settings', member_path=[leaf_name], value=initial_value))
                    assert edit('forge_asset_document_patch', owner,
                                patch=[dict(op='replace', path='', value=nested)])['ok']
                    assert content(owner) == nested
                    nested_state = status(owner)
                    for patch_path, bad_value in [('/component_bindings/4/member_path', 'speed'),
                            ('/component_bindings/4/member_path', ['']),
                            ('/component_bindings/4/member_path', ['speed'] * 17),
                            ('/keys/0/component_values/4/member_path', ['missing']),
                            ('/keys/0/component_values/5/value', 1)]:
                        invalid = edit('forge_asset_document_patch', owner,
                            patch=[dict(op='replace', path=patch_path, value=bad_value)])
                        assert not invalid['ok'], invalid
                        assert content(owner) == nested and status(owner)['revision'] == nested_state['revision']
                    failed(mutate('forge_asset_document_patch', editor=owner,
                        document_session=typed_state['session'], document_revision=typed_state['revision'],
                        patch=[dict(op='replace', path='', value=typed)]), 'stale_document')
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == typed
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == nested
                    collections = json.loads(json.dumps(nested))
                    collections['version'] = 9
                    for tail, value_kind, initial_value in [([dict(index=0), 'speed'], 'scalar', 45),
                            ([dict(key=['left', 'speed']), 'marker'], 'uint64', (1 << 64) - 1),
                            ([dict(key='right'), 'enabled'], 'bool', False)]:
                        collections['component_bindings'].append(dict(component='project.motion',
                            field='entries', element_path=tail, value_type=value_kind,
                            admission=clip['component_bindings'][0]['admission']))
                        for row in collections['keys']:
                            row['component_values'].append(dict(component='project.motion',
                                field='entries', element_path=tail, value=initial_value))
                    assert edit('forge_asset_document_patch', owner,
                        patch=[dict(op='replace', path='', value=collections)])['ok']
                    assert content(owner) == collections
                    collection_state = status(owner)
                    for patch_path, bad_value in [('/component_bindings/7/element_path', ['speed']),
                            ('/component_bindings/7/element_path', [dict(index=-1)]),
                            ('/component_bindings/7/element_path', [dict(index=1.25)]),
                            ('/component_bindings/7/element_path', [dict(index=4096)]),
                            ('/component_bindings/7/element_path', [dict(index=0, key='left')]),
                            ('/component_bindings/8/element_path', [dict(key=['left'])]),
                            ('/keys/0/component_values/7/element_path', [dict(index=1), 'speed'])]:
                        invalid = edit('forge_asset_document_patch', owner,
                            patch=[dict(op='replace', path=patch_path, value=bad_value)])
                        assert not invalid['ok'], invalid
                        assert content(owner) == collections and status(owner)['revision'] == collection_state['revision']
                    failed(mutate('forge_asset_document_patch', editor=owner,
                        document_session=nested_state['session'], document_revision=nested_state['revision'],
                        patch=[dict(op='replace', path='', value=nested)]), 'stale_document')
                    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == nested
                    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == collections
                declaration = next(row for row in discovery()['result']['schema']['components'] if row['id'] == 'forge.ui_values')
                assert declaration['admission']['format'] == 'forge.builtin-component'
                named = json.loads(json.dumps(collections))
                named['version'] = 9
                named['channels'] = []
                named['component_bindings'] = [dict(component='forge.ui_values', field='values',
                    element_path=[dict(key='amount'), 'value'], value_type='scalar', admission=declaration['admission'])]
                for keyframe in named['keys']:
                    keyframe['component_values'] = [dict(component='forge.ui_values', field='values',
                        element_path=[dict(key='amount'), 'value'], value=.25)]
                before_named = status(owner)
                assert edit('forge_asset_document_patch', owner,
                    patch=[dict(op='replace', path='', value=named)])['ok']
                assert content(owner) == named
                named_state = status(owner)
                invalid = edit('forge_asset_document_patch', owner,
                    patch=[dict(op='replace', path='/component_bindings/0/admission/format', value='forge.invalid-component')])
                assert not invalid['ok'] and content(owner) == named and status(owner)['revision'] == named_state['revision']
                failed(mutate('forge_asset_document_patch', editor=owner,
                    document_session=before_named['session'], document_revision=before_named['revision'],
                    patch=[dict(op='replace', path='', value=collections)]), 'stale_document')
                assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == collections
                assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == named
            if owner == 'tile_map':
                failed(edit('forge_asset_document_tile_collision', owner, tile=1, collision=dict(kind='none')), 'invalid_arguments')
                cells = [dict(layer=1, x=-33, y=64, tile=7, flags=0), dict(layer=1, x=0, y=0, tile=0)]
                assert edit('forge_asset_document_paint', owner, cells=cells)['ok']
                query = [dict(layer=1, x=-33, y=64), dict(layer=1, x=0, y=0)]
                values = read('forge_asset_document_cells', editor=owner,
                              document_session=status(owner)['session'], cells=query)['cells']
                assert values[0]['tile'] == 7 and values[1]['tile'] == 0
                assert edit('forge_asset_document_undo', owner)['ok']
                assert read('forge_asset_document_cells', editor=owner,
                            document_session=status(owner)['session'], cells=query)['cells'][0]['tile'] == 0
            # The fixture has no shader renderer/profile; graph compilation is not claimed.
            if owner in ('material', 'collision', 'editable_mesh', 'particle_effect', 'terrain',
                         'tile_set', 'sprite_atlas', 'animation_timeline', 'navigation_behavior', 'ui_localization', 'application_states', 'render_texture'):
                result = edit('forge_asset_document_save', owner)
                assert result['ok'], result
                end = time.monotonic() + 35
                while True:
                    settled = status(owner)
                    if not settled['save_requested'] and not settled['publishing']:
                        break
                    assert time.monotonic() < end, settled
                    time.sleep(.02)
                assert not settled['dirty'] and not settled['diagnostic'], settled
                assert json.loads((root / 'Game' / path).read_text())[key] == value
                if owner in ('editable_mesh', 'animation_timeline'):
                    old = status(owner)
                    external = content(owner)
                    external[key] = 'Changed outside the editor'
                    (root / 'Game' / path).write_text(json.dumps(external), encoding='utf-8')
                    digest = hashlib.sha256((root / 'Game' / path).read_bytes()).hexdigest()
                    def published_digest():
                        records = json.loads((root / 'Game/forge.assets.json').read_text())['assets']
                        return next(record['metadata']['forge.import']['source_digest']
                                    for record in records if record['id'] == old['asset'])
                    end = time.monotonic() + 12
                    while published_digest() != digest:
                        assert time.monotonic() < end, (owner, 'External source publication did not complete')
                        time.sleep(.1)
                    end = time.monotonic() + 2
                    while status(owner)['session'] == old['session']:
                        assert time.monotonic() < end, (owner, 'Published source did not refresh its open editor')
                        time.sleep(.1)
                    assert content(owner)[key] == external[key]
                    assert not status(owner)['dirty']
                    failed(mutate('forge_asset_document_patch', editor=owner,
                                  document_session=old['session'], document_revision=old['revision'],
                                  patch=patch), 'wrong_document')
                # Actual owner status only becomes clean after validated publication.
                assert edit('forge_asset_document_close', owner)['ok']
            else:
                assert edit('forge_asset_document_close', owner, discard=True)['ok']
            assert not status(owner)['open']
            if owner == 'material.graph':
                failed_open = mutate('forge_asset_document_open', editor=owner,
                                     path=path, domain='sprite')
                assert not failed_open['ok'] and not status(owner)['open']
            opened = mutate('forge_asset_document_open', editor=owner, path=path)
            assert opened['ok'], opened
            assert status(owner)['session'] != initial['session']
            failed(mutate('forge_asset_document_patch', editor=owner,
                          document_session=initial['session'], document_revision=1, patch=patch), 'wrong_document')
            if owner in ('particle_effect', 'terrain', 'tile_set', 'sprite_atlas'):
                assert not status(owner)['dirty']
                assert edit('forge_asset_document_patch', owner, patch=[dict(op='replace', path='/' + key,
                            value=before.get(key, 'temporary'))])['ok']
                assert status(owner)['dirty']
                assert edit('forge_asset_document_undo', owner)['ok']
                assert not status(owner)['dirty']  # Same saved-state semantics as interactive editing.
            assert edit('forge_asset_document_close', owner, discard=True)['ok']

        if render_texture_id is not None:
            check_render_texture_workflow(read, mutate, edit, status, content,
                                          save_published, render_texture_id, root, discovery)

        if NAVIGATION_BEHAVIOR_ONLY:
            owner = 'navigation_behavior'
            template = read('forge_asset_document_template', editor=owner, behavior_profile='tree')
            assert template['version'] == 2 and template['nodes'][0]['kind'] == 'wait'
            wrong = client.tool('forge_asset_document_template', target=discovery()['result']['target'],
                                editor='terrain', behavior_profile='tree')
            failed(wrong, 'invalid_argument')
            assert not mutate('forge_asset_document_create', editor=owner,
                              path='Assets/InvalidTree.behavior.json', behavior_profile='unknown')['ok']
            assert not (root/'Game/Assets/InvalidTree.behavior.json').exists()
            created = mutate('forge_asset_document_create', editor=owner,
                             path='Assets/McpTree.behavior.json', behavior_profile='tree')
            assert created['ok'], created
            original = content(owner)
            assert original['version'] == 2 and original['asset_id'] != template['asset_id']
            tree = json.loads(json.dumps(original))
            tree['next_id'] = 4
            tree['nodes'] = [dict(id=1, name='Route', kind='sequence', children=[2, 3]),
                             dict(id=2, name='Wait', kind='wait', seconds=.25),
                             dict(id=3, name='Walk', kind='move_to_point', point=[3, 0, 0])]
            old = status(owner)
            assert edit('forge_asset_document_patch', owner,
                        patch=[dict(op='replace', path='', value=tree)])['ok']
            selected = content(owner)
            failed(mutate('forge_asset_document_patch', editor=owner, document_session=old['session'],
                          document_revision=old['revision'], patch=[dict(op='replace', path='/entry', value=2)]),
                   'stale_document')
            for pointer, bad in (('/nodes/0/children', [2, 2]), ('/nodes/0/children', [1, 2]),
                                 ('/nodes/1/seconds', -1), ('/nodes/2/id', 2), ('/next_id', 2)):
                revision = status(owner)['revision']
                assert not edit('forge_asset_document_patch', owner,
                                patch=[dict(op='replace', path=pointer, value=bad)])['ok']
                assert content(owner) == selected and status(owner)['revision'] == revision
            assert edit('forge_asset_document_undo', owner)['ok']
            original['next_id'] = 4
            assert content(owner) == original
            assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == selected
            save_published(owner)
            assert json.loads((root/'Game/Assets/McpTree.behavior.json').read_text())['nodes'] == tree['nodes']
            assert edit('forge_asset_document_close', owner, discard=True)['ok']

        if COLLISION_ONLY or ATLAS_ONLY or SPRITE_GRAPH_ONLY or NAVIGATION_BEHAVIOR_ONLY or UI_LOCALIZATION_ONLY or APPLICATION_STATES_ONLY or UI_VALUES_ONLY or RENDER_TEXTURE_ONLY:
            continue

        check_audio_settings(read, mutate, root)

        check_prefab_workflow(read, mutate, failed, root)

        # External valid source edits remain protected by the same disk baseline.
        assert mutate('forge_asset_document_open', editor='material', path='Assets/MCP-material.material.json')['ok']
        external_path = root / 'Game/Assets/MCP-material.material.json'
        external = json.loads(external_path.read_text()); external['external_change'] = True
        external_path.write_text(json.dumps(external))
        assert edit('forge_asset_document_patch', 'material', patch=[dict(op='add', path='/unsaved', value=True)])['ok']
        assert not edit('forge_asset_document_save', 'material')['ok']
        assert 'unsaved' not in json.loads(external_path.read_text())
        assert not mutate('forge_document_new')['ok']  # Source owner blocks destructive switching.
        assert edit('forge_asset_document_close', 'material', discard=True)['ok']


write_capture(sys.argv[4])
print('MCP shared source editors, history, validation and publication passed')
