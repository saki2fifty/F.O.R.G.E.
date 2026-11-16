"""Opt-in real-provider integration: actual managed SDK context and MCP source owner.

Pass tools, native code host, exact installed SDK, CMake, Ninja, separately
installed supported clangd, scratch directory and capture path. No tool download.
The deterministic default regression remains mcp_code_analysis_test.py.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from mcp_test_client import Client, finish_host, protocol_cases, write_capture

TOOLS, HOST, SDK, CMAKE, NINJA, PROVIDER, SCRATCH, CAPTURE = sys.argv[1:]
scratch = Path(SCRATCH).resolve()
scratch.mkdir(parents=True, exist_ok=True)
def source_cursor(text, index):
    start = text.rfind('\n', 0, index) + 1
    return dict(line=text.count('\n', 0, index) + 1,
                column=len(text[start:index].expandtabs(4)) + 1)


for legacy in protocol_cases():
    root = scratch / ('legacy' if legacy else 'current')
    assert not root.exists(), 'Use fresh project storage for this integration run'
    process = subprocess.Popen([HOST, str(root), SDK, CMAKE, NINJA, 'execute'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, encoding='utf-8')
    try:
        config = json.loads(process.stdout.readline())
        with Client(config, legacy) as client:
            def discovery():
                return client.tool('forge_discover')

            def read(tool_name, **args):
                reply = client.tool(tool_name, target=discovery()['result']['target'], **args)
                assert reply['ok'], reply
                return reply['result']

            def mutate(tool_name, **args):
                state = discovery()
                reply = client.tool(tool_name, target=state['result']['target'],
                                    expected_revision=state['revision'],
                                    sequence=state['next_sequence'], **args)
                assert reply['ok'], reply
                return reply['result']

            def observed():
                state = read('forge_code_read', path='Native/gameplay.cpp')
                return {key: state[key] for key in ('path', 'session', 'state')}

            def settle(tool):
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    result = read(tool)
                    if not result['busy']:
                        return result
                    time.sleep(.01)
                raise AssertionError('Native tool task did not settle')

            mutate('forge_code_setup')
            mutate('forge_code_build')
            build = settle('forge_code_status')
            # Deployment support is separate from a successful CMake context.
            # Retain actual compiler output and report its real outcome.
            (root / 'analysis-build-result.json').write_text(json.dumps(build, indent=2))
            (root / 'analysis-build-log.txt').write_text(read('forge_code_build_log')['text'])
            mutate('forge_code_create', kind='header', name='Spinner.hpp')
            header_saved = (root / 'Native/Spinner.hpp').read_bytes()
            header = read('forge_code_read', path='Native/Spinner.hpp')
            initial_header_text = header['text']
            header_draft = '#pragma once\nstruct Spinner { float degrees_per_second; };\n'
            mutate('forge_code_replace', **{key: header[key] for key in ('path', 'session', 'state')}, text=header_draft)
            mutate('forge_code_open', path='Native/gameplay.cpp')
            original = read('forge_code_read', path='Native/gameplay.cpp')['text']
            saved_original = (root / 'Native/gameplay.cpp').read_bytes()
            draft = original + '\n// café unsaved header member\n#include \"Spinner.hpp\"\nvoid semantic_tick() { Spinner s; s.de }\n'
            mutate('forge_code_replace', **observed(), text=draft)
            offset = len(draft[:draft.rfind('s.de') + 4].encode())
            mutate('forge_code_analysis_start', **observed(), kind='completion', offset=offset, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            (root / 'analysis-completion-result.json').write_text(json.dumps(result, indent=2))
            assert result['complete'] and not result['stale'], result
            member = next(item for item in result['items'] if item['available'] and
                          item['text'] == 'degrees_per_second')
            mutate('forge_code_analysis_use', operation=result['operation'], index=member['index'])
            completed = draft.replace('s.de }', 's.degrees_per_second }')
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == completed
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            mutate('forge_code_redo', **observed())
            mutate('forge_code_analysis_start', **observed(), kind='declaration', offset=offset, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            (root / 'analysis-declaration-result.json').write_text(json.dumps(result, indent=2))
            assert result['complete'], result
            target = next(item for item in result['items'] if item['available'])
            mutate('forge_code_analysis_use', operation=result['operation'], index=target['index'])
            assert target['path'] == 'Native/Spinner.hpp', target
            assert read('forge_code_read', path=target['path'])['cursor']['line'] == target['line']
            assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
            assert read('forge_code_read', path='Native/Spinner.hpp')['text'] == header_draft
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == completed
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            signature_draft = completed + '\nvoid throttle(float café, float 𐐀rate);\nvoid call_signature() { throttle(1, ); }\n'
            mutate('forge_code_replace', **observed(), text=signature_draft)
            signature_before = read('forge_code_read', path='Native/gameplay.cpp')
            signature_offset = len(signature_draft[:signature_draft.rfind('1, ') + 3].encode())
            mutate('forge_code_analysis_start', **observed(), kind='signature', offset=signature_offset, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            (root / 'analysis-signature-result.json').write_text(json.dumps(result, indent=2))
            assert result['complete'] and result['readonly'] and not result['stale'], result
            active = result['items'][result['active_signature']]
            assert result['active_parameter'] == 1 and active['parameters'][1]['label'] == 'float 𐐀rate', result
            assert read('forge_code_read', path='Native/gameplay.cpp') == signature_before
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            clean = completed.replace('s.degrees_per_second }', '(void)s.degrees_per_second; }')
            for case in ('clean', 'unicode-error', 'unsaved-header'):
                text = clean
                if case == 'unicode-error':
                    text += '\nvoid diagnostic_probe() { float café = 1; (void)café; missing_symbol(); }\n'
                if case == 'unsaved-header':
                    header = read('forge_code_read', path='Native/Spinner.hpp')
                    mutate('forge_code_replace', **{key: header[key] for key in ('path', 'session', 'state')},
                           text='#pragma once\nstruct Spinner { MissingType degrees_per_second; };\n')
                mutate('forge_code_replace', **observed(), text=text)
                diagnostic_before = read('forge_code_read', path='Native/gameplay.cpp')
                mutate('forge_code_analysis_start', **observed(), kind='diagnostics', offset=0, tool=PROVIDER)
                result = settle('forge_code_analysis_status')
                (root / ('analysis-diagnostic-' + case + '-result.json')).write_text(json.dumps(result, indent=2))
                assert result['complete'] and not result['stale'] and result['occurred_at'], result
                assert read('forge_code_read', path='Native/gameplay.cpp') == diagnostic_before
                if case == 'clean':
                    assert result['total'] == 0, result
                else:
                    assert result['total'] > 0, result
                    if case == 'unicode-error':
                        error = next(item for item in result['items'] if 'missing_symbol' in item['message'])
                        assert error['begin'] == len(text[:text.index('missing_symbol')].encode()), error
                    else:
                        error = next(item for item in result['items'] if 'MissingType' in item['message'])
                    assert error['severity'] == 'Error' and error['path'] == 'Native/gameplay.cpp', error
                    mutate('forge_code_analysis_use', operation=result['operation'], index=error['index'])
                    source = read('forge_code_read', path='Native/gameplay.cpp')
                    assert source['text'] == text and source['cursor']['line'] == error['line']
                assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
                assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
                mutate('forge_code_undo', **observed())
                assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            header = read('forge_code_read', path='Native/Spinner.hpp')
            mutate('forge_code_replace', **{key: header[key] for key in ('path', 'session', 'state')}, text=header_draft)
            symbol_text = clean + '\nnamespace OutlineProbe { struct Pivot { /// Angular rate.\nfloat café; float 𐐀rate; void tick(); }; }\nvoid inspect() { OutlineProbe::Pivot p; (void)p.café; }\nvoid navigation_body();\nvoid navigation_body() {}\nstruct NavigationBase { virtual void tick() = 0; };\nstruct NavigationDerived : NavigationBase { void tick() override {} };\n'
            mutate('forge_code_replace', **observed(), text=symbol_text)
            symbol_before = read('forge_code_read', path='Native/gameplay.cpp')
            symbol_offset = len(symbol_text[:symbol_text.rindex('p.café') + 2].encode())
            mutate('forge_code_analysis_start', **observed(), kind='hover', offset=symbol_offset, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            (root / 'analysis-hover-result.json').write_text(json.dumps(result, indent=2))
            assert result['complete'] and result['readonly'] and result['total'] == 1, result
            assert 'café' in result['items'][0]['documentation'] and 'float' in result['items'][0]['documentation']
            assert result['items'][0]['begin'] == symbol_offset
            assert read('forge_code_read', path='Native/gameplay.cpp') == symbol_before
            mutate('forge_code_analysis_start', **observed(), kind='symbols', offset=0, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            assert result['complete'] and not result['limited'] and not result['stale'], result
            pages = [read('forge_code_analysis_status', offset=offset, limit=8) for offset in range(0, result['total'], 8)]
            items = [item for page in pages for item in page['items']]
            (root / 'analysis-symbols-result.json').write_text(json.dumps(dict(status=result, pages=pages), indent=2))
            assert {'OutlineProbe', 'Pivot', 'café', '𐐀rate', 'tick'}.issubset({item['label'] for item in items}), items
            member = next(item for item in items if item['label'] == 'café')
            assert member['depth'] == 2 and member['begin'] == len(symbol_text[:symbol_text.index('float café') + 6].encode()), member
            assert read('forge_code_read', path='Native/gameplay.cpp') == symbol_before
            mutate('forge_code_analysis_use', operation=result['operation'], index=member['index'])
            source = read('forge_code_read', path='Native/gameplay.cpp')
            assert source['text'] == symbol_text and source['cursor']['line'] == member['line']
            for kind, offset in (('references', symbol_offset),
                                 ('typeDefinition', len(symbol_text[:symbol_text.rindex('p.café')].encode())),
                                 ('definition', len(symbol_text[:symbol_text.index('navigation_body')].encode())),
                                 ('implementation', len(symbol_text[:symbol_text.index('virtual void tick') + 13].encode()))):
                before_navigation = read('forge_code_read', path='Native/gameplay.cpp')
                mutate('forge_code_analysis_start', **observed(), kind=kind, offset=offset, tool=PROVIDER)
                result = settle('forge_code_analysis_status')
                (root / ('analysis-' + kind + '-result.json')).write_text(json.dumps(result, indent=2))
                assert result['complete'] and not result['stale'] and result['total'] > 0, result
                assert read('forge_code_read', path='Native/gameplay.cpp') == before_navigation
                if kind == 'references':
                    assert result['scope'] == 'current_context' and result['exhaustive'] is False
                    location = next(item for item in result['items'] if item['begin'] == symbol_offset)
                    assert all(item['begin'] != member['begin'] for item in result['items']), result
                else:
                    if kind == 'implementation':
                        assert result['scope'] == 'current_context' and result['exhaustive'] is False
                    target_offset = (symbol_text.index('struct Pivot') + 7 if kind == 'typeDefinition' else
                                     symbol_text.rindex('navigation_body') if kind == 'definition' else
                                     symbol_text.index('tick() override'))
                    type_begin = len(symbol_text[:target_offset].encode())
                    location = next(item for item in result['items'] if item['begin'] == type_begin)
                mutate('forge_code_analysis_use', operation=result['operation'], index=location['index'])
                navigated = read('forge_code_read', path='Native/gameplay.cpp')
                assert navigated['text'] == symbol_text and navigated['cursor']['line'] == location['line']
                assert navigated['can_undo'] == before_navigation['can_undo'] and navigated['can_redo'] == before_navigation['can_redo']
            rename_text = symbol_text + '\nfloat rename_probe(float rename_rate) { /* café */ return rename_rate + rename_rate; }\n'
            mutate('forge_code_replace', **observed(), text=rename_text)
            rename_before = read('forge_code_read', path='Native/gameplay.cpp')
            rename_name = 'vitesse_é'
            rename_offset = len(rename_text[:rename_text.index('rename_rate')].encode())
            mutate('forge_code_analysis_start', **observed(), kind='rename', offset=rename_offset,
                   new_name=rename_name, tool=PROVIDER)
            rename = settle('forge_code_analysis_status')
            (root / 'analysis-rename-result.json').write_text(json.dumps(rename, indent=2))
            assert rename['complete'] and rename['total'] == 1 and rename['new_name'] == rename_name, rename
            assert rename['scope'] == 'current_context' and rename['exhaustive'] is False and 'source_edits' not in rename
            proposal = rename['items'][0]
            renamed_text = symbol_text + '\nfloat rename_probe(float vitesse_é) { /* café */ return vitesse_é + vitesse_é; }\n'
            assert proposal['text'] == renamed_text and proposal['edit_count'] == 3
            assert read('forge_code_read', path='Native/gameplay.cpp') == rename_before
            mutate('forge_code_open', path='Native/gameplay.cpp', state=rename_before['state'],
                   **source_cursor(rename_text, rename_text.rindex('rename_rate') + 2))
            mutate('forge_code_analysis_use', operation=rename['operation'], index=0)
            renamed = read('forge_code_read', path='Native/gameplay.cpp')
            assert renamed['text'] == renamed_text
            assert renamed['cursor'] == source_cursor(renamed_text, renamed_text.rindex(rename_name) + len(rename_name))
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == rename_text
            mutate('forge_code_redo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == renamed_text
            mutate('forge_code_undo', **observed())
            rejected_before = read('forge_code_read', path='Native/gameplay.cpp')
            mutate('forge_code_analysis_start', **observed(), kind='rename', offset=rename_offset,
                   new_name='7invalid', tool=PROVIDER)
            invalid_rename = settle('forge_code_analysis_status')
            assert not invalid_rename['complete'] and invalid_rename['error'], invalid_rename
            assert read('forge_code_read', path='Native/gameplay.cpp') == rejected_before
            cross_offset = len(rename_text[:rename_text.index('s.degrees_per_second') + 2].encode())
            mutate('forge_code_analysis_start', **observed(), kind='rename', offset=cross_offset,
                   new_name='angular_speed', tool=PROVIDER)
            cross_rename = settle('forge_code_analysis_status')
            (root / 'analysis-rename-cross-file-result.json').write_text(json.dumps(cross_rename, indent=2))
            assert not cross_rename['complete'] and cross_rename['error'], cross_rename
            assert read('forge_code_read', path='Native/gameplay.cpp') == rejected_before
            assert read('forge_code_read', path='Native/Spinner.hpp')['text'] == header_draft
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == symbol_text
            fix_text = symbol_text + '\n// café\nint fixes_probe() { int value = 1 return value; }\n'
            mutate('forge_code_replace', **observed(), text=fix_text)
            fix_before = read('forge_code_read', path='Native/gameplay.cpp')
            mutate('forge_code_analysis_start', **observed(), kind='diagnostics', offset=0, tool=PROVIDER)
            fix_diagnostics = settle('forge_code_analysis_status')
            assert fix_diagnostics['complete'], fix_diagnostics
            fix_start = len(fix_text[:fix_text.index('int fixes_probe')].encode('utf-8'))
            fix_offset = next(row['begin'] for row in fix_diagnostics['items']
                              if row.get('code') == 'expected_semi_declaration' and row['begin'] >= fix_start)
            mutate('forge_code_analysis_start', **observed(), kind='fixes', offset=fix_offset, tool=PROVIDER)
            fixes = settle('forge_code_analysis_status')
            (root / 'analysis-quick-fixes-result.json').write_text(json.dumps(fixes, indent=2))
            fixed_text = fix_text.replace('1 return', '1; return')
            assert fixes['complete'] and fixes['total'] >= 1, fixes
            proposal = next(row for row in fixes['items'] if row.get('text') == fixed_text)
            assert proposal['available'] and proposal['edit_count'] == 1 and 'source_edits' not in fixes
            assert read('forge_code_read', path='Native/gameplay.cpp') == fix_before
            mutate('forge_code_open', path='Native/gameplay.cpp', state=fix_before['state'],
                   **source_cursor(fix_text, fix_text.rindex('value')))
            mutate('forge_code_analysis_use', operation=fixes['operation'], index=proposal['index'])
            fixed = read('forge_code_read', path='Native/gameplay.cpp')
            assert fixed['text'] == fixed_text and fixed['cursor'] == source_cursor(fixed_text, fixed_text.rindex('value'))
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == fix_text
            mutate('forge_code_redo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == fixed_text
            mutate('forge_code_undo', **observed())
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == symbol_text
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
            selection_before = read('forge_code_read', path='Native/gameplay.cpp')
            mutate('forge_code_analysis_start', **observed(), kind='selection', offset=symbol_offset, tool=PROVIDER)
            selection = settle('forge_code_analysis_status')
            (root / 'analysis-selection-result.json').write_text(json.dumps(selection, indent=2))
            assert selection['complete'] and 0 < selection['total'] <= 16 and not selection['stale'], selection
            assert read('forge_code_read', path='Native/gameplay.cpp') == selection_before
            extent = selection['items'][min(1, selection['total'] - 1)]
            mutate('forge_code_analysis_use', operation=selection['operation'], index=extent['index'])
            selected = read('forge_code_read', path='Native/gameplay.cpp')
            selected_bytes = symbol_text.encode()[extent['begin']:extent['end']]
            assert selected['text'] == symbol_text and selected['selection']['active']
            assert selected['selection']['bytes'] == len(selected_bytes) and selected['selection']['state'] == hashlib.sha256(selected_bytes).hexdigest()
            assert selected['can_undo'] == selection_before['can_undo'] and selected['can_redo'] == selection_before['can_redo']
            format_before = read('forge_code_read', path='Native/gameplay.cpp')
            mutate('forge_code_analysis_start', **observed(), kind='format', offset=0, tool=PROVIDER)
            formatting = settle('forge_code_analysis_status')
            (root / 'analysis-format-result.json').write_text(json.dumps(formatting, indent=2))
            assert formatting['complete'] and formatting['total'] == 1, formatting
            proposal = formatting['items'][0]
            assert proposal['edit_count'] > 0 and 'format_edits' not in formatting and 'source_edits' not in formatting
            assert read('forge_code_read', path='Native/gameplay.cpp') == format_before
            mutate('forge_code_open', path='Native/gameplay.cpp', state=format_before['state'],
                   **source_cursor(symbol_text, symbol_text.rindex('p.café') + 2))
            mutate('forge_code_analysis_use', operation=formatting['operation'], index=proposal['index'])
            formatted = read('forge_code_read', path='Native/gameplay.cpp')
            assert formatted['text'] == proposal['text'] and '\r' not in formatted['text']
            assert formatted['cursor'] == source_cursor(formatted['text'], formatted['text'].rindex('p.café') + 2)
            mutate('forge_code_analysis_start', **observed(), kind='format', offset=0, tool=PROVIDER)
            empty_format = settle('forge_code_analysis_status')
            assert empty_format['complete'] and empty_format['total'] == 0, empty_format
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == symbol_text
            mutate('forge_code_redo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == proposal['text']
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == symbol_text
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            mutate('forge_code_analysis_start', **observed(), kind='hover', offset=len(symbol_text.encode()), tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            assert result['complete'] and result['readonly'] and result['total'] == 0, result
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            mutate('forge_code_replace', **observed(), text='// no declarations\n')
            mutate('forge_code_analysis_start', **observed(), kind='symbols', offset=0, tool=PROVIDER)
            result = settle('forge_code_analysis_status')
            assert result['complete'] and result['total'] == 0, result
            for kind in ('references', 'typeDefinition'):
                mutate('forge_code_analysis_start', **observed(), kind=kind, offset=0, tool=PROVIDER)
                result = settle('forge_code_analysis_status')
                assert result['complete'] and result['total'] == 0, result
            mutate('forge_code_undo', **observed())
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
            main_before_navigation = read('forge_code_read', path='Native/gameplay.cpp')
            mutate('forge_code_open', path='Native/Spinner.hpp')
            def navigate(direction):
                navigation = read('forge_code_navigation_status')
                witness = {key: navigation['source'][key] for key in ('path', 'session', 'state')}
                return mutate('forge_code_navigation_move', **witness, direction=direction,
                              navigation_revision=navigation['revision'])
            navigation = navigate('back')
            assert navigation['source']['path'] == 'Native/gameplay.cpp'
            assert navigation['source']['cursor'] == main_before_navigation['cursor']
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            navigation = navigate('forward')
            assert navigation['source']['path'] == 'Native/Spinner.hpp'
            assert read('forge_code_read', path='Native/Spinner.hpp')['text'] == header_draft
            navigate('back')
            header = read('forge_code_read', path='Native/Spinner.hpp')
            mutate('forge_code_close', **{key: header[key] for key in ('path', 'session', 'state')}, discard=True)
            navigation = navigate('forward')
            assert navigation['source']['path'] == 'Native/Spinner.hpp'
            assert read('forge_code_read', path='Native/Spinner.hpp')['text'] == initial_header_text
            cleared = mutate('forge_code_navigation_clear', navigation_revision=navigation['revision'])
            assert cleared['back_count'] == 0 and cleared['forward_count'] == 0
            assert read('forge_code_read', path='Native/gameplay.cpp')['text'] == draft
            assert (root / 'Native/gameplay.cpp').read_bytes() == saved_original
            assert (root / 'Native/Spinner.hpp').read_bytes() == header_saved
            (root / 'source-navigation-result.json').write_text(json.dumps(cleared, indent=2))
        finish_host(process, root)
    finally:
        write_capture(CAPTURE)
        if process.poll() is None:
            finish_host(process, root)
write_capture(CAPTURE)
print('Real separately installed provider, genuine managed SDK CMake context, unsaved completion/Undo/declaration, Unicode call signatures, versioned diagnostics, plain symbol information, nested file outline/empty results, Back/Forward/closed-source navigation and clean native shutdown passed')
