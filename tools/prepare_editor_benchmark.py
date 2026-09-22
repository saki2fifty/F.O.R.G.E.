"""Add a disposable, WARP-only editor target without changing product source.

Run against either the accepted baseline checkout or current checkout. Only device
creation is substituted, using that revision's existing native test adapter.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
root, out = a.source.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
original = (root / 'src/editor/main.cpp').read_text()
source, count = re.subn(r'factory->CreateDeviceAndContextsD3D12\(engine,\s*&device,\s*&context\);',
                       'forge::test::EditorFixture::device(factory, &device, &context);', original)
if count != 1:
    raise RuntimeError('Expected exactly one production device-creation boundary')
# No fixture macro: input, startup, documents, rendering and event loop are unchanged.
source = source.replace('int main(int argc, char** argv)',
                        '#include "editor_fixture.hpp"\nint main(int argc, char** argv)', 1)
(out / 'main.cpp').write_text(source)
(out / 'target.cmake').write_text(f'''
get_target_property(_benchmark_sources forge_editor SOURCES)
list(FILTER _benchmark_sources EXCLUDE REGEX "(^|/)main[.]cpp$")
add_executable(forge_editor_benchmark "{out.as_posix()}/main.cpp" ${{_benchmark_sources}})
foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES)
 get_target_property(_value forge_editor ${{_property}})
 set_property(TARGET forge_editor_benchmark PROPERTY ${{_property}} "${{_value}}")
endforeach()
target_include_directories(forge_editor_benchmark PRIVATE "${{PROJECT_SOURCE_DIR}}/tests")
target_link_libraries(forge_editor_benchmark PRIVATE d3d12 dxgi)
copy_required_dlls(forge_editor_benchmark)
''')
(out / 'inject.cmake').write_text(f'''
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
 cmake_language(DEFER CALL include "{out.as_posix()}/target.cmake")
endif()
''')
(out / 'provenance.json').write_text(json.dumps({
    'source': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
    'original_main_sha256': hashlib.sha256(original.encode()).hexdigest(),
    'benchmark_main_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'substitution': 'Existing revision-local EditorFixture::device WARP adapter only; FORGE_UI_FIXTURE undefined',
}, indent=2))
