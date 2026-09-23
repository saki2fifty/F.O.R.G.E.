"""Inspect actual final Ninja link commands for forbidden presentation dependencies."""
import subprocess,sys
from pathlib import Path
build,ninja=sys.argv[1:]
def link(target):
 lines=subprocess.check_output([ninja,'-C',build,'-t','commands',target],text=True).splitlines()
 assert lines,'Missing target '+target
 return lines[-1].lower()
runtime=link('forge_runtime')
for forbidden in ('rmlui','freetype','imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
 assert forbidden not in runtime,(forbidden,runtime)
game=link('forge_game_foundation_tests')
for forbidden in ('rmlui','freetype','imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
 assert forbidden not in game,(forbidden,game)
package=link('forge_runtime_package_tests')
for forbidden in ('imgui','sdl3','diligent','d3dcompiler','forge_authoring','forge_audio_decode','forge_gltf_native','forge_recast','forge_nav_build'):
 assert forbidden not in package,(forbidden,package)
cache=(Path(build)/'CMakeCache.txt').read_text()
if 'FORGE_BUILD_GAME:BOOL=ON' in cache:
 graphical=link('forge_game')
 for forbidden in ('imgui','forge_authoring','forge_editor','forge_gltf_native','forge_audio_decode'):
  assert forbidden not in graphical,(forbidden,graphical)
if 'FORGE_BUILD_ASSET_TOOLS:BOOL=ON' in cache:
 native=link('forge_gltf_native_tests')
 for forbidden in ('imgui','sdl3','forge_authoring','forge_ui_presenter'):
  assert forbidden not in native,(forbidden,native)
if 'FORGE_BUILD_UI_PRESENTER:BOOL=ON' in cache or 'FORGE_BUILD_EDITOR:BOOL=ON' in cache:
 presenter=link('forge_ui_presenter_tests')
 for forbidden in ('imgui','diligent','graphicsengined3d12','sdl3','forge_authoring'):
  assert forbidden not in presenter,(forbidden,presenter)
print('Runtime has no editor/presentation linkage; presenter links without editor/Diligent/SDL')
