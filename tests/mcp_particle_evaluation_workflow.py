"""Particle evaluation through the live source owner and both MCP transports."""
import json
import time
from uuid import uuid4
from mcp_particle_graph_workflow import check_particle_graph


def check_particle_evaluation(read, mutate, edit, status, content, failed, root, tools):
    owner = 'particle_effect'
    catalog = read('forge_asset_particles_catalog')
    assert catalog['profiles'] == dict(cpu='cpu_mesh2', gpu='gpu_mesh1', graph='gpu_graph1',
                                       cpu_alpha='cpu_alpha1')
    tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_evaluation')
    assert tool['inputSchema']['properties']['editor']['const'] == owner
    assert tool['inputSchema']['properties']['evaluation']['enum'] == ['cpu', 'gpu', 'graph', 'cpu_alpha']
    opacity_tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_opacity')
    assert opacity_tool['inputSchema']['properties']['editor']['const'] == owner
    assert opacity_tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert 'cpu_alpha2' in catalog['opacity'] and 'runtime_particle_opacity1' in catalog['opacity']
    billboard_tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_billboards')
    assert billboard_tool['inputSchema']['properties']['editor']['const'] == owner
    assert billboard_tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert billboard_tool['inputSchema']['properties']['backend']['enum'] == ['cpu', 'gpu']
    assert billboard_tool['inputSchema']['properties']['backend']['default'] == 'cpu'
    assert 'cpu_billboard1' in catalog['billboards'] and 'runtime_particle_billboard1' in catalog['billboards']
    def save(editor=owner):
        assert edit('forge_asset_document_save', editor)['ok']
        deadline = time.monotonic() + 35
        while True:
            settled = status(editor)
            if not settled['save_requested'] and not settled['publishing']:
                break
            assert time.monotonic() < deadline, settled
            time.sleep(.02)
        assert not settled['dirty'] and not settled['unpublished'] and not settled['diagnostic'], settled
        return settled

    path = 'Assets/DeviceEffect.particles.json'
    stops = [dict(id=str(uuid4()), time=t, value=value, interpolation='linear')
             for t, value in ((0, .2), (1, .5))]
    created = mutate('forge_asset_document_create', editor=owner, path=path, patch=[
        dict(op='add', path='/size_curve', value=stops),
        dict(op='add', path='/extensions', value=dict(preserve='opaque extension'))])
    assert created['ok'], created
    before = content(owner)
    old = status(owner)
    assert edit('forge_asset_document_particle_opacity', owner, enabled=False)['ok']
    assert content(owner) == before and status(owner)['revision'] == old['revision']
    assert edit('forge_asset_document_particle_billboards', owner, enabled=False)['ok']
    assert content(owner) == before and status(owner)['revision'] == old['revision']
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    gpu = content(owner)
    expected = dict(before, version=3, profile='gpu_mesh1')
    assert gpu == expected
    failed(mutate('forge_asset_document_particle_evaluation', editor=owner,
                  document_session=old['session'], document_revision=old['revision'],
                  evaluation='cpu'), 'stale_document')
    failed(mutate('forge_asset_document_particle_evaluation', editor=owner,
                  document_session=old['session'] + 1000,
                  document_revision=status(owner)['revision'], evaluation='cpu'), 'wrong_document')
    for value in ('automatic', '', 1, True, None):
        assert not edit('forge_asset_document_particle_evaluation', owner, evaluation=value)['ok']
        assert content(owner) == gpu
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == before
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == gpu
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu')['ok']
    assert content(owner) == before
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    unchanged = status(owner)['revision']
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert status(owner)['revision'] == unchanged
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    alpha = dict(before, version=5, profile='cpu_alpha1')
    assert content(owner) == alpha
    unchanged = status(owner)['revision']
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    assert status(owner)['revision'] == unchanged
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == gpu
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == alpha
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == gpu
    settled = save()
    assert json.loads((root / 'Game' / path).read_text()) == gpu
    record = read('forge_asset_read', asset=settled['asset'])
    metadata = record['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 3
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    settled = save()
    assert content(owner) == alpha
    assert json.loads((root / 'Game' / path).read_text()) == alpha
    record = read('forge_asset_read', asset=settled['asset'])
    metadata = record['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 5
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == alpha
    # A real published Blend Material is required by the opacity profile.
    material_path = 'Assets/Opacity.material.json'
    assert mutate('forge_asset_document_create', editor='material', path=material_path, patch=[
        dict(op='replace', path='/overrides', value=dict(model='forge.gltf.unlit.v1',
                                                        alpha=2, depth_test=True, depth_write=False))])['ok']
    material = content('material')['asset_id']
    save('material')
    assert edit('forge_asset_document_close', 'material')['ok']
    assert edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/material', value=material)])['ok']
    bound_alpha = content(owner)
    old = status(owner)
    assert edit('forge_asset_document_particle_opacity', owner, enabled=True)['ok']
    opacity = dict(bound_alpha, version=6, profile='cpu_alpha2', opacity_enabled=True,
                   opacity_start=1, opacity_end=0, opacity_curve=[])
    assert content(owner) == opacity
    failed(mutate('forge_asset_document_particle_opacity', editor=owner,
                  document_session=old['session'], document_revision=old['revision'],
                  enabled=False), 'stale_document')
    failed(mutate('forge_asset_document_particle_opacity', editor=owner,
                  document_session=old['session'] + 1000,
                  document_revision=status(owner)['revision'], enabled=False), 'wrong_document')
    unchanged = status(owner)['revision']
    assert edit('forge_asset_document_particle_opacity', owner, enabled=True)['ok']
    assert status(owner)['revision'] == unchanged
    for invalid in (None, 0, 1, 'true', []):
        assert not edit('forge_asset_document_particle_opacity', owner, enabled=invalid)['ok']
        assert content(owner) == opacity and status(owner)['revision'] == unchanged
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == bound_alpha
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == opacity
    opacity_stops = [dict(id=str(uuid4()), time=t, value=v, interpolation=mode)
                     for t, v, mode in ((0, 0, 'linear'), (.4, 1, 'step'), (1, 0, 'linear'))]
    assert edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/opacity_curve', value=opacity_stops)])['ok']
    opacity = content(owner)
    unchanged = status(owner)['revision']
    assert not edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/opacity_curve/1/time', value=0)])['ok']
    assert content(owner) == opacity and status(owner)['revision'] == unchanged
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner)['opacity_curve'] == opacity_stops
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    assert content(owner) == opacity
    assert edit('forge_asset_document_particle_opacity', owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_alpha1'
    assert content(owner)['opacity_curve'] == opacity_stops
    assert edit('forge_asset_document_particle_opacity', owner, enabled=True)['ok']
    assert content(owner) == opacity
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 6
    assert json.loads((root / 'Game' / path).read_text()) == opacity
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == opacity
    old = status(owner)
    assert edit('forge_asset_document_particle_billboards', owner, enabled=True)['ok']
    billboard = dict(opacity, version=7, profile='cpu_billboard1', billboard_enabled=True)
    assert content(owner) == billboard
    failed(mutate('forge_asset_document_particle_billboards', editor=owner,
                  document_session=old['session'], document_revision=old['revision'],
                  enabled=False), 'stale_document')
    failed(mutate('forge_asset_document_particle_billboards', editor=owner,
                  document_session=old['session'] + 1000,
                  document_revision=status(owner)['revision'], enabled=False), 'wrong_document')
    unchanged = status(owner)['revision']
    assert edit('forge_asset_document_particle_billboards', owner, enabled=True)['ok']
    assert status(owner)['revision'] == unchanged
    for invalid in (None, 0, 1, 'true', []):
        assert not edit('forge_asset_document_particle_billboards', owner, enabled=invalid)['ok']
        assert content(owner) == billboard and status(owner)['revision'] == unchanged
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == opacity
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == billboard
    for evaluation, profile in (('gpu', 'gpu_mesh1'), ('cpu', 'cpu_mesh2'), ('cpu_alpha', 'cpu_billboard1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(billboard, profile=profile)
    assert edit('forge_asset_document_particle_billboards', owner, enabled=False)['ok']
    assert content(owner) == dict(billboard, profile='cpu_alpha2', billboard_enabled=False)
    assert edit('forge_asset_document_particle_billboards', owner, enabled=True)['ok']
    assert content(owner) == billboard
    assert edit('forge_asset_document_particle_opacity', owner, enabled=False)['ok']
    assert content(owner) == dict(billboard, opacity_enabled=False)
    assert edit('forge_asset_document_particle_opacity', owner, enabled=True)['ok']
    assert content(owner) == billboard
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 7
    assert json.loads((root / 'Game' / path).read_text()) == billboard
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == billboard
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu')['ok']
    assert edit('forge_asset_document_close', owner, discard=True)['ok']

    # Explicit GPU alignment retains CPU settings; the existing default stays CPU.
    assert mutate('forge_asset_document_create', editor='material',
                  path='Assets/GpuQuads.material.json', patch=[
                      dict(op='replace', path='/overrides',
                           value=dict(model='forge.gltf.unlit.v1', alpha=1,
                                      depth_test=True, depth_write=True))])['ok']
    gpu_material = content('material')['asset_id']
    save('material')
    assert edit('forge_asset_document_close', 'material')['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    before = status(owner)
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=True)['ok']
    gpu_billboard = dict(billboard, version=8, profile='gpu_billboard1', gpu_billboard_enabled=True)
    assert content(owner) == gpu_billboard
    failed(mutate('forge_asset_document_particle_billboards', editor=owner, backend='gpu',
                  document_session=before['session'], document_revision=before['revision'],
                  enabled=False), 'stale_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=True)['ok']
    assert status(owner)['revision'] == revision
    for invalid in (None, 0, 'GPU', 'cpu_alpha', []):
        assert not edit('forge_asset_document_particle_billboards', owner,
                        backend=invalid, enabled=False)['ok']
        assert content(owner) == gpu_billboard and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == billboard
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == gpu_billboard
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=False)['ok']
    assert content(owner) == dict(gpu_billboard, profile='gpu_mesh1', gpu_billboard_enabled=False)
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=True)['ok']
    # The device profile needs opaque/masked output; retain the old Blend selection
    # in source history and bind a real published masked Material through its owner.
    assert edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/material', value=gpu_material)])['ok']
    gpu_billboard = dict(gpu_billboard, material=gpu_material)
    assert content(owner) == gpu_billboard
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 8
    assert json.loads((root / 'Game' / path).read_text()) == gpu_billboard
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == gpu_billboard
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu')['ok']
    assert content(owner) == dict(gpu_billboard, profile='cpu_mesh2')
    assert edit('forge_asset_document_close', owner, discard=True)['ok']

    # Trails use the same source/history/publication owner as other evaluations.
    trail_tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_trails')
    assert trail_tool['inputSchema']['properties']['editor']['const'] == owner
    assert 'cpu_trail1' in catalog['trails'] and 'runtime_particle_trail1' in catalog['trails']
    trail_path = 'Assets/TrailEffect.particles.json'
    assert mutate('forge_asset_document_create', editor=owner, path=trail_path)['ok']
    original_trail = content(owner)
    before = status(owner)
    assert edit('forge_asset_document_particle_trails', owner, enabled=False)['ok']
    assert content(owner) == original_trail and status(owner)['revision'] == before['revision']
    settings = dict(duration=.75, segments=8)
    assert edit('forge_asset_document_particle_trails', owner, enabled=True, trail=settings)['ok']
    trail_source = content(owner)
    assert trail_source['version'] == 9 and trail_source['profile'] == 'cpu_trail1'
    assert trail_source['trail'] == settings and trail_source['mesh'] == original_trail['mesh']
    assert trail_source['asset_id'] == original_trail['asset_id'] and trail_source['trail_enabled']
    failed(mutate('forge_asset_document_particle_trails', editor=owner,
                  document_session=before['session'], document_revision=before['revision'],
                  enabled=False), 'stale_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_trails', owner, enabled=True)['ok']
    assert status(owner)['revision'] == revision
    for invalid in (dict(duration=0, segments=8), dict(duration=.5, segments=17),
                    dict(duration=True, segments=8), dict(duration=.5, segments=1.5),
                    dict(duration=.5), dict(duration=.5, segments=8, future=True)):
        assert not edit('forge_asset_document_particle_trails', owner,
                        enabled=True, trail=invalid)['ok']
        assert content(owner) == trail_source and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original_trail
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == trail_source
    for evaluation, profile in (('gpu', 'gpu_mesh1'), ('cpu', 'cpu_mesh2'), ('cpu_alpha', 'cpu_trail1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(trail_source, profile=profile)
    assert edit('forge_asset_document_particle_trails', owner, enabled=False)['ok']
    assert content(owner) == dict(trail_source, profile='cpu_alpha1', trail_enabled=False)
    assert edit('forge_asset_document_particle_trails', owner, enabled=True)['ok']
    assert content(owner) == trail_source
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 10
    assert json.loads((root / 'Game' / trail_path).read_text()) == trail_source
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=trail_path)['ok']
    assert content(owner) == trail_source
    assert edit('forge_asset_document_close', owner)['ok']

    assert 'cpu_collision1' in catalog['collision'] and 'runtime_particle_collision1' in catalog['collision']
    collision_path = 'Assets/PlaneEffect.particles.json'
    assert mutate('forge_asset_document_create', editor=owner, path=collision_path)['ok']
    original_collision = content(owner)
    before = status(owner)
    assert edit('forge_asset_document_particle_collision', owner, enabled=False)['ok']
    assert content(owner) == original_collision and status(owner)['revision'] == before['revision']
    plane = dict(normal=[1, 3, 0], offset=-2, radius=.05, restitution=.75, friction=.2, bounce_limit=8)
    assert edit('forge_asset_document_particle_collision', owner, enabled=True, collision=plane)['ok']
    collision_source = content(owner)
    assert collision_source['version'] == 10 and collision_source['profile'] == 'cpu_collision1'
    assert collision_source['collision'] == plane and collision_source['collision_enabled']
    assert collision_source['mesh'] == original_collision['mesh']
    assert collision_source['asset_id'] == original_collision['asset_id']
    failed(mutate('forge_asset_document_particle_collision', editor=owner,
                  document_session=before['session'], document_revision=before['revision'],
                  enabled=False), 'stale_document')
    failed(mutate('forge_asset_document_particle_collision', editor=owner,
                  document_session=before['session'] + 1000,
                  document_revision=status(owner)['revision'], enabled=False), 'wrong_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_collision', owner, enabled=True)['ok']
    assert content(owner) == collision_source and status(owner)['revision'] == revision
    for invalid in (dict(plane, normal=[0, 0, 0]), dict(plane, normal=[0, 1]),
                    dict(plane, normal=[0, True, 0]), dict(plane, offset=True),
                    dict(plane, radius=-.1), dict(plane, restitution=1.01),
                    dict(plane, friction=-1), dict(plane, bounce_limit=17),
                    dict(plane, bounce_limit=1.5), dict(plane, future=True),
                    {key: value for key, value in plane.items() if key != 'normal'}):
        assert not edit('forge_asset_document_particle_collision', owner,
                        enabled=True, collision=invalid)['ok']
        assert content(owner) == collision_source and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original_collision
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == collision_source
    for evaluation, profile in (('gpu', 'gpu_mesh1'), ('cpu', 'cpu_mesh2'), ('cpu_alpha', 'cpu_collision1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(collision_source, profile=profile)
    assert edit('forge_asset_document_particle_collision', owner, enabled=False)['ok']
    assert content(owner) == dict(collision_source, profile='cpu_alpha1', collision_enabled=False)
    assert edit('forge_asset_document_particle_collision', owner, enabled=True)['ok']
    assert content(owner) == collision_source
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 11
    assert json.loads((root / 'Game' / collision_path).read_text()) == collision_source
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=collision_path)['ok']
    assert content(owner) == collision_source
    assert edit('forge_asset_document_close', owner)['ok']

    weighted_tool = next(t for t in tools if t['name'] == 'forge_asset_document_particle_gpu_weighted')
    assert weighted_tool['inputSchema']['properties']['editor']['const'] == owner
    assert weighted_tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert 'gpu_weighted1' in catalog['gpu_weighted'] and 'runtime_particle_gpu_weighted1' in catalog['gpu_weighted']
    weighted_path = 'Assets/WeightedDevice.particles.json'
    assert mutate('forge_asset_document_create', editor=owner, path=weighted_path, patch=[
        dict(op='replace', path='/material', value=material),
        dict(op='add', path='/extensions', value=dict(preserve='weighted source'))])['ok']
    original = content(owner)
    before = status(owner)
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=False)['ok']
    assert content(owner) == original and status(owner)['revision'] == before['revision']
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    weighted = content(owner)
    assert weighted['version'] == 11 and weighted['profile'] == 'gpu_weighted1'
    assert weighted['gpu_weighted_enabled'] and weighted['mesh'] == original['mesh']
    assert weighted['asset_id'] == original['asset_id'] and weighted['extensions'] == original['extensions']
    failed(mutate('forge_asset_document_particle_gpu_weighted', editor=owner,
                  document_session=before['session'], document_revision=before['revision'],
                  enabled=False), 'stale_document')
    failed(mutate('forge_asset_document_particle_gpu_weighted', editor=owner,
                  document_session=before['session'] + 1000,
                  document_revision=status(owner)['revision'], enabled=False), 'wrong_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    assert status(owner)['revision'] == revision
    for invalid in (0, 1, None, 'true', [], {}):
        assert not edit('forge_asset_document_particle_gpu_weighted', owner, enabled=invalid)['ok']
        assert content(owner) == weighted and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == weighted
    for evaluation, profile in (('cpu', 'cpu_mesh2'), ('cpu_alpha', 'cpu_alpha1'), ('gpu', 'gpu_weighted1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(weighted, profile=profile)
    assert edit('forge_asset_document_particle_billboards', owner, backend='gpu', enabled=True)['ok']
    aligned = content(owner)
    assert aligned == dict(weighted, gpu_billboard_enabled=True, profile='gpu_weighted_billboard1')
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=False)['ok']
    assert content(owner) == dict(aligned, profile='gpu_billboard1', gpu_weighted_enabled=False)
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    assert content(owner) == aligned
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 14
    assert json.loads((root / 'Game' / weighted_path).read_text()) == aligned
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=weighted_path)['ok']
    assert content(owner) == aligned
    assert edit('forge_asset_document_close', owner)['ok']

    assert opacity_tool['inputSchema']['properties']['backend']['enum'] == ['cpu', 'gpu']
    assert opacity_tool['inputSchema']['properties']['backend']['default'] == 'cpu'
    assert 'runtime_particle_gpu_opacity1' in catalog['opacity'] and 'values16-19' in catalog['opacity']
    assert mutate('forge_asset_document_open', editor=owner, path=weighted_path)['ok']
    before = status(owner)
    assert edit('forge_asset_document_particle_opacity', owner, backend='gpu', enabled=False)['ok']
    assert content(owner) == aligned and status(owner)['revision'] == before['revision']
    assert edit('forge_asset_document_particle_opacity', owner, backend='gpu', enabled=True)['ok']
    fading_gpu = content(owner)
    assert fading_gpu == dict(aligned, version=12, profile='gpu_weighted_opacity_billboard1',
                              gpu_opacity_enabled=True)
    failed(mutate('forge_asset_document_particle_opacity', editor=owner, backend='gpu',
                  enabled=False, document_session=before['session'],
                  document_revision=before['revision']), 'stale_document')
    failed(mutate('forge_asset_document_particle_opacity', editor=owner, backend='gpu',
                  enabled=False, document_session=before['session'] + 1000,
                  document_revision=status(owner)['revision']), 'wrong_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_opacity', owner, backend='gpu', enabled=True)['ok']
    assert status(owner)['revision'] == revision
    for invalid in ('automatic', '', None, 1, True):
        assert not edit('forge_asset_document_particle_opacity', owner, backend=invalid, enabled=True)['ok']
        assert content(owner) == fading_gpu and status(owner)['revision'] == revision
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == aligned
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == fading_gpu
    gpu_stops = [dict(id=str(uuid4()), time=t, value=value, interpolation=mode)
                 for t, value, mode in ((0, 0, 'linear'), (.5, 1, 'step'), (1, .2, 'linear'))]
    assert edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/opacity_curve', value=gpu_stops)])['ok']
    fading_gpu = content(owner)
    revision = status(owner)['revision']
    assert not edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/opacity_curve/1/time', value=1)])['ok']
    assert content(owner) == fading_gpu and status(owner)['revision'] == revision
    for evaluation, profile in (('cpu', 'cpu_mesh2'), ('gpu', 'gpu_weighted_opacity_billboard1')):
        assert edit('forge_asset_document_particle_evaluation', owner, evaluation=evaluation)['ok']
        assert content(owner) == dict(fading_gpu, profile=profile)
    assert edit('forge_asset_document_particle_opacity', owner, enabled=True)['ok']
    assert content(owner) == dict(fading_gpu, profile='cpu_alpha2', opacity_enabled=True)
    assert edit('forge_asset_document_particle_opacity', owner, enabled=False)['ok']
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner) == fading_gpu
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=False)['ok']
    assert content(owner) == dict(fading_gpu, profile='gpu_billboard1', gpu_weighted_enabled=False)
    assert edit('forge_asset_document_particle_gpu_weighted', owner, enabled=True)['ok']
    assert content(owner) == fading_gpu
    settled = save()
    metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
    assert metadata['version'] == 3 and metadata['value_version'] == 18
    assert json.loads((root / 'Game' / weighted_path).read_text()) == fading_gpu
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=weighted_path)['ok']
    assert content(owner) == fading_gpu
    assert edit('forge_asset_document_close', owner)['ok']

    check_particle_graph(read, mutate, edit, status, content, failed, root)
