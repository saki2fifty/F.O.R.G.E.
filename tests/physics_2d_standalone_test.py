"""Export Gallery75 through the production shared kit and launch its relocated game."""
import argparse
import json
from pathlib import Path
from gallery_standalone_export import verify_scene


def verify(tools, gallery, kit, evidence, retain=None):
    receipt = verify_scene(tools, gallery, kit, evidence, 'Scenes/75-2d-physics.scene.json', retain)
    (Path(evidence) / 'physics-2d-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tools', 'gallery', 'kit', 'evidence'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--retain', type=Path)
    args = parser.parse_args()
    verify(args.tools, args.gallery, args.kit, args.evidence, args.retain)
    print('Shared 2D physics Gallery export and two source-free production startups passed')
