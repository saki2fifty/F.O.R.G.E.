"""Dynamic compound Maps and copied definitions through actual Play processes."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
from runtime_client import Runtime, activate

runtime, gallery = (Path(value).resolve() for value in sys.argv[1:3])
with_sdk_play = len(sys.argv) > 3 and sys.argv[3].lower() in ('on', 'true', '1')
scene_path = 'Scenes/124-dynamic-tile-map-bodies.scene.json'
saved = json.loads((gallery / scene_path).read_text(encoding='utf-8'))
maps = {row['id']: row for row in saved['entities']
        if 'forge.tile_map_renderer' in row['components']}
assert len(maps) == 3


def transforms(state):
    return {row['id']: {key: row['components'][key] for key in
            ('forge.local_translation', 'forge.local_rotation', 'forge.local_scale')}
            for row in state['scene']['entities'] if row['id'] in maps}


def restore(worker, state):
    recovery = state['recovery']
    return worker.request('replace', scene=state['scene'], recovery=recovery,
                          recovery_session=recovery['session'], recovery_tick=recovery['tick'])


def copied_profiles(worker, state):
    manifest = state['tile_maps']
    assert len(manifest['maps']) == 3, manifest
    scope = dict(world=manifest['world'], scene=manifest['scene'])
    for row in manifest['maps']:
        assert row['ready'] and row['cells'] == 2, row
        reply = worker.request('tile_map_begin', **scope, entity=row['entity'],
                               generation=row['generation'], revision=row['revision'],
                               base_generation=0, base_revision=0)['tile_projection']
        assert not reply['stale'] and reply['full']
        assert reply['header']['definitions_version'] == 6
        definitions = {tile['id']: tile['collision']
                       for tile in reply['header']['definitions']['tiles']}
        assert definitions[2]['kind'] == 'contours'
        assert len(definitions[2]['contours']) == 2
        assert len(definitions[2]['contours'][0]['holes']) == 1
        assert len(definitions[3]['contours']) == 2
        cursor = 0
        while cursor < reply['count']:
            page = worker.request('tile_map_page', **scope, token=reply['token'], cursor=cursor)['tile_projection']
            assert page['next'] > cursor
            cursor = page['next']
        assert worker.request('tile_map_ack', **scope, token=reply['token'])['tile_projection']['accepted']


with tempfile.TemporaryDirectory(prefix='forge-dynamic-maps-', dir=runtime.parent) as temporary:
    root = Path(temporary) / 'private Gallery'
    shutil.copytree(gallery, root)
    settings_path = root / 'forge.project.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8'))
    settings['modules'] = []
    settings_path.write_text(json.dumps(settings), encoding='utf-8')
    before = (root / scene_path).read_bytes()
    for sdk_play in ((False, True) if with_sdk_play else (False,)):
        arguments = (['--sdk-project', root, '--sdk-play', 'on', '--user-data', root / 'users']
                     if sdk_play else ['--project', root])
        worker = Runtime(runtime, arguments)
        peer = None
        try:
            state = activate(worker, saved, sdk_play)
            assert state['physics']['bodies_2d'] == 4 and state['physics']['shapes_2d'] == 43
            copied_profiles(worker, state)
            for tick in range(1, 241):
                state = worker.request('step')
                if tick == 90 and not sdk_play:
                    peer = Runtime(runtime, arguments)
                    assert transforms(restore(peer, state)) == transforms(state)
                elif tick > 90 and peer:
                    assert transforms(peer.request('step')) == transforms(state)
            current = transforms(state)
            for identity, original in maps.items():
                components = current[identity]
                assert -4 < components['forge.local_translation']['y'] < 1
                assert components['forge.local_scale'] == original['components']['forge.local_scale']
                rotation = components['forge.local_rotation']
                if original['components']['forge.physics_body_2d']['lock_rotation']:
                    assert abs(rotation['z']) < 1e-6
                else:
                    assert rotation != original['components']['forge.local_rotation']
            copied_profiles(worker, state)
            if sdk_play:
                assert state['recovery'] is None
            else:
                assert state['recovery']['physics']['version'] == 10
                peer.close()
                peer = Runtime(runtime, arguments)
                assert transforms(restore(peer, state)) == transforms(state)
                assert transforms(worker.request('step')) == transforms(peer.request('step'))
            assert (root / scene_path).read_bytes() == before
        finally:
            worker.close()
            if peer:
                peer.close()

print('Dynamic Maps: both actual hosts, copied6 contours, fixed motion and exact ordinary process restart passed'
      if with_sdk_play else 'Dynamic Maps: ordinary host, copied6 contours, fixed motion and exact process restart passed')
