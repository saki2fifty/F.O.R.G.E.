"""Author a physical character and observe its actual Play route through MCP."""


def check_navigation_character(read, ok, control, until, edit_settings):
    def create(name, kind=4):
        return ok('forge_entity_create', arguments=dict(name=name, kind=kind))['results'][0]['selected']

    def add(entity, component):
        ok('forge_component_add', arguments=dict(entity=entity, component=component))

    def values(entity, component, **fields):
        ok('forge_property_set_many', arguments=dict(entity=entity, component=component, values=fields))

    floor = create('Baked character route', 3)
    values(floor, 'forge.local_scale', x=20, y=1, z=12)
    add(floor, 'forge.navigation_surface')
    ground = create('Physical floor')
    values(ground, 'forge.local_translation', y=-.1)
    add(ground, 'forge.physics_body')
    add(ground, 'forge.box_collider')
    values(ground, 'forge.box_collider', x=20, y=.2, z=12)
    wall = create('Physical wall outside baked source')
    values(wall, 'forge.local_translation', x=0, y=1.5, z=0)
    add(wall, 'forge.physics_body')
    add(wall, 'forge.box_collider')
    values(wall, 'forge.box_collider', x=.3, y=3, z=8)
    ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
        cell_size=.2, cell_height=.1, plane=0, profile='default', dynamic_obstacles=False))
    baked = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
    assert baked['asset'] and not baked['error'], baked
    actor = create('MCP physical character')
    values(actor, 'forge.local_translation', x=-4, y=.1, z=0)
    ok('forge_transform_binding', arguments=dict(entity=actor, spatial=dict(mode='world')))
    add(actor, 'forge.character_controller')
    add(actor, 'forge.navigation_agent')
    values(actor, 'forge.navigation_agent', navmesh=baked['asset'], movement=1,
           has_destination=True, destination_x=4, destination_y=0, destination_z=0, speed=0)
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
        control('forge_play_tune', entity=actor, component='forge.navigation_agent', property='speed', value=4)
        ack = until(lambda: read('forge_play_status'), lambda row: not row['tune_pending'] and row['tune_ack'] is not None)
        assert 'error' not in ack['tune_ack'], ack
        return ack['timing']['tick']

    saved = read('forge_scene_read')
    tick = start()
    control('forge_play_step')
    until(lambda: read('forge_play_status'), lambda row: row['control_ready'] and row['timing']['tick'] == tick + 1)
    first = follower()
    state = first['navigation_debug']
    assert state['sampled'] and state['phase'] == 'Moving' and -4 < state['position'][0] < -3.9, first
    assert all(abs(state['position'][axis] - first['world_affine'][offset]) < 1e-6
               for axis, offset in enumerate((3, 7, 11))), first
    control('forge_play_resume')
    blocked = until(follower, lambda row: row.get('navigation_debug', {}).get('phase') == 'Blocked')
    assert blocked['world_affine'][3] < -.45, blocked
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
    values(wall, 'forge.physics_body', enabled=False)
    ok('forge_document_save')
    saved = read('forge_scene_read')
    start()
    control('forge_play_resume')
    arrived = until(follower, lambda row: row.get('navigation_debug', {}).get('phase') == 'Arrived')
    assert arrived['world_affine'][3] > 3.8, arrived
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
