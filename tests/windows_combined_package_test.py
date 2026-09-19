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
    print('Final combined package hashes, shared DLL loading, SDK Hello and relocation passed')
