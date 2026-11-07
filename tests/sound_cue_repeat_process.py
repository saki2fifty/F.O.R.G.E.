"""Repeating-event host checks using the existing cue process project and transport."""
import copy
import time
from runtime_client import Runtime, activate


def check_repeating_hosts(runtime, root, scene, ref, cue_id, module, mode=0):
    document = copy.deepcopy(scene)
    graph = next(row['components']['forge.sound_cue'] for row in document['entities']
                 if 'forge.sound_cue' in row['components'])
    graph.update(repeat_interval_seconds=0 if mode else .25, repeat_on_start=False,
                 repeat_pause_with_game=False, repeat_mode=mode)
    for row in document['entities']:
        if 'forge.audio_source' in row['components']:
            row['components']['forge.audio_source']['pause_with_game'] = False
        if 'forge.animation_sequence' in row['components']:
            row['components']['forge.animation_sequence']['play_on_start'] = False
    checkpoint = None
    for sdk in (False, True):
        worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'on' if sdk else 'off',
                                  '--audio', 'null-test', '--user-data', root/'Users'])
        try:
            initial = activate(worker, document, sdk)
            generation = initial['activation']['generation'] if sdk else initial['world_generation']
            def snapshot(target=cue_id):
                return worker.request('snapshot', audio_source_watch=ref(target))
            def control(operation):
                return worker.request('audio_source', generation=generation, source=ref(cue_id),
                                      operation=operation, audio_source_watch=ref(cue_id))
            def until(predicate):
                deadline = time.monotonic()+6
                while True:
                    value = snapshot()
                    if predicate(value):
                        return value
                    assert time.monotonic()<deadline,{key:value.get(key) for key in ('audio_source','audio','timing')}
                    time.sleep(.025)
            if module:
                start_tick = initial['timing']['tick']
                worker.request('step')
                started = until(lambda value:value['timing']['tick']==start_tick+1 and
                                value['audio_source'].get('repeat_running'))
                native_graph = next(row['components']['forge.sound_cue'] for row in started['scene']['entities']
                                    if row['id']==cue_id)
                assert native_graph['seed']==29 and native_graph['repeat_interval_seconds']==(0 if mode else .25) and native_graph['repeat_mode']==mode,native_graph
            else:
                control('play')
                started = until(lambda value:value['audio_source'].get('accepted_triggers',0)>=1)
            tick = started['timing']['tick']
            base = started['audio_source']['accepted_triggers']
            advanced = until(lambda value:value['audio_source']['accepted_triggers']>=base+(1 if mode else 2))
            if mode:
                assert advanced['audio_source']['repeat_completed_events']>=1,advanced['audio_source']
            assert advanced['timing']['tick']==tick,advanced
            paused = control('pause')
            count = paused['audio_source']['accepted_triggers']
            time.sleep(.35)
            assert snapshot()['audio_source']['accepted_triggers']==count
            control('resume')
            resumed = until(lambda value:value['audio_source']['accepted_triggers']>count)
            assert resumed['timing']['tick']==tick,resumed
            stopped = control('stop')
            count = stopped['audio_source']['accepted_triggers']
            assert not stopped['audio_source']['repeat_running'],stopped
            assert snapshot(stopped['audio_source']['last_sources'][0]['entity'])['audio_source']['one_shots']>0
            time.sleep(.3)
            assert snapshot()['audio_source']['accepted_triggers']==count
            if not sdk:
                checkpoint = copy.deepcopy(stopped['recovery'])
            worker.request('quit')
            assert worker.process.wait(timeout=5)==0
        finally:
            worker.close()
    assert checkpoint is not None
    worker = Runtime(runtime, ['--sdk-project', root, '--sdk-play', 'off', '--audio', 'null-test',
                              '--user-data', root/'Users'])
    try:
        recovered = activate(worker, checkpoint['scene'], False, recovery=checkpoint,
            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
        status = worker.request('snapshot', audio_source_watch=ref(cue_id))['audio_source']
        assert status['accepted_triggers']==0 and not status['repeat_running'],status
        assert recovered['scene']==checkpoint['scene'],recovered
        worker.request('quit')
        assert worker.process.wait(timeout=5)==0
    finally:
        worker.close()
