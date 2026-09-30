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
    assert 'Run-Forge-Dev.cmd' not in manifest['files']
    assert not any(name.endswith('.exe') and '/' not in name for name in manifest['files'])
    assert b'bin\\forge_editor.exe' in (root/'Run-Forge.cmd').read_bytes()
    assert not any(name.startswith(('NativeSdk/', 'ReferenceGame/', 'runtime-kits/'))
                   for name in manifest['files'])
    with zipfile.ZipFile(sys.argv[2]) as addon:
        addon.extractall(root)
    developer = json.loads((root/'developer-manifest.json').read_text())
    assert developer['build_id'] == manifest['build_id']
    assert developer['source_commit'] == manifest['source_commit']
    assert developer['editor_manifest_sha256'] == hashlib.sha256(
        (root/'manifest.json').read_bytes()).hexdigest()
    for name, digest in developer['files'].items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest() == digest, name
    if os.environ.get('FORGE_COMPILED_SOURCE'):
        assert manifest['source_commit'] == os.environ['FORGE_COMPILED_SOURCE']
    files = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    assert files == set(manifest['files']) | set(developer['files']) | {'manifest.json', 'developer-manifest.json'}
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
    # The user-facing gallery is a normal editable project. Copy it before export,
    # just as the manual instructs, so package hashes remain immutable.
    gallery = root/'Gallery Copy'
    shutil.copytree(root/'Examples/FeatureGallery', gallery)
    assert len(list((gallery/'Scenes').glob('*.scene.json'))) == 4
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
