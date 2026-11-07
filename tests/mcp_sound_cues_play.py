"""Sound Cue source/history/discovery and existing scoped Play controls through MCP."""
import copy
import json


def check_sound_cues_play(read, ok, control, mutate, until, root, discovered_schema, advertised,
                         finite_only=False):
    assert advertised['forge_play_audio_control']['annotations']['idempotentHint'] is False
    assert advertised['forge_play_animation_control']['annotations']['idempotentHint'] is False
    schema = next(t for t in discovered_schema['components'] if t['id']=='forge.sound_cue')
    nodes = next(f for f in schema['fields'] if f['id']=='nodes')
    assert nodes['maximum_count']==32
    delay = next(f for f in nodes['element']['fields'] if f['id']=='delay_seconds')
    assert delay['minimum']==0 and delay['maximum']==10 and delay['default_if_missing']==0 and delay['unit']=='s',delay
    interval = next(f for f in schema['fields'] if f['id']=='repeat_interval_seconds')
    assert interval['minimum']==0 and interval['maximum']==3600 and interval['default_if_missing']==0 and interval['unit']=='s',interval
    assert next(f for f in schema['fields'] if f['id']=='repeat_on_start')['default_if_missing'] is False
    assert next(f for f in schema['fields'] if f['id']=='repeat_pause_with_game')['default_if_missing'] is True
    mode = next(f for f in schema['fields'] if f['id']=='repeat_mode')
    assert mode['default_if_missing']==0 and mode['choices']==[dict(label='Interval',value=0),dict(label='AfterCompletion',value=1)],mode
    limit = next(f for f in schema['fields'] if f['id']=='repeat_event_limit')
    assert limit['type']=='uint32' and limit['minimum']==0 and limit['maximum']==65535 and limit['default_if_missing']==0,limit
    kind = next(f for f in nodes['element']['fields'] if f['id']=='kind')
    assert kind['type']=='uint32' and kind['choices']==[
        dict(label=label,value=value) for value,label in enumerate(('Source','Layer','Weighted','Ordered'))],kind
    clip = next(row['id'] for row in json.loads((root/'Game/forge.assets.json').read_text())['assets']
                if row['source']=='Assets/FalloffTone.wav')
    actors = []
    for name in ('First source','Second source'):
        actor = ok('forge_entity_create',arguments=dict(recipe='audio.source',name=name))['results'][0]['selected']
        ok('forge_property_set_many',arguments=dict(entity=actor,component='forge.audio_source',
            values=dict(clip=clip,spatialized=False,play_on_start=False,loop=False)))
        actors.append(actor)
    cue = ok('forge_entity_create',arguments=dict(recipe='audio.sound_cue',name='Ordered event'))['results'][0]['selected']
    scene_id = read('forge_scene_read')['asset_id']
    ref = lambda actor:dict(scene=scene_id,entity=actor)
    graph = dict(enabled=True,seed=17,root=0,nodes=[
        dict(kind=3,source=None,children=[dict(target=1,weight=1),dict(target=2,weight=1)]),
        *[dict(kind=0,source=ref(actor),children=[],delay_seconds=delay)
          for actor,delay in zip(actors,(.125,.375))]])
    before = read('forge_scene_read')
    ok('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=graph))
    authored = read('forge_scene_read')
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read')==before
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read')==authored
    invalid = copy.deepcopy(graph)
    invalid['nodes'][0]['children'][0]['target']=0
    rejected = mutate('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=invalid))
    assert not rejected['ok'] and read('forge_scene_read')==authored,rejected
    invalid = copy.deepcopy(graph)
    invalid['nodes'][1]['delay_seconds']=11
    rejected = mutate('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=invalid))
    assert not rejected['ok'] and read('forge_scene_read')==authored,rejected
    if finite_only:
        check_finite_runs(read,ok,control,mutate,until,actors,cue,graph)
        return
    ok('forge_document_save')
    ok('forge_play_start')
    until(lambda:read('forge_play_status'),lambda value:value['ready'] and value['control_ready'])
    control('forge_play_pause')
    control('forge_play_watch',entity=cue)
    initial = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('available'))
    assert initial['source']['kind']=='sound_cue' and initial['source']['accepted_triggers']==0,initial
    tick = read('forge_play_status')['timing']['tick']
    for count in range(1,4):
        control('forge_play_audio_control',entity=cue,operation='one_shot')
        current = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('accepted_triggers')==count)
        assert current['source']['last_sources']==[ref(actors[(count-1)%2])],current
        assert current['source']['last_delay_seconds']==[(.125,.375)[(count-1)%2]],current
    control('forge_play_audio_control',entity=cue,operation='stop')
    rejected = until(lambda:read('forge_play_audio'),lambda row:bool(row.get('error')))
    assert rejected['source']['accepted_triggers']==3,rejected
    control('forge_play_watch',entity=actors[0])
    waiting = until(lambda:read('forge_play_audio'),
        lambda row:(row.get('source') or {}).get('delayed_one_shots')==2)
    assert waiting['source']['one_shots']==2,waiting
    control('forge_play_audio_control',entity=actors[0],operation='pause')
    until(lambda:read('forge_play_audio'),
        lambda row:(row.get('source') or {}).get('paused_one_shots')==2)
    control('forge_play_audio_control',entity=actors[0],operation='resume')
    until(lambda:read('forge_play_audio'),
        lambda row:(row.get('source') or {}).get('paused_one_shots')==0)
    control('forge_play_audio_control',entity=actors[0],operation='stop')
    stopped = until(lambda:read('forge_play_audio'),
        lambda row:(row.get('source') or {}).get('one_shots')==0)
    assert stopped['source']['delayed_one_shots']==0,stopped
    control('forge_play_watch',entity=cue)
    until(lambda:read('forge_play_audio'),
        lambda row:(row.get('source') or {}).get('accepted_triggers')==3)
    # The existing watch identity gate rejects a different entity before dispatch.
    state = read('forge_play_status')
    wrong = mutate('forge_play_audio_control',launch=state['launch'],session=state['session'],generation=state['generation'],
        entity=actors[0],operation='one_shot')
    assert not wrong['ok'],wrong
    assert read('forge_play_status')['timing']['tick']==tick
    control('forge_play_stop')
    until(lambda:read('forge_play_status'),lambda value:not value['active'])
    assert read('forge_scene_read')==authored


    # The same authoring/history and Play owners expose configured event production.
    for actor in actors:
        ok('forge_property_set_many',arguments=dict(entity=actor,component='forge.audio_source',
            values=dict(pause_with_game=False)))
    repeated = copy.deepcopy(graph)
    repeated.update(repeat_interval_seconds=.5,repeat_on_start=False,repeat_pause_with_game=False)
    before = read('forge_scene_read')
    ok('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=repeated))
    authored = read('forge_scene_read')
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read')==before
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read')==authored
    invalid = copy.deepcopy(repeated)
    invalid['repeat_interval_seconds']=.1
    rejected = mutate('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=invalid))
    assert not rejected['ok'] and read('forge_scene_read')==authored,rejected
    ok('forge_document_save')
    ok('forge_play_start')
    until(lambda:read('forge_play_status'),lambda value:value['ready'] and value['control_ready'])
    control('forge_play_pause')
    tick = read('forge_play_status')['timing']['tick']
    control('forge_play_watch',entity=cue)
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_available'))
    control('forge_play_audio_control',entity=cue,operation='play')
    started = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_running') and
                    row['source']['accepted_triggers']==1)
    assert not started['source']['repeat_paused'],started
    control('forge_play_audio_control',entity=cue,operation='pause')
    paused = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_paused'))
    assert paused['source']['repeat_running'] and paused['source']['accepted_triggers']==1,paused
    control('forge_play_audio_control',entity=cue,operation='resume')
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_running') and
          not row['source']['repeat_paused'])
    control('forge_play_audio_control',entity=cue,operation='stop')
    stopped = until(lambda:read('forge_play_audio'),lambda row:row.get('source') and not row['source'].get('repeat_running'))
    assert stopped['source']['accepted_triggers']==1,stopped
    control('forge_play_audio_control',entity=cue,operation='one_shot')
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('accepted_triggers')==2)
    assert read('forge_play_status')['timing']['tick']==tick
    control('forge_play_stop')
    until(lambda:read('forge_play_status'),lambda value:not value['active'])
    assert read('forge_scene_read')==authored

    completed = copy.deepcopy(repeated)
    completed.update(repeat_mode=1,repeat_interval_seconds=0)
    before = read('forge_scene_read')
    ok('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=completed))
    authored = read('forge_scene_read')
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read')==before
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read')==authored
    invalid = copy.deepcopy(completed)
    invalid['repeat_mode']=2
    rejected = mutate('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=invalid))
    assert not rejected['ok'] and read('forge_scene_read')==authored,rejected
    ok('forge_document_save')
    ok('forge_play_start')
    until(lambda:read('forge_play_status'),lambda value:value['ready'] and value['control_ready'])
    control('forge_play_pause')
    tick = read('forge_play_status')['timing']['tick']
    control('forge_play_watch',entity=cue)
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_mode')==1)
    control('forge_play_audio_control',entity=cue,operation='play')
    waiting = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_waiting_completion') and
                    row['source']['repeat_pending_requests']==0 and row['source']['repeat_event_voices']==1)
    assert waiting['source']['accepted_triggers']==1 and waiting['source']['repeat_completed_events']==0,waiting
    control('forge_play_audio_control',entity=cue,operation='pause')
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_paused'))
    control('forge_play_audio_control',entity=cue,operation='resume')
    until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_running') and
          not row['source']['repeat_paused'])
    control('forge_play_audio_control',entity=cue,operation='one_shot')
    extra = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('accepted_triggers')==2)
    assert extra['source']['repeat_event_voices']==1 and extra['source']['repeat_waiting_completion'],extra
    control('forge_play_audio_control',entity=cue,operation='stop')
    until(lambda:read('forge_play_audio'),lambda row:row.get('source') and not row['source'].get('repeat_running'))
    assert read('forge_play_status')['timing']['tick']==tick
    control('forge_play_stop')
    until(lambda:read('forge_play_status'),lambda value:not value['active'])
    assert read('forge_scene_read')==authored


def check_finite_runs(read, ok, control, mutate, until, actors, cue, graph):
    def hold_first_source(paused):
        # Keep a real Source-owned voice alive across arbitrarily slow RPCs.
        control('forge_play_watch',entity=actors[0])
        until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('kind')!='sound_cue' and
              (row.get('source') or {}).get('available'))
        if paused:
            control('forge_play_audio_control',entity=actors[0],operation='play')
            until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('playing'))
        control('forge_play_audio_control',entity=actors[0],operation='pause' if paused else 'resume')
        until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('source_paused') is paused)
        control('forge_play_watch',entity=cue)
        until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('kind')=='sound_cue')

    for actor in actors:
        ok('forge_property_set_many',arguments=dict(entity=actor,component='forge.audio_source',
            values=dict(pause_with_game=False,loop=actor==actors[0])))
    for mode in (0,1):
        values = copy.deepcopy(graph)
        values.update(repeat_mode=mode,repeat_interval_seconds=0 if mode else .5,
                      repeat_event_limit=2,repeat_on_start=False,repeat_pause_with_game=False)
        before = read('forge_scene_read')
        ok('forge_property_set_many',arguments=dict(entity=cue,component='forge.sound_cue',values=values))
        authored = read('forge_scene_read')
        assert ok('forge_history_undo')['changed'] and read('forge_scene_read')==before
        assert ok('forge_history_redo')['changed'] and read('forge_scene_read')==authored
        for invalid in (True,-1,1.5,'2',65536):
            rejected = mutate('forge_property_set_many',arguments=dict(entity=cue,
                component='forge.sound_cue',values=dict(repeat_event_limit=invalid)))
            assert not rejected['ok'] and read('forge_scene_read')==authored,rejected
        ok('forge_document_save')
        ok('forge_play_start')
        until(lambda:read('forge_play_status'),lambda row:row['ready'] and row['control_ready'])
        control('forge_play_pause')
        hold_first_source(True)
        until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_event_limit')==2)
        state = read('forge_play_status')
        rejected = mutate('forge_play_audio_control',launch=state['launch'],session=state['session'],
            generation=state['generation']+1,entity=cue,operation='play')
        assert not rejected['ok'],rejected
        control('forge_play_audio_control',entity=cue,operation='play')
        until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_issued_events',0)>=1)
        control('forge_play_audio_control',entity=cue,operation='pause')
        paused = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_paused'))
        assert paused['source']['repeat_running'] and not paused['source']['repeat_finished'],paused
        hold_first_source(False)
        control('forge_play_audio_control',entity=cue,operation='resume')
        done = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_finished'))
        value = done['source']
        assert value['repeat_issued_events']==2 and value['accepted_triggers']==2,value
        assert not value['repeat_running'] and not value['repeat_finishing'],value
        assert value['repeat_run_pending_requests']==0 and value['repeat_run_voices']==0,value
        if mode:
            assert value['repeat_completed_events']==2,value
        control('forge_play_audio_control',entity=cue,operation='one_shot')
        manual = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('accepted_triggers')==3)
        assert manual['source']['repeat_finished'] and manual['source']['repeat_issued_events']==2,manual
        hold_first_source(True)
        control('forge_play_audio_control',entity=cue,operation='play')
        replay = until(lambda:read('forge_play_audio'),lambda row:(row.get('source') or {}).get('repeat_issued_events',0)>=1 and
              not row['source']['repeat_finished'])
        assert replay['source']['accepted_triggers']==replay['source']['repeat_issued_events']<=2,replay
        control('forge_play_audio_control',entity=cue,operation='stop')
        stopped = until(lambda:read('forge_play_audio'),lambda row:row.get('source') and not row['source']['repeat_running'])
        assert not stopped['source']['repeat_finished'],stopped
        assert read('forge_play_status')['timing']['tick']==state['timing']['tick']
        control('forge_play_stop')
        until(lambda:read('forge_play_status'),lambda row:not row['active'])
        assert read('forge_scene_read')==authored
