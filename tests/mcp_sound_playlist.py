"""Ordered playlist creation through the discovered scene authoring owner."""
import copy
import json


def check_sound_playlist(read, ok, mutate, root, advertised, client, arguments):
    tool = advertised['forge_audio_playlist_create']
    assert tool['annotations']['readOnlyHint'] is False
    schema = tool['inputSchema']['properties']['arguments']
    assert schema['properties']['clips']['minItems'] == 1
    assert schema['properties']['clips']['maxItems'] == 8
    assert schema['required'] == ['clips']
    clip = next(row['id'] for row in json.loads((root / 'Game/forge.assets.json').read_text())['assets']
                if row['source'] == 'Assets/FalloffTone.wav')
    value = dict(name='Three-track playlist', clips=[dict(clip=clip, name=name)
                 for name in ('Opening', 'Middle', 'Ending')], gap_seconds=.125)
    before = read('forge_scene_read')
    stale = arguments(arguments=value)
    created = ok('forge_audio_playlist_create', arguments=value)['results'][0]
    authored = read('forge_scene_read')
    rows = {row['id']: row for row in authored['entities']}
    cue = rows[created['selected']]['components']['forge.sound_cue']
    assert cue['repeat_mode'] == 1 and cue['repeat_event_limit'] == 3
    assert cue['repeat_interval_seconds'] == .125 and cue['repeat_on_start']
    assert len(created['sources']) == 3 and len(cue['nodes']) == 4
    assert [edge['target'] for edge in cue['nodes'][0]['children']] == [1, 2, 3]
    for index, actor in enumerate(created['sources']):
        source = rows[actor]['components']['forge.audio_source']
        assert source['clip'] == clip and not source['play_on_start'] and not source['loop']
        assert not source['spatialized'] and source['pause_with_game']
        assert cue['nodes'][index + 1]['source']['entity'] == actor
        assert rows[actor]['name'] == value['clips'][index]['name']
    assert ok('forge_history_undo')['changed'] and read('forge_scene_read') == before
    assert ok('forge_history_redo')['changed'] and read('forge_scene_read') == authored
    stale['sequence'] = arguments()['sequence']
    rejected = client.tool('forge_audio_playlist_create', **stale)
    assert not rejected['ok'] and read('forge_scene_read') == authored, rejected
    for clips in ([], [dict(clip=clip)] * 9, [dict(clip=clip), dict(clip='invalid-id')]):
        invalid = copy.deepcopy(value)
        invalid['clips'] = clips
        rejected = mutate('forge_audio_playlist_create', arguments=invalid)
        assert not rejected['ok'] and read('forge_scene_read') == authored, rejected
    ok('forge_document_save')
    saved = list((root / 'Game').rglob('*.scene.json'))
    assert any(json.loads(path.read_text()) == authored for path in saved), saved
