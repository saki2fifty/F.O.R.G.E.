"""Real source/build owners over both MCP protocols; no substitute compiler/kit."""
from contextlib import contextmanager
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed, finish_host

TOOLS, HOST, SDK, CMAKE, NINJA = sys.argv[1:6]


@contextmanager
def editor(mode):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-code-') as temporary:
        root = Path(temporary) / 'Game'
        process = subprocess.Popen([HOST, str(root), SDK, CMAKE, NINJA, mode],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, json.loads(line)
        finally:
            finish_host(process)


def session(client):
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

    def source(tool, path='Native/Components/Rotator.hpp', **extra):
        value = read('forge_code_read', path=path)
        return mutate(tool, path=path, session=value['session'], state=value['state'], **extra)

    def settle(tool='forge_code_status', timeout=60):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            value = read(tool)
            if not value['busy']:
                return value
            time.sleep(.05)
        raise AssertionError(('native worker timed out', read('forge_code_build_log')))

    return read, arguments, mutate, source, settle


for legacy in (False, True):
    with editor('execute') as (root, config), Client(config, legacy) as client:
        read, arguments, mutate, source, settle = session(client)
        advertised = {tool['name']: tool for tool in client.request('tools/list')['result']['tools']}
        catalog = set(advertised)
        assert advertised['forge_code_build']['annotations']['openWorldHint']
        assert not advertised['forge_code_api_read']['annotations']['openWorldHint']
        assert {'forge_code_setup', 'forge_code_replace', 'forge_code_build',
                'forge_code_inspect', 'forge_code_migration_prepare'} <= catalog
        assert not read('forge_code_status')['managed']
        api = read('forge_code_api_list')
        assert api['total'] > 0
        assert read('forge_code_api_read', path='include/forge/native_sdk.h')['total_bytes'] > 0
        denied = client.tool('forge_code_api_read', target=client.tool('forge_discover')['result']['target'],
                             path='../../src/editor/main.cpp')
        assert not denied['ok'], denied
        assert mutate('forge_code_setup')['ok']
        assert not mutate('forge_code_setup')['ok']  # Existing sources stay intact.
        assert mutate('forge_code_create', kind='component', name='Rotator')['ok']
        assert mutate('forge_code_create', kind='system', name='Spin', component='Rotator')['ok']
        assert not mutate('forge_code_create', kind='header', name='Invalid.hpp', component='Rotator')['ok']
        assert not (root / 'Native/Invalid.hpp').exists()
        state = read('forge_code_read', path='Native/Components/Rotator.hpp')
        original = state['text']
        assert 'float speed = 90.0f' in original
        replacement = original.replace('90.0f', '123.0f').replace('"speed":90', '"speed":123')
        replay = arguments(path=state['path'], session=state['session'], state=state['state'], text=replacement)
        changed = client.tool('forge_code_replace', **replay)
        assert changed['ok'] and changed['result']['dirty'], changed
        assert client.tool('forge_code_replace', **replay) == changed
        stale = mutate('forge_code_replace', path=state['path'], session=state['session'],
                       state=state['state'], text=original)
        failed(stale, 'stale_source')
        assert source('forge_code_undo')['ok']
        assert read('forge_code_read', path=state['path'])['text'] == original
        assert source('forge_code_redo')['ok']
        assert source('forge_code_save')['ok']
        assert (root / state['path']).read_text() == replacement
        old_session = read('forge_code_read', path=state['path'])['session']
        assert source('forge_code_close')['ok']
        assert mutate('forge_code_open', path=state['path'])['ok']
        assert read('forge_code_read', path=state['path'])['session'] != old_session
        assert not mutate('forge_code_open', path='../Outside.cpp')['ok']
        started = mutate('forge_code_compiler_test')
        assert started['ok'] and started['result']['busy'], started
        failed(mutate('forge_code_build_cancel', operation='11111111-1111-4111-8111-111111111111'), 'wrong_job')
        assert mutate('forge_code_build_cancel', operation=started['result']['operation'])['ok']
        cancelled = settle()
        assert cancelled['error'] and not cancelled['compiler_ready'], cancelled
        before = (root / 'forge.project.json').read_bytes()
        assert mutate('forge_code_build')['ok']
        built = settle()
        log = read('forge_code_build_log')['text']
        if os.name == 'nt':
            assert not built['error'] and built['compiler_ready'], (built, log)
            assert mutate('forge_code_inspect')['ok']
            inspected = settle('forge_code_schemas')
            assert not inspected['error'] and inspected['ready'], inspected
            assert 'project.rotator' in json.dumps(inspected['schemas'])
            good = (root / 'forge.project.json').read_bytes()
            assert source('forge_code_replace', text='#error deliberate MCP invalid candidate\n')['ok']
            assert source('forge_code_save')['ok']
            assert mutate('forge_code_build')['ok']
            bad = settle()
            assert bad['error'] and (root / 'forge.project.json').read_bytes() == good
        else:
            # The product deliberately supports deployment kits only on Windows.
            # Prove real compilation reached the existing platform rejection;
            # do not fabricate a successful kit/admission on this platform.
            assert built['error'] and 'deployment kits currently target Windows' in log, (built, log)
            assert (root / 'forge.project.json').read_bytes() == before
        assert read('forge_code_build_log')['revision'] > 0

    for mode in ('code', 'read'):
        with editor(mode) as (root, config), Client(config, legacy) as client:
            read, arguments, mutate, source, settle = session(client)
            names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
            assert 'forge_code_read' in names and 'forge_code_build' not in names
            assert 'forge_code_inspect' not in names
            assert ('forge_code_replace' in names) == (mode == 'code')
            denied = client.request('tools/call', dict(name='forge_code_build', arguments=arguments()))
            assert denied.get('error', {}).get('code') == -32602, denied
            if mode == 'code':
                assert mutate('forge_code_setup')['ok']
                assert mutate('forge_code_create', kind='component', name='Rotator')['ok']
                state = read('forge_code_read', path='Native/Components/Rotator.hpp')
                assert source('forge_code_replace', text=state['text'] + '// agent edit\n')['ok']
                (root / state['path']).write_text('// external owner\n')
                assert not source('forge_code_save')['ok']
                assert read('forge_code_read', path=state['path'])['dirty']
                assert not read('forge_code_status')['busy']
                assert source('forge_code_close', discard=True)['ok']

with gzip.open(sys.argv[6], 'wt', encoding='utf-8') as output:
    json.dump(CAPTURE, output, separators=(',', ':'))
print('Actual C++ widget/build owners, replay, grants, compiler cancellation and platform limits verified')
