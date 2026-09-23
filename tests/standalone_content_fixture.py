"""Compose shipped runtime families using real native bake/conversion/import tools.
Only test source construction is here; export and runtime use production services.
"""
import copy
import json
import math
import os
from pathlib import Path
import shutil
import struct
import uuid
import wave


def complete_content(project, work, build, source, run):
    binary = lambda name: build/(name + ('.exe' if os.name == 'nt' else ''))
    scene = json.loads((project/'main.scene.json').read_text())
    settings = json.loads((project/'forge.project.json').read_text())
    catalog = json.loads((project/'forge.assets.json').read_text())
    prior_scene = scene['asset_id']
    nav = Path(run([binary('forge_navigation_tests'), work/'nav', binary('forge_nav_build'), '--prepare']).stdout.strip())
    animation = Path(run([binary('forge_animation_tests'), work/'animation', binary('tools/gltf2ozz'), source/'samples/animation/two-joints.gltf', '--prepare']).stdout.strip())
    navigation_scene = json.loads((nav/'scene.json').read_text())
    animation_scene = json.loads((animation/'scene.json').read_text())
    for prepared in (nav, animation):
        catalog['assets'] += json.loads((prepared/'forge.assets.json').read_text())['assets']
        for path in prepared.rglob('*'):
            if not path.is_file() or path.name in ('scene.json', 'forge.assets.json'):
                continue
            relative = path.relative_to(prepared)
            if relative.parts[0] == '.forge' and len(relative.parts) > 1 and relative.parts[1] == 'writer.lock':
                continue
            destination = project/relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            assert not destination.exists(), relative
            shutil.copy2(path, destination)
    # Keep the navigation scene's durable identity and geometry unchanged.
    # Camera/cube move beyond its obstacle and have no NavigationSurface component.
    for row in scene['entities']:
        if row['name'] == 'Camera':
            row['components']['forge.local_translation'] = dict(x=0, y=0, z=5)
        elif row['name'] == 'Cube':
            row['components']['forge.local_translation'] = dict(x=0, y=0, z=8)
    navigation_scene['version'] = max(navigation_scene['version'], scene['version'])
    navigation_scene.setdefault('legacy_ids', {}).update(scene.get('legacy_ids', {}))
    navigation_scene['entities'] += scene['entities']
    actor = copy.deepcopy(scene['entities'][0])
    actor['id'], actor['name'] = str(uuid.uuid4()), 'Animated actor'
    actor['components'] = {'forge.animator': animation_scene['entities'][0]['components']['forge.animator'],
                           'forge.primitive': next(e for e in scene['entities'] if e['name']=='Camera')['components']['forge.primitive']}
    navigation_scene['entities'].append(actor)
    tone = project/'tone.wav'
    with wave.open(str(tone),'wb') as audio:
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(48000)
        audio.writeframes(b''.join(struct.pack('<h',int(1000*math.sin(i*.06))) for i in range(48000)))
    # Runtime AudioClip publication goes through the ordinary importer.
    catalog['assets'] = [r for r in catalog['assets'] if r['id'] != prior_scene]
    catalog['assets'].append(dict(id=navigation_scene['asset_id'], type='scene', source='main.scene.json',
                                  schema_version=navigation_scene['version'], dependencies=[], metadata={}, dependency_edges=[]))
    (project/'forge.assets.json').write_text(json.dumps(catalog))
    imported = json.loads(run([binary('forge_tools'), '--assets', 'import', project, 'tone.wav']).stdout)
    speaker = copy.deepcopy(actor)
    speaker['id'], speaker['name'] = str(uuid.uuid4()), 'Speaker'
    speaker['components'] = {'forge.primitive': actor['components']['forge.primitive'],
        'forge.audio_source': dict(clip=imported['asset'], play_on_start=True, loop=True, gain=1,
                                  pitch=1, spatialized=False, minimum_distance=1, maximum_distance=100)}
    navigation_scene['entities'].append(speaker)
    # A real glTF material/texture/mesh family, consumed by an additional draw.
    (project/'Rendering').mkdir()
    for path in (source/'samples/projects/Rendering/Assets').iterdir():
        if path.is_file(): shutil.copy2(path, project/'Rendering'/path.name)
    imported = json.loads(run([binary('forge_tools'), '--assets', 'import', project, 'Rendering/Rendering.gltf']).stdout)
    catalog = json.loads((project/'forge.assets.json').read_text())
    # UUID sort order is not source mesh order. The floor can be first and is
    # edge-on to this camera; require the known textured cube from this fixture.
    meshes = [r for r in catalog['assets'] if r['type']=='mesh'
              and r['subasset']['owner']==imported['asset']
              and r['metadata']['forge.model']['name']=='Cube']
    assert len(meshes) == 1, 'Rendering fixture needs exactly one Cube mesh'
    mesh = meshes[0]
    model = copy.deepcopy(next(e for e in scene['entities'] if e['name']=='Cube'))
    model['id'], model['name'] = str(uuid.uuid4()), 'Imported renderable'
    model['components']['forge.local_translation'] = dict(x=1.5, y=0, z=8)
    model['components']['forge.mesh_renderer']['mesh'] = mesh['id']
    model['components']['forge.mesh_renderer']['materials'] = []  # Native mesh bindings select its imported material.
    navigation_scene['entities'].append(model)
    (project/'main.scene.json').write_text(json.dumps(navigation_scene))
    settings['startup_scene'] = dict(asset=navigation_scene['asset_id'], source='main.scene.json')
    (project/'forge.project.json').write_text(json.dumps(settings))
    # Generated source folders are not runtime inputs after export.
    shutil.rmtree(nav); shutil.rmtree(animation)
