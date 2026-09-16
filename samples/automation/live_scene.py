"""FORGE live authoring example. Python 3 standard library; no project-file writes."""
import argparse
import getpass
import json
import socket
import sys
import uuid


class Connection:
    def __init__(self, config):
        if config.get('transport') != 1 or config.get('host') != '127.0.0.1':
            raise ValueError('Expected FORGE transport 1 on 127.0.0.1')
        self.config = config
        self.next_sequence = 1

    def request(self, request, edit=False):
        envelope = {'transport': 1, 'token': self.config['token'],
                    'id': uuid.uuid4().hex, 'request': request}
        if edit:
            envelope['sequence'] = self.next_sequence
        wire = (json.dumps(envelope, ensure_ascii=False) + '\n').encode('utf-8')
        if len(wire) > 1048577:
            raise ValueError('Request exceeds 1 MiB')
        # Never automatically retry a mutation: a lost response may follow a commit.
        with socket.create_connection(('127.0.0.1', int(self.config['port'])), timeout=12) as channel:
            channel.sendall(wire)
            with channel.makefile('rb') as reader:
                raw = reader.readline(16777217)
        if not raw.endswith(b'\n') or len(raw) > 16777216:
            raise RuntimeError('Incomplete or oversized response; inspect the editor before retrying')
        response = json.loads(raw)
        if response.get('transport') != 1 or response.get('id') != envelope['id']:
            raise RuntimeError('Response identity mismatch')
        self.next_sequence = response.get('next_sequence', self.next_sequence)
        result = response['response']
        if not result.get('ok'):
            raise RuntimeError(json.dumps(result.get('error')))
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--add-example', action='store_true', help='Add four shapes in one undo step')
    parser.add_argument('--stdio', action='store_true', help='Read connection JSON from stdin for scripted use')
    args = parser.parse_args()
    config = json.loads(sys.stdin.readline() if args.stdio else
                        getpass.getpass('Paste connection JSON (hidden): '))
    connection = Connection(config)
    discovery = connection.request({'api': 1, 'method': 'discover'})
    target = discovery['result']['target']
    if args.add_example:
        commands = [{'operation': 'entity.create', 'arguments': {
            'kind': kind, 'name': 'Automation ' + name,
            'position': {'x': kind * 2.5, 'y': 0 if kind == 3 else 1, 'z': 0}}}
            for kind, name in enumerate(('Cube', 'Sphere', 'Cylinder', 'Plane'))]
        connection.request({'api': 1, 'method': 'scene.apply', 'target': target,
                            'expected_revision': discovery['revision'], 'commands': commands}, edit=True)
        print('Added four shapes. Undo once removes this batch; Save in the editor keeps it.')
    diagnostics = connection.request({'api': 1, 'method': 'scene.diagnostics', 'target': target})
    print(json.dumps(diagnostics, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
