"""Public localization value/lifetime, Pause controls and recovery in both runtime hosts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from runtime_client import Runtime, activate

runtime, module, gallery, scratch = (Path(v).resolve() for v in sys.argv[1:5])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-localization-', dir=scratch) as temporary:
    root = Path(temporary) / 'Project'
    shutil.copytree(gallery, root)
    shutil.copy2(module, root / module.name)
    info = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))
    settings = json.loads((root / 'forge.project.json').read_text(encoding='utf-8'))
    settings['startup_scene'] = None
    settings['modules'] = [dict(id='project.ui_localization_probe', implementation='1',
        sdk='experimental-1', fingerprint=info['fingerprint'], library=module.name,
        dependencies=['forge.ui'])]
    (root / 'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
    source = json.loads((root / 'Scenes/133-ui-localization.scene.json').read_text(encoding='utf-8'))
    actor = next(e['id'] for e in source['entities'] if 'forge.ui_localization' in e['components'])

    def locale(state):
        return next(e['components']['forge.ui_localization']['locale']
                    for e in state['effective_scene']['entities'] if e['id'] == actor)

    for sdk in (False, True):
        args = ['--sdk-project', root, '--user-data', root / ('sdk-users' if sdk else 'ordinary-users'),
                '--ui', 'on']
        if sdk:
            args += ['--sdk-play', 'on']
        host = Runtime(runtime, args)
        try:
            active = activate(host, source, sdk)
            assert locale(active) == 'en' and active['timing']['paused'], active
            stepped = host.request('step')
            assert locale(stepped) == stepped['ui']['localization']['locale'] == 'fr'
            assert any('Public UI Localization value/lifetime/write passed' in d['text']
                       for d in stepped['diagnostics']), stepped['diagnostics']
            tick = stepped['timing']['tick']
            state = stepped['ui']
            command = dict(version=1, session=state['session'], generation=state['generation'],
                           id=1, instance=state['documents'][0]['instance'], command='forge.locale', value='es')
            changed = host.request('ui', ui_command=command)
            assert changed['ui_ack']['ok'] and locale(changed) == 'es'
            assert changed['timing']['tick'] == tick and changed['timing']['paused']
            duplicate = host.request('ui', ui_command=command)
            assert duplicate['ui_ack'] == changed['ui_ack'] and locale(duplicate) == 'es'
            generation = changed['activation']['generation'] if sdk else changed['world_generation']
            target = dict(generation=generation, scene_asset=source['asset_id'],
                          entity=actor, type='forge.ui_localization')
            tuned = host.request('property_tune', **target, property='locale', value='fr-CA')
            assert locale(tuned) == 'fr-CA' and tuned['timing']['tick'] == tick
            for field, value in (('locale', 'x'), ('catalog', None)):
                try:
                    host.request('property_tune', **target, property=field, value=value)
                except RuntimeError:
                    pass
                else:
                    raise AssertionError('Invalid locale or live catalog change accepted')
            checkpoint = copy.deepcopy(host.request('snapshot')['recovery'])
            if sdk:
                assert checkpoint is None, 'Prepared SDK Play must preserve its existing no-recovery contract'
            else:
                assert checkpoint is not None, 'Ordinary runtime omitted its recovery checkpoint'
                restored = activate(host, checkpoint['scene'], False, recovery=checkpoint,
                                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                assert locale(restored) == 'fr-CA' and restored['timing']['tick'] == tick
                assert restored['ui']['generation'] != changed['ui']['generation']
            restored_source = activate(host, source, sdk)
            assert locale(restored_source) == 'en', 'Restart did not restore the authored locale'
            assert host.request('quit')['ok']
            assert host.process.wait(timeout=20) == 0, 'Runtime did not retire cleanly'
        finally:
            host.close()

print('Public UI Localization string ownership/layout, native writes, paused controls/live tuning, authored restart in both hosts, and ordinary-runtime recovery passed')
