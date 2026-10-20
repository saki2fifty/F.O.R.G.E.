"""Export Gallery73 through the real shared kit and launch its relocated game."""
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
    receipt = verify_scene(tools, gallery, kit, evidence, scene_source, retain)
    receipt['modes'] = sorted(modes)
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
    print('Shared Sprite image-fill Gallery export and two source-free production startups passed')
