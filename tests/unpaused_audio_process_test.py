"""Original public module in both actual Play hosts, including paused controls."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import time
from runtime_client import Runtime, activate

runtime, module = (Path(p).resolve() for p in sys.argv[1:3])
source = Path(__file__).resolve().parents[1]
gallery = source / 'samples/projects/FeatureGallery'
scene = json.loads((gallery / 'Scenes/84-independent-audio.scene.json').read_text())
for actor in scene['entities']:
    if actor['id'] == '15100000-1510-4151-8151-151000000002':
        actor['components']['forge.audio_source'].update(low_pass_enabled=True, low_pass_hz=1200.,
                                                       high_pass_enabled=True, high_pass_hz=100.)
scene_id = scene['asset_id']
ui_ref = dict(scene=scene_id, entity='15100000-1510-4151-8151-151000000002')
game_ref = dict(scene=scene_id, entity='15100000-1510-4151-8151-151000000003')
clip = next(a for a in json.loads((gallery/'forge.assets.json').read_text())['assets']
            if a['source'] == 'Assets/FalloffTone.wav')
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
for sdk_play in (False, True):
    with tempfile.TemporaryDirectory(prefix='unpaused-audio-', dir=runtime.parent) as folder:
        root = Path(folder)
        shutil.copy2(module, root/module.name)
        (root/'Assets').mkdir()
        shutil.copy2(gallery/clip['source'], root/clip['source'])
        record = dict(id=clip['id'], type='audio_clip', source=clip['source'], schema_version=1,
                      dependencies=[], metadata={})
        (root/'forge.assets.json').write_text(json.dumps(dict(version=1, assets=[record])))
        input_map = json.loads((source/'samples/native_sdk/unpaused_audio.input.json').read_text())
        game = json.loads((gallery/'forge.project.json').read_text())['game']
        game['application_id'] = 'org.forge.unpaused-audio'
        project = dict(version=2, name='Independent audio', startup_scene=None, game=game,
                       input=input_map,
                       modules=[dict(id='project.unpaused_audio', implementation='1',
                                     sdk='experimental-1', fingerprint=fingerprint,
                                     library=module.name, dependencies=['forge.audio', 'forge.input'])])
        (root/'forge.project.json').write_text(json.dumps(project))
        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on' if sdk_play else 'off',
                                   '--audio', 'offline', '--user-data', root/'users'])
        try:
            active = activate(worker, scene, sdk_play)
            if sdk_play:
                worker.request('snapshot', editor_epoch=dict(
                    epoch=active['activation']['generation'] + 1, captured=False,
                    input_device='KeyboardMouse'))
            state = worker.request('snapshot', audio_source_watch=ui_ref)
            assert state['timing']['paused'] and state['audio']['paused']
            assert not state['audio']['suspended'] and not state['audio_source']['paused'], state
            assert state['audio_source']['low_pass_enabled'] and state['audio_source']['high_pass_enabled'], state
            assert state['audio_source']['low_pass_hz'] == 1200 and state['audio_source']['high_pass_hz'] == 100
            assert worker.request('snapshot', audio_source_watch=game_ref)['audio_source']['paused']

            def press(key, expected, shots):
                worker.request('snapshot', input_events=[dict(control=key, value=1)],
                               audio_source_watch=ui_ref)
                deadline = time.monotonic() + 5
                while True:
                    current = worker.request('snapshot', audio_source_watch=ui_ref)
                    assert current['timing']['tick'] == 0 and current['timing']['paused'], current
                    if any(d['text'] == expected for d in current['diagnostics']):
                        assert current['audio']['one_shots'] == shots, current
                        break
                    assert time.monotonic() < deadline, current
                    time.sleep(.005)
                worker.request('snapshot', input_events=[dict(control=key, value=0)])

            if sdk_play:
                press('key.r', 'Independent UI one-shot queued', 1)
                press('key.y', 'Gameplay-only control rejected as expected', 1)
                press('key.t', 'Independent UI stopped', 0)
            else:
                # The retained internal driver has protocol controls, not SDK
                # control callbacks. Production C++ controls use SDK Play above.
                value = worker.request('audio_source', source=ui_ref, operation='one_shot')
                assert value['audio']['one_shots'] == 1, value
                value = worker.request('audio_source', source=ui_ref, operation='stop')
                assert value['audio']['one_shots'] == 0, value
            assert worker.request('snapshot')['timing']['tick'] == 0
            worker.request('quit')
        finally:
            worker.close()
print('Both hosts preserve pause policy; production SDK Play admits paused C++ UI controls')
