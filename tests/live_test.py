"""Exercise real loopback sockets and process-held project locks on both target OSes."""
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

HOST = str(Path(sys.argv[1]).resolve())
SAMPLE = Path(__file__).resolve().parents[1] / 'samples/automation/live_scene.py'
spec = importlib.util.spec_from_file_location('forge_example', SAMPLE)
example = importlib.util.module_from_spec(spec)
spec.loader.exec_module(example)


@contextmanager
def host(mode):
    process = subprocess.Popen([HOST, mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline()
        assert line, process.stderr.read()
        yield json.loads(line)
    finally:
        process.kill()
        process.communicate(timeout=5)


def wire(config, request, sequence=None, token=None, request_id='test'):
    result = dict(transport=1, token=config['token'] if token is None else token,
                  id=request_id, request=request)
    if sequence is not None:
        result['sequence'] = sequence
    return result


def exchange(config, envelope, fragment=False):
    raw = (json.dumps(envelope) + '\n').encode()
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        if fragment:
            channel.sendall(raw[:9])
            time.sleep(.01)
            channel.sendall(raw[9:])
        else:
            channel.sendall(raw)
        with channel.makefile('rb') as reader:
            result = json.loads(reader.readline(16777217))
    assert result['id'] == envelope['id']
    return result


def error(response, code):
    assert not response['response']['ok'] and response['response']['error']['code'] == code, response


with host('edit') as config:
    discovery = exchange(config, wire(config, dict(api=1, method='discover')), fragment=True)
    target = discovery['response']['result']['target']
    revision = discovery['response']['revision']
    assert discovery['response']['result']['mutation_available']
    apply = dict(api=1, method='scene.apply', target=target, expected_revision=revision,
                 commands=[dict(operation='entity.create', arguments=dict(kind=0, name='Live cube'))])
    error(exchange(config, wire(config, apply, 1, token='wrong')), 'unauthorized')
    applied = exchange(config, wire(config, apply, 1))
    assert applied['response']['ok'] and applied['next_sequence'] == 2
    duplicate = exchange(config, wire(config, apply, 1, request_id='retry'))
    assert duplicate['response'] == applied['response']
    changed_request = dict(apply, method='history.undo')
    error(exchange(config, wire(config, changed_request, 1)), 'sequence_conflict')
    error(exchange(config, wire(config, apply, 3)), 'invalid_sequence')
    error(exchange(config, wire(config, apply, 2)), 'stale_revision')
    error(exchange(config, wire(config, dict(api=1, method='scene.read', target={}), 3)), 'wrong_target')
    error(exchange(config, wire(config, dict(api=1, method='native.run'))), 'unknown_method')
    read_request = dict(api=1, method='scene.read', target=target)
    read = exchange(config, wire(config, read_request))['response']
    assert len(read['result']['entities']) == 1
    # A complete request still commits if the client loses its response. Retrying
    # its sequence retrieves the receipt and must not undo a second time.
    undo = dict(api=1, method='history.undo', target=target, expected_revision=read['revision'])
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        channel.sendall((json.dumps(wire(config, undo, 3))+'\n').encode())
    time.sleep(.03)
    receipt = exchange(config, wire(config, undo, 3))
    assert receipt['response']['ok']
    assert not exchange(config, wire(config, read_request))['response']['result']['entities']
    # A partially transmitted request cannot change authored state.
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        channel.sendall(b'{"transport":1,')
    time.sleep(.02)
    assert exchange(config, wire(config, read_request))['response']['ok']
    for raw in (b'{bad\n', b'['*70 + b'0' + b']'*70 + b'\n'):
        with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
            channel.sendall(raw)
            with channel.makefile('rb') as reader:
                error(json.loads(reader.readline()), 'invalid_request')
    # Input cap closes an abusive request; the listener remains usable.
    with socket.create_connection(('127.0.0.1', config['port']), timeout=5) as channel:
        try:
            channel.sendall(b'x' * (1048576 + 2))
            assert not channel.recv(1)
        except (ConnectionResetError, BrokenPipeError):
            pass
    assert exchange(config, wire(config, read_request))['response']['ok']
    # Expired receipts never re-execute, even after undo returns to similar content.
    for sequence in range(4, 38):
        current = exchange(config, wire(config, read_request))['response']
        noop = dict(api=1, method='history.undo', target=target, expected_revision=current['revision'])
        assert exchange(config, wire(config, noop, sequence))['response']['ok']
    error(exchange(config, wire(config, apply, 1)), 'receipt_expired')
    client = example.Connection(config)
    assert client.request(dict(api=1, method='discover'))['ok']
    # Exercise the actual shipped client, with no credentials on its command line.
    sample = subprocess.run([sys.executable, str(SAMPLE), '--stdio', '--add-example'],
                            input=json.dumps(config)+'\n', text=True, capture_output=True, timeout=10)
    assert sample.returncode == 0, sample.stderr
    final = exchange(config, wire(config, read_request))['response']
    assert len(final['result']['entities']) == 4
    undo = dict(api=1, method='history.undo', target=target, expected_revision=final['revision'])
    assert exchange(config, wire(config, undo, 39))['response']['ok']
    assert not exchange(config, wire(config, read_request))['response']['result']['entities']

for mode, code in [('read', 'capability_denied'), ('busy', 'busy')]:
    with host(mode) as config:
        discovery = exchange(config, wire(config, dict(api=1, method='discover')))
        data = discovery['response']
        assert not data['result']['mutation_available']
        if mode == 'read':
            assert data['result']['capabilities'] == ['scene.read'] and not data['result']['commands']
        request = dict(api=1, method='scene.apply', target=data['result']['target'],
                       expected_revision=data['revision'],
                       commands=[dict(operation='entity.create', arguments=dict(kind=0))])
        error(exchange(config, wire(config, request, 1)), code)

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    owner = subprocess.Popen([HOST, '--lease', str(root)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        assert owner.stdout.readline().strip() == 'owned'
        denied = subprocess.run([HOST, '--lease', str(root)], input='\n', text=True, capture_output=True, timeout=5)
        assert denied.returncode == 1 and (
            'ownership' in denied.stderr.lower() or 'another FORGE' in denied.stderr), denied
        # Force process death: no stale PID heuristic or manual marker deletion.
        owner.kill()
        owner.communicate(timeout=5)
        recovered = subprocess.run([HOST, '--lease', str(root)], input='\n', text=True, capture_output=True, timeout=5)
        assert recovered.returncode == 0 and recovered.stdout.strip() == 'owned', recovered
        assert (root / '.forge/writer.lock').is_file()
    finally:
        if owner.poll() is None:
            owner.kill()
            owner.communicate(timeout=5)
print('Live transport, capabilities, revisions, retries, disconnects, bounds, client example and crash-released locks passed')
