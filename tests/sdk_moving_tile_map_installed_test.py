"""Build the Map profile consumer from installed headers, relocate, run both hosts."""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from sdk_moving_tile_map_test import verify

build, cmake, ninja = (Path(p).resolve() for p in sys.argv[1:4])
source = Path(__file__).resolve().parents[1]
cache = dict(line.split('=', 1) for line in (build/'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(v for k, v in cache.items() if k.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='map-profile-sdk-', dir=build) as temporary:
    root = Path(temporary)
    stage, client, moved = root/'installed SDK', root/'client build', root/'relocated SDK'
    subprocess.run([cmake, '--install', build, '--prefix', stage, '--component', 'NativeSdk'],
                   check=True, timeout=120)
    for guide in (stage/'sdk/docs').rglob('*.md'):
        for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', guide.read_text(encoding='utf-8')):
            path = target.split('#', 1)[0]
            if path and '://' not in path:
                linked = (guide.parent/path).resolve()
                assert linked.is_relative_to(stage/'sdk') and linked.is_file(), (guide, target)
    sample = stage/'sdk/sample/moving_tile_map_probe.cpp'
    assert sample.read_bytes() == (source/'samples/native_sdk/moving_tile_map_probe.cpp').read_bytes()
    assert not (stage/'sdk/include/forge/assets.hpp').exists()
    subprocess.run([cmake, '-S', stage/'sdk/sample', '-B', client, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+compiler,
                    '-DCMAKE_MAKE_PROGRAM='+str(ninja), '-DFORGE_NATIVE_SDK='+str(stage)],
                   check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', 'moving_tile_map_gameplay', '--parallel', '2'],
                   check=True, timeout=120)
    windows = os.name == 'nt'
    module = client/('moving_tile_map_gameplay'+('.dll' if windows else '.so'))
    if not windows:
        imports = subprocess.check_output(['readelf', '-d', module], text=True)
        exports = subprocess.check_output(['nm', '-D', '--defined-only', module], text=True)
        assert 'libflecs.so.4' in imports and ' ecs_init' not in exports
    stage.rename(moved)
    runtime = moved/('bin/forge_runtime'+('.exe' if windows else ''))
    verify(runtime, module, source/'samples/projects/FeatureGallery')
print('Relocated installed-only public Map profile consumer passed both actual hosts')
