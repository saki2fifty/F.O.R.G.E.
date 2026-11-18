"""Create and tune independently filtered routes through the native MCP owners."""


def check_navigation_areas(read, ok, control, mutate, until, discovery, edit_settings, planar):
    schemas = {c['id']: c for c in discovery()['result']['schema']['components']}
    area = next(f for f in schemas['forge.navigation_area']['fields'] if f['id'] == 'area')
    costs = next(f for f in schemas['forge.navigation_area_filter']['fields'] if f['id'] == 'costs')
    assert len(area['choices']) == 16 and area['choices'][-1]['value'] == 15 and not area['animatable'], area
    assert costs['type'] == 'array' and costs['count'] == 16 and costs['default'] == [1] * 16, costs

    def create(name, kind=4):
        return ok('forge_entity_create', arguments=dict(name=name, kind=kind))['results'][0]['selected']

    def add(entity, component):
        ok('forge_component_add', arguments=dict(entity=entity, component=component))

    def values(entity, component, **fields):
        return ok('forge_property_set_many', arguments=dict(entity=entity, component=component, values=fields))

    for overlay in (False, True):
        floor = create('Expensive crossing' if overlay else 'Default floor', 4 if planar else 3)
        values(floor, 'forge.local_translation', x=0, y=0, z=0)
        if planar:
            add(floor, 'forge.navigation_region_2d')
            values(floor, 'forge.navigation_region_2d', width=3 if overlay else 18, height=6 if overlay else 12)
        else:
            values(floor, 'forge.local_scale', x=3 if overlay else 18, y=1, z=6 if overlay else 12)
            add(floor, 'forge.navigation_surface')
        if overlay:
            add(floor, 'forge.navigation_area')
            values(floor, 'forge.navigation_area', area=2)
    ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
        cell_size=.2, cell_height=.1, plane=int(planar), profile='default', dynamic_obstacles=True))
    baked = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
    assert baked['asset'] and not baked['error'], baked
    actors = []
    for filtered in (False, True):
        actor = create('Filtered Agent' if filtered else 'Default Agent')
        actors.append(actor)
        values(actor, 'forge.local_translation', x=-7, y=0 if planar else .1, z=7 if planar else 0)
        add(actor, 'forge.navigation_agent')
        values(actor, 'forge.navigation_agent', navmesh=baked['asset'], has_destination=True,
               speed=0, stopping_distance=.1, destination_x=7, destination_y=0 if planar else .1,
               destination_z=7 if planar else 0)
        if filtered:
            add(actor, 'forge.navigation_area_filter')
            expensive = [1] * 16
            expensive[2] = 20
            before = read('forge_scene_read')
            values(actor, 'forge.navigation_area_filter', costs=expensive)
            after = read('forge_scene_read')
            ok('forge_history_undo')
            assert read('forge_scene_read') == before
            ok('forge_history_redo')
            assert read('forge_scene_read') == after
            for bad in (-1, 65536, True):
                denied = mutate('forge_property_set_many', arguments=dict(entity=actor,
                    component='forge.navigation_area_filter', values=dict(mask=bad)))
                assert not denied['ok'] and read('forge_scene_read') == after, denied
    edit_settings('forge_project_settings_game_defaults')
    edit_settings('forge_project_settings_save')
    ok('forge_document_save')
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])

    def rows():
        return {row['id']: row for row in read('forge_play_snapshot')['entities'] if row['id'] in actors}

    initial = until(rows, lambda value: len(value) == 2 and all(
        row.get('navigation_debug', {}).get('has_path') for row in value.values()))
    assert len(initial[actors[0]]['navigation_debug']['path']) == 2, initial
    assert len(initial[actors[1]]['navigation_debug']['path']) > 2, initial

    def tune(field, value):
        control('forge_play_tune', entity=actors[1], component='forge.navigation_area_filter',
                property=field, value=value)
        ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert 'error' not in ack['tune_ack'], ack

    def step():
        tick = read('forge_play_status')['timing']['tick']
        control('forge_play_step')
        until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and row['timing']['tick'] == tick + 1)

    tune('mask', 0)
    step()
    denied = rows()
    assert denied[actors[1]]['navigation_debug']['phase'] == 'Failed', denied
    assert denied[actors[1]]['world_affine'] == initial[actors[1]]['world_affine'], denied
    tune('mask', 65535)
    tune('costs', [1] * 16)
    step()
    changed = rows()
    assert len(changed[actors[1]]['navigation_debug']['path']) == 2, changed
    if planar:
        assert changed[actors[1]]['world_affine'][11] == 7, changed
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
