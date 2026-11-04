"""Compile Wind values from a relocated installed SDK without private headers."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

build, cmake, ninja = (Path(value).resolve() for value in sys.argv[1:4])
source = Path(__file__).resolve().parents[1]
cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(value for key, value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
jobs = str(max(1, int(os.environ.get('FORGE_SDK_TEST_JOBS', '2'))))
with tempfile.TemporaryDirectory(prefix='wind-installed-sdk-', dir=build) as temporary:
    root = Path(temporary)
    stage, relocated, client = root / 'stage', root / 'relocated SDK', root / 'client'
    subprocess.run([cmake, '--install', build, '--prefix', stage, '--component', 'NativeSdk'], check=True)
    stage.rename(relocated)
    for name in ('mesh_wind.hpp', 'render_components.hpp', 'material_parameter.hpp'):
        installed = relocated / 'sdk/include/forge' / name
        assert installed.read_bytes() == (source / 'include/forge' / name).read_bytes(), name
    assert not (relocated / 'sdk/include/forge/assets.hpp').exists()
    client.mkdir()
    (client / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(WindInstalledClient LANGUAGES CXX)
find_package(ForgeNativeSdk CONFIG REQUIRED PATHS "${FORGE_NATIVE_SDK}/sdk" NO_DEFAULT_PATH)
add_executable(wind_client wind.cpp)
target_link_libraries(wind_client PRIVATE ForgeNativeSdk::Client)
''', encoding='utf-8')
    (client / 'wind.cpp').write_text('''#include <forge/mesh_wind.hpp>
#include <forge/render_components.hpp>
#include <forge/native_sdk_identity.h>
#include <iostream>
#include <type_traits>
int main() {
    static_assert(std::is_standard_layout_v<forge::MeshWind>);
    static_assert(std::is_trivially_copyable_v<forge::MeshWind>);
    static_assert(forge::mesh_wind_profile_version == 1);
    forge::MeshRenderer renderer;
    if (renderer.wind != forge::MeshWind{}) return 1;
    forge::validate_mesh_wind(renderer.wind);
    renderer.wind.enabled = true;
    renderer.wind.direction_x = 0;
    try { forge::validate_mesh_wind(renderer.wind); return 2; }
    catch (const std::runtime_error&) {}
    renderer.wind.amplitude = 0;
    forge::validate_mesh_wind(renderer.wind);
    renderer.wind.direction_z = .5;
    renderer.wind.amplitude = .75;
    forge::validate_mesh_wind(renderer.wind);
    std::cout << FORGE_NATIVE_SDK_FINGERPRINT << '\\n';
}
''', encoding='utf-8')
    configured = client / 'build'
    subprocess.run([cmake, '-S', client, '-B', configured, '-G', 'Ninja',
                    '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_MAKE_PROGRAM=' + str(ninja), '-DFORGE_NATIVE_SDK=' + str(relocated)], check=True)
    subprocess.run([cmake, '--build', configured, '--parallel', jobs], check=True)
    executable = configured / ('wind_client.exe' if os.name == 'nt' else 'wind_client')
    env = os.environ | {'LD_LIBRARY_PATH': str(relocated / 'bin') + ':' + os.environ.get('LD_LIBRARY_PATH', '')}
    fingerprint = subprocess.check_output([executable], env=env, text=True).strip()
    runtime = relocated / 'bin' / ('forge_runtime.exe' if os.name == 'nt' else 'forge_runtime')
    assert fingerprint == json.loads(subprocess.check_output([runtime, '--sdk-info'], env=env, text=True))['fingerprint']
    commands = (configured / 'build.ninja').read_text()
    assert str(source / 'include') not in commands and str(source / 'src') not in commands
    print(json.dumps(dict(scope='Installed-only, relocated SDK public Wind values, header closure and matching runtime identity',
                          fingerprint=fingerprint,
                          wind_header_sha256=hashlib.sha256((relocated / 'sdk/include/forge/mesh_wind.hpp').read_bytes()).hexdigest())))
