"""Original triangle-heavy mesh through actual import, collision and both hosts."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
from runtime_client import Runtime, activate

runtime, tools, module = (Path(value).resolve() for value in sys.argv[1:4])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']


def imported(root, source):
    result = subprocess.run([tools, '--assets', 'import', root, source],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report['ok'], (source, report, result.stderr)
    return report['asset']


with tempfile.TemporaryDirectory(prefix='ray-work-', dir=runtime.parent) as temporary:
    root = Path(temporary)
    assets = root/'Assets'
    assets.mkdir()
    shutil.copy2(module, root/module.name)
    settings = dict(version=2, name='Original native ray work acceptance', startup_scene=None,
        game=dict(version=1, application_id='org.forge.ray-work-test', title='Ray work test',
            profile='development', save_schema=1,
            display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
            audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
        input=dict(version=1, actions=[]),
        physics=dict(version=1, gravity=[0, -9.81, 0], layers=['Default', 'Farther']+['']*30),
        modules=[dict(id='project.physics_ray_work',
            implementation='1', sdk='experimental-1', fingerprint=fingerprint,
            library=module.name, dependencies=['forge.physics'])])
    (root/'forge.project.json').write_text(json.dumps(settings))
    triangles = 16385
    data = bytearray()
    for i in range(triangles):
        x = i*.001
        for y, z in ((-1, -1), (1, -1), (0, 1)):
            data.extend(struct.pack('<fff', x, y, z))
    (assets/'planes.bin').write_bytes(data)
    source = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[0])],
        nodes=[dict(mesh=0)], meshes=[dict(primitives=[dict(attributes=dict(POSITION=0))])],
        buffers=[dict(uri='planes.bin', byteLength=len(data))],
        bufferViews=[dict(buffer=0, byteOffset=0, byteLength=len(data))],
        accessors=[dict(bufferView=0, componentType=5126, count=3*triangles, type='VEC3',
            min=[0, -1, -1], max=[(triangles-1)*.001, 1, 1])])
    (assets/'planes.gltf').write_text(json.dumps(source))
    imported(root, 'Assets/planes.gltf')
    catalog = json.loads((root/'forge.assets.json').read_text())
    meshes = [record for record in catalog['assets'] if record['type'] == 'mesh']
    assert len(meshes) == 1, meshes
    collision, member = str(uuid.uuid4()), str(uuid.uuid4())
    recipe = dict(kind='forge.collision', version=1, asset_id=collision, root=member,
        nodes=[dict(id=member, kind='triangle_mesh', translation=[0, 0, 0],
            rotation=[0, 0, 0, 1], scale=[1, 1, 1], dimensions=[0, 0, 0], children=[],
            source=dict(mesh=meshes[0]['id'], lod=0, parts='all', degenerate='reject'))])
    (assets/'planes.collision.json').write_text(json.dumps(recipe))
    assert imported(root, 'Assets/planes.collision.json') == collision

    def body(name, x, layer):
        return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode='world'), components={
            'forge.local_translation': dict(x=x, y=0, z=0),
            'forge.physics_body': dict(motion=0, sensor=False, layer=layer, mask=0, density=1000,
                mass=0, friction=.5, restitution=0, gravity_factor=0)})

    heavy, farther = body('Triangle planes', 0, 0), body('Farther solid', 22, 1)
    heavy['components']['forge.asset_collider'] = dict(asset=collision)
    farther['components']['forge.box_collider'] = dict(x=1, y=1, z=1)
    scene = dict(version=5, asset_id=str(uuid.uuid4()), entities=[heavy, farther])
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                 (root/'forge.project.json', root/'forge.assets.json', *assets.iterdir()) if p.is_file()}
    for sdk_play in (False, True):
        args = ['--sdk-project', root]
        if sdk_play:
            args += ['--sdk-play', 'on', '--user-data', root/'users']
        worker = Runtime(runtime, args)
        fresh = None
        try:
            activate(worker, scene, sdk_play)
            previous = None
            for tick in range(1, 4):
                state = worker.request('step')
                rows = [row for row in state['diagnostics']
                        if row['text'] == 'Native ray subshape work proof passed'
                        and row['context'].get('tick') == state['timing']['tick']]
                assert len(rows) == 1 and rows[0]['context']['module'] == 'project.physics_ray_work', state
                if previous is not None:
                    assert state['scene'] == previous, 'Query failure changed the static scene'
                previous = state['scene']
                if tick == 1 and not sdk_play:
                    checkpoint = state['recovery']
                    fresh = Runtime(runtime, args)
                    fresh.request('replace', scene=state['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                elif fresh:
                    other = fresh.request('step')
                    assert other['scene'] == state['scene']
                    assert any(row['text'] == 'Native ray subshape work proof passed'
                               for row in other['diagnostics'])
        finally:
            worker.close()
            if fresh:
                fresh.close()
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in protected.items())
print('Original 16385-triangle mesh: exact16384 hit-work bound, complete overflow clearing, '
      'early canonical/layer exclusion, closest/clean retry, both actual hosts, recovery and source preservation passed')
