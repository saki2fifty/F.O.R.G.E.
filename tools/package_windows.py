"""Package an MSVC Release build and its adjacent runtime DLLs for Windows x64."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import struct
import tempfile
import zipfile


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
    cache = (build/'CMakeCache.txt').read_text()
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise ValueError('A Release build is required; Debug CRT binaries are not distributable')
    images = [build/'forge_editor.exe', build/'forge_runtime.exe']
    dlls = sorted(build.glob('*.dll'))
    if not any('graphicsengined3d12' in p.name.lower() for p in dlls):
        raise ValueError('The Diligent D3D12 runtime DLL is missing from the build output')
    images += dlls
    for image in images:
        check_pe64(image)
    source_names = ('flecs-src', 'json-src', 'sdl-src', 'imgui_source-src', 'diligent-src')
    notices = []
    for name in source_names:
        source = dependencies/name
        if not source.is_dir():
            raise ValueError(f'Dependency sources required for notices: {source}')
        matches = [p for p in source.rglob('*') if p.is_file() and '.git' not in p.parts
                   and ('license' in p.name.lower() or p.name.lower().startswith('copying'))]
        if not matches:
            raise ValueError(f'No license notice found in {source}')
        notices.extend((p, 'licenses/'+name+'/'+p.relative_to(source).as_posix()) for p in matches)
    manifest = {'architecture': 'windows-x64', 'configuration': 'Release', 'files': {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in images}}
    source = Path(__file__).resolve().parents[1]
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
            for image in images:
                archive.write(image, image.name)
            for notice, name in notices:
                archive.write(notice, name)
            for relative in sdk_files:
                archive.write(source/relative, 'sdk/'+relative)
            archive.writestr('Run-Forge-Dev.cmd', launcher)
            archive.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
            archive.writestr('Run-Forge.cmd', '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\nif not exist "Project" mkdir "Project"\r\nforge_editor.exe "%~dp0Project"\r\nset "FORGE_EXIT=%ERRORLEVEL%"\r\nif not "%FORGE_EXIT%"=="0" (\r\n  echo FORGE exited with code %FORGE_EXIT%.\r\n  pause\r\n)\r\nexit /b %FORGE_EXIT%\r\n')
            archive.writestr('README.txt', 'FORGE Windows x64 development build\n\nExtract the ENTIRE archive. Keep all DLLs beside forge_editor.exe.\nRun Run-Forge.cmd to open the editor with a scratch Project directory\nand retain console output if the editor exits with an error.\nRequires Windows 10/11 x64 and a D3D12-capable graphics driver.\nFor gameplay compilation, use Run-Forge-Dev.cmd with Visual Studio 2022 C++ tools,\nCMake 3.24+ and Ninja installed. In Native: Create source, Build & Reload, then Play.\nThis is the editor foundation, not a finished game engine.\nThe build is produced on Windows CI; real GPU execution requires your PC.\n')
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
