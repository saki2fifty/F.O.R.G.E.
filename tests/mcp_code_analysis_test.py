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
                    assert ('forge_code_analysis_diagnostic_move' in tools) == (mode != 'read')
                    if mode != 'read':
                        diagnostic_tool = tools['forge_code_analysis_diagnostic_move']
                        assert not diagnostic_tool['annotations']['readOnlyHint']
                        assert diagnostic_tool['inputSchema']['properties']['direction']['enum'] == ['next', 'previous']
                    assert tools['forge_code_navigation_status']['annotations']['readOnlyHint']
                    assert ('forge_code_navigation_move' in tools) == (mode != 'read')
                    assert ('forge_code_navigation_clear' in tools) == (mode != 'read')
                    assert ('forge_code_analysis_start' in tools) == (mode == 'analysis')
                    assert ('forge_code_analysis_cancel' in tools) == (mode == 'analysis')
                    for name in ('forge_code_find', 'forge_code_replace_matches'):
                        assert (name in tools) == (mode != 'read')
                        if mode == 'read':
                            refused = client.request('tools/call', dict(name=name, arguments={}))
                            assert refused['error']['code'] == -32602 and 'not available' in refused['error']['message'], refused
                    # Exercise literal editing once per envelope without execution permission.
                    # The analysis role retains the same code.write tools and its query suite.
                    if mode == 'write':
                        assert mutate('forge_code_open', path='Native/Spinner.cpp')['ok']
                        literal_source = '// café\n\tfloat speed = 1; // speed\n'
                        assert mutate('forge_code_replace', **observed_args(), text=literal_source)['ok']
                        found = mutate('forge_code_find', **observed_args(), query='speed', direction='next')
                        assert found['ok'] and found['result']['found'] and found['result']['source']['selection']['bytes'] == 5, found
                        changed = mutate('forge_code_replace_matches', **observed_args(), query='speed', replacement='rate', mode='next')
                        assert changed['ok'] and changed['result']['matches'] == 1 and changed['result']['changed'], changed
                        assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == literal_source.replace('speed', 'rate', 1)
                        assert mutate('forge_code_undo', **observed_args())['ok']
                        assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == literal_source
                        route = discovery()
                        request = dict(target=route['result']['target'], expected_revision=route['revision'],
                                       sequence=route['next_sequence'], **observed_args(), query='speed',
                                       replacement='vitesse_é', mode='all')
                        changed = client.tool('forge_code_replace_matches', **request)
                        assert changed['ok'] and changed['result']['matches'] == 2 and changed['result']['changed'], changed
                        assert client.tool('forge_code_replace_matches', **request) == changed
                        assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == literal_source.replace('speed', 'vitesse_é')
                        assert mutate('forge_code_undo', **observed_args())['ok']
                        before_literal_noop = read('forge_code_read', path='Native/Spinner.cpp')
                        for query, replacement in [('speed', 'speed'), ('absent', '')]:
                            value = mutate('forge_code_replace_matches', **observed_args(), query=query, replacement=replacement, mode='all')
                            assert value['ok'] and not value['result']['changed'], value
                            assert read('forge_code_read', path='Native/Spinner.cpp') == before_literal_noop
                        rejected = mutate('forge_code_replace_matches', **(observed_args() | dict(state='0' * 64)),
                                          query='speed', replacement='rate', mode='all')
                        assert not rejected['ok'] and read('forge_code_read', path='Native/Spinner.cpp') == before_literal_noop
                        for invalid in (dict(query=''), dict(query='x' * 256), dict(query='a\nb'),
                                        dict(replacement='\r\n'), dict(replacement='a\0b'),
                                        dict(replacement='x' * 4097), dict(mode='unknown'),
                                        dict(session='00000000-0000-0000-0000-000000000000')):
                            rejected = mutate('forge_code_replace_matches', **(observed_args() | dict(
                                query='speed', replacement='rate', mode='all') | invalid))
                            assert not rejected['ok'] and read('forge_code_read', path='Native/Spinner.cpp') == before_literal_noop, rejected
                        (native / 'Other.hpp').write_text('// other source\n', encoding='utf-8')
                        old_source = observed_args()
                        assert mutate('forge_code_open', path='Native/Other.hpp')['ok']
                        for name, args in [('forge_code_find', dict(direction='next')),
                                           ('forge_code_replace_matches', dict(mode='all', replacement='rate'))]:
                            rejected = mutate(name, **old_source, query='speed', **args)
                            assert not rejected['ok'] and 'stale_source' in str(rejected), rejected
                        assert mutate('forge_code_open', path='Native/Spinner.cpp')['ok']
                        assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == literal_source
                        assert (native / 'Spinner.cpp').read_bytes() == original.encode('utf-8')
                        assert read('forge_code_analysis_status')['operation'] is None, 'Literal editing launched a provider query'
                    if mode != 'analysis':
                        assert read('forge_code_analysis_status')['operation'] is None
                        continue
                    assert tools['forge_code_analysis_start']['annotations']['openWorldHint']
                    assert {'signature', 'diagnostics', 'hover', 'symbols', 'formatSelection'}.issubset(tools['forge_code_analysis_start']['inputSchema']['properties']['kind']['enum'])
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
                    for kind in ('references', 'typeDefinition', 'definition', 'implementation'):
                        before_navigation = read('forge_code_read', path='Native/Spinner.cpp')
                        started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind=kind, offset=args['offset'])))
                        assert started['ok'], started
                        status = settle()
                        assert status['complete'] and status['total'] == 1, status
                        if kind in ('references', 'implementation'):
                            assert status['scope'] == 'current_context' and status['exhaustive'] is False, status
                        assert read('forge_code_read', path='Native/Spinner.cpp') == before_navigation
                        assert mutate('forge_code_analysis_use', operation=status['operation'], index=0)['ok']
                        after_navigation = read('forge_code_read', path='Native/Spinner.cpp')
                        assert after_navigation['text'] == draft and after_navigation['can_redo'] == before_navigation['can_redo']
                    before_selection = read('forge_code_read', path='Native/Spinner.cpp')
                    assert mutate('forge_code_analysis_start', **(observed_args() | dict(kind='selection', offset=args['offset'])))['ok']
                    selection = settle()
                    assert selection['complete'] and selection['total'] == 2, selection
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_selection
                    assert mutate('forge_code_analysis_use', operation=selection['operation'], index=0)['ok']
                    selected = read('forge_code_read', path='Native/Spinner.cpp')
                    assert selected['text'] == draft and selected['selection']['active'] and selected['selection']['bytes'] > 0
                    assert selected['can_undo'] == before_selection['can_undo'] and selected['can_redo'] == before_selection['can_redo']
                    range_begin = len(draft[:draft.index('float ')].encode())
                    range_end = len(draft[:draft.index('};')].encode())
                    range_args = observed_args() | dict(kind='formatSelection', offset=range_begin, end_offset=range_end)
                    before_range = read('forge_code_read', path='Native/Spinner.cpp')
                    before_operation = read('forge_code_analysis_status')['operation']
                    for invalid in (dict(end_offset=range_begin), dict(end_offset=len(draft.encode())+1),
                                    dict(kind='format'), dict(offset=range_end), dict(end_offset=-1)):
                        route = discovery()
                        rejected_range = client.tool('forge_code_analysis_start', target=route['result']['target'],
                            expected_revision=route['revision'], sequence=route['next_sequence'],
                            **(range_args | invalid))
                        assert not rejected_range['ok'], rejected_range
                        assert read('forge_code_analysis_status')['operation'] == before_operation
                        assert read('forge_code_read', path='Native/Spinner.cpp') == before_range
                    assert mutate('forge_code_analysis_start', **range_args)['ok']
                    range_result = settle()
                    assert range_result['complete'] and range_result['total'] == 1, range_result
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_range
                    range_proposal = range_result['items'][0]['text']
                    route = discovery()
                    use_range = dict(target=route['result']['target'], expected_revision=route['revision'],
                        sequence=route['next_sequence'], operation=range_result['operation'], index=0)
                    used_range = client.tool('forge_code_analysis_use', **use_range)
                    assert used_range['ok'] and client.tool('forge_code_analysis_use', **use_range) == used_range
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == range_proposal
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    assert mutate('forge_code_redo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == range_proposal
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    navigation_source = draft + '\nint diagnostic_probe() { int value = 1 return value; }\n'
                    assert mutate('forge_code_replace', **(observed_args() | dict(text=navigation_source)))['ok']
                    assert mutate('forge_code_open', path='Native/Spinner.cpp',
                                  line=navigation_source.count('\n') + 1, column=1)['ok']
                    assert mutate('forge_code_analysis_start', **(observed_args() | dict(kind='diagnostics', offset=0)))['ok']
                    diagnostic_result = settle()
                    assert diagnostic_result['complete'] and diagnostic_result['total'] == 2, diagnostic_result
                    def diagnostic_cursor(offset):
                        start = navigation_source.rfind('\n', 0, offset) + 1
                        return dict(line=navigation_source.count('\n', 0, offset) + 1,
                                    column=len(navigation_source[start:offset].expandtabs(4)) + 1)
                    first_cursor = diagnostic_cursor(navigation_source.index('degrees_per_second'))
                    last_cursor = diagnostic_cursor(navigation_source.index('1 return') + 2)
                    route = discovery()
                    request = dict(target=route['result']['target'], expected_revision=route['revision'],
                                   sequence=route['next_sequence'], **observed_args(),
                                   operation=diagnostic_result['operation'], direction='next')
                    jumped = client.tool('forge_code_analysis_diagnostic_move', **request)
                    assert jumped['ok'] and jumped['result']['cursor'] == first_cursor, jumped
                    assert client.tool('forge_code_analysis_diagnostic_move', **request) == jumped
                    assert read('forge_code_read', path='Native/Spinner.cpp')['cursor'] == first_cursor
                    for direction, expected in [('next', last_cursor), ('next', first_cursor),
                                                ('previous', last_cursor), ('previous', first_cursor)]:
                        value = mutate('forge_code_analysis_diagnostic_move', **observed_args(),
                                       operation=diagnostic_result['operation'], direction=direction)
                        assert value['ok'] and value['result']['cursor'] == expected, value
                        assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == navigation_source
                    before_bad_navigation = read('forge_code_read', path='Native/Spinner.cpp')
                    for extra in [dict(direction='bad'), dict(operation='00000000-0000-0000-0000-000000000000'),
                                  dict(state='0' * 64)]:
                        arguments = observed_args() | dict(operation=diagnostic_result['operation'], direction='next') | extra
                        assert not mutate('forge_code_analysis_diagnostic_move', **arguments)['ok']
                        assert read('forge_code_read', path='Native/Spinner.cpp') == before_bad_navigation
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    after_navigation_undo = read('forge_code_read', path='Native/Spinner.cpp')
                    assert not mutate('forge_code_analysis_diagnostic_move', **observed_args(),
                                      operation=diagnostic_result['operation'], direction='next')['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp') == after_navigation_undo
                    rename_draft = draft + '\nfloat rename_probe(float rename_rate) { return rename_rate + rename_rate; }\n'
                    assert mutate('forge_code_replace', **(observed_args() | dict(text=rename_draft)))['ok']
                    before_rename = read('forge_code_read', path='Native/Spinner.cpp')
                    rename_args = observed_args() | dict(kind='rename', offset=len(rename_draft[:rename_draft.index('rename_rate')].encode()), new_name='angular_speed')
                    before_name_validation = read('forge_code_analysis_status')['operation']
                    for invalid_name in ('', 'x' * 257, 42):
                        route = discovery()
                        rejected_name = client.tool('forge_code_analysis_start', target=route['result']['target'],
                            expected_revision=route['revision'], sequence=route['next_sequence'],
                            **(rename_args | dict(new_name=invalid_name)))
                        assert not rejected_name['ok'], rejected_name
                        assert read('forge_code_analysis_status')['operation'] == before_name_validation
                        assert read('forge_code_read', path='Native/Spinner.cpp') == before_rename
                    route = discovery()
                    wrong_kind = client.tool('forge_code_analysis_start', target=route['result']['target'],
                        expected_revision=route['revision'], sequence=route['next_sequence'],
                        **(rename_args | dict(kind='completion')))
                    assert not wrong_kind['ok'], wrong_kind
                    assert read('forge_code_analysis_status')['operation'] == before_name_validation
                    assert mutate('forge_code_analysis_start', **rename_args)['ok']
                    rename = settle()
                    assert rename['complete'] and rename['total'] == 1 and rename['new_name'] == 'angular_speed', rename
                    assert rename['scope'] == 'current_context' and rename['exhaustive'] is False and 'source_edits' not in rename
                    expected_rename = draft + '\nfloat rename_probe(float angular_speed) { return angular_speed + angular_speed; }\n'
                    assert rename['items'][0]['text'] == expected_rename and rename['items'][0]['edit_count'] == 3
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_rename
                    route = discovery()
                    apply_rename = dict(target=route['result']['target'], expected_revision=route['revision'],
                                        sequence=route['next_sequence'], operation=rename['operation'], index=0)
                    applied = client.tool('forge_code_analysis_use', **apply_rename)
                    assert applied['ok'] and client.tool('forge_code_analysis_use', **apply_rename) == applied
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == expected_rename
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == rename_draft
                    assert mutate('forge_code_redo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == expected_rename
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    fix_source = draft + '\n// café\nint fixes_probe() { int value = 1 return value; }\n'
                    assert mutate('forge_code_replace', **(observed_args() | dict(text=fix_source)))['ok']
                    fix_before = read('forge_code_read', path='Native/Spinner.cpp')
                    fix_offset = len(fix_source[:fix_source.index('1 return') + 2].encode('utf-8'))
                    assert mutate('forge_code_analysis_start', **(observed_args() | dict(kind='fixes', offset=fix_offset)))['ok']
                    fixes = settle()
                    fixed_source = fix_source.replace('1 return', '1; return')
                    assert fixes['complete'] and fixes['total'] == 1 and fixes['items'][0]['text'] == fixed_source, fixes
                    assert fixes['items'][0]['edit_count'] == 1 and 'source_edits' not in fixes
                    assert read('forge_code_read', path='Native/Spinner.cpp') == fix_before
                    route = discovery()
                    apply_fix = dict(target=route['result']['target'], expected_revision=route['revision'],
                                     sequence=route['next_sequence'], operation=fixes['operation'], index=0)
                    applied_fix = client.tool('forge_code_analysis_use', **apply_fix)
                    assert applied_fix['ok'] and client.tool('forge_code_analysis_use', **apply_fix) == applied_fix
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == fixed_source
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == fix_source
                    assert mutate('forge_code_redo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == fixed_source
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    before_signature = read('forge_code_read', path='Native/Spinner.cpp')
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='format', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['total'] == 1, status
                    proposal = status['items'][0]
                    assert proposal['edit_count'] == 2 and 'format_edits' not in status and 'source_edits' not in status
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_signature
                    route = discovery()
                    request = dict(target=route['result']['target'], expected_revision=route['revision'],
                        sequence=route['next_sequence'], operation=status['operation'], index=0)
                    formatted = client.tool('forge_code_analysis_use', **request)
                    assert formatted['ok'] and client.tool('forge_code_analysis_use', **request) == formatted
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == proposal['text']
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    assert mutate('forge_code_redo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == proposal['text']
                    assert mutate('forge_code_undo', **observed_args())['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
                    before_signature = read('forge_code_read', path='Native/Spinner.cpp')
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='signature', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['readonly'] and status['total'] == 2, status
                    before_wrong_kind = read('forge_code_read', path='Native/Spinner.cpp')
                    assert not mutate('forge_code_analysis_diagnostic_move', **observed_args(),
                                      operation=status['operation'], direction='next')['ok']
                    assert read('forge_code_read', path='Native/Spinner.cpp') == before_wrong_kind
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
                    information_before = read('forge_code_read', path='Native/Spinner.cpp')
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='hover', offset=args['offset'])))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['readonly'] and 'float' in status['items'][0]['documentation'], status
                    refused = mutate('forge_code_analysis_use', operation=status['operation'], index=0)
                    assert not refused['ok'] and 'read-only' in str(refused), refused
                    assert read('forge_code_read', path='Native/Spinner.cpp') == information_before
                    started = mutate('forge_code_analysis_start', **(observed_args() | dict(kind='symbols', offset=0)))
                    assert started['ok'], started
                    status = settle()
                    assert status['complete'] and status['total'] == 2, status
                    child = read('forge_code_analysis_status', offset=1, limit=1)['items'][0]
                    assert child['index'] == 1 and child['depth'] == 1 and child['label'] == 'degrees_per_second'
                    assert mutate('forge_code_analysis_use', operation=status['operation'], index=child['index'])['ok']
                    source = read('forge_code_read', path='Native/Spinner.cpp')
                    assert source['text'] == draft and source['cursor']['line'] == child['line']
                    location_before = read('forge_code_read', path='Native/Spinner.cpp')
                    assert mutate('forge_code_open', path='Native/Spinner.cpp', line=1, column=1)['ok']
                    navigation = read('forge_code_navigation_status')
                    assert navigation['can_back'] and navigation['back_target']['line'] == location_before['cursor']['line']
                    witness = {key: navigation['source'][key] for key in ('path', 'session', 'state')}
                    stale = mutate('forge_code_navigation_move', **witness, direction='back', navigation_revision=navigation['revision'] - 1)
                    assert not stale['ok'], stale
                    assert read('forge_code_navigation_status') == navigation
                    route = discovery()
                    request = dict(target=route['result']['target'], expected_revision=route['revision'],
                        sequence=route['next_sequence'], **witness, direction='back', navigation_revision=navigation['revision'])
                    moved = client.tool('forge_code_navigation_move', **request)
                    assert moved['ok'] and moved['result']['source']['cursor'] == location_before['cursor'], moved
                    assert client.tool('forge_code_navigation_move', **request) == moved
                    navigation = read('forge_code_navigation_status')
                    witness = {key: navigation['source'][key] for key in ('path', 'session', 'state')}
                    moved = mutate('forge_code_navigation_move', **witness, direction='forward', navigation_revision=navigation['revision'])
                    assert moved['ok'] and moved['result']['source']['cursor'] == dict(line=1, column=1), moved
                    navigation = read('forge_code_navigation_status')
                    cleared = mutate('forge_code_navigation_clear', navigation_revision=navigation['revision'])
                    assert cleared['ok'] and cleared['result']['back_count'] == 0 and cleared['result']['forward_count'] == 0
                    assert read('forge_code_read', path='Native/Spinner.cpp')['text'] == draft
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
