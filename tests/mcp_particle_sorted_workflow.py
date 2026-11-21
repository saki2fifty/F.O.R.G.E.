"""Ordered GPU source editing through the existing document and MCP owners."""
import json


def check_gpu_sorted(read, mutate, edit, status, content, failed, root, tools,
                     catalog, save, path):
    owner = 'particle_effect'
    tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_gpu_sorted')
    assert tool['inputSchema']['properties']['editor']['const'] == owner
    assert tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert not tool['annotations']['readOnlyHint']
    assert 'runtime_particle_gpu_sorted1' in catalog['gpu_sorted']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    original = content(owner)
    before = status(owner)
    assert edit('forge_asset_document_particle_gpu_sorted', owner, enabled=False)['ok']
    assert content(owner) == original and status(owner)['revision'] == before['revision']
    assert edit('forge_asset_document_particle_gpu_sorted', owner, enabled=True)['ok']
    ordered = dict(original, version=15, profile='gpu_sorted_trail_opacity1',
                   gpu_weighted_enabled=False, gpu_sorted_enabled=True)
    assert content(owner) == ordered
    failed(mutate('forge_asset_document_particle_gpu_sorted', editor=owner, enabled=False,
                  document_session=before['session'], document_revision=before['revision']),
           'stale_document')
    current = status(owner)
    failed(mutate('forge_asset_document_particle_gpu_sorted', editor=owner, enabled=False,
                  document_session=current['session'] + 1000, document_revision=current['revision']),
           'wrong_document')
    for invalid in (None, 0, 'true'):
        assert not edit('forge_asset_document_particle_gpu_sorted', owner, enabled=invalid)['ok']
        assert content(owner) == ordered and status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_particle_gpu_sorted', owner, enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == ordered
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    assert content(owner) == dict(ordered, profile='gpu_weighted_trail_opacity1',
                                  gpu_weighted_enabled=True, gpu_sorted_enabled=False)
    assert edit('forge_asset_document_particle_gpu_sorted', owner, enabled=True)['ok']
    assert content(owner) == ordered
    for operation, enabled, profile, value in (
            ('particle_billboards', True, 'gpu_sorted_opacity_billboard1', 34),
            ('particle_collision', True, 'gpu_sorted_collision_opacity1', 36),
            ('particle_trails', True, 'gpu_sorted_trail_opacity1', 37),
            ('particle_trails', False, 'gpu_sorted_opacity1', 32),
            ('particle_opacity', False, 'gpu_sorted1', 26),
            ('particle_billboards', True, 'gpu_sorted_billboard1', 28),
            ('particle_collision', True, 'gpu_sorted_collision1', 30),
            ('particle_trails', True, 'gpu_sorted_trail1', 31),
            ('particle_opacity', True, 'gpu_sorted_trail_opacity1', 37)):
        assert edit('forge_asset_document_' + operation, owner, backend='gpu', enabled=enabled)['ok']
        assert content(owner)['profile'] == profile
        assert content(owner)['version'] == 15
        settled = save()
        metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
        assert metadata['version'] == 3 and metadata['value_version'] == value
    assert content(owner) == ordered
    assert json.loads((root / 'Game' / path).read_text()) == ordered
    assert edit('forge_asset_document_close', owner)['ok']

    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == ordered
    assert edit('forge_asset_document_close', owner)['ok']


def check_gpu_material_palette(read, mutate, edit, status, content, failed, root, tools,
                               catalog, save, path):
    owner = 'particle_effect'
    name = 'forge_asset_document_particle_gpu_material_palette'
    tool = next(t for t in tools if t['name'] == name)
    assert tool['inputSchema']['properties']['editor']['const'] == owner
    assert tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert not tool['annotations']['readOnlyHint']
    assert 'runtime_particle_gpu_material_palette1' in catalog['gpu_material_palette']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    original = content(owner)
    before = status(owner)
    assert edit(name, owner, enabled=False)['ok']
    assert content(owner) == original and status(owner)['revision'] == before['revision']
    assert edit(name, owner, enabled=True)['ok']
    palette = content(owner)
    assert palette['version'] == 17 and palette['profile'] == 'gpu_palette_sorted_trail_opacity1'
    assert palette['gpu_material_palette_enabled'] and palette['gpu_sorted_enabled']
    assert not palette['gpu_weighted_enabled'] and not palette['scene_collision_enabled']
    for key, value in original.items():
        if key not in ('version', 'profile'):
            assert palette[key] == value
    failed(mutate(name, editor=owner, enabled=False, document_session=before['session'],
                  document_revision=before['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(name, editor=owner, enabled=False, document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for invalid in (None, 0, 'true'):
        assert not edit(name, owner, enabled=invalid)['ok']
        assert content(owner) == palette and status(owner)['revision'] == current['revision']
    assert edit(name, owner, enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == palette
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    assert content(owner)['profile'] == 'gpu_weighted_trail_opacity1'
    assert content(owner)['gpu_material_palette_enabled']
    assert edit(name, owner, enabled=True)['ok'] and content(owner) == palette
    for operation, enabled, profile, value_version in (
            ('particle_billboards', True, 'gpu_palette_sorted_opacity_billboard1', 48),
            ('particle_collision', True, 'gpu_palette_sorted_collision_opacity1', 50),
            ('particle_trails', True, 'gpu_palette_sorted_trail_opacity1', 51),
            ('particle_trails', False, 'gpu_palette_sorted_opacity1', 46),
            ('particle_opacity', False, 'gpu_palette_sorted1', 40),
            ('particle_billboards', True, 'gpu_palette_sorted_billboard1', 42),
            ('particle_collision', True, 'gpu_palette_sorted_collision1', 44),
            ('particle_trails', True, 'gpu_palette_sorted_trail1', 45),
            ('particle_opacity', True, 'gpu_palette_sorted_trail_opacity1', 51)):
        assert edit('forge_asset_document_' + operation, owner, backend='gpu', enabled=enabled)['ok']
        assert content(owner)['version'] == 17 and content(owner)['profile'] == profile
        settled = save()
        metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
        assert metadata['version'] == 3 and metadata['value_version'] == value_version
    assert content(owner) == palette
    assert json.loads((root / 'Game' / path).read_text()) == palette
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == palette
    assert edit(name, owner, enabled=False)['ok']
    assert content(owner)['profile'] == original['profile']
    assert edit(name, owner, enabled=True)['ok'] and content(owner) == palette
    assert edit('forge_asset_document_close', owner, discard=True)['ok']
