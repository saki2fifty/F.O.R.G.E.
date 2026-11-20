"""Select the existing package retry fixture path from approved changed inputs."""
import argparse
import os
from pathlib import Path, PurePosixPath
import re
import subprocess

# These helpers run as Python; none supplies a native fixture compilation input.
# Unknown test inputs remain conservative, including unfamiliar Python generators.
PACKAGE_HELPERS = frozenset({
    'tests/mcp_editor_package_test.py',
    'tests/mcp_editor_package_readiness_test.py',
    'tests/package_fixture_policy.py',
    'tests/package_fixture_policy_test.py',
})


def fixture_rebuild_required(paths):
    rebuild = False
    for name in paths:
        path = PurePosixPath(name)
        if not name or path.is_absolute() or '..' in path.parts or '\\' in name:
            raise ValueError('Package-only retry has a noncanonical changed path')
        if name.startswith('docs/') or name == '.github/workflows/build.yml':
            continue
        if not name.startswith('tests/'):
            raise ValueError('Package-only retry changed product inputs: ' + name)
        rebuild |= name not in PACKAGE_HELPERS
    return rebuild


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiled-source', required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.compiled_source):
        raise ValueError('Expected immutable compiled-source SHA')
    root = Path(__file__).resolve().parents[1]
    changes = subprocess.check_output(
        ['git', 'diff', '--name-only', '-z', args.compiled_source, 'HEAD'], cwd=root)
    paths = [name.decode('utf-8') for name in changes.split(b'\0') if name]
    rebuild = fixture_rebuild_required(paths)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('fixture_rebuild=' + str(rebuild).lower() + '\n')
    print('Native fixture rebuild required:', rebuild, '; approved changed paths:', len(paths))


if __name__ == '__main__':
    main()
