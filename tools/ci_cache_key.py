"""Fingerprint the Windows editor build environment, not the product source files."""
import hashlib
import os
from pathlib import Path
import subprocess


def cache_key(root, environment, *, legacy=False):
    digest = hashlib.sha256()
    # CMake/Ninja caches contain absolute paths and compiler/SDK-specific objects.
    for name in ('ImageVersion', 'VCToolsVersion', 'WindowsSDKVersion', 'RUNNER_ARCH'):
        value = environment.get(name)
        if not value:
            raise RuntimeError(f'Missing runner/toolchain identity: {name}')
        digest.update(f'{name}={value}\n'.encode())
    digest.update(str(root.resolve()).encode())
    for name, default in (('FORGE_LINKAGE_PROFILE', 'static-core'), ('FORGE_CRT_PROFILE', 'MultiThreaded')):
        digest.update(f'{name}={environment.get(name, default)}\n'.encode())
    for command in (['cmake', '--version'], ['ninja', '--version']):
        digest.update(subprocess.check_output(command))
    # Editor consumer targets are always reconfigured by CMake. Their link/input
    # changes belong to Ninja; dependency declarations and environment stay keyed.
    # Vulkan probes explicitly require Linux and cannot affect this Windows cache.
    # Retain the previous identity as a restore fallback for existing cache entries.
    excluded = {'editor.cmake'} if legacy else {'editor.cmake', 'vulkan_probe.cmake'}
    fragments = [p for p in root.glob('cmake/*.cmake') if p.name not in excluded]
    for path in sorted([root/'CMakeLists.txt', root/'CMakePresets.json', *fragments, *root.glob('cmake/patches/*.patch'), *root.glob('cmake/patches/*.json'), root/'tools/stage_diligentfx_patch.py']):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(path.read_bytes())
    return 'forge-editor-release-v2-' + digest.hexdigest()


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    key = cache_key(root, os.environ)
    legacy = cache_key(root, os.environ, legacy=True)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('key=' + key + '\n')
        output.write('legacy_key=' + legacy + '\n')
    print('Windows build cache identity:', key)
