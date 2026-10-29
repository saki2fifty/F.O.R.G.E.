"""Actual MCP and editor capture/import owners; only HDR GPU delivery is recorded."""
from contextlib import contextmanager
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import time
from mcp_test_client import Client, failed, finish_host, test_symlink, write_capture

TOOLS, HOST, WORKER = sys.argv[1:4]


@contextmanager
def editor(mode='assets'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-reflection-capture-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), WORKER, mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, json.loads(line)
        finally:
            finish_host(process, root)


def until(fn, predicate):
    end = time.monotonic() + 40
    while time.monotonic() < end:
        value = fn()
        if predicate(value):
            return value
        time.sleep(.01)
    raise AssertionError(value)


for legacy in (False, True):
    with editor() as (root, config), Client(config, legacy) as client:
        advertised = {tool['name']: tool for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_capture_reflection_start' in advertised
        assert 'forge_asset_import_commit' in advertised and 'forge_capture_cancel' in advertised
        assert 'forge_capture_start' not in advertised  # No external-output grant needed.
        assert not advertised['forge_capture_reflection_start']['annotations']['readOnlyHint']

        def discovery():
            value = client.tool('forge_discover')
            assert value['ok'], value
            return value

        def read(tool, **extra):
            value = client.tool(tool, target=discovery()['result']['target'], **extra)
            assert value['ok'], value
            return value['result']

        def arguments(**extra):
            value = discovery()
            return dict(target=value['result']['target'], expected_revision=value['revision'],
                        sequence=value['next_sequence'], **extra)

        def mutate(tool, **extra):
            return client.tool(tool, **arguments(**extra))

        def ok(tool, **extra):
            value = mutate(tool, **extra)
            assert value['ok'], value
            return value['result']

        probe = ok('forge_entity_create', arguments=dict(recipe='render.reflection_probe'))['results'][0]['selected']
        ok('forge_property_set', arguments=dict(entity=probe, component='forge.reflection_probe',
                                              field='rotation_degrees', value=45))
        authored = read('forge_scene_read')
        for extra in (dict(size=15), dict(size=1025), dict(near_plane=0),
                      dict(near_plane=2, far_plane=1), dict(destination='../Escape'),
                      dict(destination='.forge/Reflection'), dict(destination='Assets')):
            request = dict(probe=probe, destination='Assets/Rejected', size=16)
            request.update(extra)
            assert not mutate('forge_capture_reflection_start', **request)['ok']
        assert read('forge_scene_read') == authored
        (root / 'hold_pixels').touch()
        request = arguments(probe=probe, destination='Assets/Reflection', size=16)
        admitted = client.tool('forge_capture_reflection_start', **request)
        assert admitted['ok'], admitted
        assert client.tool('forge_capture_reflection_start', **request) == admitted
        operation = admitted['result']['operation']
        failed(mutate('forge_capture_cancel', operation='11111111-1111-4111-8111-111111111111'), 'wrong_job')
        failed(mutate('forge_capture_reflection_start', probe=probe, destination='Assets/Parallel', size=16), 'capture_busy')
        ok('forge_capture_cancel', operation=operation)
        until(lambda: read('forge_capture_status'), lambda row: not row['busy'])
        assert not (root / 'Game/Assets/Reflection').exists()
        (root / 'hold_pixels').unlink()

        ok('forge_capture_reflection_start', probe=probe, destination='Assets/Reflection', size=16)
        progress = until(lambda: read('forge_capture_status'), lambda row: 'import_batch' in row['reflection'])
        review = until(lambda: read('forge_asset_import_review'), lambda row: row['state'] == 'review')
        assert progress['reflection']['import_batch'] == review['batch']
        assert review['generated']['format'] == 'forge.reflection-capture'
        assert not (root / 'Game/Assets/Reflection').exists() and read('forge_scene_read') == authored
        commit = arguments(batch=review['batch'], review=review['review'], **{'import': True})
        accepted = client.tool('forge_asset_import_commit', **commit)
        assert accepted['ok'] and client.tool('forge_asset_import_commit', **commit) == accepted
        complete = until(lambda: read('forge_capture_status'), lambda row: not row['busy'])
        assert complete['reflection']['state'] == 'assigned', complete
        assigned = read('forge_scene_read')
        entity = next(row for row in assigned['entities'] if row['id'] == probe)
        assert entity['components']['forge.reflection_probe']['texture'] == complete['reflection']['asset']
        assert entity['components']['forge.reflection_probe']['rotation_degrees'] == 0
        ok('forge_history_undo')
        assert read('forge_scene_read') == authored
        ok('forge_history_redo')
        assert read('forge_scene_read') == assigned
        ok('forge_document_save')
        assert (root / 'Game/Assets/Reflection/Reflection.dds').read_bytes()[:4] == b'DDS '
        ok('forge_asset_import_close', batch=review['batch'])
        assert not mutate('forge_capture_reflection_start', probe=probe, destination='Assets/Reflection', size=16)['ok']

        ok('forge_capture_reflection_start', probe=probe, destination='Assets/ChangedScene', size=16)
        review = until(lambda: read('forge_asset_import_review'), lambda row: row['state'] == 'review' and row['batch'] > progress['reflection']['import_batch'])
        ok('forge_property_set', arguments=dict(entity=probe, component='forge.reflection_probe', field='priority', value=9))
        edited = read('forge_scene_read')
        ok('forge_asset_import_commit', batch=review['batch'], review=review['review'], **{'import': True})
        complete = until(lambda: read('forge_capture_status'), lambda row: not row['busy'])
        assert complete['reflection']['state'] == 'unassigned' and 'asset' in complete['reflection'], complete
        assert read('forge_scene_read') == edited
        ok('forge_asset_import_close', batch=review['batch'])
        ok('forge_capture_reflection_start', probe=probe, destination='Assets/SourceOnly', size=16)
        review = until(lambda: read('forge_asset_import_review'), lambda row: row['state'] == 'review' and row['destination'] == 'Assets/SourceOnly')
        ok('forge_asset_import_commit', batch=review['batch'], review=review['review'], **{'import': False})
        complete = until(lambda: read('forge_capture_status'), lambda row: not row['busy'])
        assert complete['reflection']['state'] == 'source_only' and not complete['error'], complete
        assert (root / 'Game/Assets/SourceOnly/Reflection.dds').is_file()
        assert read('forge_scene_read') == edited
        ok('forge_asset_import_close', batch=review['batch'])
        with test_symlink(root / 'Game/Assets/Escape', root.parent, directory=True) as linked:
            if linked:
                assert not mutate('forge_capture_reflection_start', probe=probe, destination='Assets/Escape/Map', size=16)['ok']
        assert read('forge_scene_read') == edited
        print(('legacy' if legacy else 'current') + ': reflection capture/import MCP scope, replay, cancellation, assignment/history/save PASS; GPU delivery recorded')
    with editor('read') as (_, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_capture_status' in names and 'forge_capture_reflection_start' not in names
        assert 'forge_capture_cancel' not in names and 'forge_asset_import_commit' not in names

if len(sys.argv) > 4:
    write_capture(sys.argv[4])
