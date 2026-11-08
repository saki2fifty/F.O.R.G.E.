"""Project routing and complete live faders through both runtime hosts and private recovery."""
import copy
import hashlib
import json
import math
import shutil
import subprocess
from pathlib import Path
import struct
import sys
import tempfile
import time
import uuid
import wave

from runtime_client import Runtime, activate


def signed_checkpoint(value):
    value = copy.deepcopy(value)
    value.pop('integrity', None)
    digest = 14695981039346656037
    encoded = json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    for byte in encoded:
        digest = ((digest ^ byte) * 1099511628211) & ((1 << 64) - 1)
    value['integrity'] = str(digest)
    return value


def rejected(work):
    try:
        work()
    except RuntimeError:
        return
    raise AssertionError('Invalid audio routing request was accepted')


def quit_cleanly(worker):
    try:
        worker.request('quit')
        assert worker.process.wait(timeout=10) == 0, worker.process.returncode
    finally:
        worker.close()


def verify_profile(runtime, gallery, module, bus_effects, impulse=False):
    runtime, gallery = Path(runtime).resolve(), Path(gallery).resolve()
    with tempfile.TemporaryDirectory(dir=runtime.parent) as folder:
        root = Path(folder)
        game = copy.deepcopy(json.loads((gallery / 'forge.project.json').read_text())['game'])
        graph = dict(version=1, buses=[dict(key='rain', label='Rain', volume=.5,
            output='effects', sends=[dict(target='music', gain=.25)]),
            dict(key='room', label='Room', volume=.75, output='rain', sends=[])])
        if bus_effects:
            graph['buses'][0]['effects'] = dict(echo_enabled=True, echo_seconds=.02,
                                               echo_feedback=.5)
            graph['buses'][1]['effects'] = dict(low_pass_enabled=True, low_pass_hz=1200,
                                               reverb_enabled=True, reverb_decay=.4)
        impulse_id = str(uuid.uuid4()) if impulse else None
        if impulse:
            graph['buses'][1].setdefault('effects', {})['impulse_response'] = dict(
                clip=impulse_id, wet=.75)
            with wave.open(str(root / 'impulse.wav'), 'wb') as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                samples = [0] * (1025 * 2)
                samples[0], samples[1] = 16384, 24576
                samples[512], samples[513] = -8192, 3072
                samples[2048], samples[2049] = 4096, -4096
                output.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
        game['audio']['routing'] = graph
        game['application_id'] = 'org.forge.audio-routing-test'
        project = dict(version=2, name='Audio routing', simulation_hz=60,
                       startup_scene=None, input=dict(version=1, actions=[]), game=game, modules=[])
        if module is not None:
            module = Path(module).resolve()
            shutil.copy2(module, root / module.name)
            fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
            project['modules'] = [dict(id='project.audio_probe', implementation='1', sdk='experimental-1',
                fingerprint=fingerprint, library=module.name, dependencies=['forge.audio', 'forge.input'])]
        manifest = root / 'forge.project.json'
        manifest.write_text(json.dumps(project))
        with wave.open(str(root / 'tone.wav'), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(48000)
            output.writeframes(b''.join(struct.pack('<h', int(2000 * math.sin(i * .06)))
                                       for i in range(48000)))
        clip, scene_id, actor = (str(uuid.uuid4()) for _ in range(3))
        if module is not None:
            scene_id, actor = '11111111-1111-4111-8111-111111111111', '22222222-2222-4222-8222-222222222222'
        records = [dict(id=clip, type='audio_clip', source='tone.wav', schema_version=1,
                        dependencies=[])]
        if impulse:
            records.append(dict(id=impulse_id, type='audio_clip', source='impulse.wav',
                                schema_version=1, dependencies=[]))
        (root / 'forge.assets.json').write_text(json.dumps(dict(version=1, assets=records)))
        scene = dict(version=3, asset_id=scene_id, entities=[dict(id=actor, name='Rain', components={
            'forge.audio_source': dict(clip=clip, loop=True, play_on_start=True, spatialized=False,
                                       gain=1, pitch=1, minimum_distance=1, maximum_distance=100,
                                       custom_bus='room')})])
        saved = root / 'Routing.scene.json'
        saved.write_text(json.dumps(scene))
        original = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in (manifest, saved)}
        checkpoint = None
        for sdk in (False, True):
            worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on' if sdk else 'off',
                                       '--audio', 'offline', '--user-data', root / 'Users'])
            try:
                initial = activate(worker, scene, sdk)
                assert initial['audio']['bus_volumes'] == dict(rain=.5, room=.75)
                assert initial['audio']['bus_processing'] == bus_effects
                assert initial['audio']['bus_effect_chains'] == (4 if bus_effects else 0)
                if bus_effects:
                    assert initial['audio']['bus_echo_buffer_bytes'] > 0
                    assert initial['audio']['bus_reverb_buffer_bytes'] > 0
                if impulse:
                    assert initial['audio']['bus_impulse_buffer_bytes'] > 0
                    assert not initial['audio']['pending_bus_effects']
                    assert not initial['audio']['bus_effect_error']
                generation = initial['activation']['generation'] if sdk else initial['world_generation']
                tick, admitted = initial['timing']['tick'], initial['scene']
                values = dict(master_volume=.25, channel_volumes=[.5, .75, 0],
                              bus_volumes=dict(rain=.125, room=.25))
                changed = worker.request('audio_mix', generation=generation, mix=values)
                assert changed['audio']['bus_volumes'] == values['bus_volumes']
                assert changed['scene'] == admitted and changed['timing']['tick'] == tick
                legacy = worker.request('audio_mix', generation=generation,
                    mix=dict(master_volume=.5, channel_volumes=[1, .5, .75]))
                assert legacy['audio']['bus_volumes'] == values['bus_volumes']
                before = legacy['audio']
                for buses in ({}, dict(rain=1, room=1, missing=1), dict(rain=1, room=-1)):
                    rejected(lambda: worker.request('audio_mix', generation=generation,
                        mix=dict(master_volume=0, channel_volumes=[0, 0, 0], bus_volumes=buses)))
                    current = worker.request('snapshot')
                    for field in ('master_volume', 'channel_volumes', 'bus_volumes', 'voices'):
                        assert current['audio'][field] == before[field]
                rejected(lambda: worker.request('property_tune', generation=generation,
                    scene_asset=scene_id, entity=actor, type='forge.audio_source', property='custom_bus', value='missing'))
                assert worker.request('snapshot')['scene'] == admitted
                routed = worker.request('property_tune', generation=generation, scene_asset=scene_id,
                    entity=actor, type='forge.audio_source', property='custom_bus', value='rain',
                    audio_source_watch=dict(scene=scene_id, entity=actor))
                assert routed['audio_source']['custom_bus'] == 'rain' and routed['audio_source']['available']
                if not sdk:
                    checkpoint = routed['recovery']
                    assert checkpoint['version'] == 3 and checkpoint['audio']['version'] == 1
                    assert signed_checkpoint(checkpoint) == checkpoint, 'Recovery checksum oracle disagrees'
                if module is not None:
                    worker.request('property_tune', generation=generation, scene_asset=scene_id,
                        entity=actor, type='forge.audio_source', property='custom_bus', value='room')
                    stepped = worker.request('step', audio_source_watch=dict(scene=scene_id, entity=actor))
                    deadline = time.monotonic() + 5
                    while stepped['timing']['tick'] != tick + 1:
                        assert time.monotonic() < deadline, stepped
                        stepped = worker.request('snapshot', audio_source_watch=dict(scene=scene_id, entity=actor))
                    source = next(row for row in stepped['scene']['entities'] if row['id'] == actor)['components']['forge.audio_source']
                    assert source['custom_bus'] == 'rain' and source['gain'] == .5 and source['bus'] == 2, source
                    assert stepped['audio_source']['custom_bus'] == 'rain' and stepped['audio_source']['available']
            finally:
                quit_cleanly(worker)

        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'off', '--audio', 'offline',
                                   '--user-data', root / 'Users'])
        try:
            recovered = activate(worker, checkpoint['scene'], False, recovery=checkpoint,
                recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            assert recovered['audio']['master_volume'] == .5
            assert recovered['audio']['bus_volumes'] == dict(rain=.125, room=.25)
            assert recovered['audio']['bus_processing'] == bus_effects
            modes = ['missing_faders', 'unknown_key', 'changed_graph', 'legacy_version']
            if bus_effects:
                modes.append('changed_effect')
            if impulse:
                modes.append('changed_impulse')
                assert recovered['audio']['bus_impulse_buffer_bytes'] > 0
            for mode in modes:
                invalid = copy.deepcopy(checkpoint)
                if mode == 'missing_faders':
                    del invalid['audio']['mix']['bus_volumes']
                elif mode == 'unknown_key':
                    invalid['audio']['mix']['bus_volumes'] = dict(rain=1, missing=1)
                elif mode == 'changed_graph':
                    invalid['audio']['routing']['buses'][0]['output'] = 'master'
                elif mode == 'changed_effect':
                    invalid['audio']['routing']['buses'][0]['effects']['echo_feedback'] = .25
                elif mode == 'changed_impulse':
                    invalid['audio']['routing']['buses'][1]['effects']['impulse_response']['wet'] = .5
                else:
                    invalid['version'] = 2
                invalid = signed_checkpoint(invalid)
                rejected(lambda: worker.request('replace', scene=checkpoint['scene'], recovery=invalid,
                    recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick']))
                current = worker.request('snapshot')
                assert current['scene'] == recovered['scene']
                for field in ('master_volume', 'channel_volumes', 'bus_volumes', 'voices'):
                    assert current['audio'][field] == recovered['audio'][field]
        finally:
            quit_cleanly(worker)
        assert all(hashlib.sha256(path.read_bytes()).hexdigest() == digest for path, digest in original.items())
    print('Both audio routing hosts, live key admission, source tuning and private recovery passed; bus effects=' + str(bus_effects))


def verify(runtime, gallery, module=None):
    for effects in (False, True):
        verify_profile(runtime, gallery, module, effects)
    verify_profile(runtime, gallery, module, True, True)


if __name__ == '__main__':
    arguments = sys.argv[1:]
    if '--impulse-only' in arguments:
        arguments.remove('--impulse-only')
        assert 2 <= len(arguments) <= 3
        verify_profile(arguments[0], arguments[1], arguments[2] if len(arguments) == 3 else None,
                       True, True)
    else:
        verify(*arguments)
