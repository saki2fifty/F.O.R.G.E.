"""Relocated installed SDK, unregistered example scenes and real query gameplay."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from physics_volumes_example_test import verify

build, cmake, ninja = (Path(p).resolve() for p in sys.argv[1:4])
source = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(source/'tools'))
from prepare_physics_loads import prepare


def hashes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def rejected(operation, text):
    try:
        operation()
    except ValueError as error:
        assert text in str(error), error
    else:
        raise AssertionError('Invalid example preparation was accepted')


original = source/'samples/projects/PhysicsVolumes'
before = hashes(original)
cache = dict(line.split('=', 1) for line in (build/'CMakeCache.txt').read_text().splitlines()
             if '=' in line and not line.startswith(('//', '#')))
compiler = next(v for k, v in cache.items() if k.startswith('CMAKE_CXX_COMPILER:'))
with tempfile.TemporaryDirectory(prefix='rays-sdk-', dir=build) as temporary:
    root = Path(temporary)
    sdk, client = root/'installed SDK', root/'client build'
    subprocess.run([cmake, '--install', build, '--prefix', sdk, '--component', 'NativeSdk'],
                   check=True, timeout=120)
    for guide in (sdk/'sdk/docs').rglob('*.md'):
        for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', guide.read_text(encoding='utf-8')):
            path = target.split('#', 1)[0]
            if path and '://' not in path:
                linked = (guide.parent/path).resolve()
                assert linked.is_relative_to(sdk/'sdk') and linked.is_file(), (guide, target)
    contract = (sdk/'sdk/docs/extension-contracts.md').read_text(encoding='utf-8')
    assert 'physics_raycast_all' in contract and 'physics_raycast_all_2d' in contract
    subprocess.run([cmake, '-S', original/'Native', '-B', client, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+compiler,
                    '-DCMAKE_MAKE_PROGRAM='+str(ninja), '-DForgeNativeSdk_DIR='+str(sdk/'sdk')],
                   check=True, timeout=120)
    subprocess.run([cmake, '--build', client, '--target', 'gameplay', '--parallel', '2'],
                   check=True, timeout=120)
    moved = root/'relocated SDK'
    sdk.rename(moved)
    exe, suffix = ('.exe', '.dll') if sys.platform == 'win32' else ('', '.so')
    runtime, tools = moved/('bin/forge_runtime'+exe), build/('forge_tools'+exe)
    module = client/('gameplay'+suffix)

    def prepare_example(input_source, output):
        return prepare(input_source, output, runtime, tools, module=module,
                       component='project.ray_probe')

    project = root/'prepared project'
    assert json.loads((original/'forge.assets.json').read_text())['assets'] == []
    prepare_example(original, project)
    for scene in (project/'Scenes').glob('*.scene.json'):
        value = json.loads(scene.read_text())
        for entity in value['entities']:
            for name, data in entity['components'].items():
                if name.startswith('project.'):
                    assert data['$forge']['module'] == 'project.gameplay', (scene, name)
    marker = project/'keep.txt'
    marker.write_text('existing destination')
    rejected(lambda: prepare_example(original, project), 'must be new')
    assert marker.read_text() == 'existing destination'
    incompatible = root/'incompatible source'
    shutil.copytree(original, incompatible)
    path = incompatible/'Scenes/body-volumes.scene.json'
    scene = json.loads(path.read_text())
    data = next(e['components']['project.ray_probe'] for e in scene['entities']
                if 'project.ray_probe' in e['components'])
    data['$forge'] = dict(format='forge.authored-component', version=1, module='project.gameplay',
                          schema_version=1, digest='0'*64)
    path.write_text(json.dumps(scene))
    rejected(lambda: prepare_example(incompatible, root/'rejected project'), 'explicit migration')
    assert json.loads(path.read_text()) == scene
    relocated = root/'relocated project'
    project.rename(relocated)
    verify(runtime, relocated)
assert hashes(original) == before, 'Installed example validation changed product sources'
print('Relocated installed SDK/header/guide links, unregistered primary+secondary scenes, '
      'schema/destination guards, compiled editable volume/ray gameplay and both hosts passed')
