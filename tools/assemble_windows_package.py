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


def assemble(editor, sdk, output, runtime_kit, reference_game, physics_loads=None,
             physics_velocity=None, physics_contacts=None, physics_volumes=None, movement_2d=None,
             character_2d=None):
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
    root = Path(reference_game)
    paths = list(root.rglob('*'))
    if root.is_symlink() or any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in paths):
        raise ValueError('Reference game contains a nonregular entry')
    game = {safe_name(p.relative_to(root).as_posix()): p.read_bytes()
            for p in paths if p.is_file()}
    game_manifest = json.loads(game['forge.standalone.json'])
    if game_manifest.get('format') != 'forge.standalone' or game_manifest.get('version') != 1:
        raise ValueError('Invalid reference game manifest')
    if game_manifest['engine'] != kit['engine'] or game_manifest['target'] != kit['target']:
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
    for folder, source in (('PhysicsLoads', physics_loads), ('PhysicsVelocity', physics_velocity),
                           ('PhysicsContacts', physics_contacts), ('PhysicsVolumes', physics_volumes),
                           ('Movement2D', movement_2d), ('Character2D', character_2d)):
        if not source:
            continue
        root = Path(source)
        paths = list(root.rglob('*'))
        if root.is_symlink() or any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in paths):
            raise ValueError('C++ example contains a nonregular entry: ' + folder)
        example = {safe_name(p.relative_to(root).as_posix()): p.read_bytes()
                   for p in paths if p.is_file()}
        settings = json.loads(example['forge.project.json'])
        module, = settings['modules']
        expected = engine['sdk_fingerprint']
        if module['id'] != 'project.gameplay' or module['fingerprint'] != expected:
            raise ValueError('C++ example SDK mismatch')
        directory = PurePosixPath(safe_name(module['library'])).parent
        deployment = json.loads(example[str(directory / 'forge.module-kit.json')])
        if deployment['fingerprint'] != expected or deployment['library'] != PurePosixPath(module['library']).name:
            raise ValueError('C++ example deployment mismatch')
        for name, record in deployment['files'].items():
            data = example[str(directory / safe_name(name))]
            if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
                raise ValueError('C++ example module hash mismatch: ' + name)
        # Replace the uncompiled source example with the same ordinary project
        # plus matching deployment and admitted component metadata.
        prefix = 'Examples/' + folder + '/'
        for name in list(files):
            if name.startswith(prefix):
                files.pop(name)
        for name, value in example.items():
            files[prefix + name] = value
    for name, data in native.items():
        target = 'NativeSdk/' + name
        if target in files:
            raise ValueError('SDK namespace collision')
        files[target] = data
    # The final archive has one manifest covering every shipped file. Verify
    # the internal editor and SDK artifacts before combining their contents.
    if 'Run-Forge-Dev.cmd' in files:
        raise ValueError('Development launcher path collision')
    files['Run-Forge-Dev.cmd'] = (Path(__file__).resolve().parents[1] /
                                  'tools/Run-Forge-Dev.cmd').read_text().replace('\n', '\r\n').encode()
    files.pop('manifest.json')
    manifest['files'] = {name: hashlib.sha256(data).hexdigest()
                         for name, data in sorted(files.items())}
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
    print(f'Verified complete Windows package {len(files)-1} files: {output}')



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-kit', type=Path, required=True)
    parser.add_argument('--reference-game', type=Path, required=True)
    parser.add_argument('--physics-loads', type=Path)
    parser.add_argument('--physics-velocity', type=Path)
    parser.add_argument('--physics-contacts', type=Path)
    parser.add_argument('--physics-volumes', type=Path)
    parser.add_argument('--movement-2d', type=Path)
    parser.add_argument('--character-2d', type=Path)
    args = parser.parse_args()
    assemble(args.editor, args.sdk, args.output, args.runtime_kit, args.reference_game,
             args.physics_loads, args.physics_velocity, args.physics_contacts, args.physics_volumes,
             args.movement_2d, args.character_2d)
