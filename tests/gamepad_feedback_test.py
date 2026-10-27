"""Prepare an ordinary public-SDK feedback project, run its real SDL/Play adapter."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

runtime, module, driver = (Path(p).resolve() for p in sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix='feedback-project-', dir=runtime.parent) as temporary:
    root = Path(temporary)
    shutil.copy2(module, root/module.name)
    source = Path(__file__).resolve().parents[1]
    game = json.loads((source/'samples/projects/FeatureGallery/forge.project.json').read_text())['game']
    game['application_id'] = 'org.forge.gamepad-feedback-test'
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    actions = [dict(id=f'12345678-1234-4234-8234-123456789b0{i+1}', name=name, kind='digital',
                    context='Feedback', bindings=[dict(control=key), dict(control=pad)])
               for i, (name, key, pad) in enumerate((('Pulse', 'key.r', 'pad.south'),
                                                    ('Stop', 'key.t', 'pad.east')))]
    for index,(name,key) in enumerate((('Assign Player 2','key.y'),('Player 2 pulse','key.u')),start=3):
        actions.append(dict(id=f'12345678-1234-4234-8234-123456789b0{index}', name=name,
                            kind='digital',context='Feedback',bindings=[dict(control=key)]))
    project = dict(version=2, name='Gamepad feedback', startup_scene=None, simulation_hz=60,
                   game=game, input=dict(version=2, contexts=[dict(name='Feedback', active=True,
                    priority=10, consume=True, phase='control')], actions=actions), modules=[dict(
                    id='project.gamepad_feedback', implementation='1', sdk='experimental-1',
                    fingerprint=fingerprint, library=module.name,
                    dependencies=['forge.input', 'forge.game', 'forge.transforms'])])
    (root/'forge.project.json').write_text(json.dumps(project))
    metadata = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
    declaration = next(row for row in metadata['components'] if row['id'] == 'project.feedback_probe')
    stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
                 schema_version=declaration['schema_version'], digest=declaration['digest'])
    scene = dict(version=1, entities=[dict(id='feedback', name='Feedback probe', components={
        'project.feedback_probe': dict(requested=0, completed=0, failed=0, **{'$forge': stamp})})])
    (root/'feedback.scene.json').write_text(json.dumps(scene))
    subprocess.run([driver, runtime, root], check=True, timeout=60)
