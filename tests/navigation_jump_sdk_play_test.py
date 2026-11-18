"""Actual jump observations through a matching project module in SDK Play."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from gallery_project_fixture import copy_gallery_project
from runtime_client import Runtime, activate

runtime, module, gallery, scratch = map(lambda p: Path(p).resolve(), sys.argv[1:5])
planar = '--planar' in sys.argv[5:]
source = ('Scenes/228-navigation-character-jumps-xy.scene.json' if planar
          else 'Scenes/226-navigation-character-jumps.scene.json')
landing_height = -2 if planar else 2
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='jump-sdk-', dir=scratch) as temporary:
    root = copy_gallery_project(gallery, Path(temporary) / 'Project', [source], startup=source)
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    project = json.loads((root / 'forge.project.json').read_text())
    project['modules'] = [dict(id='project.navigation_probe', implementation='1',
        sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.navigation', 'forge.input'])]
    project['game'] = dict(version=1, application_id='org.forge.navigation-jump-test',
        title='Physical jump', profile='development', save_schema=1,
        display=dict(mode='windowed', width=640, height=480, display=0, vsync=False),
        audio=dict(master_volume=1), input=dict(mouse_sensitivity=1, gamepad_sensitivity=1,
            invert_mouse_y=False, invert_gamepad_y=False, stick_deadzone=.15, trigger_deadzone=.05))
    (root / 'forge.project.json').write_text(json.dumps(project))
    scene = json.loads((root / source).read_text())
    actor = next(e for e in scene['entities'] if e['name'] == 'Free route character')
    for entity in scene['entities']:
        if entity['id'] != actor['id']:
            entity['components'].pop('forge.navigation_agent', None)
    expected = None
    for restart in range(2):
        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on',
                                   '--user-data', root / 'test-users'])
        try:
            activate(worker, scene, True)
            positions = []
            diagnostic_texts = set()
            airborne = False
            for tick in range(180):
                state = worker.request('step')
                diagnostic_texts.update(d['text'] for d in state['diagnostics'])
                observed = next(e for e in state['effective_scene']['entities'] if e['id'] == actor['id'])
                nav = observed['navigation_debug']
                assert nav['sampled'] and nav['has_path'] and nav['status'] == 'success', nav
                assert all(abs(nav['position'][axis] - observed['world_affine'][offset]) < 1e-6
                           for axis, offset in enumerate((3, 7, 11))), observed
                airborne |= -1 < nav['position'][0] < 1 and nav['position'][1] > (landing_height if planar else 1)
                positions.append(nav['position'])
                assert state['recovery'] is None, 'SDK Play advertised native checkpoint recovery'
                assert not any(d['text'].startswith('navigation ') and 'failed' in d['text']
                               for d in state['diagnostics']), state
                if nav['phase'] == 'Arrived':
                    assert airborne and abs(nav['position'][1] - landing_height) < .1, nav
                    break
            else:
                raise AssertionError(('SDK jump did not land', nav))
            for text in ['navigation SDK queries passed', 'navigation agent boundary passed',
                         'navigation agent PostPhysics sample passed',
                         *(['navigation polygon native values passed'] if planar else [])]:
                assert text in diagnostic_texts, (text, sorted(diagnostic_texts))
            if expected is None:
                expected = positions
            else:
                assert len(positions) == len(expected)
                assert all(abs(a - b) < 1e-6 for p, q in zip(positions, expected)
                           for a, b in zip(p, q)), 'SDK authored restart diverged'
            worker.quit()
        finally:
            worker.close()
print('Matching SDK query/PostPhysics actual jump/raised landing/authored restart passed')
