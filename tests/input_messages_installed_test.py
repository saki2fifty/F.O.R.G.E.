"""Compile the original receiver with a relocated public SDK and run both hosts."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from input_messages_process_test import verify

build, cmake, ninja, gallery = (Path(value).resolve() for value in sys.argv[1:5])
cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(value for key, value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='input-messages-sdk-', dir=build) as folder:
    root = Path(folder)
    stage, moved, client = root / 'installed SDK', root / 'relocated SDK', root / 'client build'
    subprocess.run([cmake, '--install', build, '--prefix', stage, '--component', 'NativeSdk'],
                   check=True, timeout=120)
    assert (stage / 'sdk/include/forge/input_message.hpp').read_bytes() == (
        gallery.parents[2] / 'include/forge/input_message.hpp').read_bytes()
    assert (stage / 'sdk/sample/InputMessages.cpp').read_bytes() == (
        gallery / 'CodeExamples/InputMessages.cpp').read_bytes()
    stage.rename(moved)
    subprocess.run([cmake, '-S', moved / 'sdk/sample', '-B', client, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=' + cache['CMAKE_BUILD_TYPE:STRING'],
                    '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                    '-DFORGE_NATIVE_SDK=' + str(moved)], check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', 'input_messages_gameplay', '--parallel', '4'],
                   check=True, timeout=120)
    module = client / ('input_messages_gameplay' + ('.dll' if os.name == 'nt' else '.so'))
    if os.name != 'nt':
        dynamic = subprocess.check_output(['readelf', '-d', module], text=True)
        exports = subprocess.check_output(['nm', '-D', '--defined-only', module], text=True)
        assert 'libflecs.so.4' in dynamic and ' ecs_init' not in exports
    # The launched runtime must resolve its installed libraries after relocation,
    # without borrowing the developer cache from this test driver's environment.
    os.environ.pop('LD_LIBRARY_PATH', None)
    os.environ.pop('LD_PRELOAD', None)
    os.environ['PATH'] = (str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32')
                          if os.name == 'nt' else '/usr/bin:/bin')
    verify(moved / ('bin/forge_runtime' + ('.exe' if os.name == 'nt' else '')), module, gallery)
print('Relocated public SDK Input Message header/shared receiver/single Flecs/both hosts passed')
