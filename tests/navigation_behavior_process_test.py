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
scene = json.loads((root / 'Scenes/131-patrol-behavior.scene.json').read_text(encoding='utf-8'))
actor = next(e['id'] for e in scene['entities'] if e['name'] == 'Patrol agent - inspect behavior')

def row(state):
    return next(e for e in state['effective_scene']['entities'] if e['id'] == actor)

poses = []
for sdk in (False, True):
    args = ['--sdk-project', root, '--user-data', root / ('sdk-users' if sdk else 'ordinary-users')]
    if sdk:
        args += ['--sdk-play', 'on']
    host = Runtime(runtime, args)
    try:
        activate(host, scene, sdk)
        positions = []
        for _ in range(30):
            state = host.request('step')
            value = row(state)
            assert value['navigation_behavior_debug']['phase'] == 'Running', value
            assert 'forge.runtime.navigation_behavior_cursor' not in value['components']
            positions.append(value['components']['forge.local_translation'])
        assert any('Navigation behavior SDK progress/resource passed' in e['text'] for e in state['diagnostics']), state['diagnostics']
        assert not any(e['severity'].lower() in ('error', 'fatal') for e in state['diagnostics']), state['diagnostics']
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
        assert row(state)['navigation_behavior_debug']['elapsed'] > observation['elapsed']
        poses.append(positions)
    finally:
        host.close()
assert poses[0] == poses[1], 'Ordinary and SDK Play behavior movement differs'
print('Public behavior layout/progress/kind8 and live controls pass in both hosts; copied movement matches')
