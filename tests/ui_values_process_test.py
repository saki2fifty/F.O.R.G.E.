"""Installed value ownership and the shared UI model in both real runtime hosts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from runtime_client import Runtime, activate

runtime, module, gallery, scratch = (Path(value).resolve() for value in sys.argv[1:5])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-values-', dir=scratch) as temporary:
    root = Path(temporary) / 'Project'
    shutil.copytree(gallery, root)
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    settings = json.loads((root / 'forge.project.json').read_text())
    settings['startup_scene'] = None
    settings['modules'] = [dict(id='project.ui_values_probe', implementation='1', sdk='experimental-1',
                               fingerprint=info['fingerprint'], library=module.name, dependencies=['forge.ui'])]
    (root / 'forge.project.json').write_text(json.dumps(settings))
    source = json.loads((root / 'Scenes/151-sequence-ui.scene.json').read_text())
    for row in source['entities']:
        if 'forge.ui_values' in row['components']:
            row['components']['forge.ui_values']['values'].append(dict(name='manual', value=.5))
    original = copy.deepcopy(source)
    actor = next(row['id'] for row in source['entities'] if 'forge.ui_values' in row['components'])

    def values(state):
        return next(row['components']['forge.ui_values']['values'] for row in state['effective_scene']['entities'] if row['id'] == actor)

    for sdk in (False, True):
        arguments = ['--sdk-project', root, '--user-data', root / ('sdk-users' if sdk else 'ordinary-users'), '--ui', 'on']
        if sdk:
            arguments += ['--sdk-play', 'on']
        host = Runtime(runtime, arguments)
        try:
            active = activate(host, source, sdk)
            assert values(active) == [dict(name='amount', value=.2), dict(name='width', value=80), dict(name='manual', value=.5)]
            assert any('Public UI Values vector/string lifecycle passed' in row['text'] for row in active['diagnostics'])
            stepped = host.request('step')
            assert stepped['ui']['documents'][0]['model']['cpp_width'] == 80
            assert stepped['ui']['documents'][0]['model']['amount'] == .2
            generation = stepped['activation']['generation'] if sdk else stepped['world_generation']
            target = dict(generation=generation, scene_asset=source['asset_id'], entity=actor,
                          type='forge.ui_values', property='values')
            tuned_values = [dict(name='amount', value=.2), dict(name='width', value=80), dict(name='manual', value=.35)]
            tuned = host.request('property_tune', **target, value=tuned_values)
            assert values(tuned) == tuned_values and tuned['ui']['documents'][0]['model']['manual'] == .35
            tick = tuned['timing']['tick']
            for invalid in ([dict(name='amount', value=True)], [dict(name='tick', value=1)],
                            [dict(name='duplicate', value=1), dict(name='duplicate', value=2)]):
                try:
                    host.request('property_tune', **target, value=invalid)
                except RuntimeError:
                    pass
                else:
                    raise AssertionError('Invalid public UI Values accepted')
            held = host.request('snapshot')
            assert values(held) == tuned_values and held['timing']['tick'] == tick
            checkpoint = copy.deepcopy(held['recovery'])
            if sdk:
                assert checkpoint is None, 'SDK host must retain its existing no-rich-recovery contract'
            else:
                assert checkpoint is not None
                restored = activate(host, checkpoint['scene'], False, recovery=checkpoint,
                                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                # Recovery samples bound numbers and preserves the unanimated live value.
                assert values(restored) == tuned_values and restored['timing']['tick'] == tick
            restarted = activate(host, source, sdk)
            assert values(restarted) == original['entities'][next(i for i,row in enumerate(original['entities']) if row['id'] == actor)]['components']['forge.ui_values']['values']
            assert source == original
            assert host.request('quit')['ok'] and host.process.wait(timeout=20) == 0
        finally:
            host.close()
print('Public UI Values native lifetime, shared copied publication, paused validation/tuning/restart and both real runtime hosts passed')
