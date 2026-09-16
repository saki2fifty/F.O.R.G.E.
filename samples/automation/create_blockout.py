"""Create a scene through FORGE's in-memory API and print the final scene JSON.
Usage: python create_blockout.py PATH/TO/forge_tools.exe
"""
import json
from pathlib import Path
import subprocess
import sys


def main():
    with subprocess.Popen([str(Path(sys.argv[1]).resolve()), '--stdio'], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True, encoding='utf-8') as process:
        def call(method, **fields):
            process.stdin.write(json.dumps(dict(api=1, method=method, **fields))+'\n')
            process.stdin.flush()
            response = json.loads(process.stdout.readline())
            if not response['ok']:
                raise RuntimeError(response['error'])
            return response
        try:
            info = call('discover')['result']
            target = info['target']
            loaded = call('scene.replace', target=target, expected_revision=info['revision'],
                          document=dict(version=1, entities=[]))
            commands = [dict(operation='entity.create', arguments=dict(
                kind=kind, name=name, position=dict(x=kind*3, y=0 if kind==3 else 1, z=0)))
                for kind, name in enumerate(['Cube', 'Sphere', 'Cylinder', 'Plane'])]
            call('scene.apply', target=target, expected_revision=loaded['revision'], commands=commands)
            print(json.dumps(call('scene.read', target=target)['result'], indent=2))
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    main()
