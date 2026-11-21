"""Exercise the host's real CMake profile extraction and install substitution."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root/'src/standalone_gpu_profile.hpp').read_text(encoding='utf-8')
version = int(re.search(r'inline constexpr unsigned standalone_gpu_profile_version = (\d+);',
                        header).group(1))
live_version = int(re.search(r'inline constexpr unsigned standalone_gpu_profile_live_version = (\d+);',
                             header).group(1))
source = (root/'cmake/game.cmake').read_text(encoding='utf-8')
start = source.index(' set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS '
                     '"${PROJECT_SOURCE_DIR}/src/standalone_gpu_profile.hpp")')
end = source.index(' set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS '
                   '"${PROJECT_SOURCE_DIR}/include/forge/game_settings.hpp")', start)
block = source[start:end]
with tempfile.TemporaryDirectory(prefix='forge-gpu-profile-metadata-') as temporary:
    work = Path(temporary)
    script = work/'check.cmake'
    observed = work/'observed.txt'
    configured = work/'install.cmake'
    script.write_text(
        f'set(PROJECT_SOURCE_DIR [==[{root.as_posix()}]==])\n' + block +
        f'\nfile(WRITE [==[{observed.as_posix()}]==] "${{FORGE_RUNTIME_GPU_PROFILING}};${{FORGE_RUNTIME_GPU_PROFILING_LIVE}}")\n' +
        'configure_file("${PROJECT_SOURCE_DIR}/cmake/runtime_kit_install.cmake.in" '
        f'[==[{configured.as_posix()}]==] @ONLY)\n', encoding='utf-8')
    subprocess.run([sys.argv[1], '-P', str(script)], check=True, capture_output=True, text=True)
    assert observed.read_text() == f'{version};{live_version}'
    installed = configured.read_text(encoding='utf-8')
    key = r'\"runtime_gpu_profiling\":' + str(version)
    assert installed.count(key) == 1
    assert '@FORGE_RUNTIME_GPU_PROFILING@' not in installed
    assert installed.count(r'\"runtime_gpu_profiling_live\":' + str(live_version)) == 1
    assert '@FORGE_RUNTIME_GPU_PROFILING_LIVE@' not in installed
print('Actual CMake source extraction and runtime-kit GPU profile substitution passed')
