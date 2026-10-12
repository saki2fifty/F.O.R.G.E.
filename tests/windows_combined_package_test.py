"""Validate the final package and its shared runtime from a relocated Windows path."""
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import zipfile

with tempfile.TemporaryDirectory(prefix='FORGE combined relocation ') as temporary:
    root = Path(temporary)
    with zipfile.ZipFile(sys.argv[1]) as archive:
        archive.extractall(root)
    manifest = json.loads((root/'manifest.json').read_text())
    assert 'bin/forge_editor.exe' in manifest['files']
    assert 'Run-Forge-Dev.cmd' in manifest['files']
    assert 'NativeSdk/bin/forge_runtime.exe' in manifest['files']
    assert 'runtime-kits/shared-native-sdk/forge_game.exe' in manifest['files']
    assert 'ReferenceGame/forge_game.exe' in manifest['files']
    assert not any(name.endswith('.exe') and '/' not in name for name in manifest['files'])
    assert b'bin\\forge_editor.exe' in (root/'Run-Forge.cmd').read_bytes()
    if os.environ.get('FORGE_COMPILED_SOURCE'):
        assert manifest['source_commit'] == os.environ['FORGE_COMPILED_SOURCE']
    files = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    assert files == set(manifest['files']) | {'manifest.json'}
    for name, digest in manifest['files'].items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest() == digest, name
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
    # Exercise the actual shipped Phase7 tools and source walkthrough from a
    # relocated folder with no repository/developer DLL directories on PATH.
    reference = root / 'ReferenceGame'
    assert not (reference / 'forge_game_fixture.exe').exists()
    result = subprocess.run([str(reference / 'forge_game.exe'), '--verify-startup'],
                            cwd=reference, env=env, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    tools = root/'bin/forge_tools.exe'
    # Execute the delivered C++ example, then export it with the existing exact
    # shared runtime/module-kit path and a separately inventoried test observer.
    from physics_loads_example_test import verify
    loads = root / 'C++ Loads Copy'
    shutil.copytree(root / 'Examples/PhysicsLoads', loads)
    verify(root / 'NativeSdk/bin/forge_runtime.exe', loads, env)
    loads_settings = json.loads((loads / 'forge.project.json').read_text())
    module, = loads_settings['modules']
    loads_kit = root / 'Loads Test Runtime Kit'
    shutil.copytree(root / 'runtime-kits/shared-native-sdk', loads_kit)
    observer = Path(sys.argv[2]).resolve()
    observer_data = observer.read_bytes()
    shutil.copyfile(observer, loads_kit / 'forge_game_fixture.exe')
    kit_metadata = json.loads((loads_kit / 'forge.runtime-kit.json').read_text())
    kit_metadata['files']['forge_game_fixture.exe'] = dict(bytes=len(observer_data),
        sha256=hashlib.sha256(observer_data).hexdigest())
    (loads_kit / 'forge.runtime-kit.json').write_text(json.dumps(kit_metadata))
    loads_export = root / 'Exported C++ Physics Loads'
    loads_options = dict(destination=str(loads_export), runtime_kit=str(loads_kit),
        inspection_runtime=str(root / 'NativeSdk/bin/forge_runtime.exe'),
        module_kits={'project.gameplay': str((loads / module['library']).parent)})
    exported = subprocess.run([str(tools), '--assets', 'export-game', str(loads),
        json.dumps(loads_options)], cwd=root, env=env, text=True, capture_output=True, timeout=180)
    assert exported.returncode == 0 and json.loads(exported.stdout)['ok'], (exported.stdout, exported.stderr)
    shutil.rmtree(loads)
    shutil.rmtree(loads_kit)
    loads_manifest = json.loads((loads_export / 'forge.standalone.json').read_text())
    assert not list(loads_export.rglob('*.cpp')) and not list(loads_export.rglob('*.hpp'))
    evidence = Path(sys.argv[3]).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    for executable, arguments, log in (
        ('forge_game.exe', ['--verify-startup'], 'production-startup.log'),
        ('forge_game_fixture.exe', ['--packaged-loads', str(evidence)], 'capture-host.log')):
        result = subprocess.run([str(loads_export / executable), *arguments], cwd=loads_export,
            env=env, text=True, capture_output=True, timeout=180)
        (evidence / log).write_text(result.stdout + '\n' + result.stderr)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    assert len(json.loads((evidence / 'physics-loads-result.json').read_text())) == 6
    from physics_velocity_example_test import verify as verify_velocity
    velocity = root / 'C++ Velocity Copy'
    shutil.copytree(root / 'Examples/PhysicsVelocity', velocity)
    verify_velocity(root / 'NativeSdk/bin/forge_runtime.exe', velocity, env)
    velocity_settings = json.loads((velocity / 'forge.project.json').read_text())
    velocity_module, = velocity_settings['modules']
    velocity_kit = root / 'Velocity Test Runtime Kit'
    shutil.copytree(root / 'runtime-kits/shared-native-sdk', velocity_kit)
    shutil.copyfile(observer, velocity_kit / 'forge_game_fixture.exe')
    (velocity_kit / 'forge.runtime-kit.json').write_text(json.dumps(kit_metadata))
    velocity_export = root / 'Exported C++ Physics Velocity'
    velocity_options = dict(destination=str(velocity_export), runtime_kit=str(velocity_kit),
        inspection_runtime=str(root / 'NativeSdk/bin/forge_runtime.exe'),
        module_kits={'project.gameplay': str((velocity / velocity_module['library']).parent)})
    result = subprocess.run([str(tools), '--assets', 'export-game', str(velocity),
        json.dumps(velocity_options)], cwd=root, env=env, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    shutil.rmtree(velocity)
    shutil.rmtree(velocity_kit)
    assert not list(velocity_export.rglob('*.cpp')) and not list(velocity_export.rglob('*.hpp'))
    velocity_evidence = evidence / 'velocity'
    velocity_evidence.mkdir()
    for executable, arguments, log in (
        ('forge_game.exe', ['--verify-startup'], 'production-startup.log'),
        ('forge_game_fixture.exe', ['--packaged-velocity', str(velocity_evidence)], 'capture-host.log')):
        result = subprocess.run([str(velocity_export / executable), *arguments], cwd=velocity_export,
            env=env, text=True, capture_output=True, timeout=180)
        (velocity_evidence / log).write_text(result.stdout + '\n' + result.stderr)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    assert len(json.loads((velocity_evidence / 'physics-velocity-result.json').read_text())) == 4
    from physics_contacts_example_test import verify as verify_contacts
    contacts = root / 'C++ Contacts Copy'
    shutil.copytree(root / 'Examples/PhysicsContacts', contacts)
    verify_contacts(root / 'NativeSdk/bin/forge_runtime.exe', contacts, env)
    contacts_module, = json.loads((contacts / 'forge.project.json').read_text())['modules']
    contacts_kit = root / 'Contacts Test Runtime Kit'
    shutil.copytree(root / 'runtime-kits/shared-native-sdk', contacts_kit)
    shutil.copyfile(observer, contacts_kit / 'forge_game_fixture.exe')
    (contacts_kit / 'forge.runtime-kit.json').write_text(json.dumps(kit_metadata))
    contacts_export = root / 'Exported C++ Physics Contacts'
    contacts_options = dict(destination=str(contacts_export), runtime_kit=str(contacts_kit),
        inspection_runtime=str(root / 'NativeSdk/bin/forge_runtime.exe'),
        module_kits={'project.gameplay': str((contacts / contacts_module['library']).parent)})
    result = subprocess.run([str(tools), '--assets', 'export-game', str(contacts),
        json.dumps(contacts_options)], cwd=root, env=env, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    shutil.rmtree(contacts)
    shutil.rmtree(contacts_kit)
    assert not list(contacts_export.rglob('*.cpp')) and not list(contacts_export.rglob('*.hpp'))
    contacts_evidence = evidence / 'contacts'
    contacts_evidence.mkdir()
    for executable, arguments, log in (
        ('forge_game.exe', ['--verify-startup'], 'production-startup.log'),
        ('forge_game_fixture.exe', ['--packaged-contacts', str(contacts_evidence)], 'capture-host.log')):
        result = subprocess.run([str(contacts_export / executable), *arguments], cwd=contacts_export,
            env=env, text=True, capture_output=True, timeout=180)
        (contacts_evidence / log).write_text(result.stdout + '\n' + result.stderr)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    assert json.loads((contacts_evidence / 'physics-contacts-result.json').read_text())['begin_tick'] > 0
    from physics_volumes_example_test import verify as verify_volumes
    volumes = root / 'C++ Volumes Copy'
    shutil.copytree(root / 'Examples/PhysicsVolumes', volumes)
    verify_volumes(root / 'NativeSdk/bin/forge_runtime.exe', volumes, env)
    volumes_module, = json.loads((volumes / 'forge.project.json').read_text())['modules']
    volumes_kit = root / 'Volumes Test Runtime Kit'
    shutil.copytree(root / 'runtime-kits/shared-native-sdk', volumes_kit)
    shutil.copyfile(observer, volumes_kit / 'forge_game_fixture.exe')
    (volumes_kit / 'forge.runtime-kit.json').write_text(json.dumps(kit_metadata))
    volumes_export = root / 'Exported C++ Physics Volumes'
    volumes_options = dict(destination=str(volumes_export), runtime_kit=str(volumes_kit),
        inspection_runtime=str(root / 'NativeSdk/bin/forge_runtime.exe'),
        module_kits={'project.gameplay': str((volumes / volumes_module['library']).parent)})
    result = subprocess.run([str(tools), '--assets', 'export-game', str(volumes),
        json.dumps(volumes_options)], cwd=root, env=env, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    shutil.rmtree(volumes)
    shutil.rmtree(volumes_kit)
    assert not list(volumes_export.rglob('*.cpp')) and not list(volumes_export.rglob('*.hpp'))
    volumes_evidence = evidence / 'volumes'
    volumes_evidence.mkdir()
    for executable, arguments, log in (
        ('forge_game.exe', ['--verify-startup'], 'production-startup.log'),
        ('forge_game_fixture.exe', ['--packaged-volumes', str(volumes_evidence)], 'capture-host.log')):
        result = subprocess.run([str(volumes_export / executable), *arguments], cwd=volumes_export,
            env=env, text=True, capture_output=True, timeout=180)
        (volumes_evidence / log).write_text(result.stdout + '\n' + result.stderr)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    assert json.loads((volumes_evidence / 'physics-volumes-result.json').read_text())['components']['project.volume_probe']['last_count'] == 3
    # The user-facing gallery is a normal editable project. Copy it before export,
    # just as the manual instructs, so package hashes remain immutable.
    gallery = root/'Gallery Copy'
    shutil.copytree(root/'Examples/FeatureGallery', gallery)
    assert {'01-transforms.scene.json', '02-lighting.scene.json',
            '03-physics.scene.json', '04-cameras.scene.json'} <= {
                p.name for p in (gallery/'Scenes').glob('*.scene.json')}
    gallery_settings = json.loads((gallery/'forge.project.json').read_text())
    selected = json.loads((gallery/gallery_settings['startup_scene']['source']).read_text())
    assert gallery_settings['startup_scene']['asset'] == selected['asset_id']
    gallery_export = root/'Gallery Standalone'
    options = dict(destination=str(gallery_export), runtime_kit=str(root/'runtime-kit'))
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert json.loads(result.stdout)['ok'], result.stdout
    standalone = json.loads((gallery_export/'forge.standalone.json').read_text())
    assert standalone['settings']['startup_scene']['asset'] == selected['asset_id']
    started = subprocess.run([str(gallery_export/'forge_game.exe'), '--verify-startup'],
                             cwd=gallery_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    # The authored timeline clip must survive the same shipped export path.
    animated_scene = 'Scenes/08-transform-animation.scene.json'
    animated = json.loads((gallery/animated_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=animated['asset_id'], source=animated_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    animated_export = root/'Timeline Standalone'
    options['destination'] = str(animated_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(animated_export/'forge_game.exe'), '--verify-startup'],
                             cwd=animated_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (animated_export/'Assets/Platform.animation.json').exists()
    # The exported game admits legacy linear and current cubic transform recipes.
    curve_scene = 'Scenes/13-transform-curves.scene.json'
    curves = json.loads((gallery/curve_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=curves['asset_id'], source=curve_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    curve_export = root/'Exported Curve Comparison'
    options['destination'] = str(curve_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(curve_export/'forge_game.exe'), '--verify-startup'],
                             cwd=curve_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('Platform.animation.json', 'EasedPlatform.animation.json',
                 'ArcPlatform.animation.json'):
        assert not (curve_export/'Assets'/name).exists()
    # Authored UV islands use the same selected Mesh/material export path.
    uv_scene = 'Scenes/14-uv-islands.scene.json'
    uv = json.loads((gallery/uv_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=uv['asset_id'], source=uv_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    uv_export = root/'UV Islands Standalone'
    options['destination'] = str(uv_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(uv_export/'forge_game.exe'), '--verify-startup'],
                             cwd=uv_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('UvConnected.mesh.json', 'UvSplit.mesh.json', 'UvFitted.mesh.json'):
        assert not (uv_export/'Assets'/name).exists()
    # Authored brush symmetry use the same selected Mesh/material export path.
    brush_scene = 'Scenes/15-brush-symmetry.scene.json'
    brush = json.loads((gallery/brush_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=brush['asset_id'], source=brush_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    brush_export = root/'Brush Symmetry Standalone'
    options['destination'] = str(brush_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(brush_export/'forge_game.exe'), '--verify-startup'],
                             cwd=brush_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('SculptOneSide.mesh.json', 'SculptMirrored.mesh.json', 'PaintMirrored.mesh.json'):
        assert not (brush_export/'Assets'/name).exists()
    # Authored triangulation use the same selected Mesh/material export path.
    triangle_scene = 'Scenes/16-authored-triangulation.scene.json'
    triangles = json.loads((gallery/triangle_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=triangles['asset_id'], source=triangle_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    triangle_export = root/'Authored Triangles Standalone'
    options['destination'] = str(triangle_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(triangle_export/'forge_game.exe'), '--verify-startup'],
                             cwd=triangle_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('PolygonCube.mesh.json', 'TriangleCube.mesh.json', 'TriangleSculpt.mesh.json'):
        assert not (triangle_export/'Assets'/name).exists()
    # Flat and smooth shading use the same selected Mesh/material export path.
    shading_scene = 'Scenes/17-mesh-shading.scene.json'
    shading = json.loads((gallery/shading_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=shading['asset_id'], source=shading_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    shading_export = root/'Mesh Shading Standalone'
    options['destination'] = str(shading_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(shading_export/'forge_game.exe'), '--verify-startup'],
                             cwd=shading_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('FlatSphere.mesh.json', 'SmoothSphere.mesh.json'):
        assert not (shading_export/'Assets'/name).exists()
    batch_scene = 'Scenes/18-animation-key-batches.scene.json'
    batch = json.loads((gallery/batch_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=batch['asset_id'], source=batch_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    batch_export = root/'Animation Key Batches Standalone'
    options['destination'] = str(batch_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(batch_export/'forge_game.exe'), '--verify-startup'],
                             cwd=batch_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('BatchOriginal.animation.json', 'BatchShifted.animation.json',
                 'BatchRepeated.animation.json'):
        assert not (batch_export/'Assets'/name).exists()
    facing_scene = 'Scenes/19-mesh-facing.scene.json'
    facing = json.loads((gallery/facing_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=facing['asset_id'], source=facing_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    facing_export = root/'Mesh Facing Standalone'
    options['destination'] = str(facing_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(facing_export/'forge_game.exe'), '--verify-startup'],
                             cwd=facing_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    for name in ('OutwardRoom.mesh.json', 'InwardRoom.mesh.json'):
        assert not (facing_export/'Assets'/name).exists()
    joint_scene = 'Scenes/20-physics-joints.scene.json'
    joint = json.loads((gallery/joint_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=joint['asset_id'], source=joint_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    joint_export = root/'Physics Joint Standalone'
    options['destination'] = str(joint_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(joint_export/'forge_game.exe'), '--verify-startup'],
                             cwd=joint_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (joint_export/'Scenes').exists() and not (joint_export/'Assets').exists()
    joint_scene = 'Scenes/21-point-fixed-joints.scene.json'
    joint = json.loads((gallery/joint_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=joint['asset_id'], source=joint_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    joint_export = root/'Point Fixed Joint Standalone'
    options['destination'] = str(joint_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(joint_export/'forge_game.exe'), '--verify-startup'],
                             cwd=joint_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (joint_export/'Scenes').exists() and not (joint_export/'Assets').exists()
    joint_scene = 'Scenes/22-continuous-collision.scene.json'
    joint = json.loads((gallery/joint_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=joint['asset_id'], source=joint_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    joint_export = root/'Continuous Collision Standalone'
    options['destination'] = str(joint_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(joint_export/'forge_game.exe'), '--verify-startup'],
                             cwd=joint_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (joint_export/'Scenes').exists() and not (joint_export/'Assets').exists()
    tuning_scene = 'Scenes/23-body-motion-tuning.scene.json'
    tuning = json.loads((gallery/tuning_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=tuning['asset_id'], source=tuning_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    tuning_export = root/'Body Motion Tuning Standalone'
    options['destination'] = str(tuning_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(tuning_export/'forge_game.exe'), '--verify-startup'],
                             cwd=tuning_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (tuning_export/'Scenes').exists() and not (tuning_export/'Assets').exists()
    # Ordinary editable joint scenes run after deleting their copied sources/kits.
    for label, scene_path, observer_mode, result_file in (
        ('Slider Joint', 'Scenes/24-slider-joints.scene.json', '--packaged-slider', 'physics-slider-result.json'),
        ('Velocity Motors', 'Scenes/25-velocity-motors.scene.json', '--packaged-motors', 'physics-motors-result.json'),
        ('Position Drives', 'Scenes/26-position-drives.scene.json', '--packaged-position', 'physics-position-result.json'),
        ('Connected Contacts', 'Scenes/27-connected-collisions.scene.json', '--packaged-connected', 'physics-connected-result.json'),
        ('Swing Twist', 'Scenes/28-swing-twist-joints.scene.json', '--packaged-swing', 'physics-swing-result.json')):
        slider_source = root / (label + ' Gallery Copy')
        shutil.copytree(gallery, slider_source)
        slider_scene = scene_path
        slider = json.loads((slider_source / slider_scene).read_text())
        slider_settings = json.loads((slider_source / 'forge.project.json').read_text())
        slider_settings['startup_scene'] = dict(asset=slider['asset_id'], source=slider_scene)
        (slider_source / 'forge.project.json').write_text(json.dumps(slider_settings))
        slider_kit = root / (label + ' Test Runtime Kit')
        shutil.copytree(root / 'runtime-kit', slider_kit)
        shutil.copyfile(observer, slider_kit / 'forge_game_fixture.exe')
        slider_metadata = json.loads((slider_kit / 'forge.runtime-kit.json').read_text())
        slider_metadata['files']['forge_game_fixture.exe'] = dict(bytes=len(observer_data),
            sha256=hashlib.sha256(observer_data).hexdigest())
        (slider_kit / 'forge.runtime-kit.json').write_text(json.dumps(slider_metadata))
        slider_export = root / (label + ' Standalone')
        exported = subprocess.run([str(tools), '--assets', 'export-game', str(slider_source),
            json.dumps(dict(destination=str(slider_export), runtime_kit=str(slider_kit)))],
            cwd=root, env=env, text=True, capture_output=True, timeout=180)
        assert exported.returncode == 0 and json.loads(exported.stdout)['ok'], (exported.stdout, exported.stderr)
        shutil.rmtree(slider_source)
        shutil.rmtree(slider_kit)
        assert not (slider_export / 'Scenes').exists() and not (slider_export / 'Assets').exists()
        slider_evidence = evidence.parent / label
        slider_evidence.mkdir(parents=True, exist_ok=True)
        for executable, arguments, log in (
            ('forge_game.exe', ['--verify-startup'], 'production-startup.log'),
            ('forge_game_fixture.exe', [observer_mode, str(slider_evidence)], 'capture-host.log')):
            result = subprocess.run([str(slider_export / executable), *arguments], cwd=slider_export,
                env=env, text=True, capture_output=True, timeout=180)
            (slider_evidence / log).write_text(result.stdout + result.stderr)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
        assert len(json.loads((slider_evidence / result_file).read_text())['carriages']) == 2
    # Two skeletal clips must be discovered through the saved Animator references.
    blend_scene = 'Scenes/09-animation-blending.scene.json'
    blended = json.loads((gallery/blend_scene).read_text())
    gallery_settings['startup_scene'] = dict(asset=blended['asset_id'], source=blend_scene)
    (gallery/'forge.project.json').write_text(json.dumps(gallery_settings))
    blend_export = root/'Blend Standalone'
    options['destination'] = str(blend_export)
    result = subprocess.run([str(tools), '--assets', 'export-game', str(gallery),
                             json.dumps(options)], cwd=root, env=env, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
    started = subprocess.run([str(blend_export/'forge_game.exe'), '--verify-startup'],
                             cwd=blend_export, env=env, text=True,
                             capture_output=True, timeout=180)
    assert started.returncode == 0, (started.stdout, started.stderr)
    assert not (blend_export/'Assets/BlendColumn.gltf').exists()
    rendering = root/'Examples/Rendering'
    assert (rendering/'README.md').is_file()
    assert (root/'manual/editor/models.html').is_file()

    def import_asset(locator, success=True):
        result = subprocess.run([str(tools), '--assets', 'import', str(rendering), locator],
                                cwd=root, env=env, text=True, capture_output=True, timeout=150)
        try:
            value = json.loads(result.stdout)
        except ValueError:
            raise AssertionError((result.returncode, result.stdout, result.stderr))
        assert value['ok'] == success and (result.returncode == 0) == success, (value, result.stderr)
        return value

    model = import_asset('Assets/Rendering.gltf')
    assert not model['cache_hit']
    repeated = import_asset('Assets/Rendering.gltf')
    assert repeated['asset'] == model['asset'] and repeated['cache_hit']
    texture = import_asset('Assets/checker.png')
    assert texture['asset'] != model['asset']
    catalog = rendering/'forge.assets.json'
    selected = catalog.read_bytes()
    source = rendering/'Assets/Rendering.gltf'
    original = source.read_bytes()
    source.write_bytes(b'{')
    import_asset('Assets/Rendering.gltf', success=False)
    assert catalog.read_bytes() == selected, 'Relocated failed import replaced selected assets'
    source.write_bytes(original)
    assert import_asset('Assets/Rendering.gltf')['cache_hit']
    assert not list((rendering/'.forge/jobs').iterdir()), 'Relocated worker staging leaked'
    runtime = root/'NativeSdk/bin/forge_runtime.exe'
    info = json.loads(subprocess.check_output([str(runtime), '--sdk-info'], cwd=root, env=env, text=True))
    assert info['profile'] == 'shared-native-sdk'
    project = root/'Relocated Project'
    project.mkdir()
    (project/'forge.project.json').write_text(json.dumps(dict(version=2, name='Relocation', startup_scene=None, simulation_hz=60, input=dict(version=1, actions=[]), modules=[])))
    process = subprocess.Popen([str(runtime), '--sdk-project', str(project)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=env, cwd=root)
    deadline = threading.Timer(20, process.kill)
    deadline.start()
    try:
        process.stdin.write(json.dumps(dict(protocol=2, id=1, session='', command='hello'))+'\n')
        process.stdin.flush()
        hello = json.loads(process.stdout.readline())
        process.stdin.write(json.dumps(dict(protocol=2, id=2, session=hello['session'], command='quit'))+'\n')
        process.stdin.flush()
        remaining, errors = process.communicate(timeout=15)
        assert process.returncode == 0, errors
    finally:
        deadline.cancel()
        if process.poll() is None:
            process.kill()
            process.wait()
    assert hello['ok'], hello
    contract = hello['runtime_contract']
    assert contract['sdk_project'] and contract['profile'] == 'shared-native-sdk'
    assert contract['source_commit'] == manifest['source_commit']
    print('Final package hashes, gallery export/startup, relocated model/texture workers, '
          'cache/failure retention, shared DLL loading and SDK Hello passed')
