"""Export the shipped contact System and observe four relocated Games."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def verify(root, tools, observer, evidence, clean_environment):
    evidence = evidence / 'particle-impacts'
    evidence.mkdir(parents=True)
    source, client, build, kits = (root / name for name in
                                 ('Impact Source Project', 'Impact SDK Client',
                                  'Impact Client Build', 'Impact Module Kits'))
    shutil.copytree(root / 'Examples/FeatureGallery', source)
    client.mkdir()
    sdk = root / 'NativeSdk'
    for name in ('particle_impact_gallery.cpp', 'ParticleImpactTint.cpp'):
        shutil.copy2(sdk / 'sdk/sample' / name, client / name)
    (client / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(ForgeImpactAcceptance LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ForgeNativeSdk CONFIG REQUIRED PATHS "${FORGE_NATIVE_SDK}/sdk" NO_DEFAULT_PATH)
add_library(impact MODULE particle_impact_gallery.cpp ParticleImpactTint.cpp)
target_link_libraries(impact PRIVATE ForgeNativeSdk::Client)
set_target_properties(impact PROPERTIES PREFIX "")
forge_install_gameplay_runtime(impact NAME project.particle_impact_gallery)
''')

    def run(arguments, log, *, clean=False, cwd=root):
        result = subprocess.run([str(value) for value in arguments], cwd=cwd,
                                env=clean_environment if clean else None,
                                text=True, capture_output=True, timeout=180)
        (evidence / log).write_text(result.stdout + '\n' + result.stderr)
        assert result.returncode == 0, (log, result.stdout[-1800:], result.stderr[-1800:])
        assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
        return result

    cmake, ninja = shutil.which('cmake'), shutil.which('ninja')
    assert cmake and ninja, 'Matching compiler tools are required by the SDK acceptance gate'
    run([cmake, '-S', client, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_MAKE_PROGRAM=' + ninja, '-DFORGE_NATIVE_SDK=' + str(sdk)], 'configure.log')
    run([cmake, '--build', build, '--parallel', '4'], 'compile.log')
    run([cmake, '--install', build, '--prefix', kits, '--component', 'GameplayRuntime'], 'install.log')
    module_id = 'project.particle_impact_gallery'
    module_kit = kits / module_id
    module = json.loads((module_kit / 'forge.module-kit.json').read_text())
    native = source / 'Native'
    native.mkdir(exist_ok=True)
    for name in module['files']:
        destination = native / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(module_kit / name, destination)
    settings = json.loads((source / 'forge.project.json').read_text())
    settings['modules'] = [dict(id=module_id, implementation='1', sdk='experimental-1',
        fingerprint=module['fingerprint'], library='Native/' + module['library'],
        dependencies=['forge.physics', 'forge.particles'])]
    runtime_kit = root / 'Impact Test Runtime Kit'
    shutil.copytree(root / 'runtime-kits/shared-native-sdk', runtime_kit)
    witness = observer.read_bytes()
    shutil.copy2(observer, runtime_kit / 'forge_game_fixture.exe')
    metadata = json.loads((runtime_kit / 'forge.runtime-kit.json').read_text())
    assert metadata['engine']['runtime_particle_scene_impacts'] == 1
    assert metadata['engine']['runtime_particle_scene_resting'] == 1
    metadata['files']['forge_game_fixture.exe'] = dict(bytes=len(witness),
        sha256=hashlib.sha256(witness).hexdigest())
    (runtime_kit / 'forge.runtime-kit.json').write_text(json.dumps(metadata))
    exports = []
    for name in ('261-scene-particle-impacts', '262-xy-particle-impact-trails',
                 '263-resting-scene-particles', '264-xy-resting-scene-particles'):
        scene = json.loads((source / 'Scenes' / (name + '.scene.json')).read_text())
        settings['startup_scene'] = scene['asset_id']
        (source / 'forge.project.json').write_text(json.dumps(settings))
        destination = root / 'Relocated Impact Games' / name
        options = dict(destination=str(destination), runtime_kit=str(runtime_kit),
                       inspection_runtime=str(sdk / 'bin/forge_runtime.exe'),
                       module_kits={module_id: str(module_kit)})
        exported = run([tools, '--assets', 'export-game', source, json.dumps(options)],
                       name + '-export.log', clean=True)
        assert json.loads(exported.stdout)['ok']
        exports.append((name, destination))
    for unavailable in (source, client, build, kits, runtime_kit):
        shutil.rmtree(unavailable)
    results = []
    for name, destination in exports:
        assert not list(destination.rglob('*.cpp')) and not list(destination.rglob('*.hpp'))
        output = evidence / name
        output.mkdir()
        run([destination / 'forge_game.exe', '--verify-startup'],
            name + '-production.log', clean=True, cwd=destination)
        resting = name.startswith(('263-', '264-'))
        mode = '--packaged-particle-resting' if resting else '--packaged-particle-impacts'
        run([destination / 'forge_game_fixture.exe', mode, output],
            name + '-capture.log', clean=True, cwd=destination)
        result = json.loads((output / 'particle-impacts-result.json').read_text())
        assert result['tick'] >= 180 and len(result['emitters']) == 2
        assert not resting or all(row['resting_heads'] > 0 for row in result['emitters'])
        results.append(dict(scene=name, tick=result['tick'], source_hidden=True,
                            resting=resting, production_exit=0, witness_exit=0))
    (evidence / 'result.json').write_text(json.dumps(results, indent=2))
