"""Inspect actual final Ninja link commands for forbidden presentation dependencies."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def link(build, ninja, target):
 lines=subprocess.check_output([ninja,'-C',str(build),'-t','commands',target],text=True).splitlines()
 assert lines,'Missing target '+target
 return lines[-1].lower()


def libraries(command):
 # Inspect library filenames, not directories containing approved codec sources.
 # Match quoted paths too: Windows cmd wraps the complete link command in quotes.
 extension = r'\.(?:lib|a|dll|dylib|so(?:\.\d+)*)'
 matches = re.finditer(r'"([^"\n]+' + extension + r')"|([^\s"]+' +
                       extension + r')(?=[\s"]|$)', command.lower())
 names = [(m.group(1) or m.group(2)).replace('\\', '/').rsplit('/', 1)[-1]
          for m in matches]
 names += re.findall(r'(?:^|\s)-l([^\s"]+)', command.lower())
 assert names, ('No link libraries found', command)
 return ' '.join(names)


def verify_path_controls(ninja):
 # Query actual Ninja commands; no compiler or dummy library is executed.
 with tempfile.TemporaryDirectory(prefix='forge-link-boundaries-') as folder:
  build = Path(folder)
  (build/'build.ninja').write_text('''rule link
  command = linker $libraries
build codecs: link
  libraries = "C:/deps/diligent-build/DiligentTools/ThirdParty/libpng/libpng16_static.lib" "C:/deps/diligent-build/DiligentTools/ThirdParty/zlib.lib" forge_runtime_package.lib
build renderer: link
  libraries = "C:/deps/cpu/Diligent-GraphicsEngineD3D12-static.lib" forge_runtime_package.lib
build spaced_renderer: link
  libraries = "C:/deps/cpu/Diligent GraphicsEngine.lib" forge_runtime_package.lib
build authoring: link
  libraries = /deps/cpu/libforge_authoring.a -lSDL3
''')
  codecs = libraries(link(build, ninja, 'codecs'))
  assert 'diligent' not in codecs and 'libpng16_static.lib' in codecs, codecs
  for target in ('renderer', 'spaced_renderer'):
   assert 'diligent' in libraries(link(build, ninja, target)), target
  authoring = libraries(link(build, ninja, 'authoring'))
  assert 'forge_authoring' in authoring and 'sdl3' in authoring, authoring


build,ninja=sys.argv[1:]
verify_path_controls(ninja)
runtime=libraries(link(build,ninja,'forge_runtime'))
for forbidden in ('rmlui','freetype','imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
 assert forbidden not in runtime,(forbidden,runtime)
game=libraries(link(build,ninja,'forge_game_foundation_tests'))
for forbidden in ('rmlui','freetype','imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
 assert forbidden not in game,(forbidden,game)
package=libraries(link(build,ninja,'forge_runtime_package_tests'))
for forbidden in ('imgui','sdl3','diligent','d3dcompiler','forge_authoring','forge_audio_decode','forge_gltf_native','forge_recast','forge_nav_build'):
 assert forbidden not in package,(forbidden,package)
cache=(Path(build)/'CMakeCache.txt').read_text()
if 'FORGE_BUILD_GAME:BOOL=ON' in cache:
 graphical=libraries(link(build,ninja,'forge_game'))
 for forbidden in ('imgui','forge_authoring','forge_editor','forge_gltf_native','forge_audio_decode'):
  assert forbidden not in graphical,(forbidden,graphical)
if 'FORGE_BUILD_ASSET_TOOLS:BOOL=ON' in cache:
 native=libraries(link(build,ninja,'forge_gltf_native_tests'))
 for forbidden in ('imgui','sdl3','forge_authoring','forge_ui_presenter'):
  assert forbidden not in native,(forbidden,native)
if 'FORGE_BUILD_UI_PRESENTER:BOOL=ON' in cache or 'FORGE_BUILD_EDITOR:BOOL=ON' in cache:
 presenter=libraries(link(build,ninja,'forge_ui_presenter_tests'))
 for forbidden in ('imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
  assert forbidden not in presenter,(forbidden,presenter)
print('Runtime has no editor/presentation linkage; presenter links without editor/Diligent/SDL')
