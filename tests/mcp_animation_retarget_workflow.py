"""Explicit skeleton mapping through the native source/import owners and MCP."""
import json
from pathlib import Path
import time


def check_animation_retarget_workflow(read, mutate, edit, status, content, failed,
                                      root, client, discovery):
    owner = 'animation_retarget'
    profile = read('forge_asset_animation_retarget_catalog')
    assert profile['editor'] == owner and profile['suffix'] == '.retarget.json'
    assert profile['limits']['joints'] == 256
    assert profile['limits']['sampled_joints'] == 32768
    template = read('forge_asset_document_template', editor=owner)
    assert template['source_skeleton'] is None and template['entries'] == []
    assert not mutate('forge_asset_document_create', editor=owner,
                      path='../Outside.retarget.json')['ok']
    assert not (root / 'Outside.retarget.json').exists()

    # Original FORGE-owned test input. Both native model imports run through the
    # advertised settings/publication API; no catalog or cooked-data injection.
    fixture = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Assets/OverlayColumn.gltf'
    source = json.loads(fixture.read_text(encoding='utf-8'))
    target = json.loads(fixture.read_text(encoding='utf-8'))
    base = source['animations'][0]
    base['channels'] = [c for c in base['channels'] if c['target']['path'] != 'weights']
    source['animations'] = [base]
    target['animations'] = [target['animations'][2]]
    target['nodes'][1]['translation'] = [0, 1.75, 0]
    target['nodes'][4]['translation'] = [0, .8, 0]
    target['nodes'][1]['rotation'] = [0, 0, .3826834323650898, .9238795325112867]
    for node in target['nodes']:
        node['name'] = 'Target ' + node['name']
    paths = ['Assets/RetargetSource.gltf', 'Assets/RetargetTarget.gltf']
    for path, value in zip(paths, (source, target)):
        (root / 'Game' / path).write_text(json.dumps(value), encoding='utf-8')
        result = mutate('forge_asset_settings_open', editor='model_import', path=path)
        assert result['ok'], result
        current = read('forge_asset_settings_status', editor='model_import')
        result = mutate('forge_asset_settings_publish', editor='model_import',
                         session=current['session'], state=current['state'])
        assert result['ok'], result
        deadline = time.monotonic() + 35
        while True:
            current = read('forge_asset_settings_status', editor='model_import')
            if not current['job']:
                break
            assert time.monotonic() < deadline, current
            time.sleep(.02)
        assert not current['dirty'] and not current['diagnostic'], current
        assert mutate('forge_asset_settings_close', editor='model_import',
                       session=current['session'], state=current['state'])['ok']

    def asset(kind, source_path):
        rows = read('forge_asset_search', type=kind, text=source_path,
                     include_engine=False)['assets']
        assert len(rows) == 1, rows
        return rows[0]['asset']

    source_skeleton = asset('skeleton', paths[0])
    target_skeleton = asset('skeleton', paths[1])
    source_clip = asset('animation_clip', paths[0])
    source_rig = read('forge_asset_skeleton_joints', skeleton=source_skeleton)
    target_rig = read('forge_asset_skeleton_joints', skeleton=target_skeleton)
    assert source_rig['joint_count'] == target_rig['joint_count'] == 4
    assert source_rig['joints'][0]['parent'] is None
    page = read('forge_asset_skeleton_joints', skeleton=source_skeleton,
                 revision=source_rig['revision'], offset=1, limit=1)
    assert page['joints'] == source_rig['joints'][1:2]
    assert read('forge_asset_skeleton_joints', skeleton=source_skeleton,
                 offset=source_rig['joint_count'])['joints'] == []
    read('forge_asset_skeleton_joints', _expected_error='stale_asset',
         skeleton=source_skeleton, revision='0' * 64)
    assert not client.tool('forge_asset_skeleton_joints',
                           target=discovery()['result']['target'],
                           skeleton=source_skeleton, limit=129)['ok']
    targets = {joint['name']: joint for joint in target_rig['joints']}
    pairs = [dict(source=joint['joint'], target=targets['Target ' + joint['name']]['joint'])
             for joint in source_rig['joints']]
    assert {p['source'] for p in pairs}.isdisjoint({p['target'] for p in pairs})
    source_upper = next(j for j in source_rig['joints'] if j['name'] == 'Upper joint')
    target_upper = targets['Target Upper joint']
    assert source_upper['rest']['translation'] != target_upper['rest']['translation']
    assert source_upper['rest']['rotation'] != target_upper['rest']['rotation']

    path = 'Assets/Transferred.retarget.json'
    initial = [dict(op='replace', path='/' + key, value=value) for key, value in {
        'source_skeleton': source_skeleton, 'source_clip': source_clip,
        'target_skeleton': target_skeleton, 'entries': pairs,
        'root_translation': True, 'translation_scale': 1.75}.items()]
    result = mutate('forge_asset_document_create', editor=owner, path=path, patch=initial)
    assert result['ok'], result
    first = status(owner)
    baked = first['asset']
    original = content(owner)
    before = discovery()
    request = dict(target=before['result']['target'], expected_revision=before['revision'],
                   sequence=before['next_sequence'], editor=owner,
                   document_session=first['session'], document_revision=first['revision'],
                   patch=[dict(op='replace', path='/sampling_rate', value=60)])
    accepted = client.tool('forge_asset_document_patch', **request)
    assert accepted['ok'], accepted
    assert client.tool('forge_asset_document_patch', **request) == accepted
    assert status(owner)['revision'] == first['revision'] + 1
    assert edit('forge_asset_document_undo', owner)['ok']
    assert content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok']
    settled = status(owner)
    failed(mutate('forge_asset_document_patch', editor=owner,
                  document_session=first['session'], document_revision=first['revision'],
                  patch=[dict(op='replace', path='/sampling_rate', value=30)]), 'stale_document')
    deep = 1
    for _ in range(14):
        deep = [deep]
    for extension in (deep, 'x' * profile['limits']['source_bytes'], [0] * 8000):
        before_extension = content(owner)
        before_status = status(owner)
        assert not edit('forge_asset_document_patch', owner,
                        patch=[dict(op='add', path='/extension', value=extension)])['ok']
        assert content(owner) == before_extension
        after_status = status(owner)
        assert (after_status['revision'], after_status['dirty']) == (before_status['revision'], before_status['dirty'])
    for patch in ([dict(op='replace', path='/sampling_rate', value=121)],
                  [dict(op='replace', path='/alignment', value=[0, 0, 0, 0])],
                  [dict(op='add', path='/entries/-', value=pairs[0])],
                  [dict(op='replace', path='/asset_id', value=source_clip)]):
        assert not edit('forge_asset_document_patch', owner, patch=patch)['ok']
        assert status(owner)['revision'] == settled['revision']

    def publish(expect_success=True):
        result = edit('forge_asset_document_save', owner)
        assert result['ok'], result
        deadline = time.monotonic() + 35
        while True:
            value = status(owner)
            if not value['save_requested'] and not value['publishing']:
                break
            assert time.monotonic() < deadline, value
            time.sleep(.02)
        if expect_success:
            assert not value['dirty'] and not value['unpublished'] and not value['diagnostic'], value
        else:
            assert value['unpublished'] and value['diagnostic'], value
        return value

    publish()
    published = read('forge_asset_read', asset=baked)
    assert published['type'] == 'animation_clip'
    metadata = published['metadata']
    assert metadata['forge.import']['importer'] == 'forge.animation.retarget'
    assert metadata['forge.animation_clip']['skeleton'] == target_skeleton
    assert metadata['forge.animation_clip']['skeleton_revision'] == target_rig['revision']
    assert set(metadata['forge.animation_clip']['files']) == {'clip.json', 'clip.ozz'}
    # A structurally valid foreign target UUID reaches shared cook admission and
    # fails without replacing the last working clip or discarding source history.
    assert edit('forge_asset_document_patch', owner, patch=[dict(op='replace',
                 path='/entries/1/target', value=source_upper['joint'])])['ok']
    publish(False)
    assert read('forge_asset_read', asset=baked)['metadata'] == metadata
    assert edit('forge_asset_document_undo', owner)['ok']
    publish()
    # Existing scene operations can assign the published identity; Scene history
    # remains separate from source history and publication.
    created = mutate('forge_entity_create', arguments=dict(kind=0, name='Retarget Animator'))
    assert created['ok'], created
    actor = created['result']['results'][0]['selected']
    assert mutate('forge_component_add', arguments=dict(entity=actor, component='forge.animator'))['ok']
    for field, value in [('skeleton', target_skeleton), ('clip', baked)]:
        result = mutate('forge_property_set', arguments=dict(entity=actor,
                         component='forge.animator', field=field, value=value))
        assert result['ok'], result
    assert mutate('forge_history_undo')['ok']
    assert mutate('forge_history_redo')['ok']
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner)['entries'] == pairs
    reopened = status(owner)
    assert reopened['session'] != first['session']
    failed(mutate('forge_asset_document_patch', editor=owner,
                  document_session=first['session'], document_revision=1,
                  patch=[dict(op='replace', path='/sampling_rate', value=15)]), 'wrong_document')
    assert edit('forge_asset_document_close', owner)['ok']
