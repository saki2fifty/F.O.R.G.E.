"""Public Camera output values through ordinary and isolated SDK Play hosts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
from runtime_client import Runtime, activate

runtime, module, settings_path, scratch = (Path(value).resolve() for value in sys.argv[1:5])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='camera-output-', dir=scratch) as temporary:
    root = Path(temporary)
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    settings = json.loads(settings_path.read_text())
    settings['startup_scene'] = None
    settings['modules'] = [dict(id='project.camera_output_probe', implementation='1', sdk='experimental-1',
                               fingerprint=info['fingerprint'], library=module.name, dependencies=[])]
    (root / 'forge.project.json').write_text(json.dumps(settings))
    (root / 'forge.assets.json').write_text(json.dumps(dict(version=1, assets=[])))
    actor, output = str(uuid.uuid4()), str(uuid.uuid4())
    for sdk in (False, True):
        arguments = ['--sdk-project', root, '--user-data', root / ('sdk-users' if sdk else 'ordinary-users')]
        if sdk:
            arguments += ['--sdk-play', 'on']
        host = Runtime(runtime, arguments)
        try:
            if not sdk:
                schema = host.request('snapshot')['schema']
            def defaults(name):
                component, = [item for item in schema['components'] if item['id'] == name]
                return {field['id']: field['default'] for field in component['fields']}
            camera = defaults('forge.camera')
            camera.update(render_texture=output, order=7, extension=dict(retain='camera'))
            source = dict(version=3, asset_id=str(uuid.uuid4()), entities=[dict(
                id=actor, name='Public output camera', components={
                    'forge.local_translation': defaults('forge.local_translation'),
                    'forge.local_rotation': defaults('forge.local_rotation'),
                    'forge.local_scale': defaults('forge.local_scale'), 'forge.camera': camera})])
            original = copy.deepcopy(source)
            def values(state):
                assert not any(row['severity'].lower() in ('error', 'fatal') for row in state['diagnostics']), state['diagnostics']
                return next(row['components']['forge.camera'] for row in state['effective_scene']['entities'] if row['id'] == actor)
            active = activate(host, source, sdk)
            assert values(active) == camera
            assert any('Public Camera output layout admitted' in row['text'] for row in active['diagnostics'])
            one = host.request('step')
            expected = camera | dict(render_texture=None, viewport_width=.5, order=8)
            assert values(one) == expected
            assert values(host.request('step')) == expected
            checkpoint = one['recovery']
            if sdk:
                assert checkpoint is None
            else:
                replay = activate(host, checkpoint['scene'], False, recovery=checkpoint,
                                  recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert values(replay) == expected
            assert values(activate(host, source, sdk)) == camera
            assert source == original
            assert host.request('quit')['ok'] and host.process.wait(timeout=20) == 0
        finally:
            host.close()
print('Public Camera output reference/layout, fixed writes, restart, ordinary recovery and both runtime hosts passed')
