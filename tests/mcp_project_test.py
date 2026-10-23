"""Real project/settings/recovery owners through MCP, with explicit workspace scope."""
from contextlib import contextmanager
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed

TOOLS, HOST = map(str, map(Path, sys.argv[1:3]))


@contextmanager
def editor(mode='workspace'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-project-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, process, json.loads(line)
        finally:
            if process.poll() is None: process.kill()
            process.communicate(timeout=5)


for legacy in (False, True):
    with editor() as (root, process, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert {'forge_project_create', 'forge_project_open', 'forge_project_settings_edit',
                'forge_document_recovery_restore'} <= names

        def discovery():
            value = client.tool('forge_discover')
            assert value['ok'], value
            return value

        def read(tool, **extra):
            value = client.tool(tool, target=discovery()['result']['target'], **extra)
            assert value['ok'], value
            return value['result']

        def arguments(**extra):
            state = discovery()
            return dict(target=state['result']['target'], expected_revision=state['revision'],
                        sequence=state['next_sequence'], **extra)

        def mutate(tool, **extra): return client.tool(tool, **arguments(**extra))

        def settings(): return read('forge_project_settings_read')

        def settings_edit(tool, **extra): return mutate(tool, state=settings()['state'], **extra)

        initial_target = discovery()['result']['target']
        assert read('forge_project_list')['total'] == 0
        original = settings()
        assert not original['dirty']
        assert not settings_edit('forge_project_settings_edit', patch=[dict(op='replace', path='/simulation_hz', value=0)])['ok']
        assert settings()['state'] == original['state']
        denied = settings_edit('forge_project_settings_edit', patch=[dict(op='add', path='/modules', value=[])])
        failed(denied, 'unsupported_setting')
        assert settings()['state'] == original['state']
        changed = settings_edit('forge_project_settings_edit', patch=[dict(op='replace', path='/simulation_hz', value=120)])
        assert changed['ok'] and changed['result']['dirty'], changed
        failed(mutate('forge_project_settings_save', state=original['state']), 'stale_settings')
        assert not mutate('forge_project_create', path='Blocked', name='Blocked')['ok']
        assert not (root / 'Projects/Blocked').exists()
        failed(settings_edit('forge_project_settings_close'), 'unsaved_document')
        assert settings_edit('forge_project_settings_save')['ok']
        assert json.loads((root / 'Initial/forge.project.json').read_text())['simulation_hz'] == 120
        assert settings_edit('forge_project_settings_close')['ok']

        # The same settings editor baseline rejects external writes.
        assert settings_edit('forge_project_settings_edit', patch=[dict(op='replace', path='/simulation_hz', value=90)])['ok']
        file = root / 'Initial/forge.project.json'
        external = json.loads(file.read_text()); external['name'] = 'External name'; file.write_text(json.dumps(external))
        failed(settings_edit('forge_project_settings_save'), 'settings_conflict')
        assert json.loads(file.read_text())['simulation_hz'] == 120
        assert settings_edit('forge_project_settings_close', discard=True)['ok']

        # Only the approved root accepts new projects; failed admissions retain the old scene.
        before = read('forge_scene_read')
        assert not mutate('forge_project_create', path='../Outside/New', name='Escaped')['ok']
        assert not mutate('forge_project_create', path='.', name='Root')['ok']
        assert read('forge_scene_read') == before
        (root / 'Projects/OutsideLink').symlink_to(root / 'Outside', target_is_directory=True)
        assert not mutate('forge_project_open', path='OutsideLink')['ok']
        assert discovery()['result']['target'] == initial_target
        (root / 'source_draft').touch()
        assert not mutate('forge_project_create', path='AgentGame', name='Agent game')['ok']
        assert not (root / 'Projects/AgentGame').exists()
        (root / 'source_draft').unlink()
        create_arguments = arguments(path='AgentGame', name='Agent game')
        created = client.tool('forge_project_create', **create_arguments)
        assert created['ok'], created
        assert (root / 'Projects/AgentGame/forge.project.json').is_file()
        new_target = discovery()['result']['target']
        assert new_target != initial_target
        assert client.tool('forge_project_create', **create_arguments) == created  # Original receipt survives retarget.
        assert read('forge_project_read')['project_name'] == 'Agent game'
        assert not mutate('forge_project_create', path='AgentGame', name='Overwrite')['ok']
        assert read('forge_project_list')['projects'][0]['path'] == 'AgentGame'
        owned = subprocess.run([TOOLS.replace('forge_tools', 'forge_live_tests'), '--lease',
                                str(root / 'Projects/AgentGame')], input='', capture_output=True, text=True, timeout=5)
        assert owned.returncode == 1
        assert mutate('forge_project_create', path='LeaseGame', name='Leased project')['ok']
        assert mutate('forge_project_open', path='AgentGame')['ok']
        holder = subprocess.Popen([TOOLS.replace('forge_tools', 'forge_live_tests'), '--lease',
                                   str(root / 'Projects/LeaseGame')], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert holder.stdout.readline().strip() == 'owned'
            target_before = discovery()['result']['target']
            assert not mutate('forge_project_open', path='LeaseGame')['ok']
            assert discovery()['result']['target'] == target_before
            assert read('forge_project_read')['project_name'] == 'Agent game'
        finally:
            holder.communicate(input='\n', timeout=5)
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='Recovery cube'))['ok']
        assert not mutate('forge_project_open', initial=True)['ok']

        # Current recovery remains an undoable scene edit and uses an exact review digest.
        (root / 'autosave').touch()
        end = time.monotonic() + 5
        while True:
            recovery = read('forge_document_recovery_read')
            if recovery['available']: break
            assert time.monotonic() < end
            time.sleep(.01)
        assert mutate('forge_entity_rename', arguments=dict(entity=read('forge_scene_read')['entities'][0]['id'], name='Changed cube'))['ok']
        failed(mutate('forge_document_recovery_restore', review='0' * 64), 'stale_review')
        restored = mutate('forge_document_recovery_restore', review=recovery['review'])
        assert restored['ok'], restored
        assert read('forge_scene_read')['entities'][0]['name'] == 'Recovery cube'
        assert mutate('forge_history_undo')['ok']
        assert read('forge_scene_read')['entities'][0]['name'] == 'Changed cube'
        assert mutate('forge_document_recovery_discard', review=recovery['review'])['ok']
        assert not read('forge_document_recovery_read')['available']

        assert mutate('forge_project_open', initial=True, discard=True)['ok']
        assert read('forge_project_read')['project_name'] == 'External name'
        assert mutate('forge_project_open', path='AgentGame')['ok']
        # Untitled recovery changes document generation and still stays in workspace scope.
        assert mutate('forge_document_new')['ok']
        assert mutate('forge_entity_create', arguments=dict(kind=0, name='Untitled cube'))['ok']
        (root / 'autosave').touch()
        end = time.monotonic() + 5
        while True:
            recovery = read('forge_document_recovery_read', untitled=True)
            if recovery['available']: break
            assert time.monotonic() < end
            time.sleep(.01)
        old_target = discovery()['result']['target']
        assert mutate('forge_document_recovery_restore', untitled=True, review=recovery['review'])['ok']
        assert discovery()['result']['target'] != old_target
        assert read('forge_scene_read')['entities'][0]['name'] == 'Untitled cube'
        assert mutate('forge_document_recovery_discard', untitled=True, review=recovery['review'])['ok']
        # Manual out-of-scope UI switching revokes the connection, even with workspace authority.
        (root / 'ui_outside').touch()
        assert process.wait(timeout=5) == 0
        assert not client.tool('forge_discover')['ok']

    with editor('current') as (root, process, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_project_create' not in names and 'forge_project_open' not in names
        assert 'forge_document_recovery_restore' in names
        (root / 'ui_outside').touch()
        assert process.wait(timeout=5) == 0

    with editor('read') as (root, process, config), Client(config, legacy) as client:
        names = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_project_list' in names and 'forge_project_settings_read' in names
        assert not {'forge_project_create', 'forge_project_settings_edit', 'forge_document_recovery_restore'} & names

    with editor('recovery') as (root, process, config), Client(config, legacy) as client:
        def recovery_call(tool, mutation=False, **extra):
            observed = client.tool('forge_discover')
            assert observed['ok'], observed
            values = dict(target=observed['result']['target'], **extra)
            if mutation:
                values.update(expected_revision=observed['revision'], sequence=observed['next_sequence'])
            return client.tool(tool, **values)
        observed = client.tool('forge_discover')
        assert not observed['result']['mutation_available']
        failed(recovery_call('forge_entity_create', True, arguments=dict(kind=0)), 'busy')
        reviewed = recovery_call('forge_document_recovery_read')
        assert reviewed['ok'] and reviewed['result']['available'], reviewed
        restored = recovery_call('forge_document_recovery_restore', True, review=reviewed['result']['review'])
        assert restored['ok'], restored
        end = time.monotonic() + 5
        while not client.tool('forge_discover')['result']['mutation_available']:
            assert time.monotonic() < end, 'The recovery popup did not close after the owner completed restoration'
            time.sleep(.01)
        assert recovery_call('forge_scene_read')['result']['entities'][0]['name'] == 'Startup recovery cube'
        (root / 'other_popup').touch()
        end = time.monotonic() + 5
        while client.tool('forge_discover')['result']['mutation_available']:
            assert time.monotonic() < end
            time.sleep(.01)
        rejected = recovery_call('forge_document_recovery_discard', True, review=reviewed['result']['review'])
        assert not rejected['ok'] and 'UI interaction' in rejected['error']['message'], rejected
        assert recovery_call('forge_document_recovery_read')['result']['available']
        (root / 'other_popup').unlink()
        end = time.monotonic() + 5
        while not client.tool('forge_discover')['result']['mutation_available']:
            assert time.monotonic() < end
            time.sleep(.01)

with gzip.open(sys.argv[3], 'wt', encoding='utf-8') as output:
    json.dump(CAPTURE, output)
print('MCP approved project workspace, shared settings and recovery passed')
