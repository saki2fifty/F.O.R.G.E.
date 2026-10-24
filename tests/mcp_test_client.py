"""Shared black-box MCP test client; uses only Python's standard library."""
import gzip
import json
import os
from contextlib import contextmanager
from pathlib import Path
import queue
import subprocess
import sys
import threading

TOOLS = str(Path(sys.argv[1]))
CURRENT = '2026-07-28'
CAPTURE = []


def write_capture(path):
    """Stream compact evidence; large schema catalogs need no uncompressed copy."""
    path = Path(path)
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'wt', encoding='utf-8') as output:
        json.dump(CAPTURE, output, separators=(',', ':'))
        output.write('\n')


@contextmanager
def test_symlink(path, target, directory=False):
    try:
        path.symlink_to(target, target_is_directory=directory)
    except OSError as error:
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1314:
            raise
        print('Symlink case unavailable: this Windows account lacks link privilege')
        yield False
    else:
        try:
            yield True
        finally:
            path.unlink()


class Client:
    def __init__(self, config, legacy=False, tools=None):
        self.legacy = legacy
        self.session = len(CAPTURE)
        self.next_id = 0
        self.responses = queue.Queue()
        self.stderr = []
        self.process = subprocess.Popen(
            [str(tools) if tools else TOOLS, '--mcp-stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding='utf-8',
            env=dict(os.environ, FORGE_AUTOMATION_CONNECTION=json.dumps(config)))

        def read():
            for line in self.process.stdout:
                try:
                    self.responses.put(json.loads(line))
                except ValueError:
                    self.responses.put(dict(bad_stdout=line))

        def read_errors():
            self.stderr.extend(self.process.stderr)

        self.reader = threading.Thread(target=read)
        self.errors = threading.Thread(target=read_errors)
        self.reader.start()
        self.errors.start()
        if legacy:
            result = self.request('initialize', dict(
                protocolVersion='2025-11-25', capabilities={},
                clientInfo=dict(name='forge-tests', version='1')))
            assert result['result']['protocolVersion'] == '2025-11-25', result
            self.notify('notifications/initialized')

    def send(self, method, params=None):
        self.next_id += 1
        params = {} if params is None else dict(params)
        if not self.legacy:
            params['_meta'] = {
                'io.modelcontextprotocol/protocolVersion': CURRENT,
                'io.modelcontextprotocol/clientCapabilities': {}}
        request = dict(jsonrpc='2.0', id=self.next_id, method=method, params=params)
        self.raw(json.dumps(request))
        CAPTURE.append(dict(session=self.session, legacy=self.legacy, direction='request', message=request))
        return self.next_id

    def raw(self, wire):
        self.process.stdin.write(wire + '\n')
        self.process.stdin.flush()

    def receive(self, expected_id, timeout=8):
        response = self.responses.get(timeout=timeout)
        assert response.get('id') == expected_id, response
        assert response.get('jsonrpc') == '2.0', response
        CAPTURE.append(dict(session=self.session, legacy=self.legacy, direction='response', message=response))
        return response

    def request(self, method, params=None):
        return self.receive(self.send(method, params))

    def notify(self, method, params=None):
        self.raw(json.dumps(dict(jsonrpc='2.0', method=method, params=params or {})))

    def tool(self, tool_name, /, **arguments):
        response = self.request('tools/call', dict(name=tool_name, arguments=arguments))
        assert 'result' in response, response
        result = response['result']
        data = result['structuredContent']
        assert json.loads(result['content'][0]['text']) == data
        assert result['isError'] == (not data['ok'])
        return data

    def close(self):
        self.process.stdin.close()
        assert self.process.wait(timeout=4) == 0
        self.reader.join(timeout=3)
        self.errors.join(timeout=3)
        assert not self.reader.is_alive() and not self.errors.is_alive()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=4)
        self.reader.join(timeout=3)
        self.errors.join(timeout=3)


def failed(result, code):
    assert not result['ok'] and result['error']['code'] == code, result
