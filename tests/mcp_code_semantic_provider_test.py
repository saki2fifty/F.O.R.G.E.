"""Opt-in real-provider integration: actual managed SDK context and MCP source owner.

Pass tools, native code host, exact installed SDK, CMake, Ninja, separately
installed supported clangd, scratch directory and capture path. No tool download.
The deterministic default regression remains mcp_code_analysis_test.py.
"""
import json
from pathlib import Path
import subprocess
import sys
import time
from mcp_test_client import Client, finish_host, protocol_cases, write_capture

TOOLS, HOST, SDK, CMAKE, NINJA, PROVIDER, SCRATCH, CAPTURE = sys.argv[1:]
scratch = Path(SCRATCH).resolve()
scratch.mkdir(parents=True, exist_ok=True)
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
        finish_host(process, root)
    finally:
        write_capture(CAPTURE)
        if process.poll() is None:
            finish_host(process, root)
write_capture(CAPTURE)
print('Real separately installed provider, genuine managed SDK CMake context, unsaved completion/Undo/declaration and clean native shutdown passed')
