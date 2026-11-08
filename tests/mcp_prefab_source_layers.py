"""Ordered source contributions through shared native draft/publication owners."""
import json
import uuid


def check_prefab_source_layers(read, mutate, failed, root):
    def ok(tool, **arguments):
        result = mutate(tool, **arguments)
        assert result['ok'], (tool, result)
        return result['result']

    def draft():
        return read('forge_prefab_source_read')

    def close():
        ok('forge_prefab_source_close', state=draft()['state'])

    def publish():
        ok('forge_prefab_source_publish', state=draft()['state'])
        close()

    def set_inputs(assets):
        return ok('forge_prefab_source_mixins_set', state=draft()['state'], assets=assets)

    def effective():
        source = read('forge_prefab_source_read', view='effective')['source']
        return next(row for row in source['members'] if row['id'] == source['root'])

    original = ok('forge_entity_create', arguments=dict(kind=0, name='Source layer root'))['results'][0]['selected']
    base = ok('forge_prefab_create', entity=original, path='Assets/LayerCommon.prefab.json')['asset']
    inputs = []
    for name, values in [('Shape', dict(r=.25)), ('Green', dict(g=.85)), ('Dark', dict(g=.65))]:
        asset = ok('forge_prefab_variant_create', base=base, path=f'Assets/Layer{name}.prefab.json')['asset']
        inputs.append(asset)
        ok('forge_prefab_source_open', asset=asset)
        current = draft()
        ok('forge_prefab_source_patch', state=current['state'], patch=[dict(
            op='add', path='/overrides/-', value=dict(id=current['source']['root'], name=name,
            components={}, property_overrides={'forge.tint': values}))])
        publish()
    a, b, c = inputs
    base_path = root / 'Game/Assets/LayerCommon.prefab.json'
    supporting = {path: path.read_bytes() for path in [base_path] + [
        root / f'Game/Assets/Layer{name}.prefab.json' for name in ('Shape', 'Green', 'Dark')]}
    common = json.loads(supporting[base_path])
    common_tint = next(row for row in common['members'] if row['id'] == common['root'])['components']['forge.tint']
    composite = ok('forge_prefab_variant_create', base=a, path='Assets/LayerCombined.prefab.json')['asset']
    ok('forge_prefab_source_open', asset=composite)
    initial = draft()
    set_inputs([b, c])
    failed(mutate('forge_prefab_source_mixins_set', state=initial['state'], assets=[]), 'stale_document')
    assert draft()['source']['version'] == 8 and draft()['source']['mixins'] == [b, c]
    expected_tint = dict(common_tint, r=.25, g=.65)
    assert effective()['components']['forge.tint'] == expected_tint, effective()
    assert {item['asset'] for item in draft()['reviewed_bases']} == {base, a, b, c}
    current = draft()
    for invalid in ([a], [b, b], [composite], [str(uuid.uuid4())], [b] * 16):
        assert not mutate('forge_prefab_source_mixins_set', state=current['state'], assets=invalid)['ok']
        assert draft()['state'] == current['state']
    set_inputs([c, b])
    assert effective()['name'] == 'Green' and effective()['components']['forge.tint']['g'] == .85
    current = draft()
    member = current['source']['root']
    ok('forge_prefab_source_patch', state=current['state'], patch=[dict(op='add', path='/overrides/-',
        value=dict(id=member, components={}, property_overrides={'forge.tint': dict(g=.4)}))])
    assert effective()['components']['forge.tint']['g'] == .4
    ok('forge_prefab_source_revert', state=draft()['state'], member=member, component='forge.tint', field='g')
    assert effective()['components']['forge.tint']['g'] == .85
    set_inputs([])
    assert draft()['source']['version'] == 8 and 'mixins' not in draft()['source']
    assert effective()['components']['forge.tint']['g'] == common_tint['g']
    set_inputs([b])
    publish()
    path = root / 'Game/Assets/LayerCombined.prefab.json'
    saved = json.loads(path.read_text())
    assert saved['version'] == 8 and saved['mixins'] == [b] and 'members' not in saved
    assert all(path.read_bytes() == data for path, data in supporting.items())
    copy = ok('forge_prefab_duplicate', asset=composite, path='Assets/LayerCopy.prefab.json')['asset']
    copied = json.loads((root / 'Game/Assets/LayerCopy.prefab.json').read_text())
    assert copied['asset_id'] == copy and copy != composite and copied['mixins'] == [b]
    assert copied['root'] == saved['root'] and copied['base'] == a
    instance = ok('forge_prefab_instantiate', arguments=dict(asset=composite))['results'][0]['selected']
    ok('forge_document_save')
    ok('forge_entity_rename', arguments=dict(entity=instance, name='Layer instance override'))
    reviewed = ok('forge_prefab_apply_review', entity=instance)
    ok('forge_prefab_apply_commit', review=reviewed['review'], acknowledge_scene_save=True)
    applied = json.loads(path.read_text())
    assert applied['version'] == 8 and applied['mixins'] == [b]
    assert applied['overrides'][0]['name'] == 'Layer instance override'
    ok('forge_history_undo')
    restored = json.loads(path.read_text())
    assert {k: v for k, v in restored.items() if k != 'revision'} == {
        k: v for k, v in saved.items() if k != 'revision'}, (restored, saved)
    assert restored['revision'] > applied['revision']
    ok('forge_history_redo')
    replayed = json.loads(path.read_text())
    assert {k: v for k, v in replayed.items() if k != 'revision'} == {
        k: v for k, v in applied.items() if k != 'revision'}, (replayed, applied)
    assert replayed['revision'] > restored['revision']
    assert all(path.read_bytes() == data for path, data in supporting.items())
    # Reviewed contributors are part of exact-state publication, even when the
    # composite's own raw source and selected primary base did not change.
    ok('forge_prefab_source_open', asset=composite)
    current = draft()
    b_path = root / 'Game/Assets/LayerGreen.prefab.json'
    changed = json.loads(b_path.read_text())
    changed['revision'] += 1
    changed['overrides'][0]['property_overrides']['forge.tint']['g'] = .75
    b_path.write_text(json.dumps(changed))
    assert not mutate('forge_prefab_source_mixins_set', state=current['state'], assets=[b])['ok']
    assert draft()['state'] == current['state']
    ok('forge_prefab_source_rebase', state=current['state'])
    assert effective()['components']['forge.tint']['g'] == .75
    current = draft()
    # An unrelated complete source cannot enter this common-root namespace.
    close()
    unrelated = ok('forge_prefab_duplicate', asset=base, path='Assets/LayerUnrelated.prefab.json')['asset']
    ok('forge_prefab_source_open', asset=composite)
    current = draft()
    assert not mutate('forge_prefab_source_mixins_set', state=current['state'], assets=[unrelated])['ok']
    assert draft()['state'] == current['state']
    close()
    persisted = read('forge_scene_read')
    ok('forge_document_save')
    ok('forge_document_open', path=read('forge_project_read')['scene_path'])
    assert read('forge_scene_read') == persisted
