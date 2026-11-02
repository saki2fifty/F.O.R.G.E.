"""Prefab source/variant workflow through shared editor and durable history owners."""
import json


def check_prefab_workflow(read, mutate, failed, root, *, large_read_budget=True):
    # Prefab publication and durable Apply reuse the source editor/library/SceneDocument.
    created = mutate('forge_entity_create', arguments=dict(kind=0, name='Prefab original'))
    assert created['ok'], created
    entity = created['result']['results'][0]['selected']
    captured = mutate('forge_prefab_create', entity=entity, path='Assets/Agent.prefab.json')
    assert captured['ok'], captured
    asset = captured['result']['asset']
    assert any(item['asset'] == asset for item in read('forge_prefab_list')['prefabs'])
    assert mutate('forge_prefab_source_open', asset=asset)['ok']
    draft = read('forge_prefab_source_read')
    root_index = next(i for i, member in enumerate(draft['source']['members']) if member['id'] == draft['source']['root'])
    assert mutate('forge_prefab_source_patch', state=draft['state'], patch=[dict(op='replace',
                  path=f'/members/{root_index}/name', value='Published prefab')])['ok']
    failed(mutate('forge_prefab_source_publish', state=draft['state']), 'stale_document')
    assert mutate('forge_prefab_source_publish', state=read('forge_prefab_source_read')['state'])['ok']
    assert mutate('forge_prefab_source_close', state=read('forge_prefab_source_read')['state'])['ok']
    instance = mutate('forge_prefab_instantiate', arguments=dict(asset=asset))
    assert instance['ok'], instance
    instance_id = instance['result']['results'][0]['selected']
    assert mutate('forge_document_save')['ok']
    assert mutate('forge_entity_rename', arguments=dict(entity=instance_id, name='Applied name'))['ok']
    reviewed = mutate('forge_prefab_apply_review', entity=instance_id)
    assert reviewed['ok'] and reviewed['result']['overrides'] >= 1, reviewed
    committed = mutate('forge_prefab_apply_commit', review=reviewed['result']['review'], acknowledge_scene_save=True)
    assert committed['ok'], committed
    saved_prefab = json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())
    assert saved_prefab['members'][root_index]['name'] == 'Applied name'
    assert mutate('forge_history_undo')['ok']
    assert json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())['members'][root_index]['name'] == 'Published prefab'
    assert mutate('forge_history_redo')['ok']
    assert json.loads((root / 'Game/Assets/Agent.prefab.json').read_text())['members'][root_index]['name'] == 'Applied name'

    base_path = root / 'Game/Assets/Agent.prefab.json'
    base_bytes = base_path.read_bytes()
    created_variant = mutate('forge_prefab_variant_create', base=asset,
                             path='Assets/AgentVariant.prefab.json')
    assert created_variant['ok'], created_variant
    variant = created_variant['result']['asset']
    copied = mutate('forge_prefab_duplicate', asset=variant,
                    path='Assets/AgentVariantCopy.prefab.json')
    assert copied['ok'], copied
    copied_source = json.loads((root / 'Game/Assets/AgentVariantCopy.prefab.json').read_text())
    assert copied_source['asset_id'] != variant and copied_source['base'] == asset
    assert mutate('forge_prefab_source_open', asset=variant)['ok']
    state = read('forge_prefab_source_read')
    assert state['source']['version'] == 4 and state['source']['base'] == asset
    assert 'members' not in state['source']
    assert len(state['reviewed_bases']) == 1 and state['reviewed_bases'][0]['asset'] == asset
    assert set(state['reviewed_bases'][0]) == {'asset', 'revision', 'sha256'}
    page = read('forge_prefab_source_read', view='effective', pointer='/members', offset=0, limit=1)
    assert page['state'] == state['state'] and len(page['source']) == 1 and page['total'] >= 1
    assert page['source'][0]['id'] == state['source']['root']
    assert read('forge_prefab_source_read', pointer='/base')['source'] == asset
    assert read('forge_prefab_source_read', view='effective', pointer='/members', offset=9999, limit=1)['source'] == []
    read('forge_prefab_source_read', pointer='/base', offset=0, _expected_error='invalid_argument')
    assert read('forge_prefab_source_read')['source'] == state['source']
    member = state['source']['root']
    assert copied_source['root'] == member
    override = dict(id=member, name='Applied name', components={},
                    property_overrides={'forge.tint': dict(r=0.2, g=0.8)})
    assert mutate('forge_prefab_source_patch', state=state['state'],
                  patch=[dict(op='add', path='/overrides/-', value=override)])['ok']
    failed(mutate('forge_prefab_source_revert', state=state['state'], member=member),
           'stale_document')
    state = read('forge_prefab_source_read')
    reverted = mutate('forge_prefab_source_revert', state=state['state'], member=member,
                      component='forge.tint', field='r')
    assert reverted['ok'], reverted
    state = read('forge_prefab_source_read')
    assert state['source']['overrides'][0]['property_overrides']['forge.tint'] == dict(g=0.8)
    assert state['source']['overrides'][0]['name'] == 'Applied name'  # Equal-value intent survives.
    assert mutate('forge_prefab_source_publish', state=state['state'])['ok']
    assert mutate('forge_prefab_source_close', state=read('forge_prefab_source_read')['state'])['ok']
    linked = mutate('forge_prefab_instantiate', arguments=dict(asset=variant))
    assert linked['ok'], linked
    linked_entity = linked['result']['results'][0]['selected']
    assert mutate('forge_document_save')['ok']
    assert mutate('forge_entity_rename', arguments=dict(entity=linked_entity, name='Variant applied'))['ok']
    reviewed = mutate('forge_prefab_apply_review', entity=linked_entity)
    assert reviewed['ok'], reviewed
    assert mutate('forge_prefab_apply_commit', review=reviewed['result']['review'],
                  acknowledge_scene_save=True)['ok']
    variant_path = root / 'Game/Assets/AgentVariant.prefab.json'
    saved = json.loads(variant_path.read_text())
    assert saved['base'] == asset and 'members' not in saved
    assert saved['overrides'][0]['name'] == 'Variant applied' and base_path.read_bytes() == base_bytes
    assert mutate('forge_history_undo')['ok']
    assert json.loads(variant_path.read_text())['overrides'][0]['name'] == 'Applied name'
    assert mutate('forge_history_redo')['ok']
    assert json.loads(variant_path.read_text())['overrides'][0]['name'] == 'Variant applied'
    assert base_path.read_bytes() == base_bytes
    assert mutate('forge_prefab_source_open', asset=variant)['ok']
    state = read('forge_prefab_source_read')
    assert mutate('forge_prefab_source_patch', state=state['state'],
                  patch=[dict(op='replace', path='/overrides/0/name', value='Retained draft')])['ok']
    before_rebase = read('forge_prefab_source_read')
    external = json.loads(base_bytes)
    external['revision'] += 1
    external['members'][root_index]['name'] = 'New base name'
    base_path.write_text(json.dumps(external))
    failed(mutate('forge_prefab_source_publish', state=before_rebase['state']), 'publication_failed')
    assert 'Retained draft' not in variant_path.read_text()
    rebased = mutate('forge_prefab_source_rebase', state=before_rebase['state'])
    assert rebased['ok'], rebased
    state = read('forge_prefab_source_read')
    assert state['state'] != before_rebase['state'] and state['source'] == before_rebase['source']
    assert state['source']['overrides'][0]['name'] == 'Retained draft'
    failed(mutate('forge_prefab_source_publish', state=before_rebase['state']), 'stale_document')
    assert mutate('forge_prefab_source_publish', state=state['state'])['ok']
    assert json.loads(variant_path.read_text())['overrides'][0]['name'] == 'Retained draft'
    assert json.loads(base_path.read_text()) == external
    assert mutate('forge_prefab_source_close', state=read('forge_prefab_source_read')['state'])['ok']
    if large_read_budget:
        check_prefab_read_budget(read, mutate, entity, root / 'Game')


def check_prefab_read_budget(read, mutate, entity, project):
    # Large boundary fixtures can outlast the small test client's default wait
    # under instrumentation. Retain the production bridge's own deadline; this
    # only lets the client receive that real result rather than racing it.
    from functools import partial
    read = partial(read, _response_timeout=30)
    mutate = partial(mutate, _response_timeout=30)
    # Both authored files fit the existing 8 MiB source limit, while their
    # composed member exceeds it. Reads must remain usable through the existing
    # 16 MiB transport without returning every base plus duplicate effective data.
    # External fixture files avoid exceeding the unchanged 1 MiB request limit;
    # actual source admission and every read still use the shared library/tools.
    payload_bytes = 4 * 1024 * 1024 + 128 * 1024
    captured = mutate('forge_prefab_create', entity=entity, path='Assets/ReadBudget.prefab.json')
    assert captured['ok'], captured.get('error')
    base = captured['result']['asset']
    assert mutate('forge_prefab_source_open', asset=base)['ok']
    state = read('forge_prefab_source_read')
    root_member = state['source']['root']
    source = state['source']
    index = next(i for i, row in enumerate(source['members']) if row['id'] == root_member)
    assert mutate('forge_prefab_source_close', state=state['state'])['ok']
    source['revision'] += 1
    source['members'][index]['components']['unavailable.large_a'] = dict(opaque='x' * payload_bytes)
    (project/'Assets/ReadBudget.prefab.json').write_text(json.dumps(source))
    assert mutate('forge_prefab_refresh')['ok']
    created = mutate('forge_prefab_variant_create', base=base, path='Assets/ReadBudgetVariant.prefab.json')
    assert created['ok'], created.get('error')
    source_path = project/'Assets/ReadBudgetVariant.prefab.json'
    selected = json.loads(source_path.read_text())
    selected['revision'] += 1
    selected['overrides'] = [dict(id=root_member, components={
        'unavailable.large_b': dict(opaque='y' * payload_bytes)})]
    source_path.write_text(json.dumps(selected))
    assert mutate('forge_prefab_refresh')['ok']
    assert mutate('forge_prefab_source_open', asset=created['result']['asset'])['ok']
    state = read('forge_prefab_source_read')
    assert state['source']['base'] == base and len(state['reviewed_bases']) == 1
    read('forge_prefab_source_read', view='effective', _expected_error='response_too_large')
    for name, character in (('large_a', 'x'), ('large_b', 'y')):
        part = read('forge_prefab_source_read', view='effective',
                    pointer=f'/members/0/components/unavailable.{name}/opaque')
        assert len(part['source']) == payload_bytes and part['source'][0] == character
        assert part['state'] == state['state']
    assert read('forge_prefab_source_read')['state'] == state['state']
    assert mutate('forge_prefab_source_close', state=state['state'], discard=True)['ok']
