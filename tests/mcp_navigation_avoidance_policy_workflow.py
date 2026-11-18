"""Author and observe asymmetric peer steering through the actual MCP owners."""


def check_avoidance_policies(read, ok, control, mutate, until, discovery, edit_settings, planar):
    component = next(c for c in discovery()['result']['schema']['components']
                     if c['id'] == 'forge.navigation_avoidance')
    fields = {f['id']: f for f in component['fields']}
    for key, default, maximum in (('layers', 1, 4294967295),
                                   ('mask', 4294967295, 4294967295), ('priority', .5, 1)):
        assert fields[key]['default'] == default and fields[key]['maximum'] == maximum, fields[key]
        assert fields[key]['minimum'] == 0 and not fields[key]['animatable'], fields[key]

    def create(name, kind=4):
        return ok('forge_entity_create', arguments=dict(name=name, kind=kind))['results'][0]['selected']

    def add(entity, kind):
        ok('forge_component_add', arguments=dict(entity=entity, component=kind))

    def values(entity, kind, **props):
        return ok('forge_property_set_many', arguments=dict(entity=entity, component=kind, values=props))

    floor = create('Avoidance floor', 4 if planar else 3)
    values(floor, 'forge.local_translation', x=0, y=0, z=0)
    if planar:
        add(floor, 'forge.navigation_region_2d')
        values(floor, 'forge.navigation_region_2d', width=18, height=12)
    else:
        values(floor, 'forge.local_scale', x=18, y=1, z=12)
        add(floor, 'forge.navigation_surface')
    ok('forge_navigation_build', settings=dict(radius=.4, height=2, climb=.4, slope=45,
        cell_size=.2, cell_height=.1, plane=int(planar), profile='default'))
    baked = until(lambda: read('forge_navigation_status'), lambda row: not row['busy'])
    assert baked['asset'] and not baked['error'], baked
    actor, reference, peer = [create(name) for name in ('Observed Agent', 'Isolated reference', 'Idle peer')]
    for entity in (actor, reference, peer):
        values(entity, 'forge.local_translation', x=0 if entity == peer else -1.2,
               y=0 if planar else .1, z=(7 if entity == peer else 2) if planar else 0)
        add(entity, 'forge.navigation_agent')
        values(entity, 'forge.navigation_agent', navmesh=baked['asset'], speed=0,
               has_destination=entity != peer, destination_x=4,
               destination_y=0 if planar else .1, destination_z=2 if planar else 0)
        add(entity, 'forge.navigation_avoidance')
    values(reference, 'forge.navigation_avoidance', layers=0, mask=0)
    values(peer, 'forge.navigation_avoidance', layers=2147483648, priority=.25)
    before = read('forge_scene_read')
    values(actor, 'forge.navigation_avoidance', mask=2147483648, priority=.75)
    after = read('forge_scene_read')
    ok('forge_history_undo')
    assert read('forge_scene_read') == before
    ok('forge_history_redo')
    assert read('forge_scene_read') == after
    for key, value in (('layers', -1), ('mask', 4294967296), ('mask', True),
                       ('priority', 1.1), ('priority', True)):
        refused = mutate('forge_property_set_many', arguments=dict(entity=actor,
            component='forge.navigation_avoidance', values={key: value}))
        assert not refused['ok'] and read('forge_scene_read') == after, refused
    edit_settings('forge_project_settings_game_defaults')
    edit_settings('forge_project_settings_save')
    ok('forge_document_save')
    saved = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda row: row['ready'] and row['control_ready'])
    control('forge_play_pause')
    until(lambda: read('forge_play_status'), lambda row: row['paused'] and row['control_ready'])

    def rows():
        return {row['id']: row for row in read('forge_play_snapshot')['entities']
                if row['id'] in (actor, reference, peer)}

    until(rows, lambda value: len(value) == 3 and
          all(value[entity].get('navigation_debug', {}).get('sampled')
              for entity in (actor, reference)) and
          value[peer].get('navigation_debug', {}).get('phase') == 'Idle')

    def tune(entity, component, field, value):
        tick = read('forge_play_status')['timing']['tick']
        control('forge_play_tune', entity=entity, component=component, property=field, value=value)
        ack = until(lambda: read('forge_play_status'), lambda row:
                    not row['tune_pending'] and row['tune_ack'] is not None)
        assert 'error' not in ack['tune_ack'] and ack['timing']['tick'] == tick, ack

    def step():
        tick = read('forge_play_status')['timing']['tick']
        control('forge_play_step')
        until(lambda: read('forge_play_status'), lambda row:
              row['control_ready'] and row['timing']['tick'] == tick + 1)

    tune(actor, 'forge.navigation_agent', 'speed', 2)
    tune(reference, 'forge.navigation_agent', 'speed', 2)
    step()
    initial = rows()
    assert initial[actor]['world_affine'] == initial[reference]['world_affine'], initial
    tune(peer, 'forge.navigation_avoidance', 'priority', .9)
    for _ in range(12):
        step()
    changed = rows()
    axis = 7 if planar else 11
    assert abs(changed[actor]['world_affine'][axis] - changed[reference]['world_affine'][axis]) > .001, changed
    assert changed[peer]['world_affine'] == initial[peer]['world_affine'], changed
    if planar:
        assert changed[actor]['world_affine'][11] == 2 and changed[peer]['world_affine'][11] == 7, changed
    tune(actor, 'forge.navigation_avoidance', 'mask', 0)
    tune(actor, 'forge.navigation_avoidance', 'layers', 4294967295)
    step()
    control('forge_play_stop')
    assert read('forge_scene_read') == saved
