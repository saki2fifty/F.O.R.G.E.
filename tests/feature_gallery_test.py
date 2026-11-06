"""Admission and durable-identity check for shipped editable feature scenes."""
import json
import math
import re
from pathlib import Path
import subprocess
import struct
import sys
import uuid

project_root = Path(sys.argv[2])
project = json.loads((project_root / 'forge.project.json').read_text(encoding="utf-8"))
assets = json.loads((project_root / 'forge.assets.json').read_text(encoding="utf-8"))['assets']
by_source = {row['source']: row for row in assets}
for row in assets:
    if row['type'] == 'scene':
        saved = json.loads((project_root / row['source']).read_text(encoding='utf-8'))
        assert 3 <= saved['version'] <= 5 and 3 <= row['schema_version'] <= 5, \
            f"Gallery scene is not admitted by runtime packaging: {row['source']}"
for source, cooked in (('Assets/Shape.mesh.json', 'mesh.bin'),
                       ('Assets/RenderTextures/Split.rendertexture.json', 'render-texture.json'),
                       ('Assets/RenderTextures/Relay.rendertexture.json', 'render-texture.json'),
                       ('Assets/RenderTextures/Spin.animation.json', 'animation.clip'),
                       ('Assets/Physics2D/Solid.material.json', 'material.values'),
                       ('Assets/Physics2D/Platform.animation.json', 'animation.clip'),
                       ('Assets/Navigation/MovingObstacle3D.animation.json', 'animation.clip'),
                       ('Assets/Navigation/MovingObstacleXY.animation.json', 'animation.clip'),
                       ('Assets/Tiles/Directions.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/AnimatedDirections.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/AnimatedDirections.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/NeighborDirections.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/NeighborDirections.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/CollisionProfiles.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/CollisionProfiles.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/ConcaveTiles.material.json', 'material.values'),
                       ('Assets/Tiles/ConcaveTiles.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/ConcaveTiles.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/ContourTiles.material.json', 'material.values'),
                       ('Assets/Tiles/ContourTiles.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/ContourTiles.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/DynamicTiles.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/ChunkResidency.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/ChunkResidencyCamera.animation.json', 'animation.clip'),
                       ('Assets/Tiles/ArrowCourse.tilemap.json', 'tilemap.bin'),
                       ('Assets/Tiles/PaintedPlatforms.tilemap.json', 'tilemap.bin'),
                       ('Assets/Sprites/Badge.material.json', 'material.values'),
                       ('Assets/Sprites/SignalSheet.material.json', 'material.values'),
                       ('Assets/Sprites/Panel.material.json', 'material.values'),
                       ('Assets/Sprites/GlowShapes.material.json', 'material.values'),
                       ('Assets/Tiles/GlowShapes.tileset.json', 'tileset.bin'),
                       ('Assets/Tiles/GlowShapes.tilemap.json', 'tilemap.bin'),
                       ('Assets/Sprites/PackedAtlas.material.json', 'material.values'),
                       ('Assets/Sprites/SignalFrames.animation.json', 'animation.clip'),
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
                       ('Assets/ObjectValues/LinearAppearance.animation.json', 'animation.clip'),
                       ('Assets/ObjectValues/SmoothAppearance.animation.json', 'animation.clip'),
                       ('Assets/ObjectValues/HeldAppearance.animation.json', 'animation.clip'),
                       ('Assets/Weighted.mask.json', 'mask.json'),
                       ('Assets/Motion.blendspace.json', 'blendspace.json'),
                       ('Assets/MotionPlane.blendspace.json', 'blendspace.json')):
    asset = by_source[source]
    assert json.loads((project_root / source).read_text(encoding="utf-8"))['asset_id'] == asset['id']
    assert (project_root / '.forge/cache/derived' /
            asset['metadata']['forge.import']['key'] / cooked).is_file()
residency_source = 'Scenes/126-tile-chunk-residency.scene.json'
residency_scene = json.loads((project_root / residency_source).read_text(encoding='utf-8'))
assert residency_scene['asset_id'] == by_source[residency_source]['id']
residency_cameras = [row['components'] for row in residency_scene['entities']
                     if 'forge.camera' in row['components']]
assert len(residency_cameras) == 1
# An absent legacy primitive defaults to a cube, including on a Camera.
assert residency_cameras[0]['forge.primitive']['kind'] == 4
assert residency_cameras[0]['forge.transform_animator']['clip'] == \
    by_source['Assets/Tiles/ChunkResidencyCamera.animation.json']['id']

for plane, source in enumerate(('Scenes/129-moving-navigation-3d.scene.json',
                                'Scenes/130-moving-navigation-xy.scene.json')):
    moving = json.loads((project_root / source).read_text(encoding='utf-8'))
    assert moving['asset_id'] == by_source[source]['id']
    blockers = [row['components'] for row in moving['entities']
                if 'forge.navigation_obstacle' in row['components']]
    agents = [row['components']['forge.navigation_agent'] for row in moving['entities']
              if 'forge.navigation_agent' in row['components']]
    assert len(blockers) == len(agents) == 1
    assert blockers[0]['forge.navigation_obstacle'] == dict(enabled=True, radius=.5, height=1)
    assert blockers[0]['forge.transform_animator']['enabled']
    assert not {'forge.navigation_surface', 'forge.navigation_region_2d'} & set(blockers[0])
    selected = next(row for row in assets if row['id'] == agents[0]['navmesh'])
    assert selected['type'] == 'navmesh' and selected['schema_version'] == 4
    assert selected['metadata']['geometry_version'] == (3 if plane else 2)
    assert selected['metadata']['settings']['dynamic_obstacles'] is True
    assert selected['metadata']['settings'].get('plane', 0) == plane

link_source = 'Scenes/140-navigation-links.scene.json'
link_scene = json.loads((project_root / link_source).read_text(encoding='utf-8'))
assert link_scene['asset_id'] == by_source[link_source]['id']
links = [row for row in link_scene['entities'] if 'forge.navigation_link' in row['components']]
assert len(links) == 2
for row in links:
    link = row['components']['forge.navigation_link']
    assert link['plane'] == 1 and link['enabled'] and link['start_z'] == link['end_z'] == 0
    nav = next(asset for asset in assets if asset['type'] == 'navmesh' and
               asset['metadata']['source_scene'] == link_scene['asset_id'] and
               asset['metadata']['settings'].get('profile') == link['profile'])
    assert nav['schema_version'] == nav['metadata']['version'] == 6
    assert nav['metadata']['geometry_version'] == 3
    assert len(nav['metadata']['links']) == 1 and nav['metadata']['links'][0]['entity'] == row['id']
    assert nav['metadata']['settings'].get('dynamic_obstacles', False) is (link['profile'] == 'Loop')
    assert link['bidirectional'] is (link['profile'] == 'Loop')

tree_source = 'Scenes/139-behavior-tree.scene.json'
tree_scene = json.loads((project_root / tree_source).read_text(encoding='utf-8'))
assert tree_scene['asset_id'] == by_source[tree_source]['id']
tree_asset = by_source['Assets/Navigation/PatrolChaseTree.behavior.json']
tree_graph = json.loads((project_root / tree_asset['source']).read_text(encoding='utf-8'))
assert tree_graph['version'] == 2 and tree_graph['asset_id'] == tree_asset['id']
tree_nodes = {node['id']: node for node in tree_graph['nodes']}
assert tree_nodes[tree_graph['entry']]['kind'] == 'selector'
assert tree_nodes[1]['children'] == [2, 5] and tree_nodes[2]['children'] == [3, 4]
assert tree_nodes[5]['children'] == [6, 7, 8]
assert {node['kind'] for node in tree_nodes.values()} == {
    'selector', 'reactive_sequence', 'target_near', 'follow_target', 'sequence', 'wait', 'move_to_point'}
tree_actor = next(row for row in tree_scene['entities'] if 'forge.navigation_behavior' in row['components'])
assert tree_actor['components']['forge.navigation_behavior']['graph'] == tree_asset['id']
tree_target = next(row for row in tree_scene['entities'] if 'forge.transform_animator' in row['components'])
assert tree_target['components']['forge.transform_animator']['clip'] == \
    by_source['Assets/Navigation/ChaseTarget.animation.json']['id']
tree_nav = next(row for row in assets if row['id'] == tree_actor['components']['forge.navigation_agent']['navmesh'])
assert tree_nav['metadata']['source_scene'] == tree_scene['asset_id']
assert tree_nav['metadata']['settings']['plane'] == 1
assert (project_root / '.forge/cache/derived' / tree_asset['metadata']['forge.import']['key'] / 'behavior.json').is_file()

profile_source = 'Scenes/138-navigation-profiles.scene.json'
profile_scene = json.loads((project_root / profile_source).read_text(encoding='utf-8'))
assert profile_scene['asset_id'] == by_source[profile_source]['id']
profile_agents = [row['components']['forge.navigation_agent'] for row in profile_scene['entities']
                  if 'forge.navigation_agent' in row['components']]
assert len(profile_agents) == 2 and len({row['navmesh'] for row in profile_agents}) == 2
assert all(row['components']['forge.navigation_avoidance']['shared_group'] == 1
           for row in profile_scene['entities'] if 'forge.navigation_agent' in row['components'])
profile_records = [row for row in assets if row['id'] in {agent['navmesh'] for agent in profile_agents}]
profile_settings = {row['metadata']['settings'].get('profile', 'default'): row['metadata']['settings']
                    for row in profile_records}
assert set(profile_settings) == {'default', 'Large'}
assert all(row['metadata']['source_scene'] == profile_scene['asset_id'] for row in profile_records)
assert all(row['schema_version'] == row['metadata']['geometry_version'] == 3 for row in profile_records)
assert math.isclose(profile_settings['default']['radius'], .2, abs_tol=1e-6)
assert math.isclose(profile_settings['Large']['radius'], 1.8, abs_tol=1e-6)
assert 'profile' not in profile_settings['default']

collision_source = 'Scenes/137-navigation-collision-sources.scene.json'
collision_scene = json.loads((project_root / collision_source).read_text(encoding='utf-8'))
assert collision_scene['asset_id'] == by_source[collision_source]['id']
collision_regions = [row['components'] for row in collision_scene['entities']
                     if 'forge.navigation_region_2d' in row['components']]
assert len(collision_regions) == 5
assert sum(row['forge.navigation_region_2d']['source'] == 1 for row in collision_regions) == 4
assert {key for row in collision_regions for key in row if key.endswith('_collider_2d')} == {
    'forge.box_collider_2d', 'forge.circle_collider_2d', 'forge.capsule_collider_2d',
    'forge.tile_map_collider_2d'}
collision_agents = [row['components']['forge.navigation_agent'] for row in collision_scene['entities']
                    if 'forge.navigation_agent' in row['components']]
assert len(collision_agents) == 2 and collision_agents[0]['navmesh'] == collision_agents[1]['navmesh']
collision_nav = next(row for row in assets if row['id'] == collision_agents[0]['navmesh'])
assert collision_nav['schema_version'] == collision_nav['metadata']['geometry_version'] == 5
assert [row['asset_id'] for row in collision_nav['metadata']['tile_inputs']] == [
    by_source['Assets/Tiles/CollisionProfiles.tilemap.json']['id']]

glow_path = 'Scenes/128-sprite-tile-bloom.scene.json'
glow_scene = json.loads((project_root / glow_path).read_text(encoding='utf-8'))
assert glow_scene['asset_id'] == by_source[glow_path]['id']
assert glow_scene['rendering']['bloom'] == dict(enabled=True, threshold=.25, intensity=1.25, radius=3)
glow_sprites = [row['components']['forge.sprite_renderer'] for row in glow_scene['entities']
                if 'forge.sprite_renderer' in row['components']]
glow_maps = [row['components']['forge.tile_map_renderer'] for row in glow_scene['entities']
             if 'forge.tile_map_renderer' in row['components']]
assert len(glow_sprites) == 3 and len(glow_maps) == 1
assert {row['frame'] for row in glow_sprites} == {0, 1, 2}
assert all(row['columns'] == 4 and row['layers'] == 1 and
           row['material'] == by_source['Assets/Sprites/GlowShapes.material.json']['id']
           for row in glow_sprites)
assert glow_maps[0]['layers'] == 2 and glow_maps[0]['map'] == \
    by_source['Assets/Tiles/GlowShapes.tilemap.json']['id']
assert (project_root / 'Assets/Sprites/GlowShapes.png').is_file()

# The saved capture is an ordinary, stable Texture/Scene fixture. Its direct
# camera masks keep the emissive source panels out of the game while the supplied
# cube records them; the sphere receivers are excluded from capture mask1.
capture_source = 'Scenes/110-reflection-map-capture.scene.json'
capture_scene = json.loads((project_root / capture_source).read_text(encoding='utf-8'))
assert capture_scene['asset_id'] == by_source[capture_source]['id']
capture_rows = capture_scene['entities']
probes = [row['components']['forge.reflection_probe'] for row in capture_rows
          if 'forge.reflection_probe' in row['components']]
assert len(probes) == 1 and probes[0]['enabled'] and not probes[0]['box_projection']
assert probes[0]['rotation_degrees'] == 0
capture_map = by_source['Assets/ReflectionCapture/Reflection.dds']
assert capture_map['id'] == probes[0]['texture'] and capture_map['type'] == 'texture'
assert not capture_scene['rendering']['screen_space_reflections']['enabled']
assert not capture_scene['rendering']['environment']['sky']
cameras = [row['components']['forge.camera'] for row in capture_rows
           if 'forge.camera' in row['components']]
meshes = [row for row in capture_rows if 'forge.mesh_renderer' in row['components']]
assert len(cameras) == 1 and cameras[0]['layers'] == 2 and len(meshes) == 17
spheres = [row for row in meshes if 'captured-map sphere' in row['name']]
panels = [row for row in meshes if 'capture source' in row['name']]
assert len(spheres) == len(panels) == 2
assert all(row['components']['forge.mesh_renderer']['layers'] == 2 for row in spheres)
assert all(row['components']['forge.mesh_renderer']['layers'] == 1 for row in panels)
assert all(row['components']['forge.mesh_renderer']['layers'] == 3
           for row in meshes if row not in spheres + panels)
dds = (project_root / capture_map['source']).read_bytes()
assert dds[:4] == b'DDS ' and len(dds) == 148 + 6 * 128 * 128 * 8
assert struct.unpack_from('<II', dds, 12) == (128, 128)
assert struct.unpack_from('<IIIII', dds, 128) == (10, 3, 4, 1, 3)
assert capture_map['metadata']['forge.import']['platform'] == 'windows'
assert capture_map['metadata']['forge.import']['backend'] == 'd3d12'

# Contact-hardening is authored on one ordinary Light; the four PBR meshes
# receive real shadow depth rather than the legacy unlit blockout profile.
soft_source = 'Scenes/111-contact-hardening-shadows.scene.json'
soft_scene = json.loads((project_root / soft_source).read_text(encoding='utf-8'))
assert soft_scene['asset_id'] == by_source[soft_source]['id']
soft_lights = [row['components']['forge.light'] for row in soft_scene['entities']
               if 'forge.light' in row['components'] and row['components']['forge.light']['enabled']]
assert len(soft_lights) == 1 and soft_lights[0]['cast_shadows']
assert soft_lights[0]['kind'] == 0 and math.isclose(soft_lights[0]['shadow_angular_radius'], 0.08, abs_tol=1e-7)
assert soft_lights[0]['shadow_source_radius'] == 0
assert sum('forge.mesh_renderer' in row['components'] for row in soft_scene['entities']) == 4
assert soft_scene['rendering']['shadows']['max_lights'] == 1

# Area emission is stored on the existing Light component, with no extra assets.
area_source = 'Scenes/112-area-lights.scene.json'
area_scene = json.loads((project_root / area_source).read_text(encoding='utf-8'))
assert area_scene['asset_id'] == by_source[area_source]['id']
area_lights = [row['components']['forge.light'] for row in area_scene['entities']
               if 'forge.light' in row['components'] and row['components']['forge.light']['enabled']]
assert {light['kind'] for light in area_lights} == {3, 4} and len(area_lights) == 2
assert all(not light['cast_shadows'] for light in area_lights)
rectangle, = [light for light in area_lights if light['kind'] == 3]
sphere, = [light for light in area_lights if light['kind'] == 4]
assert math.isclose(rectangle['emitter_width'], 2.4, abs_tol=1e-6)
assert math.isclose(rectangle['emitter_height'], .55, abs_tol=1e-6)
assert math.isclose(sphere['emitter_radius'], .8, abs_tol=1e-6)
assert sum('forge.mesh_renderer' in row['components'] for row in area_scene['entities']) == 4

message_scene = json.loads((project_root / 'Scenes/150-input-messages.scene.json').read_text())
assert message_scene['asset_id'] == by_source['Scenes/150-input-messages.scene.json']['id']
message_sources = [row for row in message_scene['entities'] if 'forge.input_message' in row['components']]
assert len(message_sources) == 2
message_actions = {row['id']: row for row in project['input']['actions']}
for row in message_sources:
    message = row['components']['forge.input_message']
    assert message == dict(enabled=True, action=message['action'], payload='forge.local_translation')
    assert message['payload'] in row['components']
    assert message_actions[message['action']]['kind'] == 'digital'
    assert message_actions[message['action']]['bindings'] == [dict(control='key.space')]
assert (project_root / 'CodeExamples/InputMessages.cpp').is_file()
fog_path = 'Scenes/160-local-fog-volumes.scene.json'
fog_scene = json.loads((project_root / fog_path).read_text())
assert fog_scene['asset_id'] == by_source[fog_path]['id']
fog_values = [row['components']['forge.fog_volume'] for row in fog_scene['entities']
              if 'forge.fog_volume' in row['components']]
assert len(fog_values) == 2 and {row['shape'] for row in fog_values} == {0, 1}
assert all(row['enabled'] and row['density'] > 0 for row in fog_values)
assert sum('forge.camera' in row['components'] for row in fog_scene['entities']) == 1

trigger_scene = json.loads((project_root / 'Scenes/159-input-message-triggers.scene.json').read_text())
assert trigger_scene['asset_id'] == by_source['Scenes/159-input-message-triggers.scene.json']['id']
trigger_sources = [row for row in trigger_scene['entities'] if 'forge.input_message' in row['components']]
assert len(trigger_sources) == 3
assert sorted(row['components']['forge.input_message']['trigger'] for row in trigger_sources) == [1, 2, 2]
for row in trigger_sources:
    message = row['components']['forge.input_message']
    kind = message_actions[message['action']]['kind']
    assert message['payload'] == 'forge.local_translation' and message['payload'] in row['components']
    assert (message['trigger'] == 1 and kind == 'digital') or (message['trigger'] == 2 and kind in ('axis1', 'axis2'))
for example in ('InputMessageTriggers.cpp', 'InputMessageTriggers.hpp'):
    assert (project_root / 'CodeExamples' / example).is_file()

ui_sequence = json.loads((project_root / 'Scenes/151-sequence-ui.scene.json').read_text())
assert ui_sequence['asset_id'] == by_source['Scenes/151-sequence-ui.scene.json']['id']
ui_hud = next(row for row in ui_sequence['entities'] if 'forge.ui_values' in row['components'])
assert ui_hud['components']['forge.ui_values']['values'] == [dict(name='amount', value=.2), dict(name='width', value=80.)]
ui_clip = by_source['Assets/Sequences/UiValues.animation.json']
assert ui_hud['components']['forge.transform_animator']['clip'] == ui_clip['id']
ui_keys = json.loads((project_root / ui_clip['source']).read_text())
assert ui_keys['version'] == 9 and ui_keys['channels'] == []
assert len(ui_keys['component_bindings']) == 2 and len(ui_keys['keys']) == 3
assert {row['element_path'][0]['key'] for row in ui_keys['component_bindings']} == {'amount', 'width'}
assert all(row['admission']['format'] == 'forge.builtin-component' for row in ui_keys['component_bindings'])
ui_controllers = [row['components']['forge.animation_sequence'] for row in ui_sequence['entities'] if 'forge.animation_sequence' in row['components']]
assert sorted(row['duration'] for row in ui_controllers) == [2, 4]
assert next(row for row in ui_controllers if row['duration'] == 2)['sequence_start_seconds'] == 1

camera_images = json.loads((project_root / 'Scenes/152-camera-render-textures.scene.json').read_text())
assert camera_images['asset_id'] == by_source['Scenes/152-camera-render-textures.scene.json']['id']
outputs = {by_source['Assets/RenderTextures/' + name + '.rendertexture.json']['id'] for name in ('Split', 'Relay')}
producers = [row['components']['forge.camera'] for row in camera_images['entities']
             if row['components'].get('forge.camera', {}).get('render_texture')]
assert len(producers) == 3 and {row['render_texture'] for row in producers} == outputs
assert sorted(row['layers'] for row in producers) == [1, 1, 4]
assert len([row for row in camera_images['entities'] if 'forge.transform_animator' in row['components']]) == 2
for name, output in (('Split', 'Split'), ('Relay', 'Relay')):
    material = json.loads((project_root / ('Assets/RenderTextures/' + name + '.material.json')).read_text())
    assert material['version'] == 3
    assert material['overrides']['textures']['baseColorTexture']['render_texture'] == by_source['Assets/RenderTextures/' + output + '.rendertexture.json']['id']

transitions = json.loads((project_root / 'Scenes/153-camera-transitions.scene.json').read_text())
assert transitions['asset_id'] == by_source['Scenes/153-camera-transitions.scene.json']['id']
transition_shots = sorted((row['components']['forge.camera_shot'] for row in transitions['entities']
                           if 'forge.camera_shot' in row['components']), key=lambda row: row['start_seconds'])
assert [(row['start_seconds'], row['end_seconds'], row['blend_in_seconds']) for row in transition_shots] == [(0, 2, 0), (2, 4, 1), (4, 5.5, 1)]
assert len({row['camera']['entity'] for row in transition_shots}) == 2
rigs = [row for row in transitions['entities'] if 'forge.transform_animator' in row['components']]
assert len(rigs) == 1 and rigs[0]['components']['forge.transform_animator']['clip'] == by_source['Assets/CameraTransitions/Rig.animation.json']['id']

camera_ui = json.loads((project_root / 'Scenes/154-camera-output-ui.scene.json').read_text())
assert camera_ui['asset_id'] == by_source['Scenes/154-camera-output-ui.scene.json']['id']
panels = [row for row in camera_ui['entities'] if 'forge.ui_document' in row['components']]
assert len(panels) == 2
routed = next(row for row in panels if row['components']['forge.ui_document']['render_texture'])
assert routed['components']['forge.ui_document']['render_texture'] == by_source['Assets/RenderTextures/Split.rendertexture.json']['id']
assert routed['components']['forge.ui_document']['document'] == by_source['Assets/UI/CameraOutput/Telemetry.rml']['id']
assert routed['components']['forge.ui_values']['values'] == [dict(name='amount', value=1)]
assert len({row['id'] for row in camera_ui['entities']} & {row['id'] for row in camera_images['entities']}) == 0
assert len([row for row in camera_ui['entities'] if 'forge.camera' in row['components']]) == 4

filters = json.loads((project_root / 'Scenes/155-ui-filters.scene.json').read_text())
assert filters['asset_id'] == by_source['Scenes/155-ui-filters.scene.json']['id']
filter_hud = next(row for row in filters['entities'] if 'forge.ui_document' in row['components'])
assert filter_hud['components']['forge.ui_document']['document'] == by_source['Assets/UI/Filters/Lab.rml']['id']
assert filter_hud['components']['forge.ui_values']['values'] == [dict(name='blur', value=4)]
assert by_source['Assets/UI/Filters/Lab.rml']['metadata']['forge.ui_source']['runtime_features'] == dict(filters=2)
assert len([row for row in filters['entities'] if 'forge.camera' in row['components']]) == 1
assert not {row['id'] for row in filters['entities']} & {row['id'] for row in camera_ui['entities']}
shadows = json.loads((project_root / 'Scenes/162-ui-drop-shadows.scene.json').read_text())
assert shadows['asset_id'] == by_source['Scenes/162-ui-drop-shadows.scene.json']['id']
shadow_hud = next(row for row in shadows['entities'] if 'forge.ui_document' in row['components'])
assert shadow_hud['components']['forge.ui_document']['document'] == by_source['Assets/UI/Shadows/Lab.rml']['id']
assert shadow_hud['components']['forge.ui_values']['values'] == [dict(name='blur', value=4.0)]
assert by_source['Assets/UI/Shadows/Lab.rml']['metadata']['forge.ui_source']['runtime_features'] == dict(filters=2)
assert not {row['id'] for row in shadows['entities']} & {row['id'] for row in filters['entities']}
backdrops = json.loads((project_root / 'Scenes/163-ui-backdrop-filters.scene.json').read_text())
assert backdrops['asset_id'] == by_source['Scenes/163-ui-backdrop-filters.scene.json']['id']
backdrop_hud = next(row for row in backdrops['entities'] if 'forge.ui_document' in row['components'])
assert backdrop_hud['components']['forge.ui_document']['document'] == by_source['Assets/UI/Backdrops/Lab.rml']['id']
assert backdrop_hud['components']['forge.ui_values']['values'] == [dict(name='blur', value=4)]
assert by_source['Assets/UI/Backdrops/Lab.rml']['metadata']['forge.ui_source']['runtime_features'] == dict(filters=3)
assert not {row['id'] for row in backdrops['entities']} & {row['id'] for row in shadows['entities']}
gradients = json.loads((project_root / 'Scenes/164-ui-linear-gradients.scene.json').read_text())
assert gradients['asset_id'] == by_source['Scenes/164-ui-linear-gradients.scene.json']['id']
gradient_hud = next(row for row in gradients['entities'] if 'forge.ui_document' in row['components'])
assert gradient_hud['components']['forge.ui_document']['document'] == by_source['Assets/UI/Gradients/Lab.rml']['id']
assert gradient_hud['components']['forge.ui_values']['values'] == [dict(name='angle', value=45)]
assert by_source['Assets/UI/Gradients/Lab.rml']['metadata']['forge.ui_source']['runtime_features'] == dict(gradients=2)
assert not {row['id'] for row in gradients['entities']} & {row['id'] for row in backdrops['entities']}
curved = json.loads((project_root / 'Scenes/165-ui-radial-conic-gradients.scene.json').read_text())
assert curved['asset_id'] == by_source['Scenes/165-ui-radial-conic-gradients.scene.json']['id']
curved_hud = next(row for row in curved['entities'] if 'forge.ui_document' in row['components'])
assert curved_hud['components']['forge.ui_document']['document'] == by_source['Assets/UI/CurvedGradients/Lab.rml']['id']
assert curved_hud['components']['forge.ui_values']['values'] == [dict(name='angle', value=45)]
assert by_source['Assets/UI/CurvedGradients/Lab.rml']['metadata']['forge.ui_source']['runtime_features'] == dict(gradients=2)
assert not {row['id'] for row in curved['entities']} & {row['id'] for row in gradients['entities']}
masked = json.loads((project_root / 'Scenes/166-ui-alpha-masks.scene.json').read_text())
assert masked['asset_id'] == by_source['Scenes/166-ui-alpha-masks.scene.json']['id']
mask_hud = next(row for row in masked['entities'] if 'forge.ui_document' in row['components'])
mask_doc = by_source['Assets/UI/Masks/Lab.rml']
assert mask_hud['components']['forge.ui_document']['document'] == mask_doc['id']
assert mask_hud['components']['forge.ui_values']['values'] == [dict(name='angle', value=45)]
assert mask_doc['metadata']['forge.ui_source']['runtime_features'] == dict(filters=4, gradients=2)
assert by_source['Assets/UI/BitmapFormats/health.png']['id'] in mask_doc['dependencies']
assert any(edge['target'] == by_source['Assets/UI/BitmapFormats/health.png']['id']
           and edge['kind'] == 'runtime' and edge['role'] == 'declared:ui-mask-image'
           for edge in mask_doc['dependency_edges'])

assert not {row['id'] for row in masked['entities']} & {row['id'] for row in curved['entities']}

luminance = json.loads((project_root / 'Scenes/171-ui-luminance-masks.scene.json').read_text())
assert luminance['asset_id'] == by_source['Scenes/171-ui-luminance-masks.scene.json']['id']
luminance_hud = next(row for row in luminance['entities'] if 'forge.ui_document' in row['components'])
luminance_doc = by_source['Assets/UI/LuminanceMasks/Lab.rml']
assert luminance_hud['components']['forge.ui_document']['document'] == luminance_doc['id']
assert luminance_hud['components']['forge.ui_values']['values'] == [dict(name='angle', value=45)]
assert luminance_doc['metadata']['forge.ui_source']['runtime_features'] == dict(filters=6, gradients=2)
assert any(edge['target'] == by_source['Assets/UI/BitmapFormats/health.png']['id']
           and edge['kind'] == 'runtime' and edge['role'] == 'declared:ui-mask-image'
           for edge in luminance_doc['dependency_edges'])
assert not {row['id'] for row in luminance['entities']} & {row['id'] for row in masked['entities']}


shadow_motion = json.loads((project_root / 'Scenes/173-ui-shadow-motion.scene.json').read_text())
assert shadow_motion['asset_id'] == by_source['Scenes/173-ui-shadow-motion.scene.json']['id']
shadow_motion_hud = next(row for row in shadow_motion['entities'] if 'forge.ui_document' in row['components'])
shadow_motion_doc = by_source['Assets/UI/ShadowMotion/Lab.rml']
assert shadow_motion_hud['components']['forge.ui_document']['document'] == shadow_motion_doc['id']
assert shadow_motion_hud['components']['forge.ui_values']['values'] == [dict(name='blur', value=4)]
assert shadow_motion_doc['metadata']['forge.ui_source']['runtime_features'] == dict(filters=7)
assert not {row['id'] for row in shadow_motion['entities']} & {row['id'] for row in luminance['entities']}





surface = json.loads((project_root / 'Scenes/174-interactive-ui-surfaces.scene.json').read_text())
assert surface['asset_id'] == by_source['Scenes/174-interactive-ui-surfaces.scene.json']['id']
surface_mesh, = [row for row in surface['entities'] if 'forge.ui_surface_input' in row['components']]
output = surface_mesh['components']['forge.ui_surface_input']['render_texture']
assert surface_mesh['components']['forge.ui_surface_input']['enabled']
assert surface_mesh['components']['forge.mesh_renderer']['layers'] == 4
surface_ui = [row['components']['forge.ui_document'] for row in surface['entities']
              if 'forge.ui_document' in row['components']]
assert any(row['document'] == by_source['Assets/UI/SceneSurface/Panel.rml']['id']
           and row['render_texture'] == output for row in surface_ui)
assert any(row['document'] == by_source['Assets/UI/SceneSurface/Guide.rml']['id']
           and row['render_texture'] is None for row in surface_ui)
assert not {row['id'] for row in surface['entities']} & {row['id'] for row in shadow_motion['entities']}


runtime_reflections = json.loads((project_root / 'Scenes/176-runtime-reflection-capture.scene.json').read_text())
assert runtime_reflections['asset_id'] == by_source['Scenes/176-runtime-reflection-capture.scene.json']['id']
runtime_probe, = [row['components']['forge.reflection_probe'] for row in runtime_reflections['entities']
                 if 'forge.reflection_probe' in row['components']]
assert runtime_probe['runtime_capture'] and runtime_probe['capture_size'] == 64
assert runtime_probe['capture_layers'] == 1 and runtime_probe['capture_interval'] == .25
runtime_sources = [row['components'] for row in runtime_reflections['entities'] if 'forge.transform_animator' in row['components']]
assert len(runtime_sources) == 2 and all(row['forge.mesh_renderer']['layers'] == 1 for row in runtime_sources)
assert all(row['forge.transform_animator']['clip'] == by_source['Assets/RenderTextures/Spin.animation.json']['id'] for row in runtime_sources)
saved_capture = json.loads((project_root / 'Scenes/110-reflection-map-capture.scene.json').read_text())
assert not {row['id'] for row in runtime_reflections['entities']} & {row['id'] for row in saved_capture['entities']}

fbx = json.loads((project_root / 'Scenes/175-fbx-model-import.scene.json').read_text())
assert fbx['asset_id'] == by_source['Scenes/175-fbx-model-import.scene.json']['id']
fbx_models = {row['source']: row for row in assets if row['type'] == 'model'
              and row['source'].startswith('Assets/FBX/')}
assert set(fbx_models) == {'Assets/FBX/Static.fbx', 'Assets/FBX/Moving.fbx', 'Assets/FBX/Skin.fbx'}
assert all(row['metadata']['forge.import']['importer'] == 'forge.model.fbx'
           for row in fbx_models.values())
fbx_roots = [row['components']['forge.model_source'] for row in fbx['entities']
             if 'forge.model_source' in row['components']
             and row['components']['forge.model_source']['node'] is None]
assert len(fbx_roots) == 3 and {row['model'] for row in fbx_roots} == {row['id'] for row in fbx_models.values()}
assert len([row for row in fbx['entities'] if 'forge.animator' in row['components']]) == 2
assert (project_root / 'Assets/FBX/Checker.png').is_file()
assert not {row['id'] for row in fbx['entities']} & {row['id'] for row in surface['entities']}

scenes = sorted((project_root / 'Scenes').glob('*.scene.json'))
assert {'71-world-sprites.scene.json', '72-sprite-frame-animation.scene.json',
        '73-sprite-image-fill.scene.json', '74-tile-maps.scene.json',
        '75-2d-physics.scene.json', '76-connected-2d-bodies.scene.json',
        '77-painted-2d-collision.scene.json',
        '78-sprite-image-regions.scene.json',
        '79-one-way-platforms.scene.json', '80-xy-navigation.scene.json',
        '81-moving-tile-maps.scene.json', '82-tile-map-triggers.scene.json',
        '83-audio-source-codecs.scene.json', '84-independent-audio.scene.json', '85-audio-filters.scene.json', '86-one-way-tile-maps.scene.json', '87-sprite-frame-regions.scene.json', '90-audio-echo.scene.json', '91-single-touch-gestures.scene.json', '92-multiple-gamepads.scene.json', '93-gamepad-motion.scene.json', '94-sprite-atlas-packing.scene.json', '95-sprite-frame-metadata.scene.json'} <= {p.name for p in scenes}
player_scene = json.loads((project_root / 'Scenes/92-multiple-gamepads.scene.json').read_text())
assert player_scene['asset_id'] == by_source['Scenes/92-multiple-gamepads.scene.json']['id']
players = [row for row in player_scene['entities'] if row['name'].startswith('Player ')]
assert len(players) == 4 and len({tuple(row['components']['forge.local_translation'].values()) for row in players}) == 4
player_actions = [a for a in project['input']['actions']
                  if a['name'] in {f'Player {slot} move' for slot in range(1, 5)}]
assert len(player_actions) == 4
for slot,action in enumerate(player_actions,start=1):
    assert action['kind'] == 'axis2'
    assert {b['control'] for b in action['bindings']} == {f'pad.{slot}.left_x', f'pad.{slot}.left_y'}
    assert all(b['radial'] and b['deadzone'] == .2 for b in action['bindings'])
assert (project_root / 'CodeExamples/MultipleGamepads.cpp').is_file()
controller_scene = json.loads((project_root / 'Scenes/149-sixteen-controllers.scene.json').read_text())
assert controller_scene['asset_id'] == by_source['Scenes/149-sixteen-controllers.scene.json']['id']
assert len([row for row in controller_scene['entities'] if 'forge.sprite_renderer' in row['components']]) == 16
controller_actions = [a for a in project['input']['actions'] if a['name'].startswith('Player ')]
assert len(controller_actions) == 16
for slot in range(1, 17):
    action, = [row for row in controller_actions if row['name'] == f'Player {slot} move']
    assert action['kind'] == 'axis2'
    assert {row['control'] for row in action['bindings']} == {f'pad.{slot}.left_x', f'pad.{slot}.left_y'}
    assert all(row['radial'] and row['deadzone'] == .2 for row in action['bindings'])
tile_scene = json.loads((project_root / 'Scenes/74-tile-maps.scene.json').read_text(encoding='utf-8'))
maps = [row['components']['forge.tile_map_renderer'] for row in tile_scene['entities']
        if 'forge.tile_map_renderer' in row['components']]
assert len(maps) == 2 and {row['map'] for row in maps} == {
    by_source['Assets/Tiles/ArrowCourse.tilemap.json']['id']}
animated_scene = json.loads((project_root / 'Scenes/121-animated-tiles.scene.json').read_text(encoding='utf-8'))
animated_maps = [e['components']['forge.tile_map_renderer'] for e in animated_scene['entities']
                 if 'forge.tile_map_renderer' in e['components']]
assert len(animated_maps) == 1 and animated_maps[0]['map'] == by_source[
    'Assets/Tiles/AnimatedDirections.tilemap.json']['id']
animated_set = json.loads((project_root / 'Assets/Tiles/AnimatedDirections.tileset.json').read_text())
assert animated_set['version'] == 4 and len(animated_set['tiles']) == 4
assert [f['frame'] for f in animated_set['tiles'][0]['animation']['frames']] == [0, 1, 2, 3]
assert [f['frame'] for f in animated_set['tiles'][1]['animation']['frames']] == [3, 2, 1, 0]
assert [f['seconds'] for f in animated_set['tiles'][2]['animation']['frames']] == [.125, .25, .125, .5]
assert 'animation' not in animated_set['tiles'][3]
neighbor_scene = json.loads((project_root / 'Scenes/122-neighbor-tiles.scene.json').read_text())
neighbor_maps = [e['components']['forge.tile_map_renderer'] for e in neighbor_scene['entities']
                 if 'forge.tile_map_renderer' in e['components']]
assert len(neighbor_maps) == 1 and neighbor_maps[0]['map'] == by_source[
    'Assets/Tiles/NeighborDirections.tilemap.json']['id']
neighbor_set = json.loads((project_root / 'Assets/Tiles/NeighborDirections.tileset.json').read_text())
assert neighbor_set['version'] == 5 and len(neighbor_set['tiles']) == 5
assert neighbor_set['tiles'][0]['autotile'] == dict(enabled=True, rules=[
    dict(mask=1, same=0, tile=3), dict(mask=4, same=0, tile=4),
    dict(mask=16, same=0, tile=5), dict(mask=64, same=0, tile=6)])
assert len(neighbor_set['tiles'][0]['animation']['frames']) == 4
assert not any('autotile' in row for row in neighbor_set['tiles'][1:])
contour_scene = json.loads((project_root / 'Scenes/123-tile-collision-contours.scene.json').read_text())
contour_maps = [e['components']['forge.tile_map_renderer'] for e in contour_scene['entities']
                if 'forge.tile_map_renderer' in e['components']]
assert len(contour_maps) == 1 and contour_maps[0]['map'] == by_source[
    'Assets/Tiles/ContourTiles.tilemap.json']['id']
contour_set = json.loads((project_root / 'Assets/Tiles/ContourTiles.tileset.json').read_text())
assert contour_set['version'] == 6 and len(contour_set['tiles']) == 3
ring, bars, floor = contour_set['tiles']
assert ring['collision']['kind'] == bars['collision']['kind'] == 'contours'
assert len(ring['collision']['contours']) == len(bars['collision']['contours']) == 2
assert len(ring['collision']['contours'][0]['holes']) == 1
assert not ring['collision']['contours'][1]['holes']
assert all(not row['holes'] for row in bars['collision']['contours'])
assert 'collision' not in floor
dynamic_scene = json.loads((project_root / 'Scenes/124-dynamic-tile-map-bodies.scene.json').read_text())
dynamic_maps = [row for row in dynamic_scene['entities']
                if 'forge.tile_map_collider_2d' in row['components']]
assert len(dynamic_maps) == 3
assert all(row['spatial']['mode'] == 'world' and
           row['components']['forge.physics_body_2d']['motion'] == 2 and
           not row['components']['forge.physics_body_2d']['one_way'] and
           row['components']['forge.tile_map_renderer']['map'] ==
               by_source['Assets/Tiles/DynamicTiles.tilemap.json']['id'] for row in dynamic_maps)
assert sum(row['components']['forge.physics_body_2d']['lock_rotation'] for row in dynamic_maps) == 1
assert sorted(row['components']['forge.physics_body_2d']['mass'] for row in dynamic_maps) == [0, 0, 12]
assert sum(row['components']['forge.local_scale']['x'] < 0 for row in dynamic_maps) == 1
one_way_scene = json.loads((project_root / 'Scenes/125-custom-polygon-one-way.scene.json').read_text())
one_way_maps = [row for row in one_way_scene['entities']
                if 'forge.tile_map_collider_2d' in row['components']]
assert len(one_way_maps) == 1
one_way_components = one_way_maps[0]['components']
assert one_way_components['forge.tile_map_renderer']['map'] == by_source['Assets/Tiles/ContourTiles.tilemap.json']['id']
assert one_way_components['forge.physics_body_2d']['one_way'] is True
assert one_way_components['forge.physics_body_2d']['motion'] == 0
assert one_way_components['forge.physics_body_2d']['sensor'] is False
riser = next(row for row in one_way_scene['entities'] if row['name'] == 'Violet - upward passage through the ring')
assert riser['components']['forge.physics_body_2d']['motion'] == 2
assert riser['components']['forge.physics_body_2d']['gravity_factor'] == -1
panel_scene = json.loads((project_root / 'Scenes/73-sprite-image-fill.scene.json').read_text(encoding='utf-8'))
panels = [row['components']['forge.sprite_renderer'] for row in panel_scene['entities']
          if 'forge.sprite_renderer' in row['components']]
assert sorted(row['image_mode'] for row in panels) == [0, 1, 1, 2, 2]
assert {row['material'] for row in panels} == {by_source['Assets/Sprites/Panel.material.json']['id']}
assert any(row['image_mode'] == 2 and row['flip_x'] and row['flip_y'] for row in panels)
assert any(row['image_mode'] == 1 and row['width'] < 1 and row['height'] < 1 for row in panels)
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
        '63-portable-prefab-rigs.scene.json',
        '64-runtime-prefab-creation.scene.json',
        '142-prefab-variants.scene.json',
        '143-fast-approximate-antialiasing.scene.json',
        '144-foliage-placement.scene.json',
        '65-two-dimensional-blend-spaces.scene.json',
        '66-object-material-values.scene.json',
        '67-animated-material-values.scene.json',
        '68-sequence-camera-shots.scene.json',
        '69-scene-color-adjustments.scene.json',
        '96-automatic-exposure.scene.json',
        '97-packed-sprite-fills.scene.json',
        '98-distance-fog.scene.json',
        '99-sprite-frame-filtering.scene.json',
        '100-ui-animation.scene.json',
        '101-tile-collision-profiles.scene.json',
        '102-temporal-antialiasing.scene.json',
        '103-depth-of-field.scene.json',
        '104-motion-blur.scene.json',
        '105-ambient-occlusion.scene.json',
        '106-screen-space-reflections.scene.json',
        '107-local-reflection-probes.scene.json',
        '108-material-normal-occlusion.scene.json',
        '109-rough-reflection-filtering.scene.json',
        '110-reflection-map-capture.scene.json',
        '111-contact-hardening-shadows.scene.json',
        '112-area-lights.scene.json',
        '114-weighted-transparency.scene.json',
        '70-sequence-audio-cues.scene.json',
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
        if re.search(r'\.components\.forge\.sprite_renderer\.frame_regions\[\d+\]$', location):
            # Legacy source rectangles omit the additive frame metadata. Keep
            # every explicit value/extension exact while checking native defaults.
            expected = dict(rotated_clockwise=False, source_width=0, source_height=0,
                            trim_x=0, trim_y=0) | expected
        if location.endswith('.components.forge.ui_document'):
            # Optional output routing admits old main-window sources as null.
            expected = dict(render_texture=None) | expected
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
        assert source['version'] in (3, 4, 5), path.name
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
        if any('prefab_instance' in row for row in source['entities']):
            # The memory-only authoring API intentionally owns no project sources.
            # Use the native project/content owner to realize structured prefabs.
            assert source['version'] in (4, 5) and len(sys.argv) == 4
            before = path.read_bytes()
            admission = subprocess.run(
                [sys.argv[3], '--admit', str(project_root), str(path)],
                capture_output=True, text=True)
            assert admission.returncode == 0, (
                path.name, admission.returncode, admission.stdout, admission.stderr)
            assert path.read_bytes() == before
            continue
        result = call('scene.replace', target=context['target'],
                      expected_revision=context['revision'], document=source)
        context = call('discover')['result']
        loaded = call('scene.read', target=context['target'])['result']
        # Additive builtin settings: old source bytes remain unchanged while
        # admitted typed values explicitly expose their documented defaults.
        expected = json.loads(json.dumps(source))
        for row in expected['entities']:
            message = row['components'].get('forge.input_message')
            if message is not None:
                message.setdefault('trigger', 0)
            camera = row['components'].get('forge.camera')
            if camera is not None:
                camera.setdefault('render_texture', None)
            shot = row['components'].get('forge.camera_shot')
            if shot is not None:
                shot.setdefault('blend_in_seconds', 0.)
            light = row['components'].get('forge.light')
            if light is not None:
                light.setdefault('shadow_source_radius', 0.)
                light.setdefault('shadow_angular_radius', 0.)
                light.setdefault('emitter_radius', .5)
                light.setdefault('emitter_width', 1.)
                light.setdefault('emitter_height', 1.)
                light.setdefault('texture', None)
                light.setdefault('image_width', 1.)
                light.setdefault('image_height', 1.)
                light.setdefault('image_tiled', False)
            probe = row['components'].get('forge.reflection_probe')
            if probe is not None:
                probe.setdefault('diffuse_intensity', 0.)
                probe.setdefault('runtime_capture', False)
                probe.setdefault('capture_size', 128)
                probe.setdefault('capture_interval', .25)
                probe.setdefault('capture_near', .1)
                probe.setdefault('capture_far', 100.)
                probe.setdefault('capture_layers', 4294967295)
            decal = row['components'].get('forge.decal')
            if decal is not None:
                decal.setdefault('color_influence', 1.)
                decal.setdefault('normal_influence', 0.)
                decal.setdefault('roughness_influence', 0.)
            renderer = row['components'].get('forge.mesh_renderer')
            if renderer is not None:
                renderer.setdefault('material_parameters', [])
                renderer.setdefault('wind', dict(enabled=False, amplitude=.2, height=1.6,
                                                 direction_x=1., direction_z=0., frequency=.5,
                                                 wavelength=8., phase=0.))
            region = row['components'].get('forge.navigation_region_2d')
            if region is not None:
                region.setdefault('points', [])
                region.setdefault('source', 0)
            avoidance = row['components'].get('forge.navigation_avoidance')
            if avoidance is not None:
                avoidance.setdefault('shared_group', 0)
            sprite = row['components'].get('forge.sprite_renderer')
            if sprite is not None:
                sprite.setdefault('mip_filter', 0)
                sprite.setdefault('texture_frames', False)
                sprite.setdefault('image_mode', 0)
                sprite.setdefault('pixels_per_meter', 100.)
                for field in ('border_left', 'border_right', 'border_top', 'border_bottom'):
                    sprite.setdefault(field, 0)
                sprite.setdefault('region_enabled', False)
                sprite.setdefault('frame_regions', [])
                for field in ('region_x', 'region_y', 'region_width', 'region_height'):
                    sprite.setdefault(field, 0)
            tile_map = row['components'].get('forge.tile_map_renderer')
            if tile_map is not None:
                tile_map.setdefault('mip_filter', 0)
            transform = row['components'].get('forge.transform_animator')
            if transform is not None:
                transform.setdefault('sequence', None)
                transform.setdefault('sequence_start_seconds', 0.)
                transform.setdefault('sequence_next_clip', None)
                transform.setdefault('sequence_next_start_seconds', 0.)
            sequence = row['components'].get('forge.animation_sequence')
            if sequence is not None:
                sequence.setdefault('sequence', None)
                sequence.setdefault('sequence_start_seconds', 0.)
            avoidance = row['components'].get('forge.navigation_avoidance')
            if avoidance is not None:
                avoidance.setdefault('shared_group', 0)
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
                animator.setdefault('blend_position_y', 0.)
            sound = row['components'].get('forge.audio_source')
            if sound is not None:
                sound.setdefault('cone_enabled', False)
                sound.setdefault('inner_angle', 60.)
                sound.setdefault('outer_angle', 120.)
                sound.setdefault('outer_gain', .25)
                sound.setdefault('bus', 0)
                sound.setdefault('doppler', 0.)
                sound.setdefault('pause_with_game', True)
                sound.setdefault('low_pass_enabled', False)
                sound.setdefault('high_pass_enabled', False)
                sound.setdefault('low_pass_hz', 20000.)
                sound.setdefault('high_pass_hz', 20.)
                sound.setdefault('echo_enabled', False)
                sound.setdefault('echo_seconds', .25)
                sound.setdefault('echo_feedback', .35)
                sound.setdefault('reverb_enabled', False)
                sound.setdefault('reverb_decay', 1.5)
                sound.setdefault('reverb_damping', .5)
                sound.setdefault('reverb_wet', .25)
            body = row['components'].get('forge.physics_body')
            if body is not None:
                body.setdefault('continuous_collision', False)
                body.setdefault('linear_damping', .05)
                body.setdefault('angular_damping', .05)
                body.setdefault('max_linear_velocity', 500.)
                body.setdefault('max_angular_velocity', 2700.)
                body.setdefault('allow_sleeping', True)
            planar_body = row['components'].get('forge.physics_body_2d')
            if planar_body is not None:
                planar_body.setdefault('one_way', False)
                planar_body.setdefault('one_way_margin', .02)
            controller_2d = row['components'].get('forge.character_controller_2d')
            if controller_2d is not None:
                controller_2d.setdefault('platform_carry', False)
                controller_2d.setdefault('crouch_height', 0.)
                controller_2d.setdefault('floor_stop_on_slope', False)
                controller_2d.setdefault('slide_on_ceiling', True)
                controller_2d.setdefault('step_height', 0.)
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
        if path.name.startswith('83-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities']
                       if 'forge.audio_source' in row['components']]
            assets = [by_source['Assets/AudioCodecs/' + name]
                      for name in ('StereoReference.wav', 'Stereo.flac', 'Stereo.mp3')]
            assert len(sources) == 3 and sum(item['play_on_start'] for item in sources) == 1
            assert {item['clip'] for item in sources} == {item['id'] for item in assets}
            assert {item['metadata']['forge.import']['importer'] for item in assets} == {
                'forge.audio.wav', 'forge.audio.flac', 'forge.audio.mp3'}
            assert all(item['metadata']['forge.audio']['frames'] == 12000 and
                       item['metadata']['forge.audio']['channels'] == 2 for item in assets)
            assert all(not item['spatialized'] and item['loop'] for item in sources)
        if path.name.startswith('84-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities']
                       if 'forge.audio_source' in row['components']]
            assert len(sources) == 3 and sum(item['play_on_start'] for item in sources) == 2
            assert sum(item['pause_with_game'] for item in sources) == 1
            assert all(item['clip'] == by_source['Assets/FalloffTone.wav']['id'] for item in sources)
            assert not next(item for item in sources if not item['play_on_start'])['pause_with_game']
        if path.name.startswith('85-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities']
                       if 'forge.audio_source' in row['components']]
            assert len(sources) == 3 and sum(item['play_on_start'] for item in sources) == 1
            assert sum(item['low_pass_enabled'] for item in sources) == 1
            assert sum(item['high_pass_enabled'] for item in sources) == 1
            assert next(item for item in sources if item['low_pass_enabled'])['low_pass_hz'] == 100
            assert next(item for item in sources if item['high_pass_enabled'])['high_pass_hz'] == 1000
        if path.name.startswith('90-'):
            sources = [row['components']['forge.audio_source'] for row in loaded['entities']
                       if 'forge.audio_source' in row['components']]
            assert len(sources) == 3 and sum(item['echo_enabled'] for item in sources) == 2
            assert sum(item['play_on_start'] for item in sources) == 1
            assert not any(item['loop'] for item in sources)
            assert {item['echo_seconds'] for item in sources if item['echo_enabled']} == {.25, .5}
            assert all(item['clip'] == by_source['Assets/ProcessingMono.wav']['id'] for item in sources)
        if path.name.startswith('91-'):
            assert len(loaded['entities']) == 2
            assert [a['bindings'][0]['control'] for a in project['input']['actions']
                    if a['name'].startswith('Single touch ')] == [
                'touch.gesture.'+f for f in ('tap','double_tap','long_press','swipe_left','swipe_right','swipe_up','swipe_down')]
        if path.name.startswith('89-'):
            assert len(loaded['entities']) == 2
            assert [a['bindings'][0]['control'] for a in project['input']['actions']
                    if a['name'].startswith('Touch gesture ')] == [
                'touch.gesture.'+f for f in ('active','pan_x','pan_y','pinch','rotation')]
        if path.name.startswith('88-'):
            assert len(loaded['entities']) == 2
            assert any('forge.sprite_renderer' in row['components'] for row in loaded['entities'])
            assert [a['bindings'][0]['control'] for a in project['input']['actions'] if a['name'].startswith('Touch0 ')] == [
                'touch.0.'+field for field in ('down', 'x', 'y', 'pressure')]
        if path.name.startswith('87-'):
            sprites = [row['components']['forge.sprite_renderer'] for row in loaded['entities']
                       if 'forge.sprite_renderer' in row['components']]
            assert len(sprites) == 6 and sum(bool(row['frame_regions']) for row in sprites) == 5
            banks = [row for row in sprites if row['frame_regions']]
            assert all(row['frame'] < len(row['frame_regions']) for row in banks)
            assert any(len({(r['width'], r['height']) for r in row['frame_regions']}) == 4 for row in banks)
        if path.name.startswith('86-'):
            platforms = [row['components']['forge.physics_body_2d'] for row in loaded['entities']
                         if 'forge.tile_map_collider_2d' in row['components']]
            assert len(platforms) == 1 and platforms[0]['one_way']
            assert platforms[0]['motion'] == 0 and not platforms[0]['sensor']
            rising = next(row for row in loaded['entities'] if row['name'].startswith('Cyan'))
            assert rising['components']['forge.physics_body_2d']['gravity_factor'] == -1
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
