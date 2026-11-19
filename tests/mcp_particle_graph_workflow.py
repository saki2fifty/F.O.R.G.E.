"""Typed particle graph source authoring through the existing live source owners."""
import json
import time
from uuid import uuid4


def check_particle_graph(read, mutate, edit, status, content, failed, root):
    catalog = read('forge_asset_graph_catalog', domain='particle')
    schemas = {node['schema'] for node in catalog['nodes']}
    assert {'particle_output', 'particle_position', 'particle_velocity', 'particle_origin',
            'particle_age', 'particle_age01', 'particle_size', 'particle_color',
            'particle_random', 'sine', 'add', 'function'} <= schemas
    assert not schemas & {'parameter', 'texture2d', 'world_position', 'ui_time', 'surface'}
    assert not any(node['schema'].startswith('particle_')
                   for node in read('forge_asset_graph_catalog')['nodes'])
    owner = 'material.graph'
    template = read('forge_asset_document_template', editor=owner, domain='particle')
    assert template['version'] == 4 and template['graph']['format'] == 'forge.particle.graph'
    output = read('forge_asset_graph_node', schema='particle_output')
    assert [port['key'] for port in output['inputs']] == ['position', 'size', 'color']
    path = 'Assets/TypedParticleGraph.shader.json'
    assert mutate('forge_asset_document_create', editor=owner, path=path, domain='particle')['ok']
    initial = content(owner)
    before = status(owner)
    assert before['particle_graph'] and isinstance(before['compiled_current'], bool)
    assert isinstance(before['compiler_pending'], bool)
    age = read('forge_asset_graph_node', schema='particle_age')['node']
    unavailable = dict(age, version=2, data=dict(opaque_future=[1, 2, 3]))
    projected = read('forge_asset_graph_node', node=unavailable)
    assert not projected['available'] and projected['node'] == unavailable
    assert not projected['inputs'] and not projected['outputs']
    sine = read('forge_asset_graph_node', schema='sine')['node']
    def edge(source, target, port):
        return dict(id=str(uuid4()), **{'from': dict(node=source['id'], port='out'),
                                        'to': dict(node=target['id'], port=port)})
    patches = [dict(op='add', path='/graph/nodes/-', value=node) for node in (age, sine)]
    patches += [dict(op='add', path='/graph/edges/-', value=value) for value in
                (edge(age, sine, 'value'), edge(sine, initial['graph']['nodes'][0], 'size'))]
    assert edit('forge_asset_document_patch', owner, patch=patches)['ok']
    changed = content(owner)
    failed(mutate('forge_asset_document_patch', editor=owner, patch=patches,
                  document_session=before['session'], document_revision=before['revision']),
           'stale_document')
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == initial
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == changed
    assert edit('forge_asset_document_close', owner, discard=True)['ok']
    assert json.loads((root / 'Game' / path).read_text()) == initial
    # Source/history proof only. A cooked Compute program requires the Windows worker.
    owner = 'particle_effect'
    effect_path = 'Assets/TypedGraphEffect.particles.json'
    assert mutate('forge_asset_document_create', editor=owner, path=effect_path)['ok']
    original = content(owner)
    before = status(owner)
    program = dict(shader=initial['asset_id'], extent=[2, 3, 4], maximum_size=.8)
    assert edit('forge_asset_document_particle_program', owner, program=program)['ok']
    graph = content(owner)
    assert graph['version'] == 4 and graph['profile'] == 'gpu_graph1' and graph['program'] == program
    assert graph['asset_id'] == original['asset_id']
    failed(mutate('forge_asset_document_particle_program', editor=owner, program=program,
                  document_session=before['session'], document_revision=before['revision']),
           'stale_document')
    revision = status(owner)['revision']
    assert edit('forge_asset_document_particle_program', owner, program=program)['ok']
    assert status(owner)['revision'] == revision
    for invalid in (dict(program, extent=[-1, 2, 3]), dict(program, extent=[1, 2]),
                    dict(program, extent=[1e7, 2, 3]), dict(program, extent=[1e-100, 2, 3]),
                    dict(program, maximum_size=-1),
                    dict(program, maximum_size=1e5), dict(program, shader=None),
                    dict(program, extra='unknown')):
        assert not edit('forge_asset_document_particle_program', owner, program=invalid)['ok']
        assert content(owner) == graph
    for invalid in (None, 1, 'true'):
        assert not edit('forge_asset_document_particle_program', owner, program=program, activate=invalid)['ok']
        assert content(owner) == graph
    failed(mutate('forge_asset_document_particle_program', editor=owner, program=program,
                  document_session=status(owner)['session'] + 1000,
                  document_revision=status(owner)['revision']), 'wrong_document')
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == graph
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu')['ok']
    cpu = content(owner)
    assert cpu['version'] == 4 and cpu['profile'] == 'cpu_mesh2' and cpu['program'] == program
    retained_revision = status(owner)['revision']
    adjusted = dict(program, maximum_size=.7)
    assert edit('forge_asset_document_particle_program', owner, program=adjusted, activate=False)['ok']
    assert status(owner)['revision'] == retained_revision + 1
    assert content(owner)['profile'] == 'cpu_mesh2' and content(owner)['program'] == adjusted
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == cpu
    assert edit('forge_asset_document_save', owner)['ok']
    deadline = time.monotonic() + 35
    while True:
        settled = status(owner)
        if not settled['save_requested'] and not settled['publishing']:
            break
        assert time.monotonic() < deadline, settled
        time.sleep(.02)
    assert not settled['dirty'] and not settled['unpublished'] and not settled['diagnostic'], settled
    record = read('forge_asset_read', asset=settled['asset'])
    assert record['metadata']['forge.particle_effect']['value_version'] == 2
    assert json.loads((root / 'Game' / effect_path).read_text()) == cpu
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=effect_path)['ok']
    assert content(owner) == cpu
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='graph')['ok']
    assert content(owner) == graph
    assert edit('forge_asset_document_close', owner, discard=True)['ok']
