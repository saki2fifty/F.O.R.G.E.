"""Admission and durable-identity check for shipped editable feature scenes."""
import json
import math
from pathlib import Path
import subprocess
import sys
import uuid

project_root = Path(sys.argv[2])
project = json.loads((project_root / 'forge.project.json').read_text(encoding="utf-8"))
assets = json.loads((project_root / 'forge.assets.json').read_text(encoding="utf-8"))['assets']
by_source = {row['source']: row for row in assets}
for source, cooked in (('Assets/Shape.mesh.json', 'mesh.bin'),
                       ('Assets/Terrain/LayeredHills.terrain.json', 'mesh.bin'),
                       ('Assets/Terrain/LayeredHills.collision.json', 'collision.bin'),
                       ('Assets/Bloom/WarmGlow.material.json', 'material.values'),
                       ('Assets/Bloom/CoolGlow.material.json', 'material.values'),
                       ('Assets/ModelRefinement/OriginalCube.mesh.json', 'mesh.bin'),
                       ('Assets/ModelRefinement/CenterSplitCube.mesh.json', 'mesh.bin'),
                       ('Assets/ModelRefinement/SubdividedCube.mesh.json', 'mesh.bin'),
                       ('Assets/Frame.mesh.json', 'mesh.bin'),
                       ('Assets/UvConnected.mesh.json', 'mesh.bin'),
                       ('Assets/UvSplit.mesh.json', 'mesh.bin'),
                       ('Assets/UvFitted.mesh.json', 'mesh.bin'),
                       ('Assets/UvPackingOriginal.mesh.json', 'mesh.bin'),
                       ('Assets/UvPackingPacked.mesh.json', 'mesh.bin'),
                       ('Assets/UvPackingRotated.mesh.json', 'mesh.bin'),
                       ('Assets/SculptOneSide.mesh.json', 'mesh.bin'),
                       ('Assets/SculptMirrored.mesh.json', 'mesh.bin'),
                       ('Assets/PaintMirrored.mesh.json', 'mesh.bin'),
                       ('Assets/PolygonCube.mesh.json', 'mesh.bin'),
                       ('Assets/TriangleCube.mesh.json', 'mesh.bin'),
                       ('Assets/TriangleSculpt.mesh.json', 'mesh.bin'),
                       ('Assets/FlatSphere.mesh.json', 'mesh.bin'),
                       ('Assets/SmoothSphere.mesh.json', 'mesh.bin'),
                       ('Assets/OutwardRoom.mesh.json', 'mesh.bin'),
                       ('Assets/InwardRoom.mesh.json', 'mesh.bin'),
                       ('Assets/RaisedPanel.mesh.json', 'mesh.bin'),
                       ('Assets/RecessedPanel.mesh.json', 'mesh.bin'),
                       ('Assets/SculptSphere.mesh.json', 'mesh.bin'),
                       ('Assets/PaintedSphere.mesh.json', 'mesh.bin'),
                       ('Assets/Checker.material.json', 'material.values'),
                       ('Assets/Platform.animation.json', 'animation.clip'),
                       ('Assets/EasedPlatform.animation.json', 'animation.clip'),
                       ('Assets/ArcPlatform.animation.json', 'animation.clip'),
                       ('Assets/DopplerPass.animation.json', 'animation.clip'),
                       ('Assets/BatchOriginal.animation.json', 'animation.clip'),
                       ('Assets/BatchShifted.animation.json', 'animation.clip'),
                       ('Assets/BatchRepeated.animation.json', 'animation.clip'),
                       ('Assets/AnimationTiming/OriginalTiming.animation.json', 'animation.clip'),
                       ('Assets/AnimationTiming/RetimedTiming.animation.json', 'animation.clip'),
                       ('Assets/AnimationTiming/FrameGridTiming.animation.json', 'animation.clip'),
                       ('Assets/AnimationValues/OriginalValues.animation.json', 'animation.clip'),
                       ('Assets/AnimationValues/PositionValues.animation.json', 'animation.clip'),
                       ('Assets/AnimationValues/ScaleValues.animation.json', 'animation.clip'),
                       ('Assets/AnimatedProperties/CameraZoom.animation.json', 'animation.clip'),
                       ('Assets/AnimatedProperties/LightPulse.animation.json', 'animation.clip'),
                       ('Assets/Weighted.mask.json', 'mask.json'),
                       ('Assets/Motion.blendspace.json', 'blendspace.json')):
    asset = by_source[source]
    assert json.loads((project_root / source).read_text(encoding="utf-8"))['asset_id'] == asset['id']
    assert (project_root / '.forge/cache/derived' /
            asset['metadata']['forge.import']['key'] / cooked).is_file()
scenes = sorted((project_root / 'Scenes').glob('*.scene.json'))
assert {'01-transforms.scene.json', '02-lighting.scene.json',
        '03-physics.scene.json', '04-cameras.scene.json',
        '05-editable-mesh.scene.json', '06-sculpt.scene.json',
        '07-vertex-paint.scene.json', '08-transform-animation.scene.json',
        '09-animation-blending.scene.json', '10-animation-states.scene.json',
        '38-animation-additive-overlays.scene.json', '39-animation-morph-states.scene.json',
        '40-two-bone-ik.scene.json', '41-joint-aim.scene.json', '42-animation-bone-masks.scene.json',
        '43-animation-blend-spaces.scene.json',
        '44-uv-island-packing.scene.json',
        '45-animation-timing.scene.json',
        '46-model-refinement.scene.json',
        '47-animation-key-values.scene.json',
        '48-hdr-bloom.scene.json', '49-animated-camera-and-light.scene.json', '50-navigation-progress.scene.json',
        '55-layered-heightfield-terrain.scene.json',
        '56-particle-shapes-and-lifetime.scene.json',
        '57-particle-emitter-appearance.scene.json',
        '58-simulation-speed.scene.json',
        '62-coordinated-animation.scene.json',
        '11-animation-markers.scene.json', '12-inset-panels.scene.json',
        '13-transform-curves.scene.json', '14-uv-islands.scene.json', '15-brush-symmetry.scene.json', '16-authored-triangulation.scene.json', '17-mesh-shading.scene.json', '18-animation-key-batches.scene.json', '19-mesh-facing.scene.json', '20-physics-joints.scene.json', '21-point-fixed-joints.scene.json', '22-continuous-collision.scene.json', '23-body-motion-tuning.scene.json', '24-slider-joints.scene.json', '25-velocity-motors.scene.json', '26-position-drives.scene.json', '27-connected-collisions.scene.json', '28-swing-twist-joints.scene.json', '29-distance-springs.scene.json', '30-animation-exits.scene.json', '31-audio-falloff.scene.json', '32-directional-audio.scene.json', '33-audio-mixer.scene.json', '34-audio-import-processing.scene.json', '35-moving-audio.scene.json', '36-animation-root-motion.scene.json', '37-animation-subtree-blending.scene.json'} <= {p.name for p in scenes}
assert project['version'] == 2 and project['input']['version'] == 1
assert project['game']['application_id'] == 'org.forge.feature-gallery'
assert project['game']['display']['mode'] == 'windowed'
assert project['startup_scene']['source'] == 'Scenes/' + scenes[0].name

process = subprocess.Popen([sys.argv[1], '--stdio'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True, encoding="utf-8")

def call(method, **fields):
    process.stdin.write(json.dumps(dict(api=1, method=method, **fields)) + '\n')
    process.stdin.flush()
    result = json.loads(process.stdout.readline())
    assert result['ok'], (method, result)
    return result

def same_authored_value(actual, expected, location='$'):
    # SceneDraft recasts reflected float32 values. Native JSON float parsing may
    # differ by a final float32 ULP across platforms; document structure and
    # discrete identities must still match exactly.
    if isinstance(expected, dict):
        assert isinstance(actual, dict) and actual.keys() == expected.keys(), location
        for key in expected:
            same_authored_value(actual[key], expected[key], location + '.' + key)
    elif isinstance(expected, list):
        assert isinstance(actual, list) and len(actual) == len(expected), location
        for index, (item, value) in enumerate(zip(actual, expected)):
            same_authored_value(item, value, f'{location}[{index}]')
    elif isinstance(expected, float):
        assert isinstance(actual, (float, int)) and not isinstance(actual, bool), location
        assert math.isclose(actual, expected, rel_tol=2e-7, abs_tol=2e-7), (
            location, actual, expected)
    else:
        assert actual == expected and type(actual) is type(expected), (
            location, actual, expected)

try:
    context = call('discover')['result']
    all_ids = set()
    for path in scenes:
        source = json.loads(path.read_text(encoding="utf-8"))
        assert source['version'] == 3
        assert str(uuid.UUID(source['asset_id'])) == source['asset_id']
        assert source['asset_id'] not in all_ids
        all_ids.add(source['asset_id'])
        ids = [entity['id'] for entity in source['entities']]
        # Scene-specific consumer tests own participant counts. Small UI and
        # particle examples legitimately have fewer objects than the early scenes.
        assert ids and len(ids) == len(set(ids)), (path.name, ids)
        for entity_id in ids:
            assert str(uuid.UUID(entity_id)) == entity_id and entity_id not in all_ids
            all_ids.add(entity_id)
        assert any('forge.camera' in row['components'] for row in source['entities'])
        result = call('scene.replace', target=context['target'],
                      expected_revision=context['revision'], document=source)
        context = call('discover')['result']
        loaded = call('scene.read', target=context['target'])['result']
        # Additive builtin settings: old source bytes remain unchanged while
        # admitted typed values explicitly expose their documented defaults.
        expected = json.loads(json.dumps(source))
        for row in expected['entities']:
            transform = row['components'].get('forge.transform_animator')
            if transform is not None:
                transform.setdefault('sequence', None)
                transform.setdefault('sequence_start_seconds', 0.)
            particles = row['components'].get('forge.particle_emitter')
            if particles is not None:
                for field in ('size_multiplier', 'tint_r', 'tint_g', 'tint_b'):
                    particles.setdefault(field, 1.)
            animator = row['components'].get('forge.animator')
            if animator is not None:
                animator.setdefault('root_motion', 0)
                animator.setdefault('root_joint', None)
                animator.setdefault('blend_root', None)
                animator.setdefault('blend_mode', 0)
                animator.setdefault('blend_mask', None)
                animator.setdefault('blend_space', None)
                animator.setdefault('blend_position', 0.)
            sound = row['components'].get('forge.audio_source')
            if sound is not None:
                sound.setdefault('cone_enabled', False)
                sound.setdefault('inner_angle', 60.)
                sound.setdefault('outer_angle', 120.)
                sound.setdefault('outer_gain', .25)
                sound.setdefault('bus', 0)
                sound.setdefault('doppler', 0.)
            body = row['components'].get('forge.physics_body')
            if body is not None:
                body.setdefault('continuous_collision', False)
                body.setdefault('linear_damping', .05)
                body.setdefault('angular_damping', .05)
                body.setdefault('max_linear_velocity', 500.)
                body.setdefault('max_angular_velocity', 2700.)
                body.setdefault('allow_sleeping', True)
            joint = row['components'].get('forge.physics_joint')
            if joint is not None:
                joint.setdefault('min_travel', -1.)
                joint.setdefault('max_travel', 1.)
                joint.setdefault('motor_enabled', False)
                joint.setdefault('hinge_motor_speed', 90.)
                joint.setdefault('slider_motor_speed', 1.)
                joint.setdefault('max_motor_torque', 10.)
                joint.setdefault('max_motor_force', 100.)
                joint.setdefault('motor_mode', 0)
                joint.setdefault('hinge_motor_angle', 0.)
                joint.setdefault('slider_motor_position', 0.)
                joint.setdefault('motor_frequency', 2.)
                joint.setdefault('motor_damping', 1.)
                joint.setdefault('collide_connected', True)
                joint.setdefault('swing_normal_angle', 45.)
                joint.setdefault('swing_plane_angle', 45.)
                joint.setdefault('distance_spring_frequency', 0.)
                joint.setdefault('distance_spring_damping', 1.)
        same_authored_value(loaded, expected, str(path))
        assert context['revision'] == result['revision']
        for entity in loaded['entities']:
            renderer = entity['components'].get('forge.mesh_renderer')
            if renderer and any(a['id'] == renderer.get('mesh') and
                                a['source'].endswith('.mesh.json') for a in assets):
                assert all(binding['slot'] == 'default' for binding in renderer['materials'])
        if path.name.startswith('16-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities'] if 'forge.mesh_renderer' in row['components']]
            assert len(meshes) == 3 and set(meshes) == {
                by_source['Assets/' + name]['id'] for name in
                ('PolygonCube.mesh.json', 'TriangleCube.mesh.json', 'TriangleSculpt.mesh.json')}
        if path.name.startswith('15-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert len(meshes) == 3 and set(meshes) == {
                by_source['Assets/' + name]['id'] for name in
                ('SculptOneSide.mesh.json', 'SculptMirrored.mesh.json', 'PaintMirrored.mesh.json')}
        if path.name.startswith('14-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert len(meshes) == 3 and set(meshes) == {
                by_source['Assets/' + name]['id'] for name in
                ('UvConnected.mesh.json', 'UvSplit.mesh.json', 'UvFitted.mesh.json')}
        if path.name.startswith('13-'):
            players = [row['components']['forge.transform_animator']
                       for row in loaded['entities']
                       if 'forge.transform_animator' in row['components']]
            assert len(players) == 3
            assert {row['clip'] for row in players} == {
                by_source['Assets/' + name]['id'] for name in
                ('Platform.animation.json', 'EasedPlatform.animation.json',
                 'ArcPlatform.animation.json')}
        if path.name.startswith('12-'):
            panels = [row['components']['forge.mesh_renderer']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert len(panels) == 3
            assert {row['mesh'] for row in panels} == {
                by_source['Assets/' + name]['id'] for name in
                ('Frame.mesh.json', 'RaisedPanel.mesh.json', 'RecessedPanel.mesh.json')}
            assert all(row['materials'] == [{'slot': 'default',
                       'material': by_source['Assets/Checker.material.json']['id']}]
                       for row in panels)
        if path.name.startswith('01-'):
            assert sum('parent' in row for row in loaded['entities']) == 3
        if path.name.startswith('02-'):
            assert {row['components']['forge.light']['kind'] for row in loaded['entities']
                    if 'forge.light' in row['components']} == {0, 1, 2}
        if path.name.startswith('03-'):
            assert sum(row['components'].get('forge.physics_body', {}).get('motion') == 2
                       for row in loaded['entities']) == 3
        if path.name.startswith('05-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/Shape.mesh.json']['id']]
            bindings = next(row['components']['forge.mesh_renderer']['materials']
                            for row in loaded['entities']
                            if 'forge.mesh_renderer' in row['components'])
            assert bindings == [{'slot': 'default',
                                 'material': by_source['Assets/Checker.material.json']['id']}]
        if path.name.startswith('06-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/SculptSphere.mesh.json']['id']]
        if path.name.startswith('07-'):
            meshes = [row['components']['forge.mesh_renderer']['mesh']
                      for row in loaded['entities']
                      if 'forge.mesh_renderer' in row['components']]
            assert meshes == [by_source['Assets/PaintedSphere.mesh.json']['id']]
            painted = json.loads((project_root / 'Assets/PaintedSphere.mesh.json').read_text(encoding="utf-8"))
            assert all('color' in vertex for vertex in painted['vertices'])
        if path.name.startswith('08-'):
            movers = [row['components']['forge.transform_animator'] for row in loaded['entities']
                      if 'forge.transform_animator' in row['components']]
            assert len(movers) == 1 and movers[0]['clip'] == by_source['Assets/Platform.animation.json']['id']
            assert movers[0]['loop'] and movers[0]['play_on_start']
        if path.name.startswith('09-'):
            players = [row['components']['forge.animator'] for row in loaded['entities']
                       if 'forge.animator' in row['components']]
            assert len(players) == 3 and sorted(p['blend_weight'] for p in players) == [0, .5, 1]
            assert len({p['skeleton'] for p in players}) == 1
            assert len({p['clip'] for p in players}) == 1
            assert len({p['blend_clip'] for p in players}) == 1
            assert players[0]['clip'] != players[0]['blend_clip']
            model = next(row for row in assets
                         if row['source'] == 'Assets/BlendColumn.gltf' and row['type'] == 'model')
            cooked = project_root / '.forge/cache/derived' / model['metadata']['forge.import']['key']
            assert (cooked/'skeleton.ozz').is_file()
            assert (cooked/'clip-0.ozz').is_file() and (cooked/'clip-1.ozz').is_file()
        if path.name.startswith('10-'):
            players = [row['components']['forge.animator'] for row in loaded['entities']
                       if 'forge.animator' in row['components']]
            assert sorted(p['state'] for p in players) == [1, 2, 3]
            states = by_source['Assets/Locomotion.states.json']
            assert all(p['states'] == states['id'] for p in players)
            graph = json.loads((project_root/states['source']).read_text(encoding="utf-8"))
            assert [(s['id'], s['name']) for s in graph['states']] == [(1, 'Idle'), (2, 'Walk'), (3, 'Run')]
            assert (project_root/'Assets/Quaternius-LICENSE.txt').is_file()
            assert (project_root/'Assets/QuaterniusLocomotion.glb').is_file()
        if path.name.startswith('04-'):
            cameras = [row['components']['forge.camera'] for row in loaded['entities']
                       if 'forge.camera' in row['components']]
            assert {camera['projection'] for camera in cameras} == {0, 1}
            assert sorted(camera['viewport_x'] for camera in cameras) == [0, .5]
        if path.name.startswith('30-'):
            players = [row['components']['forge.animator'] for row in loaded['entities']
                       if 'forge.animator' in row['components']]
            assert sorted(p['state'] for p in players) == [1, 2, 3]
            states = by_source['Assets/TimedLocomotion.states.json']
            assert all(p['states'] == states['id'] for p in players)
            graph = json.loads((project_root/states['source']).read_text(encoding="utf-8"))
            assert graph['version'] == 2
            assert [s['exit'] for s in graph['states']] == [
                dict(state=2, phase=1), dict(state=3, phase=1), dict(state=1, phase=1)]
        if path.name.startswith('31-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities']
                       if 'forge.audio_source' in row['components']]
            tone = by_source['Assets/FalloffTone.wav']
            assert sorted(s['attenuation'] for s in sources) == [0, 1, 2]
            assert all(s['clip'] == tone['id'] and s['rolloff'] == 1 and s['spatialized']
                       and s['loop'] for s in sources)
            assert sum(s['play_on_start'] for s in sources) == 1
            assert any('forge.audio_listener' in row['components'] for row in loaded['entities'])
        if path.name.startswith('32-'):
            speakers = [row for row in loaded['entities'] if 'forge.audio_source' in row['components']]
            assert len(speakers) == 1 and speakers[0].get('parent')
            cone = speakers[0]['components']['forge.audio_source']
            assert cone['cone_enabled'] and cone['inner_angle'] == 60 and cone['outer_angle'] == 120
            assert cone['outer_gain'] == .25 and cone['rolloff'] == 0
            assert cone['clip'] == by_source['Assets/FalloffTone.wav']['id']
        if path.name.startswith('33-'):
            channels = [row['components']['forge.audio_source'] for row in loaded['entities'] if 'forge.audio_source' in row['components']]
            assert len(channels) == 3 and {item['bus'] for item in channels} == {1,2,3}
            assert {item['pitch'] for item in channels} == {.75,1.,1.5}
            assert all(not item['spatialized'] and item['play_on_start'] and item['clip'] == by_source['Assets/FalloffTone.wav']['id'] for item in channels)
        if path.name.startswith('34-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities'] if 'forge.audio_source' in row['components']]
            original = by_source['Assets/ProcessingOriginal.wav']
            processed = by_source['Assets/ProcessingMono.wav']
            assert len(sources) == 2 and sum(item['play_on_start'] for item in sources) == 1
            assert next(item for item in sources if item['play_on_start'])['clip'] == processed['id']
            assert {item['clip'] for item in sources} == {original['id'], processed['id']}
            assert all(not item['spatialized'] and item['loop'] for item in sources)
            assert original['metadata']['forge.audio']['channels'] == 2 and original['metadata']['forge.audio']['frames'] == 67200
            assert processed['metadata']['forge.audio']['channels'] == 1 and processed['metadata']['forge.audio']['frames'] == 48000
            assert (project_root / original['source']).read_bytes() == (project_root / processed['source']).read_bytes()
            sidecar = json.loads((project_root / (processed['source'] + '.forge-import.json')).read_text(encoding="utf-8"))
            assert sidecar['settings']['overrides'] == dict(force_mono=True, normalize_peak=True, trim_silence=True, trim_threshold_db=-60)
            for asset in (original, processed):
                assert (project_root / '.forge/cache/derived' / asset['metadata']['forge.import']['key'] / 'audio.fpcm').is_file()
        if path.name.startswith('35-'):
            sounds = [row for row in loaded['entities'] if 'forge.audio_source' in row['components']]
            parents = [row for row in loaded['entities'] if 'forge.transform_animator' in row['components']]
            assert len(sounds) == 2 and len(parents) == 1
            assert {row['components']['forge.audio_source']['doppler'] for row in sounds} == {0.,1.}
            assert all(row['parent'] == parents[0]['id'] and row['spatial']['mode'] == 'follow_structure' for row in sounds)
            assert parents[0]['components']['forge.transform_animator']['clip'] == by_source['Assets/DopplerPass.animation.json']['id']
            assert sum(row['components']['forge.audio_source']['play_on_start'] for row in sounds) == 1
        if path.name.startswith('58-'):
            components = [row['components'] for row in loaded['entities']]
            assert sum('forge.physics_body' in row for row in components) == 2
            assert sum('forge.transform_animator' in row for row in components) == 1
            assert sum('forge.particle_emitter' in row for row in components) == 1
            assert sum('forge.ui_document' in row for row in components) == 1
            hud = next(row for row in components if 'forge.ui_document' in row)
            assert hud['forge.ui_document']['document'] == by_source['Assets/UI/SimulationSpeed.rml']['id']
            assert hud['forge.primitive']['kind'] == 4
            assert next(row['forge.transform_animator']['clip'] for row in components
                        if 'forge.transform_animator' in row) == by_source['Assets/AnimationTiming/OriginalTiming.animation.json']['id']
            assert next(row['forge.particle_emitter']['effect'] for row in components
                        if 'forge.particle_emitter' in row) == by_source['Assets/Particles/SharedJet.particles.json']['id']
            assert not any('time_scale' in key for row in components for key in row)
        if path.name.startswith('36-'):
            actors = [row for row in loaded['entities'] if 'forge.animator' in row['components']]
            assert len(actors) == 3
            assert {row['components']['forge.animator']['root_motion'] for row in actors} == {0, 1, 2}
            records = {record['id']: record for record in assets}
            for actor in actors:
                animator = actor['components']['forge.animator']
                model = actor['components']['forge.model_source']['model']
                assert records[model]['source'] == 'Assets/LocomotionColumn.gltf'
                for field, kind in [('root_joint', 'model_node'), ('clip', 'animation_clip'), ('skeleton', 'skeleton')]:
                    record = records[animator[field]]
                    assert record['type'] == kind and record['subasset']['owner'] == model
    assert project['startup_scene']['asset'] == json.loads(scenes[0].read_text(encoding="utf-8"))['asset_id']
finally:
    process.stdin.close()
    assert process.wait(timeout=10) == 0
print(f'Feature Gallery: {len(scenes)} authored scenes admitted and unchanged by FORGE')
