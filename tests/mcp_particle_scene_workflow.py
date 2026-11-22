"""Scene particle source, grants, history and publication through shared MCP owners."""
import json
import time
from uuid import uuid4


def check_particle_scene(read, mutate, edit, status, content, failed, root, tools):
    owner = 'particle_effect'
    name = 'forge_asset_document_particle_scene_collision'
    tool = next(item for item in tools if item['name'] == name)
    schema = tool['inputSchema']['properties']
    assert schema['editor']['const'] == owner and schema['enabled']['type'] == 'boolean'
    assert set(schema['collision']['required']) == {'radius', 'restitution', 'friction', 'bounce_limit', 'mask'}
    assert schema['space']['enum'] == ['world3d', 'worldxy']
    assert not tool['annotations']['readOnlyHint']
    catalog = read('forge_asset_particles_catalog')
    assert 'runtime_particle_scene_collision1' in catalog['scene_collision']
    assert {key: catalog['limits'][key] for key in (
        'scene_collision_capacity', 'scene_collision_traces', 'scene_collision_queries',
        'scene_collision_birth_queries')} == dict(scene_collision_capacity=1024,
            scene_collision_traces=2048, scene_collision_queries=8192, scene_collision_birth_queries=256)
    path = 'Assets/SceneResponse.particles.json'
    stops = [dict(id=str(uuid4()), time=t, value=size, interpolation='linear')
             for t, size in ((0, .2), (1, .5))]
    assert mutate('forge_asset_document_create', editor=owner, path=path, patch=[
        dict(op='add', path='/size_curve', value=stops),
        dict(op='add', path='/extensions', value={'retained': 'source extension'})])['ok']
    original = content(owner)
    old = status(owner)
    assert edit(name, owner, enabled=False)['ok']
    assert content(owner) == original and status(owner)['revision'] == old['revision']
    settings = dict(radius=.025, restitution=.75, friction=.2, bounce_limit=8, mask=0x80000001)
    assert edit(name, owner, enabled=True, collision=settings)['ok']
    scene = content(owner)
    assert scene['version'] == 16 and scene['profile'] == 'cpu_scene_collision1'
    assert scene['scene_collision_enabled'] and scene['scene_collision'] == settings
    assert not scene['collision_enabled'] and not scene['trail_enabled']
    assert scene['asset_id'] == original['asset_id'] and scene['size_curve'] == stops
    assert scene['extensions'] == original['extensions']
    failed(mutate(name, editor=owner, enabled=False, document_session=old['session'],
                  document_revision=old['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(name, editor=owner, enabled=False, document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for invalid in (None, 1, 'true'):
        assert not edit(name, owner, enabled=invalid)['ok']
        assert content(owner) == scene and status(owner)['revision'] == current['revision']
    invalid_settings = [dict(settings, radius=0), dict(settings, restitution=1.1),
        dict(settings, friction=-.1), dict(settings, bounce_limit=17),
        dict(settings, mask=-1), dict(settings, mask=2**32), dict(settings, mask=1.5),
        dict(settings, extra=True), {key: value for key, value in settings.items() if key != 'radius'}]
    for invalid in invalid_settings:
        assert not edit(name, owner, enabled=False, collision=invalid)['ok']
        assert content(owner) == scene and status(owner)['revision'] == current['revision']
    assert not edit(name, owner, enabled=True, backend='gpu')['ok']
    assert edit(name, owner, enabled=True)['ok']
    assert content(owner) == scene and status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == scene

    def save(version):
        assert edit('forge_asset_document_save', owner)['ok']
        deadline = time.monotonic() + 35
        while True:
            settled = status(owner)
            if not settled['save_requested'] and not settled['publishing']:
                break
            assert time.monotonic() < deadline, settled
            time.sleep(.02)
        assert not settled['dirty'] and not settled['unpublished'] and not settled['diagnostic'], settled
        metadata = read('forge_asset_read', asset=settled['asset'])['metadata']['forge.particle_effect']
        assert metadata['version'] == 3 and metadata['value_version'] == version
        assert json.loads((root / 'Game' / path).read_text()) == content(owner)

    save(38)
    assert edit('forge_asset_document_particle_billboards', owner, enabled=True)['ok']
    camera = content(owner)
    assert camera['profile'] == 'cpu_scene_collision_billboard1' and camera['scene_collision'] == settings
    save(39)
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner)['profile'] == 'gpu_mesh1'
    assert content(owner)['scene_collision'] == settings
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    assert content(owner) == camera
    assert edit('forge_asset_document_particle_collision', owner, enabled=True)['ok']
    assert content(owner)['profile'] == 'cpu_collision1'
    assert not content(owner)['scene_collision_enabled'] and content(owner)['scene_collision'] == settings
    assert edit(name, owner, enabled=True)['ok']
    assert content(owner) == camera
    assert edit('forge_asset_document_particle_billboards', owner, enabled=False)['ok']
    assert content(owner) == scene
    save(38)
    previous_session = status(owner)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == scene
    failed(mutate(name, editor=owner, enabled=False, document_session=previous_session['session'],
                  document_revision=previous_session['revision']), 'wrong_document')
    for invalid in (None, 1, 'xy', 'WORLDXY'):
        assert not edit(name, owner, enabled=True, space=invalid)['ok']
        assert content(owner) == scene
    narrow = dict(settings, radius=.001)
    assert edit(name, owner, enabled=True, collision=narrow)['ok']
    three_d = content(owner)
    revision = status(owner)['revision']
    assert not edit(name, owner, enabled=True, space='worldxy')['ok']
    assert content(owner) == three_d and status(owner)['revision'] == revision
    xy_settings = dict(settings, radius=.05)
    assert edit(name, owner, enabled=True, collision=xy_settings, space='worldxy')['ok']
    xy = content(owner)
    assert xy['version'] == 18 and xy['profile'] == 'cpu_scene_collision_2d1'
    assert xy['scene_collision_2d'] and xy['scene_collision'] == xy_settings
    assert xy['asset_id'] == scene['asset_id'] and xy['extensions'] == scene['extensions']
    assert xy['size_curve'] == stops
    save(52)
    assert edit('forge_asset_document_particle_billboards', owner, enabled=True)['ok']
    xy_camera = content(owner)
    assert xy_camera['profile'] == 'cpu_scene_collision_2d_billboard1'
    save(53)
    assert edit(name, owner, enabled=False)['ok']
    assert content(owner)['scene_collision_2d'] and not content(owner)['scene_collision_enabled']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == xy_camera
    assert edit('forge_asset_document_redo', owner)['ok'] and not content(owner)['scene_collision_enabled']
    assert edit(name, owner, enabled=True)['ok'] and content(owner) == xy_camera
    assert edit(name, owner, enabled=True, collision=narrow, space='world3d')['ok']
    assert content(owner)['profile'] == 'cpu_scene_collision_billboard1'
    assert not content(owner)['scene_collision_2d'] and content(owner)['scene_collision'] == narrow
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == xy_camera
    save(53)
    trail_name = 'forge_asset_document_particle_trails'
    trail_tool = next(item for item in tools if item['name'] == trail_name)
    assert trail_tool['inputSchema']['properties']['space']['enum'] == ['local', 'world']
    assert trail_tool['inputSchema']['properties']['space']['default'] == 'local'
    assert 'runtime_particle_scene_trail1' in catalog['trails']
    settings = dict(duration=.625, segments=12)
    before = status(owner)
    assert edit(trail_name, owner, space='world', enabled=True, trail=settings)['ok']
    historical = content(owner)
    assert historical['version'] == 19 and historical['profile'] == 'cpu_scene_trail_2d1'
    assert historical['world_trail_enabled'] and historical['scene_collision_2d']
    assert historical['trail'] == settings and historical['scene_collision'] == xy_camera['scene_collision']
    assert historical['size_curve'] == stops and historical['extensions'] == original['extensions']
    failed(mutate(trail_name, editor=owner, space='world', enabled=False,
                  document_session=before['session'], document_revision=before['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(trail_name, editor=owner, space='world', enabled=False,
                  document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for space in ('automatic', '', None, 1, True):
        assert not edit(trail_name, owner, space=space, enabled=True)['ok']
        assert content(owner) == historical and status(owner)['revision'] == current['revision']
    assert not edit(trail_name, owner, backend='gpu', space='world', enabled=True)['ok']
    assert content(owner) == historical and status(owner)['revision'] == current['revision']
    assert edit(trail_name, owner, space='world', enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    save(55)
    assert edit(name, owner, enabled=True, space='world3d')['ok']
    assert content(owner)['profile'] == 'cpu_scene_trail1' and content(owner)['version'] == 19
    save(54)
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == historical
    assert edit(trail_name, owner, enabled=True)['ok']
    assert content(owner)['profile'] == 'cpu_trail1' and not content(owner)['world_trail_enabled']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == historical
    assert edit(trail_name, owner, space='world', enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_collision_2d_billboard1'
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == historical
    save(55)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == historical
    impact_name = 'forge_asset_document_particle_scene_impacts'
    impact_tool = next(item for item in tools if item['name'] == impact_name)
    assert impact_tool['inputSchema']['properties']['editor']['const'] == owner
    assert impact_tool['inputSchema']['properties']['enabled']['type'] == 'boolean'
    assert not impact_tool['annotations']['readOnlyHint']
    assert 'runtime_particle_scene_impacts1' in catalog['scene_impacts']
    before = status(owner)
    assert edit(impact_name, owner, enabled=False)['ok']
    assert content(owner) == historical and status(owner)['revision'] == before['revision']
    assert edit(impact_name, owner, enabled=True)['ok']
    impacts = content(owner)
    assert impacts['version'] == 20 and impacts['profile'] == 'cpu_scene_impact_trail_2d1'
    assert impacts['scene_impacts_enabled'] and impacts['trail'] == historical['trail']
    failed(mutate(impact_name, editor=owner, enabled=False,
                  document_session=before['session'], document_revision=before['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(impact_name, editor=owner, enabled=False,
                  document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for invalid in (None, 1, 'true'):
        assert not edit(impact_name, owner, enabled=invalid)['ok']
        assert content(owner) == impacts and status(owner)['revision'] == current['revision']
    assert edit(impact_name, owner, enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    save(61)
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == historical
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == impacts
    assert edit(impact_name, owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_trail_2d1'
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == impacts
    save(61)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == impacts
    rest_name = 'forge_asset_document_particle_scene_resting'
    rest_tool = next(item for item in tools if item['name'] == rest_name)
    rest_schema = rest_tool['inputSchema']['properties']
    assert rest_schema['editor']['const'] == owner and rest_schema['enabled']['type'] == 'boolean'
    assert not rest_tool['annotations']['readOnlyHint']
    assert 'runtime_particle_scene_resting1' in catalog['scene_resting']
    before = status(owner)
    assert edit(rest_name, owner, enabled=False)['ok']
    assert content(owner) == impacts and status(owner)['revision'] == before['revision']
    assert edit(rest_name, owner, enabled=True)['ok']
    resting = content(owner)
    assert resting['version'] == 21 and resting['profile'] == 'cpu_scene_rest_impact_trail_2d1'
    assert resting['scene_resting_enabled'] and resting['scene_impacts_enabled']
    for key in ('asset_id', 'size_curve', 'extensions', 'trail', 'scene_collision'):
        assert resting[key] == impacts[key]
    failed(mutate(rest_name, editor=owner, enabled=False,
                  document_session=before['session'], document_revision=before['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(rest_name, editor=owner, enabled=False,
                  document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for invalid in (None, 1, 'true'):
        assert not edit(rest_name, owner, enabled=invalid)['ok']
        assert content(owner) == resting and status(owner)['revision'] == current['revision']
    assert edit(rest_name, owner, enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == impacts
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == resting
    save(73)
    assert edit(impact_name, owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_trail_2d1'
    assert content(owner)['scene_resting_enabled']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == resting
    assert edit(name, owner, enabled=True, space='world3d')['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_impact_trail1'
    assert edit(trail_name, owner, space='world', enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_impact_billboard1'
    assert edit('forge_asset_document_particle_billboards', owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_impact1'
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner)['profile'] == 'gpu_mesh1' and content(owner)['scene_resting_enabled']
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='cpu_alpha')['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_impact1'
    for _ in range(5):
        assert edit('forge_asset_document_undo', owner)['ok']
    assert content(owner) == resting
    assert edit(rest_name, owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_impact_trail_2d1'
    assert content(owner)['scene_impacts_enabled'] and not content(owner)['scene_resting_enabled']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == resting
    save(73)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == resting
    motion_name = 'forge_asset_document_particle_scene_motion'
    motion_tool = next(item for item in tools if item['name'] == motion_name)
    schema = motion_tool['inputSchema']['properties']
    assert schema['editor']['const'] == owner and schema['enabled']['type'] == 'boolean'
    assert not motion_tool['annotations']['readOnlyHint']
    assert 'runtime_particle_scene_motion1' in catalog['scene_motion']
    before = status(owner)
    assert edit(motion_name, owner, enabled=False)['ok']
    assert content(owner) == resting and status(owner)['revision'] == before['revision']
    assert edit(motion_name, owner, enabled=True)['ok']
    moving = content(owner)
    assert moving['version'] == 22 and moving['profile'] == 'cpu_scene_motion_rest_impact_trail_2d1'
    assert moving['scene_motion_enabled'] and moving['scene_resting_enabled'] and moving['scene_impacts_enabled']
    for key in ('asset_id', 'size_curve', 'extensions', 'trail', 'scene_collision'):
        assert moving[key] == resting[key]
    failed(mutate(motion_name, editor=owner, enabled=False,
                  document_session=before['session'], document_revision=before['revision']), 'stale_document')
    current = status(owner)
    failed(mutate(motion_name, editor=owner, enabled=False,
                  document_session=current['session'] + 1000,
                  document_revision=current['revision']), 'wrong_document')
    for invalid in (None, 1, 'true'):
        assert not edit(motion_name, owner, enabled=invalid)['ok']
        assert content(owner) == moving and status(owner)['revision'] == current['revision']
    assert edit(motion_name, owner, enabled=True)['ok']
    assert status(owner)['revision'] == current['revision']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == resting
    assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == moving
    save(97)
    assert edit('forge_asset_document_particle_evaluation', owner, evaluation='gpu')['ok']
    assert content(owner)['profile'] == 'gpu_mesh1' and content(owner)['scene_motion_enabled']
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == moving
    assert edit(motion_name, owner, enabled=False)['ok']
    assert content(owner)['profile'] == 'cpu_scene_rest_impact_trail_2d1'
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == moving
    save(97)
    assert edit('forge_asset_document_close', owner)['ok']
    assert mutate('forge_asset_document_open', editor=owner, path=path)['ok']
    assert content(owner) == moving
    assert edit('forge_asset_document_close', owner)['ok']
