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
