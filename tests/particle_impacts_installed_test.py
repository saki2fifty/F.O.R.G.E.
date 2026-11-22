"""Build impact clients only from a relocated exact SDK and run its native host."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from particle_impact_process_test import verify

build, cmake, ninja, fixture, gallery = (Path(value).resolve() for value in sys.argv[1:6])
source = Path(__file__).resolve().parents[1]
cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(value for key, value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='particle-impact-sdk-', dir=build) as temporary:
    root = Path(temporary)
    installed, moved, client = root / 'installed SDK', root / 'relocated SDK', root / 'client build'
    subprocess.run([cmake, '--install', build, '--prefix', installed, '--component', 'NativeSdk'],
                   check=True, timeout=120)
    for name in ('native_sdk.h', 'sdk_client.hpp'):
        assert (installed / 'sdk/include/forge' / name).read_bytes() == (source / 'include/forge' / name).read_bytes()
    for name in ('particle_impacts_probe.cpp', 'particle_impact_gallery.cpp'):
        assert (installed / 'sdk/sample' / name).read_bytes() == (source / 'samples/native_sdk' / name).read_bytes()
    assert (installed / 'sdk/sample/ParticleImpactTint.cpp').read_bytes() == (
        gallery / 'CodeExamples/ParticleImpactTint.cpp').read_bytes()
    assert not (installed / 'sdk/include/forge/particle_runtime_access.hpp').exists()
    installed.rename(moved)
    subprocess.run([cmake, '-S', moved / 'sdk/sample', '-B', client, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=' + cache['CMAKE_BUILD_TYPE:STRING'],
                    '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                    '-DFORGE_NATIVE_SDK=' + str(moved)], check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', 'particle_impacts_gameplay',
                    'particle_impacts_denied_gameplay', 'particle_impact_gallery_gameplay',
                    '--parallel', '4'], check=True, timeout=120)
    suffix = '.dll' if os.name == 'nt' else '.so'
    modules = [client / (name + suffix) for name in ('particle_impacts_gameplay',
               'particle_impacts_denied_gameplay', 'particle_impact_gallery_gameplay')]
    if os.name != 'nt':
        for module in modules:
            dynamic = subprocess.check_output(['readelf', '-d', module], text=True)
            exports = subprocess.check_output(['nm', '-D', '--defined-only', module], text=True)
            assert 'libflecs.so.4' in dynamic and ' ecs_init' not in exports
    # Use the SDK's shared Flecs, without a developer-cache loader search path.
    environment = os.environ.copy()
    environment.pop('LD_LIBRARY_PATH', None)
    environment.pop('LD_PRELOAD', None)
    environment['PATH'] = (str(Path(environment.get('SystemRoot', 'C:/Windows')) / 'System32')
                           if os.name == 'nt' else '/usr/bin:/bin')
    runtime = moved / ('bin/forge_runtime' + ('.exe' if os.name == 'nt' else ''))
    # The fixture's own build dependencies are independent; only the launched
    # installed runtime uses this stripped loader environment.
    original = os.environ.copy()
    try:
        os.environ.clear()
        os.environ.update(environment)
        verify(runtime, modules[2], fixture, gallery)
    finally:
        os.environ.clear()
        os.environ.update(original)
print('Relocated SDK impact clients: public headers, single Flecs and source-hidden native consumer passed')
