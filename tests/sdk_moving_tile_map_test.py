"""Public C++ Kinematic/trigger Map consumers through both real runtime hosts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from runtime_client import Runtime, activate


def verify(runtime, module, gallery):
    runtime, module, gallery = map(Path, (runtime, module, gallery))
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    with tempfile.TemporaryDirectory(prefix='map-profile-', dir=runtime.parent) as directory:
        root = Path(directory) / 'Map profile project'
        shutil.copytree(gallery, root)
        shutil.copyfile(module, root / module.name)
        path = root / 'forge.project.json'
        settings = json.loads(path.read_text())
        settings['modules'] = [dict(id='project.moving_tile_map_probe', implementation='1',
            sdk='experimental-1', fingerprint=fingerprint, library=module.name,
            dependencies=['forge.physics', 'forge.resources'])]
        path.write_text(json.dumps(settings))
        source = root / 'Scenes/77-painted-2d-collision.scene.json'
        before = source.read_bytes()
        saved = json.loads(before)
        for sdk_play in (False, True):
            for motion, sensor in ((0, False), (1, False), (0, True), (1, True)):
                scene = copy.deepcopy(saved)
                actor = next(e for e in scene['entities'] if 'forge.tile_map_collider_2d' in e['components'])
                body = actor['components']['forge.physics_body_2d']
                body.update(motion=motion, sensor=sensor)
                args = ['--sdk-project', root]
                if sdk_play:
                    args += ['--sdk-play', 'on', '--user-data', root / 'users']
                worker = Runtime(runtime, args)
                try:
                    initial = activate(worker, scene, sdk_play)
                    assert initial['physics']['bodies_2d'] == 5, initial
                    reports = []
                    for tick in range(1, 151):
                        current = worker.request('step')
                        assert current['physics']['shapes_2d'] == 8, current['physics']
                        reports += [row['text'] for row in current['diagnostics']
                                    if row['text'].startswith('Map profile SDK passed ')]
                    assert any(row.startswith(f'Map profile SDK passed 150 {motion} {int(sensor)} ')
                               for row in reports), reports
                    if sdk_play:
                        assert current['recovery'] is None
                    else:
                        assert current['recovery']['version'] == 2
                    row = next(e for e in current['scene']['entities'] if e['id'] == actor['id'])
                    assert abs(row['components']['forge.local_translation']['x'] -
                               (1.5 if motion else 0)) < 1e-5, row
                    assert source.read_bytes() == before
                finally:
                    worker.close()
    print('Public SDK Map flags/targets/queries/exclusions/trigger events passed all4 profiles in both hosts')


if __name__ == '__main__':
    verify(*(Path(p).resolve() for p in sys.argv[1:4]))
