"""Black-box native MCP/LiveAuthoring checks. No third-party Python packages."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

from mcp_test_client import CAPTURE, CURRENT, Client, failed

TOOLS, HOST = map(str, map(Path, sys.argv[1:3]))


@contextmanager
def host(mode='edit'):
    process = subprocess.Popen([HOST, mode], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline()
        assert line, 'LiveAuthoring host failed to start'
        yield process, json.loads(line)
    finally:
        process.kill()
        process.communicate(timeout=5)


def direct(config, request):
    wire = dict(transport=1, token=config['token'], id='parity', request=request)
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        channel.sendall((json.dumps(wire) + '\n').encode())
        with channel.makefile('rb') as reader:
            return json.loads(reader.readline(16777217))['response']


for legacy in (False, True):
    with host() as (_, config), Client(config, legacy) as client:
        health = 'ping' if legacy else 'server/discover'
        if not legacy:
            discovered = client.request('server/discover')['result']
            assert discovered['supportedVersions'] == [CURRENT]
            assert discovered['resultType'] == 'complete'
            assert list(discovered['capabilities']) == ['tools']
        catalog = client.request('tools/list')['result']['tools']
        names = [tool['name'] for tool in catalog]
        assert len(names) == len(set(names))
        discovery = client.tool('forge_discover')
        assert set(discovery['result']['methods']) == {
            'discover', 'scene.read', 'entity.query', 'scene.diagnostics',
            'scene.apply', 'scene.replace', 'history.undo', 'history.redo'}
        commands = discovery['result']['commands']
        assert len(catalog) == len(commands) + 8
        for command in commands:
            tool_name = 'forge_' + command['id'].replace('.', '_')
            item = next(tool for tool in catalog if tool['name'] == tool_name)
            assert item['inputSchema']['properties']['arguments'] == command['input_schema']
            assert not item['annotations']['readOnlyHint']
        for tool in catalog:
            schema = tool['inputSchema']
            assert schema['type'] == 'object' and isinstance(schema['properties'], dict)
            assert isinstance(schema['required'], list)
            assert not schema['additionalProperties']
        target = discovery['result']['target']
        args = dict(target=target, expected_revision=discovery['revision'],
                    sequence=discovery['next_sequence'],
                    arguments=dict(kind=0, name='Agent cube'))
        created = client.tool('forge_entity_create', **args)
        assert created['ok'], created
        observed = direct(config, dict(api=1, method='scene.read', target=target))
        assert len(observed['result']['entities']) == 1
        cube = observed['result']['entities'][0]['id']
        assert observed['result']['entities'][0]['name'] == 'Agent cube'
        queried = client.tool('forge_entity_query', target=target, text='Agent', limit=1)
        assert queried['result']['total'] == 1 and queried['result']['entities'][0]['id'] == cube
        assert client.tool('forge_scene_diagnostics', target=target)['ok']
        failed(client.tool('forge_entity_query', target=target, limit=0), 'invalid_arguments')

        # Same explicit sequence retrieves the editor-owned receipt; changed data conflicts.
        assert client.tool('forge_entity_create', **args) == created
        conflict = dict(args, arguments=dict(kind=0, name='Must not appear'))
        failed(client.tool('forge_entity_create', **conflict), 'sequence_conflict')
        stale = dict(args, sequence=created['next_sequence'])
        failed(client.tool('forge_entity_create', **stale), 'stale_revision')
        current = client.tool('forge_discover')
        state = dict(target=target, expected_revision=current['revision'],
                     sequence=current['next_sequence'])
        # A late failing command rolls back earlier valid commands in the same batch.
        batch = [dict(operation='entity.create', arguments=dict(name='Discard this', kind=1)),
                 dict(operation='entity.rename', arguments=dict(entity='missing', name='Fail'))]
        assert not client.tool('forge_scene_apply', commands=batch, **state)['ok']
        assert len(client.tool('forge_scene_read', target=target)['result']['entities']) == 1
        current = client.tool('forge_discover')
        state.update(expected_revision=current['revision'], sequence=current['next_sequence'])
        changed = client.tool('forge_property_set_many', **state,
                              arguments=dict(entity=cube, component='forge.local_translation',
                                             values=dict(x=3.0, y=2.0)))
        assert changed['ok'], changed
        position = client.tool('forge_scene_read', target=target)['result']['entities'][0][
            'components']['forge.local_translation']
        assert position['x'] == 3 and position['y'] == 2
        undone = client.tool('forge_history_undo', target=target,
                             expected_revision=changed['revision'], sequence=changed['next_sequence'])
        assert undone['ok'], undone
        redone = client.tool('forge_history_redo', target=target,
                             expected_revision=undone['revision'], sequence=undone['next_sequence'])
        assert redone['ok'], redone
        assert client.tool('forge_scene_read', target=target)['result']['entities'][0]['id'] == cube
        failed(client.tool('forge_scene_read', target=dict(target, session='foreign')), 'wrong_target')

        # Missing arguments do not consume a mutation sequence.
        failed(client.tool('forge_entity_create', arguments=dict(kind=0)), 'invalid_arguments')
        assert client.tool('forge_discover')['next_sequence'] == redone['next_sequence']
        before = client.tool('forge_discover')
        client.notify('tools/call', dict(name='forge_entity_create', arguments=dict(
            target=target, sequence=before['next_sequence'], expected_revision=before['revision'],
            arguments=dict(kind=0, name='Notification must not mutate'))))
        client.notify('unknown/notification')
        ping_result = client.request(health)['result']
        assert ping_result == {} if legacy else ping_result['resultType'] == 'complete'
        assert client.tool('forge_discover')['next_sequence'] == before['next_sequence']
        assert client.request('tools/call', dict(name='forge_shell', arguments={}))['error']['code'] == -32602
        assert client.request('unsupported/method')['error']['code'] == -32601

        snapshot = client.tool('forge_scene_read', target=target)['result']
        replacement = json.loads(json.dumps(snapshot))
        replacement['entities'][0]['name'] = 'Replaced cube'
        current = client.tool('forge_discover')
        replaced = client.tool('forge_scene_replace', target=target, document=replacement,
                               expected_revision=current['revision'], sequence=current['next_sequence'])
        assert replaced['ok'], replaced
        assert client.tool('forge_scene_read', target=target)['result']['entities'][0]['name'] == 'Replaced cube'
        restored = client.tool('forge_history_undo', target=target,
                               expected_revision=replaced['revision'], sequence=replaced['next_sequence'])
        assert restored['ok'], restored
        assert client.tool('forge_scene_read', target=target)['result']['entities'][0]['name'] == 'Agent cube'

        for wire, code in (('{bad', -32700), ('[]', -32600),
                           ('[' * 70 + '0' + ']' * 70, -32700),
                           (' ' * 1048577, -32700)):
            client.raw(wire)
            assert client.receive(None)['error']['code'] == code
        assert client.request(health).get('result') is not None
        if not legacy:
            client.raw(json.dumps(dict(jsonrpc='2.0', id='version-test', method='server/discover',
                                       params={'_meta': {
                                           'io.modelcontextprotocol/protocolVersion': 'unknown',
                                           'io.modelcontextprotocol/clientCapabilities': {}}})))
            error = client.receive('version-test')['error']
            assert error['code'] == -32022 and error['data']['supported'] == [CURRENT]
        all_output = json.dumps(CAPTURE) + ''.join(client.stderr)
        assert config['token'] not in all_output
        client.close()

with host('read') as (_, config), Client(config) as client:
    names = [item['name'] for item in client.request('tools/list')['result']['tools']]
    assert set(names) == {'forge_discover', 'forge_scene_read', 'forge_entity_query',
                          'forge_scene_diagnostics'}
    assert client.request('tools/call', dict(name='forge_entity_create', arguments={}))['error']['code'] == -32602
    assert not client.tool('forge_discover')['result']['mutation_available']

with host('busy') as (_, config), Client(config) as client:
    current = client.tool('forge_discover')
    assert not current['result']['mutation_available']
    failed(client.tool('forge_entity_create', target=current['result']['target'],
                       expected_revision=current['revision'], sequence=current['next_sequence'],
                       arguments=dict(kind=0)), 'busy')

with host() as (process, config):
    with Client(dict(config, token='0' * 64)) as client:
        assert client.request('tools/list')['error']['code'] == -32000
        failed(client.tool('forge_discover'), 'unauthorized')
        assert config['token'] not in ''.join(client.stderr)
    with Client(config) as client:
        client.tool('forge_discover')
        process.kill()
        process.wait(timeout=3)
        failed(client.tool('forge_discover'), 'connection_unavailable')


# Stall a real socket to prove cancellation and EOF interrupt an in-flight request.
@contextmanager
def stalled_peer():
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(5)
    accepted = threading.Event()
    release = threading.Event()

    def serve():
        try:
            channel, _ = listener.accept()
            with channel, channel.makefile('rb') as reader:
                reader.readline(1048577)
                accepted.set()
                release.wait(timeout=5)
        except OSError:
            pass

    worker = threading.Thread(target=serve)
    worker.start()
    try:
        yield dict(transport=1, host='127.0.0.1', port=listener.getsockname()[1], token='a' * 64), accepted
    finally:
        release.set()
        listener.close()
        worker.join(timeout=6)
        assert not worker.is_alive()


with stalled_peer() as (config, accepted), Client(config) as client:
    pending = client.send('tools/list')
    assert accepted.wait(timeout=3)
    queued = client.send('server/discover')
    client.notify('notifications/cancelled', dict(requestId=queued))
    client.notify('notifications/cancelled', dict(requestId=pending))
    ping = client.send('server/discover')
    assert client.receive(ping, timeout=3)['result']['resultType'] == 'complete'
    client.close()

with stalled_peer() as (config, accepted), Client(config) as client:
    client.send('tools/list')
    assert accepted.wait(timeout=3)
    client.close()

# Duplicate active IDs cannot replace another request's cancellation/correlation state.
with stalled_peer() as (config, accepted), Client(config) as client:
    pending = client.send('tools/list')
    assert accepted.wait(timeout=3)
    client.next_id = pending - 1
    client.send('server/discover')
    assert client.process.wait(timeout=4) == 0

# Excessive pipelining closes the bridge instead of retaining unbounded work.
with stalled_peer() as (config, accepted), Client(config) as client:
    client.send('tools/list')
    assert accepted.wait(timeout=3)
    for _ in range(9):
        client.send('server/discover')
    assert client.process.wait(timeout=4) == 0

for config in ({}, dict(transport=1, host='example.com', port=80, token='a' * 64),
               dict(transport=1, host='127.0.0.1', port=65536, token='a' * 64)):
    result = subprocess.run([TOOLS, '--mcp-stdio'], input='', capture_output=True, text=True,
                            timeout=5, env=dict(os.environ, FORGE_AUTOMATION_CONNECTION=json.dumps(config)))
    assert result.returncode == 2 and not result.stdout
    assert 'a' * 64 not in result.stderr

if len(sys.argv) > 3:
    Path(sys.argv[3]).write_text(json.dumps(CAPTURE, indent=2), encoding='utf-8')
print('Native MCP: current/legacy protocol, shared schemas, scene parity, transactions, history, '
      'replay, permissions, malformed input, cancellation, EOF and secret redaction passed')
