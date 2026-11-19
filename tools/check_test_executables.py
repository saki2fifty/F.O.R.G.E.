"""Check direct build-directory executables used by a configured CTest selection.

This prerequisite check does not execute tests or establish runtime acceptance.
"""
import argparse
import json
from pathlib import Path
import subprocess


def check_executables(document, build, name_prefix=''):
    build = build.resolve()
    tests = document['tests']
    if not tests:
        raise ValueError('No tests selected for executable verification')
    missing, executables = [], set()
    for test in tests:
        if not test['name'].startswith(name_prefix):
            raise ValueError('Test outside the required ' + name_prefix + ' scope: ' + test['name'])
        command = test.get('command')
        if not command:
            missing.append(test['name'] + ': native test command is unavailable')
            continue
        for argument in command:
            path = Path(argument)
            native = path.suffix.lower() == '.exe' or (
                not path.suffix and (path.name.startswith('forge_') or path.name == 'gltf2ozz'))
            if path.is_absolute() and path.parent.resolve() == build and native:
                executables.add(path)
                if not path.is_file():
                    missing.append(test['name'] + ': missing ' + path.name)
    if missing:
        raise ValueError('Incomplete native test prerequisites:\n' + '\n'.join(sorted(missing)))
    return dict(tests=len(tests), native_executables=len(executables))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--test-regex', required=True)
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--name-prefix', default='')
    args = parser.parse_args()
    document = json.loads(subprocess.check_output(
        [args.ctest, '--test-dir', str(args.build.resolve()), '--show-only=json-v1',
         '-R', args.test_regex], text=True))
    try:
        print(json.dumps(check_executables(document, args.build, args.name_prefix)))
    except ValueError as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
