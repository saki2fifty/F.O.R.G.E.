"""Real CLI model publication, explicit correspondence, relocation and failure retention."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temp:
    project = Path(temp) / 'Model project with spaces'
    (project / 'Assets').mkdir(parents=True)
    (project / 'Assets/triangle.bin').write_bytes(struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0))
    mesh = {'primitives': [{'attributes': {'POSITION': 0}}]}
    document = {'asset': {'version': '2.0'},
                'buffers': [{'uri': 'triangle.bin', 'byteLength': 36}],
                'bufferViews': [{'buffer': 0, 'byteLength': 36}],
                'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'VEC3',
                               'count': 3, 'min': [0, 0, 0], 'max': [1, 1, 0]}],
                'meshes': [mesh, mesh], 'nodes': [], 'scenes': [{'nodes': []}], 'scene': 0}
    source = project / 'Assets/test.gltf'
    source.write_text(json.dumps(document), encoding='utf-8')
    env = os.environ.copy()
    env['PATH'] = str(exe.parent) + os.pathsep + env.get('PATH', '')

    def call(root, settings=None, decisions=None, success=True, executable=None):
        args = [str(executable) if executable else exe.name, '--assets', 'import', str(root), 'Assets/test.gltf']
        if settings is not None or decisions is not None:
            args.append(json.dumps(settings or {}))
        if decisions is not None:
            args.append(json.dumps(decisions))
        result = subprocess.run(args, cwd=temp, env=env, text=True, capture_output=True, timeout=120)
        try:
            value = json.loads(result.stdout)
        except ValueError:
            raise AssertionError((result.returncode, result.stdout, result.stderr))
        assert value['ok'] == success and (result.returncode == 0) == success, (value, result.stderr)
        return value

    first = call(project)
    sidecar = source.with_name(source.name + '.forge-import.json')
    catalog = project / 'forge.assets.json'
    selected = catalog.read_bytes(), sidecar.read_bytes()
    ambiguous = call(project, success=False)
    assert ambiguous['error']['code'] == 'subasset.identity-ambiguous', ambiguous
    conflicts = ambiguous['identity_conflicts']
    assert len(conflicts) == 1 and len(conflicts[0]['observations']) == 2, conflicts
    assert selected == (catalog.read_bytes(), sidecar.read_bytes())
    decisions = [dict(address=address, previous=previous) for address, previous in
                 zip(conflicts[0]['observations'], conflicts[0]['previous'], strict=True)]
    resolved = call(project, decisions=decisions)
    assert resolved['asset'] == first['asset'] and resolved['cache_hit']
    selected = catalog.read_bytes(), sidecar.read_bytes()
    call(project, decisions=[decisions[0], decisions[0]], success=False)
    call(project, settings={'max_texture_size': 0}, success=False)
    assert selected == (catalog.read_bytes(), sidecar.read_bytes())
    relocated = Path(temp) / 'Relocated model project'
    shutil.copytree(project, relocated, ignore=shutil.ignore_patterns('.forge'))
    moved = call(relocated, decisions=decisions)
    assert moved['asset'] == first['asset'] and moved['build_key'] == first['build_key']
    assert not moved['cache_hit']
    (relocated / 'Assets/triangle.bin').write_bytes(b'bad')
    before = (relocated / 'forge.assets.json').read_bytes()
    call(relocated, success=False)
    assert before == (relocated / 'forge.assets.json').read_bytes()
    assert not list((relocated / '.forge/jobs').iterdir())
print('Model CLI/PATH, structured identity conflicts, explicit choices, relocation and failures passed')

# Animated CLI import uses the packaged converter, preserves a complete family,
# and must fail without it even when a previous family/cache exists.
with tempfile.TemporaryDirectory(dir=scratch) as temp:
    project = Path(temp) / 'Animated model project'
    assets = project / 'Assets'
    assets.mkdir(parents=True)
    data = struct.pack('<8f', 0, 1, 0, 0, 0, 1, 2, 3)
    (assets / 'animation.bin').write_bytes(data)
    doc = {'asset': {'version': '2.0'}, 'nodes': [{}], 'scenes': [{'nodes': [0]}], 'scene': 0,
           'buffers': [{'uri': 'animation.bin', 'byteLength': len(data)}],
           'bufferViews': [{'buffer': 0, 'byteLength': 8}, {'buffer': 0, 'byteOffset': 8, 'byteLength': 24}],
           'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'SCALAR', 'count': 2, 'min': [0], 'max': [1]},
                         {'bufferView': 1, 'componentType': 5126, 'type': 'VEC3', 'count': 2}],
           'animations': [{'name': 'Move', 'samplers': [{'input': 0, 'output': 1}],
                           'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]}]}
    source = assets / 'test.gltf'
    source.write_text(json.dumps(doc))
    env = os.environ.copy()
    env['PATH'] = str(exe.parent) + os.pathsep + env.get('PATH', '')
    imported = call(project, settings={'animation_sampling_rate': 60, 'animation_optimize': False})
    catalog = project / 'forge.assets.json'
    sidecar = source.with_name(source.name + '.forge-import.json')
    selected = catalog.read_bytes(), sidecar.read_bytes()
    inputs = json.loads(sidecar.read_text())['build_inputs']
    assert inputs['key_version'] == 2 and len(inputs['tool_revisions']['gltf2ozz']) == 64
    assert inputs['settings']['animation_sampling_rate'] == 60
    assert call(project)['cache_hit']
    selected = catalog.read_bytes(), sidecar.read_bytes()
    # A deliberately incomplete copy of the packaged tools, in this temporary directory.
    tools = Path(temp) / 'Tools without converter'
    tools.mkdir()
    shutil.copy2(exe, tools / exe.name)
    worker = exe.parent / ('forge_asset_build.exe' if os.name == 'nt' else 'forge_asset_build')
    shutil.copy2(worker, tools / worker.name)
    for dll in exe.parent.glob('*.dll'):
        shutil.copy2(dll, tools / dll.name)
    env['PATH'] = str(tools) + os.pathsep + os.environ.get('PATH', '')
    call(project, success=False, executable=tools / exe.name)
    assert selected == (catalog.read_bytes(), sidecar.read_bytes())
    env['PATH'] = str(exe.parent) + os.pathsep + os.environ.get('PATH', '')
    call(project, settings={'animation_sampling_rate': 0}, success=False)
    assert selected == (catalog.read_bytes(), sidecar.read_bytes())
    assert call(project)['cache_hit']
    assert not list((project / '.forge/jobs').iterdir())
print('Animated CLI settings, tool-key provenance, cache and missing-converter retention passed')
