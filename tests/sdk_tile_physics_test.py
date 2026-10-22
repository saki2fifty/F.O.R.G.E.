"""Original exact-SDK painted collision through both real hosts and fresh recovery."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from runtime_client import Runtime

runtime, module, gallery = (Path(value).resolve() for value in sys.argv[1:4])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']


def replace_scene(worker, scene, sdk_play):
    response = worker.request('replace', scene=scene)
    if not sdk_play:
        return response
    deadline = time.monotonic() + 10
    while not response.get('candidate'):
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request('snapshot')
    ticket = response['candidate']['ticket']
    candidate = worker.request('candidate', ticket=ticket)['candidate']
    assert candidate['session'] == worker.session
    response = worker.request('snapshot', candidate_ack=dict(
        session=worker.session, ticket=ticket, accepted=True, diagnostic=''))
    while response['activation']['generation'] != ticket:
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request('snapshot')
    return worker.request('snapshot', editor_epoch=dict(
        epoch=ticket, captured=False, input_device='EditorHost'))


def motion(state):
    return state['recovery']['physics']['planar']['metadata']['mapping']


with tempfile.TemporaryDirectory(dir=runtime.parent) as directory:
    project = Path(directory) / 'painted SDK project'
    shutil.copytree(gallery, project)
    shutil.copyfile(module, project / module.name)
    settings_path = project / 'forge.project.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8'))
    settings['modules'] = [dict(id='project.tile_physics_probe', implementation='1',
        sdk='experimental-1', fingerprint=fingerprint, library=module.name,
        dependencies=['forge.physics', 'forge.resources'])]
    settings_path.write_text(json.dumps(settings), encoding='utf-8')
    scene_path = project / 'Scenes/77-painted-2d-collision.scene.json'
    source = scene_path.read_bytes()
    map_path = project / 'Assets/Tiles/PaintedPlatforms.tilemap.json'
    map_source = map_path.read_bytes()
    scene = json.loads(source)
    for sdk_play in (False, True):
        arguments = ['--sdk-project', project]
        if sdk_play:
            arguments += ['--sdk-play', 'on', '--user-data', project / 'users']
        a, b = Runtime(runtime, arguments), None
        try:
            initial = replace_scene(a, scene, sdk_play)
            assert initial['physics']['bodies_2d'] == 5
            assert initial['physics']['shapes_2d'] == 8
            applied = False
            for tick in range(1, 151):
                left = a.request('step')
                applied |= any(row['text'] == 'Painted SDK edit applied'
                               for row in left['diagnostics'])
                if tick >= 2:
                    assert left['physics']['shapes_2d'] == 7, left['physics']
                if b:
                    assert motion(b.request('step')) == motion(left)
                if not sdk_play and tick == 10:
                    checkpoint = left['recovery']
                    assert checkpoint['version'] == 2
                    assert checkpoint['tile_maps']['version'] == 1
                    entry, = checkpoint['tile_maps']['entries']
                    assert entry['edited'] and entry['revision'] == 1 and entry['cells']
                    b = Runtime(runtime, arguments)
                    restored = b.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick'] == tick
                    assert restored['physics']['shapes_2d'] == 7
                    assert motion(restored) == motion(left)
                    assert restored['recovery']['tile_maps'] == checkpoint['tile_maps']
                if sdk_play:
                    assert left['recovery'] is None  # Arbitrary native Play state is not restored.
            assert applied, 'The matching loaded C++ module never completed its cell edit'
            amber = next(row for row in left['scene']['entities'] if row['name'].startswith('Amber'))
            assert abs(amber['components']['forge.local_translation']['y'] + 2.55) < .025, amber
            if b:
                # Destroy/restart the worker, then rebuild edited Map values before native shapes.
                checkpoint = left['recovery']
                b.close()
                b = Runtime(runtime, arguments)
                restarted = b.request('replace', scene=left['scene'], recovery=checkpoint,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert motion(restarted) == motion(left)
                assert motion(b.request('step')) == motion(a.request('step'))
            clean = replace_scene(a, scene, sdk_play)
            assert clean['timing']['tick'] == 0 and clean['physics']['shapes_2d'] == 8
            assert scene_path.read_bytes() == source and map_path.read_bytes() == map_source
        finally:
            a.close()
            if b:
                b.close()

print('Exact-SDK painted edit/query helpers, both real hosts, fresh edited-Map-before-physics '
      'recovery, deterministic continuation and clean source replacement passed')
