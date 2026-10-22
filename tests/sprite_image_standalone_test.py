"""Export Sprite fill and region Galleries, then launch their relocated games."""
import argparse
import json
from pathlib import Path
from gallery_standalone_export import verify_scene


def verify(tools, gallery, kit, evidence, retain=None):
    scene_source = 'Scenes/73-sprite-image-fill.scene.json'
    scene = json.loads((Path(gallery) / scene_source).read_text(encoding='utf-8'))
    modes = {row['components']['forge.sprite_renderer']['image_mode']
             for row in scene['entities'] if 'forge.sprite_renderer' in row['components']}
    assert modes == {0, 1, 2}, modes
    receipt = verify_scene(tools, gallery, kit, Path(evidence) / 'fill', scene_source)
    receipt['modes'] = sorted(modes)
    region_source = 'Scenes/78-sprite-image-regions.scene.json'
    region_scene = json.loads((Path(gallery) / region_source).read_text(encoding='utf-8'))
    sprites = [row['components']['forge.sprite_renderer'] for row in region_scene['entities']
               if 'forge.sprite_renderer' in row['components']]
    assert len(sprites) == 6 and sum(row['region_enabled'] for row in sprites) == 5, sprites
    # Retain this newer consumer for the existing fresh-runner check. Both
    # scenes still perform two production startups after source/kit deletion.
    receipt['regions'] = verify_scene(tools, gallery, kit, Path(evidence) / 'regions', region_source, retain)
    (Path(evidence) / 'sprite-image-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tools', 'gallery', 'kit', 'evidence'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--retain', type=Path)
    args = parser.parse_args()
    verify(args.tools, args.gallery, args.kit, args.evidence, args.retain)
    print('Shared Sprite fill/region exports and four source-free production startups passed')
