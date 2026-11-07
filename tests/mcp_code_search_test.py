"""Actual shared source-search jobs and navigation over both MCP framings."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import Client, failed, finish_host, write_capture

TOOLS, HOST = sys.argv[1:3]

for legacy in (False, True):
    for mode in ('read', 'write'):
        with tempfile.TemporaryDirectory(prefix='forge-code-search-') as temporary:
            root = Path(temporary) / 'Project'
            process = subprocess.Popen([HOST, str(root), str(root / 'UnusedSdk'),
                                        'cmake', 'ninja', mode], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, encoding='utf-8')
            try:
                config = json.loads(process.stdout.readline())
                native = root / 'Native'
                native.mkdir(exist_ok=True)
                text = '// café\r\n\tfloat speed = 2;\r\nspeed Speed speeds\r\n'
                (native / 'Rotator.hpp').write_bytes(text.encode())
                (native / 'Other.cpp').write_text('float unrelated;\n')
                (native / 'Builds').mkdir()
                (native / 'Builds/Hidden.cpp').write_text('speed\n')
                scene_before = {p: p.read_bytes() for p in (root / 'Scenes').glob('*') if p.is_file()}
                with Client(config, legacy) as client:
                    def discovery():
                        return client.tool('forge_discover')

                    def read(tool, **args):
                        reply = client.tool(tool, target=discovery()['result']['target'], **args)
                        assert reply['ok'], reply
                        return reply['result']

                    def mutate(tool, **args):
                        state = discovery()
                        return client.tool(tool, target=state['result']['target'],
                                           expected_revision=state['revision'],
                                           sequence=state['next_sequence'], **args)

                    def settle():
                        deadline = time.monotonic() + 20
                        while time.monotonic() < deadline:
                            status = read('forge_code_search_status', offset=0, limit=1)
                            if not status['busy']:
                                return status
                            time.sleep(.005)
                        raise AssertionError('Source-search timeout')

                    tools = {t['name']: t for t in client.request('tools/list')['result']['tools']}
                    for name in ('start', 'status', 'cancel'):
                        tool = tools['forge_code_search_' + name]
                        assert tool['annotations']['readOnlyHint']
                        assert not tool['annotations']['openWorldHint']
                        assert tool['annotations']['idempotentHint'] == (name != 'start')
                    before = discovery()
                    started = read('forge_code_search_start', query='speed', whole_word=True)
                    status = settle()
                    assert status['complete'] and status['total'] == 2, status
                    assert status['matches'][0]['line'] == 2
                    assert status['matches'][0]['column'] == 11
                    assert status['matches'][0]['path'] == 'Native/Rotator.hpp'
                    assert not status['matches'][0]['open']
                    second = read('forge_code_search_status', offset=1, limit=1)['matches'][0]
                    assert second['line'] == 3 and second['column'] == 1
                    after = discovery()
                    assert before['revision'] == after['revision']
                    assert before['next_sequence'] == after['next_sequence']
                    invalid = client.tool('forge_code_search_start', target=after['result']['target'],
                                          query='one\ntwo')
                    assert not invalid['ok'], invalid
                    invalid = client.tool('forge_code_search_start', target=after['result']['target'],
                                          query='speed', root='../')
                    assert not invalid['ok'], invalid
                    failed(client.tool('forge_code_search_cancel', target=after['result']['target'],
                                       operation='11111111-1111-4111-8111-111111111111'), 'wrong_job')
                    if mode == 'write':
                        match = status['matches'][0]
                        opened = mutate('forge_code_open', **{k: match[k] for k in ('path', 'state', 'line', 'column')})
                        assert opened['ok'], opened
                        assert opened['result']['cursor'] == {'line': 2, 'column': 11}, opened
                        current = read('forge_code_read', path=match['path'])
                        draft = '// draft\nfloat speed = 8;\n'
                        changed = mutate('forge_code_replace', path=match['path'], session=current['session'],
                                         state=current['state'], text=draft)
                        assert changed['ok'], changed
                        failed(mutate('forge_code_open', path=match['path'], state=match['state']), 'stale_source')
                        read('forge_code_search_start', query='speed')
                        status = settle()
                        assert status['total'] == 1 and status['matches'][0]['open'], status
                        assert status['matches'][0]['excerpt'] == 'float speed = 8;'
                        assert (native / 'Rotator.hpp').read_bytes() == text.encode()
                        current = read('forge_code_read', path=match['path'])
                        assert mutate('forge_code_undo', path=match['path'], session=current['session'],
                                      state=current['state'])['ok']
                    else:
                        assert 'forge_code_open' not in tools
                    read('forge_code_search_start', query='SPEED', match_case=False, whole_word=True)
                    assert settle()['total'] == 3
                    cancelled = read('forge_code_search_start', query='speed')
                    read('forge_code_search_cancel', operation=cancelled['operation'])
                    status = settle()
                    # Cancellation can arrive after the tiny task was observed complete.
                    assert not status['busy']
                    assert {p: p.read_bytes() for p in scene_before} == scene_before
                finish_host(process, root)
            finally:
                if process.poll() is None:
                    finish_host(process)

write_capture(sys.argv[3])
print('Project source/draft search, permissions, stale navigation and clean shutdown: both MCP framings passed')
