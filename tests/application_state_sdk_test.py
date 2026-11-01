"""Exact public state conditions/events/queued controls in both runtime hosts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
from runtime_client import Runtime, activate

DOMAIN = 'application_session_with_owned_strings'
MENU = 'menu_with_owned_storage'
PLAY = 'play_with_owned_storage'
PARENT = 'game_with_owned_storage'


def verify(runtime, module, tools, settings_source, scratch):
    runtime, module, tools, settings_source, scratch = map(Path, (runtime, module, tools, settings_source, scratch))
    scratch.mkdir(parents=True, exist_ok=True)
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    with tempfile.TemporaryDirectory(prefix='state-sdk-', dir=scratch) as folder:
        root = Path(folder)
        (root/'Assets').mkdir()
        settings = json.loads(settings_source.read_text(encoding='utf-8'))
        settings['startup_scene'] = None
        settings['modules'] = [dict(id='project.application_states_probe', implementation='1',
                                   sdk='experimental-1', fingerprint=fingerprint,
                                   library=module.name, dependencies=[])]
        (root/'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
        shutil.copyfile(module, root/module.name)
        (root/'forge.assets.json').write_text(json.dumps(dict(version=1, assets=[])), encoding='utf-8')
        asset = str(uuid.uuid4())
        definition = dict(format='forge.application-states', version=1, asset_id=asset, domains=[
            dict(key=DOMAIN, initial=MENU, states=[dict(key=MENU), dict(key=PARENT), dict(key=PLAY, parent=PARENT)]),
            dict(key='hud', initial='hidden', computed=True,
                 states=[dict(key='hidden'), dict(key='visible')],
                 rules=[dict(state='visible', all=[dict(domain=DOMAIN, state=PARENT)])])])
        locator = 'Assets/Probe.appstates.json'
        (root/locator).write_text(json.dumps(definition), encoding='utf-8')
        subprocess.run([tools, '--assets', 'import', root, locator], check=True,
                       stdout=subprocess.DEVNULL, timeout=30)
        (root/'Assets').rename(root/'Source offline')
        scene = dict(version=3, asset_id=str(uuid.uuid4()), entities=[
            dict(id=str(uuid.uuid4()), name='Modes', spatial=dict(mode='world'),
                 components={'forge.application_states': dict(definition=asset)})])
        original = copy.deepcopy(scene)
        for sdk in (False, True):
            worker = Runtime(runtime, ['--sdk-project', root, '--user-data', root/('sdk-users' if sdk else 'ordinary-users'),
                                       *(['--sdk-play', 'on'] if sdk else [])])
            try:
                for incarnation in range(2):
                    current = activate(worker, scene, sdk)
                    assert current['application_states']['domains'][0]['active'] == MENU, current
                    reports = []
                    if sdk:
                        for _ in range(5):
                            current = worker.request('step')
                            reports += [row['text'] for row in current['diagnostics']]
                        expected = 'Application state SDK conditions, owned events, queued receipts and rejection passed'
                    else:
                        generation = current['world_generation']
                        current = worker.request('step')
                        reports += [row['text'] for row in current['diagnostics']]
                        worker.request('application_state', generation=generation, domain=DOMAIN,
                                       state=PLAY, expected_revision=1)
                        current = worker.request('step')
                        reports += [row['text'] for row in current['diagnostics']]
                        worker.request('application_state', generation=generation, domain=DOMAIN,
                                       state=MENU, expected_revision=2)
                        current = worker.request('step')
                        reports += [row['text'] for row in current['diagnostics']]
                        expected = 'Application state SDK legacy conditions, owned events and unavailable Game guard passed'
                    assert expected in reports, (sdk, incarnation, reports)
                    assert not any(row['severity'].lower() in ('error', 'fatal') for row in current['diagnostics']), current['diagnostics']
                    assert current['application_states']['domains'][0]['active'] == MENU
                    assert current['application_states']['domains'][0]['revision'] == 3
                    assert scene == original
                    assert current['recovery'] is None if sdk else current['recovery'] is not None
                worker.request('quit')
                assert worker.process.wait(timeout=10) == 0
            finally:
                worker.close()
    print('Public SDK state ancestor query, owned events, receipts/rejections and fresh-World reset passed in both hosts')


if __name__ == '__main__':
    verify(*(Path(value).resolve() for value in sys.argv[1:6]))
