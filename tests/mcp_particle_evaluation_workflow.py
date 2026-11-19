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
    def save():
        assert edit('forge_asset_document_save', owner)['ok']
        deadline = time.monotonic() + 35
        while True:
            settled = status(owner)
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
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu')['ok']
    assert edit('forge_asset_document_close', owner, discard=True)['ok']

    check_particle_graph(read, mutate, edit, status, content, failed, root)
