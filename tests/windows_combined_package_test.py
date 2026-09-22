"""Validate the final package and its shared runtime from a relocated Windows path."""
import hashlib
import json
import os
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
    tools = root/'forge_tools.exe'
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
    print('Final package hashes, relocated model/texture workers, cache/failure retention, '
          'shared DLL loading and SDK Hello passed')
