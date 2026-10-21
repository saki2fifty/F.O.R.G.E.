"""Export Gallery75/76 through the production shared kit and launch relocated games."""
import argparse
import json
from pathlib import Path
from gallery_standalone_export import verify_scene


def verify(tools, gallery, kit, evidence, retain=None, connected=False):
    source = 'Scenes/76-connected-2d-bodies.scene.json' if connected else 'Scenes/75-2d-physics.scene.json'
    receipt = verify_scene(tools, gallery, kit, evidence, source, retain)
    (Path(evidence) / 'physics-2d-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tools', 'gallery', 'kit', 'evidence'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--retain', type=Path)
    parser.add_argument('--connected', action='store_true', help='Export the connected-body Gallery76')
    args = parser.parse_args()
    verify(args.tools, args.gallery, args.kit, args.evidence, args.retain, args.connected)
    print('Shared 2D physics Gallery export and two source-free production startups passed')
