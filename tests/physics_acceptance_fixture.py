"""Build a reusable physics level through production import and scene admission.

No FPS input policy or runtime simulation is implemented here. The generated
project is usable by the editor and the headless acceptance runner.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import uuid


def make_project(tools, output):
    root = output.resolve() / ('PhysicsAcceptance-' + str(uuid.uuid4()))
    assets = root / 'Assets'
    assets.mkdir(parents=True)

    def write(path, value):
        path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def command(*args):
        result = subprocess.run([str(tools), *map(str, args)], capture_output=True,
                                text=True, timeout=180)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        value = json.loads(result.stdout)
        if not value.get('ok', False):
            raise RuntimeError(result.stdout)
        return value

    vertices = [(-.5, -.5, -.5), (.5, -.5, -.5), (.5, .5, -.5), (-.5, .5, -.5),
                (-.5, -.5, .5), (.5, -.5, .5), (.5, .5, .5), (-.5, .5, .5)]
    indices = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
               3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]
    binary = b''.join(struct.pack('<3f', *v) for v in vertices)
    binary += struct.pack('<36H', *indices)
    (assets / 'obstacle.bin').write_bytes(binary)
    write(assets / 'obstacle.gltf', dict(
        asset=dict(version='2.0', generator='FORGE physics acceptance fixture'),
        buffers=[dict(uri='obstacle.bin', byteLength=len(binary))],
        bufferViews=[dict(buffer=0, byteOffset=0, byteLength=96),
                     dict(buffer=0, byteOffset=96, byteLength=72)],
        accessors=[dict(bufferView=0, componentType=5126, count=8, type='VEC3',
                        min=[-.5, -.5, -.5], max=[.5, .5, .5]),
                   dict(bufferView=1, componentType=5123, count=36, type='SCALAR')],
        meshes=[dict(name='Obstacle', primitives=[dict(attributes=dict(POSITION=0), indices=1)])],
        nodes=[dict(name='Imported obstacle', mesh=0)], scenes=[dict(nodes=[0])], scene=0))
    model = command('--assets', 'import', root, 'Assets/obstacle.gltf')['asset']
    catalog = json.loads((root / 'forge.assets.json').read_text())
    mesh = next(a['id'] for a in catalog['assets']
                if a['type'] == 'mesh' and a.get('subasset', {}).get('owner') == model)
    collision = {}
    for kind in ('box', 'sphere', 'capsule', 'cylinder', 'convex_hull', 'triangle_mesh', 'compound'):
        identity, member = str(uuid.uuid4()), str(uuid.uuid4())
        dimensions = ([1, 1, 1] if kind == 'box' else [.5, 0, 0] if kind == 'sphere'
                      else [.5, 1, 0] if kind in ('capsule', 'cylinder') else [0, 0, 0])
        node = dict(id=member, kind=kind, translation=[0, 0, 0], rotation=[0, 0, 0, 1],
                    scale=[1, 1, 1], dimensions=dimensions, children=[])
        if kind in ('convex_hull', 'triangle_mesh'):
            node['source'] = dict(mesh=mesh, lod=0, parts='all', degenerate='reject')
        nodes = [node]
        if kind == 'compound':
            for x in (-.65, .65):
                child = str(uuid.uuid4())
                node['children'].append(child)
                nodes.append(dict(id=child, kind='box', translation=[x, 0, 0],
                                  rotation=[0, 0, 0, 1], scale=[1, 1, 1],
                                  dimensions=[1, 1, 1], children=[]))
        source = f'Assets/{kind}.collision.json'
        write(root / source, dict(kind='forge.collision', version=1, asset_id=identity,
                                  root=member, nodes=nodes))
        assert command('--assets', 'import', root, source)['asset'] == identity
        collision[kind] = identity

    rows = []

    def box(name, position, size, motion=0, rotation=(0, 0, 0), asset=None, sensor=False):
        components = {'forge.position': dict(zip(('x', 'y', 'z'), position)),
                      'forge.rotation': dict(zip(('x', 'y', 'z'), rotation)),
                      'forge.scale': dict(zip(('x', 'y', 'z'), size)),
                      'forge.physics_body': dict(motion=motion, density=1000, mass=0,
                                                friction=.5, restitution=0, gravity_factor=1,
                                                enabled=True, sensor=sensor, layer=0, mask=0xffffffff)}
        if asset:
            components['forge.asset_collider'] = dict(asset=asset)
        else:
            components['forge.box_collider'] = dict(x=1, y=1, z=1)
        rows.append(dict(id=name, name=name, components=components))

    box('Floor', (0, -.5, 0), (30, 1, 30))
    box('Wall', (-8, 1.5, 0), (.5, 3, 12))
    for step in range(5):
        height = .25 * (step + 1)
        box(f'Stair {step + 1}', (-4, height / 2, 2 + step * .7), (3, height, .7))
    box('Too high step', (0, .6, 4), (2, 1.2, 2))
    box('Gentle slope', (4, .6, 4), (3, .3, 4), rotation=(20, 0, 0))
    box('Steep slope', (7, 1.5, 4), (2, .3, 4), rotation=(65, 0, 0))
    box('Moving platform', (4, .15, -3), (3, .3, 3), motion=1)
    box('Pushable', (0, .5, 1), (1, 1, 1), motion=2)
    box('Sensor', (0, 1, -3), (3, 2, .25), sensor=True)
    box('Imported static collision', (-4, .5, -5), (2, 1, 2), asset=collision['triangle_mesh'])
    box('Convex obstacle', (-1, .5, -5), (1, 1, 1), asset=collision['convex_hull'])
    for i, kind in enumerate(('box', 'sphere', 'capsule', 'cylinder', 'compound')):
        box(f'Asset {kind}', (-6 + i * 3, 1, -9), (1, 1, 1), asset=collision[kind])
    rows.append(dict(id='Character', name='Character', components={'forge.position': dict(x=0, y=.1, z=-6)}))
    process = subprocess.Popen([str(tools), '--stdio'], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True)
    try:
        def request(method, **fields):
            process.stdin.write(json.dumps(dict(api=1, method=method, **fields)) + '\n')
            process.stdin.flush()
            result = json.loads(process.stdout.readline())
            if not result['ok']:
                raise RuntimeError(str(result))
            return result
        context = request('discover')['result']
        result = request('scene.replace', target=context['target'], expected_revision=context['revision'],
                         document=dict(version=1, entities=rows))
        scene = request('scene.read', target=context['target'])['result']
        for row in scene['entities']:
            if row['name'] in ('Character', 'Pushable'):
                row['spatial'] = dict(mode='world')
            if row['name'] == 'Character':
                row['components']['forge.primitive'] = dict(kind=4)
                row['components']['forge.character_controller'] = dict(
                    enabled=True, shape=0, radius=.35, height=1.1, crouch_height=.45,
                    mass=70, max_strength=100, max_slope=45, step_height=.4,
                    step_forward=.15, floor_probe=.5, gravity_factor=1, layer=0, mask=0xffffffff)
        character = next(row['id'] for row in scene['entities'] if row['name'] == 'Character')
        scene['entities'].append(dict(id=str(uuid.uuid4()), name='Character reference envelope',
                                      parent=character, spatial=dict(mode='follow_structure'),
                                      components={'forge.local_translation': dict(x=0, y=.9, z=0),
                                                  'forge.local_rotation': dict(x=0, y=0, z=0, w=1),
                                                  'forge.local_scale': dict(x=.7, y=1.8, z=.7),
                                                  'forge.primitive': dict(kind=0)}))
        request('scene.replace', target=context['target'], expected_revision=result['revision'], document=scene)
        scene = request('scene.read', target=context['target'])['result']
        write(root / 'main.scene.json', scene)
        write(root / 'forge.project.json', dict(version=1, name='Physics acceptance',
                                               startup_scene='main.scene.json'))
    finally:
        process.stdin.close()
        process.wait(timeout=10)
    # Project discovery registers the admitted scene through the ordinary editor flow.
    write(root / 'acceptance.json', dict(scene='main.scene.json', collision_assets=collision,
                                        imported_model=model, imported_mesh=mesh))
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tools', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(make_project(args.tools.resolve(), args.output))
