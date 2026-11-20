"""GPU ribbon authoring through the live source owner and either MCP transport."""
import json


def check_gpu_trails(read, mutate, edit, status, content, failed, root, tools,
                     catalog, save, path):
    owner = 'particle_effect'
    tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_trails')
    assert tool['inputSchema']['properties']['backend']['enum'] == ['cpu', 'gpu']
    assert tool['inputSchema']['properties']['backend']['default'] == 'cpu'
    assert 'gpu_trail1' in catalog['trails'] and 'runtime_particle_gpu_trail1' in catalog['trails']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    original = content(owner)
    before = status(owner)
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=False)['ok']
    assert content(owner) == original and status(owner)['revision'] == before['revision']
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=True)['ok']
    active = content(owner)
    assert active == dict(original, version=14, profile='gpu_trail1',
                          gpu_trail_enabled=True, gpu_collision_enabled=False,
                          collision_enabled=False, collision=dict(normal=[0, 1, 0], offset=-1,
                              radius=0, restitution=.5, friction=.1, bounce_limit=4),
                          gpu_weighted_enabled=False, gpu_opacity_enabled=False)
    failed(mutate('forge_asset_document_particle_trails', editor=owner, backend='gpu', enabled=False,
                  document_session=before['session'], document_revision=before['revision']),
           'stale_document')
    failed(mutate('forge_asset_document_particle_trails', editor=owner, backend='gpu', enabled=False,
                  document_session=before['session'] + 1000,
                  document_revision=status(owner)['revision']), 'wrong_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=True)['ok']
    assert status(owner)['revision'] == revision
    for backend in ('automatic', '', None, 1, True):
        assert not edit('forge_asset_document_particle_trails', owner, backend=backend,
                        enabled=True)['ok']
        assert content(owner) == active and status(owner)['revision'] == revision
    for settings in (dict(duration=0, segments=8), dict(duration=31, segments=8),
                     dict(duration=.5, segments=17), dict(duration=True, segments=8),
                     dict(duration=.5, segments=1.5), dict(duration=.5),
                     dict(duration=.5, segments=8, future=True),
                     dict(duration=30, segments=16)):
        assert not edit('forge_asset_document_particle_trails', owner, backend='gpu',
                        enabled=True, trail=settings)['ok']
        assert content(owner) == active and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == active
    for evaluation, profile in (('cpu', 'cpu_mesh2'), ('cpu_alpha', 'cpu_trail1'),
                                ('gpu', 'gpu_trail1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(active, profile=profile)
    assert edit('forge_asset_document_particle_trails', owner, enabled=False)['ok']
    assert content(owner) == dict(active, trail_enabled=False)
    assert edit('forge_asset_document_particle_trails', owner, enabled=True)['ok']
    assert content(owner) == dict(active, profile='cpu_trail1')
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner) == active
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 23
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == active
    assert mutate('forge_asset_document_create', editor='material',
                  path='Assets/GpuRibbon.material.json', patch=[
        dict(op='replace', path='/overrides', value=dict(model='forge.gltf.unlit.v1',
             alpha=2, depth_test=True, depth_write=False, double_sided=True))])['ok']
    material = content('material')['asset_id']
    save('material')
    assert edit('forge_asset_document_close', 'material')['ok']
    assert edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/material', value=material)])['ok']
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    weighted = dict(active, material=material, profile='gpu_weighted_trail1',
                    gpu_weighted_enabled=True)
    assert content(owner) == weighted
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['value_version'] == 24
    assert edit('forge_asset_document_particle_opacity', owner, backend='gpu', enabled=True)['ok']
    fading = dict(weighted, profile='gpu_weighted_trail_opacity1', gpu_opacity_enabled=True)
    assert content(owner) == fading
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=True)['ok']
    assert content(owner) == dict(fading, profile='gpu_weighted_opacity_billboard1',
                                  gpu_trail_enabled=False, gpu_billboard_enabled=True)
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=True)['ok']
    assert content(owner) == fading
    assert edit('forge_asset_document_particle_collision', owner, backend='gpu', enabled=True)['ok']
    assert content(owner) == dict(fading, profile='gpu_weighted_collision_opacity1',
                                  gpu_trail_enabled=False, gpu_collision_enabled=True)
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=True)['ok']
    assert content(owner) == fading
    settings = dict(duration=.25, segments=3)
    assert edit('forge_asset_document_particle_trails', owner, backend='gpu', enabled=True,
                trail=settings)['ok']
    changed = dict(fading, trail=settings)
    assert content(owner) == changed
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == fading
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == changed
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 25
    assert json.loads((root / 'Game' / path).read_text()) == changed
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == changed
    assert edit('forge_asset_document_close', owner)['ok']
