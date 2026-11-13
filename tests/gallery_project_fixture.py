"""Shared private Gallery project staging for native authoring workflows."""
import json
from pathlib import Path
import shutil


def json_strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for child in value:
            yield from json_strings(child)
    elif isinstance(value, dict):
        for child in value.values():
            yield from json_strings(child)


def copy_gallery_project(gallery, output, sources, startup=None):
    """Copy original Gallery identities, sources and their bounded dependency closure."""
    gallery, output = Path(gallery), Path(output)
    output.mkdir()
    catalog = json.loads((gallery/'forge.assets.json').read_text(encoding='utf-8'))
    records = {row['id']: row for row in catalog['assets']}
    pending = [row['id'] for row in records.values() if row['source'] in sources]
    missing = set(sources) - {records[asset]['source'] for asset in pending}
    for source in missing:
        path = Path(source)
        assert not path.is_absolute() and '..' not in path.parts and (gallery/path).is_file(), source
        # Fixtures can also need unregistered raw UI resources. Only Scenes carry IDs.
        if source.startswith('Scenes/') and source.endswith('.scene.json'):
            scene = json.loads((gallery/source).read_text(encoding='utf-8'))
            pending.extend(value for value in json_strings(scene) if value in records)
    selected = set()
    while pending:
        asset = pending.pop()
        if asset in selected or asset not in records:
            continue
        selected.add(asset)
        record = records[asset]
        pending.extend(record['dependencies'])
        if record['type'] == 'scene':
            # Older Scene records predate catalog dependency publication. Use a
            # conservative ID match to retain their authored resources as well.
            pending.extend(value for value in json_strings(json.loads(
                (gallery/record['source']).read_text(encoding='utf-8'))) if value in records)
        if records[asset].get('subasset'):
            pending.append(records[asset]['subasset']['owner'])
    catalog['assets'] = [row for row in catalog['assets'] if row['id'] in selected]
    settings = json.loads((gallery/'forge.project.json').read_text(encoding='utf-8'))
    if startup is not None:
        assert startup in sources and startup.endswith('.scene.json')
        scene = json.loads((gallery/startup).read_text(encoding='utf-8'))
        settings['startup_scene'] = dict(asset=scene['asset_id'], source=startup)
    paths = set(sources) | {settings['startup_scene']['source']}
    for row in catalog['assets']:
        paths.add(row['source'])
        paths.update(edge['source'] for edge in row.get('source_dependencies', []))
        sidecar = row['source']+'.forge-import.json'
        if (gallery/sidecar).exists():
            paths.add(sidecar)
        imported = row.get('metadata', {}).get('forge.import')
        if imported:
            cache = Path('.forge/cache/derived')/imported['key']
            shutil.copytree(gallery/cache, output/cache, dirs_exist_ok=True)
    for relative in paths:
        target = output/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(gallery/relative, target)
    (output/'forge.assets.json').write_text(json.dumps(catalog), encoding='utf-8')
    (output/'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
    return output



# Each native capture keeps the original files it exercises, including legacy
# Scene references, without importing unrelated Gallery examples before capture.
CAPTURE_PROJECTS = {
    'audio_preview': (
        (
            'Assets/FalloffTone.wav',
            'Assets/ProcessingMono.wav',
        ), None),
    'audio_mixer': (
        (
            'Scenes/33-audio-mixer.scene.json',
            'Scenes/34-audio-import-processing.scene.json',
            'Scenes/35-moving-audio.scene.json',
        ), 'Scenes/33-audio-mixer.scene.json'),
    'audio_cue': (
        (
            'Scenes/70-sequence-audio-cues.scene.json',
        ), 'Scenes/70-sequence-audio-cues.scene.json'),
    'sequence_frame': (
        (
            'Scenes/70-sequence-audio-cues.scene.json',
        ), 'Scenes/70-sequence-audio-cues.scene.json'),
    'sprite_frame': (
        (
            'Scenes/72-sprite-frame-animation.scene.json',
        ), 'Scenes/72-sprite-frame-animation.scene.json'),
    'view_image': (
        (
            'Scenes/70-sequence-audio-cues.scene.json',
            'Scenes/59-ui-png-jpeg.scene.json',
            'Assets/UI/BitmapFormats/health.png',
            'Assets/UI/BitmapFormats/landscape.jpg',
        ), 'Scenes/70-sequence-audio-cues.scene.json'),
    'animation_root_motion': (
        (
            'Scenes/36-animation-root-motion.scene.json',
            'Scenes/37-animation-subtree-blending.scene.json',
            'Scenes/38-animation-additive-overlays.scene.json',
            'Scenes/40-two-bone-ik.scene.json',
            'Scenes/41-joint-aim.scene.json',
            'Scenes/42-animation-bone-masks.scene.json',
            'Scenes/43-animation-blend-spaces.scene.json',
            'Scenes/30-animation-exits.scene.json',
            'Assets/Weighted.mask.json',
            'Assets/Motion.blendspace.json',
            'Assets/TimedLocomotion.states.json',
        ), 'Scenes/36-animation-root-motion.scene.json'),
}


def copy_capture_project(gallery, output, workflow):
    sources, startup = CAPTURE_PROJECTS[workflow]
    return copy_gallery_project(gallery, output, sources, startup)
