"""Shared supervised C++ analysis/source owners over both MCP envelopes."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import Client, finish_host, protocol_cases, write_capture

TOOLS, HOST, PROVIDER = sys.argv[1:4]
for legacy in protocol_cases():
    for mode in ('read', 'write', 'analysis'):
        with tempfile.TemporaryDirectory(prefix='forge-code-analysis-') as temporary:
            root = Path(temporary) / 'Project'
            process = subprocess.Popen([HOST, str(root), str(root / 'UnusedSdk'),
                                        PROVIDER, 'unused', mode], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, encoding='utf-8')
            try:
                config = json.loads(process.stdout.readline())
                native = root / 'Native'
                native.mkdir(exist_ok=True)
                original = '// saved source\n'
                (native / 'Spinner.cpp').write_text(original)
                draft = '// café\nstruct Spinner { float degrees_per_second; };\nvoid tick() { Spinner s; s.de }\n'
                with Client(config, legacy) as client:
                    def discovery():
                        return client.tool('forge_discover')

                    target = discovery()['result']['target']

                    def read(tool, **args):
                        reply = client.tool(tool, target=target, **args)
                        assert reply['ok'], reply
                        return reply['result']

                    def mutate(tool, **args):
                        state = discovery()
                        assert state['result']['target'] == target, state
                        return client.tool(tool, target=state['result']['target'],
                                           expected_revision=state['revision'],
                                           sequence=state['next_sequence'], **args)

                    def observed_args():
                        source = read('forge_code_read', path='Native/Spinner.cpp')
                        return {key: source[key] for key in ('path', 'session', 'state')}

                    def settle():
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            status = read('forge_code_analysis_status', limit=1)
                            if not status['busy']:
                                return status
                            time.sleep(.01)
                        raise AssertionError('Analysis did not settle')

                    tools = {tool['name']: tool for tool in client.request('tools/list')['result']['tools']}
                    for name in ('status', 'log'):
                        assert tools['forge_code_analysis_' + name]['annotations']['readOnlyHint']
                        assert not tools['forge_code_analysis_' + name]['annotations']['openWorldHint']
                    assert ('forge_code_analysis_use' in tools) == (mode != 'read')
                    assert ('forge_code_analysis_start' in tools) == (mode == 'analysis')
                    assert ('forge_code_analysis_cancel' in tools) == (mode == 'analysis')
                    if mode != 'analysis':
                        assert read('forge_code_analysis_status')['operation'] is None
                        continue
                    assert tools['forge_code_analysis_start']['annotations']['openWorldHint']
                    assert {'signature', 'diagnostics'}.issubset(tools['forge_code_analysis_start']['inputSchema']['properties']['kind']['enum'])
                    assert not tools['forge_code_analysis_use']['annotations']['openWorldHint']
                    assert mutate('forge_code_open', path='Native/Spinner.cpp')['ok']
                    assert mutate('forge_code_replace', **observed_args(), text=draft)['ok']
                    args = observed_args() | dict(kind='completion', offset=len(draft[:draft.rfind('s.de')+4].encode()))
                    route = discovery()
                    stale = client.tool('forge_code_analysis_start', target=route['result']['target'],
                                        expected_revision=route['revision'] - 1,
                                        sequence=route['next_sequence'], **args)
                    assert not stale['ok'], stale
                    started = mutate('forge_code_analysis_start', **args)
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['total'] == 1, status
                    assert status['items'][0]['available']
                    before = discovery()
                    bad = mutate('forge_code_analysis_use', operation=status['operation'], index=64)
                    assert not bad['ok'], bad
                    route = discovery()
                    use_args = dict(target=route['result']['target'], expected_revision=route['revision'],
                                    sequence=route['next_sequence'], operation=status['operation'], index=0)
                    used = client.tool('forge_code_analysis_use', **use_args)
                    assert used['ok'], used
                    replayed = client.tool('forge_code_analysis_use', **use_args)
                    assert replayed == used, (used, replayed)
                    completed = draft.replace('s.de }', 's.degrees_per_second }')
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == completed
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='declaration', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'], status
                    assert mutate('forge_code_analysis_use', operation=status['operation'], index=0)['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['cursor']['line'] == 2
                    before_signature = read('forge_code_read', path='Native/Spinner.cpp')
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='signature', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['readonly'] and status['total'] == 2, status
                    assert status['active_signature'] == 0 and status['active_parameter'] == 1, status
                    assert status['items'][0]['parameters'][1] == dict(label='float 𐐀rate', documentation='Rate to apply.')
                    other = read('forge_code_analysis_status', offset=1, limit=1)
                    assert other['items'][0]['index'] == 1 and other['active_signature'] == 0
                    refused = mutate('forge_code_analysis_use', operation=status['operation'], index=0)
                    assert not refused['ok'] and 'read-only' in str(refused), refused
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_signature
                    diagnostic_before = read('forge_code_read', path='Native/Spinner.cpp')
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='diagnostics', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['total'] == 1, status
                    diagnostic = status['items'][0]
                    assert diagnostic['severity'] == 'Warning' and diagnostic['code'] == 'fixture-warning'
                    assert status['occurred_at'] and diagnostic['path'] == 'Native/Spinner.cpp'
                    assert read('forge_code_read', path='Native/Spinner.cpp') == diagnostic_before
                    assert mutate('forge_code_analysis_use', operation=status['operation'], index=0)['ok']
                    source = read('forge_code_read', path='Native/Spinner.cpp')
                    assert source['text'] == draft and source['cursor']['line'] == diagnostic['line']
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='completion', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert mutate('forge_code_replace', **observed_args(), text=draft+'// changed\n')['ok']
                    refused = mutate('forge_code_analysis_use', operation=status['operation'], index=0)
                    assert not refused['ok'], refused
                    assert read('forge_code_analysis_log', limit=4096)['text']
                    assert (native / 'Spinner.cpp').read_text() == original
                finish_host(process, root)
            finally:
                if process.poll() is None:
                    finish_host(process, root)
write_capture(sys.argv[4])
print('C++ analysis permissions, revisions, receipt replay, completion Undo, stale source and declaration: both MCP envelopes passed')
