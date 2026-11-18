"""Author a physical character and observe its actual Play route through MCP."""


def check_navigation_character(read, ok, control, until, edit_settings, planar=False, jump=False, grounded=False, drop=False):
    assert not (jump and drop)
    linked = jump or drop
    goal_height = -1 if drop else 1 if planar else 2
    lift = 0 if drop else 1
    assert not (planar and linked) or grounded
    assert not grounded or planar
    def create(name, kind=4):
        return ok('forge_entity_create', arguments=dict(name=name, kind=kind))['results'][0]['selected']

    def add(entity, component):
        ok('forge_component_add', arguments=dict(entity=entity, component=component))

    def values(entity, component, **fields):
        ok('forge_property_set_many', arguments=dict(entity=entity, component=component, values=fields))

    floor = create('Baked character route', 4 if planar else 3)
    if planar:
        add(floor, 'forge.navigation_region_2d')
        values(floor, 'forge.navigation_region_2d', width=4 if linked else 20, height=6 if linked else 12)
        if linked:
            values(floor, 'forge.local_translation', x=-3, y=0)
            landing = create('Baked XY landing')
            values(landing, 'forge.local_translation', x=3, y=0)
            add(landing, 'forge.navigation_region_2d')
            values(landing, 'forge.navigation_region_2d', width=4, height=6)
            add(floor, 'forge.navigation_link')
            values(floor, 'forge.navigation_link', plane=1, start_x=1, end_x=5,
                   end_y=goal_height, arc_height=lift, bidirectional=False)
        if grounded:
            ground = create('Physical grounded XY floor')
            values(ground, 'forge.local_translation', x=-3 if linked else 0, y=-.5)
            add(ground, 'forge.physics_body_2d')
            add(ground, 'forge.box_collider_2d')
            values(ground, 'forge.box_collider_2d', x=4 if linked else 20, y=1)
            if linked:
                support = create('Physical XY landing')
                values(support, 'forge.local_translation', x=3, y=goal_height-.5)
                add(support, 'forge.physics_body_2d')
                add(support, 'forge.box_collider_2d')
                values(support, 'forge.box_collider_2d', x=4, y=1)
    else:
        values(floor, 'forge.local_scale', x=4 if linked else 20, y=1, z=6 if linked else 12)
        if linked:
            values(floor, 'forge.local_translation', x=-3, y=0)
        add(floor, 'forge.navigation_surface')
        ground = create('Physical floor')
        values(ground, 'forge.local_translation', x=-3 if linked else 0, y=-.1)
        add(ground, 'forge.physics_body')
        add(ground, 'forge.box_collider')
        values(ground, 'forge.box_collider', x=4 if linked else 20, y=.2, z=6 if linked else 12)
        if linked:
            landing = create('Baked lower landing' if drop else 'Baked raised landing', 3)
            values(landing, 'forge.local_translation', x=3, y=goal_height)
            values(landing, 'forge.local_scale', x=4, y=1, z=6)
            add(landing, 'forge.navigation_surface')
            add(floor, 'forge.navigation_link')
            values(floor, 'forge.navigation_link', start_x=.25, end_x=1.25,
                   end_y=goal_height, arc_height=lift, bidirectional=False)
            support = create('Physical raised landing')
            values(support, 'forge.local_translation', x=3, y=goal_height-.1)
            add(support, 'forge.physics_body')
            add(support, 'forge.box_collider')
            values(support, 'forge.box_collider', x=4, y=.2, z=6)
    body_type = 'forge.physics_body_2d' if planar else 'forge.physics_body'
    collider_type = 'forge.box_collider_2d' if planar else 'forge.box_collider'
    wall = create('Physical wall outside baked source')
    values(wall, 'forge.local_translation', x=0, y=3 if linked else 1.5, z=0)
    add(wall, body_type)
    add(wall, collider_type)
    values(wall, collider_type, **(dict(x=.3, y=20 if linked else 8) if planar else dict(x=.3, y=20 if linked else 3, z=8)))
    ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
        cell_size=.2, cell_height=.1, plane=int(planar), profile='default', dynamic_obstacles=False))
    baked = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
    assert baked['asset'] and not baked['error'], baked
    actor = create('MCP physical character')
    start_x = -3 if linked else -4
    values(actor, 'forge.local_translation', x=start_x, y=0 if grounded else .1, z=37.125 if planar else 0)
    ok('forge_transform_binding', arguments=dict(entity=actor, spatial=dict(mode='world')))
    controller = 'forge.character_controller_2d' if planar else 'forge.character_controller'
    add(actor, controller)
    if planar:
        values(actor, controller, mode=0 if grounded else 1, gravity_factor=1 if grounded else 0)
    add(actor, 'forge.navigation_agent')
    before_navigation = read('forge_scene_read')
    values(actor, 'forge.navigation_agent', navmesh=baked['asset'], movement=3 if grounded else 2 if planar else 1,
           has_destination=True, destination_x=3 if linked else 4,
           destination_y=goal_height if linked else 0, destination_z=0, speed=0)
    authored_navigation = read('forge_scene_read')
    ok('forge_history_undo')
    assert read('forge_scene_read') == before_navigation
    ok('forge_history_redo')
    assert read('forge_scene_read') == authored_navigation
    edit_settings('forge_project_settings_game_defaults')
    edit_settings('forge_project_settings_save')
    ok('forge_document_save')

    def follower():
        return next(e for e in read('forge_play_snapshot')['entities'] if e['id'] == actor)

    def start():
        ok('forge_play_start')
        until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
        control('forge_play_pause')
        until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])
        control('forge_play_tune', entity=actor, component='forge.navigation_agent', property='speed', value=6 if linked else 4)
        ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert 'error' not in ack['tune_ack'], ack
        return ack['timing']['tick']

    saved = read('forge_scene_read')
    tick = start()
    control('forge_play_step')
    until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and row['timing']['tick'] == tick + 1)
    first = follower()
    state = first['navigation_debug']
    assert state['sampled'] and state['phase'] == 'Moving' and start_x < state['position'][0] < start_x + (.15 if linked else .1), first
    assert all(abs(state['position'][axis] - first['world_affine'][offset]) < 1e-6
               for axis, offset in enumerate((3, 7, 11))), first
    if planar:
        assert state['position'][2] == 37.125 and state['velocity'][2] == 0, first
    control('forge_play_resume')
    blocked = until(follower, lambda row: row.get('navigation_debug', {}).get('phase') == ('Failed' if linked else 'Blocked'))
    assert blocked['world_affine'][3] < (-.44 if planar else -.45), blocked
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    values(wall, body_type, enabled=False)
    if linked:
        values(actor, 'forge.local_translation', x=-2)
        values(actor, 'forge.navigation_agent', stopping_distance=5, destination_x=2)
        ok('forge_document_save')
        parked_source = read('forge_scene_read')
        tick = start()
        control('forge_play_step')
        until(lambda: read('forge_play_status'),
              lambda row: row['control_ready'] and row['timing']['tick'] == tick + 1)
        parked = follower()
        assert parked['navigation_debug']['phase'] == 'Arrived' and \
            abs(parked['world_affine'][3] + 2) < 1e-6, parked
        control('forge_play_stop')
        assert read('forge_scene_read') == parked_source
        values(actor, 'forge.local_translation', x=-3)
        values(actor, 'forge.navigation_agent', stopping_distance=.1, destination_x=3)
    ok('forge_document_save')
    saved = read('forge_scene_read')
    start()
    control('forge_play_resume')
    arrived = until(follower, lambda row: row.get('navigation_debug', {}).get('phase') == 'Arrived')
    assert arrived['world_affine'][3] > (2.8 if linked else 3.8), arrived
    if grounded and not linked:
        assert abs(arrived['world_affine'][7]) < .02, arrived
    if linked:
        assert abs(arrived['world_affine'][7] - goal_height) < .1, arrived
    if planar:
        assert arrived['world_affine'][11] == 37.125, arrived
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
