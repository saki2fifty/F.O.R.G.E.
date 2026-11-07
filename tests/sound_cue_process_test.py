"""Bounded cue dispatch through both actual runtime hosts and matching installed C++."""
import copy
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
import wave
from pathlib import Path
from runtime_client import Runtime, activate


def rejected(work):
    try:
        work()
    except RuntimeError:
        return
    raise AssertionError('Invalid cue control was admitted')


def verify(runtime, gallery, module=None):
    runtime, gallery = Path(runtime).resolve(), Path(gallery).resolve()
    with tempfile.TemporaryDirectory(dir=runtime.parent) as folder:
        root = Path(folder)
        game = copy.deepcopy(json.loads((gallery/'forge.project.json').read_text())['game'])
        game['application_id'] = 'org.forge.sound-cue-test'
        game['audio']['routing'] = dict(version=1, buses=[])
        modules = []
        if module:
            module = Path(module).resolve()
            shutil.copy2(module, root/module.name)
            fingerprint = json.loads(subprocess.check_output([runtime,'--sdk-info'],text=True))['fingerprint']
            modules = [dict(id='project.audio_probe',implementation='1',sdk='experimental-1',
                fingerprint=fingerprint,library=module.name,dependencies=['forge.audio','forge.input'])]
        (root/'forge.project.json').write_text(json.dumps(dict(version=2,name='Sound Cues',simulation_hz=60,
            startup_scene=None,input=dict(version=1,actions=[]),game=game,modules=modules)))
        with wave.open(str(root/'tone.wav'),'wb') as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(48000)
            output.writeframes(b''.join(struct.pack('<h',2000 if i%80<40 else -2000) for i in range(48000)))
        clip = str(uuid.uuid4())
        (root/'forge.assets.json').write_text(json.dumps(dict(version=1,assets=[dict(
            id=clip,type='audio_clip',source='tone.wav',schema_version=1,dependencies=[])])))
        scene_id = '11111111-1111-4111-8111-111111111111'
        a = '22222222-2222-4222-8222-222222222222'
        b = '33333333-3333-4333-8333-333333333333'
        cue_id = '44444444-4444-4444-8444-444444444444'
        director = '55555555-5555-4555-8555-555555555555'
        event = '66666666-6666-4666-8666-666666666666'
        ref = lambda value: dict(scene=scene_id,entity=value)
        values = dict(clip=clip,play_on_start=False,loop=False,spatialized=False,gain=1,pitch=1,
                      minimum_distance=1,maximum_distance=100)
        graph = dict(enabled=True,seed=1,root=0,nodes=[
            dict(kind=3,source=None,children=[dict(target=1,weight=1),dict(target=2,weight=1)]),
            dict(kind=0,source=ref(a),children=[],delay_seconds=.125),
            dict(kind=0,source=ref(b),children=[],delay_seconds=.375)])
        scene = dict(version=3,asset_id=scene_id,entities=[
            dict(id=a,name='First',components={'forge.audio_source':values}),
            dict(id=b,name='Second',components={'forge.audio_source':values}),
            dict(id=cue_id,name='Event graph',components={'forge.sound_cue':graph}),
            dict(id=director,name='Timeline',components={'forge.animation_sequence':dict(duration=1,enabled=True,
                play_on_start=True,loop=False,playback_speed=1,sequence=None,sequence_start_seconds=0,
                sequence_rate=1,sequence_loop=False,sequence_extra_placements=[])}),
            dict(id=event,name='Timed event',components={'forge.audio_cue':dict(sequence=ref(director),source=ref(cue_id),time_seconds=.1,enabled=True)})])
        original = {p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
        checkpoint = None
        for sdk in (False,True):
            worker = Runtime(runtime,['--sdk-project',root,'--sdk-play','on' if sdk else 'off',
                                      '--audio','offline','--user-data',root/'Users'])
            try:
                initial = activate(worker,scene,sdk)
                generation = initial['activation']['generation'] if sdk else initial['world_generation']
                admitted = initial['scene']
                def watch():
                    return worker.request('snapshot',audio_source_watch=ref(cue_id))['audio_source']
                def trigger(operation='one_shot', target=cue_id):
                    return worker.request('audio_source',generation=generation,source=ref(target),
                                          operation=operation,audio_source_watch=ref(cue_id))
                state = watch()
                assert state['kind']=='sound_cue' and state['accepted_triggers']==0 and state['maximum_outputs']==1,state
                first = trigger()
                assert first['audio_source']['accepted_triggers']==1 and first['audio_source']['last_sources']==[ref(a)],first
                assert first['audio_source']['last_delay_seconds']==[.125],first
                assert first['scene']==admitted and first['timing']['tick']==initial['timing']['tick']
                second = trigger()
                assert second['audio_source']['last_sources']==[ref(b)] and second['audio_source']['accepted_triggers']==2
                for operation in ('play','pause','resume','stop','unknown'):
                    rejected(lambda:trigger(operation))
                rejected(lambda:trigger(target=str(uuid.uuid4())))
                assert watch()['accepted_triggers']==2
                # Advance through the actual Timeline event. Both hosts use the same AudioService.
                start = second['timing']['tick']
                for tick in range(7):
                    stepped = worker.request('step',audio_source_watch=ref(cue_id))
                    deadline = time.monotonic()+5
                    while stepped['timing']['tick'] != start+tick+1:
                        assert time.monotonic()<deadline,stepped
                        time.sleep(.002)
                        stepped = worker.request('snapshot',audio_source_watch=ref(cue_id))
                current = stepped['audio_source']
                if module:
                    config = next(row for row in stepped['scene']['entities'] if row['id']==cue_id)['components']['forge.sound_cue']
                    assert config['seed']==29 and config['nodes'][0]['children']==list(reversed(graph['nodes'][0]['children'])),config
                    assert all(node['delay_seconds']==.075 for node in config['nodes'] if node['kind']==0),config
                    assert current['accepted_triggers'] >= 8,current
                    assert not any('cue trigger rejected' in str(d) for d in stepped.get('diagnostics',[]))
                else:
                    assert current['accepted_triggers']==3 and current['last_sources']==[ref(a)],current
                if not sdk:
                    checkpoint = copy.deepcopy(stepped['recovery'])
                worker.request('quit')
                assert worker.process.wait(timeout=5)==0
            finally:
                worker.close()
        assert checkpoint is not None
        worker = Runtime(runtime,['--sdk-project',root,'--sdk-play','off','--audio','offline',
                                  '--user-data',root/'Users'])
        try:
            recovered = activate(worker,checkpoint['scene'],False,recovery=checkpoint,
                recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
            state = worker.request('snapshot',audio_source_watch=ref(cue_id))['audio_source']
            assert state['accepted_triggers']==0 and state['last_sources']==[],state
            saved_graph = next(row for row in checkpoint['scene']['entities'] if row['id']==cue_id)['components']['forge.sound_cue']
            assert next(row for row in recovered['scene']['entities'] if row['id']==cue_id)['components']['forge.sound_cue']==saved_graph
            first_target = saved_graph['nodes'][saved_graph['nodes'][0]['children'][0]['target']]['source']
            triggered = worker.request('audio_source',generation=recovered['world_generation'],source=ref(cue_id),
                operation='one_shot',audio_source_watch=ref(cue_id))['audio_source']
            assert triggered['accepted_triggers']==1 and triggered['last_sources']==[first_target],triggered
            first_delay = saved_graph['nodes'][saved_graph['nodes'][0]['children'][0]['target']]['delay_seconds']
            assert triggered['last_delay_seconds']==[first_delay],triggered
            worker.request('quit')
            assert worker.process.wait(timeout=5)==0
        finally:
            worker.close()
        from sound_cue_repeat_process import check_repeating_hosts
        check_repeating_hosts(runtime, root, scene, ref, cue_id, module)
        check_repeating_hosts(runtime, root, scene, ref, cue_id, module, mode=1)
        check_repeating_hosts(runtime, root, scene, ref, cue_id, module, limit=2)
        check_repeating_hosts(runtime, root, scene, ref, cue_id, module, mode=1, limit=2)
        assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in original.items())
    print('Both cue hosts, Timeline, repeating/finite output-time events, controls, fresh recovery and fifteen clean exits passed')


if __name__ == '__main__':
    verify(*sys.argv[1:])
