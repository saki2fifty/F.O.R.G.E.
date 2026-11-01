"""Build using installed public headers only, relocate, execute both hosts."""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from application_state_sdk_test import verify

build, cmake, ninja, tools, settings = map(lambda value: Path(value).resolve(), sys.argv[1:6])
cache = dict(line.split('=', 1) for line in (build/'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(value for key, value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='state-installed-sdk-', dir=build) as folder:
    root = Path(folder)
    stage, client, moved = root/'installed SDK', root/'client build', root/'relocated SDK'
    subprocess.run([cmake, '--install', build, '--prefix', stage, '--component', 'NativeSdk'], check=True, timeout=120)
    assert not (stage/'sdk/include/forge/application_states.hpp').exists()
    assert (stage/'sdk/include/forge/application_state_ecs.hpp').is_file()
    source_root = Path(__file__).resolve().parents[1]
    for document in ('application-states.md', 'decisions/030-application-states.md'):
        installed = stage/'sdk/docs'/document
        assert installed.read_bytes() == (source_root/'docs'/document).read_bytes()
        for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', installed.read_text(encoding='utf-8')):
            path = target.split('#', 1)[0]
            if path and '://' not in path:
                linked = (installed.parent/path).resolve()
                assert linked.is_relative_to(stage/'sdk') and linked.is_file(), (installed, target)
    subprocess.run([cmake, '-S', stage/'sdk/sample', '-B', client, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE='+cache['CMAKE_BUILD_TYPE:STRING'], '-DCMAKE_CXX_COMPILER='+compiler,
                    '-DCMAKE_MAKE_PROGRAM='+str(ninja), '-DFORGE_NATIVE_SDK='+str(stage)], check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', 'application_states_gameplay', '--parallel', '2'], check=True, timeout=120)
    module = client/('application_states_gameplay'+('.dll' if os.name == 'nt' else '.so'))
    if os.name != 'nt':
        dynamic = subprocess.check_output(['readelf', '-d', module], text=True)
        exports = subprocess.check_output(['nm', '-D', '--defined-only', module], text=True)
        assert 'libflecs.so.4' in dynamic and ' ecs_init' not in exports
    stage.rename(moved)
    verify(moved/('bin/forge_runtime'+('.exe' if os.name == 'nt' else '')), module, tools, settings, root/'cases')
print('Installed-only relocated SDK state consumer passed both actual runtime hosts')
