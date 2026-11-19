"""Build navigation clients from installed headers and run a relocated SDK host."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from navigation_area_sdk_test import verify
if sys.argv[6:] == ['--rebuild']:
    from navigation_rebuild_sdk_test import verify


build, cmake, ninja, prepare, worker = [Path(p).resolve() for p in sys.argv[1:6]]
gallery_rebuild = sys.argv[6:] == ['--rebuild-gallery']
rebuild = sys.argv[6:] == ['--rebuild'] or gallery_rebuild
sample = ('navigation_rebuild_gallery.cpp' if gallery_rebuild else
          'navigation_rebuild_probe.cpp' if rebuild else 'navigation_area_probe.cpp')
target = ('navigation_rebuild_gallery_gameplay' if gallery_rebuild else
          'navigation_rebuild_gameplay' if rebuild else 'navigation_area_gameplay')
source = Path(__file__).resolve().parents[1]
cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(value for key, value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='navigation-area-sdk-', dir=build) as temporary:
    root = Path(temporary)
    installed, client, moved = root / 'installed SDK', root / 'client build', root / 'relocated SDK'
    subprocess.run([cmake, '--install', build, '--prefix', installed, '--component', 'NativeSdk'],
                   check=True, timeout=120)
    for name in ('native_sdk.h', 'sdk_client.hpp', 'navigation_components.hpp'):
        assert (installed / 'sdk/include/forge' / name).read_bytes() == (source / 'include/forge' / name).read_bytes()
    assert (installed / ('sdk/sample/' + sample)).read_bytes() == (
        source / 'samples/native_sdk' / sample).read_bytes()
    assert not (installed / 'sdk/include/forge/assets.hpp').exists()
    if gallery_rebuild:
        assert (installed / 'sdk/sample/RuntimeNavigationRebuildSystem.cpp').read_bytes() == (
            source / 'samples/projects/FeatureGallery/CodeExamples/RuntimeNavigationRebuildSystem.cpp').read_bytes()
    subprocess.run([cmake, '-S', installed / 'sdk/sample', '-B', client, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + compiler,
        '-DCMAKE_MAKE_PROGRAM=' + str(ninja), '-DFORGE_NATIVE_SDK=' + str(installed)],
        check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', target,
                    '--parallel', os.environ.get('FORGE_SDK_TEST_JOBS', '2')], check=True, timeout=120)
    windows = os.name == 'nt'
    module = client / (target + ('.dll' if windows else '.so'))
    if not windows:
        imports = subprocess.check_output(['readelf', '-d', module], text=True)
        exports = subprocess.check_output(['nm', '-D', '--defined-only', module], text=True)
        assert 'libflecs.so.4' in imports and ' ecs_init' not in exports
    installed.rename(moved)
    assert not installed.exists()
    runtime = moved / ('bin/forge_runtime' + ('.exe' if windows else ''))
    assert (moved / ('bin/forge_nav_build' + ('.exe' if windows else ''))).is_file()
    native_env = os.environ.copy()
    if not windows:
        # Keep the compiler runtime if this host needs one; never resolve FORGE
        # shared libraries from the original build or product source checkout.
        retained = [path for path in native_env.get('LD_LIBRARY_PATH', '').split(os.pathsep)
                    if path and not Path(path).resolve().is_relative_to(build) and
                    not Path(path).resolve().is_relative_to(source)]
        native_env['LD_LIBRARY_PATH'] = os.pathsep.join([str(moved / 'bin'), *retained])
    if gallery_rebuild:
        from navigation_rebuild_gallery_sdk_test import verify as verify_gallery
        verify_gallery(runtime, module, source / 'samples/projects/FeatureGallery',
                       root / 'projects', env=native_env)
    else:
        for planar in ((False,) if rebuild else (False, True)):
            verify(runtime, module, prepare, worker, root / 'projects', planar, env=native_env)
print('Installed-only policy client: relocated SDK, both planes and four graceful native hosts passed')
