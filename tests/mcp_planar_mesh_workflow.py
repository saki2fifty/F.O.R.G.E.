"""Planar creation through the existing source document and publication owners."""
import json
import math
import time


def check_planar_mesh_workflow(read, mutate, edit, status, content, root, declared, *, curved_only=False):
    owner = 'editable_mesh'
    schemas = {tool['name']: tool['inputSchema']['properties'] for tool in declared}
    for name in ('forge_asset_document_template', 'forge_asset_document_create'):
        properties = schemas[name]
        assert properties['mesh_profile']['enum'] == [
            'cube', 'sculpt_sphere', 'planar_quad', 'planar_polygon', 'planar_outline', 'planar_holes', 'planar_ellipse', 'planar_curve']
        assert properties['outline']['minItems'] == 3 and properties['outline']['maxItems'] == 16
        assert properties['holes']['minItems'] == 1 and properties['holes']['maxItems'] == 3
        assert properties['holes']['items'] == properties['outline']
        assert properties['mesh_size']['minItems'] == properties['mesh_size']['maxItems'] == 2
    before_scene = read('forge_scene_read')
    quad = read('forge_asset_document_template', editor=owner,
                mesh_profile='planar_quad', mesh_size=[4, 2])
    assert [v['position'] for v in quad['vertices']] == [[-2, -1, 0], [2, -1, 0], [2, 1, 0], [-2, 1, 0]]
    assert not status(owner)['open']
    assert not list((root / 'Game/Assets').glob('*.mesh.json'))
    concave = [[0, 0], [3, 0], [3, 1], [1, 1], [1, 3], [0, 3]]
    for points in [concave, list(reversed(concave))]:
        preview = read('forge_asset_document_template', editor=owner,
                       mesh_profile='planar_outline', outline=points)
        assert [v['position'][:2] for v in preview['vertices']] == points
        assert len(preview['faces']) == 4 and preview['next_id'] == 11
        assert all(len(face['corners']) == 3 for face in preview['faces'])
        vertices = {v['id']: v['position'] for v in preview['vertices']}
        area = 0.
        for face in preview['faces']:
            a, b, c = [vertices[corner['vertex']] for corner in face['corners']]
            twice = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            assert twice > 0
            area += twice / 2
            for corner in face['corners']:
                p = vertices[corner['vertex']]
                assert abs(corner['uv'][0] - p[0] / 3) < 1e-10
                assert abs(corner['uv'][1] - (1 - p[1] / 3)) < 1e-10
        assert area == 5
    frame = [[-2, -2], [2, -2], [2, 2], [-2, 2]]
    opening = [[-.5, -.5], [.5, -.5], [.5, .5], [-.5, .5]]
    for boundary, hole in [(frame, opening), (list(reversed(frame)), list(reversed(opening)))]:
        preview = read('forge_asset_document_template', editor=owner,
                       mesh_profile='planar_holes', outline=boundary, holes=[hole])
        assert [v['position'][:2] for v in preview['vertices']] == boundary + hole
        assert len(preview['faces']) == 8 and preview['next_id'] == 17
        positions = {v['id']: v['position'] for v in preview['vertices']}
        area = 0
        for face in preview['faces']:
            a, b, c = [positions[corner['vertex']] for corner in face['corners']]
            t = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            assert t > 0
            area += t / 2
        assert area == 15
    k = 4 * (math.sqrt(2) - 1) / 3
    knots = [dict(point=[1, 0], incoming=[0, -k], outgoing=[0, k]),
             dict(point=[0, 1], incoming=[k, 0], outgoing=[-k, 0]),
             dict(point=[-1, 0], incoming=[0, k], outgoing=[0, -k]),
             dict(point=[0, -1], incoming=[-k, 0], outgoing=[k, 0])]
    if curved_only:
        for profile, options, vertices in [
                ('planar_ellipse', dict(ellipse_radii=[2, 1], ellipse_segments=24), 24),
                ('planar_ellipse', dict(ellipse_radii=[2, 1], ellipse_segments=24,
                                        ellipse_opening_scale=.5), 48),
                ('planar_curve', dict(curve_knots=knots, curve_tolerance=.01), None)]:
            preview = read('forge_asset_document_template', editor=owner,
                           mesh_profile=profile, **options)
            assert preview['version'] == 1 and all(len(f['corners']) == 3 for f in preview['faces'])
            assert 3 <= len(preview['vertices']) <= 64
            if vertices is not None:
                assert len(preview['vertices']) == vertices
            assert 'curve_knots' not in preview and 'ellipse_radii' not in preview
    invalid = [dict(mesh_profile='unknown'), dict(mesh_profile='planar_quad', mesh_size=[0, 1]),
               dict(mesh_profile='planar_polygon'), dict(mesh_profile='planar_polygon', outline=[]),
               dict(mesh_profile='cube', outline=[[0, 0], [1, 0], [0, 1]]),
               dict(mesh_profile='planar_outline'),
               dict(mesh_profile='planar_outline', outline=[[0, 0], [2, 2], [0, 2], [2, 0]]),
               dict(mesh_profile='planar_outline', outline=[[0, 0], [2, 0], [2, 2], [1, 0], [0, 2]]),
               dict(mesh_profile='planar_outline', outline=concave + [[0, 0]]),
               dict(mesh_profile='planar_polygon', outline=[
                   [math.cos(4 * math.pi * i / 5), math.sin(4 * math.pi * i / 5)] for i in range(5)])]
    invalid += [dict(mesh_profile='planar_holes', outline=frame),
                dict(mesh_profile='planar_holes', holes=[opening]),
                dict(mesh_profile='planar_holes', outline=frame, holes=[]),
                dict(mesh_profile='planar_holes', outline=frame, holes=[opening] * 4),
                dict(mesh_profile='planar_holes', outline=frame, holes=[opening, opening]),
                dict(mesh_profile='planar_holes', outline=frame,
                     holes=[[[1.5, 1.5], [3, 1.5], [3, 3], [1.5, 3]]]),
                dict(mesh_profile='planar_outline', outline=frame, holes=[opening]),
                dict(mesh_profile='cube', holes=[opening])]
    if curved_only:
        invalid = [dict(mesh_profile='planar_ellipse', ellipse_radii=[0, 1]),
                   dict(mesh_profile='planar_ellipse', ellipse_radii=[-1, 1]),
                   dict(mesh_profile='planar_ellipse', ellipse_segments=7),
                   dict(mesh_profile='planar_ellipse', ellipse_segments=64, ellipse_opening_scale=.5),
                   dict(mesh_profile='planar_ellipse', ellipse_opening_scale=1),
                   dict(mesh_profile='planar_curve'),
                   dict(mesh_profile='planar_curve', curve_knots=knots, curve_tolerance=0),
                   dict(mesh_profile='planar_curve', curve_knots=knots, curve_tolerance=1e-8),
                   dict(mesh_profile='planar_curve', curve_knots=knots[:2]),
                   dict(mesh_profile='cube', curve_knots=knots),
                   dict(mesh_profile='planar_curve', curve_knots=knots, ellipse_segments=32)]
    for options in invalid:
        result = mutate('forge_asset_document_create', editor=owner, path='Assets/Rejected.mesh.json', **options)
        assert not result['ok'] and not (root / 'Game/Assets/Rejected.mesh.json').exists(), result
        assert not status(owner)['open']
    result = mutate('forge_asset_document_create', editor='material',
                    path='Assets/Rejected.material.json', mesh_profile='planar_quad')
    assert not result['ok'] and not (root / 'Game/Assets/Rejected.material.json').exists(), result

    def save():
        result = edit('forge_asset_document_save', owner)
        assert result['ok'], result
        deadline = time.monotonic() + 35
        while True:
            current = status(owner)
            if not current['save_requested'] and not current['publishing']:
                break
            assert time.monotonic() < deadline, current
            time.sleep(.02)
        assert not current['dirty'] and not current['diagnostic'], current
        records = json.loads((root / 'Game/forge.assets.json').read_text())['assets']
        record = next(row for row in records if row['id'] == current['asset'])
        assert record['type'] == 'mesh' and record['metadata']['forge.import']

    cases = [
            ('planar_polygon', dict(outline=[[-1, -1], [1, -1], [1, 1], [-1, 1]])),
            ('planar_quad', dict(mesh_size=[4, 2])),
            ('planar_outline', dict(outline=concave)),
            ('planar_outline', dict(outline=list(reversed(concave)))),
            ('planar_holes', dict(outline=frame, holes=[opening]))]
    if curved_only:
        cases = [('planar_ellipse', dict(ellipse_radii=[2, 1], ellipse_segments=24)),
                 ('planar_ellipse', dict(ellipse_radii=[2, 1], ellipse_segments=24, ellipse_opening_scale=.5)),
                 ('planar_curve', dict(curve_knots=knots, curve_tolerance=.01))]
    for index, (profile, options) in enumerate(cases):
        suffix = '-clockwise' if index == 3 else ''
        path = f'Assets/{profile}{suffix}-{index}.mesh.json'
        result = mutate('forge_asset_document_create', editor=owner, path=path,
                        mesh_profile=profile, **options)
        assert result['ok'], result
        baseline = content(owner)
        assert baseline['asset_id'] != quad['asset_id'] and baseline['version'] == 1
        old = status(owner)
        result = edit('forge_asset_document_patch', owner,
                      patch=[dict(op='replace', path=f'/vertices/{i}/position/2', value=1)
                             for i in range(len(baseline['vertices']))])
        assert result['ok'], result
        changed = content(owner)
        assert all(v['position'][2] == 1 for v in changed['vertices'])
        assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == baseline
        assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == changed
        rejected = mutate('forge_asset_document_patch', editor=owner,
                          document_session=old['session'], document_revision=old['revision'],
                          patch=[dict(op='replace', path='/vertices/0/position/0', value=0)])
        assert not rejected['ok'] and content(owner) == changed, rejected
        rejected = edit('forge_asset_document_patch', owner,
                        patch=[dict(op='replace', path='/vertices/0/position/0', value=100001)])
        assert not rejected['ok'] and content(owner) == changed, rejected
        save()
        assert json.loads((root / 'Game' / path).read_text()) == changed
        assert edit('forge_asset_document_close', owner)['ok']
        rejected = mutate('forge_asset_document_open', editor=owner, path=path, mesh_profile=profile)
        assert not rejected['ok'] and not status(owner)['open'], rejected
        assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
        assert content(owner) == changed
        assert edit('forge_asset_document_close', owner)['ok']
    assert read('forge_scene_read') == before_scene
