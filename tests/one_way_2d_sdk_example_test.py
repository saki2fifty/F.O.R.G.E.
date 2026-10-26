"""Build the 2D movement examples with a relocated installed SDK and both Play hosts."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from movement_2d_example_test import verify


def main():
    build, cmake, ninja = (Path(p).resolve() for p in sys.argv[1:4])
    source = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(source / 'tools'))
    from prepare_physics_loads import prepare
    cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
                 if '=' in line and not line.startswith(('//', '#')))
    compiler = next(v for k, v in cache.items() if k.startswith('CMAKE_CXX_COMPILER:'))
    with tempfile.TemporaryDirectory(prefix='one-way-sdk-', dir=build) as temporary:
        root = Path(temporary)
        sdk = root / 'installed sdk'
        subprocess.run([cmake, '--install', build, '--prefix', sdk, '--component', 'NativeSdk'],
                       check=True)
        moved = root / 'relocated sdk'
        sdk.rename(moved)
        for name, component in [('Movement2D', 'project.movement_2d'),
                                ('Character2D', 'project.character_movement_2d')]:
            client = root / (name + ' client source')
            shutil.copytree(source / 'samples/projects' / name / 'Native', client)
            (client / 'one_way_boundary.cpp').write_text(
                '#include <forge/physics_components.hpp>\n'
                '#include <forge/character_components.hpp>\n'
                'static_assert(!forge::CharacterController2D{}.platform_carry);\n'
                'static_assert(!forge::PhysicsBody2D{}.one_way);\n'
                'static_assert(forge::PhysicsBody2D{}.one_way_margin == .02f);\n', encoding='utf-8')
            with (client / 'CMakeLists.txt').open('a', encoding='utf-8') as output:
                output.write('\ntarget_sources(gameplay PRIVATE one_way_boundary.cpp)\n')
            compiled = root / (name + ' client build')
            subprocess.run([cmake, '-S', client, '-B', compiled, '-G', 'Ninja',
                            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + compiler,
                            '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                            '-DForgeNativeSdk_DIR=' + str(moved / 'sdk')], check=True)
            subprocess.run([cmake, '--build', compiled, '--target', 'gameplay', '--parallel', '2'],
                           check=True)
            suffix = '.dll' if sys.platform == 'win32' else '.so'
            executable = '.exe' if sys.platform == 'win32' else ''
            project = root / (name + ' prepared project')
            prepare(source / 'samples/projects' / name, project,
                    moved / ('bin/forge_runtime' + executable), build / ('forge_tools' + executable),
                    module=compiled / ('gameplay' + suffix), component=component,
                    dependencies=['forge.physics', 'forge.input'], import_sources=[
                        'Assets/Sprites/SignalSheet.png', 'Assets/Sprites/SignalSheet.material.json',
                        'Assets/Physics2D/Solid.material.json', 'Assets/Tiles/Directions.tileset.json',
                        'Assets/Tiles/PaintedPlatforms.tilemap.json'])
            observations = verify(moved / ('bin/forge_runtime' + executable), project)
            assert len(observations) == 4
            if name == 'Character2D':
                assert all(r['one_way_landing'] for r in observations)
                assert all(r['platform_ride'] for r in observations)
    print('Relocated installed SDK/public one-way defaults/compiled Movement2D and Character2D/both hosts passed')


if __name__ == '__main__':
    main()
