"""Headless native tools and loopback REST lifecycle/admission."""
import json
import socket
import subprocess
import sys
import threading
import time
from urllib.request import urlopen

process = subprocess.Popen([sys.argv[1], '--ecs-stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
def call(operation, **values):
    process.stdin.write(json.dumps(dict(operation=operation, **values)) + '\n')
    process.stdin.flush()
    line = process.stdout.readline()
    assert line, 'Tools terminated unexpectedly'
    return json.loads(line)
try:
    stats = call('statistics')
    assert stats['ok'] and stats['result']['memory_bytes'] > 0, stats
    exported = call('world.export')
    assert exported['ok'], exported
    query = call('query', expression='*', limit=10)
    assert query['ok'], query
    # Release a probe socket before binding the tested native listener.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    stopped = call('rest.request', method='GET', path='/world')
    assert not stopped['ok'], stopped
    started = call('rest.start', port=port)
    assert started['ok'], started
    for method, path in [('PUT', '/entity/ShouldNotExist'), ('DELETE', '/entity/flecs'), ('PUT', '/script/x'), ('GET', '/commands/capture')]:
        denied = call('rest.request', method=method, path=path)
        assert denied['ok'] and denied['result']['status'] == 403, denied
    native = call('rest.request', method='GET', path='/entity/flecs')
    assert native['ok'] and native['result']['status'] == 200, native
    received = []
    def get():
        try:
            with urlopen(f'http://127.0.0.1:{port}/entity/flecs', timeout=5) as response:
                received.append(response.status)
        except Exception as error:
            received.append(str(error))
    thread = threading.Thread(target=get)
    thread.start()
    deadline = time.monotonic() + 6
    while thread.is_alive() and time.monotonic() < deadline:
        call('poll')
        time.sleep(.01)
    thread.join(timeout=1)
    assert received == [200], received
    assert call('rest.stop')['ok']
    assert not call('rest.request', method='GET', path='/world')['ok']
    assert call('rest.start', port=port)['ok'], 'Port was not released'
    assert call('rest.stop')['ok']
    valid = call('world.import', document={'results': [{'name': 'InspectionProbe', 'components': {'forge\\.local_translation': {'x': 1, 'y': 2, 'z': 3}}}]})
    assert valid['ok'], valid
    inspected = call('query', expression='forge\\.local_translation')
    assert inspected['ok'] and inspected['result']['results'][0]['name'] == 'InspectionProbe', inspected
    before = inspected['result']
    bad = call('world.import', document={'results': 'invalid'})
    assert not bad['ok'], bad
    assert call('statistics')['ok'], 'Failed import damaged previous world'
    assert call('query', expression='forge\\.local_translation')['result'] == before, 'Failed import altered previous content'
finally:
    process.stdin.close()
    try:
        code = process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise
    assert code == 0, process.stderr.read()
print('ECS query/JSON/stats and native loopback REST lifecycle/read-only admission passed')
