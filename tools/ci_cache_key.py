"""Fingerprint the Windows editor build environment, not the product source files."""
import hashlib
import os
from pathlib import Path
import subprocess


def cache_key(root, environment):
    digest = hashlib.sha256()
    # CMake/Ninja caches contain absolute paths and compiler/SDK-specific objects.
    for name in ('ImageVersion', 'VCToolsVersion', 'WindowsSDKVersion', 'RUNNER_ARCH'):
        value = environment.get(name)
        if not value:
            raise RuntimeError(f'Missing runner/toolchain identity: {name}')
        digest.update(f'{name}={value}\n'.encode())
    digest.update(str(root.resolve()).encode())
    for command in (['cmake', '--version'], ['ninja', '--version']):
        digest.update(subprocess.check_output(command))
    for path in sorted([root/'CMakeLists.txt', root/'CMakePresets.json', *root.glob('cmake/*.cmake')]):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(path.read_bytes())
    return 'forge-editor-release-v1-' + digest.hexdigest()


if __name__ == '__main__':
    key = cache_key(Path(__file__).resolve().parents[1], os.environ)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('key=' + key + '\n')
    print('Windows build cache identity:', key)
