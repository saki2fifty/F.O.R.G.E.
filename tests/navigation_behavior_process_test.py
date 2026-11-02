"""Actual public behavior POD/cursor and resource-kind8 through both runtime hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
from runtime_client import Runtime, activate

runtime, module, gallery, scratch = (Path(v).resolve() for v in sys.argv[1:5])
root = scratch / str(uuid.uuid4())
shutil.copytree(gallery, root)
shutil.copy2(module, root / module.name)
info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
settings = json.loads((root / 'forge.project.json').read_text(encoding='utf-8'))
settings['startup_scene'] = None
settings['modules'] = [dict(id='project.navigation_behavior_probe', implementation='1',
    sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
    dependencies=['forge.navigation', 'forge.resources'])]
(root / 'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')

def exercise(source, name, tree=False, links=False, collection=False):
    scene = json.loads((root / source).read_text(encoding='utf-8'))
    actor = next(e['id'] for e in scene['entities'] if e['name'] == name)
    def row(state):
        return next(e for e in state['effective_scene']['entities'] if e['id'] == actor)

    poses = []
    for sdk in (False, True):
        args = ['--sdk-project', root, '--user-data', root / ('sdk-users' if sdk else 'ordinary-users')]
        if sdk:
            args += ['--sdk-play', 'on']
        print('SDK navigation case:', source, 'sdk_play=', sdk, 'starting', flush=True)
        host = Runtime(runtime, args)
        try:
            activate(host, scene, sdk)
            print('SDK navigation case activated:', source, 'sdk_play=', sdk, flush=True)
            positions = []
            leaves = set()
            crossed_link = False
            passed_wall = False
            for _ in range(300 if collection else 240 if links else 180 if tree else 30):
                state = host.request('step')
                value = row(state)
                assert value['navigation_behavior_debug']['phase'] == 'Running', value
                assert 'forge.runtime.navigation_behavior_cursor' not in value['components']
                positions.append(value['components']['forge.local_translation'])
                if collection:
                    point = positions[-1]
                    assert value['navigation_debug']['phase'] != 'Failed', value
                    if abs(point['x']) < .5:
                        assert abs(point['z']) > 4, point
                    passed_wall |= point['x'] > 3.8
                if links and value.get('navigation_debug', {}).get('link') is not None:
                    crossed_link = True
                    assert value['navigation_debug']['position'][2] == -1, value
                if tree:
                    debug = value['navigation_behavior_debug']
                    assert debug['profile'] == 'tree' and debug['tree_status'] in ('Running', 'Success'), debug
                    leaves.add(debug['node'])
            assert any('Navigation behavior SDK progress/resource passed' in e['text'] for e in state['diagnostics']), state['diagnostics']
            assert not any(e['severity'].lower() in ('error', 'fatal') for e in state['diagnostics']), state['diagnostics']
            if links:
                assert crossed_link, 'SDK host did not traverse an admitted link'
                assert any('Navigation link SDK authored source passed' in e['text'] for e in state['diagnostics']), state['diagnostics']
            if tree:
                assert {4, 6, 7} <= leaves, leaves
                assert any('Navigation behavior SDK tree progress passed' in e['text'] for e in state['diagnostics'])
            if collection:
                assert passed_wall, 'SDK host did not follow the collected physical route'
            observation = row(state)['navigation_behavior_debug']
            generation = state['activation']['generation'] if sdk else state['world_generation']
            target = dict(generation=generation, scene_asset=scene['asset_id'],
                          entity=actor, type='forge.navigation_behavior')
            tuned = host.request('property_tune', **target, property='enabled', value=False)
            assert tuned['property_tune']['value'] is False
            for _ in range(20):
                state = host.request('step')
                current = row(state)['navigation_behavior_debug']
                assert current['phase'] == 'Disabled'
                assert all(current[k] == observation[k] for k in ('state', 'elapsed', 'transitions'))
            for field in ('graph', 'target', 'anchor'):
                try:
                    host.request('property_tune', **target, property=field, value=None)
                except RuntimeError:
                    pass
                else:
                    raise AssertionError('Live behavior reference/identity tuning was accepted')
            host.request('property_tune', **target, property='enabled', value=True)
            state = host.request('step')
            assert row(state)['navigation_behavior_debug']['elapsed'] > observation['elapsed'] or (
                tree and row(state)['navigation_behavior_debug']['steps'] > observation['steps'])
            poses.append(positions)
            print('SDK navigation case verified:', source, 'sdk_play=', sdk, flush=True)
            host.request('quit')
            assert host.process.wait(timeout=10) == 0, 'Behavior runtime did not exit cleanly'
        finally:
            host.close()
    differences = [(i + 1, a, b) for i, (a, b) in enumerate(zip(*poses)) if a != b]
    assert not differences, ('Ordinary and SDK Play behavior movement differs', differences[:3])

exercise('Scenes/131-patrol-behavior.scene.json', 'Patrol agent - inspect behavior')
exercise('Scenes/139-behavior-tree.scene.json', 'Tree agent - inspect Node and Tree result', True)
exercise('Scenes/140-navigation-links.scene.json', 'Cyan patrol - two-way link', links=True)
exercise('Scenes/141-navigation-collision-collection.scene.json',
         'Cyan patrol - collected physical geometry', collection=True)
print('Public behavior/link layout and string ownership, typed progress, kind8, live controls and exact ordinary/SDK host movement passed')
