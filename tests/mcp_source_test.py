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

TOOLS, HOST, WORKER = map(str, map(Path, sys.argv[1:4]))
COLLISION_ONLY = "--tile-collision-only" in sys.argv[5:]
ATLAS_ONLY = "--atlas-only" in sys.argv[5:]
SPRITE_GRAPH_ONLY = "--sprite-graph-only" in sys.argv[5:]
NAVIGATION_BEHAVIOR_ONLY = "--navigation-behavior-only" in sys.argv[5:]
UI_LOCALIZATION_ONLY = "--ui-localization-only" in sys.argv[5:]


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
            finish_host(process)


for legacy in (False, True):
    with editor('read') as (root, process, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_asset_document_types' in names
        assert not any('forge_asset_document_' + name in names for name in ('create', 'patch', 'save', 'paint', 'close', 'tile_collision', 'tile_animation', 'tile_autotile'))

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

        def read(tool, **extra):
            result = client.tool(tool, target=discovery()['result']['target'], **extra)
            assert result['ok'], result
            return result['result']

        def mutate(tool, **extra):
            state = discovery()
            return client.tool(tool, target=state['result']['target'],
                               expected_revision=state['revision'], sequence=state['next_sequence'], **extra)

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
        assert len(kinds) == 15 and all(not item['status']['open'] for item in kinds), kinds
        localization_catalog = read('forge_asset_localization_catalog')
        assert localization_catalog['editor'] == 'ui_localization'
        assert localization_catalog['limits']['locales'] == 8
        assert localization_catalog['command'] == dict(name='forge.locale', value='fr')
        assert 'tr(forge_locale)' in localization_catalog['binding']
        behavior_catalog = read('forge_asset_behavior_catalog')
        assert behavior_catalog['editor'] == 'navigation_behavior'
        assert {a['kind'] for a in behavior_catalog['actions']} == {'wait', 'move_to_point', 'follow_target'}
        assert len(behavior_catalog['conditions']) == 5 and behavior_catalog['limits']['outgoing'] == 8
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
        atlas_image = None
        if not COLLISION_ONLY and not SPRITE_GRAPH_ONLY and not NAVIGATION_BEHAVIOR_ONLY and not UI_LOCALIZATION_ONLY:
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
                         'tile_set', 'sprite_atlas', 'animation_timeline', 'navigation_behavior', 'ui_localization'):
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

        if COLLISION_ONLY or ATLAS_ONLY or SPRITE_GRAPH_ONLY or NAVIGATION_BEHAVIOR_ONLY or UI_LOCALIZATION_ONLY:
            continue

        # Typed importer settings use the shared draft, schema and native worker.
        with wave.open(str(root / 'Game/Assets/Settings.wav'), 'wb') as wav:
            wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(8000)
            wav.writeframes(b'\x01\x00\x02\x00' * 80)
        assert mutate('forge_asset_settings_open', editor='audio_import', path='Assets/Settings.wav')['ok']
        schema = read('forge_asset_settings_schema', editor='audio_import')
        assert {'force_mono', 'normalize_peak'} <= {rule['key'] for rule in schema['rules']}
        initial = read('forge_asset_settings_status', editor='audio_import')
        def import_edit(tool, **extra):
            current = read('forge_asset_settings_status', editor='audio_import')
            return mutate(tool, editor='audio_import', session=current['session'], state=current['state'], **extra)
        # Invalid second field must not adopt the otherwise valid first field.
        invalid = import_edit('forge_asset_settings_edit', changes=[dict(key='force_mono', value=True),
                              dict(key='missing_setting', value=True)])
        assert not invalid['ok']
        assert read('forge_asset_settings_status', editor='audio_import')['state'] == initial['state']
        assert import_edit('forge_asset_settings_edit', changes=[dict(key='force_mono', value=True)])['ok']
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

        # Prefab publication and durable Apply reuse the source editor/library/SceneDocument.
        created = mutate('forge_entity_create', arguments=dict(kind=0, name='Prefab original'))
        assert created['ok'], created
        entity = created['result']['results'][0]['selected']
        captured = mutate('forge_prefab_create', entity=entity, path='Assets/Agent.prefab.json')
        assert captured['ok'], captured
        asset = captured['result']['asset']
        assert any(item['asset'] == asset for item in read('forge_prefab_list')['prefabs'])
        assert mutate('forge_prefab_source_open', asset=asset)['ok']
        draft = read('forge_prefab_source_read')
        root_index = next(i for i, member in enumerate(draft['source']['members']) if member['id'] == draft['source']['root'])
        assert mutate('forge_prefab_source_patch', state=draft['state'], patch=[dict(op='replace',
                      path=f'/members/{root_index}/name', value='Published prefab')])['ok']
        failed(mutate('forge_prefab_source_publish', state=draft['state']), 'stale_document')
        assert mutate('forge_prefab_source_publish', state=read('forge_prefab_source_read')['state'])['ok']
        assert mutate('forge_prefab_source_close', state=read('forge_prefab_source_read')['state'])['ok']
        instance = mutate('forge_prefab_instantiate', arguments=dict(asset=asset))
        assert instance['ok'], instance
        instance_id = instance['result']['results'][0]['selected']
        assert mutate('forge_document_save')['ok']
        assert mutate('forge_entity_rename', arguments=dict(entity=instance_id, name='Applied name'))['ok']
        reviewed = mutate('forge_prefab_apply_review', entity=instance_id)
        assert reviewed['ok'] and reviewed['result']['overrides'] >= 1, reviewed
        committed = mutate('forge_prefab_apply_commit', review=reviewed['result']['review'], acknowledge_scene_save=True)
        assert committed['ok'], committed
        saved_prefab = json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())
        assert saved_prefab['members'][root_index]['name'] == 'Applied name'
        assert mutate('forge_history_undo')['ok']
        assert json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())['members'][root_index]['name'] == 'Published prefab'
        assert mutate('forge_history_redo')['ok']
        assert json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())['members'][root_index]['name'] == 'Applied name'

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
