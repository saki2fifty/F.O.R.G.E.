"""MCP against real EditorFiles/SceneDocument owners, leases and disk files."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from mcp_test_client import CAPTURE, Client, failed, write_capture

TOOLS, HOST = map(str, map(Path, sys.argv[1:3]))


@contextmanager
def editor(mode='files'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-doc-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, process, json.loads(line)
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)


def native(config, request, sequence=None):
    wire = dict(transport=1, token=config['token'], id='document-test', request=request)
    if sequence is not None:
        wire['sequence'] = sequence
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        channel.sendall((json.dumps(wire) + '\n').encode())
        with channel.makefile('rb') as reader:
            return json.loads(reader.readline(16777217))['response']


def wait_for(predicate):
    end = time.monotonic() + 4
    while not predicate():
        assert time.monotonic() < end, 'Owner did not process the test control marker'
        time.sleep(.01)


for legacy in (False, True):
    with editor() as (root, process, config), Client(config, legacy) as client:
        project = root / 'Game'

        def discover():
            result = client.tool('forge_discover')
            assert result['ok'], result
            return result

        def arguments(**extra):
            state = discover()
            return dict(target=state['result']['target'], expected_revision=state['revision'],
                        sequence=state['next_sequence'], **extra)

        def mutate(name, **extra):
            return client.tool(name, **arguments(**extra))

        def status():
            state = discover()
            result = client.tool('forge_project_read', target=state['result']['target'])
            assert result['ok'], result
            return result['result']

        catalog = client.request('tools/list')['result']['tools']
        assert len(catalog) == 42, len(catalog)
        state = discover()
        workflows = state['result']['workflow_methods']
        assert {item['method'] for item in workflows} == {
            'project.read', 'document.save', 'document.save_as', 'document.new', 'document.open'}
        assert set(state['result']['capabilities']) == {
            'scene.read', 'scene.edit', 'history', 'document.read', 'document.write'}
        for method in workflows:
            tool = next(item for item in catalog if item['name'] ==
                        'forge_' + method['method'].replace('.', '_'))
            props = dict(tool['inputSchema']['properties'])
            for field in ('target', 'expected_revision', 'sequence'):
                props.pop(field, None)
            assert props == method['input_schema']['properties']
            assert tool['annotations']['readOnlyHint'] == (not method['mutation'])
        assert status()['scene_path'] == 'Scenes/main.scene.json'
        assert not status()['dirty'] and status()['persisted']
        original = discover()['result']['target']
        # Existing project writer ownership remains exclusive across processes.
        lease = subprocess.run([sys.argv[1].replace('forge_tools', 'forge_live_tests'),
                                '--lease', str(project)], input='', capture_output=True,
                               text=True, timeout=5)
        assert lease.returncode == 1 and 'owned' not in lease.stdout, lease

        created = mutate('forge_entity_create', arguments=dict(kind=0, name='Saved agent cube'))
        assert created['ok'], created
        assert status()['dirty']
        saved = mutate('forge_document_save')
        assert saved['ok'] and not saved['result']['rediscover'], saved
        saved_path = project / 'Scenes/main.scene.json'
        saved_bytes = saved_path.read_bytes()
        saved_doc = json.loads(saved_bytes)
        assert saved_doc['entities'][0]['name'] == 'Saved agent cube'
        assert discover()['result']['target'] == original

        # Disk conflict/deletion is not silently overwritten, even for a clean draft.
        disk_edit = dict(saved_doc, entities=[])
        saved_path.write_text(json.dumps(disk_edit), encoding='utf-8')
        failed(mutate('forge_document_save'), 'document_error')
        assert json.loads(saved_path.read_bytes()) == disk_edit
        saved_path.unlink()
        failed(mutate('forge_document_save'), 'document_error')
        assert not saved_path.exists()
        saved_path.write_bytes(saved_bytes)

        # Save As delegates copy identity/history semantics to SceneDocument.
        copied_args = arguments(path='Scenes/é copy.scene.json')
        # Lose the original response after sending a complete file operation.
        request = dict(api=1, method='document.save_as', **copied_args)
        sequence = request.pop('sequence')
        wire = dict(transport=1, token=config['token'], id='lost-file-ack',
                    request=request, sequence=sequence)
        with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
            channel.sendall((json.dumps(wire) + '\n').encode())
        copy_path = project / 'Scenes/é copy.scene.json'
        wait_for(copy_path.exists)
        copied = client.tool('forge_document_save_as', **copied_args)
        assert copied['ok'] and copied['result']['rediscover'], copied
        copy_bytes = copy_path.read_bytes()
        copy_doc = json.loads(copy_bytes)
        assert copy_doc['asset_id'] != saved_doc['asset_id']
        assert copy_doc['entities'][0]['id'] != saved_doc['entities'][0]['id']
        current = discover()
        assert current['result']['target']['session'] != original['session']
        assert current['result']['target']['id'] == copy_doc['asset_id']
        assert not mutate('forge_history_undo')['result']['changed']
        # Replaying an old file mutation after retargeting returns the original receipt.
        replay = client.tool('forge_document_save_as', **copied_args)
        assert replay['result'] == copied['result'] and copy_path.read_bytes() == copy_bytes
        failed(client.tool('forge_document_save_as', **dict(copied_args, path='Scenes/conflict.scene.json')),
               'sequence_conflict')
        assert not (project / 'Scenes/conflict.scene.json').exists()
        failed(client.tool('forge_document_save', **dict(arguments(), target=original)), 'wrong_target')
        failed(client.tool('forge_document_save', **dict(arguments(), expected_revision=0)),
               'stale_revision')
        before = status()
        for path in ('../escape.scene.json', str(root / 'escape.scene.json'),
                     '.forge/recovery/invalid.scene.json', 'forge.project.json',
                     'Scenes/invalid.txt', 'Scenes/bad\x00.scene.json',
                     'Scenes/back\\slash.scene.json', 'Scenes/stream:bad.scene.json'):
            failed(mutate('forge_document_save_as', path=path), 'document_error')
        failed(mutate('forge_document_save_as', path='Scenes/main.scene.json'), 'document_error')
        assert status() == before and saved_path.read_bytes() == saved_bytes
        assert not (root / 'escape.scene.json').exists()

        # Shared native service also rejects unknown and incorrectly typed arguments.
        for extra in (dict(path=False), dict(path=''), dict(path='x' * 1025),
                      dict(path='Scenes/test.scene.json', unexpected=True)):
            args = arguments(**extra)
            sequence = args.pop('sequence')
            failed(native(config, dict(api=1, method='document.save_as', **args), sequence),
                   'invalid_arguments')
        args = arguments(discard='yes')
        sequence = args.pop('sequence')
        failed(native(config, dict(api=1, method='document.new', **args), sequence),
               'invalid_arguments')

        # Editor work gates do not queue a later save/switch behind the agent's back.
        for marker in ('block_all', 'file_busy', 'build_busy', 'gesture'):
            (root / marker).touch()
            failed(mutate('forge_document_new'), 'busy' if marker == 'gesture' else 'document_error')
            assert status()['scene_path'] == before['scene_path']
            (root / marker).unlink()
        (root / 'source_draft').touch()
        failed(mutate('forge_document_open', path='Scenes/main.scene.json', discard=True),
               'document_error')
        assert mutate('forge_document_save')['ok']
        (root / 'source_draft').unlink()

        # Dirty-switch loss is opt-in, with normal recovery preservation on failure.
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='Unsaved'))['ok']
        (root / 'autosave').touch()
        wait_for(lambda: status()['recovery_available'])
        recovery = list((project / '.forge/recovery').glob('*.json'))
        assert len(recovery) == 1
        recovery_bytes = recovery[0].read_bytes()
        failed(mutate('forge_document_new'), 'document_error')
        failed(mutate('forge_document_open', path='Scenes/main.scene.json'), 'document_error')
        malformed = project / 'Scenes/malformed.scene.json'
        malformed.write_text('{bad', encoding='utf-8')
        generation = status()['generation']
        failed(mutate('forge_document_open', path='Scenes/malformed.scene.json', discard=True),
               'document_error')
        assert status()['generation'] == generation and status()['dirty']
        assert recovery[0].read_bytes() == recovery_bytes
        opened = mutate('forge_document_open', path='Scenes/main.scene.json', discard=True)
        assert opened['ok'] and opened['result']['rediscover'], opened
        assert discover()['result']['target']['id'] == saved_doc['asset_id']
        assert not status()['dirty']
        assert not recovery[0].exists()
        assert mutate('forge_document_new')['ok']
        assert status()['scene_path'] is None and not status()['persisted']
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='First save'))['ok']
        untitled_target = discover()['result']['target']
        failed(mutate('forge_document_save'), 'document_error')
        first = mutate('forge_document_save_as', path='Scenes/first.scene.json')
        assert first['ok'] and first['result']['rediscover'], first
        assert discover()['result']['target']['id'] == untitled_target['id']
        failed(client.tool('forge_document_save', **dict(arguments(), target=untitled_target)),
               'wrong_target')
        assert mutate('forge_history_undo')['result']['changed']

        # Symlink escapes use the existing canonical project containment checks.
        try:
            (project / 'Scenes/outside').symlink_to(root / 'Other/Scenes', target_is_directory=True)
        except OSError:
            pass  # Windows may prohibit symlinks without developer mode/privilege.
        else:
            failed(mutate('forge_document_save_as', path='Scenes/outside/escaped.scene.json'),
                   'document_error')
            assert not (root / 'Other/Scenes/escaped.scene.json').exists()

        # A file grant survives UI scene changes; old targets cannot mutate the new document.
        previous = discover()['result']['target']
        (root / 'ui_new').touch()
        wait_for(lambda: not (root / 'ui_new').exists())
        wait_for(lambda: discover()['result']['target'] != previous)
        failed(client.tool('forge_document_save', **dict(arguments(), target=previous)), 'wrong_target')
        assert config['token'] not in json.dumps(CAPTURE)
        assert config['token'] not in ''.join(client.stderr)
        (root / 'ui_project').touch()
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

for mode, count in (('read', 5), ('edit', 38)):
    with editor(mode) as (root, process, config), Client(config) as client:
        catalog = client.request('tools/list')['result']['tools']
        assert len(catalog) == count
        assert not any(item['name'].startswith('forge_document_') for item in catalog)
        state = client.tool('forge_discover')
        assert client.tool('forge_project_read', target=state['result']['target'])['ok']
        request = dict(api=1, method='document.save', target=state['result']['target'],
                       expected_revision=state['revision'])
        failed(native(config, request, state['next_sequence']), 'unknown_method')
        assert client.request('tools/call', dict(name='forge_document_save', arguments={}))['error']['code'] == -32602
        (root / 'ui_new').touch()
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

if os.name == 'posix':
    with editor() as (root, process, config), Client(config) as client:
        # Replacing the lock pathname invalidates the owner's held inode identity.
        # Windows denies this replacement while the exclusive handle is open.
        lock = root / 'Game/.forge/writer.lock'
        lock.unlink()
        lock.write_text('replacement', encoding='utf-8')
        assert process.wait(timeout=4) == 0
        failed(client.tool('forge_discover'), 'connection_unavailable')

if len(sys.argv) > 3:
    write_capture(sys.argv[3])
print('MCP documents: owner saves, copy/first-save identities, disk conflicts, scope, '
      'dirty/busy gates, leases, fresh targets, replay and revocation passed')
