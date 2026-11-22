"""Run the original impact System in a source-hidden production runtime."""
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from runtime_client import Runtime


def verify(runtime, module, fixture, gallery):
    runtime, module, fixture, gallery = map(Path, (runtime, module, fixture, gallery))
    fingerprint = json.loads(subprocess.check_output(
        [runtime, '--sdk-info'], text=True))['fingerprint']
    for planar in (False, True):
        with tempfile.TemporaryDirectory(prefix='particle-impact-runtime-', dir=runtime.parent) as temporary:
            scratch = Path(temporary)
            root = scratch / 'project'
            shutil.copytree(gallery, root)
            action = '--prepare-xy-scene-impacts' if planar else '--prepare-scene-impacts'
            subprocess.run([fixture, action, root], check=True, timeout=90)
            path = ('Scenes/262-xy-particle-impact-trails.scene.json' if planar else
                    'Scenes/261-scene-particle-impacts.scene.json')
            source = json.loads((root / path).read_text())
            actors = [row['id'] for row in source['entities']
                      if 'forge.particle_emitter' in row['components']]
            assert len(actors) == 2
            (root / 'Native').mkdir(exist_ok=True)
            shutil.copy2(module, root / 'Native' / module.name)
            project = dict(version=2, name='Particle impact runtime', startup_scene=None,
                           input=dict(version=1, actions=[]), modules=[dict(
                               id='project.particle_impact_gallery', implementation='1',
                               sdk='experimental-1', fingerprint=fingerprint,
                               library='Native/' + module.name,
                               dependencies=['forge.physics', 'forge.particles'])])
            (root / 'forge.project.json').write_text(json.dumps(project))
            # Only the saved catalog and published cache remain available.
            # The production host receives a detached Scene value over protocol2.
            (root / 'Assets').rename(scratch / 'unavailable Assets')
            (root / 'Scenes').rename(scratch / 'unavailable Scenes')
            worker = Runtime(runtime, ['--sdk-project', root], hello_fields={'simulation_hz': 60})
            try:
                state = worker.request('replace', scene=source)

                def emitters(response):
                    return [row for row in response['effective_scene']['entities']
                            if row['id'] in actors]

                deadline = time.monotonic() + 15
                while not all(row.get('particle_playback') for row in emitters(state)):
                    assert time.monotonic() < deadline, 'Impact runtime preparation stalled'
                    time.sleep(.005)
                    state = worker.request('snapshot')
                assert len(emitters(state)) == 2
                for row in emitters(state):
                    assert row['components']['forge.particle_emitter']['tint_g'] != .15
                for _ in range(180):
                    state = worker.request('step')
                assert state['timing']['tick'] == 180
                for row in emitters(state):
                    value = row['components']['forge.particle_emitter']
                    assert value['tint_r'] == 1 and abs(value['tint_g'] - .15) < 1e-6
                    assert value['tint_b'] == .5
                    assert row['particle_playback']['generation'] > 0
                observed = worker.request('snapshot')
                assert observed['timing']['tick'] == state['timing']['tick']
                assert emitters(observed) == emitters(state)
                worker.quit()
                assert worker.process.returncode == 0
            finally:
                worker.close()
    print('Source-hidden production runtime: 3D/XY impacts drove the original Gallery System; clean exits')


if __name__ == '__main__':
    verify(*(Path(value).resolve() for value in sys.argv[1:5]))
