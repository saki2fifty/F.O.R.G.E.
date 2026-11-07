"""Sound Cue source/history/discovery and existing scoped Play controls through MCP."""
import copy
import json


def check_sound_cues_play(read, ok, control, mutate, until, root, discovered_schema, advertised):
    assert advertised['forge_play_audio_control']['annotations']['idempotentHint'] is False
    assert advertised['forge_play_animation_control']['annotations']['idempotentHint'] is False
    schema = next(t for t in discovered_schema['components'] if t['id']=='forge.sound_cue')
    nodes = next(f for f in schema['fields'] if f['id']=='nodes')
    assert nodes['maximum_count']==32
    delay = next(f for f in nodes['element']['fields'] if f['id']=='delay_seconds')
    assert delay['minimum']==0 and delay['maximum']==10 and delay['default_if_missing']==0 and delay['unit']=='s',delay
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
