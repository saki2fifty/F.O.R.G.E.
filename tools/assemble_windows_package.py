"""Combine verified editor and exact SDK artifacts from the same source/build."""
import argparse
import hashlib
import json
import os
import shutil
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


def assemble(editor, sdk, output, developer_output, runtime_kit=None, reference_game=None):
    files = {}
    with zipfile.ZipFile(editor) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = safe_name(entry.filename)
            if name in files:
                raise ValueError('Duplicate editor archive path')
            files[name] = archive.read(entry)
    editor_files = dict(files)
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
    if reference_game is not None:
        root = Path(reference_game)
        paths = list(root.rglob('*'))
        if root.is_symlink() or any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in paths):
            raise ValueError('Reference game contains a nonregular entry')
        game = {safe_name(p.relative_to(root).as_posix()): p.read_bytes()
                for p in paths if p.is_file()}
        game_manifest = json.loads(game['forge.standalone.json'])
        if game_manifest.get('format') != 'forge.standalone' or game_manifest.get('version') != 1:
            raise ValueError('Invalid reference game manifest')
        if runtime_kit is None or game_manifest['engine'] != kit['engine'] or game_manifest['target'] != kit['target']:
            raise ValueError('Reference game/runtime build identity mismatch')
        if game_manifest['executable'] != 'forge_game.exe' or 'forge_game_fixture.exe' in game:
            raise ValueError('Reference delivery must use the production game executable')
        if set(game) != set(game_manifest['files']) | {'forge.standalone.json'}:
            raise ValueError('Reference game inventory mismatch')
        for name, record in game_manifest['files'].items():
            if len(game[name]) != record['bytes'] or hashlib.sha256(game[name]).hexdigest() != record['sha256']:
                raise ValueError('Reference game hash mismatch: ' + name)
        if game.get('flecs.dll') != native['bin/flecs.dll'] or game['forge_game.exe'] != (Path(runtime_kit)/'forge_game.exe').read_bytes():
            raise ValueError('Reference game must use the matching runtime binaries')
        for name, value in game.items():
            target = 'ReferenceGame/' + name
            if target in files:
                raise ValueError('Reference game namespace collision')
            files[target] = value
        manifest['reference_game'] = {'path': 'ReferenceGame', 'executable': 'forge_game.exe'}
    for name, data in native.items():
        target = 'NativeSdk/' + name
        if target in files:
            raise ValueError('SDK namespace collision')
        files[target] = data
    # The ordinary editor is exactly the prevalidated editor archive. The
    # matching SDK, reference game and shared host form a separate optional
    # overlay. Bind that overlay to the exact editor manifest and verify each
    # of its members before either artifact can be published.
    developer_files = {name: data for name, data in files.items() if name not in editor_files}
    note = ('FORGE Developer Kit | Build: ' + build['build_id'] + '\n\n'
            'Extract this ZIP into the root of the matching FORGE editor ZIP.\n'
            'Do not extract it into a nested folder. Then use Run-Forge-Dev.cmd\n'
            'for C++ gameplay or open ReferenceGame/forge_game.exe.\n'
            'NativeSdk/ and runtime-kits/shared-native-sdk/ are exact-version\n'
            'additions for gameplay modules and standalone export.\n')
    developer_files['Developer-README.txt'] = note.encode()
    developer_files['Run-Forge-Dev.cmd'] = (Path(__file__).resolve().parents[1] /
                                             'tools/Run-Forge-Dev.cmd').read_text().replace('\n', '\r\n').encode()
    developer_manifest = dict(build_id=build['build_id'],
                              source_commit=build['source_commit'],
                              editor_manifest_sha256=hashlib.sha256(
                                  editor_files['manifest.json']).hexdigest(),
                              files={name: hashlib.sha256(value).hexdigest()
                                     for name, value in developer_files.items()})
    developer_files['developer-manifest.json'] = (json.dumps(developer_manifest, indent=2)+'\n').encode()
    output = Path(output)
    developer_output = Path(developer_output)
    output.parent.mkdir(parents=True, exist_ok=True)
    developer_output.parent.mkdir(parents=True, exist_ok=True)
    fd, staging = tempfile.mkstemp(dir=developer_output.parent, suffix='.pending')
    os.close(fd)
    try:
        with zipfile.ZipFile(staging, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(developer_files.items()):
                archive.writestr(name, data)
        # The input editor archive passed hash verification above; retain its
        # exact bytes instead of rewriting its independent manifest.
        core_staging = output.with_suffix('.zip.pending')
        try:
            shutil.copy2(editor, core_staging)
            os.replace(staging, developer_output)
            os.replace(core_staging, output)
        finally:
            core_staging.unlink(missing_ok=True)
    finally:
        Path(staging).unlink(missing_ok=True)
    print(f'Verified editor {len(editor_files)-1} files: {output}')
    print(f'Verified optional developer kit {len(developer_files)-1} files: {developer_output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--developer-output', type=Path, required=True)
    parser.add_argument('--runtime-kit', type=Path, required=True)
    parser.add_argument('--reference-game', type=Path, required=True)
    args = parser.parse_args()
    assemble(args.editor, args.sdk, args.output, args.developer_output, args.runtime_kit, args.reference_game)
