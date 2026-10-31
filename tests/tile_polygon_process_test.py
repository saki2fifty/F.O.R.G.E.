"""Collision profiles and copied definitions through actual isolated Play hosts."""
import json
from pathlib import Path
import shutil
import sys
import tempfile

from runtime_client import Runtime, activate


runtime, gallery = (Path(value).resolve() for value in sys.argv[1:3])
with_sdk_play = len(sys.argv) > 3 and sys.argv[3].lower() in ('on', 'true', '1')
custom_one_way = len(sys.argv) > 4
scene_path = sys.argv[4] if custom_one_way else 'Scenes/101-tile-collision-profiles.scene.json'
saved = json.loads((gallery / scene_path).read_text(encoding='utf-8'))
actors = [row for row in saved['entities'] if 'forge.physics_body_2d' in row['components']
          and row['components']['forge.physics_body_2d']['motion'] == 2]
box = None if custom_one_way else next(row['id'] for row in actors if 'forge.box_collider_2d' in row['components'])


def positions(state):
    return {row['id']: row['components']['forge.local_translation']
            for row in state['scene']['entities'] if row['id'] in {actor['id'] for actor in actors}}


def restore(worker, state):
    recovery = state['recovery']
    return worker.request('replace', scene=state['scene'], recovery=recovery,
                          recovery_session=recovery['session'], recovery_tick=recovery['tick'])


def copied_profiles(worker, state):
    manifest = state['tile_maps']
    assert len(manifest['maps']) == 1, manifest
    row = manifest['maps'][0]
    assert row['ready'] and row['cells'] == (5 if custom_one_way else 23), row
    scope = dict(world=manifest['world'], scene=manifest['scene'])
    reply = worker.request('tile_map_begin', **scope, entity=row['entity'],
                           generation=row['generation'], revision=row['revision'],
                           base_generation=0, base_revision=0)['tile_projection']
    assert not reply['stale'] and reply['full']
    assert reply['header']['definitions_version'] == (6 if custom_one_way else 2)
    definitions = {tile['id']: tile['collision'] for tile in reply['header']['definitions']['tiles']}
    if custom_one_way:
        assert definitions[2]['kind'] == definitions[3]['kind'] == 'contours'
        assert len(definitions[2]['contours']) == 2
        assert len(definitions[2]['contours'][0]['holes']) == 1
        assert definitions[4] == dict(kind='full_cell')
    else:
        assert definitions[2] == dict(kind='full_cell')
        assert definitions[3] == dict(kind='convex', points=[[0, 0], [1, 0], [1, 1]])
        assert definitions[4] == dict(kind='none')
    cursor = 0
    while cursor < reply['count']:
        page = worker.request('tile_map_page', **scope, token=reply['token'], cursor=cursor)['tile_projection']
        assert page['next'] > cursor
        cursor = page['next']
    assert worker.request('tile_map_ack', **scope, token=reply['token'])['tile_projection']['accepted']


with tempfile.TemporaryDirectory(prefix='forge-tile-polygons-', dir=runtime.parent) as temporary:
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
            assert state['physics']['bodies_2d'] == (7 if custom_one_way else 5)
            assert state['physics']['shapes_2d'] == (22 if custom_one_way else 14)
            copied_profiles(worker, state)
            for tick in range(1, 181):
                state = worker.request('step')
                if tick == 90 and not sdk_play:
                    peer = Runtime(runtime, arguments)
                    restored = restore(peer, state)
                    assert positions(restored) == positions(state)
                elif tick > 90 and peer:
                    assert positions(peer.request('step')) == positions(state)
            if custom_one_way:
                heights = {'Amber - outer ring roof': .15, 'Cyan - inside the ring void': -2.85,
                           'Rose - island inside the void': -1.35, 'Green - between separate bars': -3.85,
                           'Violet - upward passage through the ring': 2.1}
                for actor in actors:
                    assert abs(positions(state)[actor['id']]['y'] - heights[actor['name']]) < .04, actor['name']
            else:
                assert abs(positions(state)[box]['y'] + 2.55) < .03
                for actor in actors:
                    if actor['id'] != box:
                        assert abs(positions(state)[actor['id']]['x'] -
                                   actor['components']['forge.local_translation']['x']) > .25
            copied_profiles(worker, state)
            if sdk_play:
                assert state['recovery'] is None
            else:
                assert state['recovery']['physics']['version'] == 10
                peer.close()
                peer = Runtime(runtime, arguments)
                assert positions(restore(peer, state)) == positions(state)
                assert positions(worker.request('step')) == positions(peer.request('step'))
            assert (root / scene_path).read_bytes() == before
        finally:
            worker.close()
            if peer:
                peer.close()

print(('Original custom one-way boundaries, ring/void/island landings and upward passage' if custom_one_way
       else 'Convex/None profiles') + ', copied Play definitions, motion and ordinary process restart passed'
      + (' in both actual hosts' if with_sdk_play else ' in the ordinary host'))
