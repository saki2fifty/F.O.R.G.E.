"""Real native source pixels through the existing capture and MCP owners."""
import base64
from contextlib import contextmanager
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
from mcp_test_client import Client, failed, finish_host, write_capture, protocol_cases

TOOLS, HOST, FONT, GALLERY, RECEIPT = sys.argv[1:]


@contextmanager
def host(readonly=False):
    with tempfile.TemporaryDirectory(prefix='forge-native-source-image-') as directory:
        root = Path(directory)
        process = subprocess.Popen([HOST, directory, FONT, GALLERY,
                                    '--mcp-read' if readonly else '--mcp'],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        try:
            handoff_file = root / 'mcp-connection.json'
            end = time.monotonic() + 20
            while not handoff_file.exists():
                assert process.poll() is None, process.stderr.read()
                assert time.monotonic() < end, 'Native source-image handoff was not received'
                time.sleep(.01)
            yield root, json.loads(handoff_file.read_text())
        finally:
            finish_host(process, root)


def exercise(legacy):
    with host() as (_, handoff), Client(handoff['connection'], legacy, TOOLS) as client:
        def discover():
            value = client.tool('forge_discover')
            assert value['ok'], value
            return value

        def read(name, **arguments):
            value = client.tool(name, target=discover()['result']['target'], **arguments)
            assert value['ok'], value
            return value['result']

        def arguments(**extra):
            value = discover()
            return dict(target=value['result']['target'], expected_revision=value['revision'],
                        sequence=value['next_sequence'], **extra)

        def mutate(name, **extra):
            return client.tool(name, **arguments(**extra))

        def guard():
            source = read('forge_ui_source_read')
            return {key: source[key] for key in ('path', 'session', 'revision', 'state')}

        tools = client.request('tools/list')['result']['tools']
        advertised = {tool['name']: tool for tool in tools}
        assert 'forge_capture_ui_source' in advertised
        assert 'forge_capture_image' in advertised
        baseline = read('forge_ui_source_read')
        disk = Path(handoff['project'], baseline['path']).read_bytes()
        draft = baseline['text'] + '<!-- unsaved native preview -->\n'
        changed = mutate('forge_ui_source_replace', **guard(), text=draft)
        assert changed['ok'] and changed['result']['dirty'], changed
        preview = read('forge_ui_source_preview', **guard(), width=800, height=600, density=1)
        source = guard() | dict(preview_instance=preview['instance'],
                               preview_revision=preview['revision'])
        assert not preview['stale']
        before = read('forge_ui_source_read')
        for key, value in (('revision', 999999), ('preview_revision', 999999)):
            bad = source | {key: value}
            failed(mutate('forge_capture_ui_source', path='rejected.png', source=bad),
                   'stale_ui_source' if key == 'revision' else 'stale_ui_preview')
        failed(mutate('forge_capture_ui_source', path='../outside.png', source=source),
               'invalid_path')
        replay_arguments = arguments(path='preview.png', source=source)
        accepted = client.tool('forge_capture_ui_source', **replay_arguments)
        replay = client.tool('forge_capture_ui_source', **replay_arguments)
        assert accepted['ok'] and replay['ok'] and replay['result'] == accepted['result']
        operation = accepted['result']['operation']
        end = time.monotonic() + 15
        while True:
            status = read('forge_capture_status')
            if not status['busy']:
                break
            assert time.monotonic() < end, status
            time.sleep(.01)
        assert status['operation'] == operation and not status['error'], status
        assert status['view'] == 'ui_source' and status['source']['state'] == source['state']
        response = client.request('tools/call', dict(name='forge_capture_image', arguments=dict(
            target=discover()['result']['target'], operation=operation)))['result']
        assert not response['isError'], response
        blocks = response['content']
        assert len(blocks) == 2 and blocks[1]['type'] == 'image'
        assert blocks[1]['mimeType'] == 'image/png'
        plain = json.loads(blocks[0]['text'])
        assert plain == response['structuredContent'] and 'data' not in plain['result']['image']
        pixels = base64.b64decode(blocks[1]['data'], validate=True)
        assert pixels[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', pixels[16:24]) == (800, 600)
        assert plain['result']['source']['state'] == source['state']
        assert pixels == Path(handoff['output'], 'preview.png').read_bytes()
        failed(mutate('forge_capture_ui_source', path='preview.png', source=source), 'output_exists')
        assert read('forge_ui_source_read') == before
        assert Path(handoff['project'], baseline['path']).read_bytes() == disk
    with host(True) as (_, handoff), Client(handoff['connection'], legacy, TOOLS) as client:
        names = {item['name'] for item in client.request('tools/list')['result']['tools']}
        assert 'forge_ui_source_preview' in names and 'forge_capture_ui_source' not in names
        assert 'forge_capture_image' not in names
    print(('legacy' if legacy else 'current') +
          ': real native source PNG, guards, output grants/conflicts, replay and image blocks pass')


for legacy in protocol_cases():
    exercise(legacy)
write_capture(RECEIPT)
