"""Exercise the existing Play owner and rebuild worker through actual MCP."""
import uuid


def check_navigation_rebuild(read, ok, control, mutate, until, navigation_floor,
                             edit_settings, advertised):
    assert advertised['forge_play_navigation']['annotations']['readOnlyHint']
    assert not advertised['forge_play_navigation_rebuild']['annotations']['readOnlyHint']
    assert advertised['forge_play_navigation_rebuild']['annotations']['openWorldHint']
    floor, baked = navigation_floor()
    assert floor and baked['asset']
    edit_settings('forge_project_settings_game_defaults')
    edit_settings('forge_project_settings_save')
    ok('forge_document_save')
    authored = read('forge_scene_read')
    ok('forge_play_start')
    until(lambda: read('forge_play_status'), lambda s: s['ready'] and s['control_ready'])
    control('forge_play_pause')
    before = until(lambda: read('forge_play_status'), lambda s: s['paused'] and s['control_ready'])
    if not before['sdk']:
        until(lambda: read('forge_play_status'), lambda s: s['recovery_ready'])
    tick = before['timing']['tick']
    control('forge_play_navigation_rebuild', operation='start', asset=baked['asset'])
    observed = until(lambda: read('forge_play_navigation'),
                     lambda s: (s.get('value') or {}).get('phase') in
                     ('Applied', 'Cancelled', 'Stale', 'Failed'))
    result = observed['value']
    assert result['phase'] == 'Applied' and not result['diagnostic'], observed
    assert result['asset'] == baked['asset'] and len(result['geometry_revision']) == 64
    assert result['query_generation'] > 0
    after = read('forge_play_status')
    assert after['paused'] and after['timing']['tick'] == tick, after
    assert not after['recovery_ready'], after
    assert read('forge_scene_read') == authored
    bad = mutate('forge_play_navigation_rebuild', launch=after['launch'], session=after['session'],
                 generation=after['generation'] + 1, operation='cancel', token=result['token'])
    assert not bad['ok'] and bad['error']['code'] == 'stale_play', bad
    bad = mutate('forge_play_navigation_rebuild', launch=after['launch'], session=after['session'],
                 generation=after['generation'], operation='start', asset=baked['asset'],
                 token=result['token'])
    assert not bad['ok'] and bad['error']['code'] == 'invalid_request', bad
    control('forge_play_navigation_rebuild', operation='start', asset=str(uuid.uuid4()))
    failed = until(lambda: read('forge_play_status'),
                   lambda s: bool(s['navigation_rebuild_error']) and not s['navigation_rebuild_pending'])
    assert failed['active'] and failed['paused'] and failed['timing']['tick'] == tick, failed
    control('forge_play_navigation_rebuild', operation='start', asset=baked['asset'])
    next_state = until(lambda: read('forge_play_navigation'),
                       lambda s: (s.get('value') or {}).get('token') != result['token'] and
                       (s.get('value') or {}).get('phase') == 'Applied')
    assert not next_state['error'], next_state
    control('forge_play_stop')
    until(lambda: read('forge_play_status'), lambda s: not s['active'])
    assert read('forge_scene_read') == authored
