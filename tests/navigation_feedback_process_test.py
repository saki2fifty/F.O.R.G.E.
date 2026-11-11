"""Read-only navigation details through actual ordinary/SDK Play snapshots."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from runtime_client import Runtime, activate

runtime, prepare, worker = (Path(value).resolve() for value in sys.argv[1:4])
planar = '--planar' in sys.argv[4:]
sdk_play = '--sdk-play' in sys.argv[4:]
root = Path(subprocess.check_output(
    [prepare, runtime.parent / 'navigation-feedback-process-data', worker,
     '--prepare-planar' if planar else '--prepare'], text=True, timeout=30).strip())
source = (root / 'scene.json').read_bytes()
scene = json.loads(source)
# Reuse FORGE's maintained original example game configuration, without a project
# code module. This exercises the production SDK host's built-in navigation owner.
gallery = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/forge.project.json'
game = json.loads(gallery.read_text(encoding='utf-8'))['game']
game['application_id'] = 'org.forge.navigation-feedback-test'
manifest = dict(version=2, name='Navigation feedback test', startup_scene=None, game=game,
                input=dict(version=1, actions=[]), modules=[])
(root / 'forge.project.json').write_text(json.dumps(manifest), encoding='utf-8')
project_bytes = (root / 'forge.project.json').read_bytes()
arguments = ['--sdk-project' if sdk_play else '--project', root]
if sdk_play:
    arguments += ['--sdk-play', 'on', '--user-data', root / 'test-users']
client = Runtime(runtime, arguments)


def actor(state):
    return next(row for row in state['effective_scene']['entities'] if row['name'] == 'Agent')


def debug(state):
    return actor(state)['navigation_debug']


def generation(state):
    return state['activation']['generation'] if sdk_play else state['world_generation']


try:
    state = activate(client, scene, sdk_play)
    assert debug(state)['diagnostic'] == '' and debug(state)['phase'] == 'Pending'
    state = client.request('step')
    assert debug(state)['sampled'] and debug(state)['diagnostic'] == ''
    target = dict(generation=generation(state), scene_asset=scene['asset_id'],
                  entity=actor(state)['id'], type='forge.navigation_agent')
    client.request('property_tune', **target, property='destination_x', value=100)
    failed = client.request('step')
    details = debug(failed)
    assert details['phase'] == 'Failed' and details['diagnostic'] and not details['sampled'], details
    tick = failed['timing']['tick']
    for _ in range(3):
        observed = client.request('snapshot')
        assert debug(observed) == details and observed['timing']['tick'] == tick
    changed = client.request('property_tune', **target, property='destination_x', value=8)
    assert debug(changed)['phase'] == 'Pending' and debug(changed)['diagnostic'] == ''
    resumed = client.request('step')
    assert debug(resumed)['sampled'] and debug(resumed)['diagnostic'] == ''
    bad = copy.deepcopy(scene)
    next(row for row in bad['entities'] if row['name'] == 'Agent')['components']['forge.navigation_agent']['navmesh'] = None
    if sdk_play:
        # SDK preparation admits navigation resources before publishing a candidate.
        # A bad replacement must retain the last active world, not become a live agent.
        previous_generation = generation(resumed)
        replaced = client.request('replace', scene=bad)
        deadline = time.monotonic() + 5
        while replaced.get('loading', {}).get('state') != 'failed':
            assert time.monotonic() < deadline, replaced.get('loading')
            time.sleep(.005)
            replaced = client.request('snapshot')
        assert 'Choose a NavMesh' in replaced['loading']['error'], replaced['loading']
        assert generation(replaced) == previous_generation
        assert debug(replaced)['sampled'] and debug(replaced)['diagnostic'] == ''
    else:
        # Ordinary replacement prepares the candidate before publishing it too.
        previous_generation = generation(resumed)
        try:
            client.request('replace', scene=bad)
        except RuntimeError as error:
            assert 'Choose a NavMesh' in str(error), error
        else:
            raise AssertionError('Missing NavMesh candidate was published')
        retained = client.request('snapshot')
        assert generation(retained) == previous_generation
        assert debug(retained) == debug(resumed), (retained, resumed)
    activate(client, scene, sdk_play)
    restored = client.request('step')
    assert debug(restored)['sampled'] and debug(restored)['diagnostic'] == ''
    assert (root / 'scene.json').read_bytes() == source
    assert (root / 'forge.project.json').read_bytes() == project_bytes
    print(('SDK Play' if sdk_play else 'Ordinary') + (' XY' if planar else ' 3D') +
          ': concrete route/asset failures, pure reads, correction/replacement and unchanged source passed')
finally:
    client.close()
    shutil.rmtree(root)
