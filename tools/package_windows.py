"""Package an MSVC Release build and its adjacent runtime DLLs for Windows x64."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import struct
import tempfile
import zipfile
from build_identity import validate_build_id
from build_manual import build_manual


def check_pe64(path):
    with path.open('rb') as stream:
        if stream.read(2) != b'MZ':
            raise ValueError(f'Not a Windows executable: {path}')
        stream.seek(0x3c)
        pointer = stream.read(4)
        if len(pointer) != 4:
            raise ValueError(f'Truncated executable: {path}')
        stream.seek(struct.unpack('<I', pointer)[0])
        if stream.read(6) != b'PE\0\0\x64\x86':
            raise ValueError(f'Not a Windows x64 PE image: {path}')


def package(build, dependencies, output):
    build, dependencies, output = map(Path, (build, dependencies, output))
    metadata = json.loads((build/'build.json').read_text())
    build_id = validate_build_id(metadata['build_id'])
    cache = (build/'CMakeCache.txt').read_text()
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise ValueError('A Release build is required; Debug CRT binaries are not distributable')
    images = [build/'forge_editor.exe', build/'forge_runtime.exe', build/'forge_tools.exe', build/'forge_nav_build.exe', build/'forge_asset_build.exe', build/'forge_shader_build.exe']
    dlls = sorted(build.glob('*.dll'))
    if not any('graphicsengined3d12' in p.name.lower() for p in dlls):
        raise ValueError('The Diligent D3D12 runtime DLL is missing from the build output')
    if not any(p.name.lower().startswith('archiver') for p in dlls):
        raise ValueError('The Diligent render-state Archiver runtime DLL is missing')
    images += dlls
    for image in images:
        check_pe64(image)
    converter = build/'tools/gltf2ozz.exe'
    check_pe64(converter)
    source_names = ('flecs-src', 'json-src', 'sdl-src', 'imgui_source-src', 'diligent-src', 'jolt-src', 'miniaudio-src', 'ozz-src', 'recast-src', 'rmlui-src', 'freetype-src', 'ktx-src', 'webp-src', 'meshoptimizer-src', 'draco-src')
    notices = []
    for name in source_names:
        source = dependencies/name
        if not source.is_dir():
            raise ValueError(f'Dependency sources required for notices: {source}')
        matches = [p for p in source.rglob('*') if p.is_file() and '.git' not in p.parts
                   and ('license' in p.name.lower() or p.name.lower().startswith('copying') or p.name.lower() == 'ftl.txt' or p.name.lower().startswith('notice') or p.name.lower() in ('patents', 'authors') or 'LICENSES' in p.relative_to(source).parts)]
        if not matches:
            raise ValueError(f'No license notice found in {source}')
        notices.extend((p, 'licenses/'+name+'/'+p.relative_to(source).as_posix()) for p in matches)
    manifest = {'build_id': build_id, 'source_commit': metadata['source_commit'], 'architecture': 'windows-x64', 'configuration': 'Release', 'files': {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in images}}
    manifest['files']['tools/gltf2ozz.exe'] = hashlib.sha256(converter.read_bytes()).hexdigest()
    source = Path(__file__).resolve().parents[1]
    notices.append((source/'docs/licenses/ozz-converter.txt', 'licenses/ozz-converter.txt'))
    for notice, name in notices:
        manifest['files'][name] = hashlib.sha256(notice.read_bytes()).hexdigest()
    ui_resources = sorted(p for p in (source/'resources/ui').rglob('*') if p.is_file())
    for resource in ui_resources:
        manifest['files']['resources/ui/'+resource.name] = hashlib.sha256(resource.read_bytes()).hexdigest()
    manual_output = build/'manual'
    # Render from current sources; stale removed pages cannot leak from a cached build.
    import shutil
    shutil.rmtree(manual_output, ignore_errors=True)
    build_manual(source/'manual', manual_output, build_id)
    manual_files = sorted(p for p in manual_output.rglob('*') if p.is_file())
    for page in manual_files:
        manifest['files']['manual/'+page.relative_to(manual_output).as_posix()] = hashlib.sha256(page.read_bytes()).hexdigest()
    manifest['files']['build.json'] = hashlib.sha256((build/'build.json').read_bytes()).hexdigest()
    example_root = source/'samples/projects'
    example_files = sorted(p for p in example_root.rglob('*') if p.is_file() and p.suffix in ('.json', '.wav', '.gltf', '.png', '.md'))
    for example in example_files:
        manifest['files']['Examples/'+example.relative_to(example_root).as_posix()] = hashlib.sha256(example.read_bytes()).hexdigest()
    animation_source = source/'samples/animation/two-joints.gltf'
    manifest['files']['Examples/Animation/two-joints.gltf'] = hashlib.sha256(animation_source.read_bytes()).hexdigest()
    automation_sources = sorted((source/'samples/automation').glob('*.py'))
    for automation_source in automation_sources:
        manifest['files']['Examples/Automation/'+automation_source.name] = hashlib.sha256(automation_source.read_bytes()).hexdigest()
    sdk_files = ('include/forge/module_api.h', 'samples/native/movement.c', 'samples/native/CMakeLists.txt')
    for relative in sdk_files:
        manifest['files']['sdk/'+relative] = hashlib.sha256((source/relative).read_bytes()).hexdigest()
    launcher = (source/'tools/Run-Forge-Dev.cmd').read_text().replace('\n', '\r\n')
    manifest['files']['Run-Forge-Dev.cmd'] = hashlib.sha256(launcher.encode()).hexdigest()
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=output.parent, suffix='.zip.pending')
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            def write_text(name, text):
                manifest['files'][name] = hashlib.sha256(text.encode()).hexdigest()
                archive.writestr(name, text)
            for resource in ui_resources:
                archive.write(resource, 'resources/ui/'+resource.name)
            archive.write(converter, 'tools/gltf2ozz.exe')
            archive.write(animation_source, 'Examples/Animation/two-joints.gltf')
            for image in images:
                archive.write(image, image.name)
            for notice, name in notices:
                archive.write(notice, name)
            for relative in sdk_files:
                archive.write(source/relative, 'sdk/'+relative)
            for example in example_files:
                archive.write(example, 'Examples/'+example.relative_to(example_root).as_posix())
            for automation_source in automation_sources:
                archive.write(automation_source, 'Examples/Automation/'+automation_source.name)
            archive.writestr('Run-Forge-Dev.cmd', launcher)
            archive.write(build/'build.json', 'build.json')
            for page in manual_files:
                archive.write(page, 'manual/'+page.relative_to(manual_output).as_posix())
            write_text('Run-Forge.cmd', '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\nif not exist "Project" mkdir "Project"\r\nforge_editor.exe "%~dp0Project"\r\nset "FORGE_EXIT=%ERRORLEVEL%"\r\nif not "%FORGE_EXIT%"=="0" (\r\n  echo FORGE exited with code %FORGE_EXIT%.\r\n  pause\r\n)\r\nexit /b %FORGE_EXIT%\r\n')
            write_text('README.txt', f'FORGE Windows x64 | Build: {build_id}\n\nOpen Help > User Manual or manual/index.html for offline instructions.\nOpen the Examples/Blockout project through File > Open project for a sample scene.\nOpen Examples/Rendering for the model/material/camera walkthrough.\n\nExtract the ENTIRE archive. Keep all DLLs beside forge_editor.exe.\nRun Run-Forge.cmd to open the editor with a scratch Project directory\nand retain console output if the editor exits with an error.\nRequires Windows 10/11 x64 and a D3D12-capable graphics driver.\nFor gameplay compilation, use Run-Forge-Dev.cmd with Visual Studio 2022 C++ tools,\nCMake 3.30+ and Ninja installed. In Gameplay Code: Create source, Build & Reload, then Play.\nThis is the editor foundation, not a finished game engine.\nThe build is produced on Windows CI; real GPU execution requires your PC.\n')
            archive.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    print(output.resolve())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--dependencies', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.build, args.dependencies, args.output)
