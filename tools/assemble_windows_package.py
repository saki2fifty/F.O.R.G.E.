"""Combine verified editor and exact SDK artifacts from the same source/build."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tarfile
import tempfile
import zipfile


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe archive path: ' + name)
    return path.as_posix()


def verified(files, manifest, manifest_name):
    declared = manifest['files']
    if set(files) != set(declared) | {manifest_name}:
        raise ValueError('Archive and manifest file sets differ')
    for name, digest in declared.items():
        if hashlib.sha256(files[name]).hexdigest() != digest:
            raise ValueError('File hash mismatch: ' + name)


def assemble(editor, sdk, output, runtime_kit=None):
    files = {}
    with zipfile.ZipFile(editor) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = safe_name(entry.filename)
            if name in files:
                raise ValueError('Duplicate editor archive path')
            files[name] = archive.read(entry)
    manifest = json.loads(files['manifest.json'])
    verified(files, manifest, 'manifest.json')
    native = {}
    with tarfile.open(sdk, 'r:gz') as archive:
        for entry in archive:
            if entry.isdir():
                continue
            if not entry.isfile():
                raise ValueError('Windows SDK archive must contain regular files only')
            name = safe_name(entry.name)
            if name in native:
                raise ValueError('Duplicate SDK archive path')
            native[name] = archive.extractfile(entry).read()
    sdk_manifest = json.loads(native['sdk-manifest.json'])
    if sdk_manifest.get('symlinks'):
        raise ValueError('Windows SDK cannot contain symlinks')
    verified(native, sdk_manifest, 'sdk-manifest.json')
    build = sdk_manifest['build']
    if any(build[key] != manifest[key] for key in ('source_commit', 'build_id')):
        raise ValueError('Editor/SDK source or build identity mismatch')
    if json.loads(native['build.json']) != build:
        raise ValueError('SDK build metadata mismatch')
    if 'bin/forge_runtime.exe' not in native or 'bin/flecs.dll' not in native:
        raise ValueError('Shared Windows SDK runtime is missing')
    if runtime_kit is not None:
        root = Path(runtime_kit)
        kit = json.loads((root/'forge.runtime-kit.json').read_text())
        if kit.get('format') != 'forge.runtime-kit' or kit.get('version') != 1:
            raise ValueError('Invalid shared graphical runtime kit')
        engine = kit['engine']
        if any(engine[key] != manifest[key] for key in ('source_commit', 'build_id')):
            raise ValueError('Graphical runtime/editor build identity mismatch')
        if engine['profile'] != 'shared-native-sdk':
            raise ValueError('Native SDK requires the shared graphical runtime')
        if kit['target'] != dict(platform='windows', backend='d3d12'):
            raise ValueError('Unexpected graphical runtime target')
        paths = list(root.rglob('*'))
        if any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in paths):
            raise ValueError('Runtime kit contains a nonregular entry')
        data = {safe_name(p.relative_to(root).as_posix()): p.read_bytes()
                for p in paths if p.is_file()}
        if set(data) != set(kit['files']) | {'forge.runtime-kit.json'}:
            raise ValueError('Runtime kit inventory mismatch')
        for name, record in kit['files'].items():
            if len(data[name]) != record['bytes'] or hashlib.sha256(data[name]).hexdigest() != record['sha256']:
                raise ValueError('Runtime kit hash mismatch: '+name)
        if data.get('flecs.dll') != native['bin/flecs.dll']:
            raise ValueError('Graphical runtime and SDK must use the same shared Flecs build')
        for name, value in data.items():
            target = 'runtime-kits/shared-native-sdk/'+name
            if target in files:
                raise ValueError('Shared runtime kit namespace collision')
            files[target] = value
            manifest['files'][target] = hashlib.sha256(value).hexdigest()
    for name, data in native.items():
        target = 'NativeSdk/' + name
        if target in files:
            raise ValueError('SDK namespace collision')
        files[target] = data
        manifest['files'][target] = hashlib.sha256(data).hexdigest()
    note = ('\nExact C++ gameplay SDK: NativeSdk/ contains the matching shared runtime,\n'
            'headers and samples. SDK projects use it automatically for Editor Play.\n'
            'Use the matching Visual Studio C++ toolchain/runtime described in its SDK docs.\n'
            'Stop Play, rebuild project code externally, then Play again.\n'
            'runtime-kits/shared-native-sdk contains the matching graphical game host for native gameplay exports.\n')
    files['README.txt'] += note.encode()
    manifest['files']['README.txt'] = hashlib.sha256(files['README.txt']).hexdigest()
    manifest['native_sdk'] = {'path': 'NativeSdk', 'source_commit': build['source_commit'],
                              'build_id': build['build_id']}
    files['manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, staging = tempfile.mkstemp(dir=output.parent, suffix='.pending')
    os.close(fd)
    try:
        with zipfile.ZipFile(staging, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        os.replace(staging, output)
    finally:
        Path(staging).unlink(missing_ok=True)
    print(f'Combined {len(files)-1} hashed files: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-kit', type=Path, required=True)
    args = parser.parse_args()
    assemble(args.editor, args.sdk, args.output, args.runtime_kit)
