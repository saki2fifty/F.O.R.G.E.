"""Shared editor source/history/publication owners through both MCP envelopes."""
from contextlib import contextmanager
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import wave
from mcp_test_client import CAPTURE, Client, failed

TOOLS, HOST, WORKER = map(str, map(Path, sys.argv[1:4]))


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
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)


for legacy in (False, True):
    with editor() as (root, process, config), Client(config, legacy) as client:
        tool_names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert {'forge_asset_document_create', 'forge_asset_settings_publish', 'forge_prefab_apply_commit'} <= tool_names
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
        assert len(kinds) == 12 and all(not item['status']['open'] for item in kinds), kinds
        graph_types = read('forge_asset_graph_catalog')['nodes']
        assert len(graph_types) > 10
        assert read('forge_asset_graph_node', schema='constant')['outputs']
        # A Tile Set source can be authored before its dependent Tile Map.
        kinds.sort(key=lambda item: item['editor'] == 'tile_map')
        set_id = None
        for kind in kinds:
            owner = kind['editor']
            extra = dict(tile_set=set_id) if owner == 'tile_map' else {}
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
            if owner == 'tile_map':
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
                         'tile_set', 'animation_timeline'):
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
                # Actual owner status only becomes clean after validated publication.
                assert edit('forge_asset_document_close', owner)['ok']
            else:
                assert edit('forge_asset_document_close', owner, discard=True)['ok']
            assert not status(owner)['open']
            opened = mutate('forge_asset_document_open', editor=owner, path=path)
            assert opened['ok'], opened
            assert status(owner)['session'] != initial['session']
            failed(mutate('forge_asset_document_patch', editor=owner,
                          document_session=initial['session'], document_revision=1, patch=patch), 'wrong_document')
            if owner in ('particle_effect', 'terrain', 'tile_set'):
                assert not status(owner)['dirty']
                assert edit('forge_asset_document_patch', owner, patch=[dict(op='replace', path='/' + key,
                            value=before.get(key, 'temporary'))])['ok']
                assert status(owner)['dirty']
                assert edit('forge_asset_document_undo', owner)['ok']
                assert not status(owner)['dirty']  # Same saved-state semantics as interactive editing.
            assert edit('forge_asset_document_close', owner, discard=True)['ok']

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

    with editor('read') as (root, process, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_asset_document_types' in names
        assert not any('forge_asset_document_' + name in names for name in ('create', 'patch', 'save', 'paint', 'close'))

Path(sys.argv[4]).write_text(json.dumps(CAPTURE, indent=2) + '\n')
print('MCP shared source editors, history, validation and publication passed')
