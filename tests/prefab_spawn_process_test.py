"""Actual optional Gallery C++ consumer in direct-host and SDK Play processes."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from physics_velocity_example_test import Worker

runtime, gallery, module = (Path(arg).resolve() for arg in sys.argv[1:4])
scene_id = 'fab9f548-3f22-4fbc-a5e0-bd0a2cffd9a1'
prefab_id = '3f95b972-aa2a-4fab-999a-8857a5a35e11'
scene_path = Path('Scenes/64-runtime-prefab-creation.scene.json')
prefab_path = Path('Assets/RuntimeRobot.prefab.json')
dt = struct.unpack('f', struct.pack('f', 1/60))[0]


def check(state, tick):
    assert not any(d['severity'].lower() in ('error', 'fatal')
                   for d in state['diagnostics']), state['diagnostics']
    roots = {r['name']: r for r in state['scene']['entities'] if 'prefab_instance' in r}
    expected = 0 if tick < 2 else 2 if tick < 121 else 3
    assert len(roots) == expected, (tick, list(roots))
    ids = set()
    values = {r['id']: r['components'] for r in state['effective_scene']['entities']}
    for index, name in enumerate(('Spawned left', 'Spawned right', 'Spawned later')):
        if name not in roots:
            continue
        row = roots[name]
        members = set(row['prefab_instance']['members'].values())
        assert len(members) == 2 and not ids.intersection(members), roots
        ids.update(members)
        pose = values[row['id']]
        assert pose['forge.local_translation'] == dict(x=(-4 if index == 0 else 4 if index == 1 else 0),
                                                       y=1.3, z=(-3 if index == 2 else 0)), pose
        count = tick - (1 if index < 2 else 120)
        radians = count * dt * (80 if index == 0 else -80 if index == 1 else 150) * math.pi / 180
        quaternion = pose['forge.local_rotation']
        assert abs(quaternion['y']-math.sin(radians/2)) < 3e-6, (tick, index, quaternion, radians)
        assert abs(quaternion['w']-math.cos(radians/2)) < 3e-6, (tick, index, quaternion, radians)
    return ids


with tempfile.TemporaryDirectory(prefix='forge-prefab-sdk-', dir=runtime.parent) as scratch:
    root = Path(scratch)/'project'
    for relative in (scene_path, prefab_path):
        (root/relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(gallery/relative, root/relative)
    shutil.copyfile(module, root/module.name)
    catalog = json.loads((gallery/'forge.assets.json').read_text())
    catalog['assets'] = [r for r in catalog['assets'] if r['id'] in (scene_id, prefab_id)]
    assert len(catalog['assets']) == 2
    (root/'forge.assets.json').write_text(json.dumps(catalog))
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    game = json.loads((gallery/'forge.project.json').read_text())['game']
    game['application_id'] = 'org.forge.runtime-prefab-probe'
    settings = dict(version=2, name='Runtime Prefab SDK', game=game,
                    startup_scene=dict(asset=scene_id, source=scene_path.as_posix()),
                    input=dict(version=1, actions=[]), modules=[dict(
                        id='project.prefab_spawn_probe', implementation='1', sdk='experimental-1',
                        fingerprint=fingerprint, library=module.name, dependencies=['forge.transforms'])])
    (root/'forge.project.json').write_text(json.dumps(settings))
    original = (root/scene_path).read_bytes()
    source = json.loads(original)
    source['_prefab_sources'] = [json.loads((root/prefab_path).read_bytes())]
    direct = Worker(runtime, root)
    try:
        state = direct.request('replace', scene=source)
        check(state, 0)
        for tick in range(1, 126):
            state = direct.request('step')
            direct_ids = check(state, tick)
    finally:
        direct.close()

    class EditorWorker(Worker):
        def __init__(self):
            self.process = subprocess.Popen([runtime, '--sdk-project', root, '--sdk-play', 'on',
                '--audio', 'offline', '--user-data', Path(scratch)/'user-data'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, cwd=root)
            self.sequence, self.session = 0, ''
            self.request('hello')

    editor = EditorWorker()
    try:
        # Keep caller scene edits while resolving an unused declared definition.
        edited = json.loads(original)
        stage = next(row for row in edited['entities'] if row['name'] == 'Floor')
        stage['name'] = 'Unsaved stage'
        state = editor.request('replace', scene=edited)
        deadline = time.monotonic()+20
        while state.get('candidate') is None:
            assert time.monotonic() < deadline, state.get('loading')
            time.sleep(.005)
            state = editor.request('snapshot')
        ticket = state['candidate']['ticket']
        assert editor.request('candidate', ticket=ticket).get('candidate')
        state = editor.request('snapshot', editor_epoch=dict(epoch=1, captured=False,
                    input_device='KeyboardMouse'), candidate_ack=dict(session=editor.session,
                    ticket=ticket, accepted=True, diagnostic=''))
        while state.get('activation', {}).get('state') not in ('ready', 'active'):
            assert time.monotonic() < deadline, state.get('activation')
            time.sleep(.005)
            state = editor.request('snapshot')
        check(state, 0)
        assert next(row for row in state['scene']['entities'] if row['id'] == stage['id'])['name'] == 'Unsaved stage'
        for tick in range(1, 126):
            play_ids = check(editor.request('step'), tick)
        assert play_ids.isdisjoint(direct_ids), 'Fresh runtime reused generated member identities'
    finally:
        editor.close()
    assert (root/scene_path).read_bytes() == original, 'Runtime spawning wrote the authored scene'
print('Actual Gallery64 C++ delayed creation/placement/rotation in direct host and SDK Play passed')
